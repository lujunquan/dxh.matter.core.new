# dxh.matter.core — DUWI Matter Bridge Core

桥接器工程:把 DUWI 智能设备以 Matter 动态 endpoint 暴露给控制器。
总览见 [README.md](README.md),架构见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 硬性边界

- **SDK 只读**:`third_party/connectedhomeip` 是 out-of-tree git submodule,
  不修改其中任何已跟踪文件。需要 SDK 侧行为时,在本工程侧适配,
  而不是改 SDK 源码。
- 桥接逻辑只写在 `src/`,设备协议接入只通过 `src/adapters/IDeviceAdapter.h`
  这个稳定接缝。
- 数据模型的事实来源是 `zap/dxh-bridge.zap` + `zap/dxh-bridge.matter`;
  两者都是提交进仓库的**输入**,不是构建产物。

## 常用命令

```bash
./scripts/setup.sh          # 首次/切换分支后:建 SDK 软链接(幂等)
./scripts/build.sh          # 构建 out/debug
./scripts/build.sh --clean  # 先清 out/
./scripts/test.sh           # 宿主单测,不需要 SDK 环境
./scripts/format.sh         # clang-format / clang-tidy
```

构建参数通过环境变量传入:`DXH_OUT_DIR`、`DXH_GN_ARGS`(见
[docs/BUILD.md](docs/BUILD.md))。

## Skills(.claude/skills/)

`zap-matter-analysis`、`chip-tool-testing`、`python-test-runner` 三个 skill
是从 `third_party/connectedhomeip/.agents/skills/` 软链过来的,内容按
**SDK 仓库为 cwd** 编写。在本工程里使用时必须翻译路径:

| skill 里的写法 | 本工程实际位置 |
|---|---|
| `scripts/run_in_build_env.sh`、`scripts/build/build_examples.py`、`scripts/activate.sh` | `third_party/connectedhomeip/scripts/...` |
| `src/python_testing/`、`src/app/tests/suites/`、`out/`(SDK 产物) | `third_party/connectedhomeip/src/python_testing/` 等 |
| 构建 example / chip-tool | 本工程构建走 `scripts/build.sh`,不要套用 SDK 的 `build_examples.py --target linux-x64-*` |

skill 里的通用推理(jq/grep 查 ZAP、chip-tool 交互流程、认证用例的跑法)适用;
里面的具体命令行和绝对/相对路径不能照抄。

`code-driven-cluster-*` 系列的 skill 是给「在 `src/app/clusters` 里实现或迁移
cluster」用的,与本工程 out-of-tree 的定位冲突,故不安装。