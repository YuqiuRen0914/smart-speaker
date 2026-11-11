# dsh-pet 完整动画归档

保存日期：2026-11-11。来源：[PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet)，固定提交 [`c25386848f70f6e40902e4376f6e7b1e196bc326`](https://github.com/PC2005-cloud/dsh-pet/tree/c25386848f70f6e40902e4376f6e7b1e196bc326)。上游后续新增动作不会自动进入本归档。

| 内容 | 路径 | 数量／大小 |
| --- | --- | --- |
| 全部原始角色动画 | [WebM](upstream/dsh-pet/assets/webm/) | 114 个，57,832,768 bytes |
| 全部上游动画预览 | [GIF](upstream/dsh-pet/assets/preview/) | 109 个，69,079,427 bytes |
| 全部提示词配方 | [prompts](upstream/prompts/) | 3 个 |
| 配置、角色原图、文档与交互参考源码 | [upstream](upstream/) | 92 个 |

共 318 个上游文件、128,952,411 bytes（约 123 MiB），均原样保存。GIF 少于 WebM 是上游原有情况。所有 WebM 均为 640×360、24fps 的 VP9，共 26,997 帧，总时长 1,124.912 秒。帧数与 alpha 标记由 ffprobe 读取，不等同于透明边缘或设备画面的验证。

[逐文件清单](source-manifest.json)记录路径、字节数、Git blob SHA-1 和 SHA-256；[动作目录](animation-catalog.json)记录每段时长、分辨率和帧数。按未截断的上游提交树核对了两个素材目录的完整性。源码仅供参考，不包含可运行的完整桌面插件。

在仓库根目录运行以下命令：

```sh
python3 tools/resource-tools/dsh_pet_assets.py
python3 tools/resource-tools/dsh_pet_assets.py --fetch
python3 tools/resource-tools/dsh_pet_assets.py --catalog
```

第一条离线校验。第二条用 curl 恢复缺失原件，不覆盖现有文件。第三条用 ffprobe 重建动作目录。工具不访问设备。

保留上游 [LICENSE](upstream/LICENSE) 和 [README 的许可说明](upstream/README.md#许可)。代码为 MIT；上游 README 将角色形象授权指向 **CC BY-NC-SA 4.0（署名、非商业、相同方式共享）**，并要求公开衍生作品附作者仓库链接。原型「溟月」来自 B 站 @上善无形，女仆鲸鱼娘设计来自 @ZipZipPipe。这是社区二创形象，代码 MIT 不授予该形象的商业使用许可。

完整 114 段派生动画由 SD 卡加载，已有 Pebble／legacy 大肥鱼路径提供回退。此归档不编译进固件；安装和交互适配见 [角色方案](../../../../docs/development/06-pet-character.md)。
