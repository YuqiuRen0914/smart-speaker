# 账号展示资源

本目录是桌面宠物与音箱共同依赖的 `lunaredu_account_resources` ESP-IDF component。
它拥有账号头像描述、宠物资料、动画清单与离线展示缓存；不启动线程或网络会话。

页面通过 `lunaredu_account_resources.h` 读取原生快照，不依赖 JSON。
产品组合通过 `lunaredu_account_resources_json.h` 把 `observer()` 返回的静态
`DeviceLinkObserver` 注册到 DeviceLink 配置：

- OS 拥有账号绑定、凭据、绑定代次、私有资源作用域和云请求。
- 此模块接收绑定快照和借用的 presence JSON，解析展示资料并通过 Resource 公共接口请求头像。
- 回调之间串行更新，读取返回独立快照；过期的绑定修订不能恢复旧账号数据。
- 换账号、改变绑定代次或解除绑定时清空展示资料。缺失或不完整的资源描述保留最近完整版本，
  显式 null 则撤销对应资源。设置保存失败时保留下一次重试机会。
- `lunar_pet_cache/manifest` 保留现有设备的展示缓存格式；OS 在 `lunar_link/binding`
  单独保存绑定身份。OS 首次读取旧缓存中的身份用于迁移，已保存的空绑定阻止旧身份复活。

`AccountResourcesSnapshot` 是产品组合快照。Eyes 的姿势能力只接收自己的
`CompanionLibrarySnapshot`，由产品转换，Eyes 不依赖本模块或 DeviceLink。

验证：`pwsh tools/repo/test-account-resources.ps1`。测试直接链接当前 OS 绑定实现与
本模块，覆盖离线迁移、资源增量、写入失败重试、换绑、解绑和迟到回调。
