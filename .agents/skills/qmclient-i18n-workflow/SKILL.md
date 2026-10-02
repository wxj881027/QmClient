---
name: qmclient-i18n-workflow
description: 修改或审计翻译、Localize 文本、配置说明及语言生成链时使用；维护 TOML 来源、草稿质量、覆盖策略，并按需排查污染、迁移和历史漂移。
---
# QmClient 翻译工作流

## 维护源与文本

- 维护源：`qmclient_scripts/languages_qmclient/translations/i18n/*.toml`。
- 运行时产物：`data/languages/*.txt`，由 `generate_all.py` 生成，不手改。
- 模型草稿：`qmclient_scripts/languages_qmclient/translations_draft/<语言>/*.toml`，审核后才回填。
- `Localize` / `Localizable`、`Register` help、`MACRO_CONFIG_*` 的 Desc 与可翻译默认文案使用英文 source；其他语言写 TOML。
- Desc 对应 `m_pHelpLocalizeKey`，由 UI 本地化。可配置默认文案在展示/发送前按现有调用方式本地化，自定义内容无译文则保留原文。

## 生成与验证

从仓库根运行，Windows 用 `python` 或 `py -3`，Linux/macOS 用 `python3`：

```text
python qmclient_scripts/languages_qmclient/extract_strings.py
python qmclient_scripts/languages_qmclient/generate_all.py
python qmclient_scripts/languages_qmclient/validate.py
python qmclient_scripts/languages_qmclient/review_duplicate_entries.py --show-groups 0 --show-unused 0
```

源码 key 变化时按此顺序运行；仅译文变化且提取结果仍新鲜时可复用提取结果。只读审查不运行会更新文件的 extract/generate。

extract 默认增量维护完整 active keys；缓存失效或严格重建时才用 `--full`。validate 默认重新扫描新鲜度，本地快速校验可显式用 `--incremental`；不得把增量验证说成完整重扫。

validate 提示提取结果过期且增量提取不能消除差异时，使用 `extract_strings.py --full` 重建并重新验证，不反复运行未收敛的增量命令。写入故障按实际错误处理，针对性的隔离方法见 [审计参考](references/audit.md)。

## 覆盖与质量

覆盖规则以 `validate.py`、`generate_all.GENERATED_LANGUAGES` 和 `i18n_store.english_fallback_identities` 为准：普通 active key 缺译会失败；既有双语回退模块的条目允许非简中语言回退英文。运行时可回退不等于验证允许任意缺译。

局部任务不自动补译无关历史缺口，也不硬编码“12 语全部补齐”。本次新增 key 按所属模块满足现有门禁；若范围外缺口阻断验证，报告来源与影响，不改门禁或把条目移入回退模块来绕过校验。

数字、占位符与 identity 完整性继续校验。跨模块同一 identity 按 first-wins 解析且 integrity 报错；确认归属后合并到正确维护模块。数字兼容逻辑、配置 Desc 迁移和历史漂移检查见 [审计参考](references/audit.md)。只读审计仅运行对应只读命令，不调用模型或改写产物。

## 模型补译（任务需要时）

用 `translate_with_local_http.py --languages <目标语言> --modules <目标模块>` 限定生成范围，先用 `--dry-run` 核对候选；补缺不加 `--rewrite`。使用已有且获授权的服务配置，具体参数以脚本为准。

审核草稿中的语义、术语、数字和占位符后，对同一语言和模块显式 `--write-back` 回填；该命令处理所选模块中的适用草稿，运行前核对其中所有待写条目，不能假定只写刚审核的一条；用户已授权补译时，审核由当前任务完成，不另设人工确认步骤。只 patch 相关 message block，不重排或重写整个模块。最后生成产物并验证。

写草稿前使用 `translate_with_local_http.language_quality_failure(...)` 预检，回填后核对维护源，避免不合格条目被跳过或清理。具体数字、原文同值与术语规则见 [审计参考](references/audit.md)。

API 不可用时改为离线手工补译：直接按上述格式写 `translations_draft/<语言>/<模块>.toml`（用 `i18n_store.dump_module` 生成、用 `translate_with_local_http.load_existing_draft_module` 合并既有条目，保证格式与脚本一致），再走同一条 `--write-back`。该命令不需要 API key。
