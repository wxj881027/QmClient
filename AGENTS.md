# QmClient 20.0 重构分支工作规则

本分支 `codex/qmclient-20.0-refactor` 从官方 DDNet `20.0` tag 创建，基线提交 `a5a61806e434ef22141b622db989087fbba8ed21`。目标是在官方 20.0 之上重新实现 QmClient、TClient 和已确认的 BC 能力，形成一层薄的、可删除、可测试、可独立演进的 Qm 扩展层。

**来源对照优先级**：Qm 是本客户端的主线，用户可见行为和完成判定以 Qm 旧实现及当前 Qm 语义为准。TClient 与 BC 同源（Qm 与 BC 都基于 TClient 开发）；BC 代码已跟进官方 20.0 而 TClient 没有，因此需要参考 TC/BC 实现时优先看 BC，TClient（`dyl_dev`）只作历史补充。BC 仓库可以复制到本地引用并在台账登记路径，但 BC 缺失不阻塞 Qm 功能迁移；有 BC 参考时记录差异，没有时记录对照缺口。旧来源的聚合器、字段依赖和提交不得直接带入本分支；先确认官方 20.0 是否已提供等价能力，再逐功能重写，不直接 cherry-pick 大提交。工作树中切换基线后出现的既有未跟踪目录、脚本和发布文件，视为用户或其他流程留下的内容，未确认来源前不得删除、回退或清理。

## 硬约束

- **TClient 字段禁用**：新迁移代码不得出现 `m_TClient.xxx`、`GameClient()->m_TClient.xxx`、`TClientComponent()` 或 `tclient.h` include，也不能通过给 `CTClient` 加 getter 绕过。需要的官方状态从 DDNet 20.0 公开接口读取；缺能力时加最小 `Qm_ADAPTER` / `UPSTREAM_CHANGE`。历史兼容代码只能位于明确的 `legacy_adapter` 边界内，必须有白名单、删除条件和台账记录。静态门禁：`py -3 qmclient_scripts/check_qmclient_boundary.py`。
- **配置前缀**：配置键使用 `qm_` / `Qm` 前缀，不为复用旧字段新增 `cl_` 键；源自 TClient/BC 的功能同样使用 `qm_` 新键，旧 `tc_*` 键只经配置迁移器白名单映射。配置头中每个键都要能反查消费点、设置入口、默认值、迁移规则和测试。
- **官方行为默认不变**：默认配置和功能关闭时，官方 20.0 的输入、预测、网络发送、地图、demo 和渲染行为不得改变。协议、snapshot、demo/skin/map 格式、物理、碰撞、预测、回放、排名和服务端玩法默认不改；确需修改必须标 `UPSTREAM_CHANGE`、单独评审、记录默认行为与删除条件。
- **官方触点登记**：修改官方文件前先在 `docs/qmclient/upstream-patches.md` 登记 hook 形状、默认行为、冲突风险、重放方式和删除条件；优先独立文件和稳定的 manifest/include 集成点，业务不得散落在 `gameclient.cpp`、`menus.cpp`、`hud.cpp`、`chat.cpp` 和 renderer 中。
- **范围授权**：一次只处理一个功能或一个明确问题，聚焦用户请求，不顺手做无关的"现代化"或全仓整理；引擎核心、服务端玩法、地图编辑器、第三方库、CI/release、协议字段等超范围改动必须单独说明并获明确授权。
- **不交付空壳**：不交付空模块、空文档、无行为 stub 或把任务推迟到"以后再决定"；未见实现的功能保留为明确 `DEFER`，不用相似名称功能冒充完成。
- **工作树与进程**：不擅自 reset、覆盖、清理用户或其他流程留下的内容；客户端进程只操作当前工作区构建出的实例，未经当次明确授权不启动、关闭工作区外的正式客户端。
- **诚实汇报**：汇报写清改动、实际运行的命令、结果和未运行的验证项；未跑的检查不得表述为通过；跳过、失败、环境缺失和未确认项单独列为 gap。

门禁职责固定为：`check_qmclient_boundary.py` 仅检查 TClient 依赖和 `*_logic.*` 的 include 纯度，不检查台账同步、命名结构、配置归属、行为覆盖或性能。该脚本按需手动运行，不引入 Git hook 或 CI。

## 架构

```text
DDNet 20.0 官方基线
  -> 最小官方 adapter / hook
  -> CQmRuntime 组合根
  -> feature-owned model / policy
  -> 游戏内渲染、HUD、输入和 UI presentation
  -> 可选 storage / HTTP / helper / platform adapter
```

- `CQmRuntime` 只负责 feature/service 所有权、生命周期分发、固定输入优先级、渲染槽位分发和扩展层 telemetry；不做万能 FeatureManager、万能 Context 或业务网络层，不超过 3000 行。官方状态 adapter 只做对应 feature 的状态到快照打包，不做业务计算；单个 adapter 设 1000 行上限，超过按状态域拆分。
- feature 状态归 feature 所有；UI 只读 model，render 只消费准备好的状态；feature 之间走窄接口、不可变 snapshot 或明确 service，不互相查找完整对象。不首发通用 EventBus；不复制 DDNet 的 renderer、prediction、network、UI 或 config 宏系统。
- 所有权分类：`UPSTREAM_BASELINE`（官方已有，直接用）/ `Qm_NATIVE`（纯官方公开能力，代码在扩展层）/ `Qm_ADAPTER`（官方只保留窄 hook、snapshot 或回调）/ `UPSTREAM_CHANGE`（改官方底层，单独记录、默认行为不变、可重放）/ `EXTERNAL_ADAPTER`（HTTP、helper、音频、平台边界）。adapter 不得 include Qm UI、歌词 provider、统计业务、外部服务或 TClient 聚合器。
- **平行诊断实现已被接受（2026-09-06）**：官方 backend 文件内的诊断触点（graphics 事件、debug callback、device fault、backend snapshot 等）是诊断能力的必要成本，不为"合并方便"牺牲更合理的 hook 形状。约束不变：每个触点保持窄、默认行为不变、可单独重放、有删除条件；新增触点逐个登记并评估冲突面。2026-09-04 的诊断范围冻结继续有效：不再向官方后端添加新的深层诊断字段，扩展需求先给证据和冲突评估。
- 新增第三方 `.cpp`/`.h` 原则上不超过 2000 行，超过时拆 model、logic、render、storage、platform 或 provider。新代码优先放 `src/game/client/components/qmclient/`、`src/game/client/QmUi/`、专用 adapter 目录和测试目录。
- **命名与结构标准**（统一结构是重构的核心收益，不统一等于白做）：feature 目录 `features/<feature>/` 与 model ID `qm.<feature>`、配置前缀 `qm_<feature>_*`、diagnostics feature 计时窗口同名对应；文件布局 `qm_<feature>.h/.cpp`（feature 类 + model）、`qm_<feature>_logic.h`（纯逻辑：只依赖明确列入门禁的 STL 头、`base/` 工具头与同目录头，禁止 engine/game 状态头，输入输出为 struct，可脱离游戏单测，由 `check_qmclient_boundary.py` 静态检查）、`qm_<feature>_adapter.cpp`（官方状态/渲染桥接）、`qm_<feature>_ui.cpp`（presentation，如有）；测试 `src/test/qmclient_<feature>_test.cpp`。偏离该结构必须在台账条目说明原因。

## 功能台账

功能清单和迁移提示统一维护在 `docs/qmclient/feature-ledger.md`，以用户可见能力为单位，人和 AI 共用：

- 每个条目就是该功能的开工提示：状态、来源、行为要点、实现注意点、实现入口、验证方式。开工前定位来源并补全条目；完成后回填证据和提交号。所有条目统一 `qm.` 命名并与代码中 `SQmFeatureModel` 的 model ID 对齐。
- 台账条目与对应功能代码在同一提交中更新，不允许代码先行、台账滞后。
- 迁移路径：失败测试 → 最小实现 → Qm 旧行为对照 → 切换入口 → 删除旧路径。BC/TClient 仅是同源参考；使用 BC 时记录 Qm 方案与 BC 原行为的差异，不把"方案覆盖"理解为"逐字复刻"。
- **重构方向**：旧代码基于 TClient 聚合器架构，底层重构后不能直接搬运，只作行为对照；每次重构必须向更简洁、更高性能方向走——结构和概念数不高于旧实现，热路径不引入新的每帧分配或遍历，feature 关闭时不构造输入、不执行 logic、不进入对应 render。新实现比旧实现更复杂或更慢视为重构失败，重新设计。
- **重构说明**：条目补全时必须写明如何重构——新方案、相比旧实现的优势、代价或缺点、旧代码哪些部分不搬及原因；可搬运的片段逐个列出并给理由。
- **统一命名**：所有新功能统一使用 `qm.` model ID、`qm_<feature>_*` 配置键、`features/<feature>/` 目录、`qm_<feature>_*` 源文件和 `qmclient_<feature>_test.cpp` 测试名；已有 feature 代码必须对应台账中的非 `TODO` 条目。结构一致性由台账、人工 review 和测试维护，不由边界门禁自动解析。
- **行为与实现分离**：用户可见行为默认与旧版一致，有意的行为变更必须单独登记（旧行为 → 新行为、理由、用户迁移方式）；内部结构、命名、文件布局一律按新标准统一，不为"像旧代码"而妥协。
- 不以"文件已迁移"代替"功能已保全"；命令、绑定、聊天触发、文件触发和自动触发，以及资源（运行时/生成资源、字体、图标 atlas、emoji、HTML、helper、缓存、安装包内容）同样在台账登记。
- 按任务范围加载上下文：先读直接实现、调用方和相关测试；只有任务涉及配置、文案、资源、官方触点、生命周期或性能时，再读取对应专题文档。不要因为一次小修改重复加载全部文档。
- 新功能和较大行为改动开始前，写清行为不变量、状态 owner、官方触点和验证方式；小修复不要求补齐与任务无关的架构材料。

## 文档路由

- 当前阶段、阶段出口和下一步：`docs/qmclient/plan.md`。
- 远程功能候选和重构板块：`docs/qmclient/refactor-feature-catalog.md`；选择下一项功能或整理阶段 6 波次时读取，不能把其中候选状态当成本地完成状态。
- 模块边界和已定架构决策：`docs/qmclient/architecture.md`；只有修改所有权、公共接口、组合根或层次关系时读取。
- 功能来源、行为对照、迁移入口和功能状态：`docs/qmclient/feature-ledger.md`；处理具体功能时读取对应条目。
- 卡片字段、presentation、交互和资源契约：`docs/qmclient/card-ui-spec.md`；处理卡片或 UI 行为时读取。
- 官方文件触点：`docs/qmclient/upstream-patches.md`；新增或改变官方集成边界时读取并更新。
- 配置迁移、初期取舍和历史决策：`docs/qmclient/early-critical-decisions.md`；只有触及这些主题时读取。
- i18n、性能、基线和验收证据分别读取 `i18n.md`、`performance.md`、`20.0-baseline-audit.md`、`verification-checklist.md`。
- 阶段状态只以 `plan.md` 为准，功能状态只以 `feature-ledger.md` 为准，验证结果只以 `verification-checklist.md` 为准。其他文档中的旧快照只作历史背景。

## 生命周期与状态矩阵

有状态或有副作用的 feature 必须覆盖并写清 reset、取消、generation 和错误回退：

- 模式：offline / main online / dummy online / spectator；normal / race / DDRace / Gores / volleyball / unsupported map。
- 流程：connect / join / map change / team change / freeze / unfreeze / death / finish；disconnect / reconnect / renderer switch / fullscreen / resize / shutdown。
- 场景：live play / demo playback / demo seek / editor 或 settings preview。
- 旧 HTTP/job/helper 结果不得覆盖新服务器、新玩家、新歌曲；worker 不直接修改 UI、`CGameClient` 或 graphics 对象；断线、切图、关闭窗口和异常退出必须释放任务、纹理、音频、输入、IME、临时文件和外部进程状态。

## UI 与 i18n

- 旧 UI 保持官方界面，Qm 功能集中到 Qm 设置页；新 UI 把同一批 feature model 分布到卡片页面，两种模式不得各自实现业务状态。card ID、排序、折叠、页面、搜索、分页、DPI、键盘导航、触控属于兼容接口。
- 图标使用 Phosphor Bold，构建期生成稳定 atlas/manifest，运行时用稳定 ID，不在卡片中散落 SVG 路径。动画先走窄的、可测试的 motion/curve 层；引入第三方 UI toolkit 前必须做依赖、许可证、平台、帧时间和可删除性评估。
- 优先复用 DDNet 20.0 的文本渲染和缓存，不在每帧重建 text container、quad、纹理或字符串。资源加载失败要有可见但不阻断核心游戏的 fallback。
- Qm 文案单一 source（`translations/i18n/qmclient/*.toml`）；使用 `py -3 qmclient_scripts/qmclient_i18n.py validate` 校验 schema、重复 key 和 Qm 源码字面量文案覆盖，使用 `generate --output <path>` 生成确定性 JSON catalog。该脚本只负责 i18n source 维护，不属于 boundary 门禁，也不检查台账、命名、配置、行为或性能；当前 catalog 尚未接入运行时，语言 txt 仍是后续生成物，不得混入未审核 CJK source 或机器翻译草稿。
- 任何客户端显示的文本文案，都得走 Localize 等函数的官方路径。

## 性能与验证

- 性能证据自动写入用户可写的 diagnostics/session 目录（session/report JSONL、ring buffer、`report.json`），不靠手动复制；日志失败不得阻止游戏启动。telemetry 非阻塞，不改变默认渲染行为，不明显损害 1% low；GPU query 必须非阻塞。各后端必须记录的字段以 `performance.md` 和 `upstream-patches.md` 为准。
- 性能结论必须来自同场景、同构建类型、同硬件、同后端、同采样窗口的 A/B 数据；日志"存在"不等于性能问题已定位。
- 每个完整改动批次运行与风险匹配的构建和测试；涉及边界的代码运行 `py -3 qmclient_scripts/check_qmclient_boundary.py`，涉及文案时运行 i18n 校验，涉及启动或生命周期时运行 runtime smoke。纯文档改动做链接、路径和状态核对。核心逻辑或公共接口改动做一次只读 review；不要求每个小编辑重复全套验证。

## 构建环境

- 目标平台 Windows、Linux、macOS、Android、iOS（当前已有），跨平台代码不得依赖 Windows 专有行为。`ddnet-libs/` 是 submodule 依赖，保留现有快照。
- Windows：VS2022 MSVC x64 + Windows SDK + CMake + Ninja + Python 3 + Rust。统一入口 `qmclient_scripts/cmake-windows.cmd`（自动初始化 VS 环境），脚本必须从仓库根目录调用，每次构建重新初始化环境：
  - 首次配置：`cmd /c qmclient_scripts\cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release`
  - 增量构建：`cmd /c qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target game-client --parallel 14`
- 构建目录统一在仓库根：`cmake-build-release/`（标准）、`cmake-build-debug/`（Debug）、`cmake-build-linux/`（WSL 门禁），由 `.gitignore` 排除，不用带自定义后缀的目录名。客户端必须从构建目录启动（Vulkan 着色器等运行时资源依赖工作目录），不从仓库根目录直接运行 `DDNet.exe`。
- **Linux 编译门禁**：在 WSL 内用 `cmake-build-linux/` 目录配置和构建，至少 `game-client` 编译通过；依赖缺失按发行版安装开发包，首次跑通后把实际命令记录到 `docs/qmclient/20.0-baseline-audit.md`。（如果 开发者电脑缺失 WSL 可不执行）
- 同一构建目录内 `game-client`、`testrunner`、C++/Rust 测试、打包目标串行执行。runtime smoke：构建后 `py -3 qmclient_scripts/check_qmclient_runtime_smoke.py`（临时 storage，只启动当前工作区构建产物）。日志和临时文件放 `tmp/`。
- 修改 `config_variables*.h` 或全局配置结构后，Release 启动早期异常先用 `--clean-first` 完整重建再归因。构建失败先确认 `ddnet-libs/` submodule 已初始化、构建经入口脚本初始化了 MSVC 环境。脚本/JSON 中 Windows 路径优先正斜杠或仓库相对路径。

## DDNet C++ 约定

- 除 `src/base/` 外遵循 DDNet 命名：`C` 前缀类型、`m_` 成员、`g_` 全局、`s_` 静态；定长数组/`std::array`/`std::vector` 用 `a`/`v` 前缀；新枚举 `enum class`，常量 `constexpr`。代码注释用中文。
- 不为现代化把既有接口整体模板化或工具层化；避免裸 new/delete、热路径临时对象、为未来并发预埋的锁或原子。
- 外部输入、数组边界、索引、空指针必须校验；不静默忽略错误；明确对象所有权、引用有效期和序列化边界。
- review 重点：每帧/tick/玩家/实体路径的分配与开销、预测一致性、协议体积和带宽变化；findings 按正确性 > UB > 内存安全 > 生命周期 > 并发 > 性能 > 兼容性排序，写明严重度、位置、影响和最小修复。
- 源文件 UTF-8，保留既有 BOM、换行和缩进；优先局部编辑，不做无关全仓格式化；文档路径用正斜杠。

## 提交与版本

- 一次提交一个清晰动作：基线、adapter、单功能迁移、测试、性能证据或文档收口；提交标题 `<type>(<scope>): <中文简述>`。工作树允许存在并行任务留下的修改，默认一次提交，不替用户拆分或覆盖提交范围之外的内容。
- 是否提交、推送、建 PR 或发版以用户当次明确要求为准；受保护分支默认走 PR。发布必须由用户明确授权；旧 `bump_version.py` / release 脚本未恢复并验证前不是可执行入口。
- QmClient 版本与 DDNet 基线版本分开记录；完整功能完成后按版本策略更新 MMP，重构期不改官方 release 版本号。
- 删除旧代码前完成新入口接管、配置/命令/资源/持久化对照、引用扫描、测试、smoke 和至少一次 rebase 演练。

## 文档

规则只有本文件一个来源，不新增子目录 `AGENTS.md` 或重复的 agent 规则文件。专题文档的读取条件见“文档路由”；新增规则先判断是否属于硬约束，属于设计理由、当前状态或验证证据的内容不要复制到本文件。旧 `docs/superpowers/` 和历史方案文档只作迁移线索，过时内容不得复制进新文档。任何未解释的配置、命令、资源、触点或状态分支保持 `TODO`，不得标 `DONE`。
