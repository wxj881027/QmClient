# Release 操作参考

仅发布、版本管理或 Release 说明任务读取。下面列出操作顺序，不产生额外授权；执行范围遵循父 skill。

1. 用 `python qmclient_scripts/bump_version.py --tag vX.Y.Z --dry-run` 核对版本解析（不展示待修改文件 diff），再去掉 `--dry-run` 更新。
2. 验证版本差异及发布要求，提交标题为 `chore: bump version to X.Y.Z`。
3. 已授权发布时核对目标仓库的 workflow 触发条件与发布权限，再创建对应 tag 并推送；不要顺带推送其他分支或 tag。

说明由 `qmclient_scripts/generate_release_notes.py` 确定性生成和润色，不调用外部 AI。具体规范见 [RELEASE_NOTE_TEMPLATE.md](../../../../docs/RELEASE_NOTE_TEMPLATE.md)。

- Stable 使用 `vX.Y.Z`，生成普通 Release；脚本输出应面向玩家，可选人工润色。
- Nightly 使用 `.github/workflows/nightly.yml` 对应流程，脚本输出即终稿。生成器支持预发布说明不等于 CI 支持任意 tag：当前 `build.yml` 的版本校验仅接受 v/V 开头的数字版本，不能假定 rc/beta tag 可直接发布。
- 输出按 scope 对应功能领域分组；必要时在 commit body 写 `Release-ZH: 中文发布说明`。
- 正式 tag 发布后不强推，修正发新版本。Nightly tag 的覆盖仅属于已授权的 Nightly 发布流程。
- tag 已存在但 Release 缺失时，重复 push 不会重新触发构建；检查该 tag 的 Actions 并按授权重跑 `build.yml`。

生成 Stable 说明时使用实际版本替换 `vX.Y.Z`：

```text
python qmclient_scripts/generate_release_notes.py --version vX.Y.Z --current-tag vX.Y.Z --channel auto
```

Nightly 使用 `--version nightly --current-tag nightly --channel pre-release`；commit、branch、built-at 使用实际构建信息。不要为普通规则修改运行发版命令。
