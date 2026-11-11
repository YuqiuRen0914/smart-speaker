# 小星平台接入

状态：LunarLink 源码保留；因线上绑定接口 404，用户要求临时切回小智官方平台。日期：2026-11-12。

## 临时使用小智官方平台

`lunaredu_pet/Kconfig.projbuild` 提供构建时平台选择，默认小智。小智分支复用本仓库基线的激活挑战、MQTT hello、AES-CTR UDP 与 60 ms Opus；LunarLink 分支保留 bootstrap、wire、AES-GCM 与 20 ms Opus。两者共享当前会话、按键取消、单轮输入、音频焦点与播放所有者。

小智使用 MAC Device-Id 和原有小智 NVS 记录，不使用 Board 凭据、不依赖 Board 绑定。官方端点固定为 `https://api.tenclass.net/xiaozhi/ota/`，网页按实际平台显示激活码。清除本机配对不会删除云端绑定，仍保留原有的新配对挑战保护。LunarLink 端点与暂停偏好继续使用独立键。

新增真实小智激活客户端测试，覆盖无 Board 账号、无 Board 鉴权头、挑战返回 202、获取 MQTT 配置及 HTTP 401。主机回归验证不替代账号绑定和实际声音验收。

2026-11-12 实机验证：构建 `36ed06af-153a-4fcf-a6b5-a91c8dcabde2` 成功，应用 SHA-256 为 `a21bfd80efc22e7ad4a0567831016f7a5da9bbd8fe1e476e0c64c7a780d2cb1b`。USB 仅写入既有 `ota_1` 并通过哈希校验，保留配置与资源。完整主机套件、额外小智激活测试、12 项工具测试和独立路径审计通过。

设备沿用原有小智激活记录，实际 MQTT 连接及云端文字回复成功，无需重新输入绑定码。最初测试收到 TTS 但未收到 UDP 音频；原有通道恢复流程重建并发送端点探测后，观察到 51 个实际播放帧、对应 UDP 接收和播放完成。用户随后明确确认“已有声音”。本次通过范围为已有账号的小智连接及测试回答声音；不扩展为麦克风实录、长期运行或唤醒验收。日志另有唤醒前端连续内存不足和音频退出时重复 disable 提示，保留为后续观察项。

用户指定以本机 `xiaoxing` 产品使用的平台替换小智后端。本次只参考该产品的公开协议事实，在独立仓库内适配已有 DeviceLink、Pet conversation、MQTT、UDP、Opus 和产品工具；没有引入该产品、新版 OS 或外部源码路径作为构建依赖。版本继续为 1.0.35，接线及单轮交互策略保留。

## 平台与身份

- 设备账号管理：`https://board.lunaredu.cn`。现有 DeviceLink 所有者的配对、轮询、presence、发现与固件票据改为 `/api/link/device-enrollment/...`、`/api/link/device-runtime/...`。
- 默认助手配置：`https://gateway.lunaredu.cn/wire/v1/device/bootstrap`。使用 `lunaredu.wire` / `device` / v1 / `mqtt`；请求携带现有设备序列号、Client-Id 和设备云端凭据，不另设小智配对码。
- 账号、序列号、环境、凭据和 binding epoch 变化会使旧 bootstrap 结果及传输回调失效。解绑采用 `binding/revocation`，核对 `expectedBindingEpoch`；不猜测服务端解绑成功。
- 保留现有账号和 Wi-Fi 记录；不读取或复用旧小智 MQTT 凭据。每次启动向当前平台重新获取会话凭据，MQTT 密码只在内存中使用。端点使用新的 NVS 键 `ll_url`，本地暂停偏好使用 `ll_blocked`，避免旧端点及解绑状态误迁移。
- 开发环境不会把开发凭据发给默认生产 gateway；必须显式配置对应 HTTPS bootstrap 端点。
- 网页的“暂停平台连接”只清理助手会话并暂停 bootstrap；账号解绑仍由原账号菜单负责。“重新连接助手”恢复 bootstrap。设备需先完成 Board 账号绑定。

## 会话、音频与工具

原会话生命周期继续拥有采集、打断、取消和音频焦点。旧代码里的 `xiaozhi` 文件名、服务 ID 及本地 API 别名暂保留，以免破坏网页和既有产品调用；它们不表示仍会连接小智。

MQTT 使用带 request、session、binding epoch 和 activity 身份的 LunarLink 消息；每轮聆听建立新 activity。只接受对应会话及当前轮次的回复。会话协商只提供已实现的 Opus：上行 16 kHz、下行 24 kHz、单声道、20 ms。PCM 和其他协商格式会被明确拒绝。

UDP 使用 v2 16 字节头、AES-128-GCM、16 字节认证标签及最大 1468 字节载荷。连接、方向、长度、序号和认证均校验；重复及旧包被丢弃。相同 key/nonce 的重复配置保留已消费的序号，避免重复 GCM IV；发送序号耗尽拒绝继续加密。

`playback_ready` 交由原有待发队列在 RX 回调外发送。回答完成后等待压缩队列、当前写入及输出尾部宽限；只有实际写入成功、序号范围完整且没有输出错误才回报 `delivered=true`。丢包、解码失败和写入失败不能冒充声音成功。这仍不能代替人耳真机验收。

平台的 `capability.list` / `activity.submit` 调用已有产品工具，不另建业务状态。写操作使用有界的会话级幂等回执，重复请求回放同一结果，参数冲突和容量不足拒绝执行；产品回调在会话锁外运行。结果保留原业务返回内容；工具自身返回的 `accepted` 仅表示接受请求，不能解释为物理动作完成。失败的写入结果标为 completion unknown，不盲目重放。

未宣告 narration、playback flow/integrity、通知推送、远程 teleop 或自定义唤醒词。小星新版 `operation.invoke` 持久远控是另一份契约；本产品关闭旧 `commandType` 轮询，防止把新平台命令套进旧执行器。原本地 API 和本次接入的语音工具可用；新版平台远控尚未迁移。

本地唤醒模型仍是“你好小智”，与云平台名字无关；本次不会自动补装模型或解决之前记录的唤醒前端内存不足。

## 验证方法

主机套件新增：

- 真实显示缓存条件：一级菜单返回首页时共享设置面板被隐藏；原按钮、导航和二级返回测试继续运行。
- 实际 bootstrap 编码和解析：身份请求头、payload、绑定代次、不完整 MQTT、401。
- 实际异步 bootstrap 作业：正常结果接受；重置、换绑、账号或凭据变化、暂停后的迟到结果丢弃。
- 实际配对及解绑函数：新路径、配对链接检查、账号/epoch 校验、过期解绑回执拒绝。
- 实际 wire 与 UDP 类：协商格式、会话/轮次隔离、重复 session reply、播放完整度、GCM 篡改/重放、重复配置保持序号。
- 实际工具分发：锁外执行、重复写入、参数冲突、失败回执、队列满与换绑失效。

加密主机测试的 PSA 适配层使用 OpenSSL 验证真实 GCM 数据；需要 `pkg-config` 和 OpenSSL 开发库。固件仅使用 ESP-IDF 自带 PSA，不新增 OpenSSL 固件依赖。Windows 主机入口已注册测试，但未验证其 OpenSSL 环境。

执行 `node test/run_host_tests.mjs`、工具 unittest、受管构建与独立路径审计。最终构建 UUID、摘要和结果见 [开发状态](implementation-status.md)。本次未烧录、未使用账号凭据向平台发起实际请求；A-AI 的绑定、唤醒、真实收音、云端回答声音及联合负载仍待真机记录。

## 2026-11-12 在线接入检查

当前工作区固件已通过 USB 写入并校验，构建任务为 `dc53197f-4952-4648-a1b8-5784ee5f5285`。设备联网后，读取 `device_link/status.get` 并重新触发 `pairing.start`，最终仍返回 `pairing_request_rejected`，尚未建立账号绑定。

主机对源码配置的 `https://board.lunaredu.cn/api/link/device-enrollment/pairing-sessions` 使用不含凭据的空 JSON 请求，两次均得到 HTTP 404 和 HTML 页面。因此当前线上地址未提供可用的该绑定路由；尚不能确定是部署缺失还是 API 地址已变化。没有改写设备凭据、清除账号数据或尝试替代账号绑定。

`https://gateway.lunaredu.cn/wire/v1/device/bootstrap` 可达：空请求返回 HTTP 400 / `communication_contract_incompatible`；携带当前 wire 协议头和 profile 的请求进入身份校验，返回 HTTP 400 / `Missing MAC or device_id`。这些探测不含设备身份或令牌，不能证明已鉴权或语音会话可用。

本次重新运行 `test/lunarlink_{bootstrap,binding,protocol,generation,capabilities}_test.py`，五组均通过。后续需要确认已部署的 Board API 地址，或在服务端补齐现有绑定契约；完成绑定后再验证 bootstrap、MQTT 和实际收音回答。唤醒词模型缺失是独立的设备资源问题，不能用修复绑定替代。

## 协议参考来源

仅按用户指定只读查看原硬件仓库中的 `apps/products/xiaoxing/main/product/product_config.h` 与产品服务组合，以及其引用的 `service_assistant` 的 bootstrap、wire、media UDP 契约和 `service_platform_link` 的绑定 HTTP 契约。采用的是实际产品默认 gateway 域名，而非旧 README 的 device 域名。消息 profile、session 对象、activity、音频头、AAD、GCM IV、绑定代次与端点路径作为互通协议事实在本工程重写；没有导入原仓库历史、凭据、日志、缓存或新的组件所有权。本机外部目录不出现在 CMake 依赖中，最终独立路径审计通过。
