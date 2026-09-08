# QmClient 功能迁移台账

本文件是人和 AI 共用的**本地功能台账**：每个条目就是已选定功能的开工提示（来源、行为要点、实现注意点、实现入口、验证方式）。远程模块候选和板块归类见 [`refactor-feature-catalog.md`](refactor-feature-catalog.md)，不能把候选目录直接当作本地完成状态。规则与硬约束见根目录 `AGENTS.md`，本文件只登记已进入本地核对或实现流程的功能事实和迁移提示。

## 来源与对照优先级

- 本客户端是 Qm 客户端：所有条目统一使用 `qm.` 命名（含源自 TClient/BC 的能力），条目 ID 与代码中 `SQmFeatureModel` 的 model ID 对齐（snake_case）；分节标题保留来源分组，便于定位对照代码。
- Qm 是主线：用户可见行为、重构取舍和完成判定以 Qm 旧实现及当前 Qm 语义为准。TClient 与 BC 同源（Qm 与 BC 都基于 TClient 开发）；BC 代码已跟进官方 20.0，TClient 没有，因此需要参考 TC/BC 时优先使用 BC 源码，TClient 代码只作历史补充。
- BC 仓库可复制到本地用于引用，并在对应条目的"来源"登记路径；BC 不是必需来源，缺失时不阻塞 Qm 迁移，但必须记录行为对照缺口。BC 能力属于次要参考，不替代 Qm 需求。
- 旧 `tc_*` 配置名不作为新配置名；旧键只经配置迁移器白名单映射为 `qm_` 新键（见 `qm.config_migration`）。

- 状态：`TODO` 未开始（允许只有一行要点）｜`DOING` 进行中｜`DONE` 完成（行为对照＋测试＋落盘证据齐全）｜`DEFER` 明确推迟（写明原因）。旧 TSV 的 `INVENTORY`/`PARTIAL` 分别对应 `TODO`/`DOING`。
- 开工前：先定位来源文件/符号并回填"来源"，把条目补全为完整提示块——必须包含**重构说明**：新方案、相比旧实现的优势、代价或缺点、旧代码不搬部分及原因。旧代码因底层架构重构不能直接搬运，只作行为对照；重构向更简洁、更高性能方向走，新实现更复杂或更慢视为重构失败。有落盘数据的功能必须声明持久化文件（路径、格式）和旧数据兼容方式（迁移或明确放弃）；有 UI 文案的功能登记 i18n key；渲染或每 tick 路径上的功能给出开销预算。
- 完成时：回填证据和提交号，状态改 `DONE` 前必须有 Qm 旧行为对照、测试和证据；BC 参考可补充差异，但不是完成的前置条件。
- 同步规则：台账条目与对应功能代码在同一提交中更新，不允许代码先行、台账滞后。

## 迁移波次

1. **第一波（架构验证切片）**：`qm.speedrun_timer`、`qm.auto_team_lock`、`qm.player_indicator` 的切片实现标记 `DONE`；纯 logic、构建和边界测试已有。真实 UI/联网 smoke 属于阶段 2 架构验证清单，不回写为功能未实现。
2. **第二波（高频日常）**：Qm HUD/聊天/视觉类与 TClient 视觉/状态条类。
3. **第三波（外部依赖）**：媒体、语音、歌词、provider、更新器、Axiom；同时启动 `qm.telemetry` 用户反馈遥测。
4. **第四波（UI 卡片化）**：已迁功能接入统一卡片模型，收口 legacy 设置页。

## Qm 生命周期与账号

- [ ] `qm.playtime` 服务器时间 / playtime / 本地统计 —— TODO
- [ ] `qm.identity` Qm / Arg 玩家识别 —— TODO
- [ ] `qm.dev_presence` 开发者 presence —— TODO
- [ ] `qm.axiom` Axiom 登录与成绩查询 —— TODO

## Qm 基础设施

### `qm.diagnostics` 性能诊断 —— DOING

- 来源：重构新增能力（无旧版一一对应）。
- 要点：session/report 自动落盘、graphics 事件入口、feature 计时、ring buffer、丢弃统计、保留策略。
- 注意：非阻塞写入、日志失败不阻断启动、不改默认渲染；范围冻结（2026-09-04，见 `upstream-patches.md`）。
- 验证：`run_cxx_tests`、`check_qmclient_runtime_smoke.py`、backend 字段与 `.tmp` 清理检查。
- 证据：`performance.md`。剩余 gap：Vulkan 成功路径 runtime、GLES 编译（缺 GLES3 头）、Metal。

### `qm.telemetry` 用户反馈遥测 —— TODO（第三波启动）

- 要点：opt-in 反馈通道，`qm_telemetry` 默认 `0`；匿名聚合，不含聊天内容、个人信息和本机路径。
- 注意：载荷 schema 先评审再实现；上传失败静默、有限重试；设置页可见隐私说明；可随时停用并清理本地数据。
- 验证：默认关闭时零网络行为；开启时载荷 schema 测试与失败回退 smoke。

### `qm.config_migration` 旧配置迁移 —— DOING

- 来源：QmClient/TClient 旧配置键（白名单旧键捕获）。
- 要点：主配置 / autoexec / 命令行三阶段捕获；版本闸门 `qm_config_migration_version`；新键优先；详细边界见 `early-critical-decisions.md` 第 7 节。
- 注意：无法解析或越界的旧命令按官方规则保留，不静默丢弃；不把 autoexec 内容复制回主配置。
- 验证：`run_cxx_tests`。gap：真实旧配置语料库的 fixture 测试；迁移白名单当前只捕获旧 `tc_*` 键，旧 Qm 自身的 `qm_*` 键是否有语义变化需要迁移，待盘点 `dyl_dev` 后确认。

### `qm.ui` 设置页与卡片 UI —— DOING

- 来源：旧 Qm 卡片行为对照见 `qm-legacy-card-audit.md`；全局底座见 `src/game/client/ui/`，组合根见 `core/qm_runtime.cpp`；目标模型见 `ui-ux-refactor-plan.md`（卡片定义与页面声明分离，主体按新标准重构，dyl_dev 旧代码只作行为对照）。BC 对照仍有缺口，不阻塞底座工作。
- 重构说明：保留稳定 descriptor 和独立 model，不搬旧聚合器及其字段依赖。本轮把 descriptor 去页面所有权：`SCardPage` 只声明 card ID 列表，`SCardOrderModel` 放置身份改为 `(page, card)`，present 标记防止重启 merge 把移出的卡片补回默认页，跨页移动在目标页已有放置时合并。相比旧实现的收益：一张卡片可被多页声明且只有一份实现与一份业务状态；代价：偏好与投影都要按放置寻址，旧全局偏好需迁移。旧值的成员接入访问冲突根因仍未确认，不宣称已定位。
- 行为不变量：默认官方设置和游戏行为不变；reset、切图、重连不清除 UI 偏好；shutdown 后 UI model 不可访问；状态导入包含未知或重复 ID 时整批拒绝，不污染已有状态。搜索与页面列表使用同一可见性、availability 和用户排序规则。
- 行为变更登记：旧版隐藏是卡片全局状态（隐藏后从所有页面消失且不参与搜索）；新版隐藏是 `(page, card)` 放置级状态，只影响本页投影，其他声明页与全局搜索仍可见（`ui-ux-refactor-plan.md` 2.10 既定方向）。理由：多页声明后"全局隐藏"语义不再成立。用户迁移：升级后此前隐藏的卡片会在其他声明页恢复显示，需按页重新隐藏。
- 状态 owner：descriptor/页面声明由冻结的 registry 所有，feature model 仍归 feature，放置（列/序/present）由 `CCardOrderModel` 所有，`(page, card)` 偏好由 `CCardUiModel` 所有，页面级视图偏好由 `SCardViewPreferences` 所有。`SCardModelSnapshot` 拷贝 enabled/availability/偏好，仅保留冻结 descriptor 的只读指针。新增 Phosphor 图标资源与 UI 文案见下方本批条目；没有新增配置键或命令，构建 manifest 触点已登记。
- 持久化 schema v3：用户 storage 的 `qmclient/ui-preferences.json` 保存 `version`、`placements`（card/page/column/order/present/visible/collapsed 的按放置完整状态）和 `view`（mode/light/animations）；同一 order model 是排序的唯一事实源，偏好只跟随放置、不构成第二排序来源。迁移：v2 全局 `cards` 偏好映射到其声明的每个页面放置并直接保留 `placements`；v1 card order 继续兼容读取。上限 1 MiB / 4096 条，order 为非负有符号 32 位整数；检查类型、重复字段/ID、版本、嵌入 NUL、文件读写/flush/close/rename 错误。完整验证后才替换模型，坏文件保留已有偏好；缺失文件用默认值，未修改不写文件，同目录临时文件替换失败保持 dirty。启动加载、布局事务后自动安排保存（连续操作合并写入，失败保留旧文件重试）；旧 Qm stable ID 到新卡片的完整迁移尚未完成。
- 统一搜索：`card_search_logic`（纯逻辑：分词、CJK 子串匹配、字段优先级、多分词跨字段全命中）+ `CCardSearchIndex`（registry + 可选 provider 聚合卡片标题/描述/别名/控件文本），与渲染共用本地化文本源，不索引任意用户数据；搜索结果排序不写回分类布局。
- 拖拽：`card_drag_logic`（纯逻辑：armed→dragging→commit/cancel 状态机、阈值、页 tab 悬停跨页预览、可见序 drop order、单视觉列提交映射、让位动画轨道、边缘自动滚动）；松手才提交事务，预览不改已提交布局；视图按帧输入同一份 geometry snapshot 解析目标，避免让位动画造成目标抖动。
- 官方 adapter：`presentation/qm_card_settings_adapter.*` 实现 `ICardSettingsAdapter` 的 snapshot/动作接口；官方 `ddnet.hud` 与四个当前 Qm feature 卡片使用 `toggle` presentation，分别绑定 `ClShowhud`、`QmDiagnostics`、`QmPlayerIndicator`、`QmAutoTeamLock` 和 `QmSpeedrunTimer`，从 `DefaultConfig` 读取默认值；非法值、未知 descriptor/owner/presentation 均拒绝，读取不修改配置。官方旧 checkbox 未修改，Qm 设置页已接列表/卡片两种展示；真实 UI smoke 未运行，不把接口测试当作 UI smoke。
- 旧布局对照：本轮已对照旧快照 `QmCardOrderModel.cpp`、`SettingsCardDeckLogic.cpp` 及其测试。新实现把旧"页面绑定卡片列表"改为声明式投影（`card_deck_projection`：按页过滤 present/可见/availability，OrderModel 缺失的声明卡片按默认位置补位，隐藏卡片不回流默认页）；旧跨页移动语义保留为"源页移出 + 目标页合并"。不搬旧字符串所有权、聚合器或配置依赖。
- 验证入口：`qmclient_card_registry_test.cpp`、`qmclient_card_order_model_test.cpp`、`qmclient_card_deck_projection_test.cpp`、`qmclient_card_preferences_test.cpp`、`qmclient_card_settings_adapter_test.cpp`、`qmclient_card_search_logic_test.cpp`、`qmclient_card_drag_logic_test.cpp`、`qmclient_ui_foundation_test.cpp`；runtime smoke 验证实际初始化/释放。2026-09-08 本批实跑：Release 构建 game-client + testrunner 通过，testrunner 全量 500 项通过（含卡片/搜索/拖拽/资源 87 项），`check_qmclient_boundary.py` 通过（51 源文件、3 纯逻辑文件），`qmclient_i18n.py validate` 通过（补登记 Open page / Reset page / Toggle 三条文案），`check_qmclient_runtime_smoke.py` 通过。卡片视图用冻结 input dispatch 明确"激活搜索框先处理文本/IME，Tab 再转卡片导航，其余卡片快捷键后处理"。gap：多页拖拽、跨页投放与搜索导航的真实鼠标/触控交互未验证，不因模型或序列化测试通过而判定完成。
- 要点：legacy 模式在官方设置页末尾追加 Qm 页；新 UI 卡片模型见 `ui-ux-refactor-plan.md`；两套模式共用 feature model。
- 注意：card ID / 排序 / 折叠 / 搜索是兼容接口；卡片注册冻结；不在卡片中散落 SVG 或每帧重建文本。
- 验证：`run_cxx_tests`；UI 打开/切换/搜索性能待 diagnostics 指标补齐后补 A/B 证据。

### `qm.ui` 资源加载与调度风险清理（2026-09-07，本批待验证）

- 来源：直接对照 `tmp/qm-dyl-dev-6d37ad1c/src/game/client/components/qmclient/settings_resource_preview.cpp` 的后台解码、上传队列、上传成功公布纹理及失败占位行为，以及 `settings_resource_jobs.cpp` 的 generation 校验；不搬旧聚合器或多层预算对象。
- 本批实现：loader 使用官方 `IEngine` job pool / `IStorage` / PNG decoder；结果仅在 `STATE_DONE` 后由主线程读取，每次 `Poll` 默认最多上传一张，全部上传成功才进入 READY。sink 按 page/path 查找已提交纹理，部分上传失败或 invalidate 会释放当前页纹理。
- 生命周期：单独的 request ID 区分同一 map generation 内的 resize/retry；协作取消在文件分块读取与各解码之间检查，不将 `IJob::STATE_ABORTED` 当作 worker 完成。worker 不引用 loader/UI/graphics；storage 生命周期沿用官方 `CClient::Run` 先 `ShutdownJobs` 后 `GameClient::OnShutdown` 的顺序。
- 资源预算：单文件压缩大小 16 MiB、单图 4M 像素、单页保留解码结果 64 MiB、最多 16 个资源/2 个 pending job、每资源最多 8 条 fallback、最多 8192 个 manifest；Assets 页面最多保留 64 个预览纹理并按最近帧淘汰。解码器临时内存另外存在，不能把 64 MiB 声称为整个进程峰值。拒绝绝对路径、路径穿越、重复路径和空 manifest；大图限制是当前 provider 的明确限制，不等于官方资源文件格式改变。
- 删除错误入口：撤除 runtime 启动/reset/map/resize 时的 `data/extras.png` 请求和未消费纹理 owner。它不对应实际 UI 使用，不再作为资源页完成或 runtime 成功证据。现仅由实际 UI 请求资源，真实画面和性能 A/B 仍待验。
- 关闭门控：auto team lock / speedrun timer 的 model enabled 同时要求 availability；不可用时不进入 runtime 的输入构造与逻辑分支。auto lock 仅在 availability 下降沿清理；timer 自动关闭后立即清理 model，当前帧不继续进入 render。这遵循阶段 4 的关闭约束；不承诺关闭后保留五秒过期提示。
- 官方 Assets provider：六类目录在后台扫描并发布不可变快照；可见项才请求缩略图。entities 按官方 mod 顺序尝试目录资源并回退 flat PNG，其他类型兼容 flat/子目录布局；候选存在但损坏时继续下一条。页面切换取消扫描/未完成预览，已完成缩略图保留在有界 LRU；resize/map/reset 递增 generation，旧扫描和旧缩略图结果不能覆盖新状态。失败显示官方 `Error` 文案且不逐帧重试，显式刷新才重开。
- 测试代码：`qmclient_ui_foundation_test.cpp` 增加真实 job pool + 临时 storage + 生成 PNG fixture，覆盖上传预算、cache 命中、缺失/损坏 fallback、缩略图尺寸、读取/上传失败、同 generation 取消重开、部分上传失效、目录 generation、失败不重试和 shutdown。当前尚未执行，最终统一交 terra / high。
- 卡片排序修正：相对移动由 `CCardUiModel::MoveCardRelative` 统一计算；跨页/跨列向下移动插入目标之后，同组移动先扣除源项位置。修复两个视图共用的边界错误，并增加同页/跨页/跨列与非法目标测试；registry 拒绝非法 owner 枚举。
- 真实首个资源消费者：设置页列表/卡片按钮现在从 `CSettingsIconResources` 请求 Phosphor Bold atlas 并绘制上传结果，tooltip 复用官方 `CTooltips`。页面未打开不发 job，已完成 atlas 重开命中；关闭时取消临时加载，frame end 只回收迟到结果；resize/DPI/map/reset 失效后仅在页面重新绘制时请求。加载失败退回可操作的文字按钮，不影响游戏。
- 可搬运资源及理由：旧快照的 `data/qmclient/icons/qm_icons_bold_{1,2,4}x.{png,json}` 与 `LICENSE_PHOSPHOR.txt` 原样复用；它们是已生成、独立于旧聚合器的 MIT 图形数据，不含旧代码依赖。新 `generate_qm_icon_manifest.py` 只用 Python 标准库生成 C++ manifest 并验证三倍率 ID/UV 一致；不搬旧多权重、MSDF 或 backend shader 改造。PNG 本身沿用旧生成物，本批未建立从 SVG 重新光栅化的工具链。
- 实际执行：`py -3 qmclient_scripts/generate_qm_icon_manifest.py --source data/qmclient/icons --output tmp/qm_icon_manifest.h` 成功，得到 17 个图标；当前最终测试结果统一记录于 `verification-checklist.md`。官方 Assets 六页也已接异步目录/预览 provider，玩家皮肤目录不属于本批六类资源页，未宣称迁移；真实 UI 和性能 A/B 仍待验。
- 输入优先级契约修正：descriptor 提供按 `input_priority` 降序、ID 稳定兜底的视图；Qm input dispatch 同样改为槽位优先、槽内较大 priority 先执行并在消费后立即停止。render/update 仍按较小 priority 先执行，避免无意改变已有绘制顺序；新增 registry/dispatch 回归测试。
- loader 指标：每页累计 load attempt、cache hit、同请求去重、取消、失败和上传资源数，记录最近后台加载耗时、最近/峰值解码保留字节。指标不在 worker 中写 UI，主线程合并后供 A/B 报告读取；当前仅覆盖解码 artifact 保留内存，不含 PNG 解码器临时分配、driver staging 或 GPU 显存，报告必须保留该口径。
- 审查修复：Assets 64 项 resident 同时限制 preview record/manifest/cache/metrics；淘汰、切页取消、DPI 替换和全局 invalidate 都注销对应元数据，request ID 保持单调以拒绝重新注册同名页面的旧 job。目录汇总指标仅代表仍在 resident 中的项，不再累计已淘汰项。偏好保存使用 `std::filesystem::rename` 的 error-code 接口，不走官方 Windows 删除目标后重试的 fallback；失败保留原文件和 dirty，已加目标被占用回归测试。

## Qm HUD 与聊天

- [ ] `qm.hud_notification` HUD notifications —— TODO
- [ ] `qm.chat_bubble` chat bubble —— TODO
- [ ] `qm.emoji` emoji —— TODO
- [ ] `qm.pie_menu` pie menu —— TODO
- [ ] `qm.input_overlay` 输入覆盖层 —— TODO
- [ ] `qm.ime` IME —— TODO
- [ ] `qm.dynamic_icons` 动态图标 —— TODO
- [ ] `qm.afk_presentation` AFK 展示 —— TODO（旧文件线索 `afk_presentation.h`，与动态图标的关系对照时确认）
- [ ] `qm.msg_filter` 消息过滤 —— TODO
- [ ] `qm.chat_interact` 聊天交互 —— TODO

## Qm 视觉

### `qm.player_indicator` 队友方向指示器 —— DONE（第一波切片）

- 来源：Qm 队友方向指示器样板（旧台账归属 QmClient；对照 `dyl_dev` 的 `tclient/player_indicator.*`）；BC 参考 `tmp/bc-bestclient`（BestProjectTeam/BestClient main `1132c6b` 的 `src/game/client/components/tclient/player_indicator.cpp`）。Qm 旧实现与 BC 行为基本同源。
- 行为不变量：
  - 仅在线、非 demo、非观战、race gamemode、有活跃本地角色且 `ZoomAllowed` 时渲染；`qm_player_indicator=0` 时零绘制。
  - `qm_player_indicator_team_only=1` 时本地必须已加入 DDRace 队伍；无论该开关如何，只渲染同一服务器队伍的活跃非观战玩家。冻结/队伍语义已收口（提交 `8a5475276`）。
  - `frozen_only`、hide_visible、variable distance、颜色、tee 渲染按旧 TClient 语义；unfreeze 判定为 `FreezeEnd > 0 || DeepFrozen` 且 predicted `IsInFreeze == 0`。
  - adapter 同时跳过 `m_aLocalIds[0]` 和 `m_aLocalIds[1]`，避免双客户端模式把另一端本地 dummy 当作普通队友；与旧 TClient/BC 的本地角色过滤语义一致。
- 重构：旧 TClient 聚合器状态迁为 feature-owned model；官方状态由 player indicator 自有 adapter 打包为窄快照（`SQmPlayerIndicatorFrame`），渲染只消费已准备状态。优势：无 TClient 字段依赖、状态边界显式，避免共享 game-state adapter 变成聚合器；代价：adapter 每帧构造固定大小 frame，feature 关闭时不进入该路径。
- 实现：`features/player_indicator/`（logic + adapter + feature 切片），官方状态经 `qm_player_indicator_adapter.*` 消费，`ENTITY_OVERLAY` 槽位渲染。
- 配置/资源：第一阶段迁移 14 个旧 `tc_*` 指示器键，旧键只作为配置输入兼容层；无新增资源。
- 验证：`qmclient_player_indicator_test.cpp`（含双本地 ID 回归）、boundary check、Release testrunner、runtime smoke。
- 后续验证：真实多人 race、demo、观战 smoke 和渲染截图/A-B 仍在阶段 2 验证清单中；不影响本切片的纯逻辑与构建完成状态。
  - `tc_warlist_indicator*` 的过滤与着色属于独立的 `qm.warlist`（TODO），不纳入本 feature 的实现范围。
  - BC 特有的 `OptimizerAllowRenderPos` 性能裁剪不属于 Qm 旧行为，保留为 BC 差异记录。

- [ ] `qm.weapon_laser` weapon/laser presentation —— TODO
- [ ] `qm.jelly` jelly —— TODO
- [ ] `qm.tee_hue` tee hue —— TODO
- [ ] `qm.collision_hitbox` collision hitbox —— TODO
- [ ] `qm.hammer_hit` hammer hit detection —— TODO
- [ ] `qm.focus_mode` focus mode —— TODO
- [ ] `qm.skin_queue` skin queue —— TODO
- [ ] `qm.dummy_mini` dummy mini view —— TODO
- [ ] `qm.streamer_privacy` streamer 隐私 —— TODO

## Qm 媒体与扩展

- [ ] `qm.i18n` Qm 翻译 —— 进行中（官方 source-key TOML、DeepSeek Python 翻译和官方 txt 生成链已建立；实际语言回填待执行）
- [ ] `qm.voice` 语音 —— TODO
- [ ] `qm.lyrics` 歌词 —— TODO
- [ ] `qm.provider_netease` 网易云 provider —— TODO
- [ ] `qm.provider_qishui` 汽水 provider —— TODO
- [ ] `qm.media_controls` system media controls —— TODO
- [ ] `qm.scripting` 脚本 —— TODO
- [ ] `qm.updater` 更新器与自动安装退出流程 —— TODO

## 视觉与状态（对照源：TClient，优先参考 BC 同名实现）

- [ ] `qm.background_particles` background particles —— TODO
- [ ] `qm.background_draw` background draw —— TODO
- [ ] `qm.moving_tiles` moving tiles —— TODO
- [ ] `qm.outlines` outlines —— TODO
- [ ] `qm.rainbow` rainbow —— TODO
- [ ] `qm.trails` trails —— TODO
- [ ] `qm.pet` pet —— TODO
- [ ] `qm.skin_profiles` skin profiles —— TODO
- [ ] `qm.status_bar` status bar —— TODO
- [ ] `qm.warlist` warlist —— TODO
- [ ] `qm.settings_page` TC 设置与配置展示 —— TODO（对照 `menus_tclient.cpp` / `config_variables_tclient.h`）

## 输入与辅助（对照源：TClient，优先参考 BC 同名实现）

### `qm.auto_team_lock` 自动队伍锁定 —— DONE（第一波切片）

- 来源：Qm 旧实现 `dyl_dev:src/game/client/gameclient.cpp` `CGameClient::UpdateAutoTeamLock()`；BC 参考 `tmp/bc-bestclient`（BestProjectTeam/BestClient main `1132c6b` 的 `src/game/client/gameclient.cpp:1512`、`src/engine/shared/config_variables_bestclient.h:129-130`）。Qm 旧实现与 BC 同源，仅配置前缀不同（`qm_` / `bc_`），本次以 Qm 旧行为为准。
- 行为不变量：
  - 仅 online 且当前 dummy 本地玩家有效时执行；离线、无效本地玩家、tick 不合法时清空状态。
  - `Team > TEAM_FLOCK && Team < TEAM_SUPER` 视为可锁定；进入或变更到可锁定队伍后延迟 `qm_auto_team_lock_delay` 发送一次 `/lock 1`。
  - 同一 dummy 已在可锁定队伍时不重复发；离开可锁定队伍会取消 pending；发送后同队不重发。
  - 主/dummy 两个槽位独立保存 last team / deadline / pending，与旧 `NUM_DUMMIES` 数组等价。
- 重构：logic 纯函数化（`SQmAutoTeamLockInput` 快照输入 / `SQmAutoTeamLockAction` 动作输出，不 include 官方头，可脱离游戏单测），状态 feature-owned。优势：无 TClient 字段、延时溢出防护显式化、可单测；代价：每 tick 构造一次小 struct 输入快照。当前迁移新增 `Disable()`：功能关闭时只清 pending/deadline、保留最后可见队伍，避免“关闭-重开”立即重发。
- 实现：`features/auto_team_lock/`（`CQmAutoTeamLockLogic` 纯逻辑 + 状态/action adapter + feature 类，logic 固定 2 个 dummy 槽位 `QM_AUTO_TEAM_LOCK_DUMMY_SLOT_COUNT`）；配置 `qm_auto_team_lock`（默认 0）、`qm_auto_team_lock_delay`（默认 5s，0–30）。
- 注意：只消费官方状态快照（`SQmAutoTeamLockInput`），不读 TClient 字段；禁用时不构造输入、不执行 logic、不发送网络命令。关闭期间无法逐帧记录队伍变化的重开边界见 gap。
- 验证：`qmclient_auto_team_lock_test.cpp`（含 per-dummy、离开取消、关闭-重开不重发、队伍变化后重发）；boundary check；Release testrunner；runtime smoke 已默认覆盖离线启动路径（见 `performance.md` 2026-09-05 记录）。
- 后续验证：真实服务器对 `/lock 1` 的接受、重复 lock 和队伍重进响应尚未 smoke，列入阶段 2 验证清单。
  - BC 原版没有 Qm 的延时溢出防护和禁用期 pending 收口；这是已记录的 Qm 行为改进，不阻塞完成。
  - 特征关闭期间队伍变化的真实用户感知纳入后续手动回归。

- [ ] `qm.bind_chat` bind chat —— TODO
- [ ] `qm.bind_wheel` bind wheel —— TODO
- [ ] `qm.repeat` repeat —— TODO
- [ ] `qm.air_rescue` Air Rescue —— TODO
- [ ] `qm.spec_id` Spec ID —— TODO
- [ ] `qm.emote_cycle` emote cycle —— TODO
- [ ] `qm.auto_reply` auto reply —— TODO
- [ ] `qm.red_packet` red packet claim —— TODO
- [ ] `qm.freeze_detect` freeze / waterfall detection —— TODO
- [ ] `qm.freeze_combo` freeze wakeup / combo —— TODO

## 玩法辅助（对照源：TClient，优先参考 BC 同名实现）

- [ ] `qm.gores_progress` Gores 距离场与进度 —— TODO
- [ ] `qm.weapon_cycle` 自动 weapon cycle —— TODO
- [ ] `qm.fast_practice` fast practice —— TODO
- [ ] `qm.antiping` antiping —— TODO
- [ ] `qm.ghost` ghost —— TODO
- [ ] `qm.remove_anti` remove anti —— TODO
- [ ] `qm.mod_weapon` mod weapon —— TODO
- [ ] `qm.dummy_pred` dummy / 预测相关辅助 —— TODO

## 地图与社区（对照源：TClient，优先参考 BC 同名实现）

- [ ] `qm.map_favorites` 地图收藏与分类缓存 —— TODO
- [ ] `qm.map_notes` 地图笔记 —— TODO
- [ ] `qm.map_history` map history —— TODO
- [ ] `qm.save_slots` 本地存档列表与 join hint —— TODO
- [ ] `qm.friend_alert` 好友上线/进入提醒 —— TODO
- [ ] `qm.communities` custom communities —— TODO（社区 URL、过滤配置待盘点）

## BC 参考能力（对照源：BC）

### `qm.speedrun_timer` Speedrun 计时器 —— DONE（第一波切片）

- 来源：Qm 旧实现 `dyl_dev:src/game/client/components/hud.cpp` `CHud::RenderSpeedrunTimer()` + `FormatSpeedrunTime()`；BC 参考 `tmp/bc-bestclient`（BestProjectTeam/BestClient main `1132c6b` 的 `src/game/client/components/hud.cpp:1116`、`src/engine/shared/config_variables_bestclient.h:276-282`）。倒计时、过期展示、自动停用逻辑与 BC 一致；Qm 额外保留 `qm_speedrun_timer_time` legacy MM:SS 键。
- 行为不变量：
  - 本地角色存在、race start（`GAMESTATEFLAG_RACETIME` 且 `WarmupTimer < 0`）、配置总时长 > 0 时显示倒计时；剩余 <= 60s 变红。
  - 归零后请求一次 kill；`qm_speedrun_timer_auto_disable=1` 时同时关闭功能。功能保持开启时 `TIME EXPIRED!` 显示 5 秒且新 race 开始立即清除；功能被关闭后按关闭路径立即停止渲染，不再显示过期尾巴（与旧版差异，另见 gap）。
  - 无本地角色、race 未开始时不显示；当前仅 `STATE_ONLINE` 进入完整输入路径，离线/demo/观战不构造可请求 kill 的输入。
  - 功能关闭时不构造输入、不执行 logic、不进入 HUD 绘制；`cl_showhud` 关闭时不绘制。
- 重构：倒计时/过期/kill 请求全部收进纯逻辑（`SQmSpeedrunTimerInput` / `SQmSpeedrunTimerState`，不 include 官方头），HUD 只消费 `State()`，渲染走官方 `qm.speedrun.hud` 槽位。优势：时间语义单处可测、无 TClient 字段；代价：每 tick 一次输入快照（小 struct）。与 BC 的差异是显式限制在线才 `SendKill()`/自动停用，避免离线/demo 意外发送。
- 实现：`features/speedrun_timer/`（`CQmSpeedrunTimerLogic` 纯逻辑 + 状态/action adapter + HUD 渲染）；官方触点 `qm.speedrun.hud`（`hud.cpp`，见 `upstream-patches.md`）。
- 配置：`qm_speedrun_timer`（默认 0）、`qm_speedrun_timer_time`（legacy MM:SS）、`qm_speedrun_timer_hours/minutes/seconds/milliseconds`、`qm_speedrun_timer_auto_disable`。
- 验证：`qmclient_speedrun_timer_test.cpp`（含过期待 5 秒、无本地角色期间不丢 expired、离线不发送/不停用）、Release testrunner、boundary check、runtime smoke。
- 未完成项：
  - 全局卡片、UI/UX 和资源加载契约尚未完成。
  - demo/观战、真实 race tick 对齐、warmup 翻转和服务器 kill 响应尚未 smoke。

- [ ] `qm.client_indicator` Client Indicator —— TODO
- [ ] `qm.particles_3d` 3D Particles —— TODO（形状/mixed/碰撞/辉光配置待盘点）
- [ ] `qm.media_background` Media Background —— TODO
- [ ] `qm.finish_prediction` Finish Prediction —— DEFER（旧实现完成状态未确认；确认后转 TODO）
- [ ] `qm.split_timer` Split Timer —— DEFER（旧实现完成状态未确认，checkpoint 来源待定义）
