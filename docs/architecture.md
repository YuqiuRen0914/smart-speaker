# 独立工程的架构边界

状态：active。核对日期：2026-11-10。当前基线为用户此前回退、确认旧版界面的 1.0.35；错误抽离的 1.2.5 已由本次修正替代。保留该版本原有所有者和生命周期，不把后来版本的 API 与产品页面混装。

| 目录 | 负责什么 | 入口 |
| --- | --- | --- |
| `main/` | 产品启动、服务组合、引脚策略、旧版页面、音乐与网页曲库 | `main/product/`、`main/smart_speaker/` |
| `lunaros/` | OS 内核、执行器、网络、交付、存储、资源、设备 API、设备连接、桌面基础 | `include/lunaredu_lunaros/`、`src/subsystems.cmake` |
| `lunaros_apps/lunaredu_pet/` | 原版 Pet/Assistant 的状态和行为 | 组件公开头与 `src/` 内部域 |
| `components/` | 原版 BSP、音频辅助、账号资源、字符资产、JSON、NVS | 各组件公开头与 CMake |
| `components_device/` | GPIO 按键、I²S、SD、SPI LCD、MPU6050 驱动 | 对应组件契约 |
| `lunaros/assets/fonts/`、`data/` | 字体和工厂存储输入 | `cmake/storage.cmake` |
| `cmake/`、`tools/` | 独立组件闭包、构建任务、资源暂存和路径检查 | `tools/build.py`、`tools/check-project.py` |

该版本的 LunarOS 本来就是单一 IDF 组件，内部按 kernel/model/app/persistence/network/delivery/device_api/device_link/device_web/desktop 分域。保留原版 `src/subsystems.cmake` 和 `desktop/sources.cmake`，不导入后来版本的 Runtime、Resource、Music、Desktop 契约去拼接旧产品，也不新增泛化 shared/common 层。

`cmake/components.cmake` 显式列出 13 个本地组件。`LUNAREDU_REPO_ROOT` 兼容名固定指向本 checkout；没有外部 SDK、父目录搜索或其他产品依赖。第三方组件通过公开注册表与锁文件获取。固件使用与历史已运行版本一致的 ESP-IDF 6.0.1。

产品音频、曲库和页面保留原版所有者。旋钮修复属于产品输入：ISR 去抖，音量增量合并，每轮至多处理一条普通命令和一次合并音量，避免持续排空队列占用执行器。真机发现的缓存暂停错误通过 GPIO 控制函数 IRAM 和转换表 DRAM 修复；路径审计检查实际 ELF 存储区。此次未把原版播放状态迁到后来共享 Music，也没有新增后台播放器或另一份曲库。

后续功能先从产品页面和真实能力所有者扩展。游戏、AI 伴侣和番茄钟的 App 边界在实现时确定；不因规划提前换掉当前交互基线。涉及资源生命周期、公共接口、实时性或物理行为，分别补主机回归和指定硬件验收。

独立构建时禁止读取原仓库；构建路径审计核对实际组件与编译源文件均来自本工程或指定 ESP-IDF。设备操作和实体验收记录见 [基线修正](baseline-correction.md)，历史错误基线的隔离构建仍保留于 [旧记录](standalone-validation.md)。

2026-11-12 助手平台适配继续使用原 DeviceLink 和 Pet conversation 所有者：Board 绑定身份 → LunarLink bootstrap → 原 MQTT/UDP/Opus/音频焦点。协议适配在本工程内实现，没有新增外部 SDK 或后来 OS 组件依赖；兼容服务/API 名称与未迁移的新版持久远控见 [平台接入](development/07-lunarlink-platform.md)。
