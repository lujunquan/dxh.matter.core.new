# 代码架构与开发指南

本文面向本仓库的日常开发：代码怎么分层、运行时怎么流转、改一个功能要动哪些文件。
构建与运行命令见 [BUILD.md](BUILD.md)，通讯适配器接入见
[ADAPTER_GUIDE.md](ADAPTER_GUIDE.md)，本文只讲代码本身。

> 阅读顺序建议：第 1–4 节建立整体印象 → 第 5–7 节看实现 → 第 8 节按任务查配方
> → 第 9 节动手前过一遍约束清单。

---

## 1. 工程目标与当前状态

把 DUWI 生态里的设备，以 Matter **桥接设备**（bridged device）的形式暴露给
Apple Home / Google Home / Alexa / Home Assistant 等控制器。每个物理设备对应
一个**动态 endpoint**，挂在 Aggregator endpoint 1 下。

| 能力 | 状态 |
|---|---|
| Matter 主流程（启动、配网、动态 endpoint） | 已完成 |
| Bridged Device Basic Information（BDBI） | 已完成（code-driven cluster，运行时注册） |
| On/Off Light、Temperature Sensor 动态 endpoint | 已完成 |
| `IDeviceAdapter` 稳定接缝 | 已完成 |
| Mock 适配器 + 控制台模拟 | 已完成 |
| 真实 DUWI 通讯适配器 | **待接入** |
| 设备增量发现（运行中上线/下线） | **未实现**（只有启动时 `Discover()`） |
| Actions / Rooms / Composed Device | 未启用 |
| 生产 VID/PID、DAC 证书 | 待替换（当前 `0xFFF1` / `0x8001`） |
| 多适配器并存 | 未支持（设备键是 `physicalId`） |

**定位**：out-of-tree。`third_party/connectedhomeip` 是只读 git submodule；
本工程拥有构建入口、数据模型、产品配置和桥接逻辑。

---

## 2. 代码地图

```
src/
├── model/
│   └── DeviceTypes.h             领域模型，SDK 无关
├── adapters/
│   ├── IDeviceAdapter.h          通讯适配器接口（稳定接缝）
│   └── MockDeviceAdapter.h/.cpp  内存假设备 + 控制台模拟钩子
├── bridge/                       Matter 语义层（依赖 SDK）
│   ├── StableId.h/.cpp           确定性 UniqueID（纯 C++）
│   ├── BridgedDevice.h/.cpp      单设备状态 + BDBI cluster 生命周期
│   ├── BridgeEndpointManager.*   动态 endpoint 分配 / 查找 / 注销
│   └── BridgedAttributeAccess.*  ember 外部属性回调 + 写命令转发
└── app/
    ├── main.cpp                  SDK 启动入口
    ├── BridgeApplication.h/.cpp  组合根：适配器 ↔ 桥接层 ↔ 控制台
    └── BUILD.gn                  唯一的可执行目标
tests/test_core.cpp               宿主单测（不含 SDK）
zap/dxh-bridge.zap                数据模型源文件（输入）
zap/dxh-bridge.matter             ZAP 生成的 IDL（输入，必须提交）
config/CHIPProjectAppConfig.h     产品配置（VID/PID、动态 endpoint 数量）
```

分层依赖是单向的：

```
        ┌──────────── src/app ────────────┐
        │ main / BridgeApplication（组合根）│
        └───────┬───────────────┬─────────┘
                │               │
                ▼               ▼
        ┌── src/bridge ──┐   ┌── src/adapters ──┐
        │ endpoint/BDBI  │   │ IDeviceAdapter   │
        │ 属性读写/上报   │   │ Mock / 真实实现   │
        └───────┬────────┘   └────────┬─────────┘
                └────────┬────────────┘
                         ▼
                   ┌─ src/model ─┐
                   │ 领域模型     │
                   └─────────────┘
```

硬性规则：

- `src/model`、`src/adapters`、`bridge/StableId.*` **不得** include
  `<app/...>`、`<platform/...>` 等 connectedhomeip 头文件。
- `src/bridge` 不得 include `adapters/*` 的实现；它只认识 `model::DeviceDescriptor`。
- 只有 `src/app` 同时知道适配器和桥接层。

---

## 3. 运行时总览

### 3.1 启动 / 关闭

```
main()                                      src/app/main.cpp
 ├─ ChipLinuxAppInit(argc, argv)             SDK：平台层、CHIP Server、配网参数
 │    └─ ApplicationInit()                   → BridgeApplication::Init()
 │         ├─ BridgeEndpointManager::Initialize()
 │         │    ├─ 计算 fixed endpoint 数量，动态起点 = lastFixed + 1 (=3)
 │         │    └─ 禁用最后一个 fixed endpoint（ZAP placeholder，endpoint 2）
 │         ├─ SetOnOffWriteHandler(...)
 │         ├─ new MockDeviceAdapter
 │         ├─ adapter->SetStateChangedCallback(...)
 │         ├─ adapter->Start()
 │         ├─ AddDiscoveredDevices()
 │         │    ├─ Discover() 快照
 │         │    ├─ CreateDevice()  → uniqueId + BridgedOnOff/temperature 对象
 │         │    └─ AddDevice()     → 动态 endpoint + 注册 BDBI
 │         └─ StartConsoleThread()（仅 Mock 阶段的开发辅助）
 └─ ChipLinuxAppMainLoop()                   Matter 事件循环（主线程）
      └─ 退出时 ApplicationShutdown()        → BridgeApplication::Shutdown()
           ├─ StopConsoleThread()
           ├─ SetOnOffWriteHandler(nullptr)
           ├─ adapter->Stop()
           ├─ 逆序 RemoveDevice()（先注销 BDBI，再清动态 endpoint）
           └─ 释放 device/adapter
```

`BridgeApplication` 只实现 SDK 要求的 `ApplicationInit()` /
`ApplicationShutdown()` 两个钩子，其余由 SDK 的 Linux app-main 驱动。

### 3.2 两个方向的数据流

**设备 → 控制器（状态上报）**

```
设备/网关（未来真实适配器线程）
   │  StateChangedCallback(DeviceDescriptor)
   ▼
BridgeApplication::OnAdapterStateChanged        // 任意线程
   │  Platform::New 拷贝 descriptor
   │  PlatformMgr().ScheduleWork(ApplyAdapterUpdate)
   ▼
BridgeApplication::ApplyAdapterUpdate           // Matter 线程
   ├─ device->SetReachable(...)                 // BDBI 自行上报
   ├─ OnOff: ApplyOnOff() -> changed callback
   │      -> MatterReportingAttributeChangeCallback(ep, OnOff, onOff)
   └─ Temp:  ApplyMeasuredValue() -> changed callback
          -> TemperatureMeasurement::SetMeasuredValue(ep, value)
   ▼
订阅者收到 ReportData
```

**控制器 → 设备（写命令）**

```
Matter Write(OnOff)
   ▼
emberAfExternalAttributeWriteCallback           // Matter 线程
   │  校验 endpoint→device、cluster、attribute、reachable
   ▼
BridgeApplication::HandleMatterOnOffWrite
   │  adapter->SetOnOff(physicalId, on)
   ▼
返回 InteractionModel::Status
```

关键语义：**写命令不会乐观更新 Matter 侧状态**。`SetOnOff()` 只表示命令被
接受，真正状态由适配器回调回报后写入。Mock 适配器是同步回调，所以看起来立即
生效；真实适配器可以异步。

### 3.3 属性有两套存储模型

这是读代码时最容易困惑的点：

| Cluster | 存储/实现方式 | 读 | 写 | 上报 |
|---|---|---|---|---|
| `OnOff`（动态 endpoint） | ember **external storage** | `emberAfExternalAttributeReadCallback` | `emberAfExternalAttributeWriteCallback` | 业务代码显式调 `MatterReportingAttributeChangeCallback` |
| `TemperatureMeasurement`（动态 endpoint） | SDK **code-driven cluster** | cluster 内部状态 | 无 | `SetMeasuredValue()` 内部触发 |
| `BridgedDeviceBasicInformation` | SDK **code-driven cluster**，运行时注册 | cluster 内部状态 | node-label 走 delegate | cluster 自行上报 |

所以 `BridgeApplication::CreateDevice()` 里两个子类的 changed callback 行为不同：
On/Off 是“通知 IM 有变化”，温度是“把新值写进 cluster”。

---

## 4. 模块详解

### 4.1 `src/model/DeviceTypes.h`

SDK 无关的领域模型，所有跨层数据都用这里的类型。

| 类型 | 说明 |
|---|---|
| `DeviceKind` | `kOnOffLight` / `kTemperatureSensor`；`DeviceKindName()` 返回日志用字符串 |
| `AdapterStatus` | `kOk / kNotSupported / kInvalidArgument / kNotFound / kTransportError` |
| `DeviceState` | `reachable`、`onOff`、`measuredValue / min / max`（**0.01 °C**，与 Matter 一致） |
| `DeviceDescriptor` | `physicalId`、`name`、`location`、`kind`、`state` |

约束：

- `physicalId` 必须跨重启、固件升级、重新配对保持稳定。
- `location` 当前只被保存和打日志，**尚未映射到 Matter**（Actions/Rooms 未启用）。
- 温度单位固定为 0.01 °C，避免每个适配器各自转换。

### 4.2 `src/adapters` — 通讯接缝

```cpp
class IDeviceAdapter {
    using StateChangedCallback = std::function<void(const model::DeviceDescriptor &)>;

    virtual const char * Name() const = 0;      // 参与 UniqueID，发布后不可改
    virtual bool Start();                        // 建链；false 中止启动
    virtual void Stop();
    virtual std::vector<model::DeviceDescriptor> Discover() = 0;
    virtual model::AdapterStatus SetOnOff(const std::string & physicalId, bool on) = 0;
    virtual void SetStateChangedCallback(StateChangedCallback) = 0;
};
```

生命周期：`SetStateChangedCallback` → `Start` → `Discover` → 运行期
`SetOnOff` / 回调 → `Stop`。

线程约定：

- `StateChangedCallback` **可能来自任意线程**，桥接层负责调度到 Matter 线程；
  回调参数按值拷贝，适配器不必保证引用生命周期。
- `SetOnOff` 在 Matter 线程被调用；如果协议客户端不线程安全，适配器内部自行串行化。

Mock 实现（[MockDeviceAdapter.cpp](../src/adapters/MockDeviceAdapter.cpp)）：

- 固定 3 台设备：`duwi-lamp-001`、`duwi-lamp-002`、`duwi-sensor-001`。
- `SetOnOff` 会区分 `kNotFound` / `kNotSupported` / `kTransportError`（离线）。
- 模拟钩子：`SimulateToggleAll()`、`SimulateTemperatureDelta()`、
  `SimulateFirstTemperatureSensorReachability()`，供控制台测试。

### 4.3 `src/bridge/StableId`

`MakeStableUniqueId(adapterName, physicalId)`：

- 两路 64 位 FNV-1a（不同 offset basis、不同拼接顺序）拼成 128 位。
- 输出 **32 位大写十六进制**，同时用于：
  - Descriptor 的 `EndpointUniqueID`（`chip_enable_endpoint_unique_id = true`）
  - BDBI 的 `UniqueID`
- 纯 C++、可单测；**输入一变输出就变**，所以适配器名和 physicalId 都不能改。

### 4.4 `src/bridge/BridgedDevice`

类层次：

```
BridgedDevice                     BDBI delegate + 设备元数据
├── BridgedOnOffDevice            mOn + ApplyOnOff()
└── BridgedTemperatureSensor      mMeasured/Min/Max + ApplyMeasuredValue()
```

基类职责：

| 成员/方法 | 说明 |
|---|---|
| `mName` / `mUniqueId` / `mPhysicalId` / `mLocation` | 设备元数据；name 上限 32B |
| `mEndpointId` / `mParentEndpointId` | 由 `BridgeEndpointManager` 写入 |
| `mBridgedDevice` | `LazyRegisteredServerCluster<BridgedDeviceBasicInformationCluster>` |
| `CreateBridgedDeviceInfo()` | 端点创建后注册 BDBI；注入 `MutableData`（reachable、nodeLabel、configurationVersion）和 `FixedData`（uniqueId） |
| `Unregister()` | 删除前必须先从 `CodegenDataModelProvider` 注销 |
| `SetReachable()` | 值变化才写 cluster；BDBI 自行上报 |
| `SetName()` | 同时更新 cluster 的 NodeLabel |
| `IncreaseConfigurationVersion()` | 委托 BDBI cluster；按规范同时递增桥自身的 ConfigurationVersion |
| `OnNodeLabelChanged()` | 控制器写 NodeLabel 时回写 `mName` |

子类只负责各自属性的镜像与变更通知：

- `ApplyOnOff()` / `ApplyMeasuredValue()` 都**只在值真正变化时**触发回调，
  避免重复上报和无意义唤醒。
- 温度值会 clamp 到 `min/max`。

### 4.5 `src/bridge/BridgeEndpointManager`

单例，负责动态 endpoint 的槽位与 endpoint id。

```cpp
kMaxDevices             = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT   // 16
kMaxClustersPerEndpoint = 4
kAggregatorEndpointId   = 1
mDevices[16]            // slot -> BridgedDevice*
mDataVersions[16][4]    // 每个 slot 每个 cluster 的 DataVersion
mFirstDynamicEndpoint   // = fixedEndpointCount（当前为 3）
```

- `Initialize()`：计算动态起点（fixed 端点 0/1/2 → 3），并**禁用最后一个 fixed
  endpoint**（endpoint 2 是 ZAP placeholder）。
- `AddDevice()`：
  1. 找空 slot；
  2. 按 `DeviceKind` 选 `EmberAfEndpointType`、device type 列表、cluster 数量；
  3. 从 `mNextEndpointId` 开始尝试 `emberAfSetDynamicEndpointWithEpUniqueId`
     （或非 UniqueID 版本）；
  4. 若返回 `CHIP_ERROR_ENDPOINT_EXISTS`，在 3..18 内循环找下一个 id；
  5. 成功后记录 slot，并**在端点存在之后**注册 BDBI cluster；
  6. 返回 slot；失败返回 -1。
- `RemoveDevice()`：`device->Unregister()` → `emberAfClearDynamicEndpoint(slot)`
  → 清 slot。
- `FindByEndpoint()`：`emberAfGetDynamicIndexFromEndpoint` → slot → 设备。
- `FindByPhysicalId()`：线性查 `mDevices`。

动态 endpoint 的 device type 组合：

| 设备 | device types |
|---|---|
| On/Off Light | `0x0100` + Bridged Node `0x0013` |
| Temperature Sensor | `0x0302` + Bridged Node `0x0013` |

### 4.6 `src/bridge/BridgedAttributeAccess`

这里实现 ember 要求的两个全局回调，并把写命令经函数指针转发给应用层，
从而让 `bridge` 不依赖具体适配器。

读：

- `OnOff` cluster：`OnOff`、`ClusterRevision`
- `TemperatureMeasurement`：`MeasuredValue`、`MinMeasuredValue`、
  `MaxMeasuredValue`、`FeatureMap`、`ClusterRevision`
- `maxReadLength` 必须**精确等于**属性宽度，否则返回 `Failure`
  （SDK 会按声明大小询问，这里是最常见的“读属性失败”来源）。

写：

- 只接受 `OnOff` cluster 的 `OnOff` 属性；
- 设备必须 `IsReachable()`；
- 取 `gOnOffWriteHandler`，交给 `BridgeApplication`。

⚠️ 注意：本文件把动态 endpoint 的 cluster revision 硬编码为
`OnOff = 4`、`TemperatureMeasurement = 1`，而当前 SDK 数据模型
（`zap/dxh-bridge.matter`）给出的是 `6` / `4`。接真实控制器或做认证前
需要核对这些值是否真的通过 external callback 暴露；这是升级 SDK 时的高危点。

### 4.7 `src/app/BridgeApplication`

组合根与生命周期所有者。

成员：

| 成员 | 所有者 | 说明 |
|---|---|---|
| `mAdapter` | 本类 | 目前类型是 `unique_ptr<MockDeviceAdapter>` |
| `mDevices` | 本类 | `unique_ptr<BridgedDevice>`，下标无业务含义 |
| `mDevicesByPhysicalId` | 本类 | 回调更新时按 physicalId 查找 |
| `mConsoleThread` | 本类 | pthread，仅 Mock 阶段 |

`Init()` 的顺序见 3.1。`AddDiscoveredDevices()` 里的一个细节：温度传感器在
`AddDevice()` 成功之后，还要调用
`TemperatureMeasurement::SetMeasuredValueRange()` /
`SetMeasuredValue()` 把快照写进 code-driven cluster——因为外部属性回调
**不参与** code-driven cluster 的读取。

`CreateDevice()`：

- On/Off：changed callback → `MatterReportingAttributeChangeCallback`
- Temperature：changed callback → `TemperatureMeasurement::SetMeasuredValue`

`OnAdapterStateChanged()` / `ApplyAdapterUpdate()`：

- 用 `Platform::New` 拷贝 descriptor，`ScheduleWork` 投递；
- 未知 physicalId 只记错误日志（增量发现尚未实现）。

控制台线程（仅开发期）：

| 键 | 效果 |
|---|---|
| `c` | 翻转所有灯 |
| `t` | 所有温度 +1.00 °C |
| `u` / `v` | 第一个温度传感器离线 / 在线 |

stdin 在独立线程读取，通过 `SystemLayer().ScheduleLambda` 投递到 Matter 线程。

> **接真实适配器时注意**：`mAdapter` 目前是 `unique_ptr<MockDeviceAdapter>`，
> 而 `HandleConsoleInput()` 调用了 Mock 专属的 `Simulate*` 方法。切换实现时
> 需要把成员改为 `unique_ptr<IDeviceAdapter>`，并把控制台模拟拆成 Mock 专属
> 代码（或加一个可选的仿真钩子接口），否则编译不过。

### 4.8 `main.cpp`

只做两件事：`ChipLinuxAppInit()` 和 `ChipLinuxAppMainLoop()`。
业务初始化全部在 `ApplicationInit()`（`BridgeApplication.cpp` 末尾）。

---

## 5. Matter 数据模型（ZAP）

数据模型源：[zap/dxh-bridge.zap](../zap/dxh-bridge.zap)，生成的 IDL
[zap/dxh-bridge.matter](../zap/dxh-bridge.matter) 是 codegen 的输入，两者都必须提交。

| Endpoint | 内容 | 运行时 |
|---|---|---|
| 0 | Root device，19 个 cluster（Descriptor、AccessControl、BasicInformation、网络/诊断、OperationalCredentials…） | 正常 |
| 1 | Aggregator：`Identify` + `Descriptor` | 正常；所有桥接设备的 parent |
| 2 | ZAP **placeholder**：`OnOff`、`LevelControl`、`Descriptor`、`Switch`、`TemperatureMeasurement` | **启动后被禁用**，仅用于生成 cluster 实现 |
| 3..18 | 动态 endpoint（16 个） | 运行时按发现结果创建 |

为什么需要 placeholder：`DECLARE_DYNAMIC_CLUSTER` 只提供属性元数据，cluster 的
实现和回调注册由 codegen 根据固定 endpoint 生成。动态 endpoint 用到的每个
cluster 都必须先在某个固定 endpoint 上存在。因此：

> **新增动态 cluster 时，必须同时在 endpoint 2 加入该 cluster，并重新生成
> `.matter`**，否则会出现“endpoint 可见但属性读写全 Failure”。

Endpoint 数量有两个同步点：

- `config/CHIPProjectAppConfig.h` 的 `CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT`
- `zap/BUILD.gn` 的 `-DDYNAMIC_ENDPOINT_COUNT=`

改一处必须改另一处。

---

## 6. 线程、生命周期与内存

| 线程 | 来源 | 允许做什么 |
|---|---|---|
| Matter 线程 | `ChipLinuxAppMainLoop` / `PlatformMgr().RunEventLoop()` | 所有 SDK 对象、cluster、属性、endpoint |
| 适配器线程（未来） | 真实适配器 | 只调用状态回调；不得直接碰 Matter 对象 |
| 控制台线程 | `BridgeApplication` | 读 stdin；通过 `ScheduleLambda` 进入 Matter 线程 |

跨线程规则：

- 适配器回调 → `PlatformMgr().ScheduleWork`（当前实现）。
- 其他异步事件 → `SystemLayer().ScheduleLambda`。
- 禁止在 Matter 线程做阻塞 IO（真实适配器的网络读写在适配器线程完成）。

所有权：

- `BridgeApplication` 拥有 adapter 和所有 `BridgedDevice`（`unique_ptr`）。
- `BridgeEndpointManager` 只持有裸指针，不负责释放。
- 关闭顺序必须是：停止外部事件源（console/adapter）→ `RemoveDevice()`（先从
  code-driven registry 注销，再清 endpoint）→ 释放对象。顺序反了会留下悬空
  cluster 注册。

---

## 7. 构建系统速查

- 可执行目标：`//src/app:dxh-matter-bridge`
  （[src/app/BUILD.gn](../src/app/BUILD.gn)），`include_dirs = [".."]`，
  `cflags = ["-Wconversion"]`。
- 数据模型目标：`//zap:dxh-bridge-data-model`（`chip_data_model`）。
- 依赖里的数据模型通过 `${chip_root}/examples/dxh-matter-core/zap:...` 引用，
  这是 GN 可见性规则的要求（`setup.sh` 建软链接）。
- 产品配置：`config/CHIPProjectAppConfig.h`，由 `args.gni` 注入到所有编译单元。
- 构建参数：`args.gni`（standalone 配置、tracing、endpoint unique id）。
- `build.sh`：`setup.sh` → 在 SDK 目录内 source `activate.sh` → `gn gen` →
  `ninja` → macOS 上 `dsymutil` 生成 `.dSYM`（lldb 断点需要）。
- clangd 用 `out/debug/compile_commands.json`；新增源文件或改 ZAP 后重新
  `gn gen`（VS Code 里有对应 task）。

新增源文件的完整动作：加进 `src/app/BUILD.gn` 的 `sources`；若引入新依赖，
补 `deps`（优先复用 SDK 已有的库）。

---

## 8. 开发配方

### 8.1 接入真实通讯适配器

1. 在 `src/adapters/` 新建 `DuwiXxxAdapter.h/.cpp`，实现 `IDeviceAdapter`。
2. 适配器内部：建链、订阅设备列表、把云端事件转换成 `DeviceDescriptor` 回调。
3. `BridgeApplication.h`：把 `mAdapter` 改为 `std::unique_ptr<adapters::IDeviceAdapter>`；
   把控制台模拟从 `HandleConsoleInput()` 中解耦（Mock 专属）。
4. `BridgeApplication.cpp` 的 `Init()`：构造新适配器并替换 Mock。
5. `src/app/BUILD.gn`：加源文件；第三方库加到 `deps`/`libs`。
6. `tests/`：给适配器的状态机、错误码映射加宿主单测（协议层可用 fake server）。
7. 按 [ADAPTER_GUIDE.md](ADAPTER_GUIDE.md) 的验收清单跑 chip-tool。

骨架：

```cpp
class DuwiMqttAdapter final : public IDeviceAdapter
{
public:
    const char * Name() const override { return "duwi"; }
    bool Start() override;                          // 连接、订阅
    void Stop() override;
    std::vector<model::DeviceDescriptor> Discover() override;
    model::AdapterStatus SetOnOff(const std::string & physicalId, bool on) override;
    void SetStateChangedCallback(StateChangedCallback callback) override
    {
        mCallback = std::move(callback);
    }
private:
    StateChangedCallback mCallback;
};
```

注意：

- `Start()` 返回 false 会中止桥启动。
- `Discover()` 返回启动瞬间的稳定快照；运行中新增设备需要增量发现（见 8.5）。
- 回调可从任意线程触发；不要在里面访问 Matter 对象。
- `SetOnOff()` 返回 `kOk` 只表示“已接受”，不要在这里改 Matter 状态。

### 8.2 新增一种设备类型 / 动态 endpoint

以新增 “Dimmable Light” 为例：

1. **模型**：[DeviceTypes.h](../src/model/DeviceTypes.h)
   增加 `DeviceKind::kDimmableLight`，更新 `DeviceKindName()`，按需扩展
   `DeviceState`（如 `currentLevel`）。
2. **动态端点声明**：[BridgeEndpointManager.cpp](../src/bridge/BridgeEndpointManager.cpp)
   增加属性表、cluster 表、`DECLARE_DYNAMIC_ENDPOINT`，并在 `AddDevice()` 的
   `switch` 里映射 device type 列表。
3. **设备类**：[BridgedDevice.h](../src/bridge/BridgedDevice.h) 增加子类
   （或扩展现有类），维护镜像状态 + `ApplyXxx()` + changed callback。
4. **属性读写**：[BridgedAttributeAccess.cpp](../src/bridge/BridgedAttributeAccess.cpp)
   按存储模型二选一：
   - external storage：在读/写回调里增加分支，注意 `maxReadLength` 精确匹配；
   - code-driven cluster：调用 SDK 的 `SetXxx()`，不要在外部回调里重复实现。
5. **ZAP**：在 `endpoint 2` 加入该设备用到的 cluster，重新生成 `.matter`
   （见 [zap/README.md](../zap/README.md)）。
6. **工厂**：[BridgeApplication.cpp](../src/app/BridgeApplication.cpp) 的
   `CreateDevice()` 构造新子类并接好 changed callback。
7. **容量**：若单个 endpoint 的 cluster 数超过 4，调整
   `kMaxClustersPerEndpoint` 与 `mDataVersions`。
8. **验证**：`./scripts/test.sh`、`./scripts/build.sh`、chip-tool 读写/订阅。

### 8.3 新增属性（现有 cluster）

- 先判断它属于 external storage 还是 code-driven：
  - external：加 `DECLARE_DYNAMIC_ATTRIBUTE`，在读回调（以及写回调，如果可写）
    加分支；在值变化处调 `MatterReportingAttributeChangeCallback`。
  - code-driven：用 SDK 的 cluster API 更新，不要重复实现读写。
- 若属性在固定 endpoint 上（非动态），同时更新 ZAP 并重新生成。
- 单测里覆盖“值未变化不触发回调”。

### 8.4 修改 ZAP 数据模型

1. 用 ZAP GUI 打开 `zap/dxh-bridge.zap`（命令见 [zap/README.md](../zap/README.md)）。
2. 修改固定 endpoint（0/1/2）；动态 endpoint 的 cluster 必须先在 2 上存在。
3. 重新生成 `zap/dxh-bridge.matter`，与 `.zap` 一起提交。
4. `./scripts/build.sh --clean`，确认 codegen 产物和 cluster revision。

### 8.5 增量发现（待实现的设计）

当前只有启动快照。真实适配器支持设备后上线时，需要：

1. `IDeviceAdapter` 增加“设备上线/下线”事件（或复用 `reachable` 状态回调）。
2. `ApplyAdapterUpdate()` 遇到未知 `physicalId` 时：
   `CreateDevice + AddDevice`，再放入 `mDevicesByPhysicalId`。
3. 设备移除时 `RemoveDevice()` + 从容器删除；注意先注销 BDBI。
4. 设备只是离线时**不要**销毁 endpoint，改 `reachable=false` 即可（订阅者体验
   更好，也避免 endpoint id 漂移）。

### 8.6 调试

- lldb / VS Code：`launch.json` 已配置；macOS 断点依赖 `build.sh` 生成的
  `.dSYM`。
- IM trace：`./scripts/run.sh --trace-to json:/tmp/dxh.trace.json`。
- 断点建议：`ApplicationInit`、`BridgeEndpointManager::AddDevice`、
  `emberAfExternalAttributeReadCallback`、`BridgeApplication::ApplyAdapterUpdate`。
- 重新配网用新的 `--KVS` 路径。
- ASan：`DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' ./scripts/build.sh`。

---

## 9. 约束清单（动手前过一遍）

1. **SDK 只读**。不要改 `third_party/connectedhomeip` 的已跟踪文件；需要 SDK
   行为变化时，在 fork 上改并走 submodule 升级流程。
2. **placeholder 规则**。动态 endpoint 用到的 cluster 必须出现在 endpoint 2。
3. **UniqueID 稳定**。`adapterName + physicalId` 一旦发布不可变；改了要递增
   ConfigurationVersion，控制器端可能仍会出现重复设备。
4. **endpoint 数量双点同步**。`CHIPProjectAppConfig.h` 与 `zap/BUILD.gn`。
5. **写命令不乐观更新**。状态以适配器回调为准。
6. **设备离线拒绝写**。`BridgedAttributeAccess` 会检查 `IsReachable()`。
7. **`maxReadLength` 精确匹配**。按属性宽度返回，否则控制器读到 Failure。
8. **`ApplyXxx` 只在值变化时通知**，避免上报风暴。
9. **UI 线程/适配器线程不能直接碰 Matter 对象**，一律 `ScheduleWork` /
   `ScheduleLambda`。
10. **注册/注销顺序**：endpoint 先创建再注册 BDBI；删除时先 `Unregister()`
    再清 endpoint。
11. **`zap/*.matter` 是输入不是产物**，必须和 `.zap` 一起提交。
12. **动态 endpoint 的 cluster revision** 目前硬编码在
    `BridgedAttributeAccess.cpp`，升级 SDK/认证前核对（见 4.6）。
13. **Mock 类型耦合**：`BridgeApplication::mAdapter` 目前是 `MockDeviceAdapter`，
    接真实适配器时要改类型并解耦控制台模拟。

---

## 10. 验证矩阵（chip-tool）

```bash
CT=third_party/connectedhomeip/out/darwin-arm64-chip-tool-clang/chip-tool
$CT pairing onnetwork 1 20202021

# 拓扑：endpoint 0 的 parts-list 应包含 1 和所有动态 endpoint
$CT descriptor read parts-list 1 0
$CT descriptor read device-type-list 1 3

# On/Off
$CT onoff read on-off 1 3
$CT onoff toggle 1 3

# 温度
$CT temperaturemeasurement read measured-value 1 4

# Bridged Device Basic Information
$CT bridgeddevicebasicinformation read reachable 1 4
$CT bridgeddevicebasicinformation subscribe reachable 1 4 1 10
```

预期：控制台按 `u` 后，已订阅的 `reachable` 应立即变为 false 并收到上报；
`v` 恢复 true。

---

## 11. SDK 升级重点核对

升级 `third_party/connectedhomeip` 后，优先验证：

1. `BridgedDeviceBasicInformationCluster` 的 `MutableData` / `FixedData` /
   `Context` 字段（本工程在 `CreateBridgedDeviceInfo()` 里构造）。
2. `emberAfSetDynamicEndpointWithEpUniqueId()` 的参数与返回值。
3. `DECLARE_DYNAMIC_*` 宏与 external attribute 回调签名。
4. `CodegenDataModelProvider::Registry().Register/Unregister`。
5. `TemperatureMeasurement::SetMeasuredValue*` 的 CodegenIntegration。
6. ZAP 重新生成后 cluster revision 是否变化（对照 4.6 的硬编码值）。

---

## 12. 代码索引

| 想改什么 | 改哪里 |
|---|---|
| 设备协议接入 | `src/adapters/`、`BridgeApplication::Init()` |
| 设备类型/端点模板 | `BridgeEndpointManager.cpp`、`DeviceTypes.h` |
| 单个设备的状态/属性语义 | `BridgedDevice.h/.cpp` |
| 属性读写/上报 | `BridgedAttributeAccess.cpp`、`BridgeApplication::CreateDevice()` |
| endpoint 数量 | `CHIPProjectAppConfig.h`、`zap/BUILD.gn` |
| 数据模型/cluster 集合 | `zap/dxh-bridge.zap` + 重新生成 `.matter` |
| 构建目标/源文件/依赖 | `src/app/BUILD.gn` |
| 产品 VID/PID | `config/CHIPProjectAppConfig.h` |
| 启动参数/调试 | `scripts/run.sh`、`.vscode/launch.json` |