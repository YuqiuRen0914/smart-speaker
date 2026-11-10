# LunarEdu Audio

共享 PCM 音频访问、音量处理和输出参考缓存。Pet 与 Settings 使用本组件；I²S 通道和引脚由独立外设管理。唤醒词、模型和语音处理归 Pet，本组件不依赖 ESP-SR。

`src/local_pcm_audio_bridge.cpp` 只依赖 `PcmBackend` 接口；具体 I²S 装配集中在
`src/backends/i2s.cpp`。禁用音频时只编译禁用实现。

输出参考使用启动时分配的固定环形缓冲，容量由 `LUNAREDU_AUDIO_REFERENCE_SAMPLES`
配置，默认 8000 个样本（16000 字节，16 kHz 下 500 ms）。满时丢弃最旧样本并计数，
关闭输出或重配采样率会清空。持续输入不再保留无消费者的历史缓存。
