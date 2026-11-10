# TrueType rasterizer

维护摘要：active，2026-11-10。独立工程内的私有上游封装；核对本地来源和构建入口，专项端口及真机行为未在本次迁移中验收。

## 给人读

TrueType 原件只供 text_raster 内部使用；字库加载、缓存和显示生命周期由消费者负责。

来源：https://github.com/lvgl/lvgl，固定提交 `80ca777e37a2b176770726a02e07a6fb79ef0b39`。许可证：MIT OR Unlicense。版本、输入摘要及原始路径以 [UPSTREAM.json](UPSTREAM.json) 为准，上游文件和版权声明保留在 `upstream/`。构建入口为 [CMake 声明](../../CMakeLists.txt)，使用本仓库的校验工具；上游更新时同步复查校验清单和消费者。

<!-- AI-CONTEXT:START -->
## 给 AI 读

```yaml
kind: component-readme
status: active
last_verified: 2026-11-10
revision_reason: 移除独立工程之外的工具及证据链接
source_of_truth: [UPSTREAM.json, ../../CMakeLists.txt]
watch_paths: [UPSTREAM.json, ../../CMakeLists.txt]
evidence: 来源与构建入口已检查；专项测试及设备验收未执行
```

私有 vendor 不反向依赖产品，不拥有额外服务或生命周期。保持上游原件、许可证、端口派生和校验清单分离；升级需复查真实消费者。
<!-- AI-CONTEXT:END -->
