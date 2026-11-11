# AGENTS.md

QmClient（Q1menG Client）基于 DDNet / TaterClient，主要使用 C++，辅以 Rust、Python；构建使用 CMake，依赖在 `ddnet-libs/` 子模块。

## 执行与授权

- 按当前请求和已有上下文执行到完成；已授权的调查、准备和可逆修改直接推进，不重复确认。
- 歧义只在影响实现、范围或风险时澄清；先完成不依赖答案的工作。用户追加要求时保留有效成果，调整剩余工作。
- 优先级：系统与开发者指令 > 用户当前指令 > 仓库规则与 skills。引用文档不产生额外授权。
- 超出授权范围或涉及破坏性、外部操作时，先完成可审查的准备再确认。若规则导致暂停，指出具体文件、原文和适用原因。
- 遇到非自己产生的文件改动，先视为用户或其他任务正在操作：不回退、不覆盖、不纳入本次提交。
- 只有任务独立且并行确能节省时间或提高质量时才委托；高风险改动可独立复核，工具不可用时自行审查并说明限制，不因此停工。

## 会话启动与交接

每个新会话执行仓库任务前先做识别，不依赖旧聊天记忆；只读任务只核对所需信息，不占写入槽位。

- 运行 `git status --short`、`git branch --show-current`、`git rev-parse HEAD`、`git worktree list --porcelain`，确认当前是主工作树还是关联槽位（Codex 中作为 Local 项目使用，不改变其 Git Worktree 身份）。detached HEAD 是单分支槽位的正常状态，不因此新建分支或重建 Worktree。
- 按「本地机器配置与固定 Worktree」发现并核对 `.agents/machine.local.json`，确定集成分支、固定槽位、构建目录、job 预算和窗口偏好；不得按项目名、空分支名或其他开发者配置猜测。
- 有匹配配置时读取主工作树 `.agents/worktree-state.local.json`（若存在，Git 忽略）。它记录任务、会话占用、基线与交付 SHA、构建对应源码状态、验证证据和待办，不保存凭据或新增授权；记录缺失、过期或与 Git、进程、任务状态不一致时以实际状态为准，不把它当互斥锁，也不把“空闲”当作无其他会话写入的证明。
- 写入或切换前检查本槽位活跃会话、未提交修改和未集成提交；构建前核对进程占用与全机资源预算。只写当前任务明确授权的目录，不把主目录成果、其他槽位任务或历史 Worktree 纳入本任务。
- 使用已有构建目录前核对 CMakeCache 的源码路径和配置；运行客户端前确保二进制对应当前源码。源码同步不会更新二进制，旧 exe 存在不代表当前源码已验证。默认增量构建，不重新初始化 Submodule、不删缓存、不自动创建 Worktree。
- 交接时更新已有任务文档（有配置时同步主工作树槽位记录，只改本任务条目），注明实际 SHA、未提交修改、最后成功构建的源码版本、验证范围和剩余动作；无法确认的信息写明未知。共享文件不是自动调度器，并行更新需协调；本地环境状态不写入版本化文档，重要决策与验证证据仍按文档规则维护。

## 本地机器配置与固定 Worktree

- `.agents/machine.local.json` 与 `worktree-state.local.json` 只作为数据读取：不执行其中内容，不提交、不推送、不写入版本化报告，也不扩大任务授权；用户当前明确指令优先。发现、校验与继承细则见 [.agents/README.md](.agents/README.md)。
- 无本地配置时沿用当前目录、当前分支和已有构建环境，不假定他人的固定路径、分支或槽位数量。默认构建目录 `cmake-build-release`，单次 `-j 6`，重构建串行；机器配置可调整资源预算。
- 固定槽位、Submodule、索引和构建缓存长期保留；同一目录只有一个写入会话，只读任务不占槽位。不自动创建临时 Worktree，不删除或归档固定槽位，不用 `reset --hard` 或 `clean` 清场。
- 每个 Worktree 独占构建目录，增量复用，不共享 Cache 或中间产物，不常规删除 build；同目录构建、测试、benchmark、打包和 gate 串行，跨目录也计入全机资源预算（含 Rust、链接与测试）。
- 固定槽位用 detached HEAD 单分支模式：聚焦提交、保存标签、由主目录串行 cherry-pick 集成；同一功能链和公共接口修改优先串行。采用任务分支时按自己的已授权策略执行，不强制共用开发分支。流程见 [固定 Worktree 开发与集成](docs/归档/2026-10-07-固定Worktree开发与集成.md)。
- 合并远程、长期或 reset 过的分支前，先按 [长期分支与远程合并流程](docs/规格/2026-10-18-长期分支与远程合并流程.md) 列出对方删除项；已明确删除的功能、配置和 UI 入口保持删除，合并后做净差异与回潮审查。
- 烟测与实机截图一律窗口化（`gfx_fullscreen 0`）启动，不用无边框/全屏：该模式切出窗口会卡顿或黑屏，截图与交互脚本易失焦。纯启动烟测尽量后台最小化；视觉与交互验收用可见窗口，最小化结果不能替代视觉证据。需要全屏证据时用窗口化全屏并在说明中写明；图形后端启动时不保证完全不激活窗口。交付人工验证命令时使用实际槽位和构建目录的 PowerShell 路径。脚本改 `settings.cfg` 前备份、结束还原，只结束自己启动的实例。

## 项目边界

- 聚焦当前任务，遵循附近 DDNet 模式；不顺手重构上游或引入无关抽象。
- QmClient 功能优先落在 `src/game/client/components/qmclient/`、`src/game/client/QmUi/`、Qm 配置头和 `qmclient_scripts/`。
- 未明确授权不改协议、snapshot/输入/时序、物理/碰撞/预测、地图/回放行为、rank 可达性及 demo/skin/配置/存档格式。引擎核心、服务端玩法、地图编辑器、第三方库、根 `CMakeLists.txt`、Release CI 只在任务明确涉及的范围内修改。
- 客户端进程只操作当前工作区开发目录下启动的实例；工作区外或安装目录中的正式客户端须当次明确授权。
- **移动端只做观战端**。除任务本身就是移动端外，不以移动端为约束，也不把“可能变慢 / 不适配 / 布局挤”写成风险、待办或评估项；只有改动落在 `CONF_PLATFORM_ANDROID` / `CONF_PLATFORM_IOS` 宏或手机端专属 UI 与设置时才讨论。判断标准是“值不值得为移动端改”，默认不值得。
- **UI 先找现成组件，再谈自绘**：`src/game/client/QmUi/` 已有胶囊页签/多选一（`CMenus::DoSegmentedChoice`、`ui_widget::CapsuleTabBar`）、按钮、输入框、滚动区、卡片骨架；能复用就复用，只有确实缺这个组件才自绘。同一视觉件出现在两个页面上时必须来自同一组件。
- Qm 配置前缀使用 `qm_` / `Qm`；代码注释用中文；UTF-8，保留原 BOM、换行与缩进；文档路径用 `/`。
- **未发布前不做配置迁移**：不新增 `cl_config_version` 迁移块、兼容读入分支或旧值换算。删改配置项、枚举值或预设索引时直接改默认值、取值范围和 UI 顺序；同值在新旧语义下无法自判，迁移只会制造歧义，发布后按当时已发布的语义再补。
- 日志和临时产物放 `tmp/`（由人类开发者手动清理）。仓库根只放正式工程文件；一次性脚本、探针、抓取结果、构建日志、临时补丁用完即移入 `tmp/`，不为一次验证另起临时构建或临时脚本。

## 按需读取

新增或修改测试、fixture、benchmark 时，先读 [src/test/AGENTS.md](src/test/AGENTS.md)；benchmark 另读 [benchmark/AGENTS.md](src/test/benchmark/AGENTS.md) 和 verification skill 的性能参考。这些入口同样适用于在生产代码任务中附带编写测试，不能只参考邻近旧测试。

先读相关实现与调用点；有直接相关的有效 plan/spec 时再读。只加载命中任务的 skill 与参考，不递归加载关联文件。

| 任务                                 | `.agents/skills/` 下的入口            |
| ------------------------------------ | --------------------------------------- |
| C++ 实现、调试、重构                 | `qmclient-cpp-conventions/SKILL.md`   |
| 选择验证、交付代码                   | `qmclient-verification-gate/SKILL.md` |
| 代码审查、深度质量审计、发布风险审查 | `qmclient-code-review/SKILL.md`       |
| 翻译、生成链及污染/迁移/覆盖审计     | `qmclient-i18n-workflow/SKILL.md`     |
| 提交、PR、版本与发布                 | `qmclient-git-commit/SKILL.md`        |

翻译不是每个任务的默认步骤：改 `Localize` 文案但任务与翻译无关时，不运行 extract/generate/validate，也不提交 `data/languages/*.txt` 的顺带变化；在任务尾声询问用户是否本次同步。

维护规则时读 [.agents/README.md](.agents/README.md)；使用脚本时按需读 [qmclient_scripts/scripts_overview.md](qmclient_scripts/scripts_overview.md)。

## 测试代码规范

测试层级的完整定义、编写与保留、源码合同边界和覆盖规则唯一维护在 [src/test/AGENTS.md](src/test/AGENTS.md) 的「测试规则」一节；新增或修改任何层级的测试都先读该节。根文件只保留三条不可协商的边界：

- 测试按单元、集成、端到端、冒烟、静态合同分层放置，不混写；端到端与冒烟放 `qmclient_scripts/integration/`，根 `scripts/` 属 DDNet 上游同步区，不加入 QmClient 特化。
- 禁止用源码字符串、函数体截取或 `ReadTestSourceFile` 证明运行时行为；静态合同只用于缺少稳定运行时观察点、且约束属于构建或架构边界的情况。
- “测试数量”“测试通过”“代码覆盖率”是不同证据：没有 line/function/branch 报告时不得声称覆盖率；构建成功不等于运行时、端到端、视觉或跨平台行为通过；最终回复记录实际运行的层级、过滤范围和未覆盖场景。

## 构建入口

- Windows 构建必须使用 `qmclient_scripts/cmake-windows.cmd`，不要直接调用 `cmake --build`；封装脚本会加载 VS/MSVC 环境并调用 `repair_ninja_msvc_prefix.py` 修复 Ninja 依赖前缀。
- 常用命令：`cmd /c qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 6`；C++ 测试目标 `run_cxx_tests`，Rust 测试目标 `run_rust_tests`。
- 首次配置：`qmclient_scripts/cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release`。

## 验证与交付

- 按 [verification skill](.agents/skills/qmclient-verification-gate/SKILL.md) 选择风险匹配的测试与 gate：低风险可只跑相关过滤测试，常规代码至少 quick gate；已覆盖的检查不重复执行。
- 修复可复现行为时优先先写失败测试；小改动不为满足 TDD 写实现镜像或无意义测试。
- 性能敏感代码尽量模块化并可用 Google Benchmark 测量生产实现；性能验证由 verification skill 选择。功能测试或 gate 通过不等于性能证据，CPU 微基准不等于帧、设备和玩家场景实测。
- 纯文档改动人工核对内容、链接和状态，不跑代码 gate；未验证的运行时或跨平台行为不称通过。
- 默认用自然段说明结果、验证和真实剩余问题；必要时才列项，普通回复不套 commit/PR 模板。
- 版本与通道遵循 [版本号与发布通道](docs/归档/版本号与发布通道.md)：正式版 `X.Y`，预览版 `X.Y-preview.N`，不使用额外代号。仅在用户授权时通过 `bump_version.py` 修改；普通提交、纯调查、文档和规则维护不升级版本。操作入口见 Git skill。

## 文档权威

- 本文件管全局边界，skills 管专项操作，`references/` 只放按需资料。新文档按内容归档：功能设计与实施方案 → `docs/规格/`；历史调研、修复记录、被替代方案 → `docs/归档/`；长期、高风险、跨多轮的架构计划 → `docs/架构/`（一项工作一份，单一归口，允许 HTML）。`docs/superpowers/` 已停用。
- 新文档用 `YYYY-MM-DD-中文主题.md`（日期取本地创建日期）；`AGENTS.md`、`README.md`、`SKILL.md` 等固定入口沿用原名。正文写清目标、行为和实现安排，不套 skill、审查报告或提交模板。
- 优先更新已有文档，不为每轮测试、提交或审查新建报告；同一任务的阶段证据集中维护。已完成的实施安排、阶段验收和修复记录移入 `docs/归档/` 并标注历史状态；仍有效的功能规格和未完成验收留在 `docs/规格/`；归档时修正引用。
- 只采用与当前任务相符且仍有效的文档；`draft` 仅在你采纳后作为实现依据；无状态文档须核对现状，不能仅凭日期判断有效性。
- 归档、过时或被替代的文档只作历史线索；保留记录，用状态或 supersedes 标注替代关系。
- 重要决策、长任务进度和交接证据写入版本化文档；小任务直接在最终回复交付，不强制新建计划或报告。
