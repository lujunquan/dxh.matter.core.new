# 架构说明

本文描述 `dxh.matter.core.new` 的分层、数据流、线程模型和不可破坏的约束。

---

## 1. 分层

| 层 | 目录 | 是否依赖 Matter SDK | 职责 |
|---|---|---|---|
| 领域模型 | `src/model` | 否 | `DeviceDescriptor`、`DeviceState`、`DeviceKind`、`AdapterStatus` |
| 适配器 | `src/adapters` | 否 | 与 DUWI 设备通讯的接口与实现 |
| 桥接层 | `src/bridge` | 是 | 动态 endpoint、Matter 属性读写、UniqueID、BDBI 注册 |
| 应用层 | `src/app` | 是 | 组合根、生命周期、适配器事件调度、控制台模拟 |
| 数据模型 | `zap/` | 构建期 | 固定 endpoint 与 cluster/attribute 集合 |

依赖方向严格单向：

```
app ──▶ bridge ──▶ model
 │         │
 └────────▶adapters ──▶ model
```

`bridge` 不得 include `adapters` 的实现；它只通过 `model::DeviceDescriptor`
接收数据。`app` 负责把两者接起来。

---

## 2. 数据流

### 2.1 设备状态 → Matter 上报

```
适配器线程 / Matter 线程
   │  IDeviceAdapter::StateChangedCallback(DeviceDescriptor)
   ▼
BridgeApplication::OnAdapterStateChanged
   │  Platform::New<DeviceDescriptor> + PlatformMgr().ScheduleWork
   ▼
(Matter 线程) BridgeApplication::ApplyAdapterUpdate
   │  SetReachable / ApplyOnOff / ApplyMeasuredValue
   ▼
BridgedDevice 变更回调
   │  MatterReportingAttributeChangeCallback(endpoint, cluster, attribute)
   ▼
订阅该属性的控制器收到 ReportData
```

要点：

- 适配器回调可能在任意线程；`ApplyAdapterUpdate` 保证在 Matter 线程执行。
- `BridgedDevice::ApplyOnOff` / `ApplyMeasuredValue` 只在值变化时触发回调，避免
  重复上报和无意义唤醒。
- `reachable`、`node-label`、`unique-id` 由 Bridged Device Basic Information
  cluster 自行上报，不需要应用层再调 reporting。

### 2.2 Matter 写命令 → 设备

```
Matter Write(OnOff)
   ▼
emberAfExternalAttributeWriteCallback
   │  校验 cluster / attribute / reachable
   ▼
BridgeApplication::HandleMatterOnOffWrite
   │  查 physicalId
   ▼
IDeviceAdapter::SetOnOff(physicalId, on)
   │  成功/失败
   ▼
返回 InteractionModel::Status
```

写命令**不会**直接修改 Matter 侧状态。设备状态以适配器回报为准；Mock 适配器会
同步回调，因此本地看起来是立即生效的。真实适配器应在设备确认后回报。

### 2.3 启动顺序

```
main()
  └─ ChipLinuxAppInit()            SDK：平台层、Server、配网参数
       └─ ApplicationInit()
            ├─ BridgeEndpointManager::Initialize()   计算动态 endpoint 起点
            ├─ 注册 OnOffWriteHandler
            ├─ 创建并启动 IDeviceAdapter
            ├─ Discover() → 创建设备 → AddDevice()（动态 endpoint + BDBI 注册）
            └─ 启动控制台模拟线程
  └─ ChipLinuxAppMainLoop()        Matter 事件循环
       └─ ApplicationShutdown()
            ├─ 停止控制台线程
            ├─ adapter.Stop()
            └─ RemoveDevice()（注销 BDBI + 清除动态 endpoint）
```

---

## 3. Endpoint 模型

| Endpoint | 类型 | 说明 |
|---|---|---|
| 0 | root device | SDK 必需 |
| 1 | Aggregator | 所有桥接设备的 parent endpoint |
| 2 | ZAP placeholder | 包含动态 endpoint 所需的全部 cluster；运行时被禁用 |
| 3..18 | 动态 | `CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT = 16` |

动态 endpoint 的 DeviceTypeList：

- On/Off Light：`0x0100` + Bridged Node `0x0013`
- Temperature Sensor：`0x0302` + Bridged Node `0x0013`

每个动态 endpoint 拥有独立 `DataVersion` 存储（`BridgeEndpointManager` 内部），
生命周期覆盖整个运行期。

---

## 4. 线程模型

| 线程 | 来源 | 允许的操作 |
|---|---|---|
| Matter 线程 | `PlatformMgr().RunEventLoop()` | 所有 `BridgedDevice` 状态、cluster API、attribute storage |
| 适配器线程 | 未来真实适配器 | 只调用回调；不得直接访问 Matter 对象 |
| 控制台线程 | `BridgeApplication` | 读取 stdin，通过 `ScheduleWork` 投递到 Matter 线程 |

规则：任何跨线程状态变化都必须经过 `PlatformMgr().ScheduleWork`（或
`SystemLayer().ScheduleLambda`）。

---

## 5. 不可破坏的约束

1. **ZAP placeholder 必须包含动态 endpoint 用到的 cluster。**
   `DECLARE_DYNAMIC_*` 只告诉运行时属性元数据；cluster 的实现注册仍由
   codegen 根据固定 endpoint 生成。新增动态 cluster 时，必须同时在
   `zap/dxh-bridge.zap` 的 endpoint 2 中加入该 cluster，然后重新生成。
2. **UniqueID 必须稳定。**
   `MakeStableUniqueId(adapterName, physicalId)` 的结果一经暴露给控制器就不能
   改变；否则需要递增该设备的 ConfigurationVersion，且控制器端可能出现重复
   设备。适配器名称和 physicalId 都必须稳定。
3. **动态 endpoint 数量有限。**
   `kMaxDevices` 来自 `CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT`。超出时
   `AddDevice()` 返回 -1，设备不会被暴露。
4. **SDK 树保持只读。**
   唯一的例外是 `setup.sh` 创建的未跟踪软链接
   `connectedhomeip/examples/dxh-matter-core`，用于满足 GN 可见性规则。
5. **适配器层不得 include SDK。**
   保证协议实现可以脱离 Matter 单测和复用。

---

## 6. 扩展点

| 需求 | 修改位置 |
|---|---|
| 新通讯协议 | `src/adapters/` 新增 `IDeviceAdapter` 实现；`BridgeApplication::Init` 替换 |
| 新设备类型 | `src/model/DeviceTypes.h` 增加 `DeviceKind`；`bridge/BridgeEndpointManager` 增加 endpoint 模板；ZAP placeholder 增加 cluster |
| 新 cluster | 优先使用 code-driven cluster（`DefaultServerCluster`）；同时更新 ZAP placeholder |
| 房间 / Actions | 重新在 ZAP 中启用 Actions，并实现 `Actions::Delegate` |
| 组合设备（Composed Device） | `AddDevice` 支持 parentEndpointId；ZAP 中启用 `ma_aggregator` 子节点模型 |
| 多适配器 | 设备键改为 `(adapterName, physicalId)`，UniqueID 已包含 adapterName |

---

## 7. 与 SDK 示例的关系

`src/bridge` 中的动态 endpoint 声明和外部属性回调基于 Matter SDK 的
`examples/bridge-app`，但有以下实质差异：

- 去除了 Actions/Rooms/ComposedDevice，保留最小可用桥接模型。
- 设备来源改为 `IDeviceAdapter::Discover()`，而不是编译期硬编码。
- UniqueID 由稳定哈希派生，而不是随机值。
- 写命令转发给适配器，而不是直接改本地状态。
- 数据模型、产品配置、构建入口都由本工程拥有。

后续 SDK 升级时，重点核对：

- `BridgedDeviceBasicInformationCluster` 的 `MutableData` / `FixedData` / `Context`
- `emberAfSetDynamicEndpointWithEpUniqueId` 签名
- `DECLARE_DYNAMIC_*` 宏
- `ServerClusterInterfaceRegistry::Register/Unregister`