# QmClient 重构初期关键决策清单

日期：2026-09-05

这份文档只讲初期最关键、最难判断、最容易一步错步步错的东西。  
目标不是把所有细节一次写完，而是先把最该定死的边界定死。

## 0. 当前进度快照

### 已经落地并验证过的底座

- `CQmRuntime` 组合根已经存在。
- 诊断落盘、事件监听、窗口/帧/特性计时已经接上。
- `CQmUiModel` 已经有注册、冻结、页面查询的骨架。
- `CQmConfigMigration` 已经有版本闸门和首批旧配置迁移。
- `QmPlayerIndicator` 已经有独立 feature 切片。
- 旧版设置页切片已经接到新结构里。

### 已经做过的验证

- `run_cxx_tests`
- `py -3 qmclient_scripts/check_qmclient_boundary.py`
- `py -3 qmclient_scripts/check_qmclient_runtime_smoke.py`

### 当前还在收口的点

- render slot 最小集合已固定，仍需要由真实功能继续验证是否要增加槽位。
- `CQmRuntime` 的职责边界还要继续压窄，防止膨胀。
- `IGameClient` 的扩张要止住。
- UI descriptor 最小契约还要继续补齐。
- SDL3 / 平台层切口要在后续切片里继续确认。

## 1. 现在就能定死的东西

### 1.1 `CQmRuntime` 的边界

现在就可以定死：

- `CQmRuntime` 只负责组装、生命周期分发、固定渲染槽位分发、Qm 级诊断接线。
- 不拥有复杂业务状态。
- 不做万能 service locator。
- 不直接承载 UI 业务逻辑。
- 不直接承载 feature 之间的业务协调器。

一句话：`CQmRuntime` 是“总接线板”，不是“第二个 TClient”。

### 1.2 新状态的 owner 规则

现在就可以定死：

- 和玩法/可见行为直接相关的状态，归各自 feature。
- 只是把官方状态转成 feature 可消费格式的，归 adapter。
- 只为绘制服务、可随帧重建的，归 render frame / presentation cache。
- 页面开关、排序、折叠、搜索等，归 UI 元数据，不归 feature 业务状态。

一句话：先定“谁拥有状态”，再写代码。

### 1.3 UI 基础设施的职责

现在就可以定死：

- UI 基础设施只负责页面、卡片、搜索、排序、折叠、主题、动画开关、导航。
- UI 不直接存 feature 的业务状态。
- 新旧 UI 共享同一批 feature model。
- card descriptor 是“展示描述”，不是“业务对象”。

### 1.4 平台层和功能层的切口

现在就可以定死：

- SDL3、OpenGL、Vulkan、IME、窗口事件、输入设备属于平台层。
- Qm 功能只读平台层暴露的稳定能力，不直接吃平台实现细节。
- 图形诊断属于底层/平台适配，不属于某个 feature 的业务逻辑。

## 2. 现在不能只靠拍脑袋定死的东西

这些必须靠后续真实切片验证。

### 2.1 render slot 的最终集合

现在能确认“必须有分层 slot”，但还不能拍板最终全集。  
因为后续至少会遇到：

- 背景层能力
- 实体前后层能力
- HUD/overlay
- 菜单 / 新 UI
- 调试层

当前代码已固定最小集合：

- `WORLD_BACKGROUND`
- `ENTITY_UNDERLAY`
- `ENTITY_OVERLAY`
- `HUD_OVERLAY`
- `MENU_OVERLAY`

当前只接入已有 player indicator 所需的 `ENTITY_OVERLAY`。其他槽位先作为稳定契约保留，后续用真实功能验证是否还要继续拆分。

### 2.2 通用 feature 生命周期接口

现在能确定“不能乱做万能接口”，但不能一次把所有生命周期都设计完。  
应当按真实功能需要增加，而不是先做一个大而全接口。

### 2.3 UI descriptor 的最终字段全集

现在能确定至少要有：

- `id`
- `owner`
- `page`
- `title_key`
- `icon_id`
- `order`
- `search_keywords`
- `visibility`

但 `actions`、`renderer`、`category`、`version` 等字段，最好随真实卡片迁移再收敛。

### 2.4 诊断指标的最终口径

现在能确定要记录：

- update/render/frame
- feature timing
- 1% low / p95 / p99
- 内存拆分
- backend 事件

但具体采样窗口、统计周期、UI 细分指标口径，要靠后续 A/B 数据再收敛。

## 3. 我现在能直接做掉的事

这些是可以立即推进、且风险可控的。

### 已经能做

- 修底层时序问题，比如配置迁移窗口这种会影响后续所有功能的点。
- 写清边界文档，避免后续 AI 再把 runtime、UI、feature 混起来。
- 给现有底座做 review，提前抓“会放大未来成本”的问题。

### 接下来还能继续做

- 给 `CQmRuntime` 写更明确的职责清单和禁止事项。
- 给状态 owner 写成规则表和例子。
- 把 render slot 提成明确枚举和说明，不靠口头约定。
- 给 UI model 补“当前必须字段”和“暂不引入字段”的说明。
- 选 1 到 2 个真实功能切片，验证这套边界是不是真的站得住。

## 4. 现在不该优先做的事

- 先做漂亮的新 UI 外观。
- 先引入大的 UI toolkit。
- 先把所有功能一股脑迁过来。
- 先做一个很大的通用 feature framework。
- 先追求 SDL3 全量迁移。

这些都不是初期第一优先级。

## 5. 初期真正的工作顺序

1. 先定 `CQmRuntime` 边界。
2. 再定状态 owner 规则。
3. 再定 render slot 最小集合。
4. 再定 UI descriptor 最小契约。
5. 用 1 到 2 个真实功能切片验证。
6. 验证通过后再批量迁移功能。

## 6. 给后续 AI 的执行提示

如果当前任务不是在解决上面 4 个核心判断之一，就先问一句：  
“这件事会不会让 runtime 变大、状态 owner 变乱、render slot 失控、UI 重新绑死业务？”

如果答案是“会”，先停，再收边界。

## 7. 配置迁移的实际边界

配置迁移分三个阶段处理：

1. 主配置 `settings_ddnet.cfg`：保留官方未知命令写回行为，同时捕获白名单中的旧 `tc_*` 键。
2. `autoexec_client.cfg` / `autoexec.cfg`：只捕获白名单旧键；普通未知命令静默忽略且不写回主配置，避免每次保存设置时把 autoexec 内容复制进去。
3. 命令行和 `-f` 外部配置：使用命令行回调继续捕获白名单旧键，最后再统一执行迁移，因此命令行中的新键优先于旧键。

迁移器只在主配置、autoexec 和命令行解析全部结束后收口。命令行中的 `tc_*` 旧键支持迁移，但普通命令行参数、连接链接、demo/map 路径仍由官方客户端回调处理。迁移版本只有在所有捕获的旧命令都成功转换，或被当前键明确覆盖后才推进。
