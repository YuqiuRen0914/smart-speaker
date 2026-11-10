# 第三方来源与许可证

当前以 1.0.35 旧版为基线。公开注册表组件由 ESP-IDF Component Manager 获取，版本和内容摘要见 [dependencies.lock](dependencies.lock)，版本约束见 [cmake/dependency-constraints.txt](cmake/dependency-constraints.txt)。各下载组件的许可证在其 `managed_components/` 目录中保留。

使用的公开依赖包括 LVGL 9.5.0（MIT）、ArduinoJson 7.4.3（MIT）、esp-audio-player 1.1.0 及其 Helix MP3 依赖，以及 ESP-DSP、ESP-SR、ESP Audio Codec、MQTT、摄像头/图像组件、LittleFS 和 MPU6050 等。各组件的具体许可、版权及传递依赖条款以其对应版本原件为准；注册表公开可获取不改变许可条款。

随源码保留的中文字体为 Adobe Source 衍生子集，原件在 `lunaros/assets/fonts/lunar-sans-sc.ttf`，[SIL Open Font License 1.1](lunaros/assets/fonts/LICENSE.txt) 随工程和工厂字体存储一起交付。原版来源清单保留在 `lunaros/assets/fonts/manifest.json`。

ESP-IDF 6.0.1 及其工具链由开发者另行安装，按各自许可证使用。LunarOS、Pet、驱动和音箱第一方源码只抽取该产品依赖闭包，来源摘要见 [抽离清单](docs/extraction-manifest.json)；第一方对外开源许可证仍待仓库所有者选择，本声明不替代许可授权。
