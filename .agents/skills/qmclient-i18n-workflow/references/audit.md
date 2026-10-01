
# QmClient 翻译审计

维护路径、授权、生成与覆盖要求遵循 [父 skill](../SKILL.md)。先按用户指定语言、模块或问题检查；只读请求交付 findings，已有修复授权则验证后继续处理。

## 检查点

- `source_keys.CONFIG_MACRO_HELP_HEADERS` 将 `src/engine/shared/config_variables.h`、`config_variables_qmclient.h`、`config_variables_tclient.h` 分别映射到 menus、qmclient、tclient。核对完整相对路径匹配，避免后缀匹配扩大提取范围。
- Desc 必须是英文 source，旧中文保留为简中译文；迁移还需核对 TOML identity 和调用点。
- `translate_with_local_http.py` 的 `digits_compatible` 按多重集合比较：源码数字及其出现次数必须保留；允许额外出现源码中 zero…ten 对应的数字及次数。不是普通集合，也不是将所有英文数字词转成数字后要求两边相等。
- 检查重复 identity、数字/占位符破坏、历史术语漂移；警告按具体含义判断，不将所有 WARN 一律升级或忽略。
- 全语言覆盖审计采用生成脚本实际语言列表与回退规则；三头文件 CJK 清零仅用于明确的全量迁移验收。

## 按需命令

历史译法只读审计：

```text
python qmclient_scripts/languages_qmclient/audit_translation_drift.py --git-ref HEAD
```

修改提取、迁移或质量校验逻辑时运行相关测试；共享规则变化运行语言脚本测试入口：

```text
python -m unittest discover qmclient_scripts/languages_qmclient/tests
```

历史中文 Desc 迁移用 `migrate_cjk_config_help.py`；先核对映射再使用 `--apply`。映射文件在 `qmclient_scripts/languages_qmclient/translations/_migrations/`，`--map` 相对路径按脚本目录解析。`cjk_config_help_map.json` 对应 qmclient/tclient，`cjk_config_help_map_ddnet.json` 对应主配置头。

报告列出确定缺陷、范围内覆盖和实际失败项。已存在的范围外缺译单独说明，不因审计自动调用模型批量补齐。

## 草稿回填边界

写草稿前用 `translate_with_local_http.language_quality_failure(language, source, translation, terminology=...)` 预检；`--write-back` 只接收合格条目，后续草稿清理也可能移除不合格条目，不能以命令完成代替核对维护源。

典型失败包括译文引入源串不允许的 ASCII 数字（如 "Double-tap" 对应日文「2回」），以及译文逐字等于源串但不在 `SAME_SOURCE_ALLOWED_BY_LANGUAGE` / `may_keep_source_text` 许可范围内。术语校验仅对 `simplified_chinese` 生效；保留失败原因并核对最终 TOML 与生成结果。

## 环境故障

如果 Windows 写文件实际出现 `OSError: [Errno 22] Invalid argument`，先确认失败文件、权限和调用位置，不将历史沙箱现象作为每次任务都拆进程重试的要求。确认是连续写入故障时，可按单语言、单模块隔离执行：生成脚本没有语言命令行参数，可在 `tmp/` 下的驱动中调用 `generate_all.generate_configured_languages(generate_all.read_strings(), [language])`；回填使用 `--languages <一种> --modules <一个模块>`。重试后核对所有目标产物，不能隐藏持续失败。
