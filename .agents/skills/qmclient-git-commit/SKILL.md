---
name: qmclient-git-commit
description: 用户要求 git commit、PR、版本更新或 Release 时使用；维护提交文案与发布边界，不用于普通最终回复。
---

# QmClient Git 与发布

## 操作范围

按用户已授权的步骤执行。“提交”不自动授权推送、开 PR、合并或删除分支；写文案不授权 Git 操作。已授权的步骤无需重复确认。

工作树可包含并行任务：检查 diff，只暂存本次改动。默认一个聚焦提交，只有用户要求或边界清晰时拆分。受保护分支遵守仓库权限，已授权推送时优先分支/PR，不绕过保护。

## Commit

标题用 `<type>(<scope>): <中文简述>`，例如 `fix(hud): 修复通知栏锚点错误`。常用 type 使用 `feat`、`fix`、`perf`、`refactor`、`docs`、`test`、`chore`、`ci`、`revert`；scope 用短英文模块名；`build`、`style`、`improve` 等其他受支持类型以 `check_commit_msg.py` 的 `KNOWN_TYPES` 为准。

默认带简短 body：说明实际问题、变更行为及必要验证；多类独立改动才按类型分组，不放空组或完整日志。

特殊标题：

- merge 保留 Git 默认 message。
- 上游同步用 `chore(sync): <中文简述>`。
- cherry-pick 沿用类型规范，可保留来源尾注。
- 版本提交用 `chore: bump version to X.Y.Z`。

提交消息校验入口为 `qmclient_scripts/check_commit_msg.py`；不要为了文案重新定义其规则。

## PR

采用仓库 [.github/pull_request_template.md](../../../.github/pull_request_template.md)，删除不适用的分组、示例检查和占位。模板中的示例命令不扩大验证范围，实际检查由 `qmclient-verification-gate` 决定。

标题与最终 squash commit 风格一致。正文让未读对话的 reviewer 看懂问题、最终行为、验证与真实限制。只勾选已执行项；范围变化后同步重写标题和正文。

## 版本与 Release

功能交付或用户要求版本更新时，统一通过 `python qmclient_scripts/bump_version.py --version X.Y.Z` 或 `--tag vX.Y.Z`；不手改版本源。`CLIENT_RELEASE_VERSION` 源自 `QMCLIENT_VERSION`。纯文档与规则维护不升客户端版本。

发布任务再读 [release.md](references/release.md)，并按 `docs/RELEASE_NOTE_TEMPLATE.md` 的通道与说明规范执行。版本参数使用本次实际目标，先以 `--dry-run` 核对版本解析（该选项不展示文件 diff），实际更新后检查目标文件差异；发版脚本或生成逻辑变动按风险补验证。

提交前验证由 `qmclient-verification-gate` 统一决定，已完成的相关测试证据可复用。普通最终回复遵循根 `AGENTS.md`，不套本 skill 的提交结构。
