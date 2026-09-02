# QmClient 20.0 重构分支工作规则

本分支 `codex/qmclient-20.0-refactor` 从官方 DDNet `20.0` tag 创建，基线提交为 `a5a61806e434ef22141b622db989087fbba8ed21`。这是重新实现 QmClient、TClient 和已确认 BC 能力的重构分支，不是对 `dyl_dev` 的继续整理。

`dyl_dev` 只用于功能盘点、行为对照和迁移证据。不得把它的 QmClient/TClient 聚合器、字段依赖或全部提交直接带入本分支。当前仓库中切换基线后出现的既有未跟踪目录、脚本、发布文件和 submodule 状态，视为用户或其他流程留下的内容；未确认来源前不得删除、回退或清理。

## 1. 工作目标

- 以官方 DDNet 20.0 的架构、协议、物理、预测、快照、demo、地图和渲染行为为默认事实。
- 在官方客户端之上建立一层薄的、可删除、可测试、可独立演进的 QmClient 扩展层。
- 逐项重新实现当前 QmClient、TClient 以及必要的 BC 能力，不能以“文件已经迁移”代替“功能已经保全”。
- 让官方后续提交可以低冲突地同步；官方文件中的 Qm 改动必须少、窄、可定位、可单独重放。
- 游戏内性能和外部 UI 性能都必须有准确、自动落盘的证据；不能依赖用户手动复制日志。

## 2. 唯一规则来源

本分支只保留一个用于 AI/开发执行的根级 `AGENTS.md`。不要新增 `.agents/`、子目录 `AGENTS.md` 或重复的 agent 规则文件。

长期设计和过程记录放在 `docs/qmclient/`，建议至少维护：

```text
architecture.md
20.0-baseline-audit.md
feature-migration.tsv
upstream-patches.md
performance.md
i18n.md
```

这些文档记录事实、决策、状态和证据，不替代本文件的硬约束。旧 `docs/superpowers/` 文档只作为迁移线索，不能作为新实现的默认依据；过时内容不得复制进新文档。

## 3. 架构边界

目标结构采用官方 `CComponent` 生命周期和少量稳定 hook：

```text
DDNet 20.0
  -> 最小官方 adapter / hook
  -> CQmRuntime 组合根
  -> feature-owned model / policy
  -> 游戏内渲染、HUD、输入和 UI presentation
  -> 可选的 storage / HTTP / helper / platform adapter
```

`CQmRuntime` 只能负责 feature/service 的所有权、生命周期分发、固定输入优先级和扩展层 telemetry。它不能变成万能 `FeatureManager`、万能 `Context`、业务网络层或 3000 行以上的上帝对象。

规则：

- feature 状态由 feature 自己拥有；UI 只读 model，render 只消费已准备好的状态。
- feature 之间通过窄接口、不可变 snapshot 或明确 service 交互，不互相查找完整对象。
- 不首发通用 EventBus。只有明确的“一次事件、多处无关消费者”语义才增加类型明确的事件。
- 不复制 DDNet 的 renderer、prediction、network、UI 或 config 宏系统。
- 新增的第三方 `.cpp` / `.h` 原则上不超过 2000 行；超过时拆成 model、logic、render、storage、platform 或 provider。
- 新增代码优先放在 `src/game/client/components/qmclient/`、`src/game/client/QmUi/`、专用 adapter 目录和测试目录；修改官方文件前先记录原因和影响面。

### 3.1 所有权分类

每个改动必须归入一种所有权：

- `UPSTREAM_BASELINE`：DDNet 20.0 已提供，直接使用，不复制第三方实现。
- `Qm_NATIVE`：只使用官方公开能力即可完成的 Qm 功能，代码放扩展层。
- `Qm_ADAPTER`：Qm 功能需要官方提供一个最小能力，官方侧只保留窄 hook、snapshot 或回调。
- `UPSTREAM_CHANGE`：确实需要改官方底层的能力，必须单独记录、保持默认行为不变，并可重放到新上游。
- `EXTERNAL_ADAPTER`：HTTP、helper、音频、平台或第三方资源边界；不得让官方核心依赖外部业务。

adapter 不得 include Qm UI、歌词 provider、统计业务、外部服务或 TClient 聚合器。

### 3.2 TClient 字段禁用

这是硬约束：**新迁移代码不得使用 TClient 的字段作为状态、服务或跨模块 API。**

- 不得新增 `m_TClient.xxx`、`GameClient()->m_TClient.xxx`、`TClientComponent().xxx` 或对 `tclient.h` 的依赖。
- `CTClient` 只能作为 `dyl_dev` 的历史行为来源和迁移期只读对照对象，不能成为新架构的状态仓库。
- 不能通过给 `CTClient` 增加 getter 把旧字段依赖伪装成新接口。
- 需要的状态必须归属新的 feature，例如地图历史、Gores 进度、好友 presence、冻结事件、输入策略分别拥有自己的 model/policy。
- 需要官方状态时从 DDNet 20.0 的公开接口读取；缺少能力时增加最小 `Qm_ADAPTER` / `UPSTREAM_CHANGE`，不得绕道读取 TClient 私有状态。
- 迁移期的 legacy comparison adapter 必须有白名单、删除条件和台账记录，不能被新 feature 逻辑依赖。

新迁移目录的静态门禁禁止出现 `m_TClient`、`CTClient`、`TClientComponent()` 和 `tclient.h` include。历史兼容代码只能位于明确的 `legacy_adapter` 边界内。

## 4. 功能保全

功能以用户可见能力为单位登记，不以类、文件或组件为单位登记。功能台账每行至少包含：

```text
id, user_visible_name, source_project, source_files_and_symbols, owner
decision, config_keys_and_defaults, legacy_config_migration
console_commands, keybinds_and_input_priority, chat_or_network_triggers
resources_and_generated_assets, persistent_files_and_schema
thread_job_queue_cancel_generation, lifecycle_matrix, mode_matrix
upstream_touchpoints, platform_matrix, behavior_invariants
tests, smoke_steps_and_failure_signals, performance_budget
privacy_and_failure_fallback, status, replacement_commit, evidence
legacy_tclient_fields_read, new_state_owner
official_state_source_or_adapter, legacy_accessor_removal_condition
```

定义在配置头中的每个键都必须能反向找到消费点、设置入口、默认值、迁移规则和测试。命令、绑定、聊天触发、文件触发和自动触发同样需要登记。资源包括运行时资源、生成资源、字体、图标 atlas、emoji、HTML、helper、缓存和安装包内容。

迁移前必须重点盘点以下当前能力，不得因它们藏在聚合器中而漏掉：

- Qm 生命周期、服务器时间、playtime、本地统计、Qm/Arg 识别、开发者 presence、Axiom 登录和成绩查询。
- Qm 翻译、语音、歌词、网易云/汽水 provider、system media controls、脚本、更新器和自动安装退出流程。
- Qm HUD notifications、chat bubble、emoji、pie menu、输入覆盖层、IME、动态图标、skin queue、dummy mini view、streamer 隐私。
- Qm weapon/laser presentation、jelly、tee hue、collision hitbox、hammer hit detection、focus mode、消息过滤和聊天交互。
- TClient 的 background particles、background draw、moving tiles、outlines、rainbow、trails、pet、player indicator、skin profiles、status bar、warlist。
- TClient 的 bind chat、bind wheel、repeat、Air Rescue、Spec ID、emote cycle、freeze/waterfall detection、freeze wakeup/combo、auto reply、auto team lock、red packet claim。
- TClient 的 Gores 距离场和进度、自动 weapon cycle、fast practice、antiping、ghost、remove anti、mod weapon、dummy/预测相关功能。
- 地图收藏、分类缓存、地图笔记、map history、本地存档列表和 join hint、好友上线/进入提醒、custom communities。
- BC 的 Client Indicator、3D Particles、Speedrun Timer、Auto Team Lock、Media Background，以及尚未确认完成的 Finish Prediction、Split Timer。

BC 能力必须记录当前 Qm 方案与 BC 原行为的差异；“方案覆盖”不等于“逐字复刻”。未见实现的功能必须保留为明确 backlog 或 `DEFER`，不能用相似名称功能冒充完成。

## 5. 生命周期和状态矩阵

所有有状态或副作用的 feature 至少覆盖：

```text
offline / main online / dummy online / spectator
normal / race / DDRace / Gores / volleyball / unsupported map
live play / demo playback / demo seek / editor or settings preview
connect / join / map change / team change / freeze / unfreeze / death / finish
disconnect / reconnect / renderer switch / fullscreen / resize / shutdown
```

必须明确 reset、取消、generation 和错误回退。旧 HTTP/job/helper 结果不得覆盖新服务器、新玩家或新歌曲；worker 不得直接修改 UI、`CGameClient` 或 graphics 对象。断线、切图、关闭窗口和异常退出必须释放任务、纹理、音频、输入、IME、临时文件和外部进程状态。

默认配置和功能关闭时，官方 20.0 的输入、预测、网络发送、地图行为、demo 行为和渲染语义不得改变。没有明确批准，不改协议、snapshot、demo/skin/map 格式、物理、碰撞、预测、回放、排名和服务端玩法。

## 6. UI、UX、i18n 和资源

- 旧 UI 模式保持官方 UI，并把 Qm 功能集中到 Qm 设置页；新 UI 模式把同一批 feature model 分布到卡片化页面。两种模式不得各自实现一套业务状态。
- card ID、排序、折叠、页面、搜索、分页、拖拽、未知 card 保留、DPI、键盘导航、触控和 preview 状态都属于兼容接口。
- Phosphor Icons 使用 Bold weight。图标在构建阶段生成稳定的 atlas/manifest，运行时用稳定 ID；不得在卡片中散落 SVG 路径或每次打开页面重新加载图标。
- 动画先用窄的、可测试的 motion/curve 层；引入 Smooth UI Toolkit 或其他库前必须做依赖、许可证、平台、帧时间和可删除性评估，不能把第三方 UI toolkit 直接绑进官方底层。
- 优先复用 DDNet 20.0 的文本渲染、缓存和资源生命周期。只有性能数据证明存在缺口，才增加 Qm 专用 cache；不在每帧重建 text container、quad、纹理或字符串。
- Qm 文案维护单一 source，推荐 `translations/i18n/qmclient/*.toml`，语言 txt 是生成物。source、配置描述和菜单文案不得混入未经审核的 CJK source、重复 key 或机器翻译草稿。
- 资源加载失败要有可见但不阻断核心游戏的 fallback；安装包、开发目录、平台目录和生成资源都必须在台账中验证。

## 7. 性能和图形诊断

性能日志必须自动写入用户可写的 QmClient diagnostics/session 目录，不能要求手动复制控制台。至少记录：

- 游戏 update/render、tick、snapshot、prediction、输入准备和各 feature 的耗时；提供平均、p95、p99、最大帧和 1% low，明确采样窗口、帧率上限和 cap/vsync 状态。
- 内存分为进程工作集、Qm owned bytes、纹理/atlas/font cache、后台任务结果和外部 helper，不能把一个数字冒充全部内存。
- UI 首次打开、预热、页面切换、搜索、滚动、卡片重排、文本布局、图标/字体加载、纹理上传和后台 job 等待。
- session metadata、周期性 JSONL、异常前后 ring buffer 和自动 `report.json`；日志失败不得阻止游戏启动。

图形后端必须有统一诊断字段和后端专属字段：

- 启动自动记录 GPU、驱动、API、adapter/device、swapchain/context、格式、分辨率、MSAA、shader/pipeline cache 能力。
- Vulkan 记录 instance/device/API/validation/layer/queue/present、内存堆、query 方法及不可用原因；validation callback 不得造成每帧同步。
- OpenGL/GLES 记录 vendor/renderer/version/GLSL/extensions、context/profile、纹理/缓冲能力、swap interval 和 debug callback；Metal 记录 device、feature set、drawable/present 和 shader/library 状态。
- resize、fullscreen、renderer switch、shader failure、device loss、fallback 和资源重建都必须自动形成事件；字段在不同后端尽量对齐，不可比较的字段明确标注。
- GPU query 必须非阻塞；任何 telemetry 不能改变默认渲染行为或明显损害 1% low。

性能结论必须来自同场景、同构建类型、同硬件、同后端和同采样窗口的 A/B 数据。日志“存在”不等于性能问题已定位。

## 8. 上游同步和提交

- 每个官方文件的修改都记录在 `upstream-patches.md`，说明原因、hook 形状、默认行为、冲突风险、重放方式和删除条件。
- 优先独立文件和一个稳定的 manifest/include 集成点；不要让业务散落在 `gameclient.cpp`、`menus.cpp`、`hud.cpp`、`chat.cpp` 和 renderer 中。
- 一个提交只做一个清晰动作：基线、adapter、单功能迁移、测试、性能证据或文档收口，不混合全仓格式化和无关上游同步。
- 不直接 cherry-pick `dyl_dev` 的大提交。先确认官方 20.0 是否已提供等价能力，再逐功能重写或制作最小 patch。
- 新功能未完成时不要修改官方 release 版本号；QmClient 版本和 DDNet base 版本分开记录。
- 删除旧代码前，必须完成新入口接管、配置/命令/资源/持久化对照、引用扫描、测试、smoke、性能证据和至少一次 rebase 演练。

## 9. 开发和验证流程

开始修改前：

1. 读取本文件、当前有效 plan/spec 和功能台账条目。
2. 用 CodeGraph 查询定义、调用方和影响面；字面配置、资源、日志和文档使用精确搜索。
3. 写清行为不变量、owner、官方触点、状态矩阵和验证方式。
4. 一个功能一个边界；不要把 bugfix、功能迁移和无关重构混在同一补丁。

实现时：

- 优先失败测试 -> 最小实现 -> 行为对照 -> 切换入口 -> 删除旧路径。
- 代码注释用中文；遵循 DDNet 命名和生命周期风格；避免不必要堆分配、锁、线程和模板抽象。
- 新迁移代码经过只读 review，必要时按功能、架构、性能/图形拆给独立子代理；主代理负责去重、定级和最终结论。

完成前：

- 运行与改动风险匹配的构建、C++/Rust 测试、固定 smoke、性能 session 和图形后端验证。
- 纯文档改动不跑代码 gate；代码改动按仓库提供的 verification gate 执行，未跑项目明确写为 gap。
- 提交前检查 `git diff --check`、台账状态、资源/生成物、翻译 source/产物、官方 patch 记录和未计划文件。
- 不因测试迁就实现，不把 build 成功、源码字符串命中或一次 focused test 当成全量验证。

任何未解释的配置、命令、资源、字段、官方触点、平台条件或状态分支都保持 `INVENTORY`，不能标记 `DONE`。
