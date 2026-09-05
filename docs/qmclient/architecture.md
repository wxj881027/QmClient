# QmClient 20.0 重构架构总览

本文件记录重构架构的总原则。统一 UI 与卡片模型的详细设计见 [`ui-architecture.md`](ui-architecture.md)。
更适合快速接手的白话版总览见 [`review-notes.md`](review-notes.md)。
重构初期必须优先判断和收口的事项见 [`early-critical-decisions.md`](early-critical-decisions.md)。

## 当前架构方向

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

图形事件 listener 目前采用“每个 graphics source 注册一次”的策略。listener 通过可失效 token 绑定 source 生命周期，runtime shutdown 或切换 source 时先使 token 失效；回调再以当前 diagnostics session generation 作为写入闸门。这样避免按 session 重复注册，也避免旧 graphics source 的迟到事件污染新 session。若未来需要真正移除任意 listener，再单独设计带句柄的官方接口，不在 feature 层私自维护函数指针。

旧 QmClient/TClient 代码只作为功能盘点和行为对照来源。新实现是按功能重新设计、重构和重实现，不要求复制旧代码结构，也不得依赖 TClient 字段。

配置兼容层也有明确边界：主配置沿用官方未知命令保留机制，autoexec 只让 Qm 迁移器识别白名单旧键，不把普通未知命令复制回主配置；命令行和 `-f` 外部配置在同一次启动迁移收口前参与旧键捕获。这样既不丢旧配置，又不会改变 autoexec 的独立语义。
