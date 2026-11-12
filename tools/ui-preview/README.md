# 240×240 界面主机预览

直接编译生产 `smart_speaker_home.cpp` 和锁定的 LVGL 9.5.0，以 RGB565 虚拟屏幕输出 PPM；这不是重新用 HTML 绘制的示意图。所有时间、地点、天气和纯白背景均为显式主机测试夹具，不进入固件；没有加载用户背景，也不访问设备或网络。不证明 LCD 色彩、触摸、PSRAM 余量、刷新性能或音频联合负载通过。

首次受管固件构建获取 `managed_components/` 后，安装本机 CMake 和 C++20 编译器：

```sh
cmake -S tools/ui-preview -B .local/ui-preview/build -DCMAKE_BUILD_TYPE=Release
cmake --build .local/ui-preview/build --parallel 2
.local/ui-preview/build/home_preview lunaros/assets/fonts/lunar-sans-sc.ttf main/assets/home_digits.ttf .local/ui-preview/renders
```

这里的 CMake 只构建主机预览；固件仍须使用 `python tools/build.py --jobs 2`。断言在 Release 下保持开启：检查显示状态格式、240×240 对象边界、双位数字宽度、实际字体覆盖、反复隐藏／显示时对象复用，以及禁止天气网络调用。覆盖午夜、23:59、雪、多云、长地名、离线、未校时、未配置和缺失背景。纯白背景用于文字对比度压力检查。

同一工具也编译生产 `smart_speaker_boot.cpp`，输出 `boot-*.ppm` 和 110 帧 `boot-frame-NNN.ppm`。网络时序是显式夹具：1.6 秒 Wi-Fi 连接、2.4 秒校时有效。黄色细线在 420 ms 展开；380 ms 起让首页的真实小时／分钟控件在裁切容器内上移显露，未校时使用 `--`；就绪后用 760 ms 平移到首页，秒数／日期／天气／温湿度分别从第 300／470／620／800 ms 开始显露，背景在第 650–1400 ms 从上往下以柔边揭示。底部真实网络提示与版本在退场前 260 ms 消隐。实际设备显示刷新与调度可能延长墙钟时间。

首页显示任务持有五个小型透明裁切容器，子控件不设置整体 `opa`，没有整屏截图、离屏 alpha 合成层或时钟副本。动画结束时裁切高度及位置回归正常布局；以原版首页渲染逐像素对比确认最终样式未变。网络服务／壁纸缓存及时间快照保持原所有者；无新增 LVGL 定时器或固件任务。现行固件只在开场呈现这组时钟信息。开场后由 LVGL 合成壁纸和鲸鱼娘；配网页保留黑底细线及末段渐隐。

断言检查无网／无校时从首次更新起约 5.9 秒退出、需配网与黑底回退、毫秒计数回绕、重连不重播、时钟显露／等待／平移／信息入场各阶段取消恢复、100 次创建／取消无遗留控件。终点及取消后的帧与完整首页逐像素一致；纯白背景在开场期间不泄漏，末段逐步进入，退出恢复正常对比度。主机上跳跃时序验证状态边界，逐帧预览用 40 ms 步长；不能当作真机帧率或负载证据。

动画性能回归同时在真实视图更新／LVGL flush 接口统计每个 20 ms 显示轮询的刷新像素，输出 `boot-render-work.csv`（时间、像素、分块数、主机绘制耗时）。内容按生产路径的一秒节奏更新；断言 1.2–1.6 秒未变化的等待姿态没有新增刷新。这个断言在修复前稳定失败：201 次轮询共 11,059,200 像素、192 次整屏；只移除重复样式写入后为 1,357,683 像素、2 次整屏，等待姿态 0 像素。另输出亮背景的 `boot-render-work-bright.csv`，检查壁纸揭示区间的局部刷新量与逐帧全刷等价；实测从 1,670,400 降到 582,240 像素。110 帧无背景动画与修复前逐像素一致。主机微秒值不等于 ESP32 耗时，不能把工作量下降直接换算为实屏 FPS。

设备 `display.stats.get` 的 `boot_*` 指标只记录本次开机区间；帧间隔包含绘制、调度及剩余帧预算等待，p95 使用 10 ms 桶（溢出桶返回实测最大值），刷新计数／像素使用已有 LVGL 所有者统计的差值。采样直方图与累加器由常驻显示任务独占，放在静态存储中，不增加其任务栈、不创建新任务或改变优先级。等待姿态也计入帧间隔，平均值不能独立代表运动阶段的顺滑程度。

生成 GIF（需要壁纸工具的 Pillow 依赖；循环仅用于预览）：

```sh
python - <<'PY'
from pathlib import Path
from PIL import Image
frames = [Image.open(p) for p in sorted(Path('.local/ui-preview/renders').glob('boot-frame-*.ppm'))]
frames[0].save('.local/ui-preview/renders/boot-choreography-preview.gif', save_all=True, append_images=frames[1:], duration=40, loop=0)
PY
```

数字字体已随源码提供，常规编译无需 FontTools。重新生成时使用独立环境和锁定依赖：

```sh
python -m venv .local/ui-font-tools
.local/ui-font-tools/bin/python -m pip install -r tools/resource-tools/clock-font-requirements.txt
.local/ui-font-tools/bin/python tools/resource-tools/prepare_clock_font.py
cmake --build .local/ui-preview/build --target export_clock_font --parallel 2
.local/ui-preview/build/export_clock_font main/assets/home_digits.ttf > main/assets/home_clock_font.c
```

子集生成器校验原字体摘要，输出固定 weight 400、等宽数字及收紧行框的 OFL 子集。来源与许可在 `components/lunaredu_character_assets/third_party/teko/`；子集 2,472 bytes，SHA-256 `b0e04f6d8471aca2528da0a222583b08bdb4cdd98b8887a5fc6086d069c9a969`。位图生成器从锁定的 TinyTTF 提取两个字号的 A8 掩码和字距。预览逐个比对 24 个字形，并用生产首页验证开场之后从 `--:--` 变成真实时间的画面与原版一致，输出 `clock-before-sync.ppm` 和 `clock-after-sync.ppm`。同一场景下首轮／复绘的主机耗时用于诊断，不当作实屏帧时。

可用 Pillow（离线壁纸工具的既有依赖）将 PPM 转为 PNG，例如：

```sh
python -c 'from PIL import Image; Image.open(".local/ui-preview/renders/home-snow-reference.ppm").save(".local/ui-preview/renders/home-snow-reference.png")'
```


命令末尾可加 `main/assets/home_pet_idle.bin`，渲染实际壁纸首页视图和六个应用入口。输出 `wallpaper-home.ppm`、`app-1.ppm` 至 `app-6.ppm`，并检查在显式测试壁纸上拖动或隐藏宠物后，旧位置会完整恢复，SD 帧的透明区域仍显示壁纸。

宠物帧更新只刷新实际精灵尺寸：默认帧的损伤面积由 96×96（9,216 像素）缩至 80×80（6,400 像素），画面逐像素一致。96／73／80 像素尺寸切换后的局部刷新与整屏重绘结果相同，移动和隐藏后恢复背景。这些像素数量不等同于真机帧率。

## 电子木鱼

同一 `home_preview` 也编译生产 `WoodFishView`，输出 `wood-fish-*.ppm`，覆盖静止、落槌、飘字、动画结束、页面重入、连续 100 次敲击和毫秒回绕。`wood-fish-continuous.ppm` 显示三次敲击的独立 +1；断言检查其同时存活、结束后全部隐藏及始终位于 y=40–88 的上方区域，不与 y≥91 的木鱼／木槌重叠。断言检查 240×240 边界、字体覆盖、动画结束后的像素恢复及对象数量稳定。主机图使用显式计数夹具，不访问设备；不能替代真机帧率、音画同步与人耳验收。

主体源图为 `main/assets/wood_fish.svg`，按用户提供的浅色剪影参考重新绘制；`wood_fish_mask.c` 是其 160×124 A8 掩码（19,840 字节），固件无需 SVG 解码器或 SD 素材。离线转换使用 `@resvg/resvg-js` 2.6.2 渲染透明 PNG，再以 Pillow 读取 alpha 通道逐行输出 C 数组；工具仅在本地资源准备环境使用，不加入固件依赖。木槌由 LVGL 绘制，所有控件复用，无每次敲击创建的对象或定时器。

### 音乐唱片页

预览同时编译生产 `smart_speaker_music_view.cpp` 和频谱分析器。输出 `music-frame-000.ppm` 至 `music-frame-079.ppm`、默认黑胶、暂停、长歌名／长歌词与封面切换画面。频谱来自主机显式合成的 250 Hz／1800 Hz PCM，经生产 FFT 计算；封面是测试渐变图，均不进入产品默认数据。检查动画局部刷新、暂停稳定后零刷新、全量与增量重绘一致、100 次进入退出与背景恢复。主机渲染不是实屏帧率或声音连续性证明。

音乐页同时输出 `music-render-work.csv`，统计每帧生产 FFT、视图更新与 LVGL 刷新的总主机耗时（不含 PPM 文件写入）。2026-11-12 修复将逐段绘制的 64 条频谱线与唱片合成一个 176×176 RGB565 图像，用定点坐标旋转，并缓存 FFT Hann 窗。相同 80 帧夹具，排除首次创建帧，平均约 120 → 47 µs；每帧刷新区域仍为 30,976 像素。这个主机对比不能换算为 ESP32 帧率。局部／全量重绘等价、暂停稳定后零刷新和 100 次进出检查通过。

2026-11-12 后续：顶部歌名按用户反馈固定、省略超长内容；歌词采用 216×28 的固定裁切区，短句居中，换句直接替换，长句停留 1.2 秒后按 24 px/s 水平移动。文字节点只允许在裁切区内横向溢出，测试单独检查其可见边界、固定 Y 坐标和换行归一；其他控件仍严格检查 240×240 对象边界。新增 `music-lyrics-horizontal.ppm` 与 `music-lyrics-single-baseline.ppm`。

直接 LCD 提交路径与 LVGL 图像输出在主机上逐像素比较；覆盖全屏重绘后的图像补画和隐藏后背景恢复。产品复用原显示所有者与 LCD 完成屏障，不由主机耗时推断实机 FPS；实机应读取 `music_presented_frames`，其提交耗时由 `last_music_transfer_us` 单独记录。

### 二级图标菜单（2026-11-12）

图形定义为本工程原创，不含第三方图标；数字使用路径绘制，不依赖主机字体。重建命令：`python3 tools/resource-tools/prepare_menu_icons.py --resvg-module .local/wood-fish-tools/node_modules/@resvg/resvg-js`。

同一生产 AppLauncher 输出 `secondary-0-0.ppm` 至 `secondary-5-2.ppm`（各模块按实际 2／3 项，共 13 页）。检查全部文字／对象边界、同页重复显示零刷新、一级圆点与二级方格切换后的像素一致性、100 次隐藏／显示对象复用。

二级菜单使用本工程原创 SVG 派生的 120×120 RGB565 静态资源，共 374,400 字节 Flash；显示时不缩放、不运行 SVG 解码，也不依赖 SD。原始图形定义及可重复转换在 `tools/resource-tools/prepare_menu_icons.py`，输出 `main/assets/secondary_menu_icons.svg` 和 `.c`。离线依赖沿用 Pillow 与 resvg 2.6.2；固件无新增第三方依赖。主机预览不能代替实体屏幕颜色、触摸与负载验收。

## 环境声

预览链接生产 AmbientView 和四张默认 RGB565 插画，输出 ambient-select/pure/controls/timer-0..3.ppm。
验证 240×240 边界、字体覆盖、浮层隐藏后逐像素恢复，以及纯壁纸静止轮询不重绘。
不包含设备 I/O、真实触摸、声音或硬件性能验收。
