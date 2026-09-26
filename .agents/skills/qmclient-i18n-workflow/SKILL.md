---
name: qmclient-i18n-workflow
description: 修改翻译、Localize 文本、配置说明或语言生成脚本时使用；维护 TOML 来源、生成链、草稿审核及实际覆盖规则。
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

⚠️ 缓存**已经失效**时，增量 extract 每跑一次只会多补少量 key，反复跑不收敛（validate 会一直报 `extracted_strings.txt is out of date`）。此时直接 `extract_strings.py --full` 重建，再跑一次增量确认输出无变化，才算收敛。

⚠️ Windows 沙箱内 `path.write_text()`（截断式 open）会**间歇性**抛 `OSError: [Errno 22] Invalid argument`；同一进程内连续写第 2~3 个**已存在**文件时最容易触发（与文件、目录、内容、写入次数都无稳定关系，`write_bytes`/`os.replace` 不受影响）。**单次写入的进程稳定成功**，所以 extract/generate/回填都按最小单位拆进程并重试：`generate_all.py` 没有命令行参数，用一个临时驱动调 `generate_all.generate_configured_languages(generate_all.read_strings(), [language])`，每次只生成一个语言；回填按 `--languages <一种> --modules <一个模块>` 调用。

## 覆盖与质量

覆盖规则以 `validate.py`、`generate_all.GENERATED_LANGUAGES` 和 `i18n_store.english_fallback_identities` 为准：普通 active key 缺译会失败；既有双语回退模块的条目允许非简中语言回退英文。运行时可回退不等于验证允许任意缺译。

局部任务不自动补译无关历史缺口，也不硬编码“12 语全部补齐”。本次新增 key 按所属模块满足现有门禁；若范围外缺口阻断验证，报告来源与影响，不改门禁或把条目移入回退模块来绕过校验。

数字、占位符与 identity 完整性继续校验。跨模块同一 identity 按 first-wins 解析且 integrity 报错；确认归属后合并到正确维护模块。数字兼容逻辑与迁移检查见 `qmclient-i18n-audit`。

## 模型补译（任务需要时）

用 `translate_with_local_http.py --languages <目标语言> --modules <目标模块>` 限定生成范围，先用 `--dry-run` 核对候选；补缺不加 `--rewrite`。使用已有且获授权的服务配置，具体参数以脚本为准。

审核草稿中的语义、术语、数字和占位符后，对同一语言和模块显式 `--write-back` 回填；该命令处理所选模块中的适用草稿，运行前核对其中所有待写条目，不能假定只写刚审核的一条；用户已授权补译时，审核由当前任务完成，不另设人工确认步骤。只 patch 相关 message block，不重排或重写整个模块。最后生成产物并验证。

⚠️ 写草稿前必须用 `translate_with_local_http.language_quality_failure(language, source, translation, terminology=...)` 预检。`--write-back` 只接收通过质量检查的草稿，不合格条目被静默跳过，随后 `prune_written_draft_module` 还会把它们从草稿文件里删掉，表现为「命令跑完但没写进去」。典型不合格：译文引入了源串没有的 ASCII 数字（日文「2回」对应 "Double-tap" 会触发 digit mismatch）；译文与源串逐字相同且不在 `SAME_SOURCE_ALLOWED_BY_LANGUAGE` / `may_keep_source_text` 白名单内（如法语 "Direction"、西班牙语 "Visual: Cursor"）。术语校验只对 `simplified_chinese` 生效。

API 不可用时改为离线手工补译：直接按上述格式写 `translations_draft/<语言>/<模块>.toml`（用 `i18n_store.dump_module` 生成、用 `translate_with_local_http.load_existing_draft_module` 合并既有条目，保证格式与脚本一致），再走同一条 `--write-back`。该命令不需要 API key。
