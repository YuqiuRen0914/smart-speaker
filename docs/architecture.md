# 独立工程的架构边界

状态：active。核对日期：2026-11-10。依据为产品装配、显式组件清单与 LunarOS 源码所有权声明；本次只调整源码归属和构建依赖，没有重新设计播放、任务或设备生命周期。

## 目录与所有权

| 目录 | 负责什么 | 修改入口 |
| --- | --- | --- |
| `main/` | 音箱启动、硬件装配、页面、网页曲库和产品策略 | `product.cmake`、`main/product/`、`main/smart_speaker/` |
| `lunaros/os/` | Core 资源与调度、Runtime 生命周期、公开配置/设备接口、桌面与显示机制 | 各组件公开头与 `source_ownership.json` |
| `lunaros/apps/` | 音乐等应用的领域状态及行为 | `app_music` 等真实所有者 |
| `lunaros/services/` | Audio、Network、Storage、OTA、Local Web 等服务及其资源 | 各服务公开契约与端口 |
| `lunaros/bsp/`、`components_device/` | 板级适配、外设驱动与设备资源接口 | 对应设备组件 |
| `components/` | 协议、编解码、图形和执行引擎 | 对应公共接口；上游源码与端口分开 |
| `lunaros/assets/`、`data/` | 固定 UI/字体资产、工厂存储输入 | 资源来源及许可证 |
| `cmake/`、`tools/` | 独立构建、依赖约束、资源暂存与验证 | `tools/build.py`、`cmake/components.cmake` |
| `test/` | 产品主机行为回归 | `test/run_host_tests.mjs` |

产品通过公开头文件和组件契约调用 App、Service、OS 和驱动。下层不引用 `main/` 的产品实现；跨模块组合保留在产品层。音乐状态、音频资源、网络生命周期仍各有唯一所有者，不另加泛化 shared/common 装配层，也不把系统源码摊平到 `main/`。

## 独立构建契约

`cmake/components.cmake` 明确列出本产品的 48 个本地 IDF 组件身份（包括嵌套接口适配器）；`product.cmake` 选择直接产品依赖，必需服务由保留的启动装配机制选入。该集合来自原产品实际构建依赖闭包，并非复制所有产品或整个 SDK。

LunarOS 在本仓库内维护，是工程的本地源码层，不要求用户取得独立 SDK。构建中保留 `LUNAREDU_*` 命名以维持现有组件契约；仓库根兼容变量始终指向当前 checkout。公开注册表组件由 `idf_component.yml`、`dependencies.lock` 和固定约束管理，下载目录不进入版本库。

`lunaros/source_ownership.cmake` 继续拒绝未分配或重复归属的 OS 源文件。上游校验清单与许可证保留；WAMR 在构建目录的副本内生成版本头，不改写校验过的源码。ESP-NOW 端口提供有输入/输出摘要的生成文件；修改生成输入需重新生成并更新摘要，不能手工漂移。

字体压缩包、网页 gzip 和 MicroPython QSTR 在构建目录由 Python 生成。存储池使用原有 LRZ1 格式和 `managed/fonts.*` 发布契约；产品只选择资源，不获得另一套存储状态。

## 扩展路由

壁纸和滚动启动器先修改产品页面，只有通用显示能力不足时才扩展 Display/Desktop。音乐封面和歌词状态归 Music，产品持有布局。游戏、AI 伴侣和番茄钟先确定独立 App 边界，再通过公开服务使用音频、网络与存储。网页 EQ 预设配置由产品页面提供，处理仍由 Audio DSP 执行。这些项目均为后续开发路线，本次没有实现。

## 修改后如何验证

构建入口或依赖变化：运行监控构建和 `tools/check-project.py --build-dir .build/firmware`。播放、音量、上传和退出变化：运行产品主机回归。涉及引脚、任务实时性、音频、显示或触摸的变更还需记录指定硬件上的实际观察；主机测试和固件构建不能替代真机验收。
