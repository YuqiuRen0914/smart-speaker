# Smart Speaker

维护摘要：active，2026-11-10。以用户此前回退并确认旧版界面的 **1.0.35** 为独立开发基线；保留当前接线及旋钮修复。此前误抽离的 1.2.5 不再是当前基线。核验分层见 [基线修正记录](docs/baseline-correction.md)，旧构建记录保留原结论。

## 给人读

面向 ESP32-S3 N16R8 的桌面音箱，采用双 MAX98357、240×240 ST7789、SD 卡和旋钮。当前产品保留旧版 Pebble 形象、音乐/歌词界面、本地菜单、触摸及网页曲库和 EQ。版本以 [version.txt](version.txt) 为准。

本仓库包含该版本实际使用的 LunarOS、Pet 应用及驱动源码；无需外部 LunarOS SDK 或访问原仓库。保留原版所有权与公开接口，详见 [架构边界](docs/architecture.md)。

### 编译

安装 **ESP-IDF 6.0.1** 及其官方工具链/Python 环境：

```sh
. /path/to/esp-idf/export.sh
cd /path/to/smart-speaker
python tools/build.py --jobs 2
```

Component Manager 根据 [dependencies.lock](dependencies.lock) 和 [版本约束](cmake/dependency-constraints.txt) 获取公开第三方组件，无需私有令牌；默认固件编译不需要 Node.js。来源及许可见 [第三方声明](THIRD_PARTY_NOTICES.md)。

构建任务 UUID、阶段、真实 Ninja 计数和可用的资源采样保存在 `.build/tasks/`；采样不可用记为未知。入口支持 macOS/Linux，Windows 尚未适配验证。

```sh
python tools/build.py list
python tools/build.py status TASK_UUID
python tools/build.py cancel TASK_UUID
python tools/build.py resume TASK_UUID --jobs 2
```

取消等待受管进程退出，恢复保留旧记录和增量缓存。产物位于 `.build/firmware/`，应用名仍为 `lunaredu-smart-speaker.bin`。构建仅保存产物；烧录和 OTA 分别授权。新设备还需安装字体存储镜像，应用 OTA 不初始化空白字体分区。

### 硬件与操作

引脚以 [sdkconfig.defaults](sdkconfig.defaults) 为真源，分区为 [16 MiB 双槽布局](partitions/lunaros-16m-v2.csv)。

| 外设 | GPIO |
| --- | --- |
| I²S0 WS / BCLK / DOUT | 14 / 47 / 1 |
| I²S1 WS / BCLK / DOUT | 17 / 16 / 15 |
| LCD MOSI / SCLK / CS / DC | 45 / 3 / 0 / 46 |
| 触摸 SDA / SCL / INT | 43 / 44 / 48 |
| 旋钮 A / B | 12 / 11，反向 |
| 左键 / 右键 | 8 / 7，低电平有效 |
| SD CMD / CLK / D0 | 38 / 39 / 40 |
| MPU6050 SDA / SCL | 41 / 42 |

MP3 放在 SD 的 `music/`，同名 `.lrc` 提供歌词。旋钮在音乐页面调音量，在本地菜单导航。沿用原版长按和触摸路由；它们并非后来标准桌面的应用导航。历史语音适配代码保留，不据此宣称当前硬件具备麦克风或云端语音已验收。

### 验证与规划

首次构建并获取公开组件后，可运行主机回归（另需 Node.js 和 C++ 编译器）：

```sh
node test/run_host_tests.mjs
python -m unittest discover -s tools/tests -v
python tools/check-project.py --build-dir .build/firmware
```

源码、主机回归、固件构建、OTA 和实体屏幕/音频验收分别记录。壁纸、手表式应用滚动、封面、游戏、AI 看板娘、番茄钟与更多 EQ 仍是后续规划。第一方代码的开源许可证待所有者选择，第三方许可证各自适用。

<!-- AI-CONTEXT:START -->
## 给 AI 读

```yaml
readme:
  kind: firmware-readme
  status: active
  last_verified: 2026-11-10
  revision_reason: 修正误选的新桌面版基线，恢复用户已确认的旧版及当前接线
  source_of_truth:
    - CMakeLists.txt
    - cmake/components.cmake
    - product.cmake
    - sdkconfig.defaults
    - version.txt
    - main/**
    - lunaros/src/subsystems.cmake
  watch_paths:
    - main/**
    - cmake/**
    - lunaros/**
    - lunaros_apps/**
    - components_device/**
    - tools/**
    - test/**
  evidence:
    provenance: docs/extraction-manifest.json
    current_baseline: docs/baseline-correction.md
    historical_build: docs/standalone-validation.md
    device_acceptance: 用户确认旧版界面及本轮实体操作正常，范围见基线修正记录
    publication: 未执行
```

基线为 `f1587d4` 的 1.0.35 和历史工作区的外设排查修改，另适配旋钮输入合并、ISR 去抖和每轮处理量限制。不得再次用主仓库最新版替换产品交互。原生 LunarOS 在该版本是单一 IDF 组件，内部子系统边界继续由原版声明持有；不强塞后来 1.2.x 的组件拆分或桌面入口。产品组合、页面和接线留在 `main/`；依赖不反向引用产品。原仓库路径、其他产品树、Git 历史和设备私密证据不进入分发源码。
<!-- AI-CONTEXT:END -->
