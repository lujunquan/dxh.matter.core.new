# Agent.md — DUWI Matter Bridge 开发指南（面向 AI Agent）

本文件写给在本仓库里工作的 AI Agent。先读 [README.md](README.md) 和
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，本文件是**操作手册**：硬性约束、
代码地图、常见任务流程、验证清单。

---

## 1. 项目速览

- 产品：DUWI 智能家居 Matter 桥接器（`dxh.matter.core.new`）。
- 将 DUWI 设备以 Matter **动态 endpoint** 暴露给 Apple Home / Google Home /
  Alexa / Home Assistant。
- Matter SDK 以 **out-of-tree git submodule** 引用：
  `third_party/connectedhomeip`（fork，固定 `v1.6.1.0`）。
- 当前阶段：工程骨架 + `MockDeviceAdapter`；真实 DUWI 通讯协议尚未接入。
- 构建产物：`out/debug/dxh-matter-bridge`。

依赖方向（严格单向）：

```text
app ──▶ bridge ──▶ model
 │         │
 └────────▶adapters ──▶ model
```

---

## 2. 硬性规则（MUST）

1. **SDK 只读。禁止修改 `third_party/connectedhomeip` 中任何已跟踪文件。**
   需要 SDK 侧行为时，在本工程侧适配。`scripts/setup.sh` 创建的软链接：
   - `build_overrides -> <sdk>/examples/build_overrides`
   - `<sdk>/examples/dxh-matter-core -> 本工程根目录`
     两者都是未跟踪项，不要提交。
2. **SDK 无关层不得 include connectedhomeip。**
   范围：`src/model/`、`src/adapters/`、`src/bridge/StableId.*`。
   这些文件必须能不依赖 SDK 单独编译（`scripts/test.sh` 依赖这一点）。
3. **ZAP 是数据模型唯一事实来源。**
   改数据模型只改 `zap/dxh-bridge.zap`，重新生成 `zap/dxh-bridge.matter`，
   **两个文件都要提交**。
4. **动态 endpoint 用到的每个 cluster 必须同时出现在 endpoint 2（placeholder）。**
   `DECLARE_DYNAMIC_*` 只提供属性元数据；cluster 实现仍由 codegen 按固定
   endpoint 生成。漏加会导致“endpoint 存在但属性读写失败”。
5. **UniqueID 稳定不可变。**
   `MakeStableUniqueId(adapterName, physicalId)` 一经暴露给控制器就不能改变。
   `IDeviceAdapter::Name()` 与 `physicalId` 发布后不得修改；确需修改时必须递增
   对应设备的 ConfigurationVersion 并评估控制器侧重复设备风险。
6. **线程规则。**
   所有 Matter 对象/cluster API/属性存储只能在 Matter 线程访问。适配器回调可能
   来自任意线程，必须用 `PlatformMgr().ScheduleWork` 或
   `SystemLayer().ScheduleLambda` 投递。
7. **设备状态以适配器为准。**
   Matter 写命令只转发给适配器（`SetOnOff`），不要直接改 Matter 侧状态；等适配器
   回报状态后再更新属性并上报。
8. **动态 endpoint 容量两处保持一致。**
   `config/CHIPProjectAppConfig.h` 的
   `CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT` 与 `zap/BUILD.gn` 的
   `-DDYNAMIC_ENDPOINT_COUNT=16` 必须同步。
9. **不要手改生成代码。**
   `out/**/zap-generated/`、`zapgen/`、`zzz_generated/` 均由构建生成。

---

## 3. 常用命令

| 目的 | 命令 |
|---|---|
| 初始化 SDK 子模块 + 创建软链接（幂等） | `./scripts/setup.sh` |
| 移除生成的软链接 | `./scripts/setup.sh --clean` |
| 构建 | `./scripts/build.sh` |
| 干净构建 | `./scripts/build.sh --clean` |
| ASan 构建 | `DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' ./scripts/build.sh` |
| 宿主单测（不需要 SDK） | `./scripts/test.sh` |
| 运行（开发默认参数） | `./scripts/run.sh` |
| 生成 clangd 数据库 | `./scripts/build.sh` 后 `gn gen out/debug --export-compile-commands`（需在 SDK 环境内） |
| 格式化 | `./scripts/format.sh`；仅检查：`./scripts/format.sh --check` |
| 重新生成 `.matter` | 见 [zap/README.md](zap/README.md) |

环境变量：

- `DXH_OUT_DIR`：构建输出目录（默认 `out/debug`）。
- `DXH_GN_ARGS`：传给 `gn gen` 的额外参数。
- `DXH_BINARY` / `DXH_KVS`：`run.sh` 的二进制与 KVS 路径覆盖。
- `DXH_TEST_OUT_DIR`：单测输出目录（默认 `out/tests`）。

注意：`scripts/format.sh` 只调用 `clang-format`；仓库虽有 `.clang-tidy`，
但没有接入任何脚本或 CI。

首次构建/切换分支后先跑 `./scripts/setup.sh`；`build.sh` 会自动调用它。

---

## 4. 代码地图

| 路径 | 职责 |
|---|---|
| [src/app/main.cpp](src/app/main.cpp) | `ChipLinuxAppInit()` + `ChipLinuxAppMainLoop()` |
| [src/app/BridgeApplication.cpp](src/app/BridgeApplication.cpp) | 组合根：启动/关闭、适配器事件调度、控制台模拟 |
| [src/bridge/BridgeEndpointManager.cpp](src/bridge/BridgeEndpointManager.cpp) | 动态 endpoint 声明、分配、释放；endpoint ↔ 设备映射 |
| [src/bridge/BridgedDevice.cpp](src/bridge/BridgedDevice.cpp) | BDBI cluster 注册、reachable/name、OnOff/温度状态镜像 |
| [src/bridge/BridgedAttributeAccess.cpp](src/bridge/BridgedAttributeAccess.cpp) | ember 外部属性读写回调；OnOff 写命令转发 |
| [src/bridge/StableId.cpp](src/bridge/StableId.cpp) | 128 位 FNV 派生 32 字符大写十六进制 UniqueID（无 SDK 依赖） |
| [src/adapters/IDeviceAdapter.h](src/adapters/IDeviceAdapter.h) | 设备协议稳定接缝（无 SDK 依赖） |
| [src/adapters/MockDeviceAdapter.cpp](src/adapters/MockDeviceAdapter.cpp) | Mock 设备与 `c/t/u/v` 控制台模拟事件 |
| [src/model/DeviceTypes.h](src/model/DeviceTypes.h) | `DeviceDescriptor` / `DeviceState` / `DeviceKind` / `AdapterStatus` |
| [config/CHIPProjectAppConfig.h](config/CHIPProjectAppConfig.h) | VID/PID、动态 endpoint 数量 |
| [zap/dxh-bridge.zap](zap/dxh-bridge.zap) | 数据模型源文件 |
| [tests/test_core.cpp](tests/test_core.cpp) | SDK 无关核心单测 |

Endpoint 布局：

| Endpoint | 内容 |
|---|---|
| 0 | root device |
| 1 | Aggregator（所有桥接设备的 parent） |
| 2 | ZAP placeholder（运行时禁用；必须包含动态 endpoint 用到的 cluster） |
| 3..18 | 动态 endpoint（最多 16 个） |

Mock 设备顺序：3 = Lamp 1（on/off）、4 = Lamp 2（on/off）、5 = Temperature 1。
**改 Mock 顺序或改文档里的 endpoint 号时，两处要一起改。**

---

## 5. Matter 语义与易踩的坑

- **TemperatureMeasurement 在本 SDK 是 code-driven cluster。**
  读取/上报由 cluster 自身状态提供，`emberAfExternalAttributeReadCallback` 只
  在注册时用于读取 min/max 默认值。更新温度必须调用：
  `chip::app::Clusters::TemperatureMeasurement::SetMeasuredValue(endpoint, value)`
  （见 [BridgeApplication.cpp](src/app/BridgeApplication.cpp) 的 `CreateDevice`
  与 `AddDiscoveredDevices`）。只调用 `MatterReportingAttributeChangeCallback`
  会导致 reads/订阅返回 `null`。
- **OnOff 在本 SDK 仍走 ember + 外部属性存储。**
  读走 `emberAfExternalAttributeReadCallback`；控制器写/命令经
  `emberAfExternalAttributeWriteCallback` 转发到 `IDeviceAdapter::SetOnOff`。
- **Descriptor 是 code-driven。**
  device type list 来自 `emberAfSetDynamicEndpointWithEpUniqueId(...)` 的参数，
  不要试图用外部属性回调覆盖。
- **Bridged Device Basic Information（BDBI）是 code-driven。**
  每个动态 endpoint 在 `BridgeEndpointManager::AddDevice()` 中注册；
  `SetReachable()` 会自行上报 `reachable` 与事件，应用层不要重复上报。
- **外部属性回调的 `maxReadLength` 必须严格匹配。**
  OnOff = 1 字节，温度值 = 2 字节；不匹配直接返回 `Failure`。
- **动态 endpoint 分配逻辑在 `BridgeEndpointManager` 内，区间有限。**
  新增/删除设备时注意 endpoint ID 回绕逻辑，不要越过 3..18。
- **控制台模拟需要前台 TTY**；按键在行缓冲模式下通常需要回车确认。
- **重新配网**必须换新的 `--KVS` 路径（或删除旧 KVS），否则会因已入网而失败。

---

## 6. 常见任务流程

### 6.1 接入真实 DUWI 通讯适配器

1. 在 `src/adapters/` 新建 `DuwiXxxAdapter`，实现
   [IDeviceAdapter.h](src/adapters/IDeviceAdapter.h)。
2. 在 [BridgeApplication::Init](src/app/BridgeApplication.cpp) 中替换
   `MockDeviceAdapter`。
3. 在 [src/app/BUILD.gn](src/app/BUILD.gn) 的 `sources` 加文件；第三方库加到
   `deps` / `libs`。
4. 为协议状态机补 `tests/` 单测；协议联调用 chip-tool 或 Python 测试。
5. 检查：`Name()` 与 `physicalId` 跨重启稳定；回调线程安全；失败返回正确的
   `AdapterStatus`。

详见 [docs/ADAPTER_GUIDE.md](docs/ADAPTER_GUIDE.md)。

### 6.2 新增设备类型

1. `src/model/DeviceTypes.h`：增加 `DeviceKind` 与对应状态字段。
2. `src/bridge/BridgeEndpointManager.cpp`：增加动态属性表、cluster 表、
   `DECLARE_DYNAMIC_ENDPOINT`，并在 `AddDevice()` 中映射 endpoint type。
3. `src/bridge/BridgedDevice.h/.cpp`：增加设备类或扩展现有类。
4. `src/bridge/BridgedAttributeAccess.cpp`：只有当该 cluster 不是 code-driven
   时才需要外部属性读写回调。
5. `zap/dxh-bridge.zap`：**在 endpoint 2 中加入新 cluster**，重新生成 `.matter`。
6. `src/app/BridgeApplication.cpp`：`CreateDevice()` 中构造对象；
   若 cluster 是 code-driven，注意初始化顺序与 setter 调用。
7. 增加单测 + 更新 README/docs 中的 endpoint 号。

### 6.3 新增 cluster

- 优先使用 SDK 的 code-driven cluster（`DefaultServerCluster`）。
- 若 cluster 是 code-driven，读取会走已注册的 `ServerClusterInterface`，外部
  属性回调**不会**生效（参考第 5 节的 TemperatureMeasurement）。
- 无论哪种实现，都必须把 cluster 加到 ZAP endpoint 2。
- 重新生成 `.matter` 后执行 `./scripts/build.sh --clean`，避免 codegen 缓存。

### 6.4 修改数据模型

1. 编辑 `zap/dxh-bridge.zap`（推荐 ZAP GUI）。
2. 重新生成 `zap/dxh-bridge.matter`（步骤见 [zap/README.md](zap/README.md)）。
3. `./scripts/build.sh --clean`。
4. 同步检查 `BridgeEndpointManager.cpp` 的属性/cluster 声明与文档 endpoint 号。

### 6.5 升级 SDK

1. 在子模块中切换 tag/commit，并更新父仓库索引。
2. `./scripts/build.sh --clean`。
3. 重点复核：BDBI cluster 的 `MutableData`/`FixedData`/`Context`、
   `emberAfSetDynamicEndpointWithEpUniqueId` 签名、`DECLARE_DYNAMIC_*` 宏、
   `ServerClusterInterfaceRegistry::Register/Unregister`。
4. 复核哪些 cluster 变成了 code-driven（读取路径会变化）。

---

## 7. 验证与验收

提交前至少执行：

```bash
./scripts/test.sh        # 必须 0 failure
./scripts/build.sh       # 必须成功
./scripts/run.sh         # 观察启动日志
```

启动日志应包含（Mock 配置下）：

```text
Adapter "duwi" discovered 3 device(s)
Added on-off-light device "DUWI Lamp 1" at endpoint 3 (slot 0)
Added on-off-light device "DUWI Lamp 2" at endpoint 4 (slot 1)
Added temperature-sensor device "DUWI Temperature 1" at endpoint 5 (slot 2)
DUWI Matter bridge ready. ...
```

chip-tool 验证（endpoint 号按上表）：

```bash
CT=third_party/connectedhomeip/out/darwin-arm64-chip-tool-clang/chip-tool
# Linux 主机: third_party/connectedhomeip/out/linux-x64-chip-tool-clang/chip-tool

$CT pairing onnetwork 1 20202021
$CT descriptor read parts-list 1 0
$CT descriptor read device-type-list 1 3
$CT onoff read on-off 1 3
$CT onoff toggle 1 3
$CT temperaturemeasurement read measured-value 1 5
$CT bridgeddevicebasicinformation read reachable 1 5
$CT bridgeddevicebasicinformation subscribe reachable 1 5 1 10
```

验收清单：

- [ ] `./scripts/test.sh` 通过。
- [ ] `./scripts/build.sh` 通过。
- [ ] 改动 ZAP 时，`zap/dxh-bridge.matter` 已重新生成并一起提交。
- [ ] `src/model`、`src/adapters`、`src/bridge/StableId.*` 仍无 SDK 头文件。
- [ ] 已有设备的 UniqueID 未变化。
- [ ] 动态 endpoint 增删后仍落在 3..18，且没有重复 endpoint。
- [ ] 控制器读/写/订阅行为与适配器状态一致。
- [ ] README/docs 中的 endpoint 号与实际一致。
- [ ] `./scripts/format.sh` 已执行（本机需安装 `clang-format`）。

排障提示：

- `PW_ENVIRONMENT_ROOT: unbound variable`：`build.sh` 已处理；自定义脚本先 `set +u`。
- 修改 ZAP 后行为未变化：确认 `.matter` 已更新，并做一次 `--clean` 构建。
- 读属性返回 `Failure`：检查 `maxReadLength`、外部属性回调、以及该 cluster
  是否已变成 code-driven。
- 若在本机遇到 chip-tool 链接失败（`ld64.lld ... malformed TAPI file`），
  这是 SDK/pigweed 工具链与 CommandLineTools SDK 的兼容问题，不是本工程代码问题；
  此时只能做构建/启动验证，无法完成 controller 侧联调。

---

## 8. 已知限制（不要假装已实现）

- 只在启动时 `Discover()` 一次；**不支持增量发现/移除设备**
  （见 [docs/ADAPTER_GUIDE.md](docs/ADAPTER_GUIDE.md) 第 7 节）。
- `BridgeApplication` 的设备索引键是 `physicalId`；多适配器并存需要改成
  `(adapterName, physicalId)`。
- 未启用 Actions / Rooms / Composed Device。
- 生产 VID/PID、DAC/PAA 证书、工厂数据仍使用 CSA 测试值。
- 真实 DUWI 通讯适配器尚未实现。
- CI 的 `core-tests` 只覆盖 SDK 无关单测；完整 Matter 构建在 self-hosted
  runner 上执行。

## 9. 不要做的事

- 不要修改 SDK 子模块中的已跟踪文件，也不要提交其中的软链接。
- 不要把 `out/`、`logs/`、`*.kvs`、`.env` 提交进仓库。
- `.vscode/` 下的 `settings.json`、`launch.json`、`tasks.json`、`extensions.json`
  是团队共享配置（只使用 `${workspaceFolder}` 和 `/tmp` 等不含机器信息的值），
  应当提交；个人定制放用户级设置，不要写进仓库配置。
- 不要在 `src/model`、`src/adapters`、`src/bridge/StableId.*` 中引入 SDK 头文件。
- 不要手改生成代码或只改 `.zap` 而忘记 `.matter`。
- 不要把设备状态直接写进 Matter 属性：先经过适配器。
- 不要在非 Matter 线程访问 cluster / 属性存储。
- 不要改变已发布设备的 `MakeStableUniqueId` 输入或算法。

## 10. Skills 与外部文档

`.claude/skills/` 下的 `chip-tool-testing`、`python-test-runner`、
`zap-matter-analysis` 是从 SDK 仓库软链过来的，命令与路径按 **SDK 为 cwd**
编写。在本工程使用时必须翻译路径：

| skill 中的写法 | 本工程实际位置 |
|---|---|
| `scripts/run_in_build_env.sh`、`scripts/build/build_examples.py`、`scripts/activate.sh` | `third_party/connectedhomeip/scripts/...` |
| `src/python_testing/`、`src/app/tests/suites/`、SDK 的 `out/` | `third_party/connectedhomeip/...` |
| SDK 的 `build_examples.py --target linux-x64-*` | 本工程用 `./scripts/build.sh`，不要套用 |

skill 里通用的推理（用 jq/grep 查 ZAP、chip-tool 交互流程、认证用例跑法）适用，
具体命令和路径不能照抄。`code-driven-cluster-*` 系列 skill 面向 SDK 内部
cluster 开发，与本工程 out-of-tree 定位不符，不要套用。
