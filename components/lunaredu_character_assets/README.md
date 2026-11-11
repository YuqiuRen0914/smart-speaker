# Character assets

音箱与 Soft Pet 共用的中立角色几何数据，唯一来源为 `include/lunaredu_character_assets_pebble_geometry.h`。
组件只提供不可变数据，不持有渲染器、任务、设备或产品状态。

在仓库根目录运行 `python components/lunaredu_character_assets/tools/import_pebble_geometry.py <replica-base-url>` 重新生成。
两个消费者直接依赖该组件，不保留产品目录副本。

[新版鲸鱼娘完整归档](third_party/dsh-pet/README.md) 保存用户指定上游的全部动画、预览和互动参考，不编入固件。设备转换及交互边界见 [角色方案](../../docs/development/06-pet-character.md)。
