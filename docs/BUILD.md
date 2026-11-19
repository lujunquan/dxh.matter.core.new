# 构建与运行

## 1. 前置条件

- macOS 13+（Apple Silicon / Intel）或 Linux x86_64/aarch64
- 一个已初始化、可成功构建的 `connectedhomeip` 工作区（本工程默认引用
  `../connectedhomeip`）
- 系统工具：`git`、`python3`、`bash`、`clang`
- SDK 的 `scripts/activate.sh` 会提供 `gn`、`ninja`、`zap` 和 Pigweed Python 环境

首次使用 SDK 时，先确认 SDK 自身可以构建：

```bash
cd ../connectedhomeip
scripts/run_in_build_env.sh "./scripts/build/build_examples.py --target linux-x64-tests-clang --quiet build"
```

macOS 使用 `darwin-arm64-...` 或 `darwin-x64-...` 目标。

---

## 2. 链接 SDK

```bash
export MATTER_SDK_ROOT=/absolute/path/to/connectedhomeip   # 可选，默认 ../connectedhomeip
./scripts/setup.sh
```

脚本会创建：

| 软链接 | 指向 | 用途 |
|---|---|---|
| `third_party/connectedhomeip` | SDK 根目录 | `chip_root` |
| `build_overrides` | `SDK/examples/build_overrides` | `build_root` / `chip_root` / 依赖路径 |
| `SDK/examples/dxh-matter-core` | 本工程根目录 | 满足 GN 可见性规则 |

### GN 可见性说明

Matter SDK 中部分 codegen 目标（例如 `src/app:required-privileges`）的
`visibility` 只允许 `${chip_root}/examples/*` 依赖。out-of-tree 工程的数据模型
因此必须通过 SDK 的 examples 命名空间引用：

```gn
"${chip_root}/examples/dxh-matter-core/zap:dxh-bridge-data-model"
```

`setup.sh` 创建的 `SDK/examples/dxh-matter-core` 软链接使该路径成立。SDK 的
源码和已跟踪文件不受影响；该软链接在 SDK 中显示为未跟踪项。

移除所有生成的软链接：

```bash
./scripts/setup.sh --clean
```

---

## 3. 构建

```bash
./scripts/build.sh
```

等价于：

```bash
SDK_ROOT="$(cd third_party/connectedhomeip && pwd -P)"
source "${SDK_ROOT}/scripts/activate.sh"
gn gen out/debug
ninja -C out/debug
```

常用变体：

```bash
# 干净构建
./scripts/build.sh --clean

# ASan
DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' ./scripts/build.sh

# 只编译指定目标
./scripts/build.sh //src/app:dxh-matter-bridge

# 查看编译命令
ninja -C out/debug -v
./scripts/build.sh -v
```

产物：`out/debug/dxh-matter-bridge`。

> macOS 上构建的是原生 arm64/x86_64 可执行文件，使用 Darwin 平台层；
> Linux 上使用 Linux 平台层（BlueZ BLE）。两者命令行参数基本一致。

---

## 4. 运行与配网

```bash
./scripts/run.sh
# 等价于：
# out/debug/dxh-matter-bridge --discriminator 1234 --passcode 20202021 \
#   --KVS /tmp/dxh-matter-bridge.kvs
```

自定义参数：

```bash
./scripts/run.sh \
  --discriminator 1234 \
  --passcode 20202021 \
  --KVS /tmp/dxh-$(date +%s).kvs \
  --trace-to json:/tmp/dxh.trace.json
```

> 每次重新测试配网时使用新的 KVS 路径，否则设备已入网，配对会失败。

编译 chip-tool（一次）：

```bash
cd ../connectedhomeip
scripts/run_in_build_env.sh \
  "./scripts/build/build_examples.py --target darwin-arm64-chip-tool-clang --quiet build"
```

配网与验证：

```bash
CT=../connectedhomeip/out/darwin-arm64-chip-tool-clang/chip-tool

$CT pairing onnetwork 1 20202021
$CT descriptor read parts-list 1 0
$CT onoff read on-off 1 3
$CT onoff toggle 1 3
$CT temperaturemeasurement read measured-value 1 4
$CT bridgeddevicebasicinformation read reachable 1 4
```

macOS 使用 on-network 配网。`--ble-controller` 仅在 SDK 的 Linux/BlueZ 平台层
可用。

---

## 5. 调试与排查

| 手段 | 命令 |
|---|---|
| 交互模型 trace | `./scripts/run.sh --trace-to json:/tmp/dxh.trace.json` |
| lldb | `lldb out/debug/dxh-matter-bridge` |
| ASan | `DXH_OUT_DIR=out/asan DXH_GN_ARGS='is_asan=true' ./scripts/build.sh` |
| 编译命令 | `ninja -C out/debug -v` |
| 生成代码位置 | `out/debug/gen/third_party/connectedhomeip/examples/dxh-matter-core/zap/zapgen/` |

常见问题：

| 现象 | 原因 | 处理 |
|---|---|---|
| `PW_ENVIRONMENT_ROOT: unbound variable` | SDK 的 activate.sh 不支持 `set -u` | 已由 `build.sh` 处理；自定义脚本中先 `set +u` |
| `python: No such file or directory` | activate.sh 未真正激活（常见于通过软链接路径 source） | 使用解析后的真实路径：`$(cd third_party/connectedhomeip && pwd -P)` |
| `Environment directory moved` | 通过 `third_party/connectedhomeip` 软链接 source activate.sh | 同上，使用真实路径 |
| `Invalid value for '--idl'` | `zap/*.matter` 不存在 | 按 `zap/README.md` 重新生成 |
| `Dependency not allowed` / visibility 错误 | 数据模型没有通过 `${chip_root}/examples/...` 引用 | 确认 `setup.sh` 软链接存在，且 BUILD.gn 使用 examples 路径 |
| 配网失败 | KVS 已入网 / passcode 不匹配 | 使用新的 `--KVS` 路径 |
| 动态 endpoint 不存在 | 槽位耗尽或 ZAP placeholder 缺少 cluster | 看日志 `No free dynamic endpoint slot` / 补 ZAP |
| 读属性返回 `Failure` | 外部属性回调未处理，或 `maxReadLength` 与声明大小不符 | 在 `BridgedAttributeAccess.cpp` 下断点 |

---

## 6. 升级 SDK

```bash
cd ../connectedhomeip
git fetch --tags
git checkout <tag-or-commit>

cd ../dxh.matter.core.new
./scripts/build.sh --clean
```

升级后重点验证：

1. `BridgedDevice.cpp` 中 BDBI cluster 的 `MutableData` / `FixedData` / `Context`
2. `emberAfSetDynamicEndpointWithEpUniqueID` 的参数
3. `DECLARE_DYNAMIC_*` 宏生成的属性元数据
4. `zap/` 重新生成后 cluster revision 是否变化

---

## 7. 清理

```bash
# 只删除构建产物
rm -rf out/

# 同时移除指向 SDK 的软链接（包括 SDK 中的 examples 软链接）
./scripts/setup.sh --clean
```