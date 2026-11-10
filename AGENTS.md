# Smart Speaker 协作约定

先读 README.md、docs/architecture.md，再查 product.cmake 与相关组件的公开契约。此项目必须仅靠自身源码、ESP-IDF 6.1.0 和公开第三方依赖构建，不能恢复外部 LunarOS SDK 或父目录仓库发现。

- 产品硬件装配、页面和策略留在 main/。OS、App、Service、通用组件和设备驱动保留各自所有者，不反向引用产品源码，不另建泛化 shared/common 层。
- 组件集合由 cmake/components.cmake 显式声明；OS 源码继续由 lunaros/source_ownership.json 归属。新增能力先找真实所有者，不复制领域状态和资源生命周期。
- 常规固件构建使用 `python tools/build.py --jobs 2`，回报任务 UUID。通过 list/status/cancel/resume 查询和控制；不要直接调用 idf.py/ninja 绕过记录与目录锁。取消必须等待受管进程退出。
- 构建只生成产物。烧录、OTA、上传和发布分别按用户授权执行，并核对设备硬件、固件摘要与分区；构建成功不能写成真机验收通过。
- 上游源码、许可证及摘要保持完整；派生文件写入构建目录，随仓生成文件要保留输入、生成器及校验清单。
- 不纳入其他产品、原仓库 Git 历史、私有凭据、机器配置、设备日志和构建缓存。保留用户工作区的其他修改。
- 修改 README 或运行说明时，对照当前源码写清依赖、命令和核验范围；源码、主机测试、固件构建、设备操作及发布分别记录，历史证据保留原条件。

验证入口：`node test/run_host_tests.mjs`、`python -m unittest discover -s tools/tests -v`、`python tools/check-project.py`；涉及构建时增加带 --build-dir 的路径审计。主机套件另需 Node.js 22+ 与 C++ 编译器；默认固件构建不需要 Node.js。
