# 第三方来源与许可证

本文件记录来源入口，不替代各依赖的许可证正文。固定提交、导入范围、摘要和本地补丁说明见对应 `UPSTREAM.json`；原件、版权和许可证均随所用组件保留。

| 随源码提供的依赖 | 来源及许可证入口 |
| --- | --- |
| LVGL 9.6.0 | [来源](components/lvgl/UPSTREAM.json)、[MIT](components/lvgl/upstream/LICENCE.txt)、[附属库声明](components/lvgl/upstream/COPYRIGHTS.md) |
| MicroPython | [固定提交](components/micropython/UPSTREAM.json)、[MIT](components/micropython/upstream/LICENSE) |
| WAMR 2.4.3 | [来源](components/wamr/UPSTREAM.json)、[Apache-2.0 WITH LLVM-exception](components/wamr/upstream/LICENSE)、[附属声明](components/wamr/upstream/ATTRIBUTIONS.md) |
| Espressif ESP-NOW 2.5.3 | [来源](components/espnow/UPSTREAM.json)、[Apache-2.0](components/espnow/upstream/LICENSE) |
| Espressif network provisioning codec 1.3.1 | [来源](components/network_provisioning_codec/UPSTREAM.json)、[Apache-2.0](components/network_provisioning_codec/upstream/LICENSE) |
| Nayuki QR-Code-generator | [固定提交和 MIT 来源](components/qr/vendor/qrcodegen/UPSTREAM.json)，版权声明在 `upstream/qrcodegen.c`、`upstream/qrcodegen.h` |
| stb_truetype | [固定提交及 MIT / Unlicense 来源](components/text_raster/vendor/stb_truetype/UPSTREAM.json)，许可证正文在 `upstream/stb_truetype.h` |
| ESP Insights 1.3.4（可选诊断） | [来源](lunaros/services/optional/service_diagnostics/vendor/esp_insights/UPSTREAM.json)、Apache-2.0 声明保留于 `upstream/` 原件 |
| Espressif GProf 0.2.1（可选诊断） | [来源](lunaros/services/optional/service_diagnostics/vendor/gprof/UPSTREAM.json)，版权与 Apache-2.0 声明保留于 `upstream/` 原件 |
| 中文字体，Adobe Source 衍生子集 | [SIL Open Font License 1.1](lunaros/assets/fonts/LICENSE.txt)，字体为 `lunaros/assets/fonts/lunar-sans-sc.ttf`，构建时将许可证一起写入字体存储池 |

ESP-IDF 6.1.0 由开发者另行安装。ArduinoJson、LittleFS、ESP-DSP、Espressif GMF/音频组件、MQTT、图像组件与 MPU6050 等公开注册表依赖由 ESP-IDF Component Manager 获取；完整版本和内容摘要见 [dependencies.lock](dependencies.lock)，各下载组件的许可证留在其 `managed_components/` 目录中。注册表公开可获取不改变各组件自己的许可条款。

LunarOS 和智能音箱的第一方源码作为本工程的一部分维护，其对外开源许可证尚待仓库所有者选择；本清单不替第一方代码指定许可证。
