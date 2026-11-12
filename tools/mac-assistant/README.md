# 大肥鱼音箱 · Mac 语音验证

此目录 `tools/mac-assistant/` 包含已确认方案的 Mac 服务、声库验证和协议联调工具，
与音箱固件在 smart-speaker 仓库中统一维护。从仓库根目录运行下列命令前，先执行
`cd tools/mac-assistant`。
smart-speaker 已烧录独立 Mac 平台配置，实体收音与 Cute F6 播报通过；REM-harness-lab 未修改。

## 已确认行为

- 音箱录音 → Mac 本地 SenseVoice → DeepSeek API → Mac FeiyuVoice Cute F6 → 音箱。
- 所有助手回答采用可爱声线；大肥鱼性格，称呼用户“主人”，自然、亲切、略俏皮。
- 普通话为主，覆盖英文歌名、人名、技术词。默认简短，实际操作结果准确。
- 停止说话至第一句有效回答以 2–3 秒为目标。声线优先，接受实测后更长等待。
- 复用既有单轮会话、取消、打断、音乐焦点与恢复语义。
- Mac 故障、断网或睡眠时，助手明确不可用，不自动换回另一声音；本地功能继续。
- 重连不补播过期回答。稳定验证之后才启用登录自启和接电保持唤醒；停止服务或拔电解除唤醒。

## 当前验证边界

先验证 Cute F6 在 M4 Pro / 48 GB 上能真实生成声音。CPU 为参考路径，
`mps-hybrid` 将 LLM 和 flow 放到 Apple GPU，HiFT 与 ONNX 前端保留 CPU。
HiFT 的原始 float64 F0 算法不变。模型、参考音频和原始上游源码保持原样。
此适配未继承发布者的 CUDA 验收结论，必须生成音频、测时并由用户试听。

本地推理不是端到端首响速度；还须计入收音结束判定、识别、网络及 DeepSeek 时间。
程序生成的音频转写测试不能代替真实音箱麦克风、回声和噪声验收。

## 文件与入口

- `.venv/`：独立 Python 3.11 环境，不改 ESP-IDF Python。
- `.runtime/CosyVoice/`：固定提交 `074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc` 及对应 Matcha 子模块。
- `.runtime/FeiyuVoice-CosyVoice3/`：发布者模型包；每个文件按来源大小与 SHA-256 验证。
- `.runtime/SenseVoice/`：采用 REM-harness-lab 已固定的 SenseVoiceSmall INT8 / tokens / Silero 模型。
- `.runtime/deepseek.env`：仅当前用户可读写的 API 密钥文件，已忽略 Git；密钥不写进报告。
- `scripts/benchmark_tts.py`：真实合成与计时，输出 WAV 和 JSON 证据。

```sh
.venv/bin/python -m pip install --build-constraint build-constraints.txt -r requirements-mac.txt
.venv/bin/python scripts/download_model.py
.venv/bin/python scripts/benchmark_tts.py --backend cpu --output results/cpu-01
.venv/bin/python scripts/benchmark_tts.py --backend mps-hybrid --stream --output results/mps-01
```

输出目录必须为新目录。合成耗时与试听质量分别报告；这里不把下载、导入成功作为可用验收。

## 当前结果与使用

用户已接受 `results/cpu-01/cute-1.wav` 的 Cute F6 声线。
`results/lan-e2e-02/report.json` 和 `results/lan-e2e-final/report.json` 为真实 DeepSeek 调用及模拟音箱 Opus 收发结果：
常规首音频约 5.71–10.40 秒；在合成中取消后，旧音频被丢弃，下一轮约 10.76 秒出声。
当前已取消的合成会完成计算后丢弃，后续合成可能因此排队；不会继续播放旧答案。
流式合成在本机慢于音频实时长度，正式服务选择整句缓冲后连续发送。
没有达到 2–3 秒首响目标。实体麦克风问候首音频约 7.31 秒，112 包播放无解码／播放失败，
用户确认声线正常。点歌与语音争用已通过原播放器队列修复，音乐等待语音结束后开始；
用户确认修复后的顺序正常、没有冲突。当前长按左键收音，现有唤醒词资源未就绪。
最终实机点歌／音乐中问候分别播放 104／80 包，失败为 0，音乐在播报后开始／续播。
Mac 服务停止时本地音乐继续，重新启动后设备自动重连，无设备重启或旧回答补播。

最终固件构建 UUID `f81ec243-6a05-4700-ae8b-5cf6a1deb07a`，
4,658,944 bytes，SHA-256 `e2867787e70500d651543ac3cca51db1fd4be010087169df862dec48739fe451`。
仅更新已核对的 ota_1 应用槽，写入哈希校验通过。

后台运行文件已安装到 `~/Library/Application Support/ShengyuVoice/`。
本机的 `.runtime` 和 `.venv` 为指向安装位置的链接，均被 Git 忽略。
迁移只移动源码维护位置，不移动安装目录中的模型、密钥或运行环境，不更改 launchd label。
原 `声屿STUDIO/voice-lab` 入口在本机保留为指向此目录的兼容链接，不再单独维护源码。
该链接、`.runtime`、`.venv` 和本地 `results/` 不包含在 Git 提交中。
原始模型约 7.45 GB；MPS 合成基准的进程峰值 RSS 约 8.37 GB（不等于全部 GPU 内存）。
Apple GPU 的首次热身与模型加载发生在服务启动阶段；健康检查就绪后才允许设备 bootstrap。

双击此目录的 `启动助手.command`、`停止助手.command`、`重启助手.command`、`助手状态.command`，
或运行：

```sh
.venv/bin/python scripts/service_ctl.py status
.venv/bin/python scripts/service_ctl.py stop
.venv/bin/python scripts/service_ctl.py start
.venv/bin/python scripts/service_ctl.py restart
```

服务以当前用户登录自启，接电时防自动系统休眠，屏幕仍可关闭。
停止服务或拔电后释放本服务的保持唤醒请求；不更改其他应用的电源设置。
手动睡眠或合盖后重建会话，旧回答不补播。设备和 Mac 必须在可信局域网内。
HTTP 8765、MQTT 18883、UDP 18884；bootstrap 需要随机本地令牌，MQTT 有密码和主题 ACL。
没有开放公网映射。DeepSeek 密钥仅保存在 `.runtime/deepseek.env`，不会编入音箱。

`scripts/service_ctl.py install` 更新安装的 Python 源码，随后 restart 生效。
原始环境版本见 `requirements-mac.lock.txt`，主机另外需要 Homebrew 的 mosquitto 和 opus。
新机器仍需准备 Python 环境、模型、离线前端和本机密钥，克隆源码不会携带这些运行资源。
先运行 provision；使用 `.runtime/sdkconfig.mac` 作为私有构建输入，不提交令牌。

协议以现有小智 MQTT/UDP 为基础，在 Mac profile 中增加轮次和方向隔离，
两端必须同时选择 Mac profile，不能把这一 UDP 扩展当成官方服务配置。
设备 MCP 工具从实际连接发现；超时不会重试写操作，工具仅受理时不宣称已执行。

验收脚本 `scripts/test_lan_e2e.py` 会发起少量真实 DeepSeek 请求，并用模拟设备进行录音上传、
回答回传和取消测试；不验证硬件。`scripts/validate_lifecycle.py` 会停止、启动并短暂挂起本服务，
用于检查停止后的资源释放及睡眠间隔恢复，不会挂起整台 Mac。
`scripts/validate_device_focus.py` 验证实体点歌、语音与音乐互斥及续播；
`scripts/validate_device_reconnect.py` 停止／启动 Mac 服务，验证本地音乐继续、
设备不重启、自动重连且旧回答不补播。两者输出到被 Git 忽略的 `results/`。

## 来源

- [FeiyuVoice 模型](https://huggingface.co/Esmerubphire/FeiyuVoice-CosyVoice3)
- [发布者服务](https://github.com/Esmerubphire/astrbot_plugin_feiyu_voice)
- [CosyVoice](https://github.com/FunAudioLLM/CosyVoice)

原模型包的 COPYRIGHT、NOTICE、许可证随包保留。此目录没有重新许可第三方权重与参考声音。
