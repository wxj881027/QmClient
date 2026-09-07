# QmClient 20.0 重构架构总览

本文件记录重构架构的方向和已经定死的决策。规则硬约束见根目录 `AGENTS.md`；功能清单和波次见 [`feature-ledger.md`](feature-ledger.md)；重构初期关键决策记录见 [`early-critical-decisions.md`](early-critical-decisions.md)。

## 分层

```text
1. 底层引擎/平台：窗口、输入、图形后端、配置加载（SDL3 等平台细节只在这里）
2. 组合根 CQmRuntime：接线、生命周期分发、渲染槽位、telemetry；不做万能管理器
3. 功能层：每个 feature 自己拥有状态、逻辑、测试
4. UI / 渲染层：页面卡片（怎么摆）与 presentation（怎么画）
```

```text
DDNet 20.0 官方基线
        ↓
最小官方 adapter / hook
        ↓
CQmRuntime 组合根
        ↓
独立 feature model / policy
        ↓
统一 UI 卡片模型或游戏内 presentation
```

统一卡片模型属于整个客户端，而不是 Qm 专属设施。官方功能通过官方 adapter 接入，Qm、重构后的 TC 和 BC 功能通过各自 feature 接入；所有功能可以共用页面、搜索、布局、动画和用户自定义机制，但业务所有权不混淆。

渲染也要分层：底层后端只负责提交图像，Qm 的 feature 只准备状态，`CQmRuntime` 只负责把状态送进合适的渲染槽位，不要把所有视觉功能都堆进同一个入口。当前最小槽位契约是 `WORLD_BACKGROUND`、`ENTITY_UNDERLAY`、`ENTITY_OVERLAY`、`HUD_OVERLAY`、`MENU_OVERLAY`；只有已有真实功能需要时才接入新槽位。

图形事件 listener 目前采用"每个 graphics source 注册一次"的策略。listener 通过可失效 token 绑定 source 生命周期，runtime shutdown 或切换 source 时先使 token 失效；回调再以当前 diagnostics session generation 作为写入闸门。这样避免按 session 重复注册，也避免旧 graphics source 的迟到事件污染新 session。若未来需要真正移除任意 listener，再单独设计带句柄的官方接口，不在 feature 层私自维护函数指针。

门禁脚本 `qmclient_scripts/check_qmclient_boundary.py` 只负责两件事：扫描约定范围内的源码，拦截 TClient 聚合器依赖；检查 `features/*/*_logic.*` 的 include 是否只使用 STL、`base/` 工具头和同目录头。它不解析台账、model ID、配置键、文件命名、测试覆盖、行为等价性或性能，也不替代人工 review、C++ 测试和 runtime smoke；不做 Git hook 或 CI 强制。

旧 QmClient/TClient 代码只作为功能盘点和行为对照来源。新实现是按功能重新设计、重构和重实现，不要求复制旧代码结构，也不得依赖 TClient 字段。

重构是否合格，不只看功能是否能运行：必须保留 Qm 用户可见行为，并让状态 owner、依赖层数、热路径成本或可测试性至少有明确收益。若新方案比旧实现更复杂、更慢，或只是把旧聚合器换个名字搬过来，就不能标记完成；差异、代价和不搬运部分写入功能台账。

配置兼容层也有明确边界：主配置沿用官方未知命令保留机制，autoexec 只让 Qm 迁移器识别白名单旧键，普通未知命令静默忽略且不复制回主配置；命令行和 `-f` 外部配置在同一次启动迁移收口前参与旧键捕获。这样既不丢旧配置，又不会改变 autoexec 的独立语义。三阶段迁移的完整决策见 `early-critical-decisions.md` 第 7 节。

## 已定死的决策

- `CQmRuntime` 是"总接线板"，不是"第二个 TClient"：只做组装、生命周期分发、固定渲染槽位和 Qm 级诊断接线；不拥有复杂业务状态、不做 service locator、不承载 UI 业务或 feature 间协调器。
- 状态 owner：与玩法/可见行为直接相关的状态归 feature；把官方状态转成可消费格式的归 adapter；只为绘制服务、可随帧重建的归 render frame / presentation cache；页面开关、排序、折叠、搜索归 UI 元数据。
- UI 基础设施只负责页面、卡片、搜索、排序、折叠、主题、动画开关、导航；不直接存 feature 业务状态；新旧 UI 共享同一批 feature model；card descriptor 是"展示描述"，不是"业务对象"。
- 平台切口：SDL3、OpenGL、Vulkan、IME、窗口事件、输入设备属于平台层。SDL3 是"地基材料"，Qm 功能不应直接知道 SDL3 细节；图形诊断属于底层/平台适配，不属于某个 feature。
- render slot 最小集合已固定（见上），只有真实功能需要时才增加；`ENTITY_OVERLAY` 当前由 player indicator 使用。
- 官方 backend 文件内允许必要的平行诊断实现，不为"合并方便"牺牲更合理的 hook 形状（2026-09-06 决策，见 `upstream-patches.md`）；2026-09-04 的诊断范围冻结继续有效。

## 分工举例

- 功能层算出"谁该显示"；UI 层决定"卡片怎么排"；渲染层负责"画成什么样"；底层只负责"把图像送到屏幕上"。
- 字体和图标资源要缓存复用，不在每次打开页面时重建；图标走构建期 atlas + 稳定 ID（Phosphor Bold）。

## 不优先做的事

先做漂亮的新 UI 外观、引入大 UI toolkit、一次性迁移所有功能、先造大而全的 feature framework、追求 SDL3 全量迁移——这些都不是当前优先级。

## 给后续 AI 的提醒

如果看到代码里开始出现"什么都能塞进去"的趋势、功能直接读底层私有字段、UI 直接改业务状态——先停，先收边界，再写代码。
