# DUWI Matter Bridge Core (dxh.matter.core.new)

DUWI 智能家居 Matter 桥接器的正式工程骨架。工程以 **out-of-tree** 方式引用
`connectedhomeip`（Matter SDK）：SDK 作为 git submodule 固定在
`third_party/connectedhomeip`（fork 自 `lujunquan/connectedhomeip`），本工程
不修改 SDK 源码；桥接器把 DUWI 智能设备以 Matter 动态 endpoint 的形式暴露给
Apple Home、Google Home、Alexa、Home Assistant 等控制器。

> 当前阶段：工程骨架 + 可运行的 Mock 适配器。
> 与 DUWI 真实设备的通讯方式（MQTT / HTTP / BLE / 私有网关协议等）尚未确定，
> 后续通过实现 `IDeviceAdapter` 接口接入，不需要改动 Matter 侧代码。

---

## 1. 状态与边界

| 项目 | 状态 |
|---|---|
| Matter 桥接器主流程（启动、配网、动态 endpoint） | 已完成 |
| Bridged Device Basic Information（code-driven cluster） | 已完成 |
| On/Off Light、Temperature Sensor 动态 endpoint | 已完成 |
| 设备通讯适配器接口 `IDeviceAdapter` | 已完成（稳定接缝） |
| Mock 适配器（开发/演示/自测） | 已完成 |
| DUWI 真实通讯适配器 | **待接入** |
| Actions / Rooms / Composed Device | 未启用（ZAP 中已移除 Actions） |
| 生产 VID/PID、DAC 证书 | 待替换（当前为 CSA 测试值 0xFFF1/0x8001） |

---

## 2. 架构

```
                 ┌──────────────────────────────────────────┐
                 │              Matter Controller           │
                 └───────────────────┬──────────────────────┘
                                     │ Read / Write / Subscribe
                 ┌───────────────────▼──────────────────────┐
                 │              connectedhomeip             │
                 │  data model / IM / session / mDNS / BLE  │
                 └───────────────────┬──────────────────────┘
                                     │ dynamic endpoints + external attributes
        ┌────────────────────────────▼────────────────────────────┐
        │ src/bridge  (SDK-dependent)                             │
        │  BridgeEndpointManager  BridgedDevice  AttributeAccess  │
        └────────────────────────────┬────────────────────────────┘
                                     │ DeviceDescriptor / AdapterStatus
        ┌────────────────────────────▼────────────────────────────┐
        │ src/adapters (SDK-independent)                          │
        │  IDeviceAdapter  ◀── MockDeviceAdapter (current)        │
        │                 ◀── DuwiMqttAdapter / ... (future)      │
        └────────────────────────────┬────────────────────────────┘
                                     │ transport TBD
                              DUWI 智能设备 / 网关 / 云
```

分层规则：

- `src/model`、`src/adapters`、`src/bridge/StableId.*` **禁止**包含
  connectedhomeip 头文件，可以脱离 SDK 编译和单测。
- `src/bridge` 负责 Matter 语义：动态 endpoint、属性读写、上报。
- `src/app` 是组合根：创建设备、注册回调、处理生命周期和控制台模拟。
- 设备状态变化的唯一来源是适配器；Matter 写命令也会转发给适配器，等待适配器
  回报状态后再更新 Matter 属性。

概览见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，逐模块说明、运行时流程和
开发配方见 [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)。

---

## 3. 目录结构

```
dxh.matter.core.new/
├── .gn                     GN 根配置（工程根必须是本目录）
├── .gitmodules             connectedhomeip 子模块定义（fork 地址 + 固定提交）
├── args.gni                Matter 构建参数（产品配置、动态 endpoint 等）
├── BUILD.gn                默认目标
├── config/
│   └── CHIPProjectAppConfig.h   VID/PID、动态 endpoint 数量
├── zap/
│   ├── dxh-bridge.zap      数据模型源文件（固定 endpoint 0/1/2）
│   ├── dxh-bridge.matter   由 ZAP 生成的 IDL，codegen 的输入
│   └── BUILD.gn            chip_data_model 目标
├── src/
│   ├── model/DeviceTypes.h     SDK 无关的领域模型
│   ├── adapters/               IDeviceAdapter + MockDeviceAdapter
│   ├── bridge/                 Matter 动态 endpoint / 属性访问 / 稳定 ID
│   └── app/                    main + BridgeApplication（组合根）
├── scripts/
│   ├── setup.sh            初始化 SDK 子模块 + 创建构建软链接（含 --clean）
│   ├── build.sh            gn gen + ninja
│   ├── run.sh              带开发默认参数运行
│   ├── test.sh             不依赖 SDK 的宿主单测
│   └── format.sh           clang-format
├── tests/test_core.cpp     纯 C++ 单测
├── third_party/
│   └── connectedhomeip     Matter SDK 子模块（lujunquan/connectedhomeip fork）
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPER_GUIDE.md    代码级架构、运行时流程、开发配方
│   ├── BUILD.md
│   └── ADAPTER_GUIDE.md
└── out/                    构建输出（已 gitignore）
```

---

## 4. 快速开始

前置条件：

- macOS（Apple Silicon / Intel）或 Linux x86_64/aarch64
- `git`、`python3`、`ninja`、`clang`（SDK 的 `activate.sh` 会提供 gn/ninja/zap）

`connectedhomeip` 是 git submodule（指向
`git@github.com:lujunquan/connectedhomeip.git`，固定在 `v1.6.1.0`）：

```bash
# 1) 初始化 SDK 子模块（首次会下载数 GB，包含 SDK 自身的子模块）
git submodule update --init --recursive
./scripts/setup.sh

# 2) 构建（首次会编译整套 Matter 库，耗时较长）
./scripts/build.sh

# 3) 运行（开发默认值：discriminator 1234 / passcode 20202021）
./scripts/run.sh
```

产物：`out/debug/dxh-matter-bridge`。

`setup.sh` 会在 SDK 中创建一个未跟踪的软链接
`third_party/connectedhomeip/examples/dxh-matter-core -> 本工程`。这是 GN
可见性规则的要求（详见 [docs/BUILD.md](docs/BUILD.md#gn-可见性说明)），可用
`./scripts/setup.sh --clean` 移除。首次 `git clone` 时若想一步到位，可用
`git clone --recurse-submodules <repo-url>`。

---

## 5. 配网与手工验证

先编译 chip-tool（只需一次）：

```bash
cd third_party/connectedhomeip
scripts/run_in_build_env.sh \
  "./scripts/build/build_examples.py --target darwin-arm64-chip-tool-clang --quiet build"
```

Linux 主机把 target 换成 `linux-x64-chip-tool-clang`。

```bash
CT=third_party/connectedhomeip/out/darwin-arm64-chip-tool-clang/chip-tool

$CT pairing onnetwork 1 20202021

# endpoint 0 的 parts-list 展示真实拓扑（固定 0/1/2 + 动态 3..257）
$CT descriptor read parts-list 1 0
$CT descriptor read device-type-list 1 3

$CT onoff read on-off 1 3
$CT onoff toggle 1 3
$CT temperaturemeasurement read measured-value 1 5
$CT bridgeddevicebasicinformation read reachable 1 5
$CT bridgeddevicebasicinformation subscribe reachable 1 5 1 10
```

当前 Mock 设备：

| endpoint | 设备 | Matter device type |
|---|---|---|
| 3 | DUWI Lamp 1 (on/off light) | 0x0100 On/Off Light |
| 4 | DUWI Lamp 2 (on/off light) | 0x0100 On/Off Light |
| 5 | DUWI Temperature 1 | 0x0302 Temperature Sensor |

运行中的应用支持控制台模拟（需要前台 TTY）：

| 按键 | 作用 |
|---|---|
| `c` | 翻转所有灯（模拟设备侧状态变化） |
| `t` | 所有温度传感器 +1.00 °C |
| `u` / `v` | 第一个温度传感器离线 / 在线 |

按 `u`/`v` 时，已订阅 `reachable` 的控制器应立即收到上报。

---

## 6. 调试

- 交互模型 trace：`./scripts/run.sh --trace-to json:/tmp/dxh.trace.json`
- lldb：

  ```bash
  lldb out/debug/dxh-matter-bridge
  (lldb) settings set target.run-args --discriminator 1234 --passcode 20202021 --KVS /tmp/dxh.kvs
  (lldb) breakpoint set -n ApplicationInit
  (lldb) breakpoint set -r BridgeEndpointManager::AddDevice
  (lldb) breakpoint set -r emberAfExternalAttribute
  (lldb) run
  ```

- ASan：`DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' ./scripts/build.sh`
- 重新配网前使用新的 `--KVS` 路径，或删除旧的 KVS 文件。

更多排查方法见 [docs/BUILD.md](docs/BUILD.md#调试与排查)。

---

## 7. 接入 DUWI 真实设备通讯

通讯协议确定后：

1. 在 `src/adapters/` 新增 `DuwiXxxAdapter`，实现
   [IDeviceAdapter](src/adapters/IDeviceAdapter.h)。
2. 在 `BridgeApplication::Init()` 中替换 `MockDeviceAdapter`。
3. 在 `src/app/BUILD.gn` 的 `sources` 中加入新文件（如需第三方库，同时补充
   `deps`）。
4. 在 `tests/` 增加适配器状态机单测；协议联调使用 chip-tool 或 Python 测试。

完整步骤与检查清单见 [docs/ADAPTER_GUIDE.md](docs/ADAPTER_GUIDE.md)。

关键约束：

- `IDeviceAdapter::Name()` 一旦随产品发布就不能修改（它参与 UniqueID 派生）。
- `DeviceDescriptor::physicalId` 必须跨重启、升级、重新配对保持稳定。
- 状态回调可能来自任意线程，桥接层会调度到 Matter 线程。

---

## 8. 数据模型

- `zap/dxh-bridge.zap` 是唯一数据模型源文件，同时提交 `.matter`。
- 固定 endpoint：
  - `0`：root device
  - `1`：Aggregator（桥接设备的 parent endpoint）
  - `2`：ZAP placeholder，运行时被禁用；**所有动态 endpoint 用到的 cluster
    必须同时出现在这里**
- 动态 endpoint 从 `3` 开始，最多
  `CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT`（255）个。

修改数据模型的流程见 [zap/README.md](zap/README.md)。

---

## 9. 测试

```bash
./scripts/test.sh    # 纯 C++ 核心测试，无需 SDK
```

当前覆盖：稳定 UniqueID 派生、Mock 适配器发现/命令/模拟事件。Matter 集成测试
使用 SDK 的 Python 测试框架（如 `TC_BRBINFO_*`），按实际 endpoint 号传参。

---

## 10. 已知限制 / Roadmap

- [ ] 真实 DUWI 通讯适配器
- [ ] Dimmable Light（LevelControl）与更多设备类型
- [ ] Actions / Rooms / Composed Device
- [ ] 生产 VID/PID、DAC/PAA 证书与工厂数据
- [ ] OTA / 诊断 / 远程日志
- [ ] 多适配器并存与设备去重
- [ ] 自有 CI 构建机（当前 CI 只跑 SDK 无关单测）

---

## 11. SDK 版本

工程只通过软链接引用 SDK，不复制 SDK 源码。升级 SDK 时：

```bash
cd connectedhomeip && git fetch && git checkout <tag/commit>
cd ../dxh.matter.core.new && ./scripts/build.sh --clean
```

若 SDK 更新导致 API 变化，优先修改 `src/bridge` 中的适配代码，保持
`src/model`、`src/adapters` 稳定。