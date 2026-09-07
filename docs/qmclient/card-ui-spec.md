# QmClient 全局卡片与 UI/UX 设计规格

版本：0.1
日期：2026-09-07
状态：**底层及首批 List/Cards、Assets 消费者已接入，最终自动与人工验证分项记录**

旧 Qm 行为来源审计见 [`qm-legacy-card-audit.md`](qm-legacy-card-audit.md)，基线为 `dyl_dev` 提交 `6d37ad1c112a3dd9b178be5966cb3b25fc786bb9`。本文中标注为实现契约的字段和交互，必须优先与该来源及其测试对照；旧代码只能参考，不能复制。新架构只吸收行为不变量、兼容格式和性能约束，代码结构、命名、ownership 和生命周期必须独立设计。

本文是全局卡片系统和 UI/UX 层的共同契约。`CardRegistry` 不是单纯的数据列表，UI/UX 也不能绕过卡片模型自行维护业务状态。任何官方、Qm、重构后的 TC/BC 功能进入新 UI，都必须同时满足本文的 descriptor、presentation、交互、持久化和生命周期要求。

## 1. 目标与非目标

### 目标

- 用稳定 card ID 组织官方与扩展功能。
- 让旧 UI 与新 UI 共用 feature model、descriptor、可见性和偏好。
- 支持页面、搜索、排序、折叠、隐藏、键盘导航、触控、DPI/resize 和主题。
- 资源加载可缓存、可取消、可回退，不阻塞游戏主循环。
- 卡片关闭或不可用时，不构造业务输入、不运行业务 logic、不执行对应 render。

### 非目标

- 不复制 DDNet 官方业务逻辑。
- 不首发任意用户自定义 UI 或脚本化布局。
- 不引入通用 EventBus、万能 Context 或第二套 renderer。
- 不在卡片层承载网络、HTTP、预测、协议和服务端玩法。

## 2. 分层与所有权

```text
Feature model / adapter
        ↓
CardDescriptor + CardRegistry
        ↓
CardUiModel（偏好、可见性、排序、搜索结果）
        ↓
Presentation（legacy / card UI）
        ↓
Renderer / input adapter / resource provider
```

- Feature 拥有业务状态、配置、生命周期和测试。
- Registry 拥有页面、feature 引用和卡片 descriptor，初始化后冻结。
- `CardUiModel` 拥有用户偏好，不拥有业务状态。
- Presentation 只读取 model 和不可变 snapshot，不查找完整 `CGameClient` 或 TClient 聚合器。
- Renderer 只消费已准备好的 presentation state，不在绘制路径执行文件、网络或阻塞操作。

## 3. CardDescriptor 规范

每个 descriptor 至少包含：

| 字段 | 约束 |
| --- | --- |
| `id` | 全局稳定 ID，使用 `qm.<feature>`、`ddnet.<feature>` 等小写命名；不可复用 |
| `owner` | `upstream`、`qm`、`tc-rebuilt`、`bc-rebuilt` |
| `page` | 已注册页面 ID |
| `title_key` | i18n key，不写界面硬编码文案 |
| `description_key` | 可为空，但不能把业务说明塞进 renderer |
| `icon_id` | 稳定 atlas/manifest ID，不允许散落 SVG 路径 |
| `presentation_id` | renderer contract ID，如 `settings.toggle`、`status.panel` |
| `feature_id` | 可为空；非空时绑定 feature model |
| `search_keywords` | 小写、稳定、可翻译扩展的搜索词 |
| `order` | 默认排序，只用于初始布局 |
| `input_priority` | 数值越大优先级越高；用于输入冲突仲裁 |
| `default_visible` | 默认是否显示 |
| `default_collapsed` | 默认是否折叠 |
| `version` | descriptor schema 变化时递增；后续加入持久化迁移 |

注册规则：引用不存在的 page/feature、ID 不稳定、重复 ID、冻结后注册都必须失败。注册完成后 registry 只读。

## 4. 页面与布局模型

- 页面拥有稳定 ID、标题 key、排序和页面级能力声明。
- 卡片布局顺序为：用户 order → descriptor order → card ID。
- 搜索跨 owner、跨页面；搜索结果仍使用 descriptor 的 presentation contract。
- 隐藏卡片不出现在普通页面，也不出现在搜索结果；核心恢复/应用/取消操作不允许被隐藏。
- 折叠只影响 presentation，不改变 feature 状态和资源所有权。
- 不使用数组下标作为持久化标识。

## 5. Presentation contract

每种 `presentation_id` 必须定义：

- 输入：只读 UI snapshot 和当前 `SCardUiPreferences`。
- 输出：用户动作（toggle、activate、change、reset、navigate）。
- 尺寸：最小宽高、信息密度、DPI 缩放规则。
- 状态：normal、hover、focused、pressed、disabled、loading、error、collapsed。
- 资源：图标、文本容器、异步资源句柄和 fallback。
- 输入优先级：卡片内部控件优先于页面导航，页面导航优先于背景。
- 可访问性：键盘焦点、可读标题、disabled 原因、触控命中区域。

首批 contract：

- `settings.toggle`：布尔设置。
- `settings.stepper`：有限范围数值。
- `status.panel`：只读状态与快捷操作。
- `resource.list`：可加载列表，支持 loading/error/empty。
- `native.container`：暂时承载官方复杂页面，不复制其业务逻辑。

## 6. UI/UX 规则

- 交互优先于装饰：按钮使用图标或图标加短文本，避免把普通动作做成大块说明卡。
- 所有可操作元素必须有 hover、focus、pressed、disabled 状态。
- 键盘导航顺序与视觉顺序一致；Tab 不进入隐藏或 disabled 卡片。
- 触控命中区域不得因 DPI 或文本换行变得不可用。
- 文案、图标、状态颜色和动作不能只靠颜色表达。
- 页面切换、折叠和搜索不得重建业务对象；文本、图标和纹理应复用缓存。
- 动画可关闭；关闭后不改变布局、输入和状态结果。
- 旧 UI 和新 UI 的差异只在 presentation，不允许一边修复业务状态、一边保留另一份状态。

## 7. 持久化

持久化以 card ID 为键，建议配置前缀：

```text
qm_ui_card_<id>_visible
qm_ui_card_<id>_collapsed
qm_ui_card_<id>_order
qm_ui_scale
qm_ui_theme
qm_ui_animation
```

实际落地时需要配置迁移版本和白名单。未知 card ID、非法数值、越界 order 必须忽略并回退 descriptor 默认值；不能阻塞启动，也不能覆盖新配置。

## 8. 资源生命周期

资源页必须经过 cache/provider 边界：

1. `UNLOADED`：没有可用资源。
2. `LOADING`：同 generation 只允许一个请求。
3. `READY`：允许复用，不重复加载。
4. `FAILED`：保留错误摘要，使用可见 fallback。
5. generation 变化后，旧结果不得覆盖新页面/新玩家/新服务器状态。

resize、DPI、页面切换和 shutdown 必须释放或失效临时资源。worker 不得直接触碰 UI 或 graphics 对象。

## 9. 验证矩阵

最低验证矩阵：

- 页面：首次打开、重复打开、搜索、空结果、恢复默认。
- 偏好：隐藏、排序、折叠、非法值、未知 ID、迁移。
- 输入：键盘导航、触控、IME、焦点切换、输入冲突。
- 显示：DPI、resize、主题、动画关闭、字体/图标加载失败。
- 生命周期：connect、disconnect、map change、demo seek、spectator、shutdown。
- 性能：缓存命中、首次加载、失败回退、同场景 A/B、1% low。

## 10. 完成标准与实施顺序

### 完成标准

- registry、UI model、presentation、resource provider 有明确 owner。
- 至少一个官方卡片和一个 Qm 卡片走同一 presentation contract。
- legacy/new UI 共用 model，且有 UI smoke。
- 资源首次加载、缓存命中、失败回退、generation 和 resize 有测试。
- 验证清单中的未执行项不得标记为已通过。

### 实施顺序

1. 冻结 descriptor/page/schema，补 version 和持久化迁移接口。
2. 实现 `CardUiModel` 的生命周期 adapter，避免直接把引用型 UI 对象塞进 `CQmRuntime` component。
3. 接入 `settings.toggle` 和 `status.panel` 两个 contract。
4. 接入一个官方设置卡片和一个 Qm 卡片，完成 legacy/new UI smoke。
5. 将 `CResourcePageCache` 接到实际 graphics/storage provider。
6. 补输入、DPI、主题、动画和真实生命周期验证。
7. 通过后再继续下一批功能迁移。

## 11. 当前实现映射

| 规格部分 | 当前实现 | 状态 |
| --- | --- | --- |
| descriptor / registry | `src/game/client/ui/card_registry.*` | 已实现并测试 |
| page/column/order | `src/game/client/ui/card_order_model.*` | 已实现并测试 |
| deck projection | `src/game/client/ui/card_deck_projection.*` | 已实现并测试 |
| presentation state/layout/action | `src/game/client/ui/card_presentation.*` | 已实现并测试 |
| resource lifecycle | `src/game/client/ui/resource_page_cache.*` | 生命周期底座已实现 |
| official UI adapter | menus/List/Cards，共享官方 HUD 与 Qm 诊断配置 | 已接入，真实交互待验 |
| graphics/storage resource provider | atlas 与官方六类 Assets 的实际 loader/upload | 已接入，真实画面与 A/B 待验 |
