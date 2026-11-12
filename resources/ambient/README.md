# 环境声资源

2026-11-12：四个场景为雨窗、轻柔潮水、森林、咖啡馆。海岸使用用户选定的
Alex_hears_things「Gentle small waves lapping on shore.wav」；不使用先前的 Calm ocean waves。
来源、作者、CC0 链接、输入和输出 SHA-256、长度及接缝测量见 manifest.json。
Freesound 的三个输入是公开高质量 MP3 预听版本，不宣称为无损原始录音；
咖啡馆输入为原作者 BigSoundBank 公开 MP3。

## 安装

把生成资源包中的 ambient 目录复制到 SD 根目录，或在新版网页“环境声”区域，
停止播放后选择 rain.wav、shore.wav、forest.wav、cafe.wav 并安装。可以分批安装，
失败或取消只重试对应文件。声音不放入 music 曲库。资源安装不要求设备联网下载；
网页安装需要局域网。固件构建、烧录应用和安装 SD 音源是三个独立步骤。

设备仅接受规范 PCM WAV：44 字节 RIFF 头、22050 Hz、16-bit、双声道、
无额外 chunks、至少 1 秒、数据至多 64 MiB。错误文件、缺卡和写入失败明确报错。
成功校验后才发布，FAT 覆盖保留 .bak；若发布期间断电，播放可读取保留的旧副本。
正在播放、暂停或语音占用时拒绝安装。请勿在资源读写中拔卡。

## 再生成

将四个源 MP3 放入自选输入目录，以 rain/shore/forest/cafe.mp3 命名，然后执行：

    python3 tools/resource-tools/prepare_ambient.py INPUT_DIR OUTPUT_SD_ROOT

需要主机 FFmpeg；Python 使用标准库。处理包括 50 Hz 高通、8 kHz 低通、
-26 LUFS 目标音量、-8 dBTP 目标峰值以及 1.5 秒首尾交叉淡化。
派生文件和新 manifest 一起保存。自动测量不代替试听和设备扬声器验收。

默认插画使用内置 image_gen 生成，原 PNG、240×240 BMP 和提示词保存在
main/assets/ambient/。用已有资源工具依赖 Pillow 运行
tools/resource-tools/prepare_ambient_wallpapers.py 可再生成 BMP 和 RGB565 C 数据。
默认四张占固件 460800 字节；用户替换图由现有壁纸资源所有者持有不可变 PSRAM 缓存。

## 操作和边界

- 首次进入预览四个场景，滑动选择、轻点或左键播放；以后进入恢复上次成功选择并播放。
- 播放页左滑下一场景、右滑上一场景，首尾循环；旋钮音量、左键暂停/继续、右键返回。
- 平时仅壁纸；操作提示约 3.5 秒隐藏。轻点显示浮层，再点底部定时入口。
  定时选项保持可见，触摸选项立即设置，右键只关闭选项。
- 默认持续播放；可选 30/60/90 分钟。按单调时间计算，切场景、暂停、离开页面及语音对话不延后；
  最后 5 秒只衰减环境声源。到点停止，语音结束不恢复过期声音。
- 返回菜单/时钟继续播放；成功开始本地音乐取消环境声定时。
  语音对话按已有代际/播放意图恢复。游戏仍遵循已有音频焦点规则。
- 只持久化上次成功场景及四个壁纸关联；重启不自动发声、不恢复旧倒计时。
  回到环境声页才开始播放。手动停止取消定时，暂停保留定时。
- 使用现有 wallpaper.upload 上传图片，再通过 ambient.wallpaper 分配或用空名称恢复默认。
  正在被场景引用的图片不能删除；保存失败保留原图。自选图损坏时显示默认图和明确提示。
- ambient.get 返回资源安装状态；control 的 ambient(scene) / ambient_timer(minutes)
  返回入队结果，实际播放/错误须读取 status。旧 white_noise 控制名保留为兼容入口，
  实际源改为环境声；status.source 为 ambient。

未进行本轮烧录或实体验收。需在真机验证循环听感、触摸命中、SD 并发、语音恢复、
定时末段渐弱、拔卡错误和长时间运行。不得用主机测量或构建通过替代这些结论。
