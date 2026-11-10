# lunaredu_device_mpu6050

mpu6050 外设组件，拥有该外设的公共接口、实现、配置与禁用状态实现。

产品在 CMake 中显式依赖此组件，并通过本组件的 Kconfig 启用硬件实现。
公共头平铺于 `include/`，文件名统一使用 `lunaredu_device_mpu6050` 前缀；BSP 继续负责板级资源和共享总线。

## 一行启动测试

```cpp
#include "lunaredu_device_mpu6050_test.h"

ESP_ERROR_CHECK(lunaredu::device::mpu6050::start_test(4, 5));
// 测试在后台自动运行；需要结束测试时再调用：
// ESP_ERROR_CHECK(lunaredu::device::mpu6050::stop_test());
```

独立实例读取六轴数据。

需要启用 `CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050`；未启用时启动返回 `ESP_ERR_NOT_SUPPORTED`。
重复启动返回 `ESP_ERR_INVALID_STATE`；配置对象只在启动期间借用，BSP/总线句柄需保留至停止完成。
日志中的启动成功只表示测试已运行，实物效果需要现场观察。公共行为与验收边界见
[测试说明](../../TESTING.md)。
