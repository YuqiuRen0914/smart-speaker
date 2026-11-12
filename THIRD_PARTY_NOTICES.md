# 第三方来源与许可证

当前以 1.0.35 旧版为基线。公开注册表组件由 ESP-IDF Component Manager 获取，版本和内容摘要见 [dependencies.lock](dependencies.lock)，版本约束见 [cmake/dependency-constraints.txt](cmake/dependency-constraints.txt)。各下载组件的许可证在其 `managed_components/` 目录中保留。

使用的公开依赖包括 LVGL 9.5.0（MIT）、ArduinoJson 7.4.3（MIT）、esp-audio-player 1.1.0 及其 Helix MP3 依赖，以及 ESP-DSP、ESP-SR、ESP Audio Codec、MQTT、摄像头/图像组件、LittleFS 和 MPU6050 等。各组件的具体许可、版权及传递依赖条款以其对应版本原件为准；注册表公开可获取不改变许可条款。

随源码保留的中文字体为 Adobe Source 衍生子集，原件在 `lunaros/assets/fonts/lunar-sans-sc.ttf`，[SIL Open Font License 1.1](lunaros/assets/fonts/LICENSE.txt) 随工程和工厂字体存储一起交付。原版来源清单保留在 `lunaros/assets/fonts/manifest.json`。

ESP-IDF 6.0.1 及其工具链由开发者另行安装，按各自许可证使用。LunarOS、Pet、驱动和音箱第一方源码只抽取该产品依赖闭包，来源摘要见 [抽离清单](docs/extraction-manifest.json)；第一方对外开源许可证仍待仓库所有者选择，本声明不替代许可授权。

AmoledOS launcher source (MIT, Copyright (c) 2026 Charlie Gallo) is preserved in `components/lunaredu_character_assets/third_party/amoledos/aos_launcher.c` from commit `3e8a3deead60cf86b5994a770007ff3247b8fdf9` of `charliejgallo/ESP32S3_AmoledOS`. `main/smart_speaker/smart_speaker_amoled_list.cpp` adapts and compiles its vertical list layout, icon/text rows and edge-fading callbacks through the existing product LVGL owner. The product retains its own navigation state and does not compile the upstream HAL, display initialization, audio stack, or app ABI. The full MIT license is retained beside the upstream source.

The home clock uses a modified Teko font subset under the SIL Open Font License 1.1. The original variable font, copyright/license text and source hashes are retained in `components/lunaredu_character_assets/third_party/teko/`, from Google Fonts commit `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`, path `ofl/teko`. `tools/resource-tools/prepare_clock_font.py` instantiates weight 400, subsets numeric characters, centres tabular digits, adjusts vertical metrics and renames the result “Speaker Home Digits”. The 2,472-byte `main/assets/home_digits.ttf` is the offline source for the LVGL bitmap font `main/assets/home_clock_font.c` compiled into the firmware. `tools/ui-preview/export_clock_font.cpp` generates its glyphs with LVGL TinyTTF. FontTools 4.60.1 is only needed to regenerate the subset. The full OFL accompanies the source; original notices remain in the modified font.

The SmallTV weather-clock screenshot is a visual reference supplied by the user. No SmallTV firmware, graphics, font or UI source is included. `main/smart_speaker/smart_speaker_home.cpp` implements that layout using native LVGL drawing and this project's existing weather/wallpaper owners; it is not imported open source SmallTV UI code.

The [dsh-pet archive](components/lunaredu_character_assets/third_party/dsh-pet/README.md) preserves all 114 original WebM animations, all 109 GIF previews, and selected reference files from [PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet) commit `c25386848f70f6e40902e4376f6e7b1e196bc326`. The upstream code is MIT; its README identifies the community character as CC BY-NC-SA 4.0, credits @上善无形 and @ZipZipPipe, and requires derivatives to link the original project in public channels. Original license and attribution are preserved with the archive. The original archives and reference code are not compiled into firmware. The firmware includes one 80×80 derivative of the first idle frame (CC BY-NC-SA 4.0), stored as RGB565 and alpha in `main/assets/home_pet_idle.bin`.

Factory wake resources use `wn9s_nihaoxiaozhi` from the already locked Espressif ESP-SR component (`model/wakenet_model/wn9s_nihaoxiaozhi`). The model is packaged unchanged into the ESP-SR model container by `tools/assets/seed-storage.py`; its original component LICENSE is copied to `managed/model.LICENSE.txt` in the storage seed. No custom wake model is included.

## 环境声录音（2026-11-12）

四个候选录音的作者、原始发布页与 CC0-1.0 授权链接见
[资源清单](resources/ambient/manifest.json)。派生音频经音量处理、重采样和循环接缝交叉淡化；
Freesound 输入为公开高质量预听 MP3。四张默认壁纸为本项目通过内置 image_gen 新生成，
提示词记录于 main/assets/ambient/prompts.json。原有第三方声明保持不变。
