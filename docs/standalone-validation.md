# 独立构建验证

> 历史记录：本文核验的是当时误选的 1.2.5 新桌面基线，不代表用户要求的旧版交互已正确迁移。当前独立项目已修正为 1.0.35，见 [基线修正记录](baseline-correction.md)。

日期：2026-11-10。对象：smart-speaker 1.2.5，ESP32-S3 N16R8 默认配置，ESP-IDF 6.1.0 / Xtensa GCC 15.2.0，macOS，构建并发 2。核验范围为源码闭包、主机行为、默认固件编译与产物；未烧录、未 OTA、未真机验收、未发布。

## 隔离条件

从独立项目复制一份临时源码快照，排除 `.git/`、所有构建输出和 `managed_components/`。用 macOS sandbox 明确拒绝读取原 LunarEdu 硬件仓库，并清除旧外部 SDK / 第三方源码路径环境变量。ESP-IDF Component Manager 根据本工程清单和锁获取公开组件，允许使用其正常的公共下载缓存。

快照没有 Git 历史；下载源码压缩包同样不要求原仓库历史。构建中的 Git 探测不能取得历史，但不影响以 `version.txt` 提供的版本编译。重复配置时额外禁止执行 Node，确认默认固件构建仅使用 ESP-IDF 工具及 Python。

## 构建结果

| 任务 UUID | 范围 | 结果 |
| --- | --- | --- |
| `706b6005-cd36-4730-8161-7962b4684ffa` | 首次隔离全量构建 | 暴露原仓库默认选入配网服务的隐式依赖；已在产品声明中补全 |
| `5c8aba16-b1b1-4249-baeb-8690c53046c7` | 补全后的全量构建 | 通过，保留旧任务、缓存和失败证据 |
| `221d8ceb-da06-4937-be23-50075e6cf28b` | 最终源码再次配置与增量构建；禁止 Node | 通过，配置、编译、链接和校验均完成 |

最终应用镜像：`lunaredu-smart-speaker.bin`，4,554,416 bytes，SHA-256：

```text
8e25be1694affc845af3ca8488f32fb8f0be9882cca619c5a8dcdebde4596bf6
```

应用小于 6 MiB 槽位，剩余约 28%。同时生成 bootloader、16 MiB 分区表、OTA 初始数据和字体 LittleFS 镜像。未通过删除旧输出、改产品版本或修改第三方校验值掩盖不兼容。

原始监控记录在临时 checkout 的 `.build/tasks/`；本机留存副本及产物在项目 `.build/standalone-validation/`，`origin.json` 记录实际快照位置。副本中的相对产物路径仍以原快照为基准。上述目录均忽略提交。隔离环境下进程资源采样不可用，记录为未知；没有据此报告零 CPU/RSS。

## 路径、资源与回归

- `tools/check-project.py --build-dir .build/firmware`：核对 48 个本地组件身份与 2,469 条编译源文件路径，全部属于独立 checkout 或 ESP-IDF；OS 源码所有权和 ESP-NOW 生成端口摘要通过。
- LVGL、MicroPython、WAMR 的上游字节在重复配置后仍通过既有校验清单；WAMR 的版本头生成只发生在构建副本。
- MicroPython Python QSTR 生成器与原 JS 生成器输出逐字节一致，38,449 bytes；默认构建无需运行 JS。
- 字体发布包与原字体压缩器输出一致，1,419,586 bytes；LRZ1 头、解压、manifest 长度/摘要和许可证内容已核对。
- 在禁止读取原仓库的环境运行 `node test/run_host_tests.mjs`：DSP、I²S 同步、歌词、双输出生命周期、上传、曲库操作、播放协调、退出、调度、传感快照及网页回归全部通过。
- `python -m unittest discover -s tools/tests -v`：3 项构建任务测试通过，覆盖成功/失败与增量恢复、目录锁互斥、安全取消，以及阶段/Ninja 计数记录。

构建仍有继承的 ESP-IDF Kconfig 重复定义提示、部分未使用函数及工具链链接提示；本次未把它们当作编译失败，也未将构建通过解释为相关运行行为已验收。可选 GProf/Insights 配置、Windows、其他 ESP-IDF 版本和其他硬件未在本次验证。
