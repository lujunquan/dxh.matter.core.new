# 数据模型（ZAP）

## 文件

| 文件 | 说明 |
|---|---|
| `dxh-bridge.zap` | 唯一数据模型源文件，用 ZAP 工具编辑 |
| `dxh-bridge.matter` | 由 ZAP 生成的 Matter IDL，**必须提交**；GN codegen 在配置阶段读取它 |
| `BUILD.gn` | `chip_data_model` 目标，把 `.zap` 接入 codegen |

`.matter` 不是构建产物，而是配置阶段的输入（`chip_codegen` 通过
`codegen_paths.py --idl` 读取）。修改 `.zap` 后必须同步重新生成 `.matter`。

## Endpoint 布局

| Endpoint | 设备类型 | 用途 |
|---|---|---|
| 0 | `ma_rootdevice` | Matter root device |
| 1 | `ma_aggregator` | 桥接设备的 parent endpoint（Identify + Descriptor） |
| 2 | `ma_dimmablelight` | Placeholder：运行时禁用，集中声明动态 endpoint 使用的 cluster |

动态 endpoint 从 3 开始：

- On/Off Light：`OnOff` + `Descriptor` + 运行时注册的 BDBI
- Temperature Sensor：`TemperatureMeasurement` + `Descriptor` + 运行时注册的 BDBI

## 修改流程

1. 编辑 `dxh-bridge.zap`（推荐 ZAP GUI）：

   ```bash
   third_party/connectedhomeip/scripts/tools/zap/run_zaptool.sh zap/dxh-bridge.zap
   ```

2. 重新生成 `.matter`：

   ```bash
   third_party/connectedhomeip/scripts/run_in_build_env.sh \
     "python3 third_party/connectedhomeip/scripts/tools/zap/generate.py \
      $(pwd)/zap/dxh-bridge.zap"
   ```

3. 重新构建：

   ```bash
   ./scripts/build.sh
   ```

## 关键约束

- **动态 endpoint 用到的每个 cluster 都必须出现在 endpoint 2。**
  `DECLARE_DYNAMIC_CLUSTER` 只提供属性元数据；cluster 的实现与回调注册由
  codegen 根据固定 endpoint 生成。漏加会导致"endpoint 存在但读写失败"。
- 增加/删除属性时，同步修改 `src/bridge/BridgeEndpointManager.cpp` 中的
  `DECLARE_DYNAMIC_ATTRIBUTE_*` 列表。
- 不要手改 `out/.../zap-generated/` 或 `zapgen/` 下的生成代码。
- 当前数据模型**不包含 Actions**。如需房间/场景，在 endpoint 1 重新加入
  Actions cluster，并在 `src/bridge` 中实现 `Actions::Delegate`。
- ZAP 文件中的 `package.path` 指向 `../third_party/connectedhomeip/src/app/zap-templates/...`，
  依赖 `third_party/connectedhomeip` 子模块已初始化。切换分支或升级 SDK 后
  重新运行 `./scripts/setup.sh` 与 `./scripts/build.sh --clean`。

## 常见问题

| 现象 | 处理 |
|---|---|
| `codegen_paths.py ... does not exist` | `.matter` 缺失或文件名不匹配，按上文重新生成 |
| 修改 ZAP 后行为未变化 | 确认 `.matter` 已更新，并执行 `./scripts/build.sh --clean` |
| `zap_gen.lock` 残留 | 删除 `out/debug/zap_gen.lock` 后重试 |