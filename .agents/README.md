# 项目 Agent Skills

项目级入口为 `.agents/skills/<name>/SKILL.md`，供本地 agent 工具共用。保留现有 skill 名称，避免同名全局 skill 抢占项目规则。

`AGENTS.md` 管全局授权、项目边界和任务路由；skill 管一项操作；`references/` 放按需命令或检查参考。任务计划与证据放 `docs/superpowers/`，不作为第二套通用工作流。

## 维护原则

- 每条规则只有一个维护位置，其他入口引用即可；引用不表示必须加载全部关联文件。
- description 说明触发任务，正文保留项目特有步骤、约束和完成条件，省略通用编程训诫。
- 参考文件不重新定义授权、并行、验证或回复格式；分别遵循根规则及相关 skill。
- 可机械判断的约束优先复用现有脚本；不为了缩短文档改变脚本门禁。
- 修改后核对入口、相对链接、规则一致性和代表任务的执行路径；不要把文本行数当实际 token 节省。

## 职责

| Skill | 维护内容 |
| --- | --- |
| `qmclient-cpp-conventions` | DDNet C++ 风格与专项风险路由 |
| `qmclient-verification-gate` | 风险分层、构建测试、gate 和证据复用 |
| `qmclient-git-commit` | commit/PR 文案、版本和发布操作 |
| `qmclient-code-review` | 缺陷证据、严重度和审查输出 |
| `qmclient-i18n-workflow` | 翻译维护源、生成链、覆盖策略 |
| `qmclient-i18n-audit` | 翻译污染、迁移和历史漂移检查 |
| `audit-qmclient-quality` | 深度质量审计及按需检查参考 |

质量审计的 `references/quality-scan-prompt.md` 是索引；功能、横向和测试能力参考按任务单独读取。C++ 专项仍在 `references/advanced/`，仅触发相关风险时加载。
