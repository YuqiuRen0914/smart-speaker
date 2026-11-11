# 首发开发状态

## 2026-11-12 暂停提示、壁纸恢复及音量绘制

- 移除暂停／静音双竖线及形变，仅调音量时短暂显示水平条，零音量正确归零。保留遥测兼容字段 `pause_indicator_visible/percent` 为 false／0；音量条刷新区域从 116×24 缩至 116×8，按实际像素和透明度变化刷新。
- 实机壁纸接口返回 `sdcard_not_ready`，启动日志证实 SDMMC 初始化超时，原壁纸记录仍在。启动及重新扫描复用原挂载所有者，已就绪时不重复挂载；失败最多三次，间隔 100ms。重新扫描在播放状态锁外恢复挂载、加载原壁纸，再同步曲库，不格式化或清配置。首次修复烧录后实测第一次超时、第二次成功，原壁纸和 5 首歌曲恢复；持续失败仍保留错误并可重新扫描重试。
- SD 缺失、内置宠物条件下，连续调音量无整屏刷新，采样绘制约 4ms；恢复 SD 动画后约 28ms，两者负载不同，不能直接比较。显示循环改为按剩余帧预算等待，去掉绘制后额外固定等待 20ms。精灵采用实际宽高及紧凑 RGB565A8 平面，默认帧损伤面积由 9,216 降至 6,400 像素（约 31%），未新增任务、优先级或显示缓冲。
- 回归先复现重新扫描不恢复 SD、宠物帧重绘透明边缘，再验证修复。主机全套及 12 项工具测试通过；真实 LVGL 预览通过，包括 96／73／80 像素缩放、透明区、移动、隐藏和背景恢复。前后壁纸与宠物预览逐像素相同。音量缓动验证零值、反向和时钟回绕。
- 最终受管构建 `dbee87e5-800f-421f-b7e5-4d7e210559f5`，应用 3,759,376 bytes，SHA-256 `fc8458b8099833caf24254a5ba5245559b4b7e073b38ec5a2a97916732d28f5a`。路径审计通过（13 个本地组件、2,148 个编译单元）。核对实机身份和分区后写入原 `ota_1`，写入校验通过。本地证据：`.local/ui-performance-20261112/`。连续播放与上传等联合负载不属于本次验证结论。
- 最终启动 40 秒记录的 ELF 摘要匹配，未见 panic／看门狗；设备接口确认原壁纸 ready、5 首歌曲、宠物动画可用、LCD 刷新失败数为零。最终连续调音量采样没有整屏刷新，绘制耗时采样均值约 5.7ms；现场存在用户操作与不同宠物帧，不将其和前次均值换算为严格性能提升比例。

更新时间：2026-11-11

当前工作区保留用户已有的 `AGENTS.md`、`README.md` 和开发文档修改；本轮没有清理或恢复其他文件。固件基线仍是 1.0.35、ESP-IDF 6.0.1、LVGL 9.5.0 与当前接线。

## 已完成切片

| 任务 | 阶段 | 状态 | 主机／构建证据 | 真机／双平台证据 | 遗留项 |
| --- | --- | --- | --- | --- | --- |
| 统一产品状态：桌面模式、纵向导航、二层菜单、独立设置入口 | P1 | 实现完成，待真机 | `sh test/run_host_tests.sh` 通过；`test/product_state_test.cpp` 覆盖上下移动、确认、返回、主页 | 未执行本轮真机 UI 验收 | 还需接入现有 LVGL 页面所有者并记录 240×240 截图、触摸和旋钮场景 |
| 时间业务：番茄钟状态与一次／每周提醒 | P4 | 实现完成，待持久化／真机 | 主机测试覆盖暂停、继续、未知时间不响、对话 Pending、错过和每周下一次 | 未执行 | 当前状态所有者为产品运行期内存；P4 需接 NVS/SD 版本化持久化、可信时间和提醒音频焦点 |
| 游戏核心：电子木鱼与 2048 合并移动 | P5 | 实现完成，待设备输入／音效 | 主机测试覆盖木鱼状态、2048 移动、非法方向和重置 | 未执行 | 需接入单一显示/音频协调和游戏页面；不能把 API 状态视为可玩真机验收 |
| 单电脑配对与遥测状态 | P5 | 协议状态和采集核心实现完成，双平台 GUI 未完成 | 主机测试覆盖唯一配对、旧端拒绝、断连、过期与 offline 状态；`tools/pc-agent/telemetry_agent.py` 提供 macOS／Windows 共用采集核心；设备投影新增 `product.computer` | macOS、Windows 实机均未执行 | 菜单栏／托盘包装、权限和实际 LAN 传输仍需分别验证；不显示假零值 |
| 网页管理端产品状态操作 | P1/P4/P5 | 实现完成，待设备端验证 | `main/web/home.html` 调用现有投影总线，读取 `product.get` 并提交导航、桌面、番茄钟、配对／断开 | 未执行 | 壁纸、天气、提醒编辑表单和采集端配置仍需补齐；现有音乐/EQ/上传流程保留 |

## 现有基线证据

- `docs/baseline-correction.md` 记录历史任务 UUID `c23ae4db-cc77-452d-bbcb-9415645c4fe8` 的 1.0.35 修复构建、IRAM/DRAM 路径审计和已确认的旧版界面／触摸／音频／旋钮真机结果。
- 既有主机回归包含 DSP、双 I²S、触摸路由、角色动画、歌词、暂停、上传、网页 API、输入预算等；本轮新增产品状态回归后再次通过。

## 本轮验证

```text
node test/run_host_tests.mjs                       PASS
python -m unittest discover -s tools/tests -v      PASS（5 tests）
python tools/build.py --jobs 2                     PASS（ESP-IDF 6.0.1，受管任务恢复）
python tools/check-project.py --build-dir .build/firmware PASS（2132 translation units，13 local components）
构建任务 UUID                                      `f6110a32-f0fc-484c-8a1f-c57dd0d6d748`；产物 3521776 bytes；SHA-256 `4c6a734ac46503c4c6e6e3adcc6a27fe11e03da7eaa6fcc5528b5cf64d5b1bdc`
```

## 触摸导航决策（2026-11-11）

- 用户确认桌面左滑作为唯一触摸进入应用列表的手势。桌面右滑及上下滑不操作；触摸手势不再切换曲目。桌面点按播放／暂停、长按语音保留。
- 应用界面内继续按页面处理：普通列表的横滑返回，2048 的四向滑动移动棋盘。旋钮和实体按键语义不变。
- 路由回归更新在 `test/touch_routing_test.cpp`。`node test/run_host_tests.mjs` PASS；`git diff --check` PASS。未执行固件构建或烧录，真机手势待验收。

## 新版鲸鱼娘动画归档（2026-11-11）

- **P0/P3 / A-SOURCE：** 用户指定 `PC2005-cloud/dsh-pet`。固定提交 `c25386848f70f6e40902e4376f6e7b1e196bc326` 的 114 个 WebM、109 个 GIF 和交互参考原件已保存，合计 318 个文件。按未截断的提交树核对素材目录完整性，每个文件校验大小、Git blob SHA-1 和 SHA-256；ffprobe 读帧验证共 26,997 帧。固定快照恢复测试通过：缺失原件可恢复，已有改动文件保持原样且核验报错。
- **P3 / A-PET、A-INPUT：** [角色方案](06-pet-character.md) 记录点击、拖拽、入睡／醒来和对话状态的适配方案；用户再次确认左滑进入列表，后续优化方向是减少触摸控制音乐。当前源码已将左滑接到列表，README 更正旧说明；点按行为和角色播放器未修改。
- **验证：** 全量主机回归、9 项 Python 工具测试、既有构建路径审计（2,143 个编译单元、13 个本地组件）及 `git diff --check` 通过。本轮未构建、烧录或上传设备。完整动作的设备派生格式、触摸与动画实现及真机联合负载待后续验证。

## 本次接续执行（2026-11-10）

| 任务 | 阶段／验收 | 状态 | 主机／构建证据 | 真机／双平台证据 | 遗留与下一项 |
| --- | --- | --- | --- | --- | --- |
| 重复提醒延后与错过后的推进 | P4 / A-REMIND | 核心实现完成，待持久化及设备验收 | 主机测试覆盖对话 Pending、结束后只派发一次、周计划确认后保留、时钟向前跳 100 周仍只派发一次并推进到未来最近周期 | 未执行 | 提醒计划与记录目前仍是 RAM 状态；接入版本化 NVS 存储和声音焦点 |
| 电脑协同配对安全及采集核心 | P5 / A-PC | 协议与通用 GUI 入口完成，待平台验收 | CPU 指标改为 psutil 短采样百分比；协议版本 1；旧／错配 ID 无法提交样本或断开连接；线程运行、错误态和退出释放有单测 | 当前主机为 macOS 26.3.1；未运行菜单栏依赖；Windows 未执行 | Windows PowerShell 回退需 Windows 实机验证；GPU／温度／风扇明确 unavailable；无签名／打包 |
| 受管固件验证 | P1/P4/P5，当前源码检查 | 构建通过 | `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2132 translation units、13 本地组件）；`git diff --check` PASS；3 个采集端 Python 文件通过 `py_compile`。构建 UUID `42af8645-10fd-48af-9fd9-2057f3211a21`，ESP-IDF 6.0.1、jobs 2，3,530,752 bytes，SHA-256 `6ee6ef77822e6ed4f4512daca96255a8da90dc272b115ccf379daa487676fa21` | 本轮未烧录；不扩大到安装授权 | 首次 export 误用 IDF 6.1 环境而失败，明确设置 `IDF_PYTHON_ENV_PATH` 为 6.0 环境后通过。A-UI/A-PC/A-REMIND 真机或系统 GUI 证据仍缺 |

### 本次代码范围与决策

- 修改提醒触发周期，确保周提醒在对话期间只延后当前提醒，结束后派发一次；已经错过多个星期时推进至未来最近周期，不每秒重派。处理单次重复事件不关闭周计划。
- 遥测 disconnect 必须携带已配对的 `computer_id`，阻止任意 LAN 请求把唯一已配对端置为离线。协议版本未知时拒绝；GPU、温度、风扇缺传感器继续标为 `unavailable`。
- 电脑 GUI 增加 macOS `rumps` 菜单栏和 Windows `pystray` 托盘入口；后台有界轮询、3 秒网络超时、可见状态和退出断连。桌面依赖独立于固件依赖，不加入固件组件。
- 在 ESP-IDF 激活前 shell 的 PATH 指向 6.1，不可以只切 IDF_PATH；构建前同时核对 `IDF_PATH`、`IDF_PYTHON_ENV_PATH` 和实际 Python 路径。

### 当前未完成项与接续顺序

1. P1：当前 240×240 菜单虽能呈现统一导航快照，但主要模块仍有准备中状态，宠物／壁纸／时钟桌面状态尚未真实切换显示。下一刀应实现页面显示所有者里的实际三类桌面和模块功能派发，壁纸仍需要真实资源上传／预览／选择。
2. P4：持久化番茄钟／提醒计划与待查看记录，处理写入失败、重启暂停恢复、可信时间及提醒音频焦点；实现真实日期/月历和天气来源前不能将信息看板标完成。
3. P2/P3：逐项确认曲库封面关联、白噪声、网页取消/失败状态、助手单轮完整语音和音频租约恢复；大肥鱼当前素材还未核验下载许可或转换，所以未纳入设备。
4. P5：Windows 实际采样、设备配对、托盘退出与断连；macOS 菜单栏运行、权限和退出分别验收。采集端还没有签名与安装包。
5. P6：真实屏幕、音频、音乐＋动画 SD 并发、上传＋遥测联合负载和 30 分钟／100 次重复页面操作仍未执行。任何源码／构建结果均不替代这些证据。

本次没有烧录、OTA、提交、推送或发布；保留全部工作区既有修改。

## 最新接续（2026-11-10）

| 任务 | 阶段／验收 | 状态 | 证据 | 未验证／下一步 |
| --- | --- | --- | --- | --- |
| 番茄钟／提醒持久化事务 | P4 / A-TIME / A-REMIND | 实现完成，待真机 | 时间记录写入现有 LittleFS `/user/product_time.json`，schema 1；原子发布、读写边界、坏记录保留、写失败回滚均有主机测试；重启加载后番茄钟强制暂停，提醒不补响 | 尚未断电重启和设备文件系统实测；需要真机验证提醒声音与恢复按钮。时间写失败连续 3 次后停止自动重试并保留错误 |
| 网页时间管理 | P4 / A-WEB | 实现完成，待设备端 | 网页新增提醒日期时间、一次／每周、删除、待查看处理；状态来自 `product.get`，没有浏览器副本；无效输入和设备错误可见 | 网页需在实际设备检查时区、浏览器日期输入和失败提示 |
| 时间服务调度边界 | P4 / A-FOCUS / A-LOAD | 实现完成，待负载 | 网络服务调度每秒 tick 和有界 pomodoro 请求；显示／输入任务不执行文件写入；关键写入走事务候选状态 | 音频、动画、网页上传并发需真机联合负载 |
| 最新受管构建 | P4 变更 | 通过 | `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS；构建 UUID `8e28010b-53a4-45ca-a83a-c740ecad8ba4`，ESP-IDF 6.0.1、jobs 2，3,560,720 bytes，SHA-256 `12b29b2ea7ddd1ca3da49fcdb09793e99d712299bb2d35e71757c926e1799681`；`git diff --check` PASS | 未烧录，不能据此宣称真机通过 |

## 关键决策

- 页面、网页和未来 AI 工具共用 `main/product/product_state.*` 的受控操作；没有新增万能 Manager、事件总线或第二份业务状态。
- 电脑指标有 `unavailable`、`offline`、`stale` 与 `fresh` 状态，样本过期或权限不足不转成零值。
- 提醒在未知墙钟时间不触发；对话期间进入 `pending`，结束后由上层消费；重复提醒推进下一周并保留规则。
- 2048 只提供确定的四向移动与合并规则，游戏页面和音频占用由产品装配层接入，避免在核心里初始化显示或播放器。

## 下一项任务

优先把 `ProductState` 的快照接入现有 `smart_speaker_display` 单一 UI 所有者，完成 240×240 列表与二层页面；随后接入持久化、实际提醒调度和电脑采集端 GUI。缺少硬件、云端凭据或另一操作系统时只标记对应验收项，不宣布首发整体完成。

## 本次接续（2026-11-10）：桌面显示状态

| 任务 | 阶段／验收 | 状态 | 证据 | 未验证／下一步 |
| --- | --- | --- | --- | --- |
| 三类桌面模式反馈接入既有 LVGL 所有者 | P1 / A-UI / A-WALL | 部分实现，待壁纸资源链与真机 | 现有显示任务读取唯一 `ProductState` 桌面快照：Pet 保留宠物和播放叠层；Clock 隐藏宠物并居中显示经网络校时的现有时钟；Wallpaper 清楚显示“尚未选择壁纸资源”，不假报已生效。没有新增 LVGL 初始化或第二页面状态 | 240×240 真机布局未验；尚无壁纸上传、预览、持久选择和图像解码；宠物素材授权／转换仍待核实 |
| 回归与受管固件验证 | P1 变更 | 主机、源码边界与固件通过 | `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2132 translation units、13 local components）；`git diff --check` PASS。最终构建 UUID `1f570ed0-292f-4df2-8bb0-03ee7122ff1a`，ESP-IDF 6.0.1、jobs 2，3,564,848 bytes，SHA-256 `0c3ac2ad45b1ef11e25e961c7e5e275a7b23609a382ce1c2815e64b3d3a03953` | 首次错误环境任务 UUID `9d077238-cebf-427a-a506-c5d0114bea93` 因 shell 继承 IDF 6.1 Python 而失败，任务正常退出；随后明确切到 6.0.1 与 `idf6.0_py3.11_env` 后通过。未烧录／真机验证 |

本次实际改动集中在 `main/product/product_state_runtime.*` 和 `main/smart_speaker/smart_speaker_display.cpp`：新增只读导航快照接口，避免显示帧循环复制带向量的完整产品快照；使用原显示任务和原时钟控件按桌面快照切换呈现。首发开发仍未完成；下一项优先实现静态壁纸从现有上传／SD 存储到预览、选择、重启恢复的闭环，并补上对应页面错误反馈，再进入真机 A-UI/A-WALL。天气、信息月历、完整音乐白噪声、AI 单轮语音验收、Windows 实机和 P6 联合负载仍未完成或缺证据。

## 本次后续切片（2026-11-10）：静态壁纸资源闭环

| 任务 | 阶段／验收 | 状态 | 证据 | 未验证／下一步 |
| --- | --- | --- | --- | --- |
| 壁纸资源准备、上传、预览、选择、删除与重启加载 | P1 / A-WALL / A-WEB / A-RECOVERY | 桌面与 web 闭环实现，待设备验收 | 新 `desktop_resources` 产品所有者在 SD `/wallpapers/` 保存 BMP24；拒绝非规范尺寸／压缩／调色板／路径和重复名字；上传通过 `.upload` 临时文件后 rename；LittleFS 原子记录桌面模式和已选文件；列表、应用、预览（二进制 BMP）及安全删除走现有 Device API network I/O executor；LVGL 所有者读取不可变 RGB565 PSRAM 缓存并释放旧图引用；缺卡／损坏资源给明确回退。网页带文件选择、预览、应用和删除；离线工具 `tools/resource-tools/prepare_wallpaper.py` 固定 Pillow 11.3.0，输出中心裁切 240×240 BMP24 | Mac 上无 Pillow，尚未实际运行 PNG/JPEG 转换；未在设备 FAT/SD、浏览器和 240×240 屏幕测；写入中拔卡与实际掉电时序需设备验收 | 设备端上传当前对固定 172,854 byte BMP 在网络 I/O owner 中一次性缓冲到 PSRAM后校验再写 SD；显示缓存为 115,200 byte PSRAM。测并发音乐／上传占用与 UI 响应；必要时改成流式校验／转换，不能宣称负载已通过 |
| 壁纸资源主机回归 | P1 / A-WALL / A-RECOVERY | PASS | `sh test/run_host_tests.sh` 覆盖真实 BMP 解析、bottom-up方向与 RGB565 颜色、尺寸／头字段／路径攻击、SD 缺失、重复上传、rename／写失败不替换、LittleFS 保存失败保留旧选择、配置超长／坏记录和持有图片期间资源引用有效；完整历史主机回归 PASS。`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python -m py_compile tools/resource-tools/prepare_wallpaper.py` PASS | 仅使用临时宿主文件系统及公开 API stub；不代替 ESP FAT、断电或 LVGL 真机运行 |
| 壁纸固件构建与边界检查 | P1 变更 | PASS | 最终受管构建 UUID `569b9245-c1eb-4042-9567-7f69b46e1002`，ESP-IDF 6.0.1、jobs 2，3,599,264 bytes，SHA-256 `92840021cb16396d2256abb6ec5e4bead584a9d7f8ac2289c7ea72c14f5d0f83`；`python tools/check-project.py --build-dir .build/firmware` PASS（2133 translation units、13 本地组件）；`git diff --check` PASS。沙箱首次拒绝 Component Manager 进程枚举，受管任务 UUID `425c7701-93a8-40d3-a5c9-104d72507088` 失败退出；按审批恢复后定位 LVGL image cache 声明缺少 public instance header，随后修复并最终构建通过 | 构建只证明链接，未烧录／设备实测；Mac/Windows 浏览器实际上传尚未执行 |

关键约束：壁纸上传与预览必须继续和音乐读 SD 的真实设备联合测量；设备路径在 network I/O owner 完成固定尺寸缓冲和 BMP 转换，峰值图片缓冲约 288 KB PSRAM，缓冲不足时返回错误，不伪报成功、不触碰播放器意图。网页端先裁切图片再发送；取消和超时后提示刷新核对，避免把 HTTP 发送完成当成设备提交成功。桌面选择写入 LittleFS、壁纸文件写入 SD，两者保存失败都不更新屏幕的权威桌面状态。宠物素材仍未下载和核验授权；P1/A-UI/A-WALL 真机验收及其它首发模块仍未完成。

## 接续核验（2026-11-10）：月历

- 月历核心、设备 42 格展示和网页提醒标记已经存在于工作区，本次接续没有重复建立。修复上一任务 `2269c171-c06d-4a27-9c99-f129b1297817` 的编译错误：沿用已开启的 Montserrat 16 字体，颜色读取使用 `LV_PART_MAIN`。没有修改 SDK、引脚或依赖锁。
- `node test/run_host_tests.mjs`、Python 工具 9 项测试、`py_compile`、项目路径审计（2133 translation units、13 components）、`git diff --check` 均 PASS。受管构建 UUID `9a92b8d4-ef7c-413e-be83-053309aab75e` PASS；3,605,232 bytes；SHA-256 `f7e9471d686b2f8d4dc1ca1a72d82a6ac0ab592a3e7b38e1f84f8705b1e5d397`。首次 export 仍继承 IDF 6.1 Python，任务 `ef3b8528-f633-4427-90ee-0827a265782b` 失败，随后显式固定 IDF 6.0.1 和 Python 6.0 环境。
- A-INFO 的日期、月历、提醒标记已有主机与构建证据；没有屏幕视觉证据。天气仍未配置，后续接入真实天气服务，不能以月历代替 A-INFO 完整验收。
- 正在推进天气配置、真实 HTTPS 获取、年龄／失败状态和设备／网页共用只读状态；天气地点不自动探测，使用用户在网页填写的经纬度。其余待办与真机／Windows 证据仍未完成。没有烧录、OTA、提交、推送或发布。

## 本次接续（2026-11-11）：天气信息看板

- P4/A-INFO 天气切片已实现：`main/product/weather.*` 是唯一配置与运行状态所有者；使用现有 `network_fetch_resource` 和有界 `product.weather` 网络作业；配置以 schema 1 原子写入 `/user/product_weather.json`。地点由网页输入名称和经纬度，禁止 URL/凭据注入；请求固定 HTTPS Open-Meteo，最多缓存 4096 字节。
- 真实响应字段已通过公开端点核对，解析严格检查单位、WMO code、时间新鲜度、温度／湿度范围。页面和网页显示 `fresh`、`stale`、`offline`、`time_untrusted`、`error`、`loading` 等状态及数据年龄；连续三次失败后停止自动重试，用户可显式刷新。配置替换递增 generation，旧请求不能覆盖新地点；写入失败保留旧配置。
- 网页新增天气地点、经纬度、启用开关和刷新操作；设备信息看板二层页新增天气条目并注明 Open-Meteo/CC-BY 4.0。天气样本不持久化，重启后重新获取，避免把陈旧值伪装为实时。
- 主机证据：`test/weather_test.cpp` 已加入 `test/run_host_tests.sh`/`.ps1`，覆盖真实 schema、单位/范围/时间、旧地点响应、持久化回滚、缺网/过期、限流、连续失败和取消；`node test/run_host_tests.mjs` PASS；Python 9 项 PASS；`check-project.py` PASS（2134 translation units、13 components）；`git diff --check` PASS。
- 受管固件构建 UUID `6fe641bc-dc5e-450e-a0c4-2c1c1aa31b0e` PASS，ESP-IDF 6.0.1、jobs 2；产物 3,635,136 bytes，SHA-256 `9b07c8f4d11ed15d5b51ec9f3accf5ed6ba559ca8925d6fa5c264702c657ef6d`。第一次同源码配置因 Component Manager 读取 PID 被沙箱拒绝，任务 `169bfd90-bae5-4774-9b47-edb33a2d87a9` 失败，按授权恢复完成。
- 未验证：实际设备联网、HTTPS CA/时间校准、屏幕显示、网页浏览器交互、长时间离线恢复；不能把主机和构建证据当作 A-INFO 真机通过。天气服务是非商业免费条款，商业发布需重新核对授权。

## 本次接续（2026-11-11）：AmoledOS 纵向列表适配

- P1 / A-UI：新增 `main/smart_speaker/smart_speaker_amoled_list.*`，改编已固定 AmoledOS 提交 `3e8a3deead60cf86b5994a770007ff3247b8fdf9` 的纵向 flex 列表、图标＋文字行和滚动边缘渐隐。一级列表现在容纳全部七个条目，显示焦点随 ProductState 选择滚入视口；现有网络／绑定／设备信息／歌曲库本地菜单不变。渐隐只改行内子项透明度并跳过无变化设置，避免对整行套 transform/layer。
- 新视图只持有 LVGL 对象；创建、显示、隐藏都由现有 `smart_speaker_display` 所有者执行。导航和焦点仍由 `ProductState::Navigation` 持有，没有引入上游 HAL、第二 LVGL 初始化、事件回调路由或应用 ABI。游戏等既有产品视图沿用原呈现逻辑。
- 产品详情补充现有真实数据：音乐页显示 SD／播放器状态、曲名、播放状态、音量、歌词及错误提示；助手页显示小智会话状态；电脑页显示实际采样值、各指标状态和数据年龄，不可用时不填假零。调用确认操作仍复用原播放器／小智接口。
- 来源和 MIT 许可适配说明已更新至 `docs/development/upstream-sources.md`、`THIRD_PARTY_NOTICES.md`。本地代码注明来源函数及产品侧裁剪。
- 验证：`node test/run_host_tests.mjs` PASS；`sh test/run_host_tests.sh` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2135 translation units、13 local components）；`git diff --check` PASS。ESP-IDF 6.0.1、jobs 2 的受管构建任务 UUID `3264ed89-16f0-4270-bf9f-2397f7e0786d` 成功，产物 3,641,312 bytes，SHA-256 `0ab2f51a2f1c0a624a681857f162652589f734d8f6c36553ce8cec7f3a20453e`。首次构建因沙箱禁止 Component Manager 读取进程 PID 失败，任务 `89aaae65-1e8c-4d4d-b178-8fe763688c58` 经受管 resume `216fda25-a0d4-4a09-8234-91a1355832a6` 后重新配置；首轮源码编译发现 enum flag API 用法错误，已拆成两次 LVGL flag 调用，最终构建通过。依赖解析恢复了配置中原有受条件控制的 MPU6050 依赖；`dependencies.lock` 最终无差异。
- 未验证：真机 240×240 视觉、快速滚轮滚动和完整列表边缘效果；CPU／LVGL 内存、音乐并发刷新和 100 次页面往返需要设备测量。A-UI 仍不可标为通过。

### 下一项

继续 P2/P3 的真实功能缺口：曲目封面关联及白噪声要先复用当前播放器命令、双 I²S 音频所有者和已有焦点策略，补全可取消／可失败的资源流程；之后继续助手工具执行结果及当前版大肥鱼素材授权／可复现转换。完成资源和代码后再做各自受管构建与真机联合音频验收。首发仍未完成；macOS 与 Windows 采集端、云端语音、硬件和联合负载均有独立验收缺口。

## 本次后续切片（2026-11-11）：曲目封面关联

- P2 / A-MUSIC / A-WEB / A-RECOVERY：网页曲库增加每曲封面上传、替换、移除和缩略图预览；`cover.upload` 仅接受固定 172,854 byte 的 canonical BMP24，路径必须精确匹配当前曲库曲目。网页端重用同一受限浏览器图像转换路径；离线可复现工具 `python tools/resource-tools/prepare_cover.py input.png output.bmp` 使用已锁定 Pillow 11.3.0。
- 封面以 `song.mp3.cover.bmp` 旁车形式写在同曲目录。校验后通过 `.upload`/`.bak` 两阶段 rename 支持 FAT 替换和掉电回退；读取时可从备份恢复。删曲会清理封面及其临时／备份副本，曲目列表从文件系统重扫 `has_cover`。无效图片、错曲关联、缺 SD、播放器操作繁忙、上传短读和提交失败都有错误结果。
- 播放器仍是唯一音频所有者。切曲时封面所有者在网络 I/O 路径读取 BMP 并转为 RGB565 PSRAM 图像；LVGL 原显示所有者只读共享引用，音乐功能页显示 72×72 封面缩略图。页面切换不触碰播放器意图；曲目切换／删曲释放旧引用，LVGL 图片缓存先解除源后丢弃。
- 验证：`sh test/run_host_tests.sh` PASS（新增真实 `cover.upload`/`cover.remove`/`cover.image` handler 测试，覆盖短读／取消、损坏头、缺 SD、忙锁、曲目中途删除、备份恢复和旧图像引用生命周期）；`node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2137 translation units、13 本地组件）；`git diff --check` PASS。受管构建任务 UUID `63d3cb9b-a0a5-4b27-b9d6-8d6cb5eb72ee`，由失败配置任务 `1e77cf3a-5ad2-4788-b972-b84874541a03` 恢复，ESP-IDF 6.0.1/jobs 2，产物 3,651,744 bytes，SHA-256 `39dad196d685b464af0b5c325b460e75eee593a5cb4b2725ea91d89df6f47819`。依赖求解后 `dependencies.lock` 无差异。
- 未验证：设备 FAT 卡实际掉电／断卡、网页上传视觉与替换流程、曲目播放切换时 SPI/SD 占用和 LVGL 缩略图效果尚未上板测量；Mac/Windows 浏览器体验未分别验收。A-MUSIC 仍因白噪声及这些真机场景未通过。

## 接续切片（2026-11-11）：同播放器白噪声

- P2 / A-MUSIC / A-WEB：音乐二层菜单增加“本地音乐／白噪声”，网页播放区增加白噪声入口。`smart_speaker_noise.*` 提供按块生成、可 seek 的 22.05 kHz / 16-bit stereo WAV 源；没有 SD 文件、第二播放器、任务或 I²S 初始化。源振幅限制为 ±4096，再经过原 EQ、音量和限幅；`audio_player` 成功接收后负责关闭源，入队失败由调用者释放。有限 WAV 源结束后通过原代际检查的完成队列重新播放，解码失败停止且不无限重试。
- 暂停／继续保留同一源和位置；停止退出白噪声，下一次普通播放回到歌曲；点歌替换白噪声，恢复本地音乐页面可显式切回最后歌曲。`smart_speaker.get` 增加只读 `source=music|white_noise`，白噪声不伪装成曲库条目；删去最后选中歌曲不会误停正在生成的白噪声。
- 只启用已锁定 `chmorgan/esp-audio-player` 的 WAV 解码配置，未改依赖版本、接线或基线。新增编译检查：WAV 被旧增量配置禁用时明确失败。已有 `.build/firmware/sdkconfig` 仅将 `CONFIG_AUDIO_PLAYER_ENABLE_WAV` 改为 `y`，没有删除缓存或覆盖其它配置。Windows host runner 也已接入源测试，但 Windows 未实际运行。
- 验证：`node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 local components）；`git diff --check` PASS。新增源测试覆盖 WAV header、任意 seek、奇数短读、EOF、均值／能量／相关性、振幅界限和 Mac `funopen` 释放；生产完成／命令路径覆盖无 SD 曲库为空时播放、循环、暂停／继续、停止阻止旧 EOF、失败停机与切歌。
- 受管构建任务 `fe49be57-f4ac-4b0b-9dcf-27d5a3d9f180` PASS，ESP-IDF 6.0.1 / jobs 2；3,652,720 bytes；SHA-256 `322f5eb8855335e0c7892270f8ac774802397940d87a4fb57058599fdc2a131f`。环境错误任务 `61ecd822-aca6-495e-aead-824554d54f30` 退出；Component Manager PID 沙箱失败任务 `773ec9e6-129d-421f-83c5-4fa85786aa46` 恢复为 `759b9408-d5a6-47a5-ae9f-2c71e04df441` 后发现增量 sdkconfig 保留禁用值，已通过受管 cancel 等待进程退出再修正。依赖锁最终无差异。
- 未验证：本轮没有烧录、OTA、真机可闻输出、左右声道、音质或循环边界声音验收。A-FOCUS 的助手／游戏恢复尚未闭环，不能因白噪声源码与构建通过标为 A-MUSIC 全组通过。

### 下一切片的源码核查

现有小智 bridge 在 enable input/output 时异步 `pause()`，没有暂停确认，也没有带身份的恢复记录；`tool_stop`、`tool_next`、`tool_set_volume` 等把入队直接描述成完成。游戏页面目前只有核心状态，尚未获得音频占用。下一项必须在现有播放器运行态内补真实暂停确认、嵌套占用和意图代际，再接单轮会话结束、取消／失败及游戏退出；不能复用现有 broker 的 resident 槽去覆盖 Pet 所有者。旧版全局 `audio_focus()` 的 resident 已由 Pet 正确绑定，本产品不能再绑定另一个 resident。

### 大肥鱼来源核对

- P3 / A-SOURCE / A-PET（历史状态，已由 2026-11-11 后续条目更新）：检查公开仓库固定提交 `084323845ab82b68d5388d5c638bec09d8544a8a`、README、ASSET_LICENSE、asset tree。当时已确认上游当前 `assets/pet/` 是 whaletail-maid，并等待用户确认；之后用户选择并授权 legacy BigFish 素材，现状以本节后续导入和接入记录为准。
- 尚未复制任何帧。下一项保存原始许可证和 source manifest，再实现输出带 hash 的离线转换与显示所有者接入。该来源核对不等于已经显示、运行或性能验收。

## 本次接续（2026-11-11）：2048 设备输入闭环与助手命令回执

- P5 / A-GAME：补齐已实现 2048 状态和产品页之间的设备输入路由。游戏功能页中，旋钮上／下分别执行向上／下移动，左右实体键分别执行向左／右移动；触摸返回仍退出游戏。只在 2048 页拦截这些输入，其他页面仍由原导航或音乐按键逻辑处理。使用已有 `ProductState::game`，没有新增棋盘副本、显示所有者或音频实例。
- P3 / A-AI：助手控制工具已改为先确认命令成功入队，回执用“已提交、设备状态待确认”；队列繁忙返回错误，不再把异步入队说成已经播放／暂停／切歌／停止／调音完成。当前没有为小智工具单独建立可等待的执行确认协议；真正的播放器执行状态仍以 `speaker.status` / 设备状态为准，解码或硬件执行失败后的对话回执暂未异步更正。
- 验证：`node test/run_host_tests.mjs` PASS（全量宿主回归，新增纯函数测试覆盖四向映射及非 2048 页拒绝）；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 个本地组件）；`git diff --check` PASS。
- 最终受管构建 `python tools/build.py --jobs 2` PASS；明确使用 `/Users/roderick/esp/esp-idf-v6.0.1` 和 `idf6.0_py3.11_env`。任务 UUID `8e00786d-0fe2-4d74-9e5e-ac6652e406e8`，产物 3,653,584 bytes，SHA-256 `9ff1dcdeec409f05e74a276c1f09fab2592a4e40cc742a88f6dff987dc94bf12`。较早在同一切片的构建 UUID `623dcbf3-b11f-49fa-af87-7008782dc34d` 已被加入纯函数回归前的最后构建取代；默认 shell 原先指向 IDF 6.1，本轮未使用它构建。
- 未验证：未烧录设备，2048 实体键／旋钮布局、触摸返回、实际页面刷新及游戏期间音乐焦点都没有真机证据；A-GAME/A-FOCUS 不因此标记通过。 macOS/Windows 采集端、单轮云端语音与设备负载也未验收。

### 下一项

继续 P3：先落实“游戏／助手占用同一播放器”边界与播放意图代际，再完成手动单轮会话的取消、失败和结束恢复；单独完善 `speaker.*` 工具执行失败反馈协议。并行处理 P3 角色资源时只使用固定 MIT 来源素材，生成可复现转换包和哈希清单；不导入归档限制帧。完整首发仍未完成。

## 当前接续（2026-11-11）：单轮助手与播放器焦点闭环推进

- P3 / A-AI / A-FOCUS：助手开始收音前同步请求现有播放器暂停焦点；播放暂停未确认、状态在等待期间变化、队列满或会话失败时返回明确错误并释放焦点。单轮 VAD 静音收尾后等待回答；回答播放结束或音频通道关闭时通过 Pet 原有 HostCallbacks 延迟释放焦点，避免在 Pet 会话锁内反向取得产品状态锁。
- 现有播放器仍是唯一音频所有者。新增焦点代际和播放意图 revision；恢复命令只有在原播放意图未改变、仍为本次焦点暂停、用户未手动暂停且音量大于零时才恢复。控制命令成功入队后递增 revision；恢复队列满采用最多 3 次、间隔 100 ms 的有限重试，最终保留错误状态，不无限重试。
- 修复两处被生产代码签名变化影响的主机测试夹具：播放模式测试补上焦点策略头与运行时原子变量；输入预算测试只抽取命令入队代码，并提供与生产签名一致的测试声明。另统一 `runtime_internal.h` 与运行时的五参数 `enqueue_command` 声明，消除 IDF 编译发现的三／五参数重载歧义。
- 验证：`node test/run_host_tests.mjs` PASS（完整主机回归；首次失败暴露夹具缺少新增策略符号及输入预算抽取边界，修复后通过）；`test/input_budget_test.py` 增加并通过播放意图 revision 断言：10,000 次音量合并不改变 revision，成功停止／下一曲入队递增，满队列失败不递增；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 本地组件）；`python -m py_compile` 对电脑采集端及壁纸／封面转换工具 PASS；`git diff --check` PASS。
- 受管构建：初次任务 `8945d42b-1a7f-4487-9aa0-5e23445362e2` 使用 shell 继承的 IDF 6.1 Python 环境，在配置阶段失败；受管 resume 后任务 `3e5fb3be-50f2-4b82-81a3-246de8df2bf4` 编译发现 `enqueue_command` 声明歧义。修复并在 `IDF_PATH=/Users/roderick/esp/esp-idf-v6.0.1`、`IDF_PYTHON_ENV_PATH=/Users/roderick/.espressif/python_env/idf6.0_py3.11_env` 环境中受管 resume，最终任务 UUID `97719f26-f001-4b2a-a719-0ea1135ea8c9` PASS，jobs 2；固件 3,654,464 bytes，SHA-256 `cd0ce578d1b744caf2ecace6330e3f9e11886ebbec38f3fcfafd6e8eb5030a55`。
- 未验证：没有烧录设备、云端语音凭据或本轮真机操作；暂停确认、可闻 TTS、用户原已暂停、AI 停止／换曲意图、取消／超时、游戏嵌套进入助手及 30 分钟联合负载均待设备验收。焦点恢复策略有纯函数覆盖，但现有播放器运行期分支的主机集成测试仍需补强。macOS 菜单栏和 Windows 托盘也未分别实际启动验收。
- 决策依据：P2 复用原播放器和有限命令队列；Pet 不新增产品专属 audio-focus resident；恢复失败采用有限重试并在状态中报错；环境错误仅改用锁定的 6.0.1／Python 环境，不清理增量构建目录、不改 SDK 或依赖锁。

### 本次追加：暂停等待超时回滚

- 静态审查发现焦点暂停命令已开始执行、但等待确认超时时，调用方原先直接清除 active lease；迟到的暂停可能留下永久静音。现改为超时时仍发布一次延迟 release 请求：如果暂停尚未执行，带旧 generation 的命令会被忽略；如果已完成，则播放器 executor 按队列顺序尝试恢复，并仍受播放意图 revision 约束。
- `smart_speaker_focus_policy_test.cpp` 加入超时是否需释放的策略断言，POSIX 和 Windows host runner 均编入。该策略用例及全量 `node test/run_host_tests.mjs` 通过；工具测试 9 项通过；路径审计 2139 translation units / 13 components 通过；`git diff --check` 通过。
- 再次受管构建 UUID `5bfd8a6a-7aeb-43c9-bf26-abcd5a4747ec` PASS，ESP-IDF 6.0.1 / jobs 2，3,654,464 bytes，SHA-256 `58cd7713211190cbfae391bc7ead0fc05bdaa59f19779e2a030d25b0f976b7ad`。本构建只保存产物，没有安装。
- 当前剩余：超时与真正的播放器命令执行交错尚无主机集成时序测试；游戏尚未共用该 lease，A-FOCUS 嵌套验收不完整。硬件、云端和两种桌面 OS 的运行验证仍缺。

### 当前下一项

补充可在主机运行的播放器焦点状态机集成回归（确认暂停确认超时、意图 revision、释放与有限恢复重试的先后顺序），再继续完成助手工具执行结果闭环与角色资源转换包。之后必须在硬件和云端条件可用时分别验收 A-AI/A-FOCUS；并完成 macOS、Windows 各自的采集端运行证据。首发仍未完成。

## 本次后续切片（2026-11-11）：手动单轮语音 VAD 收尾

- P3 / A-AI：修复手动发起后 `listening_mode=manual` 不参与 VAD 静音结束的问题。用户开始说话后满足最短语音和静音阈值即发送停止收音；该模式使用 `manual_finish` 会话结果，使状态机等待云端回答。手动聆听无语音达到 15 秒时结束会话并关闭音频通道。连续／自动模式继续使用原来的 `auto_stop` 和等待／恢复规则；`realtime` 不触发这两个单轮策略。
- 修改 `lunaros_apps/lunaredu_pet/src/interaction/conversation/assistant_audio.cpp` 与 `assistant_loop.cpp`，策略抽到窄的纯函数头 `assistant_turn_policy.h`；新增 C++ 回归分别覆盖 VAD 语音／静音边界、15 秒空输入边界、非单轮模式和手动停止结果，并接入 POSIX、Windows 两个 host runner。
- 验证：`node test/run_host_tests.mjs` PASS（新增 assistant turn policy 宿主测试及全量回归）；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 个本地组件）；`git diff --check` PASS。
- 受管构建 `python tools/build.py --jobs 2` PASS，ESP-IDF 6.0.1。任务 UUID `64dfc8ec-9f47-44a3-ab2a-c467715ac4e3`；产物 3,653,488 bytes；SHA-256 `fcfa782a773ea8aa7d0310068deaf008b6792bc62a482fe3c2a4b291096208ab`。
- 未验证：没有小智云端凭据／设备运行证据；VAD 对真实麦克风环境的灵敏度、静音阈值、服务端收尾与 TTS 回答、取消／断网音频释放仍待 A-AI/A-FOCUS 真机验证。手动单轮纯策略通过不代表完整语音链路验收。

### 下一项

P3 继续补会话退出与播放焦点恢复：覆盖无输入超时、连接／鉴权／传输失败、用户取消、TTS 播放完成后麦克风关闭；在现有 Pet assistant 音频租约和单一播放器边界内，用播放意图代际避免恢复旧状态。同步核查并实现当前版角色帧的离线转换和受控渲染，素材仅采用 manifest 固定的 MIT dsh-pet 来源并保留完整许可。

## 最新接续（2026-11-11）：游戏与助手共享播放器焦点

- P5 / A-GAME / A-FOCUS：主播放器运行时每个 tick 对照唯一 `ProductState::Navigation` 快照检测是否处于游戏功能页。进入游戏时，在同一命令队列提交暂停；离开游戏时释放游戏持有。助手在游戏期间请求焦点会继承同一个暂停代际，不会二次暂停；助手结束后因游戏仍持有而不会恢复；最后一个持有者退出才尝试恢复原曲。
- 游戏在语音焦点恢复待处理时进入，会撤销尚未执行的恢复并继承此前播放意图／代际；已经排队的旧暂停仍先于退出后的新恢复命令处理。AI／用户改变播放意图后仍由 revision 拒绝旧恢复。焦点队列满时清除游戏持有并在播放器错误状态记录 `game_playback_pause_queue_full`。
- `smart_speaker_focus_policy_test.cpp` 增加助手／游戏单独、嵌套和无人持有的释放判定，并覆盖暂停确认超时释放策略。全量 `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；路径审计 PASS（2139 translation units / 13 components）；`git diff --check` PASS。
- ESP-IDF 6.0.1 / jobs 2 受管构建 UUID `809729f7-7dba-48eb-953f-8ec3cb571503` PASS；固件 3,654,352 bytes，SHA-256 `98e145f698542ed46e6a677ffebbfe4dc3ff51c79d9321fbec6a318cdfae3407`。只保存产物，没有烧录。
- 证据边界：纯策略主机回归不覆盖整个播放器命令状态机的所有线程交错。尚未在设备验证游戏进入的暂停确认、助手层输入态、退出后声音恢复、AI 停止／换曲、队列满、游戏 → 助手 → 游戏 → 退出和 30 分钟联合负载；因此 A-GAME/A-FOCUS 仍未通过。旧记录中“游戏尚未共用该 lease”是此实现前的状态，现由本条更新。

### 下一项

为播放器焦点补真正的宿主命令集成时序测试（暂停迟到、租约取消、重叠助手／游戏、意图变更和有限重试），再继续完成助手设备工具的异步执行结果与当前版大肥鱼资源转换。最终仍需 ESP32 真机、云端服务、macOS 与 Windows 分别完成对应运行验收；首发未完成。

## 最新接续（2026-11-11）：播放器焦点运行时集成回归

- P3/P5 / A-FOCUS/A-GAME：新增 `test/audio_focus_state_test.py`，直接从生产 `smart_speaker_runtime.cpp` 编译焦点重试、释放和游戏导航同步函数，以主机双件验证实际状态转换。覆盖游戏暂停／退出恢复、游戏重新进入取消已排队恢复、助手与游戏嵌套持有、最后持有者释放及游戏暂停队列满。
- 测试暴露游戏暂停队列满后会在每个运行 tick 重新提交的问题。增加 `game_focus_failed` 闩锁：本次仍停留在游戏页时不重复请求；离开游戏后清除闩锁，再次进入时允许重新尝试。主机集成回归覆盖“队列满—保持不重试—退出—重新进入成功”。该测试已接入 POSIX 与 Windows host runner。
- 验证：`python test/audio_focus_state_test.py` PASS；`node test/run_host_tests.mjs` PASS（含该集成测试）；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units / 13 local components）；`git diff --check` PASS。
- 受管 ESP-IDF 6.0.1/jobs 2 构建 UUID `be7cbf7c-09f3-46d1-a586-53032a74a67f` PASS，固件 3,654,384 bytes，SHA-256 `b9fdb44032a9d66e626bfd69c49eae7803da8e894b1890bb5733cd2752caefbc`。只构建保存，未烧录。
- 将同一测试继续扩展为从生产源码截取并运行 `kAssistantPause/kAssistantResume` 两个实际命令分支。覆盖错误焦点 generation 不暂停、匹配 generation 执行暂停、播放意图 revision 改变后不恢复、解码器恢复失败后的 100 ms 有界重试、成功后状态归位，以及三次恢复失败后停止并记录 `assistant_playback_restore_failed`。扩展后的 `python test/audio_focus_state_test.py` 和完整 `node test/run_host_tests.mjs` 均 PASS；测试仍不模拟底层双 I²S 真实可闻输出。
- 无云端会话与设备证据。游戏音频共享源码／宿主状态和构建完成，但 A-GAME/A-FOCUS 尚未真机验收；macOS／Windows GUI 也未分别启动。

### 下一项

下一项继续补充暂停解码失败、恢复队列拥塞以及 AI 停止／换曲在运行期与焦点命令竞态的生产 handler 测试；然后补助手工具执行结果闭环和当前版大肥鱼素材授权下的转换流程。硬件、云端及缺失 OS 的验收继续单独保留。

## 最新接续（2026-11-11）：大肥鱼素材固定来源与离线预处理

- P3 / A-PET / A-SOURCE：用户明确授权使用并再分发 legacy 大肥鱼素材。固定上游 `QCYTSN/dsh-dafeiyu` commit `084323845ab82b68d5388d5c638bec09d8544a8a`；从固定提交归档中取回 49 张非 dragging 运行帧，并逐张核对 tree Git blob SHA-1 和字节数后存入 `components/lunaredu_character_assets/third_party/dsh-dafeiyu/legacy/dafeiyu/`。增加 `source-manifest.json`（SHA-256、字节数、Git blob SHA-1）和导入说明，保留上游原始 license 文件。
- 四张 dragging 帧未纳入：上游将其标为独立社区贡献并要求另行获得贡献者许可；当前用户授权未单独确认覆盖该贡献，因此不把它们并入包。
- 初版转换状态（下文续进已更新）：首版 `/usr/bin/python3 components/lunaredu_character_assets/tools/prepare_bigfish.py /private/tmp/smart-speaker-bigfish-prepared` PASS（系统 Pillow 11.2.1，19 帧 PNG 为 534,164 bytes）；后续 RGB565 转换及 runtime SD 帧读取证据见本节以下条目。此处早期“仅转换、尚未接入 runtime”反映当时状态，不代表当前源码。
- 下载 API 首次遇到 GitHub 未认证速率限制；最终用 pinned commit 的只读 codeload 归档取源并完成逐帧哈希校验。无证书校验绕过。
- 本切片未改生产固件代码；最近固件构建 UUID 仍为 `be7cbf7c-09f3-46d1-a586-53032a74a67f`，产物信息见前文。本轮新增资源未进入固件，未烧录。下一项：跑完整主机／工具／源码审计回归，接入运行时帧读取与现有 Pet 显示所有者；继续保留设备、云端、macOS/Windows 和 P6 联合负载验收缺口。

### 资源切片续进（本轮）

- 检查显示路径后确认：角色由唯一 `speaker_display` 任务在 `smart_speaker_display.cpp` 直绘到 RGB565 屏幕；LVGL 宠物 `pet_visual_ports_t` 没有产品绑定。新角色应沿用显示任务和既有分块／差分刷新，不增建 LVGL 动画对象或第二显示所有者。
- 转换器补充输出直接匹配现有 tile renderer 的 160×160 little-endian RGB565 帧（透明度在离线阶段合成到黑色），避免设备每帧解 PNG 和分配整屏 RGB 缓冲；同时保留透明 PNG 源输出及双格式 SHA-256。输出放 SD 资源 staging，不编译进固件。
- 验证：系统 Python/Pillow 11.2.1 重新转换 PASS，19 帧 RGB565 总计 972,800 bytes；逐帧断言长度为 51,200 bytes，转换器固定源校验通过；在两个独立输出目录重复运行并确认 19 组 PNG/RGB565 SHA-256 完全一致；49 个固定来源文件重新核对长度 PASS；Python 语法检查（缓存重定向到 `/private/tmp`）及 `git diff --check` PASS。运行时读取和状态/动作映射仍未实现，不能称为宠物运行时完成。

## 最新接续（2026-11-11）：大肥鱼帧读取与显示回退

- P3 / A-PET：扩展现有 `product::DesktopResources` 单一 SD I/O 所有者。唯一 `speaker_network` 所有者接收显示任务的帧 ID 请求，在其轮询中从 `/characters/dafeiyu/<clip>/<frame>.rgb565` 读取固定 51,200 bytes，检查长度和额外尾随字节，分配受限 PSRAM 帧并发布不可变快照；显示任务不直接读 SD、不重复初始化 LVGL。
- `speaker_display` 把现有 Pebble mood 映射到大肥鱼 idle/happy/angry/think/talk/poke 序列；只有当前所请求帧 ID 已加载时才用原分块／差分 RGB565 写屏路径呈现。缺卡、缺帧、错误长度或缓冲不足会保留旧快照但不拿它冒充新帧，显示退回原几何形象并给出素材不可用提示。
- `test/desktop_resources_test.cpp` 扩展为直接编译生产资源所有者，覆盖 RGB565 小端像素解析、帧尺寸、成功发布、缺帧、损坏长度、SD 不可用和旧快照生命周期。完整 `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 本地组件）；`git diff --check` PASS。
- ESP-IDF 6.0.1/jobs 2 受管固件构建 PASS，UUID `c7ad251f-07c3-4d18-924d-11bff6662efa`；镜像 3,656,608 bytes，SHA-256 `c8a221a489dab1c3d84f8d88389a37b16a6f5a9b8cef44d4e98f76774c145047`。本轮还未烧录；真机 SD 安装、音乐并行读卡、屏幕显示/声音、内存和刷新时延未实测。
- 资源 staging 目录现可直接把 clip 子目录复制至 SD `/characters/dafeiyu/`；该手动路径记录于素材 `IMPORT_NOTES.md`。网页资源上传和批量安装尚未接通，故 A-WEB/A-PET 不能整体验收。当前显示仍会在缺资源时用旧几何角色回退；转换和加载闭环通过主机验证不等于大肥鱼真机验收。

## 最新接续（2026-11-11）：助手阶段与情绪映射到大肥鱼帧

- P3 / A-AI / A-PET：显示任务现在读取 Pet `ServiceRuntimeSnapshot`，将 opening/listening → think、thinking → think、speaking → talk、failed → angry；仅在非活动会话时采用最近有效的助手情绪事件（happy/celebrate、curious、sleepy、error），2.2 秒后自动回到最新运行态。会话阶段优先级高于短暂情绪事件；双 I²S 表达量仍沿既有音频采样路径输入角色控制器。
- 在现有 XiaoZhi `HostCallbacks::on_event` 连接一次情绪投影，将许可词汇表映射到窄的显示枚举；不保留服务 JSON／借用字符串，不新增事件总线。大肥鱼帧策略提取至 `smart_speaker_pet_frame_policy.h` 并新增宿主回归，覆盖助手阶段优先级、情绪、普通角色 mood 回退和 poke 四帧有界循环；已接 POSIX 与 Windows host runner。
- 验证：`node test/run_host_tests.mjs` PASS（含新策略及 SD 帧资源生产实现回归）；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 本地组件）；`git diff --check` PASS。受管构建使用 ESP-IDF 6.0.1 / jobs 2，UUID `8f8bb1b1-e661-4e2f-9b89-8f25f0f4629a`；镜像 3,657,184 bytes，SHA-256 `a11ddb5d681bbd73090149b261cbdfc6be0a35a44c57e0ccc2fa5c9c01eb7017`。
- 未覆盖：手动单轮语音真机成功／失败对话时表情与 TTS 强度、情绪 TTL 遇到并发状态切换、实际四类短互动输入、自然 idle 帧动画、触摸桌面交互及音乐＋大肥鱼 SD 并发。当前 Playful 只在已有角色 mood 命中时循环 poke 4 帧；未专门动作仍需依据真实输入和产品语义映射。没有烧录／真机验证；A-AI/A-PET/A-LOAD 仍未通过。

## 最新接续（2026-11-11）：网页安装大肥鱼帧包

- P2/P3 / A-WEB / A-PET：新增 `pet.frame.upload` 设备 bulk API。只允许固定 clip/index 白名单和精确 51,200-byte 帧；SD owner 在 `/characters/dafeiyu/<clip>/<frame>.rgb565.upload` 临时文件中完整写入并 flush/close 后 rename，失败删除临时文件且不替换原帧。覆盖当前显示帧时清除旧显示快照并排队读取已提交新帧；正在读的旧快照通过 shared_ptr 保持有效。
- 网页新增角色目录选择、清单固定提交校验、19 帧白名单／完整性／尺寸校验、逐帧上传、进度和取消；部分中断明确显示已提交帧数，重新选择目录可幂等重传。只传 RGB565，PNG 与清单留在用户选定的构建资源包，不上传无关文件。
- 宿主生产资源测试增加 clip 白名单、提交完整帧替换、显示快照生存期和坏尺寸拒绝；POSIX `node test/run_host_tests.mjs` PASS（含新帧策略、资源 owner、网页 API）；`node test/web_api_test.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；源码边界检查 PASS（2139 translation units、13 local components）；`git diff --check` PASS。
- ESP-IDF 6.0.1/jobs 2 受管构建 PASS，UUID `81f3c988-8d05-427a-9484-f51c418318ed`；镜像 3,664,336 bytes，SHA-256 `c32dcb1efdf6f66bcf2295f1b617e404bc2f53046d6f0e2ecf49aacae7a33fc7`。构建未烧录；Mac/Windows 浏览器 UI 未实机运行。
- 仍待：批量写 SD 时音乐＋遥测并发测量、资源损坏回滚的实际 HTTP fault 注入、上传完所有 mood 帧后的屏幕显示和 TTS 声音验收。A-PET/A-WEB/A-LOAD 尚未通过真机/双平台验收。

### 下一项

继续实现已映射资源的触摸／会话动作优先级和缺卡后有界重试，扩展 SD 帧 API 故障注入与批量上传取消的生产路径验证；随后继续 P2/P3 音频与网页缺口。拿到设备后，优先装入该 19 帧资源包，验证语音阶段、情绪和互动回归，并实测音乐＋动画＋网页批量上传/遥测并发。仍不得把主机/build PASS 记作真机完成。

## 最新接续（2026-11-11）：大肥鱼网页资源逐帧完整性与失败恢复

- P2/P3 / A-WEB / A-PET：网页上传器现在会先校验 legacy 大肥鱼清单中 19 个 RGB565 帧路径各自唯一、文件集合完整、尺寸及 SHA-256 与清单相符；任一帧哈希不匹配会在开始网络上传前拒绝整包。SHA-256 使用页内实现，兼容设备本地 HTTP 不具备 Web Crypto 的常见浏览器环境；主机测试以 Node 标准库结果交叉核对 51,200-byte 输入，并检查篡改后 hash 改变。上传完成提示改为说明当前帧会重载，其余帧在对应状态切换后生效。
- 扩展生产 `DesktopResources` 宿主测试：将目标 `.upload` 临时路径模拟为不可写目录，确认上传返回 `pet_frame_write_failed` 且原帧字节未变化；清除故障后重试成功，并确认已发布帧快照更新、旧不可变快照仍有效。首次全量主机测试发现旧测试调用漏传 `now_ms`；修正夹具时间戳后 `node test/run_host_tests.mjs` PASS。`node test/web_api_test.mjs` PASS；工具回归 `python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2139 translation units、13 local components）；`git diff --check` PASS。
- 构建环境第一次继承 IDF 6.1 Python 而失败，未清理缓存或改 SDK。受管任务 `a65023b4-405f-4d78-b52c-fe5a55ae1e24` 与错误环境恢复尝试 `d9346961-654a-4951-9173-39f8406adc45` 保留失败记录；显式导出 ESP-IDF 6.0.1 及 `idf6.0_py3.11_env` 后受管 resume 任务 UUID `ab575249-c039-4f0d-8381-2d205759e590` PASS（jobs 2）。镜像 3,667,904 bytes，SHA-256 `1de4079fe3e4f2eb089a3c4d4dc7055ae670787e9d2807bbd506183e9f3d3904`。构建仅保存，没有烧录。
- A-WEB/A-PET 仍未整体验收：未在浏览器真机环境执行 19 帧上传／取消、未对设备 HTTP 分块读取注入短读、SD rename 失败或网络超时，也没有实测音乐播放与批量 SD 写入并行。当前只完成网页哈希预检和资源 owner 的不可写回滚／重试宿主路径。无 ESP32、云端凭据及 macOS／Windows 实际运行证据；不得据此宣布首发完成。

### 下一项

将 HTTP bulk handler 的短读／取消与 SD 临时文件写入／rename 故障纳入可重复宿主路径测试，再继续已映射资源的互动状态优先级与网页取消状态呈现。其后继续 P2/P3 尚缺的网页管理、跨模块状态一致和资源并发能力；每个实现切片仍需主机与受管构建验证，并保留设备、双 OS 和联合负载验收未通过的状态。

## 最新接续（2026-11-11）：大肥鱼上传 HTTP handler 故障回归

- P2/P3 / A-WEB / A-PET：将 `pet.frame.upload` 的生产 handler 移入 `smart_speaker_pet_frame_api.cpp` 独立模块，仍由原路由绑定调用，并只委托唯一 `DesktopResources` 持有 SD 写入；避免测试复制实现，也不引入第二路由或资源所有者。
- 新增生产 handler 宿主测试，覆盖逐块 317-byte 短分块成功读取、客户端中止／短读、零字节读、回调非法超读、无效 clip/index 与错误长度。所有读取失败都验证旧帧文件逐字节保持；成功流式接收验证最终提交字节与输入一致。资源所有者测试增加 rename 失败的临时文件清理及故障消除后重试成功。测试编入 POSIX 和 Windows host runner；`node test/run_host_tests.mjs` PASS，工具测试 9 项 PASS，路径审计 PASS（2139 TUs / 13 components），`git diff --check` PASS。
- CMake 重配置在受限沙箱因 ESP-IDF Component Manager 查询系统进程触发 `PermissionError(sysctl)`。按用户已授权的受管构建操作，经权限审核后使用原 IDF 6.0.1 / Python 3.11 锁定环境和增量目录恢复成功；受管任务 UUID `48b04d1a-c10a-4758-9bd8-2e9068a791f5`，jobs 2，镜像 3,668,032 bytes，SHA-256 `2ac39ff67e1b18f810bb1a44b0f9a8cd8f26b09123c351623d76b5f72c7228cb`。Component Manager 输出称已更新锁文件，但 `git status` / diff 确认 `dependencies.lock` 最终无工作区变化；无烧录。
- 证据边界：宿主验证覆盖 handler 回调失败和文件写/rename 故障，但设备 HTTP server 的 socket 断连行为、设备 FAT/SD rename 语义、浏览器取消时序和多路并发负载仍需实体设备验证；A-WEB/A-PET/A-LOAD 未通过完整真机验收。

### 下一项

继续实现已映射大肥鱼互动帧的实际输入触发及优先级，避免触摸互动覆盖助手会话表达；补网页上传取消后的设备最终状态核验与失败说明。之后按 P2–P6 继续补齐音乐／白噪声、时间／天气、游戏和电脑协同缺口；分别留存 macOS、Windows 与实体设备证据。

## 最新接续（2026-11-11）：上传 handler 生产故障测试与网页取消结果准确性

- P2/P3 / A-WEB / A-PET：生产 `pet.frame.upload` HTTP handler 独立于聚合 projection 编译，仍通过原 route 注册。新增主机集成回归直接编译该 handler 与唯一 `DesktopResources` owner，覆盖短分块成功、客户端中止、零字节、非法超读、无效资源身份／尺寸、成功替换，并逐项确认不完整输入时旧文件未变。SD owner 另覆盖 rename 失败的临时文件清理和移除阻塞后重试提交。测试已加入 POSIX、Windows runner。
- 网页取消／断网提示现在区分已确认提交的帧数与当前请求结果未确认的帧；XHR 取消或丢失响应时，保守说明设备可能已提交该帧，用户可重新选择整包幂等重传。之前的 SHA-256 清单预检继续在网络传输前拒绝损坏包。
- 验证：`node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 项）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units / 13 components）；`git diff --check` PASS。Windows runner 已纳入测试，但本机为 macOS，本轮未实际运行 Windows。
- 受管 ESP-IDF 6.0.1 / Python 3.11 / jobs 2 构建 UUID `48b04d1a-c10a-4758-9bd8-2e9068a791f5` PASS；镜像 3,668,032 bytes，SHA-256 `2ac39ff67e1b18f810bb1a44b0f9a8cd8f26b09123c351623d76b5f72c7228cb`。页面提示微调后的增量受管构建 UUID `eecbc492-9d4e-4877-9279-3149d5d73b0c` PASS；镜像 3,668,240 bytes，SHA-256 `d6abc527bd6c9f718e0da09f629d8af3574c4cf6d4ba5b5baa2475afcc8726b5`。初次沙箱内 CMake 重配置因 `sysctl()` 进程枚举受限失败，之后在审核授权下恢复原受管任务成功；没有清理构建缓存或烧录。
- A-WEB/A-PET/A-LOAD 仍待设备验收：需要用实际网页断网／取消、设备 SD/FAT rename 与批量写入场景确认行为，并同时运行音乐、角色动画和电脑遥测。A-PET 的 Playful 已由现有上一／下一曲短时反应及 MPU6050 摇晃驱动，助手会话状态具有更高映射优先级；未另设与既有触摸／PTT 契约冲突的新手势。

### 下一项

继续核查并补齐首发中尚未覆盖的 P2–P6 网页/固件功能，优先按验收矩阵逐项找出网页管理中 AI 配置、EQ 预设持久化和时间编辑操作的缺口；之后做音乐、动画、上传、遥测联合测量。资源上传切片源码／主机/build 通过不替代设备与双平台运行验收。

## 最新接续（2026-11-11）：网页 AI 助手配置与受管构建

- P3 / A-AI / A-WEB：网页新增 AI 助手区，复用 Pet/XiaoZhi 已公开的 `status.get`、`enabled.set`、`microphone.enabled.set`、`activation.start`、`activation.clear` 和 `conversation.test` 路由；显示配对、传输、会话和错误状态，可启停助手／麦克风、启动官方配对及显式提交语音测试。清除本机配对前要求确认并解释账户侧绑定不会被删除。只显示 activation code，不读取 MQTT 密码或其它持久凭据；测试消息“已提交”不会被描述为语音成功，页面要求观察设备会话／扬声器状态。
- 当前 Pet/XiaoZhi API 以固件常量 `kDefaultAssistantOtaUrl` 发起官方设备激活，尚无公开的自定义服务端点／认证配置契约。网页已明确标示该缺口，没有添加无法生效的假输入控件。产品规格仍要求端点与认证可配置，相关范围仍未完成；需在 Pet 所有者中设计受约束、持久化且不回显秘密的真实设置接口。
- `test/web_api_test.mjs` 增加 AI 路由方法／路径回归；`node test/run_host_tests.mjs` PASS，工具测试 9 项 PASS，路径审计 PASS（2140 TUs / 13 组件），`git diff --check` PASS。没有浏览器、设备或服务凭据，未验证配对和可闻 TTS。
- 受管 ESP-IDF 6.0.1 / Python 3.11 / jobs 2 构建 UUID `a1af73d7-b909-44bf-b69d-615161497c08` PASS，镜像 3,672,576 bytes，SHA-256 `71c059196122dac3651bf3067c3ec8803db5d3f820ca2aaf08c6d9d478e0bb04`；只保存产物，未烧录。

### 下一项

基于现有 Pet conversation 唯一状态所有者，落实符合产品规格的可配置 XiaoZhi 兼容激活端点及认证边界（保存失败须保留之前设置，状态端点不得回显凭据），并为网页提供真实 GET/SET 与错误回滚测试；不可接受自定义端点时，需把规格改变留待用户决策，不能把固定官方 endpoint 称作可配置。之后继续 P2–P6 剩余网页与业务能力，及所有真机／双平台／联合负载验收。

## 最新接续（2026-11-11）：可配置 XiaoZhi 激活端点

- P3 / A-AI / A-WEB：在现有 Pet conversation 唯一状态所有者中新增持久化激活端点；空值保留锁定的官方默认 URL。激活任务使用状态中的端点，XiaoZhi 原有激活协议继续负责服务器签发认证与 MQTT 凭据。没有添加不兼容的任意 Bearer 密钥输入。
- 网页新增 HTTPS 激活端点显示／设置，并经现有 `xiaozhi.activation.endpoint.set` 业务投影提交。固件仅接受 HTTPS、受限 authority/port/path，拒绝 userinfo、查询串、fragment、路径穿越和已经包含 `/activate` 的 URL（客户端会按协议追加该子路径）。状态投影只回传当前端点，不包括 MQTT 密码、用户名或 topic 凭据。
- 端点只有在本机无已配对凭据、无激活处理中请求时可更改；网页明确提示先解除本机配对。产品原因：不同服务的配对凭据不能跨服务复用。保存使用 NVS 单键写入并先持久化、后更新内存；失败时保持原运行值。非法旧记录回退到官方默认值并记录可见错误。
- 新增纯策略宿主回归覆盖 HTTPS URL 边界、userinfo/query/fragment/非法端口/path traversal/重复 activate 后缀拒绝，NVS 写失败不发布、已有凭据或请求处理中拒绝、成功提交、相同值幂等和回退默认值。POSIX 与 Windows host runner 已接入；`node test/run_host_tests.mjs` PASS，工具测试 9 项 PASS，路径审计 PASS（2140 TUs / 13 组件），`git diff --check` PASS。宿主策略测试用模拟持久化失败回调；真实 NVS 写故障与重启恢复仍需设备验证。
- 受管 ESP-IDF 6.0.1 / Python 3.11 / jobs 2 构建 UUID `ce9e0b1c-326a-48f8-8d8e-6f85566779c6` PASS；镜像 3,678,608 bytes，SHA-256 `e7119cb99accbca319ae6efaed4ce70046f3197732af9a13219d9e5af7a9540d`。只构建保存，未烧录。
- A-AI/A-WEB 未整体验收：没有实际兼容云端端点凭据或设备来完成自定义端点配对、成功／失败对话、MCP 工具执行、语音采集与可闻 TTS；Windows 浏览器也未实际运行。配置 API 具备源码／宿主／构建证据，服务互操作和实体安全存储仍待验证。

### 下一项

继续核查设备端自定义端点的 TLS/网络策略和配置反馈，并增加端点实际 HTTP 激活请求的可控 mock 集成测试（确保请求命中配置 URL、失败不会替换旧 MQTT 凭据）。然后按验收矩阵推进仍缺的 P2–P6：重点是网页时间／提醒保存错误呈现、AI MCP 工具执行完成反馈及资源/音频联合负载。实体硬件、云服务、macOS/Windows 实际运行与 A-LOAD 证据必须单独记录，未执行的场景保持未验收。

### 端点配置续进：解除配对保留服务选择

- 代码复查发现 `clear_activation()` 重置整个 `ActivationState` 会顺带清空服务端点。现调整为清除配对／MQTT 凭据时保留 `ota_url`，以符合“端点是设备服务配置、MQTT 密钥是配对凭据”的所有权边界；NVS 后续状态写入也保留该值。这样用户先清除旧配对，再设新服务，之后再次解除配对仍继续使用所选服务。
- 修复激活 URL 校验后缀判断的 off-by-one：纯策略测试先失败，修正后全量 host PASS。最新验证：`node test/run_host_tests.mjs` PASS；工具测试 9 项 PASS；源码审计 2140 translation units / 13 components PASS；`git diff --check` PASS。
- 包含解除配对端点保留修复的最新受管 ESP-IDF 6.0.1/jobs 2 构建 UUID `58c759ea-da3f-438c-a6ef-e1388d11fda9` PASS，镜像 3,678,976 bytes，SHA-256 `e34259ffee7ed2cf60da1eea2b351bf2fefb0fdeafd05bfa89937395873b1de6`。只构建未烧录。前一构建 UUID `ce9e0b1c-326a-48f8-8d8e-6f85566779c6` 不含本次端点保留调整，应以最新 UUID 为准。
- 下一项保持：对实际激活 HTTP 请求端点选择及配置／清除／重启组合补更强集成测试；随后继续时间／提醒网页错误闭环、MCP 工具结果反馈及其它 P2–P6 缺口。A-AI、真机、双平台和联合负载仍待实际运行证据。

## 最新接续（2026-11-11）：AI 点歌工具错误反馈更具体

- P3 / A-AI：小智 `speaker.play_track` 工具在点歌业务 handler 返回错误时，现在把实际设备错误码随 MCP `isError=true` 返回（例如 `track_not_found`、`upload_in_progress`、`audio_not_ready`、`command_queue_full`），不再归并为不透明的“点歌失败”。成功回复仍明确描述为已入队、执行结果待状态确认；没有把排队伪装成已经播放。
- 验证：全量 `node test/run_host_tests.mjs` PASS；工具测试 9 项 PASS；路径审计 PASS（2140 TUs / 13 components）；`git diff --check` PASS。该消息格式修改经 IDF 编译验证，但本轮没有服务端 MCP 实测或真机播放器执行证据。
- 受管 ESP-IDF 6.0.1 / jobs 2 构建 UUID `1c224c56-0004-49aa-bfb2-44ce6e5962df` PASS；固件 3,679,376 bytes，SHA-256 `5073eaa818a9000b1bceeb3638758c35de935672dbb2c2d1c9a76ac2843e037b`。只构建未烧录。

### 下一项

补充 MCP 真实工具调度／回复主机集成验证，并推进现有工具从“命令已提交”到可观察播放器状态的对话反馈（若播放器异步执行失败，应明确等待状态确认而不伪造执行结果）。随后继续 P2–P6 未完成模块和网页管理缺口；最终需要云端、硬件、Windows、macOS 和 30 分钟联合负载的实际证据。

## 最新接续（2026-11-11）：MCP 生产调度器宿主回归接入

- P3 / A-AI：既有 `assistant_mcp_dispatch_test.cpp` 直接编译生产 `assistant_mcp.cpp`，现已加入 POSIX `test/run_host_tests.sh` 与 Windows `test/run_host_tests.ps1`。覆盖工具回调期间可重入查询（确认 runtime mutex 已释放）、回复槽预留不被其他生产者抢占、队列满时不触发副作用、会话重置丢弃旧回复，以及不同会话使用各自队列和 session id。
- 本机独立编译运行该生产调度器测试 PASS；`node test/run_host_tests.mjs` PASS（完整套件包含该回归）；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units、13 components）；`git diff --check` PASS。Windows runner 已加测试命令，但本机未执行 Windows PowerShell/编译器，不能记录 Windows 运行通过。
- 核对并激活 ESP-IDF 6.0.1 与其 Python 3.11 环境后执行 `python tools/build.py --jobs 2`，任务 UUID `5a0340ea-930b-4d86-9580-2f5bbd242982` 状态 `succeeded`，exit code 0，镜像 3,679,376 bytes，SHA-256 `5073eaa818a9000b1bceeb3638758c35de935672dbb2c2d1c9a76ac2843e037b`。本次仅改宿主 runner，因此固件内容与前一验证构建一致；未烧录。
- 证据边界：MCP 外层调度/响应生命周期现有生产逻辑获得宿主集成回归，仍未实现播放器异步执行结果回送。当前工具明确说明已提交待确认，最终执行状态须由状态投影另行观察；云端互操作、真机语音和播放器失败反馈仍待设备／服务验证。

### 下一项

继续为音乐工具设计窄的执行结果闭环：只在现有播放器命令队列／状态所有者中发布可关联的终态，不添加通用事件总线或第二播放器状态；先覆盖失败、超时、意图变更与过期响应，再决定如何呈现给对话。其后继续 P2–P6 验收矩阵，所有真机、macOS/Windows 实际运行及联合负载保持独立未验收项。

## 最新接续（2026-11-11）：播放器状态查询与 USB 真机现状

- P3 / A-AI：`speaker.status` 经纯 JSON 投影返回播放器 `audio_ready`、`playing`、`paused`、`uploading`、音量、曲目、曲库数量和 runtime 最近错误；工具描述说明可用于复查已提交命令。最近错误是全局状态提示，不是逐命令回执，不能据此把旧错误归因给刚提交的操作。命令 handler 仍只报告已提交，未伪称异步播放执行成功。新增宿主回归验证失败状态和正常状态投影，且没有伪造 `command_id`；测试接入 POSIX 与 Windows runner。
- `node test/run_host_tests.mjs` PASS（完整套件，含 MCP 调度及播放状态投影回归）；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units、13 components）；`git diff --check` PASS。
- 以 ESP-IDF 6.0.1 / Python 3.11 环境执行 `python tools/build.py --jobs 2`，UUID `23d2ecc8-9cc3-439e-b505-735970717e3c`，状态 `succeeded` / exit 0，镜像 3,681,056 bytes，SHA-256 `af9678459700defda6f1f7e15a8cccbdf653a048b4569fe72c062fe5b2a9cc8c`。只生成产物，未烧录。
- 用户说明音箱始终连接电脑，并可经 USB 与 ESP32 通信。发现 USB CDC `/dev/cu.usbmodem214101`；一次只读串口捕获未发送命令，记录到 1.0.35 当前板的开机路径：LittleFS ready、Wi-Fi connected、SD mounted、扫描 5 首曲目、240×240 ST7789/LVGL ready、触摸和 MPU 初始化成功；唤醒词前端报 `asset_not_ready`。随后只读 `GET /api/v1/device` 和 `/api/v1/capabilities` 返回 HTTP 200；设备身份报告 `fw_version=1.0.35`、屏幕 frame_count=28655、XiaoZhi 已配对且 transport connected 但会话 inactive、Wi-Fi RSSI -32、TF 卡 mounted。该设备仍运行旧镜像，本次新构建未上板，所有新功能不可用这些状态冒充已验收；没有进行烧录、OTA 或其他设备写操作。
- 新环境约束：音箱与电脑保持 USB 连接，可用串口日志辅助真机诊断；电脑端遥测仍需验证真实平台连接、权限、采样与协议断连恢复。串口当前只证明启动及外设初始化，尚未验证显示内容、触摸／旋钮交互、扬声器出声、AI 麦克风/TTS、提醒、SD 并发和 30 分钟负载。

### 下一项

继续补齐音乐工具针对实际命令的终态关联与有限等待/过期规则，避免把全局 `last_error` 当作命令结果。随后需先准备可审查的硬件验收场景，再依据当前会话单独取得烧录授权，将受管构建产物写入设备；在此之前继续完成源码、宿主和平台侧工作，不记录新固件真机通过。

## 最新接续（2026-11-11）：2048 设备棋盘呈现

- P5 / A-GAME：在既有 `speaker_display` LVGL 所有者内，把游戏详情从纯文本数字改为 4×4 彩色方块棋盘及分数栏；复用同一 `ProductState` 棋盘快照和既有触摸／方向输入、游戏期间的播放器焦点策略。页面不创建第二个 LVGL 根或游戏状态。换页、离开游戏或选择另一游戏时隐藏棋盘控件。
- 本机全量 `node test/run_host_tests.mjs` PASS（含 MCP dispatcher、播放状态投影、2048 生产逻辑等）；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units、13 components）；`git diff --check` PASS。
- ESP-IDF 6.0.1 / Python 3.11 / jobs 2 受管构建 `python tools/build.py --jobs 2` PASS，UUID `75fc5ef7-0933-492d-808d-20026ab92478`，镜像 3,680,960 bytes，SHA-256 `e9d2c9e08ec7d6627f10f4337608b793f3bcedce4ecda5e5cd4ce11f2baec4c6`。未烧录。
- 设备仍运行 API 报告的 1.0.35 镜像；新的棋盘布局仅有编译和宿主证据，未在实体 240×240 屏幕观察。游戏滑动、实体按键／旋钮、木鱼声音、音乐恢复和提醒浮层仍需真机 A-GAME/A-FOCUS 验收。USB CDC 已确认可读取启动日志；尚未向 ESP32 写入任何数据。

### 下一项

完成这批新固件的真机功能验收需要把上述精确摘要的应用镜像写入已识别的 ESP32-S3（serial `LUNAR-9C139EA9F46C`，当前 `1.0.35`）；此动作需遵守 AGENTS.md 单独烧录授权要求。等待明确授权期间继续完成与硬件无关的播放器命令结果关联、网页与双平台缺口，不能把当前旧镜像运行状态当成新源码功能证据。

## 当前设备启动修复续进（2026-11-11）

- USB 硬件验收揭示 `home.html` 72,259 bytes 超过原默认 `CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES=65536`，导致 `app_main` 在 portal 配置时提前返回。项目默认与当前增量 `sdkconfig` 均已提高到 131,072；`test/web_api_test.mjs` 加入实际 HTML 字节数与配置上限回归。启动复测后 portal 配置已通过。
- 下一启动错误 `invalid_device_module` 已追到 `smart_speaker_projection.cpp` 注册 39 个操作，而 `device_projection_registry.cpp` 单模块限额 32。将其有界容量提升到 48，并在网页宿主回归里直接统计模块操作数，与生产注册器限额比较，防止同类启动阻断复发。
- 两次回归期间产生的中间镜像没有作为最终设备验收目标：3,680,960 bytes / SHA-256 `2f93c981e2107ef4dc11079cd201496716eee77f499c37b8eecbfab41efedd8c` 已上板，但设备日志显示服务注册提前失败；当前待验证修复镜像为 3,680,960 bytes / SHA-256 `35e436e73305b02d9e97397a92e4f83c7ba599afc1014bc3d2b0dbfae83e40bf`，受管构建 UUID `360ec5ce-93fa-4b3e-862b-c1220418c2f3`，将继续部署到 `ota_1 @ 0x620000`。
- 修复验证：`node test/run_host_tests.mjs` PASS（含页面大小、39/48 投影容量回归）；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units、13 components）；`git diff --check` PASS。
- 真机状态目前仍是 `app_main` 在声明注册失败后返回，屏幕／音频／网络服务未运行。尚未完成新镜像启动确认、网页/UI 观察、实际音乐或游戏验收；USB 读写仅用于已明确授权的应用分区恢复与诊断，LittleFS 未改写。

### 下一项

烧录并验证上述待验修复镜像。如果服务能启动，通过只读 HTTP 获取设备身份／屏幕统计和网页内容，并观察 240×240 UI；随后逐项执行当前硬件能验证的桌面导航、2048、木鱼、音乐播放／暂停及白噪声场景，记录实际可用功能与失败项。云端语音、电脑双平台和联合负载需另行提供真实证据，不能凭启动成功代替验收。

## 最新接续（2026-11-11）：产品 UI 入口修正与当前设备核对

- 用户反馈设备 UI 看起来没有变化。只读设备 API 复核确认 `display.stats.get` 报告 `local_page=home`、宠物桌面可见、屏幕已初始化且 `flush_fail_count=0`；`product.get` 中导航仍在 `desktop/pet`。新版 AmoledOS 列表实际在 `product_shell` 绘制，原固件只将下滑映射到该页面，而上滑仍打开旧设置；启动成功并不意味着用户已进入新版 UI。当前屏幕默认保留宠物桌面符合“桌面与列表分开”，但入口不够明显。
- P1 / A-UI/A-INPUT：将触摸上滑和下滑统一路由到现有 `OpenProductShell`，继续复用 ProductState 和唯一 LVGL 显示所有者。实体右键长按仍保留旧网络／绑定／设备信息设置入口。修改 `smart_speaker_touch_routing.*`、`smart_speaker_runtime.cpp` 与 `test/touch_routing_test.cpp`；没有增加第二套路由、显示初始化或业务状态。
- 验证：`node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2140 translation units / 13 components）；`git diff --check` PASS。正确 ESP-IDF 6.0.1 / Python 3.11 / jobs 2 受管构建 UUID `3771176d-a28f-4425-a8eb-453268a55b0e` PASS，产物 3,680,960 bytes，SHA-256 `61801a077101442d1050294d48b76ce06b3903dc40908686d3955e014c0fdbeb`。之前误用 6.1 Python 环境的受管任务 `db480580-1f6f-462f-b74b-2e39e415e766` configure 失败，未改依赖；显式选择 6.0 Python 环境后任务通过。
- 当前设备仍运行先前烧录的启动修复镜像，不包含刚完成的双向滑动入口改动；现有镜像可通过下滑进入产品列表。HTTP 只读检查：网页 `GET /` 返回 200 / 72,259 bytes；Wi-Fi、LittleFS、TF 卡、240×240 SPI 屏幕、音频服务正常，TF 卡挂载且曲库 5 首。一次状态快照观察到“豆浆油条.mp3”正在播放；之后查询时已停止，音频仍 ready、无播放器错误。XiaoZhi transport connected、会话 inactive，wake-word model unavailable；不可据此称语音或 TTS 已通过。时间存储 ready、设备时钟有效；天气未配置；电脑未配对，各项遥测 unavailable。2048 初始棋盘由状态 API 返回，但未完成真机操作验收。
- `display.stats.get` 无 screen mirror；尝试通过 Codex 浏览器打开设备网页时，浏览器 origin 授权策略拒绝访问，未尝试绕过。可以由用户直接打开 `http://192.168.31.169` 查看当前管理页。实体 240×240 画面仍未取得可审查截图；新版 UI 的设备实测需烧录上述最新产物后，用户上滑或下滑进入列表。
- 未执行：未烧录本次新产物、未发起播放命令或网页变更。之前针对启动修复镜像的烧录授权不自动覆盖此 hash。A-UI/A-INPUT 真机截图与手势、A-MUSIC 可闻播放、A-GAME 操作，以及云端语音、双平台和 A-LOAD 仍未验收。

### 下一项

取得针对 SHA-256 `61801a077101442d1050294d48b76ce06b3903dc40908686d3955e014c0fdbeb` 的明确烧录授权后安装到已识别设备的应用槽；重启后用 `display.stats.get` 确认列表入口，执行上下滑、旋钮选择、右键进入二层／功能和左键返回，并查看 240×240 实屏。无需烧录时，用户现在可在当前镜像上向下滑查看已实现的列表；不要把此路径冒充为本次双向入口修复已安装。

## 最新接续（2026-11-11）：SmallTV 参考信息首页

- **范围：P1 / A-UI、A-WALL，A-INFO 天气显示部分，A-SOURCE。** 按用户提供的 SmallTV 截图实现白色小时、黄色分钟／秒数、日期／星期、地点与天气图标、温湿度条及读数。应用滚动列表继续沿用既有 AmoledOS 适配。SmallTV 只有视觉参考，未导入其固件或 UI 源码；窄体数字来自固定提交的 OFL Teko 子集，来源、修改及 hash 见 `upstream-sources.md`。不据此宣称整个 P1 或 A-INFO 已验收。
- `main/smart_speaker/smart_speaker_home.*` 只格式化只读快照并维护 LVGL 控件；既有显示任务持有视图和两个有界 TinyTTF 缓存，每秒刷新，未新增网络、存储或显示任务。继续使用 `weather_snapshot` 与已有不可变壁纸缓存，复用当前网页上传。时钟和壁纸模式共用信息首页背景；无图黑底，已选资源缺失提示重新选择。没有植入测试天气、AQI 或上游截图的最高温数据。
- `NavigationSnapshot` 的新安装默认值设为 Clock；已有持久化的 Pet／其他选择仍优先恢复。`product_state_test` 检查新默认，`desktop_resources_test` 检查保存的 Pet 不被新默认覆盖。已有设备若仍显示宠物，应在网页或产品设置切换时钟／壁纸，无需清空配置。
- **主机视觉：PASS。** `tools/ui-preview/` 用生产首页代码、LVGL 9.5.0、实际中英文字体与 RGB565 240×240 虚拟屏幕渲染。检查午夜、23:59、双位数字宽度、雪／多云、纯白背景对比度、长地名、离线／过期／失败、未校时、未配置、背景缺失、字形覆盖与重复进出复用；网络调用被断言禁止。实际查看雪和明亮背景渲染。`ui/home-reference-preview.png` 是明确标注的主机测试夹具，不是设备数据；没有使用用户尚未提供的背景素材。
- **回归：PASS。** `node test/run_host_tests.mjs`；`python -m unittest discover -s tools/tests -v`（9 tests）；`python tools/check-project.py --build-dir .build/firmware`（2142 translation units / 13 local components，含链接后 ISR IRAM／DRAM 检查）；`git diff --check`。字体重复生成仍为 2,472 bytes、相同 SHA-256，常规固件构建无需 FontTools。
- **受管固件构建：PASS。** ESP-IDF 6.0.1 / Python 3.11 / jobs 2。任务 `d8455284-0839-4061-9eb6-a584982bd6d8` 首次链接因嵌入字体符号前缀错误失败；按生成的汇编修正为 `_binary_home_digits_ttf_*` 后，通过 `tools/build.py resume` 生成成功任务 **`2896690f-099c-446c-bd49-6abfd331482e`**。应用 `lunaredu-smart-speaker.bin` 为 **3,692,720 bytes**，SHA-256 **`b579cbb22d0b8a0e6c2e7afd3a3e9797837fcfb4fdf4e3f14d4e1338fc71c0a2`**，6 MiB 应用槽剩余 41%。依赖锁最终无变化，版本／接线／分区未因样式调整而更换。
- **未验证：** 未烧录、OTA、上传素材、提交或发布。本次未取得实体 LCD 截图；数字字体运行时内存、刷秒、壁纸下的实屏可读性、上下滑入口、显示与播放／语音联合负载待真机验证。字体文件体积不等于运行时内存。历史设备运行结论不能覆盖本镜像。

下一项：提供自备背景后检查主体与文字避让；在获得针对上述新 hash 的设备安装授权后，验证时钟／壁纸／宠物切换及重启恢复、真实天气、未校时／离线提示、屏幕与输入和音频联合负载。

## 最新接续（2026-11-11）：信息首页 USB 安装与启动检查

- 用户明确要求“为我烧录”，已将任务 `2896690f-099c-446c-bd49-6abfd331482e` 的 3,692,720-byte 应用镜像（SHA-256 `b579cbb22d0b8a0e6c2e7afd3a3e9797837fcfb4fdf4e3f14d4e1338fc71c0a2`）通过 USB 写入当前确认有效的 `ota_1 @ 0x620000`，esptool 写后摘要校验通过，并硬件复位。只写应用，未写分区表、otadata、NVS、LittleFS、另一应用槽或 SD 素材。
- 写前交叉核对设备 API 身份、ROM MAC、实体分区表及有效 OTA sequence=2 / state=2 / CRC。实体应用偏移及大小与构建一致；实体 otadata／filesystem 的 encryption flags 为 0，而构建默认表为 1。ROM 安全信息及构建均确认未启用 Flash Encryption／Secure Boot，本次保留实体分区表，仅安装相同应用布局。首次 stub 读取 otadata 中途停止，改用 ROM `--no-stub` 读取成功后才执行写入。
- 启动日志 ELF 摘要前缀 `156c501d6` 与新 ELF SHA-256 一致；240×240 ST7789、LVGL、中文及数字字体初始化通过。约 50 秒串口捕获内未发现 panic、Cache error、断言或任务看门狗故障。网络恢复，设备 API 的启动状态为 `boot_confirmed=true`、`pending_verify=false`、`running_image_state=valid`，LittleFS ready、TF 卡 mounted。启动早期两个 HTTP 查询超时，随后重试正常。
- 已通过现有 `product.desktop.set` 切为 Clock，并独立查询确认 `desktop=clock / page=desktop`；已选背景与其他配置保留。显示状态 `local_page=home`，刷新计数 93→158，`flush_fail_count=0`，LVGL pool 剩余 158,708 bytes／最大块 154,344 bytes。这是短时运行快照，不能视为长期内存或联合负载验收。
- 设备日志与原始身份／分区证据仅保存于忽略提交的本机 `.local/`。未取得实体屏幕照片，字形、配色、背景主体、触摸／旋钮和可闻输出仍待实际观察；未新增天气或素材配置，现有地点与经纬度是否对应未核验。未执行提交、推送或发布。

## 最新接续（2026-11-11）：天气时钟风格开机动画与安装

- **P1 / A-BOOT、A-UI、A-RECOVERY：实现及主机验证通过，设备安装／启动通过，视觉与手感待目视。** 新增 `main/smart_speaker/smart_speaker_boot.*`，按用户照片实现黑底、184×18 白色胶囊活动条、绿色英文状态、右下真实固件版本。活动段在条内往返，不展示虚构百分比；仅当 Wi-Fi 已连接且时钟有效时显示 Ready 和满条。
- 显示／字体初始化成功后，在既有 UI 任务的第一次渲染之前创建顶层启动视图。直接读取网络所有者快照，不触发联网／校时，不新建任务或 LVGL 定时器；常规音频、网络和启动健康确认继续由原所有者执行。正常路径至少展示 1.2 秒，就绪提示停留 0.5 秒；无网／未校时等待约 4.5 秒后再停留 0.5 秒并交回本地页面，需配网则交回已有配网流程。菜单／长按等显式导航立即跳过。退出删除视图对象，不因后续重连重播；显示字体初始化之前或失败时，本动画不可用，原错误路径保留。
- 启动视图可见期间仅走现有 LVGL flush，不运行 Pet 直接绘屏路径，避免覆盖。显示遥测新增只读 `boot_visible`，该时段 `local_page=boot`，退出后恢复原页面。没有更改持久化结构、音频、接线、版本或分区。
- **主机视觉／行为 PASS：** 原生 LVGL 240×240 RGB565 预览实际编译生产视图，检查联网／校时、4.5+0.5 秒离线和未校时退出、配网退出、毫秒回绕、重连不重播、100 次创建／立即取消后无遗留控件、字形和层对象边界。查看 connecting／ready 帧；`ui/boot-preview.gif` 是 80 帧主机测试夹具，展示连接→校时→就绪→首页，循环仅用于预览，不是设备截图或真实网络时序。复现见 `tools/ui-preview/README.md`。
- **回归／构建 PASS：** `node test/run_host_tests.mjs`；`python -m unittest discover -s tools/tests -v`（9 tests）；`python tools/check-project.py --build-dir .build/firmware`（2143 translation units / 13 local components，含 ISR 链接后 IRAM／DRAM 检查）；`git diff --check`。ESP-IDF 6.0.1 / Python 3.11 / jobs 2，受管构建 UUID **`7cefbb71-a5b2-427f-b35e-8bcb21927a20`**，应用 **3,694,672 bytes**，SHA-256 **`f9521621b9a6d2f7168c217a1535eac286c8e9ef4abc8a84ef563a2e833ae74e`**；依赖锁无变化。开机动画复用既有 Montserrat 14，未新增字体／图像资源依赖。
- **安装／启动 PASS：** 用户追加“完成后为我烧录”授权后，交叉核对设备 API 与 ROM 身份、实体应用槽、有效 OTA sequence=2 / state=2 / CRC、安全状态及镜像摘要。保留实体分区表，仅向 `ota_1 @ 0x620000` 写新应用；写后校验通过。未写 NVS、LittleFS、otadata、另一应用槽或 SD 素材。新启动 ELF 摘要前缀 `bd1c53606` 与本次 ELF 一致，字体／LVGL 初始化成功；约 50 秒串口捕获内未见 panic、断言、Cache error 或任务看门狗故障。设备随后报告 `boot_confirmed=true / pending_verify=false / running_image_state=valid`，LittleFS ready、TF mounted。
- 运行查询确认恢复所保存的 `desktop=wallpaper / page=desktop`，仍使用同一信息首页；本次没有改桌面或背景设置。`boot_visible=false / local_page=home`，刷新计数 126、flush 145、失败 0，LVGL pool 剩余 172,696 bytes／最大块 171,364 bytes。启动后 HTTP 观察首个成功样本在约 17.2 秒，已错过动画窗口，因此不能据此宣称捕获了动画出现／退出的真机时序。主机时序通过不能替代实屏证据。
- **未验证：** 尚无实体屏幕动画视频、主动手势打断、真机断网／无校时／配网场景和联合负载证据；不将短时状态快照当作性能或长期稳定性验收。设备原始日志／身份／分区证据留在忽略提交的本机目录。未执行提交、推送或发布。

## 最新接续（2026-11-11）：转场安装记录与开机动画重新选型

- 按用户此前“完成后为我烧录”的持续授权，已安装加载前景 120 ms 淡出、背景 420 ms 渐隐的版本。受管构建 UUID `04a9f4bb-4065-46bb-aef9-1ce80ce44560`，应用 3,695,056 bytes，SHA-256 `779bfd519b29110f6c4e1070026a688a22f6021454d8b2db56411ad664c282fa`；ELF SHA-256 `26a0ba1deb56ca6da64bef11065fbb6815ed4b73951bd9b35e71429034b7bb38`。主机回归、9 项 Python 工具测试、生产 LVGL 预览及路径审计（2143 translation units／13 local components）通过；依赖锁未变化。
- 写前重新核对 ROM 身份、安全状态、实体分区及 OTA sequence=2／state=2／CRC。只写 `ota_1 @ 0x620000` 应用，esptool 写后摘要校验通过。启动 ELF 摘要前缀一致；日志记录 2718 ms 显示开场、5311 ms 开始揭示首页、5817 ms 删除开场对象。实际揭示至删除为 506 ms，不能将源码 420 ms 当作实屏精确时长。
- 约 40 秒串口捕获内未见 panic、Cache error、断言或任务看门狗。存在 `i2s_channel_disable` 未启用通道错误及唤醒前端 internal largest block=7936、need=12288 的延后初始化告警，不宣称音频／语音或资源余量验收通过。随后只读 HTTP 状态查询连接超时，当前镜像的 API 启动确认与显示统计尚未复核；此前镜像的 API 结论不能挪用。设备原始证据仅保存在本机忽略目录。
- 用户仍反馈“不够优雅”，此版本**视觉未获认可**。本轮转为核验真正开源动画项目，记录于 `upstream-sources.md`：LVGL Music Demo 的连续开场编排、X-TRACK 同分辨率小屏的标题／底线时间线，以及 Plymouth Circle Flow 的圆弧视觉参考。未引入新依赖，未实现或安装下一版方案。后续须先形成可审查的完整动画，再做实屏验收。


## 最新接续（2026-11-11）：连续时钟开场实现与主机验证

- **范围 P1 / A-BOOT、A-UI、A-RECOVERY。** 根据用户“请进行实现”，替换被否定的活动条＋整体渐隐为细线／真实时钟连续编排。开场借用首页原有小时和分钟，380–1040 ms 裁切显露，居中等待；就绪或超时后 760 ms 平移，秒数／日期／天气／温湿度按 300／470／620／800 ms 错峰显露，650–1400 ms 带出既有壁纸。弱化的网络状态与实际版本保留，未校时使用 `--`，无演示数据或假百分比。
- `SpeakerHome` 常驻持有五个透明裁切容器；`SpeakerBootScreen` 借用视图，仅持有临时线／状态／版本，既有显示循环驱动。退出／主动导航取消恢复首页所有布局，删除临时对象；后续重连不重播。没有新增任务／定时器、整屏合成缓冲、字体或资产依赖，SDK／LVGL／接线／分区未改。保存的 Pet 和配网页使用黑底细线与末段渐隐回退，Pet 直接 LCD 路径在开场删除后恢复。
- **主机及回归 PASS：** 生产 LVGL 每 40 ms 渲染 110 帧；最终首页与改动前雪天气样本逐像素一致；终点及显露／等待／平移／信息入场各阶段取消与完整首页逐像素一致。无网／未校时 4.5+1.4 秒边界、配网／Pet 回退、毫秒回绕、重连不重播、100 次创建取消无额外控件、明亮背景延后进入通过。GIF `ui/boot-choreography-preview.gif` 是明确测试夹具，不是真机帧率或性能证据。
- `node test/run_host_tests.mjs` PASS；`python -m unittest discover -s tools/tests -v` PASS（9 tests）；`python tools/check-project.py --build-dir .build/firmware` PASS（2143 translation units / 13 local components）；`git diff --check` PASS。ESP-IDF 6.0.1／Python 3.11／jobs 2 首轮任务 `c0267a9d-3503-41bd-a0b5-bfe18d9d8dbe` 暴露目标平台 int32_t 类型推导与枚举浮点转换问题；显式类型修正并复测生产预览后，受管 resume 任务 **`9d617885-6cd3-4753-9c9a-6a96d02c676d` PASS**。
- 应用 **3,696,656 bytes**，SHA-256 **`13c87e671ed7578e6284bdf5a02b2e99ec6df71ec313ae780ec2bc7f19ee5b98`**；ELF SHA-256 **`a90de1b8be0ccac1f888dc8a672e65a91455af478265f7bb51729b847c4ade08`**。来源及适配边界见 `upstream-sources.md`，未复制上游标志或源码，依赖锁无变化。设备安装／启动与实屏验收分别追加下段证据，不以构建通过代替。


### 连续时钟开场：安装证据与用户性能反馈（2026-11-11）

- 上述 `9d617885-6cd3-4753-9c9a-6a96d02c676d` 镜像已按持续烧录授权安装到重新核验的 `ota_1 @ 0x620000`，写后摘要通过。只写应用，保留分区、otadata、NVS、LittleFS、另一槽和 SD；串口 ELF 摘要 `a90de1b8b` 对应产物。启动日志 2741 ms 显示开场、4788 ms 开始归位、6285 ms 删除视图，归位阶段实际 1497 ms。40 秒捕获未见 panic、Cache error、断言或任务看门狗；仍有既存 I2S disable 及唤醒前端最大块不足告警。
- 首轮 HTTP 超时，随后重试成功。API 确认 `boot_confirmed=true / pending_verify=false / running_image_state=valid`，保存的 `wallpaper / desktop` 恢复；`boot_visible=false`、刷新失败 0，SD／LittleFS ready。短时 LVGL pool 剩余 144,916 bytes、最大块 143,336 bytes；整体统计 `max_refresh_us=277182` 包含首次绘制和启动竞争，不能单凭此值判断某一动画阶段。
- 用户明确反馈实屏“不够丝滑，有卡顿”；因此设备视觉／性能**未通过**，不能将安装和主机预览通过表述为实屏验收通过。后续针对实际视图更新与刷新链路建立绘制量回归及真机开机时段测量。


## 最新接续（2026-11-11）：开机卡顿修正与真机对照

- 生产 LVGL 主机复现：不变的等待姿态每 20 ms 重绘整屏。LVGL 样式 setter 收到相同值也触发刷新；比较值后才修改，原 110 帧像素一致。201 次轮询的刷新量由 11,059,200 降至 1,357,683 像素（−87.7%），整屏由 192 降为 2，静止等待区间由 1,152,000 降为 0。
- 测量构建 `f95d7322-e156-453c-aed4-bce35e6ac6b9` 新增局部采样后触发 8 KiB 显示任务栈溢出并重启，已替换。采样移入显示所有者独占的静态存储，不增加任务栈或优先级。去掉重复重绘后的中间构建 `a6084856-2e31-4c0f-b07f-a006471b9394` 安装后稳定启动；72 次开机轮询，均值 47.5 ms，p95 桶上界 160 ms，最长 339.0 ms，11 次超过 50 ms，26 次成功刷新、7 次整屏，最大刷新 120.8 ms，flush 失败 0。
- 壁纸由整屏透明度渐变改为带 16px 柔边的下移揭示，局部更新刚露出的区域；保持既有壁纸缓存、最终对比度与时钟编排。亮背景夹具在壁纸揭示及重叠入场区间从 1,670,400 降到 582,240 像素（−65.1%），每帧部分刷新与强制全刷结果一致。开场阶段推迟构建隐藏菜单、跳过角色及歌词更新；20 ms 轮询预算扣除本轮工作时间，超预算时至少让出一个 tick。
- **最终构建与验证：** ESP-IDF 6.0.1 / jobs 2，UUID `43d613cd-1fa8-40dc-a56e-8e2049fb1231`，3,697,696 bytes，应用 SHA-256 `d8a04697cb4b33607c07f01243dda62080114c961ba4cea876977b2ca4862554`。生产 LVGL 主机动画／性能回归、全量主机测试、9 项 Python 工具测试、路径审计（2143 条编译单元／13 本地组件）及 diff 检查通过。依赖锁未改。
- **安装与实测：** 重新核对 ROM 身份、安全状态、实体分区、OTA 选择记录与镜像摘要，USB 仅写 `ota_1 @ 0x620000`，写后校验通过。串口 ELF 前缀 `0e27e8312` 匹配，2734 ms 显示开场、5279 ms 开始归位、6689 ms 删除视图。40 秒串口捕获内未见栈溢出、panic、Cache error、断言或看门狗。API 报告镜像 valid／已确认、保存的壁纸桌面恢复、刷新失败 0。仍有既存 I²S disable 与唤醒前端内存告警，未进行音频或联合负载验收。
- 同一设备的最终单次样本有 143 次开机轮询，均值 27.6 ms、p95 桶上界 80 ms、最长 169.0 ms；超过 50 ms 为 15/143（10.5%，中间版 11/72，15.3%），58 次成功刷新、1 次整屏、最大刷新 94.1 ms。两次启动网络就绪时间不同，不能把像素总量与轮询次数直接作为相同负载对照。轮询间隔包括静止等待和主动休眠，并非显示帧率。实屏观感及剩余长帧归因待目视复验，不能宣称卡顿完全消除。日志仅留本机忽略目录。

## 最新接续（2026-11-11）：上电花屏与首个黑帧

- 用户报告刚通电短暂花屏。上一版设备日志在 1641 ms 报告 ST7789 已开启，2734 ms 才进入 LVGL 开场；源码确认这段时间没有先写入已知画面。这个时序证实了固件接管后的暴露窗口，不能单独证明上电瞬间的面板行为。
- 由现有 SPI LCD 驱动在 ST7789 复位和方向设置后保持显示关闭，使用最多八行的临时 DMA 缓冲写满可视区域，等 SPI 传完后才开启面板。配置了可控背光的机型在这段时间保持背光关闭；本设备 RST／BL 为 -1，不能在 SPI 接管前控制面板。三线 ST7789 将开屏交回同一驱动；该支路未上板验证。未改接线、LVGL 生命周期或开机动画。
- 主机回归、9 项 Python 工具测试、路径审计（2143 条编译单元／13 个本地组件）和 diff 检查通过。ESP-IDF 6.0.1／jobs 2 构建任务 `0f62139d-efc9-4c48-b382-554e679c8ebd`，镜像 3,697,920 bytes，SHA-256 `9dca7779d92517054330b5118daef87082365eab93ca5178d0a10e2a4ec3405c`。没有用主机测试替代实体面板和冷上电复验。
- 依据开机任务的烧录授权，核对设备身份、实体分区、选中 OTA 槽和应用摘要后，仅通过 USB 写入 `ota_1 @ 0x620000`，写后校验通过。串口 ELF 前缀 `195bad402` 匹配；1667 ms 黑帧已写完、1668 ms 面板就绪、2758 ms 开场、6215 ms 退出开场。40 秒串口记录未见 panic 或看门狗；API 重试后报告镜像 valid／已确认、保存壁纸桌面恢复、刷新失败 0。串口与 API 证实软件时序，断电重插后的实体画面仍需目视确认。

## 最新接续（2026-11-11）：首次校时显示数字

- **P1 / A-BOOT、A-UI。** 用户报告冷上电时，首页由未校时占位切到真实数字的一瞬卡顿。上一版串口开场在 6215 ms 退出，7715 ms 才校时；据此用生产首页和 LVGL 模拟开场退出后从占位到有效时间的那帧。原版 TinyTTF 在第一次实际绘制数字时同步生成 A8 掩码；同样的内容更新和 17,304 像素刷新，主机首轮绘制明显慢于暖缓存复绘。该对照验证了一个额外开销，无法排除真机的传输或调度争用。
- 两种 Teko 字号改用同一 LVGL 版本离线生成的只读 A8 位图，直接从 Flash 读取，移除两个数字 TinyTTF 实例及运行时字形缓存。继续由现有显示任务消费校时状态；保持未校时占位、字体形状、布局及开场节奏。主机验证两种字号共 24 个字形的度量和透明度掩码，与原字体逐字节一致；占位帧和首次有效帧逐像素一致，开场和壁纸性能回归通过。可重复生成相同位图源码。
- 完整主机回归、9 项 Python 工具测试、路径审计（2143 个编译单元／13 个本地组件）及 diff 检查通过。ESP-IDF 6.0.1／jobs 2，受管构建 UUID `bdcaf89d-cffc-44e8-9152-f05d22b81096`，应用 3,720,128 bytes，SHA-256 `d02de2657397ae04266b0ce1834c184a25f07970c8ec69d5e0c1fd4c0edc806a`，比上一版多 22,208 bytes。链接图确认位图在只读 Flash；依赖锁无差异。
- 按开机任务的烧录授权核对 ROM 身份、实体分区、OTA 选择和摘要后，仅通过 USB 安装至 `ota_1 @ 0x620000`，写后校验通过。串口 ELF 前缀 `25f6a2c33` 匹配；6190 ms 退出开场、9733 ms 校时，覆盖开场之后才得到真实时间的场景。40 秒串口捕获未见 panic、断言或看门狗；API 确认镜像 valid／已确认、壁纸首页恢复、刷新失败 0。开场计时早于本次校时，不用于估算首个真实数字帧。冷上电实屏观感和音频联合负载未验收。

## 最新接续（2026-11-11）：鲸鱼娘全动作与 ESP32 交互

- **P3 / A-PET、A-INPUT、A-SOURCE、A-RECOVERY：源码、主机和受管构建通过。** 固定上游 WebM／GIF 原件保留。SD V2 生成 114 段／13,442 帧／1,123 分段；逐帧 CRC、逐片段摘要及包摘要校验通过，ZIP 完整性检查通过。包（索引＋按路径排序的分段）387,183,608 bytes，SHA-256 `39c6f3757a439a2b0966c5905f049d95de46ceee215846bb27dc4991c93c74bf`。240×240 主机素材预览覆盖待机、点击和三段睡眠；非设备录像。
- 动作目录复用 AmoledOS 现有列表，七行复用，支持旋钮和上下滑动；确认回宠物桌面，点播一次。睡眠、叫醒、随机小动作和本地短句由显示所有者管理；既有 Pet 会话和时间所有者提供真实字幕和待处理提醒，未新增可写副本。新增有界甩抛、弹弓和 MPU6050 反向加速度峰值检测；左滑优先于宠物手势，点宠物不控制音乐。
- 现有网络／存储执行器每次读取一个带 CRC32 的 RGB565 帧；V2 分段不超过 12 帧，避免在大文件上反复遍历 FAT 链。读取有界、同一 LCD 所有者差分绘制，退出释放像素缓冲。主机回归覆盖 V1 回退、V2 尾段、缺段、目录确认和睡眠／点播／手势优先级。具体安装见 [角色方案](06-pet-character.md)。
- **验证：** `node test/run_host_tests.mjs`、9 项 Python 工具测试、路径审计（2143 个编译单元／13 个本地组件）、`git diff --check` 通过。ESP-IDF 6.0.1／jobs 2，受管任务 `f1bd386f-e7c0-4e84-932a-33ef5f81bfab`，应用 3,733,600 bytes，SHA-256 `08f338fc3ed69f24e9846b54175a2401a0fc9b2449aad58ef125047c014ee056`。依赖锁、基线版本和分区未改。
- **未执行：** 本轮未烧录或写入实体 SD；真机触摸手感、IMU 方向／误触、动画实际帧率、云端声音／字幕、左右声道和 SD 联合负载待验。独立 TTS、持久记忆、编辑转写、图片对话、音效和自定义包尚未交付。多开、余额查询和 DSH 工作状态联动按范围排除。

### USB 安装与显示任务栈修正

- 用户授权 USB 烧录；核对 ROM 设备身份、安全配置、实体分区表校验及有效 OTA 选择，只向 `ota_1 @ 0x620000` 写入应用。未写 SD 动画包、分区表、NVS 或另一应用槽。
- 原构建 `f1bd386f-e7c0-4e84-932a-33ef5f81bfab` 写后校验通过，但真机反复在开场阶段报告 `speaker_display` 栈溢出。把显示快照和宠物动画的常驻状态移出栈、分离动画计算后，目标文件显示循环自身的栈帧从 2,144 降到 1,920 bytes；8 KiB 构建 `bdfa08bb-5ede-4346-b261-02f18e6adb98` 通过开场，但打开菜单后仍复现溢出。
- 最终保留减栈改动并将该任务的内部栈设为 12 KiB，增加每秒更新的最小余量遥测。受管构建 `21d6fa0c-e397-48f2-9421-e5bcf7be4f59`，3,733,424 bytes，SHA-256 `7e33b283300c168aafffbc35555d62d5665ddb476bd30338818012b21d13675a`。主机套件通过，最终路径审计 2143 个编译单元／13 个本地组件。
- 最终镜像写后校验通过，启动 ELF 前缀 `3bc622aca` 一致。55 秒串口捕获只出现一次启动，未见栈溢出、panic、Cache error 或看门狗。API 两次确认启动 valid，开场退出，显示 flush 失败 0；操作后栈最小余量 4,240 bytes。后续快照显示已切到宠物桌面、SD 挂载，但新版动画资源不可用；动画表现、长期负载及可闻声音仍待验。原始设备证据仅留本机忽略目录。

## 2026-11-11｜壁纸首页、角色拖动与应用入口

- A-UI / A-WALL / A-PET-TOUCH：独立宠物／表盘桌面收敛为壁纸首页；兼容旧记录的壁纸路径。左下提示「左滑进入应用」，右下鲸鱼娘；七个一级入口逐页显示，二级列表、现有功能页和音乐实体控制继续复用。
- 触摸在采样端把两轴转换到 LCD 坐标；接触从宠物开始则由拖拽拥有，空白处左滑才导航。主机回归用触摸原始坐标验证向下、向左、松手、空白处左滑和取消行为。点击空白不切换播放。
- 继续读取已安装的 SD 动画包。显示任务持有约 27 KiB PSRAM 图像缓冲区，由 LVGL 合成壁纸与角色，退出首页停止请求 SD 帧；旧包黑色像素作为透明区域。缺包使用 19.2 KB 内置待机帧。未引入第二个显示任务或改变硬件接线。
- 验证：主机套件通过，工具测试 9 项通过。240×240 LVGL 主机预览检查七个入口、字形与边界，以及拖离／隐藏角色时的壁纸像素恢复。ESP-IDF 6.0.1 受管构建任务 `0a51b736-44f5-4683-9961-9c426c870d49` 通过，应用 3,754,736 字节，最小应用槽剩余 40%；路径审计覆盖 2,146 个编译单元、13 个本地组件。
- 构建完成时尚未写入设备；后续安装和启动结果见下节。仍需实机确认屏幕方向、拖拽和左右滑动、播放时触摸及 SD 动画联合负载。

### USB 烧录与启动核验（2026-11-11）

- 按用户“为我烧录”的授权，核验 ESP32-S3 ROM 身份、16 MB Flash、分区表和安全状态（Secure Boot／Flash Encryption 均关闭）。目标构建任务 `0a51b736-44f5-4683-9961-9c426c870d49`，应用 3,754,736 bytes，SHA-256 `fb85e7cad787efad097072e115af8b113284a4ea001a4f3544704a6ab07b6053`。
- 先前写入的 `ota_0 @ 0x20000` 全槽读回与该产物逐字节相同，但启动日志显示设备继续从 `ota_1 @ 0x620000` 启动旧 ELF。随后将相同应用写入当前启动槽 `ota_1`；esptool 写后摘要校验通过。只写应用区，保留分区表、OTA 元数据、NVS、LittleFS 和 SD 卡。
- 复位后串口确认从 `0x620000` 加载，ELF SHA-256 前缀 `95c778829` 与本次 ELF 一致。约 22 秒观察内开机界面正常退出、TF 卡挂载，未见 panic／断言／看门狗；日志仍报告两条 4 KiB 无效 MP3 目录项及 `wake frontend ... asset_not_ready`。未执行实际音乐、唤醒、触摸、角色动画或联合负载验收；屏幕方向和交互观感仍待实机确认。

### 壁纸首页信息层恢复（2026-11-11）

- 根因：显示任务把首页信息层可见条件绑定到 `g_boot.active()`；开机转场结束后调用 `g_home.hide()`，所以主页只剩壁纸、角色和左滑提示。时钟／天气应用入口仍存在，天气配置和服务不是该现象的原因。
- 根因复现：生产 `SpeakerHome` 组合预览显示信息布局正常；设备源码路径则仅在 `home_visible && g_boot.active()` 时刷新视图，开机完成后 `g_home.hide()`，因此壁纸桌面没有日期、时钟和天气。角色的 80px 可见帧此前锚在 `142,141`，会遮盖日期和天气指标。
- 修正：信息层改为随壁纸首页持续显示，每秒从原有系统时钟和天气快照更新；进入菜单或设置页隐藏。保留温度、湿度读数及原进度条；按用户要求移除左滑提示。角色锚定到右下角 `160,160`，并在 96px LVGL 帧画布内放置 80px 可见帧，保证屏幕边界及透明帧绘制正确。
- 验证：生产 LVGL 预览检查有效天气、240×240 边界、壁纸合成、角色移动／隐藏后的旧区域恢复；预览中角色不覆盖日期、温湿度条或读数。`node test/run_host_tests.mjs`、`python -m unittest discover -s tools/tests -v`（9 项）、`python tools/check-project.py --build-dir .build/firmware`（2146 编译单元／13 本地组件）及 `git diff --check` 通过。
- 受管构建 UUID `ac1a2c9f-d5d3-4445-87ea-c3d774a26ba1`，ESP-IDF 6.0.1 / jobs 2，应用 3,754,384 bytes，SHA-256 `7851a0ee10ee4f36f0f37e4421893a4c17ac22553f39e9110a669f997bbf6cb0`。依用户先前的“为我烧录”授权，重新核对设备 MAC `9c:13:9e:a9:f4:6c` 后只写 `ota_1 @ 0x620000` 应用区，写后摘要校验通过；保留分区表、OTA 元数据、NVS、LittleFS 与 SD。启动日志 ELF 前缀 `870a37240` 一致，开机信息层入场并退出；18 秒观察未见 panic／看门狗。
- 天气配置／实时读数尚未通过设备 API 查询；如果首页显示“请在网页设置天气”，需在管理页检查地点、经纬度和启用状态。设备日志仍报告唤醒资源 `asset_not_ready`；与本次时钟天气显示回归无关。天气网络更新、手势与音乐联合负载以及实体屏幕观感仍待验证。
- 用户查看真机照片后要求移除首页“长按呼叫小智”空闲提示，并将右下角色上移。角色提示仅在助手活动／配对等待／失败时保留；正常传输状态和其它即时错误提示不变。角色锚点 y 从 200 调至 185（可见 80px 帧从 y=160 上移至 145），x 保持 160。
- 更新后的生产 LVGL 预览、全量主机套件、9 项工具测试、路径审计（2146 编译单元／13 本地组件）和 `git diff --check` 通过。受管构建 UUID `7974fdaa-b18f-4639-bd7d-0705619dba94`，ESP-IDF 6.0.1 / jobs 2，应用 3,754,240 bytes，SHA-256 `0eee5b0e9073de481443abd741bbe1ddc0577872259afadecb49e57390c1c95c`。按持续烧录授权，只写 `ota_1 @ 0x620000` 应用区，esptool 写后摘要校验通过；启动 ELF 前缀 `e395fc7fa` 匹配。16 秒串口观察确认开机转场结束、TF 卡挂载，未见 panic／看门狗。主机预览不能替代实体屏幕照片复核；天气实时数据配置状态仍待设备 API 核验。
- 用户补充天气图标需保留，删除的是冗余文字。首页保留城市、按真实天气代码绘制的图标、温湿度及其读数；移除实时／离线等徽标、天气名称和有效读数年龄。天气无读数时保留短提示。时钟、日期、指标区整体上移 8 px 以收紧无天气副标题后的垂直空隙。
- 生产 LVGL 240×240 预览通过，显示城市／天气图标／时钟／日期／温湿度／角色，并覆盖有效天气、长地点、未校时及壁纸合成；主机套件、工具测试 9 项、路径审计（2146 编译单元／13 本地组件）、`git diff --check` 通过。受管构建 UUID `ff89b98b-8af5-4473-b025-253a2038bff6`，ESP-IDF 6.0.1 / jobs 2，应用 3,752,176 bytes，SHA-256 `29d66470cc08a625cc7ccfc9d1a3be84a788e0c16e6c5272ae9eb023eb3457ff`。
- 按持续 USB 烧录授权，只写 `ota_1 @ 0x620000` 应用区，写后摘要校验通过；设备启动 ELF 前缀 `a7a3b37ca` 与构建一致。15 秒串口观察确认 TF 卡挂载、开机转场正常结束，未见 panic／看门狗。实屏排版仍请以设备为准；天气配置读取数值与网络刷新状态继续在信息页／网页查看。

## 2026-11-11｜全局输入、单轮对话与待机唤醒

- **A-INPUT / A-UI / A-AI / A-GAME：源码、主机和受管构建通过，真机待验。** 首页旋钮调音量，左右短按上一首／下一首，空白处左滑进入应用。应用选择页旋钮／横向滑动切换且两端停止，左键／点按进入，右键短按回首页；本次开机记住选中项，重启从第一项开始。二级菜单旋钮／纵向滑动选择，左键确认，右键逐级返回，横向滑动不导航。设置内现有网络／信息／账号页面返回设置层；配网返回网络页。音乐左键播放／暂停、旋钮音量，番茄钟左键开始／暂停／继续，木鱼左键敲击，2048 仅四向触摸移动。
- 左键长按呼叫助手，右键长按取消并回首页；均在稳定按下 800 ms 时触发一次，松手不带出短按，双键同按不执行动作。**没有长按文字、进度条或预备动画。** 页面／助手输入所有者切换时丢弃旧按压、触摸和旋钮积压。触摸长按不呼叫助手，宠物按住后仍可拖动。复用现有输入轮询、产品路由和功能所有者，没有另建路由或业务状态。
- Pet 公开增加明确的 `request_listening()`／`cancel_conversation()`，返回值仅表示接纳请求；`interaction_active` 区分真实前台对话与后台 MQTT 连接。任意页面左键长按或“你好小智”唤醒发起单轮，自动停句，回答后恢复来源页和仍有效的播放意图；回答中再次长按左键沿用会话重新聆听。连接／聆听／思考／回答期间右键短按取消，右键长按取消并回首页。全屏会话层复用现有宠物和字幕，输入恢复不重建页面。自定义“大肥鱼”模型暂缓，设备停用／麦克风静音策略继续有效。
- 取消／重新聆听以会话代际立即失效旧工作，优先处理控制请求，清理旧上传、下行队列及待处理工具消息；音频停止、焦点获取和共享时钟确认在运行时锁外执行。阶段／会话过滤拒绝已取消回复，焦点恢复由既有播放意图所有者管理。部分云端音频没有逐轮身份，主机覆盖代际与阶段过滤，实际延迟包到达时序仍需云端联合验收。
- 待机麦克风沿用现有 I²S0 音乐时钟，不以启用采集触发静音或改为 16 kHz；采集边界有界转换为 16 kHz，保持左右槽选择和增益。时钟／采集生命周期切换丢弃跨代数据，回答结束恢复播放器最新请求的音乐采样率，超时的已提交时钟切换仍负责恢复。没有新建音频任务、播放器或 I²S 所有者。实际唤醒率、音乐串音、可闻断音和联合负载待真机验证。
- 出厂 LittleFS 暂存由锁定的公开 ESP-SR 组件生成 `wn9s_nihaoxiaozhi` 模型，保留来源许可证；不新增组件依赖或分区。`managed/model.bin` 为 125,951 bytes，SHA-256 `6400b3ff9c677da8be3bec023240a4d63af8d5279ae50aa49fec31497c4a6247`，revision `6400b3ff9c677da8`。已有设备仅更新应用不会安装缺失模型；后续应通过现有 managed-assets 安装接口提供托管的 HTTPS 资源及相应摘要／版本／长度，不能为补模型覆盖用户文件系统。生成的出厂元数据 URL 为空，不代表已发布或已安装。
- **验证：** 最终 `node test/run_host_tests.mjs` 全量通过，包含实际输入轮询、层级返回、长按阈值／时钟回绕、旧输入隔离、2048 触摸、取消／重听／焦点竞争、模型格式及截断拒绝、8／16／22.05／32／44.1／48 kHz 采集与音乐采样率恢复。`python3 -m unittest discover -s tools/tests -v` 12 项通过；生产 LVGL 240×240 预览通过，检查真实应用页上对话层、两行字幕和退出后的逐像素恢复。`python3 tools/check-project.py --build-dir .build/firmware` 通过，覆盖 2,146 个编译单元、13 个本地组件；`git diff --check` 通过。本地预览位于 `.local/ui-preview/interaction/`，不是设备截图。
- **最终构建：** ESP-IDF 6.0.1／jobs 2，受管任务 UUID `7f7aa352-e700-435b-a2ed-91cc821ca868`，状态 succeeded。应用 3,756,112 bytes，SHA-256 `40f87db62f66467320d2bfffd4fafe630533d28df4e66a5afdd3f6ae2a01a761`，最小应用槽剩余约 40%。本轮没有烧录、OTA、资源上传或发布；未进行当前镜像的屏幕、按键／触摸手感、声音、模型加载／内存、云端对话和音乐／游戏唤醒联合负载验收。历史镜像的启动或视觉结果不能代替本轮验收。

### USB 烧录与启动确认（2026-11-11）

- 用户授权烧录后，设备 API 与当前 USB ROM 读取均确认为 ESP32-S3 MAC `9c:13:9e:a9:f4:6c`（设备序列号 `LUNAR-9C139EA9F46C`）。安全启动及 Flash Encryption 关闭。实体分区表与构建的 OTA 槽地址／长度一致；`ota_1` 已由有效 OTA sequence 2 选中，地址 `0x620000`，大小 6 MiB。只写应用到当前槽，未触碰分区表、otadata、NVS、LittleFS、另一应用槽或 SD。
- 受管镜像 `lunaredu-smart-speaker.bin` 为 3,756,112 bytes，SHA-256 `40f87db62f66467320d2bfffd4fafe630533d28df4e66a5afdd3f6ae2a01a761`。esptool 写后报告 `Hash of data verified.`；设备随后通过 `/api/v1/modules/smart_speaker/ota.status.get` 确认 `boot_confirmed=true`、`pending_verify=false`、`running_image_state=valid`、版本 1.0.35、构建时间 19:05:19。API 同时确认显示初始化、LittleFS ready、TF mounted、Wi-Fi connected；对话未运行。
- 复位后约 25 秒串口观察未见 panic、assert 或看门狗；采集开始时设备 uptime 已约 52 秒，因此这不是完整冷启动日志。日志报告 `wake frontend init deferred ... internal largest block=7680 need=12288`，设备 API 的 `wake_word_available=false`、`wake_word_model` 为空。当前设备 LittleFS 缺少本轮新增唤醒模型；而日志同时显示 ESP-SR 前端所需连续内部内存块大于当时可用值，因此仅补装模型也不保证唤醒能够初始化。该限制待单独排查，不能报告全页面唤醒已在实体设备通过。
- 实体分区表的 otadata／filesystem 加密标志与当前构建表不同（设备为 0、构建为 1）；本次未改分区表，且安全信息显示 Flash Encryption 关闭。无屏幕照片、按键／触摸手感、可闻输出、云端单轮对话或音乐／游戏联合负载验收。本机串口记录保存在忽略目录 `.local/interaction-flash-20261111/`。

## 2026-11-12：一级菜单返回与小星 LunarLink 平台

用户要求改用指定 `xiaoxing` 产品的平台，并修复一级菜单右短按露出设置页。本次继续在 1.0.35、当前接线及既有交互修复上开发；保留任务开始时全部工作区修改。

菜单问题已用生产显示缓存条件复现：导航已经回 Desktop，产品 shell 的面板可见状态却没有参与旧缓存命中，提前返回留下共享面板的默认“设置”。缓存现在同时核对 overlay 所有权；主机回归验证一级菜单退回时面板隐藏，既有导航和二级逐层返回回归继续通过。没有改变按键或触摸语义。

助手改接小星使用的 Board + LunarLink gateway：原 DeviceLink 对齐 `/api/link/...` 配对、presence、发现、固件票据和按代次确认解绑；原 Pet conversation 接入 bound identity bootstrap、wire v1 MQTT、Opus20、AES-GCM UDP v2、逐轮消息隔离、原业务工具与幂等回执、真实音频写入后的播放完成回执。旧小智凭据及端点不恢复到新平台；用户启用、麦克风和待机偏好仍由原有所有者管理。新平台持久 `operation.invoke` 远控未迁移，本产品显式关闭旧 `commandType` 轮询；本地 API 与本次语音工具继续复用。完整契约、资源生命周期及限制见 [平台接入](07-lunarlink-platform.md)。

验证结果：

- `node test/run_host_tests.mjs` 全套通过，包含新增显示缓存、wire/GCM、bootstrap、Board 绑定、异步作业代次、原业务工具幂等回执测试。之后针对最终协议的 session close、播放完整度、等效 GCM nonce 与开发环境隔离修改，又运行对应的协议／工具／作业回归，通过。
- `python3 -m unittest discover -s tools/tests -v`：12 项通过。
- ESP-IDF 6.0.1，`python tools/build.py --jobs 2` 最终任务 **`3870c42c-9068-4ff8-ba20-f2c5681ab904`** 成功。初次迁移构建中的生命周期源码编译错误已修复并通过受管 resume；最终交付是后续独立成功任务的产物。
- 最终 `python3 tools/check-project.py --build-dir .build/firmware` 通过：2,148 个编译源单元、13 个本地组件；包含已修复 ISR 的最终 ELF 路径检查。`git diff --check` 通过。

最终应用 `lunaredu-smart-speaker.bin`：**3,816,448 bytes**，SHA-256：

```text
6e4fca54f1c191e34100549e527346319061b2a5988388a096a12ea77d08d4f6
```

设备与验收边界：本次未烧录、未 OTA、未使用设备凭据发起实际平台请求；未覆盖 NVS、文件系统或 SD。A-UI 实体屏幕上的一级返回、A-AI 实际 Board 绑定和 gateway 会话／收音／播放、账号切换及联合负载仍未验证。原唤醒模型资源未安装和内部连续内存不足的真机观察继续有效，本次平台改接不能代替该项修复或验收。没有提交、推送或发布。

### 2026-11-12 一级应用入口简化（A-UI）

- 七个入口统一移除功能小字介绍与底部“左右切换／点按进入”提示，保留应用名称和分页圆点。图标底板从 90×90 调整为 120×120，内部图形与圆角同步放大，标题及分页指示重新排布。输入和导航行为不变。
- 使用 `cmake --build .local/ui-preview/build --parallel 2` 编译生产视图，运行 `home_preview` 输出七页 240×240 预览；现有主机预览断言通过，并目视核对音乐、时间管理和设置页面，无文字或图标遮挡。预览保存在 `.local/ui-preview/launcher-simplified/`。
- 本次为纯布局修改，未运行固件构建、烧录或真机验收。

- 后续用户授权烧录：受管构建 `32bdc3ad-1994-4edb-860e-eb5f04f813e3` 成功，应用 3,759,936 bytes，SHA-256 `9d8e162ee98df39cdc1d9ed2145e8641ba6b49962c09b40358852db7ac2e83ce`；项目路径审计通过（13 个本地组件、2,148 个编译单元）。核对 ROM 身份、实机分区与 OTA 选择后，仅写入 `ota_1`（`0x620000`），写入校验通过。重启后 40 秒串口记录的 ELF 摘要与本次构建一致，字体就绪、开场退出，未见 panic／看门狗；屏幕视觉与触摸手感仍待用户实测。证据位于 `.local/ui-launcher-flash-20261112/`。
