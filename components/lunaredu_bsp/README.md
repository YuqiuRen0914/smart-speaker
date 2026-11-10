# lunaredu_bsp

`lunaredu_bsp` 是 LunarEdu 硬件仓库的板级支持组件。它负责把当前统一 N16R8 核心板
不同装配 profile 下的
GPIO、I2C、camera、WS2812、SD card 等板载资源整理成稳定接口，供上层设备模块
按需占用。

## 公开入口

- `include/bsp/lunaredu_bsp.h`：板级资源、资源占用、I2C、LED、SD card 等综合入口。
- `include/bsp/lunaredu_camera.h`：camera 板级接线与 XCLK 时钟描述。
- `include/bsp/lunaredu_pins.h`：当前板型的引脚定义。

`bsp/` 这一层 include 前缀是刻意保留的，因为 `board.h`、`pins.h` 这类名字太通用，
不适合直接暴露到全局 include 命名空间。
`lunaredu_camera.h` 在所有板载资源 profile 下都可 include，且不依赖摄像头厂商头文件。
`bsp_camera_get_board_config()` 只返回板级描述，不占用资源、不初始化驱动；
不带 camera 的 profile 会清空描述并返回 `ESP_ERR_NOT_SUPPORTED`。
Camera Device 将这个描述转换为驱动配置，并通过 `BSP_RESOURCE_CAMERA` 仲裁共享引脚。
`esp32-camera` 依赖、媒体／推理采集配置、驱动生命周期和帧缓冲均由 Camera Device 拥有。

## 内部结构

- `src/bsp_board.cpp`：板级身份与基础初始化。
- `src/bsp_resources.cpp`：板载资源互斥与占用状态。
- `src/bsp_shared_pins.cpp`：共享引脚冲突检测。
- `src/bsp_camera.cpp`：camera 板级描述与无摄像头板型的明确返回值。
- `src/bsp_i2c.cpp`：I2C bus 初始化与访问。
- `src/bsp_i2s.cpp`：I2S 相关共享 pin / 资源分组的占用与释放。
- `src/bsp_led.cpp`：板载普通 LED 控制。
- `src/bsp_sdcard.cpp`：TF card / SDMMC 板级挂载支持。
- `src/bsp_spi.cpp`：SPI bus 初始化/释放与对应板级资源占用。

板载状态 LED 和 TF card 都提供成对的 init/mount 与 deinit/unmount 入口。
上层模块应通过这些入口释放 BSP 资源声明，而不是直接重置 GPIO 或 SDMMC 状态。
资源占用失败诊断应优先使用 `bsp_resource_last_claim_snapshot()`；旧的
`bsp_resource_last_claim()` 仅为兼容保留，会返回 BSP 内部状态指针。
共享引脚模式优先使用硬件语义明确的枚举名，例如
`BSP_SHARED_PIN_MODE_BASEBOARD_MPU6050_I2C`；旧 `BSP_SHARED_PIN_MODE_I2C2`
只是兼容别名。
BSP I2C 是共享单例总线，所有 init/deinit/read/write 入口在组件内串行化；
单个设备模块不应在其他模块仍可能访问 I2C 时调用 `bsp_i2c_deinit()`。
已初始化的 I2C 不允许隐式切换到另一总线配置；须显式释放后重建。SPI、I2C、SD
的底层释放失败会保留相关句柄和资源声明，允许重试；没有板载 SD 的 profile 仍提供明确禁用接口。
资源 owner 必须非空且少于 `BSP_RESOURCE_CLAIM_OWNER_MAX` 字节，超长名称直接拒绝。
I2S claim 必须零初始化，每次申请取得独立 lease；相同引脚不能由两个 lease 同时占用，
live claim 不允许重复申请。需要转移时只能保留一个负责释放的副本，释放失败须保留 claim 重试。
当某个上层模块声明自己要占用 BSP SPI / I2S 资源时，bus 生命周期或 pin group
占用也应通过 BSP 入口完成，而不是在设备模块里直接 claim/release 底板资源。

## 边界

BSP 保留为 `components/lunaredu_bsp` 独立组件，位于 LunarOS 与 Device 下方。
它不反向依赖 LunarOS、Device 或 app；只使用 BSP 的项目无需引入摄像头厂商驱动。
LunarOS 负责系统服务和生命周期编排，Device 负责具体外设驱动和操作，BSP 提供板级事实与仲裁。

这个组件只描述“板子有什么资源、这些资源怎么安全占用”。它不应该包含 camera
业务状态、拍照保存、传感器采样、网络协议、Local API operation 或 app 启动策略。

如果后续增加新板型，优先在 BSP 中扩展板级绑定和资源仲裁，而不是让上层模块直接
散写 GPIO 和外设初始化细节。
