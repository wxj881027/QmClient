> 请抬头享受阳光｜日子很好 我很我---------致咩子
# scripts_overview

这个文档统一说明 `qmclient_scripts/` 的脚本分层、推荐入口，以及 `check_gate.py` 相关工作流语义。

目标：

- 给 agent 一个单一脚本说明入口
- 不再把“脚本总览”和“gate 工作流说明”拆成两份重复文档

## 脚本分层

### `scripts/` 与 `qmclient_scripts/` 边界

根目录 `scripts/` 视为 DDNet 上游同步目录。QmClient 自有逻辑、gate 入口、生成脚本和检查脚本都放在 `qmclient_scripts/`，不要在 gate 或 GitHub workflow 中直接调用 QmClient 修改过的 `scripts/` 文件。

需要复用上游脚本时，先把脚本复制到 `qmclient_scripts/`，在复制件上做 QmClient 适配；不要让 `qmclient_scripts/` 里的脚本反向依赖根目录 `scripts/` 的 QmClient 特化行为。唯一例外是明确保持上游原样的脚本或平台入口，例如 `scripts/android/cmake_android.sh`。

### 1. `gate/`

这是仓库级门禁与脚本治理层。

当前规范入口：

| 入口 | 说明 |
|------|------|
| `qmclient_scripts/gate/check_gate.py` | Python 版仓库级门禁总入口 |
| `checks/strict_build` | 严格构建与静态分析（`check_gate.py` 调度模块） |
| `qmclient_scripts/gate/check_settings_ui_migration.py` | P5 标准设置页统一 UI 迁移结构清单（按页面或全量） |
| `qmclient_scripts/gate/baseline_debt_allowlist.json` | 基线白名单数据 |

适用：

- 跑仓库级 `quick/default/full` 门禁
- 跑严格构建、`/analyze`、clang-tidy、ASan
- 维护 baseline debt allowlist

### 2. 构建与平台辅助

这类脚本负责让构建或平台行为成立，不是门禁总入口。

当前主要脚本：

- `qmclient_scripts/cmake-windows.cmd`
- `qmclient_scripts/darwin_fix_install_names.py`
- `qmclient_scripts/make_lib_openssl.sh`
- `qmclient_scripts/cmake-windows-filter.py` — 过滤 Windows/MSVC 构建日志噪音（如"注意: 包含文件:"前缀）
- `qmclient_scripts/repair_ninja_msvc_prefix.py` — 修复 Ninja + MSVC 下的依赖前缀编码（configure/build 通用）
- `qmclient_scripts/preview-crash-dialog.cmd [build-dir] [graphics|assertion|fatal|hang]` — 不启动完整客户端、不制造真实崩溃，预览 Windows 喜庆崩溃窗口；默认使用 `cmake-build-release` 和 `graphics`。窗口内的“放烟花”按钮只在预览窗口中播放 GDI 烟花动画，不改变报告结果。

### 3. 代码卫生与内容生成辅助

这类脚本是具体检查项或生成项，不负责仓库级编排。

当前主要脚本：

- `qmclient_scripts/check_config_variables.py`
- `qmclient_scripts/check_header_guards.py`
- `qmclient_scripts/bump_version.py`
- `qmclient_scripts/fix_style.py`
- `qmclient_scripts/export_settings_commands_table.py`
- `qmclient_scripts/generate_release_notes.py`

### 4. 其他专用脚本

与门禁主链无直接关系，按各自职责独立存在：

- `qmclient_scripts/integration/`：QmClient 自有客户端/服务端进程级集成与冒烟测试；不得把 QmClient 特化场景加入根目录上游 `scripts/`
- `qmclient_scripts/coverage/`：使用独立 clang/Ninja 构建采集 C++ LLVM line/function/region/branch coverage

- `qmclient_scripts/languages_qmclient/`
- `qmclient_scripts/qmclient_center_server/`
- `qmclient_scripts/diff_update.py`
- `qmclient_scripts/tw_api.py`
- `qmclient_scripts/update.zsh`：旧增量更新服务部署脚本；必须显式设置 `QM_UPDATE_SCRIPTS_DIR` 与 `QM_UPDATE_OUTPUT_DIR`，可用 `QM_UPDATE_RELEASE_REPOSITORY` 覆盖发布仓库

### 5. 独立开源的服务仓库

`qmclient_scripts/` 下另有一组**独立 Git 仓库**（各自有独立 remote，已被根 `.gitignore` 排除，不并入主仓库）：

| 目录 | 仓库 | 语言 |
|------|------|------|
| `qmclient_scripts/qmclient_center_server/` | [wxj881027/qmclient-center-server](https://github.com/wxj881027/qmclient-center-server) | Node.js |
| `qmclient_scripts/qmclient_voice_server/` | [wxj881027/qmclient-voicesrv](https://github.com/wxj881027/qmclient-voicesrv) | Rust |
| `qmclient_scripts/qmclient_titles/` | [wxj881027/qmclient-titles](https://github.com/wxj881027/qmclient-titles) | Node.js |

分工、端口与部署约定见 `qmclient_scripts/SERVICES.md`。这些目录只做源码与部署配置，**不含运行期数据**（游玩时长、新闻内容、称号兑换码等），密钥一律通过 `EnvironmentFile` 注入。

`qmclient_scripts/languages_qmclient/` 语言脚本入口：

测试层级与高债务测试统计：

```text
python qmclient_scripts/test_inventory.py
python qmclient_scripts/test_inventory.py --json > tmp/test-inventory.json
```

其中 `python_unit_files` / `python_unit_cases` 只统计单元脚本，`python_integration_files` / `python_integration_cases` 单独统计 `qmclient_scripts/integration/test*.py`；`process_smoke_runners` 表示真实进程 runner 数量，`process_smoke_scenarios` 表示 runner 注册的场景数量；`e2e_scenarios` 只统计独立的完整端到端场景，不能用 smoke 数量替代。该统计按文件命名和测试目录划分层级，源码合同引用与运行时行为必须结合人工审查；统计结果不能替代 line、function 或 branch coverage 报告。

QmClient 真实进程 smoke/E2E（需要已构建的 `DDNet` 与 `DDNet-Server`）可按场景运行：

```text
python qmclient_scripts/integration/qmclient_smoke.py <build-dir> [plain_connection|focus_configuration|gores_configuration|connection_shutdown]
python qmclient_scripts/integration/e2e_qmclient.py <build-dir> [connection_failure_recovery|demo_recording|invalid_statistics_preserved|perf_log_persistence|qm_lifecycle_persistence|recording_without_connection|startup_saved_favorites]
```

gate 使用 `--run-qm-smoke` 时会在同一进程测试 check 中顺序执行 smoke 与 E2E。

- `source_keys.py`：共享源码 key 提取器，支持全量扫描与 Git diff 增量合并，提取 `Localize` / `Localizable`、`Register` help 和 QmClient 间接 key
- `extract_strings.py`：默认按 Git diff 增量更新完整 `extracted_strings.txt`、`extracted_records_cache.json` 和 `extracted_audit_report.json`；传 `--full` 时重扫源码并重建缓存；active key 清单继续只承载 i18n 主链 source key，审计报告另外输出 `must_i18n`、`business_data`、`test_only`、`needs_review`、`violation`
- `translations/i18n/*.toml`：按代码模块拆分的翻译维护源；单条记录可同时维护多语言翻译，不要求全量语言留空
- `generate_all.py`：从当前源码 key 和模块化 TOML 维护源生成 `generate_all.GENERATED_LANGUAGES` 中登记的 `data/languages/*.txt`，缺失时回退英文 key
- `review_duplicate_entries.py`：只读审查重复、相似、空译文和疑似未使用项；unused 直接按最终 active source key 集合判断，避免 context 漂移误报
- `audit_translation_drift.py`：只读对比当前 `translations/i18n/*.toml` 与 Git 历史里的 `data/languages/simplified_chinese.txt`，用于审查历史译法是否被新维护源改偏；默认基线为 `HEAD`
- `translate_with_local_http.py`：通过 OpenAI-compatible HTTP 接口生成翻译 draft；所有语言默认只写 `translations_draft/<language>/*.toml`，审核通过后才允许显式 `--write-back` 回填主 TOML 维护源；回填必须按审核通过的条目做 patch，不重写整份模块 TOML
- `validate.py`：默认重扫源码校验提取文件与审计报告新鲜度、生成产物覆盖、模块化 i18n store 可读性和 legacy overlay 删除状态；传 `--incremental` 时使用增量缓存做本地快速校验；`violation` 会返回失败，`needs_review` 只作为人工清理 backlog 提示

推荐 i18n 工作流：

1. 修改源码中的英文 key 或新增 `Localize` / `Localizable` / `Register` help 调用
2. 运行 `python3 qmclient_scripts/languages_qmclient/extract_strings.py`（默认增量；需要重建缓存时加 `--full`）
3. 按需更新 `qmclient_scripts/languages_qmclient/translations/i18n/*.toml`
4. 运行 `python3 qmclient_scripts/languages_qmclient/generate_all.py`
5. 运行 `python3 qmclient_scripts/languages_qmclient/validate.py` 与 `python3 qmclient_scripts/languages_qmclient/review_duplicate_entries.py --show-groups 0 --show-unused 0`

说明：

- `data/languages/simplified_chinese.txt` 是运行时生成产物，不再作为手工维护的长期真相源。
- `translations/i18n/*.toml` 才是翻译维护源；按代码模块拆分，单条记录可带多语言翻译，未填写的语言在生成时回退英文 key。
- `translations_draft/<language>/*.toml` 是 HTTP 模型生成的草稿维护源，用于人工审阅或后续回填，不参与运行时生成链；所有语言都先走 draft，回填必须显式使用 `--write-back`，且只 patch 审核通过的目标条目。
- 新增英文 source key 后，推荐流程是：`extract_strings.py` 提取 key -> `translate_with_local_http.py --languages ...` 生成多语言 draft -> 人工审核 draft -> `--write-back` 回填主 TOML -> `generate_all.py` 生成运行时语言文件 -> `validate.py` 验证。
- 生成 draft 补缺时不要传 `--rewrite`；它会把已有译文也纳入任务，导致接近全量重翻。`--timeout` 只控制单个 HTTP 请求，不控制整条命令总时长；远端大批量翻译应按语言或模块分段运行，避免被外层任务超时直接杀掉。
- 字符串分类按职责判断，不再按“是不是中文”判断是否漏翻译：客户端自有展示文案进入 i18n；兼容匹配/解析字面量留在业务层；测试样本文本只留测试。
- 当需要核对“当前 TOML 是否偏离项目原有简中口径”时，运行 `audit_translation_drift.py`。它是历史译法审计工具，不参与运行时生成链，也不阻断 `validate.py`。

## 推荐入口

macOS/Linux 使用 `python3`；Windows 使用 `py -3` 或已配置的 `python`。

### 仓库级门禁

```bash
python3 qmclient_scripts/gate/check_gate.py --mode quick
python3 qmclient_scripts/gate/check_gate.py --mode default
python3 qmclient_scripts/gate/check_gate.py --mode full
```

### P5 设置页迁移结构清单

```bash
python3 qmclient_scripts/gate/check_settings_ui_migration.py --page general
python3 qmclient_scripts/gate/check_settings_ui_migration.py --all
```

该清单只核对 P5 的页面结构契约：统一 layout/card/deck/scroll/input 路径、旧路径删除，以及 card registry 和搜索导航目标。页面尚未迁移时 `--all` 预期失败；每个页面切片完成后先跑对应 `--page`，P5 收口时再跑 `--all`。

### GitHub Release 说明

```bash
python3 qmclient_scripts/generate_release_notes.py --version vX --current-tag vX --output tmp/release-notes.md
```

### 版本号收口

```bash
python3 qmclient_scripts/bump_version.py --version X[.Y[.Z]]
python3 qmclient_scripts/bump_version.py --dev-version X.Y.Z
python3 qmclient_scripts/bump_version.py --tag vX[.Y[.Z]]
```

### baseline allowlist

```bash
python3 qmclient_scripts/gate/check_gate.py --mode quick --report-json-path tmp/check-gate-report.json
python3 qmclient_scripts/gate/tools/refresh_allowlist.py --report tmp/check-gate-report.json --output qmclient_scripts/gate/baseline_debt_allowlist.json
```

说明：

- `refresh_allowlist.py` 是人工确认后的维护工具，不会被 gate 自动调用
- 先看 JSON 报告，再决定是否增量合并或 `--rewrite` 全量重写

## `check_gate.py` 工作流语义

### 角色

`qmclient_scripts/gate/check_gate.py` 是仓库级总入口。

它负责：

- 把源码卫生检查、严格调试检查、测试、allowlist 与 JSON 报告收口成统一工作流
- 通过声明式检查注册表统一 mode、skip、scope 和特殊参数，避免在主流程按检查名分支
- 区分“已知历史债务”和“当前新增阻断”

### 模式

#### `quick`

- 开发期快速自查
- 不跑真实构建
- 不跑测试

默认内容：

- 配置变量使用检查
- 头文件 guard 检查
- 标准头文件检查
- `fix_style.py -n`
- 改动触及统一设置页文件时，运行 P5 设置页迁移结构清单
- 构建或测试前检查递归 Git 子模块是否已经初始化

#### `default`

- 日常提交前严格门
- 跑 quick 层源码卫生检查、C++ 全量测试和 Rust 全量测试

默认内容：

- `quick` 全部
- `run_cxx_tests`（会构建 `testrunner`，并在 build 目录下执行测试二进制；源码结构测试通过测试源码根读取 `src/...` / `data/...`）
- `run_rust_tests`

#### `full`

- 集中收口 / 准发布门
- 在 `default` 基础上增加更重检查，不作为“全量测试”的默认入口

默认内容：

- `default` 全部
- `checks/strict_build`（严格构建与静态分析只属于 full gate）
- `checks/dilate`
- 标识符命名检查
- clang-tidy warn 等附加检查（按当前 gate 开关和 mode 配置）

### 默认构建口径

- 运行/测试目录默认是 `cmake-build-release`，Windows MSVC Release 构建默认生成 PDB 符号文件用于崩溃符号化
- 严格调试检查目录默认是 `cmake-build-debug` / `cmake-build-analyze`
- Windows 默认通过 `qmclient_scripts/cmake-windows.cmd` 进入 CMake
- 在 Windows 宿主上验证 Linux 构建时，推荐通过 WSL Ubuntu + GCC/G++ + CMake + Ninja 走原生 Linux 口径，并使用独立目录（例如 `cmake-build-linux-release`），不要复用 Windows 的 `cmake-build-release`
- 同一 build 目录里的 `game-client`、`testrunner`、`run_cxx_tests`、`run_rust_tests`、`package_default` 必须串行执行，不要并行调度；并行验证只能通过拆分到不同 build 目录实现

### 结果分类

`check_gate.py` 的失败应至少能区分为：

- `环境/工具`
- `仓库基线债务`
- `当前改动/构建阻断`

JSON 与控制台报告同时区分 `RUN`、显式 `SKIP` 和按范围 `NOT_APPLICABLE`。必需检查被显式跳过时，结果标记为 `DEGRADED`，不能表述为完整 mode 通过；`--dry-run` 标记为 `DRY_RUN`。报告写入失败属于 gate 失败，必须返回非零退出码。

scope 同时保留全部改动路径和首方 C/C++ 子集：前者用于按路径触发翻译、文档、设置页等专项检查，后者继续用于格式、命名和 clang-tidy 等 C/C++ 检查。

### 常用命令

```bash
python3 qmclient_scripts/gate/check_gate.py --mode default --explain-scope --report-json-path tmp/check-gate-report.json
```

## 不要这样用

- 不要把 `check_config_variables.py`、`fix_style.py`、`check_header_guards.py` 误当成仓库级总入口
- 不要绕开 `qmclient_scripts/gate/check_gate.py` 自己临时拼一套等价门禁
- 不要把 `qmclient_scripts/` 根目录当成完全平级；门禁相关内容统一以 `gate/` 为准
- 不要在 QmClient gate 或 workflow 中直接调用根目录 `scripts/` 下的 QmClient 特化脚本；改用 `qmclient_scripts/` 复制件
