# lunaredu_device_gpio_button

gpio_button 外设组件，拥有该外设的公共接口、实现、配置与禁用状态实现。

产品在 CMake 中显式依赖此组件，并通过本组件的 Kconfig 启用硬件实现。
公共头平铺于 `include/`，文件名统一使用 `lunaredu_device_gpio_button` 前缀；BSP 继续负责板级资源和共享总线。

`PolledGpioButton` 提供独立实例的 GPIO 初始化、当前按下状态和释放，用于需要保持自行轮询/去抖时序的应用；不包含按键触发的产品动作。原事件回调接口继续保留。

## 一行启动测试

```cpp
#include "lunaredu_device_gpio_button_test.h"

ESP_ERROR_CHECK(lunaredu::device::gpio_button::start_test(4));
// 测试在后台自动运行；需要结束测试时再调用：
// ESP_ERROR_CHECK(lunaredu::device::gpio_button::stop_test());
```

轮询按键并消抖，打印按下、松开事件。

该入口不需要额外硬件功能开关。
重复启动返回 `ESP_ERR_INVALID_STATE`；配置对象只在启动期间借用，BSP/总线句柄需保留至停止完成。
日志中的启动成功只表示测试已运行，实物效果需要现场观察。公共行为与验收边界见
[测试说明](../../TESTING.md)。
