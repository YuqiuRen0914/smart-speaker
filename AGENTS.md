# Smart Speaker 协作约定

先读 README.md、docs/architecture.md 和 docs/baseline-correction.md，再查 product.cmake 与组件公开契约。当前基线必须是用户此前确认的 **1.0.35 旧版交互 + 当前接线 + 后续相关修复**；不要再次用主仓库最新版替换产品。

- 独立工程仅依赖自身源码、ESP-IDF 6.0.1 和公开第三方组件，禁止恢复外部 LunarOS SDK 或父目录发现。
- 产品接线、页面和组合留在 main/；LunarOS、Pet、BSP 和驱动保留原版所有者与生命周期，下层不反向引用产品，不另建泛化 shared/common 层。
- 组件由 cmake/components.cmake 显式声明。该版 LunarOS 是单一 IDF 组件，内部子系统由 lunaros/src/subsystems.cmake 与 desktop/sources.cmake 选择；不混入后来版本组件契约。
- 常规构建使用 `python tools/build.py --jobs 2`，回报任务 UUID；通过 list/status/cancel/resume 控制，不直接调用 idf.py/ninja。取消等待受管进程退出。
- 构建仅保存产物。烧录、OTA、上传与发布分别按授权执行，核对实体硬件、设备身份、镜像摘要和分区；构建与 OTA 通过不能代替屏幕、声音及负载验收。
- 不导入其他产品树、原仓库历史、私密凭据、设备日志或缓存。保留用户工作区修改；源基线和许可证见 docs/extraction-manifest.json、THIRD_PARTY_NOTICES.md。
- 文档对照当前源码，历史证据保留当时条件与结论；版本不能为了掩盖不兼容随意改写。

验证：`node test/run_host_tests.mjs`、`python -m unittest discover -s tools/tests -v`、`python tools/check-project.py --build-dir .build/firmware`。主机套件另需 Node.js 与 C++ 编译器，固件构建不需要 Node.js。

## 智慧终端开发规划与 AI 准则

后续功能开发在上述基线阅读之后，继续读 [开发文档入口](docs/development/README.md) 与 [AI 开发准则](docs/development/04-ai-development-rules.md)，再读当前阶段对应的产品规格、技术方案和验收项。当前源码事实与未来规划必须区分；规划不代表已实现。

- 首发目标和已确认交互见 [产品规格](docs/development/01-product-spec.md)；采用 AmoledOS 的纵向条目 UI，按计划逐步演进当前产品页面，不能用更换整套 OS 或基线实现。
- 按 [实施计划](docs/development/03-implementation-plan.md) 交付完整切片，优先查找并复用现有所有者。禁止重复建设播放器、LVGL 初始化、曲库、路由或多份可写业务状态。
- 不做无关重构、全仓格式化或工作区清理；接口、持久化、并发与物理行为变化必须说明边界和验证证据。不要用占位页、假数据、入队即成功或编译通过宣称功能完成。
- 验收按 [验收清单](docs/development/05-acceptance.md) 分别记录主机、构建、真机和联合负载结果。缺条件可完成独立工作，但未验证项必须明确，不虚构通过。
