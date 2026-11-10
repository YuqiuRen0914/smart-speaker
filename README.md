# Smart Speaker

维护摘要：active，2026-11-10。将智能音箱及其实际依赖抽为独立工程；构建入口只引用本目录与公开 ESP-IDF/组件注册表。验证范围见 [独立构建记录](docs/standalone-validation.md)，设备行为与发布分别验收。

## 给人读

面向 ESP32-S3 N16R8 的桌面音箱，使用 240×240 ST7789、SD 卡、旋钮和双 MAX98357。产品代码包含本地音乐、歌词、网页曲库管理、双 I²S 播放及 OTA 接口。版本以 [version.txt](version.txt) 为准。

本仓库已包含该产品所需的 LunarOS 源码，无需另找 LunarOS SDK、访问 LunarEdu 仓库或设置外部 SDK 路径。只抽取实际组件依赖，保留系统、服务、应用和驱动的所有权，详见 [架构边界](docs/architecture.md)。

### 编译

安装 **ESP-IDF 6.1.0** 及其官方工具链/Python 环境。在 macOS/Linux 上：

```sh
. /path/to/esp-idf/export.sh
cd /path/to/smart-speaker
python tools/build.py --jobs 2
```

首次构建由 ESP-IDF Component Manager 从公开的 [Espressif Component Registry](https://components.espressif.com/) 获取依赖；版本和摘要由 [dependencies.lock](dependencies.lock) 固定，[依赖约束](cmake/dependency-constraints.txt) 保持传递依赖版本一致。无需私有令牌。LVGL、MicroPython、WAMR 等固定上游源码及中文字库随工程提供，来源和许可证见 [第三方声明](THIRD_PARTY_NOTICES.md)。默认固件构建只使用 ESP-IDF 工具与 Python，不要求 Node.js。

构建任务输出 UUID；状态、日志、真实 Ninja 进度及可用的进程资源采样保存在 `.build/tasks/`。采样失败记为未知。常用操作：

```sh
python tools/build.py list
python tools/build.py status TASK_UUID
python tools/build.py cancel TASK_UUID
python tools/build.py resume TASK_UUID --jobs 2
```

取消等待受管进程退出后释放目录锁；恢复保留旧记录与增量缓存。产物在 `.build/firmware/`，包括 `lunaredu-smart-speaker.bin`、bootloader、分区表和 `filesystem.bin`。构建仅保存产物；烧录时还需确认设备并写入字体存储分区，单独 OTA 应用镜像不会初始化空白设备的字体。

监控入口当前支持 macOS/Linux；Windows 的该入口尚未适配和验证。可选 GProf/Insights 诊断端口的再生成工具需要 Node.js 22+，默认配置没有启用这些端口。

### 硬件

引脚真源为 [sdkconfig.defaults](sdkconfig.defaults)，分区为 [16 MiB 双槽布局](partitions/lunaros-16m-v2.csv)。

| 外设 | GPIO |
| --- | --- |
| I²S0 WS / BCLK / DOUT | 14 / 47 / 1 |
| I²S1 WS / BCLK / DOUT | 17 / 16 / 15 |
| LCD MOSI / SCLK / CS / DC | 45 / 3 / 0 / 46 |
| 触摸 SDA / SCL / INT | 43 / 44 / 48 |
| 旋钮 A / B | 12 / 11 |
| SD CMD / CLK / D0 | 38 / 39 / 40 |

这是配置声明，不能据此推断触摸控制器探测、音频、联网或实体显示已验收。本次抽离没有烧录或 OTA。

### 验证与后续开发

加载 ESP-IDF 环境并完成首次构建、获取公开组件后，可运行已有主机回归（另需 Node.js 22+ 和 C++ 编译器）：

```sh
node test/run_host_tests.mjs
python -m unittest discover -s tools/tests -v
python tools/check-project.py
```

带构建目录的审计还会核对组件和编译源文件的归属：

```sh
python tools/check-project.py --build-dir .build/firmware
```

壁纸、手表式滚动应用、封面音乐界面、游戏、AI 看板娘、番茄钟及更多 EQ 预设属于后续规划，未在本次迁移中实现。第一方代码的对外开源许可证尚待仓库所有者选择；保留的第三方许可证各自适用。

<!-- AI-CONTEXT:START -->
## 给 AI 读

```yaml
readme:
  kind: firmware-readme
  status: active
  last_verified: 2026-11-10
  revision_reason: 消除外部 LunarOS SDK 依赖并固定独立组件闭包
  source_of_truth:
    - CMakeLists.txt
    - cmake/components.cmake
    - product.cmake
    - sdkconfig.defaults
    - dependencies.lock
    - main/**
    - lunaros/source_ownership.json
    - tools/build.py
  watch_paths:
    - CMakeLists.txt
    - cmake/**
    - product.cmake
    - sdkconfig.defaults
    - main/**
    - lunaros/**/CMakeLists.txt
    - tools/**
    - test/**
  evidence:
    source_migration: docs/extraction-manifest.json
    validation: docs/standalone-validation.md
    device_acceptance: 未执行
    publication: 未发布
```

产品接线、页面及业务策略属于 `main/`；OS 机制、应用状态、服务资源及驱动分别由对应目录拥有。依赖方向、修改路由和验证要求见 [架构边界](docs/architecture.md) 与 [AGENTS.md](AGENTS.md)。`LUNAREDU_REPO_ROOT` 仅是已抽取组件的兼容变量，固定指向本仓库，不能恢复父目录发现或外部 SDK 注入。

保持 `version.txt`、分区、引脚、源文件所有权清单与组件清单对应。修改公共契约时同步复查消费者；构建、主机回归、烧录、真机验收和发布分别记录。不得导入其他产品、原仓库历史、设备凭据或本地构建目录。
<!-- AI-CONTEXT:END -->
