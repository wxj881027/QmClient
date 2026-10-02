# 项目 Agent Skills

项目级入口为 `.agents/skills/<name>/SKILL.md`，供本地 agent 工具共用。入口按操作职责组织；合并时同步迁移参考、agent 描述和任务路由，避免为同一操作保留多个规则来源。

`AGENTS.md` 管全局授权、项目边界和任务路由；skill 管一项操作；`references/` 放按需命令或检查参考。功能设计、方案和实施安排放 `docs/规格/`；历史调研、修复记录和被替代的方案放 `docs/归档/`。新增文档使用中文文件名，正文按实际内容组织，不套用 skill 模板，也不再创建工具或插件专属的文档目录。

测试书写由根 `AGENTS.md` 维护质量边界，`src/test/AGENTS.md` 提供目录内的执行入口；`src/test/benchmark/AGENTS.md` 路由到 verification skill 的性能参考。目录规则继承根规则，不另设验证、授权或性能证据标准。README 只提供导航，不承担必须读取的规则入口。

## 维护原则

- 每条规则只有一个维护位置，其他入口引用即可；引用不表示必须加载全部关联文件。
- description 说明触发任务，正文保留项目特有步骤、约束和完成条件，省略通用编程训诫。
- 参考文件不重新定义授权、并行、验证或回复格式；分别遵循根规则及相关 skill。
- 可机械判断的约束优先复用现有脚本；不为了缩短文档改变脚本门禁。
- 修改后核对入口、相对链接、规则一致性和代表任务的执行路径；不要把文本行数当实际 token 节省。

## 职责

| Skill | 维护内容 |
| --- | --- |
| `qmclient-cpp-conventions` | DDNet C++ 风格、共享 UI 职责与专项实现风险 |
| `qmclient-verification-gate` | 风险分层、构建测试、Google Benchmark、gate 和证据复用 |
| `qmclient-git-commit` | commit/PR 文案、开发/正式版本和发布操作 |
| `qmclient-code-review` | 缺陷证据、严重度、边界审查与按需深度质量审计 |
| `qmclient-i18n-workflow` | 翻译维护源、生成链、草稿质量、覆盖及按需迁移/漂移审计 |

深度质量材料归入 `qmclient-code-review/references/`，原 `audit-qmclient-quality` 不再作为独立 skill；翻译审计归入 `qmclient-i18n-workflow/references/audit.md`，原 `qmclient-i18n-audit` 不再作为独立 skill。历史任务文档中的旧名称只作当时的记录，不重新定义当前入口。

C++ 专项仍在 `references/advanced/`，仅触发相关风险时读取；两份旧性能参考合并为 `performance-workflow.md`，构建、过滤运行和性能证据统一由 verification skill 的 `references/performance.md` 维护。Google Benchmark 是代码性能验证的优先入口，`qmclient_scripts/perf/` 保留为真实客户端日志的离线诊断工具，常规验证不要求 HTML 报表。
