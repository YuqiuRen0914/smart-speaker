# LunarOS Desktop

Desktop 是 `lunaros` 的可选交互子系统，公共接口为 `lunaredu_lunaros/desktop/*`。

| 目录 | 职责 |
| --- | --- |
| `core/` | 页面标识、修订号、视口、有界输入、导航和资源端口，不依赖 LVGL |
| `local/` | LVGL 生命周期、显示端口、字体、公共绘制、应用视图调度与通用启动进度 |
| `web/` | 配网回退页与有限大小的产品页面内容；HTTP 路由及访问控制归 Device API |
| `remote/` | JSON 文档组件；具体页面与业务快照由应用及产品构造 |

`CONFIG_LUNAREDU_DESKTOP_{LOCAL,WEB,REMOTE}` 和独立的
`CONFIG_LUNAREDU_DESKTOP_LVGL_RUNTIME` 均默认关闭。音箱和月仔可以只启用显示运行时。
产品初始化显示驱动，注入 `LvglScreenSurface`，并在界面创建前注册 Shell 与应用视图。
OS 不依赖应用或外设组件。SPI DMA 缓冲归驱动；像素回调返回前必须停止读取源缓冲。

## 产品与应用的所有权

桌面宠物的 `apps/products/desktop-smart-pet-1/main/modules/desktop_shell/` 持有
`semantic_state.*`、`presentation_builder.*`、`remote/presentation_snapshot.*`、
`local/shell.c` 和 `local/page_routes.c`。这些文件没有 OS 转发入口。
产品负责组合应用状态、绑定页路由、首页选择和助手设备控制策略。

各应用持有自己的本地视图与 `RemoteViewState`。OS 只通过 `ApplicationViewPorts`
在既有 LVGL owner 上调用 `update`、`mount`、`leave` 和可选的 `blocks_idle`。
Shell 持有父对象，应用持有内部对象和临时状态；离开前由应用释放定时器与引用。
Eyes renderer 属于 Eyes，Pet 通过产品注入的可选显示端口使用它。

## 启动与尺寸

`LUNAREDU_DESKTOP_SHARED_FONTS` 提供共享字体服务，依赖 UTF-8 工具和 LVGL，
不要求完整本地 Shell。音箱等自有页面的产品可以独立选用；本地 Shell 自动选用。
字体分区挂载和字体创建仍由使用者显式调用，未选用该能力的显示运行时不引入字体实现。

`setup_lvgl_shell_view()` 使用产品传入的有效视口，不再要求固定 480×320。
当前验收尺寸为 240×240 与 480×320；其他尺寸仍需产品布局验收。

`boot_view.h` 提供最多八个阶段的 `LunarOSBootStage`。产品在启动页创建前声明
阶段标签、进行中提示和降级提示，并保持字符串有效。默认只有一个通用系统阶段。
启动页运行期间不能更换阶段表；所有阶段完成或降级并收到显式结束请求后才退出。
产品决定何时报告最后阶段就绪，OS 不假设产品具有网络、宠物或语音助手。

桌面宠物自己的六阶段定义位于 `local/boot_plan.h`。模拟器也读取该产品定义。
启动页完成后调用产品回调，由产品根据 OTA、引导和联网状态选择目标页面。

## 内置资源

Logo 使用 LVGL 标准 RLE，LOCAL 自动选择 `LV_USE_RLE` 和 `LV_BIN_DECODER_RAM_LOAD`。
原始 ARGB8888 像素及透明边缘不变；启动图 20,736 字节编码为 8,387 字节，状态图
2,704 字节编码为 1,955 字节。解码由 LVGL 管理，启动图需要约 20.25 KiB 临时像素缓冲，
缓存与关闭遵循 LVGL 生命周期，不能将 Flash 节省称为 RAM 节省。
生成器默认从批准 SVG 重建，`--repack` 可无损重编码当前像素；独立验证器核对源 SVG 和
原始像素摘要。真实 LVGL 测试逐像素核对并验证重复开关无缓存解码不遗留分配。

Web 配网页在构建目录删除行首缩进后嵌入，源 HTML 保持可读；保留脚本换行与占位符。
`provisioning_web_test.mjs` 可传入实际生成 HTML，验证网络提交、状态和错误行为。

## 验证

显示运行时将一次 LVGL 刷新中的所有传输块汇总为一帧；只有全部传输成功才增加
`refresh_count`。`flush_count` 单独统计成功传输块，刷新耗时从首块提交到末块完成。
触摸输入创建失败会撤销本次初始化并保留错误，调用者可以重试。

刷新任务保留缓冲区超过 3 秒时，运行时记录错误并重启设备，避免将仍被 DMA
使用的缓冲区提前交回 LVGL。状态查询使用独立锁；刷新等待和下一次运行时 tick
都检查超时，覆盖中间块和最后一块卡住的情况。

- `verify-desktop-subsystem.mjs` 检查后端组合和视图归属。
- `verify-lunaros-boundaries.mjs` 检查业务内容及跨模块私有依赖。
- `test-desktop-subsystem.py` 检查导航、文档、资源端口和状态复用。
- `test-desktop-views.py` 使用真实 LVGL，经过完整 Shell 初始化，覆盖两种视口、
  产品页面生命周期和非宠物产品的通用启动阶段。
  同时运行异步显示运行时的初始化回滚、分块计数、传输失败、超时恢复和独立诊断测试。

这些是源码与主机证据；显示 DMA、触控时序及实机内存仍需独立验收。
