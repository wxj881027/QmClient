# Release 操作参考

仅发布、版本管理或 Release 说明任务读取。下面列出操作顺序，不产生额外授权；执行范围遵循父 skill。

1. 按 [版本号与发布通道](../../../../docs/归档/版本号与发布通道.md) 选择正式 `vX.Y` 或预览 `vX.Y-preview.N`。用 `python qmclient_scripts/bump_version.py --tag <目标 Tag> --dry-run` 核对解析，再去掉 `--dry-run` 更新。
2. 验证版本差异及发布要求，提交标题使用解析后的版本，例如 `chore: bump version to 3.3`。
3. 已授权发布时核对目标仓库的 workflow 触发条件与发布权限，再创建对应 tag 并推送；不要顺带推送其他分支或 tag。

说明由 `qmclient_scripts/generate_release_notes.py` 确定性生成和润色，不调用外部 AI。当前通道和说明结构以 [generate_release_notes.py](../../../../qmclient_scripts/generate_release_notes.py) 及对应 workflow 为准。

- 两种通道共用 `build.yml`，`.github/workflows/nightly.yml` 仅负责预览调度。生成器兼容历史 tag 不代表新发布允许旧格式。
- 输出按 scope 对应功能领域分组；必要时在 commit body 写 `Release-ZH: 中文发布说明`。
- 公开发布前确认四平台附件和签名齐备；草稿上传完成后再公开。历史版本的删除、Tag 保留和更新顺序遵循版本规则文档。
- tag 已存在但 Release 缺失时，重复 push 不会重新触发构建；检查该 tag 的 Actions 并按授权重跑 `build.yml`。

生成说明时使用实际目标 Tag，并明确指定上一已公开正式版：

```text
python qmclient_scripts/generate_release_notes.py --version vX.Y --current-tag vX.Y --previous-tag <上一正式 Tag> --channel auto
```

预览版使用真实的 `vX.Y-preview.N`，commit、branch、built-at 使用实际构建信息。不要为普通规则修改运行发版命令。
