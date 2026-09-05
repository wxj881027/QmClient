# QmClient 统一 UI 与卡片架构决策

日期：2026-09-03

## 1. 决策结论

卡片模型是整个客户端的 UI 基础设施，不是 Qm 功能专属模块。官方 DDNet 功能、Qm 功能、重构后的 TC 功能和确认纳入的 BC 功能，都必须能够通过统一卡片注册系统进入新 UI。

卡片系统负责页面组织、搜索、排序、显示/隐藏、折叠、布局、主题、动画、输入导航和用户自定义；业务状态和业务逻辑仍由各自的功能所有者负责。

```text
官方功能 / Qm 功能 / TC 重构功能 / BC 重构功能
                ↓
       Card Descriptor / Adapter
                ↓
          Unified Card Registry
                ↓
     旧 UI Presentation / 新 UI Presentation
```

## 2. 设计动机

旧 UI 的问题不只是视觉风格，而是页面结构、绘制代码、配置控件、文案、图标和业务逻辑大量硬编码在一起，导致页面难以扩展和重排，官方与 Qm 容易形成两套 UI 逻辑，用户也无法可靠地自定义卡片、搜索、DPI、键盘导航、触控和动画。

统一卡片模型把“功能是什么”和“功能如何显示”分开，并让官方功能也能被新 UI 组合，而不把官方业务逻辑复制到 Qm 模块。

## 3. 所有权边界

### 3.1 官方功能

官方代码继续拥有官方功能的状态和业务逻辑。Qm 只通过 `Official Settings Card Adapter` 或官方提供的稳定入口，把官方功能描述为卡片。

官方功能不应因为接入卡片系统而复制一份业务实现，也不应依赖 Qm 的 feature 或 Qm UI。

### 3.2 Qm、TC 和 BC 功能

重构后的功能由新的独立 feature 拥有状态、配置、生命周期、输入、渲染、存储和测试。旧 QmClient/TClient 代码只用于功能盘点、行为对照和发现边界，不能作为新架构的状态中心。

新代码禁止依赖 `m_TClient`、`CTClient`、`TClientComponent()` 和 `tclient.h`。

## 4. Card Descriptor

卡片描述应使用通用字段，不把 Qm 概念写死在基础模型中。建议包含：

```text
id
owner: upstream | qm | tc-rebuilt | bc-rebuilt
page
title_key
description_key
icon_id
category
order
visibility
search_keywords
settings
actions
renderer
version
```

`owner` 用于所有权、版本兼容、配置迁移、上游同步和审计，不用于限制卡片是否可以显示。

## 5. 官方卡片的两种接入方式

### 5.1 声明式卡片

适用于普通配置项，例如分辨率、VSync、MSAA、音量和控制设置。卡片绑定官方配置或官方设置接口，不复制官方业务逻辑。

### 5.2 原生内容卡片

适用于服务器浏览器、复杂列表、控制台等难以立即拆解的官方页面。卡片只提供稳定的容器和 renderer，内部继续调用官方实现。

先把复杂页面作为原生卡片纳入统一页面，再逐步把简单设置转换为声明式卡片，避免一次性大规模改写官方菜单。

## 6. 新旧 UI 模式

### 6.1 旧 UI

保持官方 UI 的主要交互和布局，Qm/重构功能集中在 Qm 设置页面中。旧 UI 不要求一开始把所有官方页面全部重写成卡片，但其功能覆盖必须登记在台账中。

### 6.2 新 UI

官方功能和 Qm/TC/BC 功能统一进入卡片页面。页面只声明卡片，不直接实现业务逻辑。搜索也跨越所有 owner，例如搜索“粒子”可以同时返回官方粒子设置和 Qm/TC 粒子设置。

两种模式必须共用 feature model、Card Descriptor、配置绑定、搜索关键字、可见性判断、输入动作和错误回退。区别只在 presentation，不允许维护两套业务状态。

## 7. 用户自定义

第一阶段支持：卡片显示/隐藏、卡片排序、折叠状态、页面模式、UI 缩放、主题和强调色、动画开关、信息密度。

配置必须使用稳定卡片 ID，不得使用数组下标：

```text
ui.card.video.visible = true
ui.card.qm.diagnostics.order = 40
ui.card.audio.collapsed = false
```

核心返回、应用和取消操作不能被用户删除。强依赖上下文的高级设置可以限制移动位置或隐藏。第一阶段不支持用户编写任意 UI。

## 8. 上游合并策略

不直接把官方菜单大面积改写成 Qm 风格。官方文件中的接入应集中在官方卡片 manifest 或注册点、必要的官方设置描述、少量稳定的 renderer/入口和配置读写绑定。

官方业务逻辑、网络逻辑和复杂页面仍保留在官方模块。每个官方触点必须登记到 `upstream-patches.md`，包含默认行为、冲突风险、重放方式和删除条件。

## 9. 性能和生命周期约束

- 页面打开时不得重复加载图标、字体或文本资源；
- 卡片注册表初始化后应冻结，搜索和布局查询不得重复构造业务状态；
- UI renderer 不得在游戏 update/render 热路径执行文件、网络或阻塞操作；
- 后台任务结果必须通过 feature model 回到 UI，worker 不得直接操作 UI 或 graphics 对象；
- 卡片卸载、切换页面、调整 DPI、关闭窗口时必须释放临时资源；
- 新 UI 的动画不得影响游戏线程的 1% low；
- 诊断功能由 `qm_diagnostics` 开关控制，关闭时不应启动 Qm diagnostics writer 或图形事件监听器。

## 10. 实施顺序

1. 建立通用 `CardDescriptor`、`CardRegistry` 和 `PageRegistry`；
2. 接入少量官方简单设置；
3. 接入 Qm 诊断和 Player Indicator；
4. 加入搜索、排序、隐藏、折叠和配置保存；
5. 把复杂官方页面以原生内容卡片接入；
6. 按功能重构重实现 TC/Qm/BC 能力；
7. 完善新旧 UI presentation、动画、图标和可访问性。

## 11. 完成标准

不能因为卡片已经注册就认为功能完成。每个官方或第三方功能都必须在 `feature-migration.tsv` 中登记功能行为和配置、卡片接入状态、新旧 UI 状态、搜索/输入/资源、生命周期和模式矩阵、测试与 smoke、性能预算、旧配置处理、当前 owner 和官方触点。

