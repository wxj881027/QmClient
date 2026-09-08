# 旧版 Qm 卡片与 UI/UX 来源审计

审计日期：2026-09-07

## 来源基线

- Git 分支：`dyl_dev`
- Git 提交：`6d37ad1c112a3dd9b178be5966cb3b25fc786bb9`
- 提交时间：2026-09-02 19:26:31 +08:00
- 本地来源：`tmp/qm-dyl-dev-6d37ad1c/`，2026-09-07 由上述提交通过 `git archive` 导出并解压；归档为 `tmp/qm-dyl-dev-6d37ad1c.zip`。没有切换工作树，也没有覆盖或复制旧实现到当前编译源码。
- 快照范围：该提交的归档文件；不含 Git 历史，也不展开 submodule 的独立仓库内容。旧快照内的规则/历史方案只作来源资料，当前工作仍以仓库根规则为准。
- 本轮阅读范围：主代理已直接打开旧 `QmCardRegistry.h`、`QmCardOrderModel.h`、`settings_resource_jobs.h`、`SettingsCardDeckLogic.cpp`；Luna 已对照 `QmCardOrderModel.cpp` 与 `qm_card_order_model_test.cpp` 完成新模型的跨页/插入/分组修正。资源 loader 的 `.cpp`、调用方及资源生命周期仍待逐项验证，不把既有审计文字当作本轮已读证据。

旧 Qm 是本项目 UI 与卡片行为的主来源，但仅作为行为、状态语义、兼容格式、性能约束和验证案例的参考。不得复制旧源码、旧类层级、旧命名空间、旧 TClient 聚合器或旧 UI runtime。BC `tmp/bc-bestclient` 只作为同源实现和 DDNet 20.0 对照，不替代旧 Qm 行为。

## 关键文件

### 卡片注册与默认布局

- `src/game/client/QmUi/QmCardRegistry.h`
- `src/game/client/QmUi/QmCardRegistry.cpp`

注册表的真实模型不是只有 `id/page/title/icon`，而是：

- stable ID 命名空间：`qm:`、`tclient:`、`deck:`；
- 默认 tab；
- 默认列：`Full`、`Left`、`Right`；
- 列内默认顺序；
- 标题、搜索关键词、描述；
- stable ID 到 tab 的导航解析；
- 旧 Qm key 到 stable ID 的迁移；
- 从注册表生成完整默认 entries；
- 搜索结果携带当前 tab 和 stable ID 导航目标。

旧注册表同时容纳 Qm、TClient 和 deck 卡片。当前提交中默认表按注释包含 38 个 Qm 侧栏模块，并有 TClient/deck 命名空间卡片；审计时通过 `QmCardRegistry.cpp` 的默认表和 `QmCardRegistryTest` 做唯一性与覆盖核对，不能只按当前重构分支的 4 个 Qm feature 推断卡片规模。

### 全局顺序模型

- `src/game/client/QmUi/QmCardOrderModel.h`
- `src/game/client/QmUi/QmCardOrderModel.cpp`

`qm_card_order::CModel` 是所有设置卡片共享的顺序事实源，页面只是投影，不维护页面私有顺序。关键行为：

- `stableId|tab|column|order;` 序列化格式；
- 兼容旧 `id:column:order` 格式；
- 默认全集与用户配置 merge，缺失卡补回默认，未知/非法项跳过；
- `Move` / `MoveToTab` 跨列、跨 tab 移动；
- `LayoutRevision` 与 `StateIndexRevision` 分离，避免布局变化打断拖拽和动画；
- stable ID 到 state index 的 O(1) 查询；
- 迁移只在布局精确匹配旧版本时执行，用户已自定义布局时不覆盖。

### 卡片 deck 与交互

- `src/game/client/QmUi/SettingsCard.h`
- `src/game/client/QmUi/SettingsCard.cpp`
- `src/game/client/QmUi/SettingsCardDeck.h`
- `src/game/client/QmUi/SettingsCardDeck.cpp`
- `src/game/client/QmUi/SettingsCardDeckLogic.h`
- `src/game/client/QmUi/SettingsCardDeckLogic.cpp`
- `src/game/client/QmUi/SettingsCardGeometry.h`
- `src/game/client/QmUi/SettingsPageLayout.h`

真实交互包括：

- `m_IsCollapsed`、`m_IsVisible` 和默认折叠状态；
- 卡片定义缓存与 definition revision；
- 两列/单列/Full 列布局；
- header 预布局输入，保证按下/释放跨帧时仍命中正确几何；
- Ctrl/标题拖拽、跨列放置、自动滚动和 drop feedback；
- 入场、内容高度、重排动画；
- clipped 卡片和 active item continuation；
- 每帧诊断计数：definitions prepare、measure、动画 resolve、预布局输入、渲染卡片数和几何。

因此“全局卡片”和“UI/UX”在旧 Qm 中本来就是一个系统，不能先做一个孤立 registry 再临时补 UI。

### 主题、导航与设计 token

- `src/game/client/QmUi/UiNavigation.h/.cpp`
- `src/game/client/QmUi/UiTheme.h`
- `src/game/client/QmUi/UiTokens.h`
- `src/game/client/QmUi/QmLayout.h/.cpp`
- `src/game/client/QmUi/QmAnim.h/.cpp`
- `src/game/client/QmUi/QmAnimCurves.h`
- `src/game/client/QmUi/QmAnimResolve.h/.cpp`

旧 Qm 已经定义了 tab/list navigation、surface/border/focus/selected 主题状态、统一 spacing/radius/font/motion token，以及 content width、two-column threshold、card padding、header/handle 尺寸和 DPI 缩放规则。新 spec 不能只描述 descriptor 字段，还必须保持这些交互和视觉契约。

### 来源测试

- `src/test/qm_card_registry_test.cpp`
- `src/test/qm_card_order_model_test.cpp`
- `src/test/settings_card_deck_logic_test.cpp`

测试覆盖重复 stable ID、默认布局、TClient layout migration、未知/非法配置、序列化 round-trip、列投影、折叠、预布局输入、动画条件、拖拽放置和 geometry 变化。

### 资源页面与设置页加载参考

- `src/game/client/components/settings_runtime_cache.h/.cpp`
- `src/game/client/components/settings_resource_jobs.h/.cpp`
- `src/game/client/components/section_loader.h/.cpp`
- `src/game/client/components/qmclient/settings_resource_preview.h/.cpp`
- `src/test/section_loader_test.cpp`
- `src/test/assets_preview_scale_test.cpp`

旧 Qm 的资源页行为重点是：页面/section/cache key 分层；语言、字体、backend、窗口尺寸、UI scale 和 config hash 参与 cache key；目录扫描、decode、job result merge 和 GPU upload 分预算；可见项优先于后台预取；generation/异步结果不能覆盖新页面；资源目录变化才清空 resource plan，窗口/DPI/UI scale 主要触发布局或局部重建。当前分支的 `CResourcePageCache` 已独立重写这些生命周期中的 generation、去重、失败和 invalidate 底座，但尚未接入旧 Qm 的实际 asset provider、预算控制和 graphics upload。

## 对当前重构的结论

当前分支新增的 `CCardRegistry` / `CCardOrderModel` / `CCardUiModel` / `CResourcePageCache` 都是独立重写的底层骨架，不是旧 Qm 源码移植；它们还没有达到旧 Qm 卡片系统的行为覆盖。缺口至少包括：

- tab/column/full layout 投影；
- global order merge 与旧配置迁移；
- layout/state revision；
- drag reorder 与跨 tab 移动；
- pre-layout input 与 active item continuation；
- definition cache 和 measurement invalidation；
- motion/geometry diagnostics；
- 旧 Qm theme/token/navigation 的实际接入。

因此当前不能把旧版行为审计直接视为实现规格。下一步必须按本文来源和 `ui-ux-refactor-plan.md` 的全局卡片契约，把这些行为拆成可测试的 model/logic，再接入官方 UI。

## 读取命令

```powershell
git show dyl_dev:src/game/client/QmUi/QmCardRegistry.cpp
git show dyl_dev:src/game/client/QmUi/QmCardOrderModel.cpp
git show dyl_dev:src/game/client/QmUi/SettingsCardDeck.cpp
git show dyl_dev:src/test/qm_card_registry_test.cpp
```
