# 设备适配器开发指南

适配器是 DUWI 私有通讯协议与 Matter 桥接层之间**唯一**的接缝。协议确定后，只需
实现本文件描述的接口，不需要改动 Matter 侧代码。

---

## 1. 接口

```cpp
class IDeviceAdapter
{
public:
    using StateChangedCallback = std::function<void(const model::DeviceDescriptor &)>;

    virtual const char * Name() const = 0;
    virtual bool Start();
    virtual void Stop();
    virtual std::vector<model::DeviceDescriptor> Discover() = 0;
    virtual model::AdapterStatus SetOnOff(const std::string & physicalId, bool on) = 0;
    virtual void SetStateChangedCallback(StateChangedCallback callback) = 0;
};
```

参考实现：[MockDeviceAdapter](../src/adapters/MockDeviceAdapter.cpp)。

---

## 2. 生命周期

```
SetStateChangedCallback(cb)   ← 桥接层先注册回调
Start()                       ← 建链、登录、订阅设备列表
Discover()                    ← 返回当前所有设备及初始状态
   ...运行期...
IDeviceAdapter::SetOnOff()    ← Matter 写命令
cb(DeviceDescriptor)          ← 设备状态变化（任意线程）
Stop()                        ← 断链、释放资源
```

约束：

- `Discover()` 应在 `Start()` 成功后调用，且返回稳定快照。
- `Discover()` 可以返回空列表；后续通过回调补报设备（需要桥接层支持增量发现，
  见第 6 节）。
- `SetOnOff()` 返回 `kOk` 仅表示**命令已被接受**，最终状态以回调为准。

---

## 3. 设备身份

| 字段 | 要求 |
|---|---|
| `Name()` | 适配器身份，例如 `"duwi"`。参与 UniqueID 派生，**发布后不可修改** |
| `physicalId` | DUWI 生态内唯一、跨重启/升级/重新配对稳定，例如序列号或 MAC |
| `name` | 用户可见名称，作为 BDBI NodeLabel |
| `location` | 房间/区域，预留给 Actions/Rooms |

UniqueID 由 `MakeStableUniqueId(adapterName, physicalId)` 生成（32 位大写十六进制），
满足 Matter 对 Bridged Device Basic Information UniqueID 的稳定性要求。

---

## 4. 设备类型映射

| `DeviceKind` | Matter device type | 动态 endpoint cluster | 状态映射 |
|---|---|---|---|
| `kOnOffLight` | `0x0100` On/Off Light | OnOff + Descriptor | `DeviceState::onOff` ↔ OnOff |
| `kTemperatureSensor` | `0x0302` Temperature Sensor | TemperatureMeasurement + Descriptor | `measuredValue`（0.01 °C） |

新增设备类型的步骤：

1. `src/model/DeviceTypes.h`：增加 `DeviceKind`。
2. `src/bridge/BridgeEndpointManager.cpp`：声明动态属性表、cluster 表、
   `DECLARE_DYNAMIC_ENDPOINT`，并在 `AddDevice()` 中映射。
3. `src/bridge/BridgedDevice.h/.cpp`：增加设备类（或在现有类上扩展状态）。
4. `src/bridge/BridgedAttributeAccess.cpp`：实现外部属性读写。
5. `zap/dxh-bridge.zap`：**在 endpoint 2（placeholder）中加入新 cluster**。
6. `src/app/BridgeApplication.cpp`：在 `CreateDevice()` 中构造对应对象。
7. 增加单测与 chip-tool 验证命令。

---

## 5. 线程与错误处理

- `StateChangedCallback` 可能从适配器线程调用；桥接层会调度到 Matter 线程。
- 回调中的 `DeviceDescriptor` 会被拷贝，适配器无需保证引用生命周期。
- `SetOnOff()` 可能从 Matter 线程调用；如果协议客户端不是线程安全的，请在
  适配器内部串行化。
- 网络错误返回 `kTransportError`；未知设备返回 `kNotFound`；设备类型不支持返回
  `kNotSupported`。
- 适配器内部日志使用自己的日志系统（本层不依赖 `ChipLog*`）。

---

## 6. 接入新适配器

```cpp
// src/app/BridgeApplication.h
std::unique_ptr<adapters::DuwiMqttAdapter> mAdapter;
```

```cpp
// src/app/BridgeApplication.cpp, Init()
mAdapter = std::make_unique<adapters::DuwiMqttAdapter>(options);
mAdapter->SetStateChangedCallback(...);
if (!mAdapter->Start()) { ... }
AddDiscoveredDevices();
```

同时：

- 在 `src/app/BUILD.gn` 的 `sources` 中加入新文件。
- 如引入第三方库（MQTT、HTTP、TLS），在 `deps` / `libs` 中补充，并注意：
  - 嵌入式目标优先复用 SDK 已有的 mbedTLS/CHIP 网络栈；
  - 避免在 Matter 线程做阻塞 IO。
- 在 `tests/` 增加适配器状态机测试（协议层可用本地 fake server）。

---

## 7. 增量发现（尚未实现）

当前 `AddDiscoveredDevices()` 只在启动时调用一次。真实适配器若支持设备后加入，
需要增加：

1. `IDeviceAdapter` 增加设备上线/离线事件（或复用 `DeviceDescriptor.state.reachable`）。
2. `BridgeApplication` 收到未知 `physicalId` 时动态 `CreateDevice + AddDevice`。
3. 断电/移除设备时调用 `RemoveDevice`。
4. 订阅维护：BDBI `reachable` 上报即可，无需重建 endpoint。

在实现前，桥接器只暴露启动时发现的设备。

---

## 8. 验收清单

- [ ] `./scripts/test.sh` 通过（适配器纯逻辑单测）
- [ ] `chip-tool descriptor read parts-list 1 0` 能看到所有设备
- [ ] 每个设备的 `descriptor read device-type-list 1 <ep>` 正确
- [ ] `onoff read/toggle` 与设备实际状态一致
- [ ] 设备侧状态变化能通过订阅上报到控制器
- [ ] 设备离线时 `bridgeddevicebasicinformation read reachable` 为 false
- [ ] 重启后 UniqueID 不变
- [ ] 断线重连后不产生重复 endpoint