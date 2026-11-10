# GProf port

维护摘要：active，2026-11-10。独立工程内的私有上游封装；核对本地来源和构建入口，专项端口及真机行为未在本次迁移中验收。

## 给人读

可选诊断端口，不负责设备上传或启动独立工作线程。默认固件未启用。

来源：https://github.com/espressif/esp-iot-solution，固定提交 `4962410c80a51cd5822da1d8d2041eb4285265f1`。许可证：Apache-2.0。版本、输入摘要及原始路径以 [UPSTREAM.json](UPSTREAM.json) 为准，上游文件和版权声明保留在 `upstream/`。构建入口为 [CMake 声明](sources.cmake)，使用本仓库的校验工具；上游更新时同步复查校验清单和消费者。

端口派生工具使用 Node.js 22+，仅在选择相应诊断配置时执行；派生输出进入构建目录，不改写上游原件。

<!-- AI-CONTEXT:START -->
## 给 AI 读

```yaml
kind: component-readme
status: active
last_verified: 2026-11-10
revision_reason: 移除独立工程之外的工具及证据链接
source_of_truth: [UPSTREAM.json, sources.cmake]
watch_paths: [UPSTREAM.json, sources.cmake]
evidence: 来源与构建入口已检查；专项测试及设备验收未执行
```

私有 vendor 不反向依赖产品，不拥有额外服务或生命周期。保持上游原件、许可证、端口派生和校验清单分离；升级需复查真实消费者。
<!-- AI-CONTEXT:END -->
