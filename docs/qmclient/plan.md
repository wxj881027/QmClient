# QmClient 20.0 总体计划

本文件只保留当前阶段的顺序和出口；硬约束见 `AGENTS.md`，架构决策见 `architecture.md`，功能细节见 `feature-ledger.md`。

## 当前执行顺序（2026-09-07 用户目标）

本节是当前唯一的阶段状态来源。目标是完成阶段 0 风险清理及阶段 1–5 的基础与验证；阶段 6 在此前保持冻结。历史命令和测试数字只保存在 `verification-checklist.md`，不在本文件重复维护。

UI/UX 适配旧版 Qm 设计稿的执行顺序、保留/重写边界和 P0–P4 出口见 [`ui-ux-refactor-plan.md`](ui-ux-refactor-plan.md)。该文档不改变本表的阶段状态，只细化阶段 2–5 的 UI 工作包。

最终交付授权（2026-09-07，用户补充）：阶段 0–5 代码完成后，由 terra / high 子代理集中执行自动测试与只读验收。需要人工操作的验证项列明步骤、预期和未验证状态，不无限等待人工验收；即使用户尚未回来验证，也先提交并推送到远程 `upstream` 对应的 `wxj881027/QmClient`，使用同名分支 `codex/qmclient-20.0-refactor`，再交付最终报告和验收清单。不直接推主分支、不 force push；推送前核对远程分支状态、提交范围和敏感数据，`tmp/` 旧源码快照及测试数据不提交。人工待验授权不代表未实现代码可以标记完成。远程提交 SHA 与推送结果以最终交付报告的远端核对为准。

| 阶段 | 范围与出口 | 当前状态 |
| --- | --- | --- |
| 0 架构冻结与风险清理 | registry/model/cache 边界冻结；UI model 经受控生命周期接入；实际启动退出无访问冲突 | 自动验收通过：受控指针、clean Release、最终 runtime smoke 和两平台构建通过 |
| 1 全局卡片底层 | 稳定 presentation ID、输入优先级、默认可见/折叠、页面排序、availability、偏好持久化 schema、官方 adapter；注册冻结；UI 只消费 descriptor/snapshot；覆盖排序/搜索/隐藏/不可用/持久化 | 自动验收通过：卡片定义与页面声明分离、(page,card) 放置 order model（present 标记 + 跨页合并）、每页投影与新卡补位、统一搜索索引、拖拽事务/让位/自动滚动纯逻辑、schema v3 与 v2/v1 迁移、官方/Qm adapter、输入排序与保存失败保护已测试 |
| 2 UI/UX | 官方卡片与 Qm 卡片共用模型；旧/新 UI 都支持打开、搜索、隐藏/恢复和保存排序；键盘、触控、IME、DPI/resize、主题、动画开关 smoke | 已实现、人工待验：官方 HUD 与 Qm 诊断共用 List/Cards，设置视图接页面 tabs、真实拖拽（让位预览、跨页投放、自动滚动、自动保存）与统一搜索；键盘、搜索、布局操作、主题/动画和图标已接入；真实交互 smoke 未执行 |
| 3 资源加载 | 对照旧 Qm；manifest、异步 storage/provider、graphics 主线程接收、cache/generation、fallback、resize/DPI 和页面临时资源释放；首次/重复/失败/切图/resize smoke；同场景加载耗时及内存 A/B | 已实现、自动测试通过：设置图标与官方六类 Assets 页面均接 provider；有界后台解码、fallback、限额上传、LRU、失败提示、DPI/失效和 metrics 齐全，真实画面及性能 A/B 人工待验 |
| 4 输入与渲染调度 | 稳定注册/排序和输入优先级；关闭不构造输入、不执行 logic/render、不启动任务；逐 feature lifecycle checklist；重连/切图/demo/dummy/观战；热路径开销审查 | 已实现、自动验收通过：注册冻结、当前配置关闭门控、UI 输入优先级与生命周期矩阵齐全；真实 demo/dummy/观战待验 |
| 5 验证收口 | 固定清单：构建、单测、UI/runtime/真实联网/demo/观战 smoke、性能 A/B、可重复 line/branch coverage | 本轮自动验收完成：terra / high 构建、测试、runtime、coverage 和只读审查已收口；人工验收、性能 A/B 及未测平台明确保留，最终门控的 coverage delta 已注明 |
| 6 功能迁移 | 阶段 1–5 全部验证通过后，依低风险纯逻辑、HUD/视觉、输入/聊天、联网、外部依赖顺序逐功能迁移 | DEFER：不得提前开工 |

每一步按明确问题推进，代码与台账同步；已有三个切片保持原完成状态，其真实 UI/联网/demo/多人渲染证据单列于架构验证。不得以局部测试或文档条目替代阶段出口。

## 历史阶段（仅供旧引用定位）

1. **基线与门禁**
   - 固定 DDNet 20.0 基线，保持可构建、可启动。
   - 运行 boundary、构建、C++ 测试和 runtime smoke。
   - 维护官方触点、配置迁移和性能证据。

2. **架构验证切片**
   - 以 Qm 旧行为为主要对照，完成 `qm.speedrun_timer`、`qm.auto_team_lock`、`qm.player_indicator` 的行为收口；切片实现已完成，后续只补真实 UI/联网 smoke。
   - BC 可作为接近 DDNet 20.0 的同源参考，但 BC 仓库缺失不阻塞 Qm 迁移；有参考时记录差异，没有参考时明确证据缺口。
   - 验证 feature-owned state、纯 logic、feature adapter、render slot 和关闭路径。
   - 关闭路径必须不构造输入、不执行 logic、不进入对应 render。

3. **高频功能迁移**
   - 按功能逐项迁移 HUD、聊天、输入和视觉能力。
   - 每项先补台账，再按“Qm 行为对照 → 最小重构实现 → 测试 → 性能验证”推进；旧代码只作参考，不直接搬运。
   - 预测、协议、物理、demo 和服务端玩法默认保持不变。

4. **外部依赖与 UI**
   - 在取消、generation、权限、失败回退和隐私边界明确后，再处理媒体、HTTP、helper、语音和更新器。
   - 最后接入统一卡片 presentation，收口 legacy 设置页。

## 每个功能的出口

- 来源、行为不变量、状态 owner、配置/命令/资源和生命周期已登记。
- 纯逻辑有 focused tests，adapter 和 presentation 有构建或 smoke 验证。
- 默认关闭行为不变，热路径无未解释的额外开销。
- 与 Qm 旧行为、BC/TClient 对照差异已记录。
- BC/TClient 只是参考来源，不是 Qm 行为权威；BC 缺失时不阻塞完成，但必须记录未对照的范围。
- 官方触点、删除条件、验证证据和提交号已回填。

## 当前执行

当前优先级和未完成出口只维护在本文件的阶段表；构建、测试、smoke 和覆盖率证据只维护在 `verification-checklist.md`。不要在这里复制测试数量或历史快照。
