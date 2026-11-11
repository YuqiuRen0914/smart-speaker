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


命令末尾可加 `main/assets/home_pet_idle.bin`，渲染实际壁纸首页视图和七个应用入口。输出 `wallpaper-home.ppm`、`app-1.ppm` 至 `app-7.ppm`，并检查在显式测试壁纸上拖动或隐藏宠物后，旧位置会完整恢复，SD 帧的透明区域仍显示壁纸。

宠物帧更新只刷新实际精灵尺寸：默认帧的损伤面积由 96×96（9,216 像素）缩至 80×80（6,400 像素），画面逐像素一致。96／73／80 像素尺寸切换后的局部刷新与整屏重绘结果相同，移动和隐藏后恢复背景。这些像素数量不等同于真机帧率。
