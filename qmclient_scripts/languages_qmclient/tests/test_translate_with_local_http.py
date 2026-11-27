# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from qmclient_scripts.languages_qmclient import local_http_client
from qmclient_scripts.languages_qmclient import translate_with_local_http


class LocalHttpClientTest(unittest.TestCase):
    def test_build_chat_payload_uses_openai_compatible_shape(self):
        payload = local_http_client.build_chat_payload(
            "local-model",
            [local_http_client.ChatMessage("system", "rules")],
            temperature=0.1,
        )
        self.assertEqual(payload["model"], "local-model")
        self.assertEqual(payload["messages"][0]["role"], "system")
        self.assertEqual(payload["temperature"], 0.1)

    def test_extract_response_text_reads_first_choice_message(self):
        text = local_http_client.extract_response_text(
            {"choices": [{"message": {"content": "[]"}}]}
        )
        self.assertEqual(text, "[]")

    def test_extract_response_text_prefers_content_when_reasoning_is_present(self):
        text = local_http_client.extract_response_text(
            {
                "choices": [
                    {
                        "message": {
                            "content": "[]",
                            "reasoning_content": "thinking trace",
                        }
                    }
                ]
            }
        )
        self.assertEqual(text, "[]")


class TranslateWithLocalHttpTest(unittest.TestCase):
    def test_parser_uses_deepseek_defaults(self):
        args = translate_with_local_http.build_parser().parse_args([])

        self.assertEqual(args.base_url, "https://api.deepseek.com")
        self.assertEqual(args.model, "deepseek-v4-flash")
        self.assertEqual(args.api_key, os.getenv("DEEPSEEK_API_KEY", ""))
        self.assertGreaterEqual(args.parallel_requests, 1)
        self.assertGreaterEqual(args.parallel_languages, 1)

    def test_parse_response_requires_json_array(self):
        parsed = translate_with_local_http.parse_response(
            '[{"key":"Play","context":"","translation":"开始"}]'
        )
        self.assertEqual(parsed[0]["translation"], "开始")
        with self.assertRaises(ValueError):
            translate_with_local_http.parse_response('{"key":"Play"}')

    def test_render_prompt_includes_target_language_terminology_only(self):
        tasks = [
            translate_with_local_http.TranslationTask(
                "menus", ("Grenade", ""), "Grenade"
            )
        ]
        terminology = """[[term]]
source = "Grenade"
simplified_chinese = "榴弹枪"
traditional_chinese = "榴彈槍"
"""

        prompt = translate_with_local_http.render_prompt(
            "simplified_chinese",
            "menus",
            tasks,
            prompt_assets=("# Rules", terminology, ""),
            store={},
        )

        self.assertIn("Terminology for Simplified Chinese:", prompt)
        self.assertIn("- Grenade => 榴弹枪", prompt)
        self.assertNotIn("[[term]]", prompt)
        self.assertNotIn("traditional_chinese", prompt)
        self.assertNotIn("榴彈槍", prompt)

    def test_render_prompt_includes_zhipu_ai_terminology(self):
        tasks = [
            translate_with_local_http.TranslationTask(
                "qmclient", ("Zhipu AI API Key", ""), "Zhipu AI API Key"
            )
        ]
        terminology = """[[term]]
source = "Zhipu AI"
enforce = "pattern"
simplified_chinese = "智谱清言"
"""

        prompt = translate_with_local_http.render_prompt(
            "simplified_chinese",
            "qmclient",
            tasks,
            prompt_assets=("# Rules", terminology, ""),
            store={},
        )

        self.assertIn("- Zhipu AI => 智谱清言", prompt)
        self.assertNotIn("{terminology}", prompt)

    def test_parse_terminology_keeps_enforcement_strategy(self):
        terminology = """[[term]]
source = "Grenade"
simplified_chinese = "榴弹枪"
enforce = "pattern"

[[term]]
source = "Spectate"
simplified_chinese = "旁观"
enforce = "prompt_only"
"""

        terms = translate_with_local_http.parse_terminology_terms(terminology)

        self.assertEqual(terms["simplified_chinese"]["Grenade"].translation, "榴弹枪")
        self.assertEqual(terms["simplified_chinese"]["Grenade"].enforce, "pattern")
        self.assertEqual(terms["simplified_chinese"]["Spectate"].enforce, "prompt_only")

    def test_maintained_simplified_chinese_terminology_covers_core_terms(self):
        terminology_path = (
            translate_with_local_http.PROMPT_ASSETS_DIR / "terminology.toml"
        )

        terms = translate_with_local_http.parse_terminology_terms(
            terminology_path.read_text(encoding="utf-8")
        )["simplified_chinese"]

        expected_terms = {
            "Grenade": ("榴弹枪", "pattern"),
            "Pistol": ("手枪", "exact"),
            "Hook": ("钩子", "exact"),
            "Scoreboard": ("计分板", "exact"),
            "Console": ("控制台", "exact"),
            "Demo": ("回放", "prompt_only"),
            "Spectate": ("旁观", "prompt_only"),
            "AntiPing": ("延迟补偿（AntiPing）", "prompt_only"),
            "Skin queue": ("皮肤队列", "pattern"),
            "Translation backend": ("翻译后端", "pattern"),
            "HTTP": ("HTTP", "exact"),
        }
        self.assertGreaterEqual(len(terms), 50)
        for source, (translation, enforce) in expected_terms.items():
            self.assertEqual(terms[source].translation, translation)
            self.assertEqual(terms[source].enforce, enforce)

    def test_parse_twlang_pairs_loads_official_simplified_chinese_terms(self):
        terms = translate_with_local_http.parse_twlang_pairs(
            "Grenade\n== 榴弹枪\n\nHook\n== 钩索\n"
        )

        self.assertEqual(terms["Grenade"], "榴弹枪")
        self.assertEqual(terms["Hook"], "钩索")

    def test_render_prompt_includes_task_scoped_official_references(self):
        tasks = [
            translate_with_local_http.TranslationTask(
                "menus", ("Grenade", ""), "Grenade"
            )
        ]

        with mock.patch.object(
            translate_with_local_http,
            "official_terminology_for_language",
            return_value={"Grenade": "榴弹枪", "Hook": "钩索"},
        ):
            prompt = translate_with_local_http.render_prompt(
                "simplified_chinese",
                "menus",
                tasks,
                prompt_assets=("# Rules", "", ""),
                store={},
            )

        self.assertIn("Official DDNet Simplified Chinese references", prompt)
        self.assertIn("- Grenade => 榴弹枪", prompt)
        self.assertNotIn("- Hook => 钩索", prompt)

    def test_official_references_do_not_become_global_hard_checks(self):
        with mock.patch.object(
            translate_with_local_http,
            "official_terminology_for_language",
            return_value={"Spectate": "旁观者菜单"},
        ):
            terminology = (
                translate_with_local_http.terminology_for_language_from_assets(
                    "simplified_chinese",
                    ("", "", ""),
                )
            )

        self.assertNotIn("Spectate", terminology)

    def test_parse_response_rejects_non_object_items(self):
        with self.assertRaises(ValueError):
            translate_with_local_http.parse_response('["bad"]')

    def test_write_draft_module_writes_mirror_toml(self):
        tasks = [
            translate_with_local_http.TranslationTask(
                "menus", ("Play", "Start menu"), "Play"
            )
        ]
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            with mock.patch.object(
                translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
            ):
                out_path = translate_with_local_http.write_draft_module(
                    "simplified_chinese",
                    "menus",
                    tasks,
                    {("Play", "Start menu"): "开始游戏"},
                )
            self.assertTrue(out_path.exists())
            parsed = out_path.read_text(encoding="utf-8")
            self.assertIn('key = "Play"', parsed)
            self.assertIn('context = "Start menu"', parsed)
            self.assertIn('simplified_chinese = "开始游戏"', parsed)

    def test_write_draft_module_preserves_existing_draft_entries(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit")
        ]
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            (language_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            with mock.patch.object(
                translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
            ):
                out_path = translate_with_local_http.write_draft_module(
                    "simplified_chinese",
                    "menus",
                    tasks,
                    {("Quit", ""): "退出"},
                )
            content = out_path.read_text(encoding="utf-8")
            self.assertIn('key = "Play"', content)
            self.assertIn('key = "Quit"', content)

    def test_write_draft_module_can_replace_existing_draft_entries(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit")
        ]
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            (language_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            with mock.patch.object(
                translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
            ):
                out_path = translate_with_local_http.write_draft_module(
                    "simplified_chinese",
                    "menus",
                    tasks,
                    {("Quit", ""): "退出"},
                    merge_existing=False,
                )
            content = out_path.read_text(encoding="utf-8")
            self.assertNotIn('key = "Play"', content)
            self.assertIn('key = "Quit"', content)

    def test_collect_tasks_only_returns_missing_entries(self):
        records = [
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
            mock.Mock(
                key="Quit",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Quit", "")),
            ),
        ]
        store = {
            "menus": {
                ("Play", ""): {"simplified_chinese": "开始游戏"},
                ("Quit", ""): {"simplified_chinese": "Quit"},
            }
        }
        with (
            mock.patch.object(
                translate_with_local_http.source_keys,
                "collect_source_key_records",
                return_value=records,
            ),
            mock.patch.object(
                translate_with_local_http.i18n_store,
                "load_language_store",
                return_value=store,
            ),
        ):
            tasks = translate_with_local_http.collect_tasks("simplified_chinese")
        self.assertEqual([task.identity for task in tasks], [("Quit", "")])

    def test_collect_tasks_keeps_extra_source_keys_without_source_file(self):
        records = [
            mock.Mock(
                key="Auto reply",
                source=None,
                identity=mock.Mock(return_value=("Auto reply", "")),
            )
        ]
        with (
            mock.patch.object(
                translate_with_local_http.source_keys,
                "collect_source_key_records",
                return_value=records,
            ),
            mock.patch.object(
                translate_with_local_http.i18n_store,
                "load_language_store",
                return_value={},
            ),
        ):
            tasks = translate_with_local_http.collect_tasks("korean")
        self.assertEqual(len(tasks), 1)
        self.assertEqual(tasks[0].module, "misc")

    def test_apply_translations_to_store_updates_non_simplified_language(self):
        store = {
            "menus": {
                ("Play", ""): {"simplified_chinese": "开始游戏"},
            }
        }
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play")
        ]
        updated = translate_with_local_http.apply_translations_to_store(
            store,
            "korean",
            tasks,
            {("Play", ""): "플레이"},
        )
        self.assertEqual(
            updated["menus"][("Play", "")],
            {"simplified_chinese": "开始游戏", "korean": "플레이"},
        )

    def test_translation_patch_for_language_keeps_only_successful_translations(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit"),
        ]
        self.assertEqual(
            translate_with_local_http.translation_patch_for_language(
                "korean",
                tasks,
                {("Quit", ""): "종료"},
            ),
            {("Quit", ""): {"korean": "종료"}},
        )

    def test_write_back_module_patches_only_translated_identities(self):
        store = {
            "menus": {
                ("Play", ""): {"simplified_chinese": "开始游戏"},
                ("Quit", ""): {"simplified_chinese": "退出"},
            }
        }
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit"),
        ]

        with mock.patch.object(
            translate_with_local_http.i18n_store, "patch_module_store"
        ) as patch_module_store:
            translate_with_local_http.write_back_module(
                "korean",
                "menus",
                tasks,
                {("Quit", ""): "종료"},
                store,
            )

        self.assertEqual(
            store["menus"][("Quit", "")],
            {"simplified_chinese": "退出", "korean": "종료"},
        )
        self.assertNotIn("korean", store["menus"][("Play", "")])
        patch_module_store.assert_called_once_with(
            "menus", {("Quit", ""): {"korean": "종료"}}
        )

    def test_write_back_module_does_not_reformat_untouched_toml_blocks(self):
        store = {
            "menus": {
                ("Play", ""): {"simplified_chinese": "开始游戏"},
                ("Quit", ""): {"simplified_chinese": "退出"},
            }
        }
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit"),
        ]

        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Play"
[message.translations]
polish = "Graj"
simplified_chinese = "开始游戏"

[[message]]
key = "Quit"
[message.translations]
simplified_chinese = "退出"
""",
                encoding="utf-8",
                newline="\n",
            )
            with mock.patch.object(
                translate_with_local_http.i18n_store,
                "TRANSLATIONS_DIR",
                translations_dir,
            ):
                translate_with_local_http.write_back_module(
                    "korean",
                    "menus",
                    tasks,
                    {("Quit", ""): "종료"},
                    store,
                )

            content = path.read_text(encoding="utf-8")
            self.assertIn(
                'key = "Play"\n'
                "[message.translations]\n"
                'polish = "Graj"\n'
                'simplified_chinese = "开始游戏"\n',
                content,
            )
            self.assertIn('korean = "종료"', content)

    def test_should_write_draft_by_default_for_all_languages(self):
        self.assertTrue(
            translate_with_local_http.should_write_draft("simplified_chinese")
        )
        self.assertTrue(translate_with_local_http.should_write_draft("korean"))
        self.assertFalse(
            translate_with_local_http.should_write_draft("korean", write_back=True)
        )

    def test_api_key_uses_environment_fallback(self):
        with mock.patch.dict(os.environ, {"QMCLIENT_LOCAL_HTTP_API_KEY": "env-key"}):
            self.assertEqual(translate_with_local_http.resolve_api_key(""), "env-key")
            self.assertEqual(
                translate_with_local_http.resolve_api_key("cli-key"), "cli-key"
            )

    def test_api_key_uses_deepseek_dotenv_for_deepseek_base_url(self):
        with tempfile.TemporaryDirectory() as tmp:
            env_file = Path(tmp) / ".env"
            env_file.write_text(
                "DEEPSEEK_API_KEY=deepseek-env-key\n"
                "QMCLIENT_LOCAL_HTTP_API_KEY=generic-env-key\n",
                encoding="utf-8",
            )

            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(
                    translate_with_local_http.resolve_api_key(
                        "", "https://api.deepseek.com", env_file
                    ),
                    "deepseek-env-key",
                )
                self.assertEqual(
                    translate_with_local_http.resolve_api_key(
                        "", "https://example.com/v1", env_file
                    ),
                    "generic-env-key",
                )

    def test_api_key_environment_overrides_dotenv(self):
        with tempfile.TemporaryDirectory() as tmp:
            env_file = Path(tmp) / ".env"
            env_file.write_text("DEEPSEEK_API_KEY=dot-env-key\n", encoding="utf-8")

            with mock.patch.dict(os.environ, {"DEEPSEEK_API_KEY": "process-env-key"}):
                self.assertEqual(
                    translate_with_local_http.resolve_api_key(
                        "", "https://api.deepseek.com", env_file
                    ),
                    "process-env-key",
                )

    def test_validate_translations_accepts_matching_items(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit"),
        ]
        translations, failures = translate_with_local_http.validate_translations(
            tasks,
            [
                {"key": "Play", "context": "", "translation": "开始游戏"},
                {"key": "Quit", "context": "", "translation": "退出"},
            ],
        )
        self.assertEqual(
            translations,
            {
                ("Play", ""): "开始游戏",
                ("Quit", ""): "退出",
            },
        )
        self.assertEqual(failures, [])

    def test_validate_translations_rejects_wrong_keys_and_bad_translations(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
        ]
        translations, failures = translate_with_local_http.validate_translations(
            tasks,
            [
                {"key": "Quit", "context": "", "translation": "退出"},
                {"key": "Play", "context": "", "translation": "Play"},
            ],
        )
        self.assertEqual(translations, {})
        self.assertEqual(len(failures), 3)
        self.assertIn("unexpected identity", failures[0])
        self.assertIn("unchanged source", failures[1])
        self.assertIn("missing translation", failures[2])

    def test_validate_translations_rejects_simplified_chinese_terminology_mismatch(
        self,
    ):
        tasks = [
            translate_with_local_http.TranslationTask(
                "menus", ("Grenade", ""), "Grenade"
            )
        ]

        translations, failures = translate_with_local_http.validate_translations(
            tasks,
            [{"key": "Grenade", "context": "", "translation": "榴弹炮"}],
            "simplified_chinese",
            terminology={
                "Grenade": translate_with_local_http.TerminologyTerm("榴弹枪", "exact")
            },
        )

        self.assertEqual(translations, {})
        self.assertTrue(any("terminology mismatch" in item for item in failures))
        self.assertTrue(any("榴弹枪" in item for item in failures))

    def test_terminology_quality_prefers_longer_matching_term(self):
        self.assertEqual(
            translate_with_local_http.terminology_quality_failure(
                "Grenade Launcher",
                "榴弹炮",
                {
                    "Grenade": translate_with_local_http.TerminologyTerm(
                        "榴弹枪", "pattern"
                    ),
                    "Grenade Launcher": translate_with_local_http.TerminologyTerm(
                        "榴弹炮", "exact"
                    ),
                },
            ),
            "",
        )

    def test_terminology_quality_prefers_exact_official_source_key(self):
        self.assertEqual(
            translate_with_local_http.terminology_quality_failure(
                "Grenade Launcher",
                "榴弹炮",
                {
                    "Grenade": translate_with_local_http.TerminologyTerm(
                        "榴弹枪", "pattern"
                    ),
                    "Grenade Launcher": translate_with_local_http.TerminologyTerm(
                        "榴弹炮", "exact"
                    ),
                },
            ),
            "",
        )
        self.assertIn(
            "terminology mismatch",
            translate_with_local_http.terminology_quality_failure(
                "Grenade",
                "榴弹炮",
                {
                    "Grenade": translate_with_local_http.TerminologyTerm(
                        "榴弹枪", "exact"
                    ),
                    "Grenade Launcher": translate_with_local_http.TerminologyTerm(
                        "榴弹炮", "exact"
                    ),
                },
            ),
        )

    def test_prompt_only_terminology_does_not_block_quality(self):
        self.assertEqual(
            translate_with_local_http.terminology_quality_failure(
                "Spectate",
                "旁观者菜单",
                {
                    "Spectate": translate_with_local_http.TerminologyTerm(
                        "旁观", "prompt_only"
                    )
                },
            ),
            "",
        )

    def test_parse_translation_output_accepts_reordered_json_items(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
            translate_with_local_http.TranslationTask("menus", ("Quit", ""), "Quit"),
        ]

        translations, failures = translate_with_local_http.parse_translation_output(
            json.dumps(
                [
                    {"key": "Quit", "context": "", "translation": "退出"},
                    {"key": "Play", "context": "", "translation": "开始游戏"},
                ],
                ensure_ascii=False,
            ),
            tasks,
            "simplified_chinese",
        )

        self.assertEqual(
            translations,
            {
                ("Play", ""): "开始游戏",
                ("Quit", ""): "退出",
            },
        )
        self.assertEqual(failures, [])

    def test_parse_translation_output_reports_unexpected_json_identity(self):
        tasks = [
            translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play"),
        ]

        translations, failures = translate_with_local_http.parse_translation_output(
            json.dumps(
                [{"key": "Quit", "context": "", "translation": "退出"}],
                ensure_ascii=False,
            ),
            tasks,
            "simplified_chinese",
        )

        self.assertEqual(translations, {})
        self.assertTrue(
            any("unexpected identity returned" in item for item in failures)
        )

    def test_extract_placeholders_ignores_percentage_ranges(self):
        self.assertEqual(
            translate_with_local_http.extract_placeholders(
                "Size of entity text data (20% to 100%)"
            ),
            [],
        )
        self.assertEqual(
            translate_with_local_http.extract_placeholders(
                "Tamanho (20% a 100%)"
            ),
            [],
        )
        self.assertEqual(
            translate_with_local_http.extract_placeholders("Player %s at 50%"),
            ["%s"],
        )
        self.assertEqual(
            translate_with_local_http.extract_placeholders(
                "Jump hint horizontal position (% of screen width)"
            ),
            [],
        )

    def test_placeholder_quality_distinguishes_escaped_percent(self):
        self.assertEqual(
            translate_with_local_http.extract_placeholders("Progress: 100%%"),
            ["%%"],
        )
        self.assertEqual(
            translate_with_local_http.extract_placeholders("Progress: 100%"),
            [],
        )
        self.assertIn(
            "placeholder mismatch",
            translate_with_local_http.language_quality_failure(
                "simplified_chinese", "Progress: 100%%", "进度：100%"
            ),
        )

    def test_russian_quality_requires_cyrillic_for_translated_text(self):
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "russian", "Server browser", "Браузер серверов"
            ),
            "",
        )
        self.assertIn(
            "cyrillic",
            translate_with_local_http.language_quality_failure(
                "russian", "Server browser", "Server list"
            ),
        )

    def test_russian_quality_allows_product_names(self):
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "russian", "QmClient", "QmClient"
            ),
            "",
        )

    def test_language_specific_same_source_allowlist_is_narrow(self):
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "spanish", "Solo", "Solo"
            ),
            "",
        )
        self.assertIn(
            "unchanged source",
            translate_with_local_http.language_quality_failure(
                "spanish", "Server list", "Server list"
            ),
        )

    def test_language_quality_rejects_pseudo_english_setting_suffix(self):
        self.assertIn(
            "setting",
            translate_with_local_http.language_quality_failure(
                "german", "Prediction margin", "Prediction margin setting"
            ).casefold(),
        )
        self.assertIn(
            "setting",
            translate_with_local_http.language_quality_failure(
                "german", "Prediction margin", "prediction margin SETTING"
            ).casefold(),
        )
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "german", "Prediction margin", "Vorhersagemarge"
            ),
            "",
        )

    def test_digit_gate_allows_one_as_arabic_or_omitted(self):
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "japanese", "Go back one tick", "1ティック戻る"
            ),
            "",
        )
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "simplified_chinese", "Go back one tick", "上一个 tick"
            ),
            "",
        )

    def test_digit_gate_still_requires_source_numerics(self):
        self.assertIn(
            "digit mismatch",
            translate_with_local_http.language_quality_failure(
                "japanese", "wait 60 seconds", "六十秒待つ"
            ),
        )
        self.assertIn(
            "digit mismatch",
            translate_with_local_http.language_quality_failure(
                "german", "plain text", "Version 2"
            ),
        )

    def test_digit_gate_allows_five_word_as_optional_digit(self):
        source = "Shows five points of the ladder (1 by default)"
        self.assertEqual(
            translate_with_local_http.language_quality_failure(
                "simplified_chinese",
                source,
                "显示天梯的 5 个点（默认 1）",
            ),
            "",
        )

    def test_collect_tasks_recollects_when_existing_fails_terminology(self):
        records = [
            mock.Mock(
                key="Grenade",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Grenade", "")),
            ),
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
        ]
        store = {
            "menus": {
                ("Grenade", ""): {"simplified_chinese": "榴弹炮"},
                ("Play", ""): {"simplified_chinese": "开始游戏"},
            }
        }
        terminology = {
            "Grenade": translate_with_local_http.TerminologyTerm("榴弹枪", "exact")
        }
        with (
            mock.patch.object(
                translate_with_local_http.source_keys,
                "collect_source_key_records",
                return_value=records,
            ),
            mock.patch.object(
                translate_with_local_http.i18n_store,
                "load_language_store",
                return_value=store,
            ),
        ):
            tasks = translate_with_local_http.collect_tasks(
                "simplified_chinese", terminology=terminology
            )
        self.assertEqual([task.identity for task in tasks], [("Grenade", "")])

    def test_collect_tasks_loads_terminology_from_assets_when_not_passed(self):
        records = [
            mock.Mock(
                key="Grenade",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Grenade", "")),
            ),
        ]
        store = {
            "menus": {
                ("Grenade", ""): {"simplified_chinese": "榴弹炮"},
            }
        }
        terminology = {
            "Grenade": translate_with_local_http.TerminologyTerm("榴弹枪", "exact")
        }
        with (
            mock.patch.object(
                translate_with_local_http.source_keys,
                "collect_source_key_records",
                return_value=records,
            ),
            mock.patch.object(
                translate_with_local_http.i18n_store,
                "load_language_store",
                return_value=store,
            ),
            mock.patch.object(
                translate_with_local_http,
                "load_prompt_assets",
                return_value=("# Rules", "[[term]]\nsource = \"Grenade\"\n", ""),
            ) as load_assets,
            mock.patch.object(
                translate_with_local_http,
                "terminology_terms_for_language_from_assets",
                return_value=terminology,
            ) as load_terms,
        ):
            tasks = translate_with_local_http.collect_tasks("simplified_chinese")

        load_assets.assert_called()
        load_terms.assert_called()
        self.assertEqual([task.identity for task in tasks], [("Grenade", "")])

    def test_write_back_draft_overwrites_quality_failure_without_rewrite_flag(self):
        record = mock.Mock(
            key="Progress: %d%%",
            source=Path("src/game/client/components/menus_settings.cpp"),
            identity=mock.Mock(return_value=("Progress: %d%%", "")),
        )
        # existing has placeholder mismatch (missing %%)
        store = {
            "menus": {
                ("Progress: %d%%", ""): {"simplified_chinese": "进度：%d%"},
            }
        }
        source_texts = {("Progress: %d%%", ""): "Progress: %d%%"}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            draft_path = language_dir / "menus.toml"
            draft_path.write_text(
                """[[message]]
key = "Progress: %d%%"
[message.translations]
simplified_chinese = "进度：%d%%"
""",
                encoding="utf-8",
            )
            translations_dir = Path(tmp) / "i18n"
            translations_dir.mkdir()
            (translations_dir / "menus.toml").write_text(
                """[[message]]
key = "Progress: %d%%"
[message.translations]
simplified_chinese = "进度：%d%"
""",
                encoding="utf-8",
            )
            with (
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "TRANSLATIONS_DIR",
                    translations_dir,
                ),
            ):
                written, failures = translate_with_local_http.write_back_draft(
                    "simplified_chinese",
                    modules={"menus"},
                    source_records=[record],
                    source_texts=source_texts,
                    store=store,
                    rewrite_existing=False,
                )

            self.assertEqual(failures, [])
            self.assertEqual(written, 1)
            content = (translations_dir / "menus.toml").read_text(encoding="utf-8")
            self.assertIn('simplified_chinese = "进度：%d%%"', content)
            self.assertEqual(
                store["menus"][("Progress: %d%%", "")]["simplified_chinese"],
                "进度：%d%%",
            )

    def test_write_back_draft_skips_good_existing_without_rewrite_flag(self):
        record = mock.Mock(
            key="Play",
            source=Path("src/game/client/components/menus_settings.cpp"),
            identity=mock.Mock(return_value=("Play", "")),
        )
        store = {"menus": {("Play", ""): {"simplified_chinese": "开始游戏"}}}
        source_texts = {("Play", ""): "Play"}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            draft_path = language_dir / "menus.toml"
            draft_path.write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "游玩"
""",
                encoding="utf-8",
            )
            translations_dir = Path(tmp) / "i18n"
            translations_dir.mkdir()
            menus_path = translations_dir / "menus.toml"
            menus_path.write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            with (
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "TRANSLATIONS_DIR",
                    translations_dir,
                ),
            ):
                written, failures = translate_with_local_http.write_back_draft(
                    "simplified_chinese",
                    modules={"menus"},
                    source_records=[record],
                    source_texts=source_texts,
                    store=store,
                    rewrite_existing=False,
                )

            self.assertEqual(failures, [])
            self.assertEqual(written, 0)
            content = menus_path.read_text(encoding="utf-8")
            self.assertIn('simplified_chinese = "开始游戏"', content)
            self.assertNotIn("游玩", content)
            self.assertEqual(
                store["menus"][("Play", "")]["simplified_chinese"], "开始游戏"
            )

    def test_write_back_draft_overwrites_pseudo_setting_suffix_existing(self):
        record = mock.Mock(
            key="Prediction margin",
            source=Path("src/game/client/components/menus_settings.cpp"),
            identity=mock.Mock(return_value=("Prediction margin", "")),
        )
        store = {
            "menus": {
                ("Prediction margin", ""): {
                    "german": "Prediction margin setting"
                }
            }
        }
        source_texts = {("Prediction margin", ""): "Prediction margin"}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "german"
            language_dir.mkdir(parents=True)
            (language_dir / "menus.toml").write_text(
                """[[message]]
key = "Prediction margin"
[message.translations]
german = "Vorhersagemarge"
""",
                encoding="utf-8",
            )
            translations_dir = Path(tmp) / "i18n"
            translations_dir.mkdir()
            menus_path = translations_dir / "menus.toml"
            menus_path.write_text(
                """[[message]]
key = "Prediction margin"
[message.translations]
german = "Prediction margin setting"
""",
                encoding="utf-8",
            )
            with (
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "TRANSLATIONS_DIR",
                    translations_dir,
                ),
            ):
                written, failures = translate_with_local_http.write_back_draft(
                    "german",
                    modules={"menus"},
                    source_records=[record],
                    source_texts=source_texts,
                    store=store,
                    rewrite_existing=False,
                )

            self.assertEqual(failures, [])
            self.assertEqual(written, 1)
            content = menus_path.read_text(encoding="utf-8")
            self.assertIn('german = "Vorhersagemarge"', content)
            self.assertEqual(
                store["menus"][("Prediction margin", "")]["german"],
                "Vorhersagemarge",
            )


    def test_load_existing_draft_identities_reads_existing_module_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            (language_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            with mock.patch.object(
                translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
            ):
                identities = translate_with_local_http.load_existing_draft_identities(
                    "simplified_chinese"
                )
            self.assertEqual(identities, {("Play", "")})

    def test_parse_languages_and_modules(self):
        self.assertEqual(
            translate_with_local_http.parse_csv_values(" a, b ,,c "),
            ["a", "b", "c"],
        )

    def test_translate_task_batches_allows_ten_parallel_requests(self):
        tasks_by_module = {
            "menus": [
                translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play")
                for _ in range(11)
            ]
        }
        fake_executor = mock.MagicMock()
        fake_executor.__enter__.return_value.submit.side_effect = RuntimeError("stop")

        with mock.patch.object(
            translate_with_local_http.concurrent.futures,
            "ThreadPoolExecutor",
            return_value=fake_executor,
        ) as executor_cls:
            with self.assertRaises(RuntimeError):
                translate_with_local_http.translate_task_batches(
                    client=mock.Mock(),
                    language="simplified_chinese",
                    tasks_by_module=tasks_by_module,
                    batch_size=1,
                    prompt_assets=("", "", ""),
                    store={},
                    parallel_requests=10,
                )

        executor_cls.assert_called_once_with(max_workers=10)

    def test_translate_task_batches_allows_thirty_two_parallel_requests(self):
        tasks_by_module = {
            "menus": [
                translate_with_local_http.TranslationTask("menus", ("Play", ""), "Play")
                for _ in range(33)
            ]
        }
        fake_executor = mock.MagicMock()
        fake_executor.__enter__.return_value.submit.side_effect = RuntimeError("stop")

        with mock.patch.object(
            translate_with_local_http.concurrent.futures,
            "ThreadPoolExecutor",
            return_value=fake_executor,
        ) as executor_cls:
            with self.assertRaises(RuntimeError):
                translate_with_local_http.translate_task_batches(
                    client=mock.Mock(),
                    language="simplified_chinese",
                    tasks_by_module=tasks_by_module,
                    batch_size=1,
                    prompt_assets=("", "", ""),
                    store={},
                    parallel_requests=32,
                )

        executor_cls.assert_called_once_with(max_workers=32)

    def test_main_resume_module_and_limit_skip_existing_draft(self):
        records = [
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
            mock.Mock(
                key="Quit",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Quit", "")),
            ),
            mock.Mock(
                key="Refresh",
                source=Path("src/game/client/components/menus_browser.cpp"),
                identity=mock.Mock(return_value=("Refresh", "")),
            ),
        ]
        store = {"menus": {}, "server_browser": {}}
        fake_client = mock.Mock()
        fake_client.chat_completion.return_value = json.dumps(
            [{"key": "Quit", "context": "", "translation": "退出"}]
        )
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            (language_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            argv = [
                "translate_with_local_http.py",
                "--languages",
                "simplified_chinese",
                "--base-url",
                "http://127.0.0.1:1337/v1",
                "--model",
                "local-model",
                "--module",
                "menus",
                "--limit",
                "1",
                "--resume",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    translate_with_local_http.source_keys,
                    "collect_incremental_source_key_records",
                    return_value=(records, (), False),
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "load_language_store",
                    return_value=store,
                ),
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http,
                    "LocalHttpClient",
                    return_value=fake_client,
                ),
            ):
                translate_with_local_http.main()
            fake_client.chat_completion.assert_called_once()
            content = (language_dir / "menus.toml").read_text(encoding="utf-8")
            self.assertIn('key = "Quit"', content)
            self.assertNotIn('key = "Refresh"', content)

    def test_main_resume_retranslates_draft_with_terminology_mismatch(self):
        records = [
            mock.Mock(
                key="Grenade",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Grenade", "")),
            ),
        ]
        store = {"menus": {}}
        fake_client = mock.Mock()
        fake_client.chat_completion.return_value = json.dumps(
            [{"key": "Grenade", "context": "", "translation": "榴弹枪"}]
        )
        terminology = """[[term]]
source = "Grenade"
simplified_chinese = "榴弹枪"
enforce = "exact"
"""
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            draft_path = language_dir / "menus.toml"
            draft_path.write_text(
                """[[message]]
key = "Grenade"
[message.translations]
simplified_chinese = "榴弹炮"
""",
                encoding="utf-8",
            )
            argv = [
                "translate_with_local_http.py",
                "--languages",
                "simplified_chinese",
                "--base-url",
                "http://127.0.0.1:1337/v1",
                "--model",
                "local-model",
                "--module",
                "menus",
                "--limit",
                "1",
                "--resume",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    translate_with_local_http.source_keys,
                    "collect_incremental_source_key_records",
                    return_value=(records, (), False),
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "load_language_store",
                    return_value=store,
                ),
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http,
                    "load_prompt_assets",
                    return_value=("# Rules", terminology, ""),
                ),
                mock.patch.object(
                    translate_with_local_http,
                    "LocalHttpClient",
                    return_value=fake_client,
                ),
            ):
                translate_with_local_http.main()

            fake_client.chat_completion.assert_called_once()
            content = draft_path.read_text(encoding="utf-8")
            self.assertIn('simplified_chinese = "榴弹枪"', content)
            self.assertNotIn("榴弹炮", content)

    def test_write_back_draft_removes_written_module_draft(self):
        record = mock.Mock(
            key="Play",
            source=Path("src/game/client/components/menus_settings.cpp"),
            identity=mock.Mock(return_value=("Play", "")),
        )
        store = {"menus": {("Play", ""): {"simplified_chinese": "开始"}}}
        source_texts = {("Play", ""): "Play"}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            draft_path = language_dir / "menus.toml"
            draft_path.write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"
""",
                encoding="utf-8",
            )
            translations_dir = Path(tmp) / "i18n"
            translations_dir.mkdir()
            (translations_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始"
""",
                encoding="utf-8",
            )
            with (
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "TRANSLATIONS_DIR",
                    translations_dir,
                ),
            ):
                written, failures = translate_with_local_http.write_back_draft(
                    "simplified_chinese",
                    modules={"menus"},
                    source_records=[record],
                    source_texts=source_texts,
                    store=store,
                    rewrite_existing=True,
                )

            self.assertEqual(failures, [])
            self.assertEqual(written, 1)
            self.assertFalse(draft_path.exists())

    def test_write_back_draft_keeps_unwritten_module_draft_entries(self):
        record = mock.Mock(
            key="Play",
            source=Path("src/game/client/components/menus_settings.cpp"),
            identity=mock.Mock(return_value=("Play", "")),
        )
        store = {
            "menus": {
                ("Play", ""): {"simplified_chinese": "开始"},
                ("Quit", ""): {},
            }
        }
        source_texts = {("Play", ""): "Play", ("Quit", ""): "Quit"}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            language_dir = draft_root / "simplified_chinese"
            language_dir.mkdir(parents=True)
            draft_path = language_dir / "menus.toml"
            draft_path.write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始游戏"

[[message]]
key = "Quit"
[message.translations]
simplified_chinese = "退出"
""",
                encoding="utf-8",
            )
            translations_dir = Path(tmp) / "i18n"
            translations_dir.mkdir()
            (translations_dir / "menus.toml").write_text(
                """[[message]]
key = "Play"
[message.translations]
simplified_chinese = "开始"

[[message]]
key = "Quit"
[message.translations]
""",
                encoding="utf-8",
            )
            with (
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "TRANSLATIONS_DIR",
                    translations_dir,
                ),
            ):
                written, failures = translate_with_local_http.write_back_draft(
                    "simplified_chinese",
                    modules={"menus"},
                    source_records=[record],
                    source_texts=source_texts,
                    store=store,
                    rewrite_existing=True,
                )

            self.assertEqual(failures, [])
            self.assertEqual(written, 1)
            content = draft_path.read_text(encoding="utf-8")
            self.assertNotIn('key = "Play"', content)
            self.assertIn('key = "Quit"', content)

    def test_clean_empty_drafts_removes_empty_files_and_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            empty_dir = draft_root / "spanish"
            empty_dir.mkdir(parents=True)
            empty_file = empty_dir / "menus.toml"
            empty_file.write_text("  \n", encoding="utf-8")
            kept_dir = draft_root / "french"
            kept_dir.mkdir()
            kept_file = kept_dir / "menus.toml"
            kept_file.write_text(
                '[[message]]\nkey = "Play"\n[message.translations]\nfrench = "Jouer"\n',
                encoding="utf-8",
            )

            with mock.patch.object(
                translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
            ):
                removed = translate_with_local_http.clean_empty_drafts()

            self.assertFalse(empty_file.exists())
            self.assertFalse(empty_dir.exists())
            self.assertTrue(kept_file.exists())
            self.assertTrue(any("menus.toml" in item for item in removed))

    def test_main_dry_run_does_not_write_draft(self):
        records = [
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
        ]
        store = {"menus": {}}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            argv = [
                "translate_with_local_http.py",
                "--languages",
                "simplified_chinese",
                "--base-url",
                "http://127.0.0.1:1337/v1",
                "--model",
                "local-model",
                "--dry-run",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    translate_with_local_http.source_keys,
                    "collect_incremental_source_key_records",
                    return_value=(records, (), False),
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "load_language_store",
                    return_value=store,
                ),
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
            ):
                translate_with_local_http.main()
            self.assertFalse((draft_root / "simplified_chinese").exists())

    def test_main_batch_failure_does_not_abort_other_batches(self):
        records = [
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
            mock.Mock(
                key="Quit",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Quit", "")),
            ),
        ]
        store = {"menus": {}}
        fake_client = mock.Mock()
        fake_client.chat_completion.side_effect = [
            "not-json",
            json.dumps([{"key": "Quit", "context": "", "translation": "退出"}]),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            argv = [
                "translate_with_local_http.py",
                "--languages",
                "simplified_chinese",
                "--base-url",
                "http://127.0.0.1:1337/v1",
                "--model",
                "local-model",
                "--module",
                "menus",
                "--batch-size",
                "1",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    translate_with_local_http.source_keys,
                    "collect_incremental_source_key_records",
                    return_value=(records, (), False),
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "load_language_store",
                    return_value=store,
                ),
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http,
                    "LocalHttpClient",
                    return_value=fake_client,
                ),
            ):
                translate_with_local_http.main()
            content = (draft_root / "simplified_chinese" / "menus.toml").read_text(
                encoding="utf-8"
            )
            self.assertIn('key = "Quit"', content)
            self.assertNotIn('key = "Play"', content)

    def test_main_auto_clean_runs_after_parallel_languages(self):
        records = [
            mock.Mock(
                key="Play",
                source=Path("src/game/client/components/menus_settings.cpp"),
                identity=mock.Mock(return_value=("Play", "")),
            ),
        ]
        store = {"menus": {}}
        with tempfile.TemporaryDirectory() as tmp:
            draft_root = Path(tmp)
            empty_dir = draft_root / "spanish"
            empty_dir.mkdir(parents=True)
            empty_file = empty_dir / "menus.toml"
            empty_file.write_text(" \n", encoding="utf-8")
            argv = [
                "translate_with_local_http.py",
                "--languages",
                "spanish,french",
                "--parallel-languages",
                "2",
                "--auto-clean",
                "--module",
                "menus",
                "--limit",
                "1",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    translate_with_local_http.source_keys,
                    "collect_incremental_source_key_records",
                    return_value=(records, (), False),
                ),
                mock.patch.object(
                    translate_with_local_http.i18n_store,
                    "load_language_store",
                    return_value=store,
                ),
                mock.patch.object(
                    translate_with_local_http, "TRANSLATIONS_DRAFT_DIR", draft_root
                ),
                mock.patch.object(
                    translate_with_local_http, "translate_language_to_drafts"
                ) as translate_language,
            ):
                translate_with_local_http.main()

            self.assertEqual(translate_language.call_count, 2)
            self.assertFalse(empty_file.exists())
            self.assertFalse(empty_dir.exists())


if __name__ == "__main__":
    unittest.main()
