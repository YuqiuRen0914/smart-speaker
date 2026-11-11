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

MP3 放在 SD 的 `music/`，同名 `.lrc` 提供歌词。首页旋钮调音量，左／右键短按上一首／下一首。菜单中左键确认、右键逐级返回；全局左键长按呼叫助手、右键长按回首页。长按为 800 ms，按住触发一次，松手不带出短按，不显示任何长按提示。具体云端会话和新交互仍需单独验收。

### 壁纸首页与应用

ST7789 初始化时先保持显示关闭，写完整黑帧并等待传输完成，再开启显示，避免开机动画前暴露未初始化的像素。当前接线没有可控的背光／复位引脚，固件接管之前的瞬间仍需断电重插观察。

显示和字体初始化完成后保留连续时钟开场，显示真实网络状态和版本。开场结束后进入唯一的静态壁纸首页：壁纸上常显日期、大号时钟、天气图标、温度和湿度读数，不重复显示天气名称、刷新状态和数据年龄；右下角是鲸鱼娘，从空白处左滑进入应用。旧记录中已选的壁纸会继续使用；未选图时显示黑底，已选资源缺失时提示去网页重选。独立表盘和宠物桌面已移除，时间管理及信息看板应用仍提供完整详情。

从壁纸空白处向左滑进入应用。音乐、助手、时间管理、信息看板、游戏娱乐、电脑协同和设置各占一屏。左右滑动或转旋钮切换应用，两端停止；点按或按左键进入，右键返回首页。本次开机保留应用选中项，重启从第一项开始。二级菜单上下滑动或转旋钮选择，左键／点按确认，右键返回上一层，横向滑动不导航。音乐页左键播放／暂停、旋钮调音量，番茄钟左键开始／暂停／继续，木鱼左键敲击，2048 只接受四向触摸。

当前按用户要求临时使用小智官方平台（`CONFIG_LUNAREDU_ASSISTANT_XIAOZHI=y`）：在网页助手区获取激活码，到小智控制台添加设备，不需要 Board 账号绑定。LunarLink 接入代码保留，可通过 `CONFIG_LUNAREDU_ASSISTANT_LUNARLINK` 构建选择。本地唤醒词仍为“你好小智”。协议、数据迁移、远控边界和验收状态见 [平台接入](docs/development/07-lunarlink-platform.md)。源码与构建验证不代表云端声音已通过真机验收。

助手从任意页面接受左键长按或本地唤醒词“你好小智”，松键不结束录音；自动判断说话结束，单轮回答后恢复原页面。回答中左键长按打断并沿用会话重新聆听；连接、聆听、思考和回答中右键短按取消，右键长按取消并回首页。对话期间暂停页面输入与音乐，按仍然有效的播放意图恢复；静音／停用策略继续有效。待机使用本地唤醒检测，触摸长按只服务宠物交互，不发起语音。

出厂 `filesystem.bin` 现在包含字体和现有 ESP-SR 的 `wn9s_nihaoxiaozhi` 模型。已有设备仅刷应用不会补装缺失模型；应通过现有 managed-assets 安装接口发布 `.build/firmware/storage-pool-seed/managed/model.bin` 及 `model.json` 中的摘要、版本和长度，保留用户文件。不要用覆盖整个文件系统来补模型。待机麦克风沿用音乐 I²S 时钟，在采集边界转换为 16 kHz；回答结束后恢复最新音乐采样率。实际唤醒率、播放音乐时的识别和联合负载仍待真机验收。

安装 `tools/resource-tools/requirements.txt` 中锁定的 Pillow 后，可将 JPEG、PNG 等输入裁切为中心对齐的 240×240 RGB BMP24：

```sh
python -m pip install -r tools/resource-tools/requirements.txt
python tools/resource-tools/prepare_wallpaper.py photo.jpg output.bmp
```

网页“静态壁纸”区域只接受该规范 BMP；上传到 SD 的 `wallpapers/` 后可预览、应用或删除。所选壁纸记录保存在设备 LittleFS，图像由显示所有者持有只读 RGB565 缓存。网页管理本身需要设备已连接局域网；缺 SD、文件损坏或保存失败时显示错误并保留之前选择。Pillow 仅供离线资源工具使用，不加入固件依赖。

白噪声入口位于音乐二层菜单与网页播放控制区，选择后替换当前音乐，暂停／继续／停止共用原播放器。白噪声按块生成并循环播放，无需 SD 音频文件，仍经过现有音量、EQ、限幅和双声道输出。构建需启用 `CONFIG_AUDIO_PLAYER_ENABLE_WAV=y`；已有 `.build/firmware/sdkconfig` 若保留旧的禁用值，只修改这一配置项后运行受管构建，不清理缓存或替换其它产品配置。音频切换、助手恢复与联合负载仍须按验收清单实测。

### 验证与规划

后续“个人桌面智慧终端”的目标、六模块范围、AmoledOS 接入方案、阶段任务与 AI 开发约束，统一见 [开发文档入口](docs/development/README.md)。这些文档描述未来目标；当前实现与历史验收仍以源码和基线修正记录为准。

新版鲸鱼娘支持 **114 段动画点播**、随机小动作、睡眠与叫醒、点击回应、拖拽甩抛、轻点后再按住拉动的弹弓、IMU 倾斜与摇动，以及小智会话表情和字幕。从角色身上按住即可向任意方向拖动，点角色回应，点壁纸空白不会控制音乐。从空白处左滑，在「助手 → 宠物动作」选动作或睡觉。已有到期提醒仍可在首页显示。

将完整动画包解压到 SD 卡根目录，确认有 `characters/dsh-pet/pet-v2.idx` 和旁边的 `frames-v2/`。首页自动读取已有动画包；包缺失或读取失败时显示内置鲸鱼娘待机图。可用含 libvpx-vp9 的主机 FFmpeg 重建；`--verify` 只读核对索引、全部分段和帧校验码：

```sh
python3 tools/resource-tools/prepare_dsh_pet.py .local/dsh-pet-sd-v2
python3 tools/resource-tools/prepare_dsh_pet.py --verify .local/dsh-pet-sd-v2
```

完整 WebM 和 GIF 保存在 [上游动画归档](components/lunaredu_character_assets/third_party/dsh-pet/README.md)，SD 派生包约 387 MB。安装、回退和验收范围见 [角色方案](docs/development/06-pet-character.md)。真机触摸、IMU 方向、云端语音字幕及音乐并发待验。

首次构建并获取公开组件后，可运行主机回归（另需 Node.js、C++ 编译器、pkg-config 和 OpenSSL 开发库；后两项供 LunarLink 加密主机测试使用）：

```sh
node test/run_host_tests.mjs
python -m unittest discover -s tools/tests -v
python tools/check-project.py --build-dir .build/firmware
```

源码、主机回归、固件构建、OTA 和实体屏幕/音频验收分别记录。开场、壁纸首页和应用翻页等切片的实现及验收缺口见 [开发状态](docs/development/implementation-status.md)，不能以规划或编译结果代替真机验收。第一方代码的开源许可证待所有者选择，第三方许可证各自适用。

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
