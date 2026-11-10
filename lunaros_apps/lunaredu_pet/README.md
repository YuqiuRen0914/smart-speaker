# Pet

Pet 是拥有自身状态、交互与对话的陪伴应用。小智完整地位于 Pet 内部，按 conversation、
voice、transport、tools 拆分。Pet 不实现运动、轨迹、表演存储或动画资源播放。

公共头文件直接位于 `include/`，使用 `lunaredu_pet_` 文件名前缀，例如
`lunaredu_pet_application.h`、`lunaredu_pet_conversation.h`。不再保留
`include/lunaredu_pet/` 目录或旧路径包装；前缀避免与其他应用的 `application.h`、
`service.h`、`desktop_state.h` 重名。内部头继续位于所属 `src` 模块。

## 内部职责

| 模块 | 职责 |
|---|---|
| app | 声明 Pet 自身能力、生成只读快照 |
| runtime | 生命周期、有界输入队列、协调状态与行为 |
| state | PetState，目前只有 mood |
| interaction | 对话、语音、网络协议、通用工具 |
| behavior | 语义情绪、会话和心情的表现决策 |
| actions | 通用 OS 能力目录查询、描述与调用 |
| views（有屏产品可选） | Pet 页面和语义展示 |

`views` 仅保留陪伴页面、字幕、激活信息和显示组件的嵌入。表情资源列表、开关、缩略图、
单次预览和编辑预览由 Eyes 的 `views/catalog_view` 管理；Pet 不注册这些页面或维护预览计时器。

本地视图也不链接 Eyes。产品在 UI 初始化前通过 `lunaredu_pet_visual_port.h` 注入挂载、卸载、
刷新和音频优先显示回调；不配置时，Pet 显示已同步的宠物素材或基础文字视图。
页面离开负责卸载已挂载的显示组件，重复清理不会重复调用卸载回调。
宠物动画条目的展示数据由 Pet 的 `lunaredu_pet_companion_view_data.h` 定义。

`PetState` 目前只有心情，不预建健康、等级、经验、属性注册器或衰减系统。心情暂存内存，
停用后重新启用保留，设备重启恢复 Neutral。PetSnapshot 不包含角度、轮速、轨迹或表演句柄。

重复启用或停用是无副作用的同状态设置。真正停用会取消排队互动和短暂反应，立即发布
`active=false`、`transient_expression_active=false`、`theme_state=idle`；心情与启动完成状态保留。停用期间拒绝
新互动，重新启用时按最新内部会话阶段恢复语义快照。

Pet 输出完整情绪语义，不读取 `assistant_expression_*` 等显示配置。Eyes 根据完整语义
决定表情启用和回退。happy 与 laughing 不再被 Pet 合并成同一个配置开关。

产品只注入 `PetEnvironmentSnapshot.boot_progress`。对话阶段由 Pet 内部对话服务读取并同步，
产品不再组装会话状态回传 Pet。语义运行时与网络/语音服务仍按各自的任务频率调度，
停用语义运行时不会隐式修改用户的语音服务启用设置。

## 应用边界

Pet 只声明 `pet.state.get` 和 `pet.interact`。对其他 App 使用 `apps.list`、`apps.describe`、
`apps.call`，由 OS 校验调用者、权限、参数、结果和可用性。Pet 不导入 Motion 或 Performance。
没有这些提供方时，Pet 的状态、互动和对话仍可运行。

- Motion：短时转头/移动、速度限制、占用、运动过期和停止。
- Performance：组合表演、时间线、内置动作和 `/performances/user` 文件，通过 OS 使用 Motion。
- Eyes：具体表情、动画、姿势资源及播放能力。
- 产品：分别安装和调度上述应用，将语义状态投影到配置的显示设备。

旧 `pet.activity.*`、`pet.poses.list`、`pet.pose.show` 不保留转发入口；调用方应发现
`performance.activity.*`、`eyes.poses.list`、`eyes.pose.show`。
既有设备模块 desktop_pet 是产品组合协议，保留其外部 ID 和表演文档格式，内部由各 App 实现。

## 验证

`tools/repo/test-pet.ps1` 包含不链接任何运动/表演实现的 Pet 独立运行测试，以及分属 Motion、
Performance 的动作测试。`verify-pet-modules.mjs` 阻止执行器、目标速度和提供方依赖回流 Pet。
宿主测试和固件编译不替代设备上的音频、网络、运动和显示验收。

`tools/repo/test-desktop-views.py` 还会单独编译并运行不包含 Eyes 头文件、源文件或库的
Pet 本地视图，验证备用显示、注入显示组件和退出清理。
该目标也覆盖三种路由的图片晚到、连续换帧、撤销和离开后的回调，以及
expression/companion 切换。产品通过 `DesktopViewState.pet_companion_mode`
选择展示；没有注入渲染器时自动消费备用宠物帧。图片缓冲由提供方持有，
直到下一次提交替代帧或显式撤销，所有视图更新均在 LVGL owner 执行。

## Desktop 视图

本应用的本地视图位于 src/views/，由产品在显示初始化前注册。展示状态仅接收本应用需要的字段，业务操作通过语义意图进入已有服务。具体归属与验证入口见 [应用视图调整](../../docs/lunaros-app-views-2026-09-07.md)。
