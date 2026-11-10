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
