# QmClient UI/UX 适配当前重构计划

日期：2026-09-08  
状态：**审查后修订，目标契约待实现；不得以既有阶段 2 的局部完成替代本计划验收**  
来源基线：旧版 `dyl_dev` 快照 `6d37ad1c112a3dd9b178be5966cb3b25fc786bb9`（本地 `tmp/qm-dyl-dev-6d37ad1c/`）

本文是当前唯一的 Qm UI/UX 设计、架构和迁移计划入口。它同时覆盖用户可见交互、全局卡片的数据模型、代码层模块化边界、资源生命周期、验证矩阵和执行顺序。旧版 Qm 只作为行为与视觉来源，不作为代码移植目标。

## 1. 结论

旧 Qm 的 UI/UX 资产应拆成三层处理，但“全局卡片”必须按新的目标重新定义，不能沿用旧版写死页面的方式：

1. **保留为行为契约**：稳定卡片 ID、页面/列/顺序模型、搜索与导航、可见/折叠、输入优先级、动态高度不重叠、资源 generation/fallback、键盘/触控/DPI/resize 验收矩阵。
2. **重写为当前实现**：旧 `QmUi` 类层级、TClient 聚合器、页面私有业务状态、旧配置字段、旧 renderer/runtime 和任何直接访问 `CGameClient` 的 UI 代码。
3. **交付目标不是少量 Qm 卡片**：设置系统中的全部页面、全部设置项和所有适合模块化的内容都必须纳入全局卡片组件体系。旧版约 38 张卡片只是历史来源规模，不是迁移上限；当前分支的 4 个 Qm feature 只是第一批迁移对象。复杂页面可以先以 `native.container` 卡片接入，但不能永远留在页面私有实现中。

目标形态是：

```text
Feature model / settings descriptor
        ↓  窄 adapter + immutable snapshot
Global Card Registry（卡片定义不绑定页面）
        ↓                 ↓
Page Declaration（页面声明 card ID）  Global Search（内部文案匹配 card content）
        ↓                 ↓
Card projection / order / drag state / persistence
        ↓
legacy presentation 与 card presentation 共用同一模型
        ↓
DDNet UI renderer / resource provider
```

## 1.1 全局卡片的目标模型

全局卡片不是“每个页面预注册几张卡片的列表”，而是可被多个设置页面复用的独立设置模块。卡片定义和页面声明必须分离：

```text
GlobalCardDefinition
  id: qm.<feature-or-setting>
  title / description / category / icon
  searchable text（标题、说明、设置项、动作、别名）
  settings / actions / presentation contract
  default placement hints（可选，不是所属关系）

PageDeclaration
  page id / title
  ordered card ids
  optional grouping / visibility policy
```

页面只声明“本页要显示哪些 card ID、以什么默认顺序和分组显示”，不能把卡片实现写死在页面 renderer 中。设置系统的所有页面都必须使用这个声明模型，包括 General、Graphics、Sound、Tee/Player、Appearance、Controls、DDNet、Assets、QmClient、TClient、Search 以及后续新增页面。一个卡片可以出现在多个页面，也可以只在搜索页出现；卡片从一个页面移到另一个页面不应要求搬运业务代码或复制 UI 分支。

搜索页是全局投影，不是某个设置页的局部搜索框：输入关键词后，系统对所有可搜索卡片的标题、描述、设置项标签、动作文本和稳定别名做匹配，返回卡片 ID、所属声明页面和导航目标。搜索结果仍消费同一 presentation contract，不复制一套搜索专用卡片。

卡片位置分为页面声明默认值、统一存储的用户布局覆盖和当前视图投影。“全局”指卡片定义和布局事实源统一，不表示一个 card ID 只能有一个 page/order。业务状态按 card ID 共享；页面中的放置记录按 `(page_id, card_id)` 标识，同页不重复声明同一卡片。分组、列、顺序、折叠和本页隐藏属于放置记录；从 A 移到 B 只移除 A 的放置并插入 B，不改变其他页面引用，B 已有该卡片时合并为一个放置。页面默认声明保持不变，用户移出通过覆盖记录表示，重启 merge 不能把它补回原页。恢复单页默认仅清除该页覆盖。

Search 是动态页面声明：消费查询返回的去重 card ID 集合，不拥有持久化 placement。它显示可直接操作的真实卡片及命中字段，业务动作与分类页一致；可导航到任一有效放置，搜索独有卡片可直接操作。同一卡片的不同投影必须有独立控件/焦点 ID，避免共享业务状态造成输入串扰。搜索结果排序不写回分类布局；跨页布局编辑以明确的目标页面和插入位置提交。

示例：图形页声明 `[qm.display, qm.effects]`，外观页声明 `[qm.effects, qm.nameplates]`。调整分类只修改声明；`qm.effects` 的控件实现和设置值始终只有一份。输入其内部开关标签时，搜索页直接显示这张卡片，而不是只显示页面入口。

## 1.2 拖拽与让位 UX 契约

拖拽不是“给卡片加一个 handle，再调用相对移动”。目标 UX 是一个可理解、可恢复、可持久化的全局布局编辑器：

- 被拖卡片有稳定的 drag source、ghost/预览和明确的 drop target；命中区域、显示区域和拖拽区域来自同一份 geometry snapshot。
- 拖拽过程中，同列或目标列的相邻卡片应以连续、可预测的让位动画响应 drop preview；不得出现跳位、重叠、瞬移、拖拽后整列重排闪烁或只画图标但没有真实拖拽行为。
- 松手后才提交布局事务；拖拽预览不修改已提交布局。取消或无效投放恢复原布局。持久化失败保留已提交的内存布局和 dirty 状态，明确提示并支持重试，不能谎报保存成功。
- 位置持久化保存 `(page_id, card_id)` 放置、移出默认声明的覆盖记录、分组/列/顺序和 schema 版本；新增卡片补默认位置，未知记录隔离，非法记录局部回退，不重置其他有效自定义布局。
- 跨页拖拽通过明确的页面目标进入目标页预览，保留 source 和事务；普通切页、搜索跳转、Escape、失焦或关闭设置取消未提交拖拽并释放输入捕获。resize/DPI 变化重建几何，目标仍有效时续接预览，否则取消；已提交布局始终保留。动画关闭时仍保持相同最终位置和输入结果。

旧版 Qm 的让位动画和拖拽手感是设计参考，但不能直接复制旧类层级。实现验收必须同时看动画连续性、取消/回退、持久化 round-trip、跨页面投影和真实客户端操作；历史会话里“松手提交 + FLIP”只是某一旧实现状态，不足以替代本契约。

## 2. 证据与现状

### 2.1 旧版已验证的设计资产

- 旧版把“全局卡片”和 UI/UX 作为一个系统：卡片有稳定 ID、页面、Full/Left/Right 列、默认顺序、搜索词、折叠/可见状态，并支持跨页移动、拖拽反馈和动态布局。
- 旧版定义了统一的 surface/border/focus/selected 状态、spacing/radius/font/motion token，以及键盘/列表导航、DPI 缩放、资源 cache 和 generation 约束。
- 旧版的验收重点不是截图相似，而是：折叠后高度真实变化、同列卡片不重叠、历史隐藏项过滤（本次改为本页隐藏不影响全局搜索，见 2.10）、异步结果不覆盖新页面、输入只被一个 owner 消费。

这些内容在本文后续的卡片、presentation、资源和验证章节中固化为重构后的用户可见契约。

## 2.5 架构与所有权契约

全局卡片是 UI 与代码结构的共同抽象，不是单纯的绘制容器：

- Feature model 拥有业务状态、配置、生命周期、输入、渲染和测试。
- Global Card Registry 拥有卡片定义、页面声明、presentation contract 和稳定 ID；初始化后冻结。
- Page Declaration 只拥有页面标题、card ID 列表、分组和默认顺序，不拥有卡片业务状态。
- Card Projection 负责把全局卡片按当前页面、用户 order、可见性和列布局投影出来。
- Presentation 只读取 model 和 immutable snapshot，不查找完整 `CGameClient`、TClient 聚合器或外部服务。
- Renderer 只消费准备好的 presentation state；文件、网络、解码和阻塞操作必须在 provider/worker 边界外完成。

官方功能通过窄 adapter 或 native container 接入，不复制官方业务逻辑；Qm/TC/BC 重构功能通过独立 feature 接入，不恢复旧 TClient 聚合器。

## 2.6 Descriptor 与页面声明

`GlobalCardDefinition` 至少包含：

```text
id / owner / title_key / description_key / icon_id
feature_id / presentation_id / search_keywords
settings / actions / default_placement_hint / version
```

它不包含不可变的单一 `page` 所有权。页面使用独立的 `PageDeclaration`：

```text
page_id / title_key / card_ids / groups / default_order
```

一个 card ID 可以被多个页面声明，也可以只由搜索页投影。页面移动、重排和搜索跳转不得复制卡片实现。

## 2.7 Presentation contract

每个 `presentation_id` 必须定义输入 snapshot、输出 action、最小尺寸、DPI 规则、信息密度、normal/hover/focus/pressed/disabled/loading/error/collapsed 状态、资源 fallback、输入优先级和键盘/触控可访问性。首批 contract 包括 `settings.toggle`、`settings.stepper`、`status.panel`、`resource.list` 和承载官方复杂页面的 `native.container`。

功能关闭只停止业务 logic、游戏 render 和业务资源任务；设置卡片仍显示开关并允许重新启用。投影隐藏或离屏时跳过卡片绘制和预览资源工作；卡片隐藏不得改变功能是否运行。不可用功能保留说明与禁用态，不触发不可用动作。

## 2.8 偏好、资源与生命周期

用户偏好分为按 `(page_id, card_id)` 保存的放置/折叠/本页隐藏，以及全局 UI scale、theme 和 animation。布局事务完成后自动安排保存，连续操作可合并写入，离开设置和正常退出补刷；不能要求用户发现并点击 Save 才保留拖拽位置。写盘不在逐帧拖拽路径中执行，失败保留旧文件和 dirty 状态，重试成功才清除对应 revision，较旧保存结果不能清除新改动。

新 schema 必须定义旧单 placement 的迁移：保留原页、列、顺序和偏好，其他页面引用按新声明补齐；旧 ID 仅经显式白名单映射。覆盖首次保存、覆盖已有文件、写入失败、重启 round-trip、默认声明增删和未来 schema 回退；未知 schema 不自动覆盖原文件。

资源状态统一为 `UNLOADED → LOADING → READY/FAILED`。同一 generation 只允许一个请求；generation、页面切换、resize、DPI、connect/disconnect、map change、demo seek、shutdown 后的旧结果不得覆盖当前 UI。worker 不直接修改 UI 或 graphics 对象，失败必须有可见 fallback。

## 2.9 统一验证矩阵

自动证据覆盖 registry/order/projection/search/presentation/resource lifecycle、非法偏好、输入优先级、卡片折叠、让位几何和持久化 round-trip。工程门禁按风险执行 boundary、game-client、testrunner、runtime smoke。

人工证据单独记录：640×480、1280×720、1920×1080、4K、resize、DPI、键盘、触控、IME、动画开关、首次/重复/失败加载、切页、重进、fullscreen、demo/spectator。测试全绿不等于视觉 UX 完成。

### 2.2 当前分支已经具备的基础

- `CCardRegistry` 已有 page/feature/card 注册、owner、presentation ID、输入优先级、默认折叠/列和冻结规则，但当前 `SCardDescriptor::m_PageId` 仍把卡片绑定到单一页面，这只是过渡骨架，不是目标全局卡片模型。
- `CCardOrderModel` 已有默认值、merge、序列化、跨页/跨列移动、layout/state revision 和 dirty 状态接口。
- `card_presentation` 已把 normal/hover/focus/pressed/disabled/loading/error/collapsed 状态和 action 优先级抽成纯逻辑。
- `CQmCardSettingsView` 已使用 `CCardUiModel`、搜索、键盘焦点、顺序移动、可见性/折叠和图标资源 loader；资源 loader 已有 generation、失败记录和关闭时取消路径。

### 2.3 当前缺口

- 当前模型把 `PageId` 放进卡片 descriptor，页面和卡片没有真正解耦；因此不能支持“页面按 card ID 自由声明”、同卡片多页投影、跨页面复用和全局搜索导航。
- 当前搜索主要基于 descriptor 的有限字段，尚未建立对卡片内部设置项文本、动作文本和说明的统一 searchable content 索引。
- 旧版完整的 tab/column/full 投影、默认全集迁移和遗留布局兼容尚未形成当前分支的最终契约。
- 当前 presentation 还是最小 geometry/action 解析，尚未覆盖旧版的 definition cache、measure invalidation、拖拽预布局、动态高度/重排动画和真实 hit-test。
- 旧 UI 与 card UI 尚未完成同一 feature model 的双 presentation smoke；当前阶段 2 仍是“已实现、人工待验”。
- 资源底座已接入图标和 Assets 消费者，但首次加载、缓存命中、失败回退、DPI/resize 和同场景性能 A/B 仍需真实客户端验证。

### 2.4 历史会话补充的实现边界

Codex 既有会话“卡片+动画系统”和“拆分设置页 UI 统一计划”补充了三条不能丢失的事实：

- 历史“松手提交 + FLIP 释放动画”只描述旧时间点的实现，不限制本次目标。拖拽中的连续让位是必需能力；事务提交时机与预览动画是两个独立问题。
- 历史 B1/B2/C1 分批记录不降低本次要求。页面声明解耦、内部文案搜索、真实拖拽让位及跨页布局持久化必须实际交付；可按模块分批迁移，但不能仅留接口并标全局卡片完成。
- 设置页入场背景闪亮曾定位为文本预热阶段重复绘制卡片 shell 的合成问题；这类问题应通过 RenderOnly/预热路径的绘制契约解决，不能归因给 hover，也不能靠改变主题颜色掩盖。

滚动条宽度、滚轮 profile 和其他后台任务正在独立演进，本计划只要求卡片输入 owner 与滚动 owner 的接口清晰，不修改其具体 token 或尺寸。

历史会话只作为补充证据，不作为当前完成证明。特别是旧会话中出现的全量测试、quick gate、migration clean 和视觉修复数字，均来自旧时间点或旧分支；当前状态仍以本仓库 `plan.md`、`verification-checklist.md` 和实际运行结果为准。

### 2.10 本次审查的具体问题与修正依据（2026-09-08）

- **高：页面所有权与多页引用冲突。** `card_registry.h` 的 `SCardDescriptor::m_PageId`、`card_registry.cpp::RegisterCard` 的页面前置校验及 `card_order_model.h::Find(Id)` 均依赖单卡片单位置。只改 renderer 无法实现声明式多页复用，必须先改定义/放置身份和 merge 契约。
- **高：搜索未覆盖内部内容且存在双实现。** `card_registry.cpp::Search` 匹配 ID、标题 key、描述 key、关键词；`qm_card_settings_view.cpp::Refresh` 另行匹配本地化标题、ID 和关键词。两者均没有内部控件文本索引，过滤也不一致。应合并成一个查询服务，分类页与搜索页共用卡片 presenter。
- **高：拖拽被执行阶段条款降级。** 原 2.4 和 P1 排除拖拽中让位，与 1.2 冲突。当前 view 的 `Move` 是相对移动，不能证明拖拽完整。旧快照 `SettingsCardDeck.cpp` 的 drag placement 与 reflow 路径可作行为对照，本轮未运行旧客户端，不声称已验证其实际手感。
- **高：关闭业务与隐藏设置混淆。** 原 2.7/3.4 会要求关闭功能时不画设置入口，也会让隐藏卡片影响游戏功能；现已明确两种生命周期独立。
- **中：持久化已有基础，但交付策略不足。** `card_preferences_storage.cpp` 已有临时文件、同步和失败保留 dirty；当前 view 的 Save 和 runtime 的退出保存不能替代拖拽事务自动保存及多放置 schema。不得把“有 Serialize”当成布局体验完成。

搜索内容必须由同一设置描述提供给控件和索引，包括本地化标题、说明、控件标签、选项、动作及稳定别名；复杂 presenter 显式导出内部文本与 setting/action ID，不能靠手写几个 keyword 冒充覆盖。搜索无需绘制卡片或启动业务来发现文本，不索引密码、聊天等任意用户数据。查询规则为去首尾空白、Unicode 无关大小写匹配、空白分词且全部词需命中（允许跨字段）、中文连续子串匹配；空查询显示可搜索全集，结果按 card ID 去重，以标题/控件命中优先并以稳定 ID 打破同分。此处不承诺正则语法。

语言或内容 revision 变化时更新索引，折叠、未访问页面和功能关闭不影响检索；本页隐藏只影响该页布局，搜索仍能找到并恢复卡片。不可用项显示原因，禁止执行动作。索引与渲染共用文本源，新增内部设置项的测试必须证明无需修改搜索页面即可命中。

本计划分批限制的是接入数量，不是能力完整度。首个切片即验收多页引用、内部文本检索、直接操作、拖拽预览让位、跨页投放和重启恢复；仅注册 descriptor、按钮调序、释放动画或空 provider 均不能作为完成替代品。`native.container` 只用于保留复杂控件行为，仍需导出完整内部搜索内容；整页套壳不计作内部设置模块化完成。

## 3. 适配原则

### 3.1 保留用户结果，不保留旧结构

迁移时只对照旧版的用户可见结果、配置兼容和性能不变量。不得复制旧 `QmUi` 文件、旧命名空间、旧 TClient 聚合器或旧页面私有状态。任何需要官方能力的地方必须使用 `Qm_ADAPTER` 或登记过的 `UPSTREAM_CHANGE`。

### 3.2 一份状态，两种 presentation

legacy UI 和 card UI 必须共用 feature model、descriptor、可见性、折叠、排序、搜索词、输入 action 和错误回退。两套 UI 只能在布局和绘制上不同，不能分别维护“旧设置值”和“新卡片值”。

### 3.3 先稳定几何，再增加装饰

优先收口 content width、列投影、header/content rect、折叠高度、滚轮 owner、焦点顺序和资源生命周期；主题、强调色、复杂动画和更多图标放在几何契约稳定之后。动画关闭时布局、输入和状态结果必须不变。

### 3.4 功能关闭时零额外 UI 成本

关闭功能不运行其业务输入/logic/render；设置开关仍可见可用。隐藏设置卡片只影响 presentation，不关闭业务功能。卡片 registry 初始化后冻结，搜索/布局查询不能重建业务对象。

## 4. 分阶段执行

### P0：重做全局卡片契约与差异台账

范围：本文的全局卡片契约、`qm-legacy-card-audit.md`、当前 4 个 Qm feature。

产出：把卡片定义从页面声明中拆出，形成 `GlobalCardDefinition`、`PageDeclaration`、`CardProjection` 三个明确对象。为每个卡片补齐 `qm.<feature>` model ID、owner、presentation、默认 placement hint、搜索内容 provider、配置消费点、旧行为差异和删除条件；页面只维护 card ID 列表、分组和默认排序。

出口：台账没有“仅注册未接行为”的 DONE；未确认项标 `DEFER`，旧配置映射进入迁移白名单。

首批差异表至少记录以下状态：

| 契约 | 当前判断 | 计划处理 |
| --- | --- | --- |
| 全局 card ID 与页面声明解耦 | 当前未完成 | 移除 descriptor 对单一 `PageId` 的所有权依赖，增加多页面 projection |
| 搜索卡片内部文本 | 当前未完成 | 建立统一 searchable content provider 和全局搜索索引 |
| stable ID、默认顺序、可见/折叠 | 底层已部分实现 | 重做为全局默认 placement + 用户布局 merge |
| 拖拽注册、ghost、跨页持久化 | 部分/未完成 | 先接全局 order model，再接页面 projection |
| 让位/释放动画 | 旧版 UX 参考较好，当前重构未等价接入 | 先冻结让位、取消、回退和持久化契约，再选择 FLIP 或等价实现 |
| Name Plate/Laser 等复杂布局 | 高风险、未迁移 | 不纳入阶段 2 完成条件 |
| 滚动条尺寸与 profile | 并行范围 | 只消费窄接口，不修改具体规格 |

### P1：全局投影、搜索与持久化逻辑

新增或扩展 `*_logic.h` / 测试，覆盖：

- 页面声明 card ID 到 projection 的解析；同一 card 在多个页面出现时不复制业务状态；
- 全局默认 placement、用户 order/column/tab 偏好和页面声明的 merge；未知/非法 card ID 回退；
- 卡片内部标题、说明、设置项、动作和别名的 searchable content 生成与匹配；搜索结果到页面/卡片导航目标的解析；
- header/content/hit/drag rect 同源；折叠和动态高度全过程不重叠；
- 输入优先级、键盘焦点、搜索结果导航和本页隐藏后的全局检索/恢复；
- `normal/hover/focus/pressed/disabled/loading/error/collapsed` 状态转换；
- layout revision 与 state revision 不互相打断。

拖拽必须覆盖 `idle → armed → dragging/preview → commit 或 cancel → settling`。超过拖拽阈值才获取捕获，标题内开关/输入框优先处理自身输入。预览布局用占位计算插入位置，相邻卡片在拖拽过程中连续让位；目标变化时从当前动画位置续接，ghost 跟随指针。目标命中使用稳定布局槽位和阈值，控件命中使用当前显示几何，二者由同一几何快照推导，避免让位动画反复改变目标造成抖动。支持跨列、Full 行、边缘自动滚动、跨页目标和取消回位。

松手才提交事务，但不限制预览动画。只在插入目标、滚动或尺寸/高度变化时重新计算受影响布局；逐帧更新已有动画，不遍历全局卡片、不重新测量所有内容、不重建搜索索引。预热或 RenderOnly 阶段只允许收集布局/文本，不得重复绘制半透明 card chrome。

不在这一阶段接入复杂渲染器或大范围修改官方菜单。出口是纯逻辑单测和 boundary gate 通过。

### P2：首个真正全局卡片垂直切片

选择 `qm.diagnostics` 或 `qm.player_indicator` 作为首个完整切片：先由两个不同设置页面声明同一个 card ID，再接入 legacy presentation 和 card presentation，完成全局搜索、隐藏/恢复、折叠、键盘导航、resize/DPI 和资源失败 fallback。

出口：必须完成拖拽中让位、跨列/跨页投放、取消回位、自动保存和重启恢复，并记录真实客户端操作证据。一个官方卡片与一个 Qm 卡片走同一 presentation contract；同一 Qm card 可被两个页面声明且状态一致；搜索页可通过内部设置项文本命中并导航；旧 UI 与新 UI 的状态变更结果一致；有 game-client smoke 证据。

普通设置卡片优先于 Name Plate、Laser 等复杂布局。出现“有拖拽 handle 但没有注册 deck item/drag handler”的假交互时，必须以注册表、ghost 和行为测试为完成依据，不能以图标存在作为完成依据。

### P3：资源与交互生命周期

把图标/Assets provider 的 `UNLOADED → LOADING → READY/FAILED`、generation、取消、resize/DPI、页面切换和 shutdown 接入真实页面；补齐滚动 owner、触控命中、IME/文本输入和 popup 关闭条件。

出口：首次/重复/失败/切图/resize/shutdown 矩阵通过；worker 不直接接触 UI 或 graphics；资源指标写入 diagnostics。

### P4：全设置系统卡片化与视觉收口

按页面和功能分批迁移整个设置系统：General、Graphics、Sound、Tee/Player、Appearance、Controls、DDNet、Assets、QmClient、TClient 以及所有后续设置页。每批只迁一个明确模块，但最终必须覆盖全部设置项，并同步 `feature-ledger.md`、配置/i18n/资源/生命周期条目。页面只新增/调整 card ID 声明，不复制卡片业务实现。旧页面私有设置模块只有在全局卡片接管、搜索可见、布局可持久化、测试和 smoke 完成后才能删除。

视觉收口只允许修改共享 token/primitive；不得在业务页面继续手写卡片 chrome、滚动条、输入框外壳或折叠按钮。

滚动条相关改动必须另立功能范围并登记，不作为本计划的隐含副作用。

## 5. 验证门槛

每个 P 阶段至少保留以下证据：

- 纯逻辑测试：registry/order/model/presentation/resource lifecycle；
- `py -3 qmclient_scripts/check_qmclient_boundary.py`；
- 涉及 UI 的 Release `game-client` 构建和对应 `testrunner`；
- runtime smoke；
- 人工客户端矩阵：640×480、1280×720、1920×1080、4K，窗口 resize、DPI、键盘、触控、IME、动画关闭；
- 真实视觉/手感、GPU 后端差异和性能 A/B 单独记录，未执行不得标通过。

完成判定不是“卡片出现”，而是：同一 model 的双 presentation 行为一致；几何、输入、资源和生命周期不变量有自动证据；旧入口不可再被生产路径调用；功能台账、验证清单和官方触点台账同步更新。

控件尺寸也要按调用方契约验收：例如 multiline 输入的高度由调用方显式按两行 line size 提供，公共滚动组件不猜内容高度。UI 预热必须有负向测试，证明文本/layout 收集不会绘制可见 card chrome 或造成半透明背景二次合成。

## 6. 明确不做

- 不把“先迁几张 Qm 卡片”当成全局卡片完成；最终范围是设置系统全部页面和全部设置项的卡片化。
- 不恢复旧 TClient 聚合器、旧 `tc_*` 配置键或旧 UI runtime。
- 不为追求视觉相似直接改协议、预测、地图、demo、服务端玩法或官方 renderer 核心。
- 不把历史文档中的过期 FBO、glass、MSDF/SDF 立即落地、11 tab 重组等方案当作当前任务；需要重新采用时单独登记证据和 owner。

## 7. 相关文档与代码入口

- 行为来源与缺口：[`qm-legacy-card-audit.md`](qm-legacy-card-audit.md)
- 当前唯一 UI/UX 规范与执行计划：本文
- 阶段状态：[`plan.md`](plan.md)
- 现有底层：`src/game/client/ui/card_registry.*`、`card_order_model.*`、`card_presentation.*`、`card_deck_projection.*`
- 当前 Qm UI 接线：`src/game/client/components/qmclient/presentation/qm_card_settings_view.cpp`
