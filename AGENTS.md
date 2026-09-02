# AGENTS.md

QmClient（Q1menG Client）是基于 DDNet / TaterClient 的第三方定制客户端。

- 主要语言：C++
- 辅助语言：Rust、Python、少量平台相关语言
- 构建系统：CMake
- 依赖管理：Git Submodules（`ddnet-libs/`）
- 目标平台：Windows、Linux、macOS、Android（后续也许有 IOS）

## 极简工作流

### 范围边界

- 一次只做一个功能或明确问题；上游协议 / 物理 / 预测 / 格式改动默认不做。
- 补丁聚焦：遵循现有 DDNet/QmClient 模式，不顺手重构，不为「现代化」扩 scope。

### 启动顺序

1. 改前弄清附近源码、调用点、配置、翻译与测试。

### 完成任务后

- 代码改动默认至少 `python3 qmclient_scripts/gate/check_gate.py --mode quick`；提交前优先 `--mode default`；准发布再用 `--mode full`。Windows 使用 `py -3` 或环境中的 `python`。纯文档人工核对，不跑代码 gate。
- 开发期「改完代码后」默认只跑 quick 源码卫生门禁（quick 本身不构建、不测试）：不执行游戏编译（`game-client`），不运行测试（`testrunner` / `run_cxx_tests` / `run_rust_tests`）。编译与测试验证留给提交前 `--mode default`、准发布 `--mode full`，或用户当次明确要求时再执行。
- 核心逻辑改完后进行只读代码审查，先列 findings，再给结论。
- 汇报：写清改动、验证命令与结果、gaps。没跑的不说通过。

### 提交 commit / PR 前（用户要求提交时）

- 标题使用 `<type>(<scope>): <中文简述>`。
- 工作树可脏（多任务并行）；默认一次提交，仅用户要求或边界清晰时再拆。
- review findings 与 gate 证据先收口；受保护分支默认走 PR，勿直推。

### 发版（用户要求发新版本时）

1. `python3 qmclient_scripts/bump_version.py --tag vX.Y.Z`（Windows 使用 `py -3` 或 `python`）
2. 提交：`chore: bump version to X.Y.Z`
3. `git tag vX.Y.Z && git push origin vX.Y.Z`
4. CI 构建并调用 `generate_release_notes.py`（**自动汇总并润色**，不调外部 AI）发布 GitHub Release
5. **Nightly 以脚本输出为终稿，无需人工润色**；Stable 可选手动再改。细则见 `docs/RELEASE_NOTE_TEMPLATE.md` §0。

## 全局硬约束

- 仓库即记录系统：决策、计划、状态、证据、交接写入版本化文件。
- 一次一个功能；范围 = 用户请求 + 有效 plan/spec。歧义先问清。
- 客户端进程只能操作当前工作区开发目录下启动的实例。除非用户在当次明确授权，不得启动、关闭、重启或杀死工作区外的客户端进程；尤其不得把 `/Applications` 或其他安装目录中的正式客户端当作开发实例。
- 改行为前读真实代码；优先本地模式与 DDNet 兼容，不套泛化「现代 C++」。
- 有 codegraph 类图谱工具时优先用其取上下文。
- 无明确批准：不改协议、demo/skin 格式、物理、预测、碰撞、地图行为、rank 可达性、既有玩法语义。
- 补丁聚焦；不重写无关上游；小改不动大抽象。
- QmClient 特有工作优先落在 `src/game/client/components/qmclient/`、`src/game/client/QmUi/`、Qm 配置头、翻译、文档、metadata、`qmclient_scripts/`。
- 超出范围需批准：引擎核心、服务端玩法、地图编辑器、第三方库、CI release、协议字段、snapshot/输入/时序/回放语义等。
- 默认不改根 `CMakeLists.txt`、协议字段、序列化布局、文件格式定义（任务明确要求除外）。
- 配置项前缀 `qm_` / `Qm`，不用 `cl_`。
- 完整功能/改进后按 MMP 更新版本（纯调查/纯文本输出除外）。
- 新功能与较大行为改动默认先讨论。
- 不交付空模块、空文档、stub、「以后再决定」。
- 默认 TDD：失败测试 → 最小实现 → 整理。
- UTF-8；保留原 BOM；保持原换行（CRLF/LF）与缩进。
- commit/PR 标题使用 `<type>(<scope>): <中文简述>`；`FEAT`/`FIX`/`DEL` 可用于 body 与汇报分组。
- 文档路径统一前斜杠 `/`。
- 代码注释用中文。

## 构建与命令（摘要）

- 脚本分层与推荐入口见 `qmclient_scripts/scripts_overview.md`。
- i18n：`extract_strings` → `generate_all` → `validate` → `review_duplicate_entries`；维护源 `translations/i18n/*.toml`，`data/languages/*.txt` 为产物。
- Windows 构建入口：`qmclient_scripts/cmake-windows.cmd`；目录 `cmake-build-debug` / `cmake-build-release`。例：`cmd /c qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14`。
- 同 build 目录内 `game-client` / `testrunner` / `run_cxx_tests` / `run_rust_tests` / `package_default` **串行**。
- 日志与临时文件放 `tmp/`，勿堆仓库根。

## 十二原则：软件工程

非简单任务宁可慢一点、更谨慎；简单任务勿过度流程化。

1. **写前想清楚** — 说假设；歧义先问；有更简做法主动指出。
2. **简单优先** — 最少代码；不为一次性用法加抽象。
3. **精准修改** — 只改必须改的；不顺手「优化」邻域。
4. **目标导向** — 先定义完成标准再迭代。
5. **模型做判断** — 分类/起草/总结；确定性转换交给代码。
6. **Token 预算** — 单任务约 4k、会话约 30k 为警戒；将超限时先总结。
7. **暴露冲突** — 二选一并说明，不折中混用。
8. **写前先读** — 导出、调用方、共享工具。
9. **测试验意图** — 业务变了测试应失败。
10. **检查点** — 重要步骤后总结做了/验了/还剩什么。
11. **遵循仓库约定** — 一致性优先；反对约定要明说。
12. **失败说清楚** — 跳过项不得称完成；默认暴露不确定性。
