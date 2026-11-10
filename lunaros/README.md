# LunarOS

音频焦点恢复失败时，`AudioFocusBroker::release` 保留租约，调用者应在原 owner
循环重试；`recovery_pending` 期间 resident I/O 不会被重新放行。失败 acquire 的
补偿由 broker 保留，可调用 `recover_resident` 或由下一次 acquire 继续恢复。

BLE 配网的 `start` 接受异步 host 启动，实际就绪状态由 `snapshot`/`running`
返回。产品须从现有 owner 循环调用 `provisioning_ble_poll`，完成广播重试和停止清理；
主动停止 host 与 host 临时 reset 分开通知 profile，不增加工作线程。

`lunaros/` 是面向 ESP32-S3 + FreeRTOS 的共享设备运行层。它提供产品按需组合的通用
生命周期、任务治理、联网、设置、存储、更新、设备语义模型、本地设备 API、标准 Device Web 门户和可选 Desktop；产品只负责组合服务、
声明策略以及提供自己的应用和硬件能力。

本组件不保留伪跨平台的 `platform/esp32`，也不允许用 `adapter`、`local_api` 或自定义协议注册层
绕开统一入口。完整边界见
[`docs/lunaros-architecture.md`](../docs/lunaros-architecture.md)。

## 单组件内部结构

```text
lunaros/
  include/lunaredu_lunaros/       稳定公共契约与命令门面
  contracts/                      Presentation 等跨仓库 schema
  src/
    kernel/                       ServiceHost、executor、任务治理和基础原语
    model/                        传输无关 Device Model/capability 索引
    app/                          App 目录、切换事务、资源租约和系统音量
    persistence/                  identity、settings、storage、system
    network/                      Wi-Fi、配网、时间、HTTP 和 NetworkJobManager
    delivery/                     Resource 下载/安装与 Update/OTA
    device_api/                   本地 HTTP、访问策略、routes 和 projections
    device_link/                  官网配对、presence、命令、发现和 scheduler
    device_web/                   标准设备门户、页面覆盖和资源回退
    desktop/                      交互 core、Local LVGL 与 Remote 文档
  test/                           主机白盒测试与 ESP 平台 stub
```

产品使用 `REQUIRES lunaros`，公共头继续使用 `lunaredu_lunaros/*`。这是唯一一个 LunarOS ESP-IDF component；模块边界通过
公开 `include`、私有 `src`、内部头和仓库审计保持，而不是用一组互相转发的 wrapper component。
ESP32-S3 实现跟随所属能力，不另建 `platform`。Presentation schema 位于 `contracts/`，它的
操作注册与 HTTP 绑定统一位于 `src/device_api/projections`；通用文档组件位于 `src/desktop/remote`。
具体页面由 App 构造，产品负责自己的展示模型、快照和 App 适配装配。

Desktop 的 Local 和 Remote 后端分别由 `CONFIG_LUNAREDU_DESKTOP_{LOCAL,REMOTE}` 选择，默认关闭。
标准门户由独立的 `CONFIG_LUNAREDU_DEVICE_WEB` 选择，归属 OS，与 Desktop 平级。
OS 不依赖应用或 Device；产品初始化显示、注入端口，并注册应用可选视图。
OS 只保存通用页面标识、修订号和视口；具体 PresentationModel 属于产品，Eyes 视图端口属于 Eyes App。
本地布局支持 240×240 与 480×320；无屏产品可只启用 Web 而不编译 LVGL。
启动阶段和业务提示由产品通过 `LunarOSBootStage` 声明，OS 默认只有一个通用系统阶段。
完整 Shell 初始化按产品视口工作，OS 不内置宠物、小智或固定六阶段流程。
应用视图、产品壳和状态投影的归属见 [应用视图调整](../docs/lunaros-app-views-2026-09-07.md)。
十子系统职责、启动链路及 ESP32 取舍见 [Desktop 设计](../docs/lunaros-desktop-design.md)。

资源的数据契约与交付实现也统一归属本组件：

- `include/lunaredu_lunaros/delivery/resource_contract.h` 定义 `ResourceState`、
  `ResourceScope`、`ResourceDescriptor` 和 `ResourceSnapshot`，不引入下载或存储实现。
- `include/lunaredu_lunaros/delivery/resources.h` 提供资源请求、查询、读取、释放和重试接口。
- `src/delivery/resource/` 实现登记、下载、缓存和私有资源生命周期。

Desktop 的 `ResourceProvider` 使用上述公共类型；产品适配层连接函数端口，Desktop 保留
通用资源端口；桌面宠物产品管理自己的像素缓冲和显示策略。原独立 `lunaredu_resource` component 及其头文件入口已移除。

## 三个容易混淆的边界

### Device Model 与 Projection

Device Model 是设备能力目录，不是传输协议。它只描述 capability、module、operation 语义、权限和
owner executor，不依赖 HTTP、JSON 或状态码。Device Projection 将这套模型绑定到当前 JSON/HTTP
控制面；注册阶段校验二者一一对应，完成后一起 seal。模型索引位于 `src/model`，JSON projection
全部位于 `src/device_api/projections`。

单次 Projection contribution 按 capability、metadata、module/model 的固定锁顺序预检，
最后一个登记步骤成功后才发布前面的声明；失败不遗留新能力、元数据或激活状态。
跨域组合通过 `device_model.h` 的公开注册事务契约完成，不导入 Model 私有实现头。

### Device API

Device API 是 Device Model 的本地 HTTP 投影，也是本地控制面的唯一所有者。它负责 HTTP
server、稳定路由、请求大小限制、浏览器来源限制和配网态最小暴露。Network 不启动
HTTP server，产品也不注册私有 HTTP 路由。

稳定入口为：

```text
GET                  /api/v1/device
GET                  /api/v1/capabilities
GET|POST|PUT|DELETE  /api/v1/modules/{module}/{operation}
```

标准门户提供设备信息、名称、网络扫描/配网、绑定码和 OTA。它消费已注册的标准能力，不启动第二个 HTTP server 或维护业务状态。
产品可以单页或整套覆盖；`/recovery` 始终保留内嵌标准页。关闭门户时不嵌入网页。
接入方式、功能选用和验证范围见 [Device Web 门户](../docs/lunaros-device-web.md)。

### Network 与 Resource

Network 是连接和有限时传输的唯一调度者，拥有 Wi-Fi、SoftAP/DNS、SNTP、互联网状态、HTTP
transport 和固定容量 `NetworkJobManager`。一次请求或下载不会创建一个新 task。

Resource 是独立服务，拥有资源描述、缓存状态、下载作业桥接、临时文件、大小/哈希校验和最终
替换。它依赖 Storage，按需依赖 Network；DeviceLink 不再通过副作用启动资源系统。

典型下载链路：

```text
App / Update / DeviceLink / MicroPython
  -> 所属 Service 的策略与权限
  -> NetworkJobManager 准入和固定槽位
  -> lunar.netio worker
  -> HTTP transport
  -> 所属 Service 完成校验、安装和状态更新
```

小智的长期 MQTT/WebSocket/UDP 和实时音频仍由小智组件持有，但只能依赖 Network 的连接事实，
不能建立第二套 Wi-Fi、配网或互联网状态机。

受管字体和模型按资源各自串行化发布与读取，无关资源的下载不会占用同一把事务锁。
内存加载与流式文件安装共用格式校验；缓冲读取适配只负责范围检查，不维护第二套解析规则。
Network 作业句柄在服务重置后不复用，旧句柄不能命中新提交的作业。

### 三类设备标识

“设备身份”不是一个可复用的聚合服务，当前三类标识按所有者拆开：

- `hardware_serial_number()` 是 LunarOS 从出厂 MAC 派生的物理设备/云绑定序列号；
- `DeviceConfig.identity.local_device_uuid` 是 Settings 持有的本地 Device API UUID；
- `lunaredu::pet::conversation::assistant_client_id()` 是小智协议自己的持久化客户端 ID。

三者可以由同一块芯片的事实派生或迁移，但生命周期、存储键和重置语义不能相互代替。旧版
`board/uuid` 只由小智组件在首次读取时迁移到 `xiaozhi/client_id`，避免已激活设备改变协议身份。

## App 能力协作

DeviceLink 只持有绑定、账号身份与连接机制。产品通过显式 `device_link_json.h`
观察端口提供运行状态并消费云响应；头像与宠物资料缓存由
[`components/lunaredu_account_resources`](../components/lunaredu_account_resources/README.md) 持有。
应用 Manifest 不含助手专用字段；助手设备控制策略由产品解释当前前台。

原生调用目录位于 app_capability.h，支持提供方声明、消费依赖和产品授权交集。
产品在启动执行器前登记能力并调用 seal_capabilities；服务就绪与能力声明分开。
Calendar 声明 Clock 依赖，Motion 支持无硬件初始化的预先登记。
desktop-smart-pet-1 已使用产品能力装配入口；其他产品迁移和后续资源治理见
[目标架构与实施记录](../docs/lunaros-target-architecture-2026-09-07.md)。

原生 API 不依赖 JSON 或 Desktop；显式采用 app_capability_json.h 的能力才可进入
JSON/MCP 发现与调用。内部调用许可不自动成为 HTTP 或平台操作许可。
通过 tools/repo/test-app-capabilities.ps1 验证原生独立链接与真实 App 适配器。

## 显式组合与体积

`setup_core_runtime(executors, error)` 只建立产品列出的执行器队列，不自动选择 Network 或
netio。多执行器产品使用 `ExecutorTaskGroup` 声明栈、优先级、核心和单次业务回调；
组内先全部创建，再全部绑定，最后放行业务。失败时协作退出并释放执行器与任务登记。
单前台循环产品继续使用 maintenance，主循环拥有 main/network。App 能力 reset/seal 归产品，Kernel 不依赖 App。
标准 Device API 通过 `setup_standard_device_projections<standard_projection::services, ...>`
静态选择，旧的默认全选和运行时 include_update 入口已移除。所有仓库产品已迁移。
Wi-Fi 初始功耗由 `NetworkPolicy.initial_power_save` 声明，通用默认 BALANCED。

OTA 共用 Network HTTP 下载实现；TLS 内存阈值统一由 Network 声明。
性能构建的 Device API/Settings 冷路径使用 `-Os`；归档分域与最终固件余量通过
`tools/repo/lunaros-size-report.py` 对实际产物测量。详见
[核心优化记录](../docs/lunaros-core-optimization-2026-09-08.md)。

## 生命周期

产品 composition root 先配置策略，再用一个 `ProductDefinition` 安装完整服务图和静态声明节点；
Device Model、Projection 与 capability 声明表随后在任何 task/service 启动前 seal。固定 executor
task 绑定完成后，`ServiceHost` 才按依赖顺序启动、按反向顺序停止。每个
start/stop/activate 回调都同步派发到 `ServiceDefinition`
声明的 owner executor，调用者线程不再冒充服务 owner。Network 从一开始就在固定
`lunar.network` executor 上启动；Device API 和 Resource 同样作为共享服务注册，不由某个产品
task 私自启动。

服务的长任务必须进入自己的 Job Manager；`ServiceExecutor` 只处理短小、同步、内存内操作。
其 timeout 只限制队列准入，操作一旦入队，调用者必须等待 owner 完成，避免栈上 context 失效。

`service_available()`、整体 required readiness 和依赖快照统一检查传递的必需依赖。
服务快照的 `state` 保留本节点生命周期，`available` 表示包含依赖的实际可用性；依赖恢复后重新可用，
不把依赖方永久改为 Failed。服务诊断直接读取受锁保护的快照，不经过 main executor。
Desktop 的周期入口统一为 `lvgl_screen_runtime_handler()`，LVGL 的时间由 tick callback 提供，
没有独立空 tick 或 tick-and-handler 兼容入口。

## 任务纪律

第一方 task 只能通过 `create_managed_task()` 创建，并声明稳定 id、owner、lifetime、栈、优先级、
亲和性和内存类型。仓库审计禁止其他位置直接调用 `xTaskCreate*`、`std::thread`、
`pthread_create` 或 task 自删除。

<!-- lunaros-task-budget:start -->
任务预算参考检查集由 `tools/repo/test-lunaros-task-budget.ps1` 的 sourceRoots 定义，
覆盖共享任务声明及桌面宠物入口，不代表所有产品的任务总和或任一时刻的活动任务数。

| Lifetime | 声明数 |
| --- | ---: |
| Resident | 8 |
| Feature | 6 |
| Transient | 2 |
| Total | 16 |

参考产品预算：活动任务 20、resident 12、transient 4。
运行 `pwsh tools/repo/test-lunaros-task-budget.ps1 -UpdateDocs` 同步本表；普通检查会拒绝漂移。
<!-- lunaros-task-budget:end -->

HTTP worker 由 ESP-IDF 管理，不为每个请求创建第一方 task。

## 公共 API

公共 include 使用 `lunaredu_lunaros/<领域>/<头文件>`，物理上位于
`include/lunaredu_lunaros`。根目录不放聚合头或旧路径转发头，调用方直接包含所用接口：

| 目录 | 接口职责 |
| --- | --- |
| `kernel/` | 启动组合、服务、执行器、任务与基础工具 |
| `app/` | 应用生命周期、能力、资源租约与维护协调 |
| `model/` | 设备语义模型与呈现契约 |
| `persistence/` | 设置、身份、存储、板级服务与系统信息 |
| `network/` | 网络状态和命令、作业、HTTP、ESP-NOW 与配网 |
| `delivery/` | 下载资源的数据契约、缓存、托管资产与 OTA |
| `device_api/` | 本地 API 与 JSON/HTTP 投影，协议类型留在这一侧 |
| `device_link/` | 官网连接与对应 JSON 适配 |
| `desktop/core/` | 后端共享的视图数据、页面请求与资源端口 |
| `desktop/local/` | LVGL 运行时、视图注册、绘制与产品显示配置 |
| `desktop/remote/` | 远程文档组件 |
| `esp_idf/` | 明确依赖 ESP-IDF 的组件集成适配 |

`app/resource_registry.h` 管理应用资源租约；`delivery/resources.h` 管理下载资源，
两者不是同一个注册表。`system.h` 聚合入口已经移除；启动组合使用
`persistence/system_runtime.h`，设置、身份和 OTA 分别包含对应领域的头文件。

组件内部契约放在所属 `src` 模块。跨模块仅允许明确列出的传输策略、Web 页面字节和
板级事实接口；消费者由 `verify-lunaros-boundaries.mjs` 精确限制。Bluetooth 的宿主
生命周期、Desktop 页面消费和 Shell 内部操作不对产品公开。测试可以包含私有头，
产品和 App 只能使用公共接口。

Desktop 的 C/C++ 基础枚举以 `desktop/core/view_types.h` 的数值为共同来源；
C++ 层保留强类型和拥有字符串的数据结构。Local 样式宏统一使用 `LUNAROS_VIEW_`
前缀，字体直接调用 `lunaros_font_runtime_*`，不维护另一套同义访问函数。

LunarOS C++ 公共符号必须位于 `lunaredu::lunaros`、`lunaredu::app` 或
`lunaredu::system`、`lunaredu::desktop`；公共头不提供全局兼容 alias。组件贡献者在自己的实现命名空间内显式导入或
限定所需符号。

业务门面不暴露 ESP-IDF、FreeRTOS 锁或具体 runtime object。`managed_task.h` 只保留不透明 task
handle，FreeRTOS 头文件和创建细节都在 Kernel 内部。

## 验证

本轮执行器、存储、资源生命周期和内存优化的改动、测试及实机验收边界见
[架构与资源优化记录](../docs/archive/lunaros-validation-2026-09-06.md#lunaros-resource-optimization)。

```powershell
pwsh tools/repo/test-lunaros.ps1
pwsh tools/repo/test-lunaros-task-budget.ps1
pwsh tools/repo/audit.ps1
pwsh tools/repo/test-lunaros-config-matrix.ps1
pwsh tools/repo/build.ps1 -App products/desktop-smart-pet-1
```

配置矩阵为每个 profile 生成独立 `sdkconfig`，并核验开关的实际结果，避免产品目录中已有配置
造成假通过。主机测试、仓库审计、ESP-IDF 编译和真机验收仍是四类独立证据；编译成功不等于
联网、配网、下载、OTA、官网连接、小智和 App 切换已经过硬件验收。

## ESP-NOW

ESP-NOW 位于 `src/network/espnow/`，公共入口为 `lunaredu_lunaros/network/espnow.h`。
产品启用 `CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW`，并在启动前配置
`NetworkPolicy.espnow`。Network 服务启动/停止 ESP-NOW；产品不再初始化或销毁 Wi-Fi。

当前遥控器和验证项目使用 maintenance 服务组合，保留 Wi-Fi 配网、Web 维护和 OTA。
main/network executor 绑定到既有主循环，阻塞网络任务使用单独的 maintenance I/O worker。
真正只需要 ESP-NOW 的产品可显式配置 `station_enabled=false` 并关闭配网、时钟同步，但不应调用会开启维护网络的 `prepare_maintenance_runtime`。接收回调只由产品调用 `tick_espnow()` 时执行，每次最多处理 8 帧；
peer、发送和改信道命令统一派发到 Network owner。

共存时 Wi-Fi 连接/扫描/SoftAP 使用当前无线信道。ESP-NOW 不抢占已占用信道；冲突返回
`wifi_radio_busy` 或 `wifi_radio_channel_in_use`。SoftAP 保留 ESP-NOW 使用的 STA 接口，
无连接时沿用产品无线信道。状态中的 `channel` 是实际无线信道；固定信道 peer 不匹配时
发送返回 `espnow_peer_channel_mismatch`，不会偷偷改变路由器连接。跨信道自动发现/重配对
仍属于产品协议，当前没有新增这一功能。传输重启会清空 peer 和接收注册，产品需要重新注册。

## 运行治理与可选 Desktop

完整职责、调用与后台模型见 [目标架构与实现](../docs/lunaros-target-architecture-2026-09-07.md)。
音频独占由 OS 管理并等待在途 I/O 结束；BLE 主机生命周期由 Network 内的 bluetooth runtime 管理。
后台业务的状态属于服务，页面离开不代表停止执行。Desktop 按实际视口布局，覆盖 240×240 与 480×320。

独立开销与编译基线见 [lunaros-runtime](../apps/validation/lunaros-runtime/README.md)。
固件大小包含 ESP-IDF；运行堆、整机任务数和真实峰值需上板采样，不能由构建成功推导。

## 产品公共机制

各固件的组合方式和职责分工见 [产品接入说明](../docs/lunaros-product-integration.md)。

- `kernel/executor_tasks.h` 管理固定执行器任务的整体启动与就绪屏障；产品不再复制线程创建、等待、绑定和失败取消循环。
- `network/network_job_pump.h` 在 network I/O owner 上每轮处理一个工作项，给已排队服务操作保留执行机会。
- `delivery/boot_health.h` 统一持续健康时间窗口。必须同时满足必需服务、可达的 STA/配网入口、Device API 和产品健康条件；离线配网是有效维护路径。音箱保留 15 秒窗口，其他当前产品默认 3 秒。超时和回滚仍由 Update 服务负责。
- OTA 已确认时使用不分配内存的状态谓词，避免业务循环重复构造网络和硬件快照。
- 启动失败直接调用 `ota_service_rollback_running_image`；它读取引导器的实际镜像状态，可在 Update 服务初始化前使用，已确认镜像不会被回滚。
- `components/lunaredu_nvs` 是共享 NVS 初始化入口，失败不会自动擦除默认分区。Settings、配对和校准只管理各自的键与数据格式。
- `components/lunaredu_account_resources` 拥有账号展示资源与离线缓存；LunarOS 不接管这些产品语义。
- `apps/shared/smart_pet/runtime_lifecycle` 组合 Pet/Performance 的激活、投影发布和失败撤销。硬件停止由产品提供，业务模块之间不新增反向依赖。

故障回归：`tools/repo/test-executor-tasks.ps1`、`tools/repo/test-maintenance.ps1`、`tools/repo/test-shared-product-runtime.ps1`。这些是主机测试，不代表已完成实机 OTA 或执行器故障注入验收。
