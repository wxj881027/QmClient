# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3

import tomllib
import unittest
from pathlib import Path
from unittest import mock
import tempfile

from qmclient_scripts.languages_qmclient import (
    i18n_store,
    normalize_simplified_chinese,
    source_keys,
)


class I18nTomlTest(unittest.TestCase):
    def test_typography_normalizer_does_not_rewrite_editor_blueprint(self):
        store = {
            "editor": {
                ("%dms", "Editor"): {"simplified_chinese": "%d毫秒"},
            }
        }
        with (
            mock.patch.object(
                normalize_simplified_chinese.i18n_store,
                "load_language_store",
                return_value=store,
            ),
            mock.patch.object(
                normalize_simplified_chinese.i18n_store, "patch_module_store"
            ) as patch_module_store,
        ):
            normalize_simplified_chinese.main()

        patch_module_store.assert_not_called()

    def test_dump_module_writes_context_and_multilang(self):
        text = i18n_store.dump_module(
            [
                (
                    i18n_store.Message("Play", "Start menu"),
                    {
                        "simplified_chinese": "开始游戏",
                        "traditional_chinese": "開始遊戲",
                    },
                )
            ]
        )
        parsed = tomllib.loads(text)
        self.assertEqual(parsed["message"][0]["key"], "Play")
        self.assertEqual(parsed["message"][0]["context"], "Start menu")
        self.assertEqual(
            parsed["message"][0]["translations"]["simplified_chinese"], "开始游戏"
        )

    def test_dump_editor_module_preserves_authoritative_translation_verbatim(self):
        text = i18n_store.dump_module(
            [
                (
                    i18n_store.Message("%dms", "Editor"),
                    {"simplified_chinese": "%d毫秒"},
                )
            ],
            module_name="editor",
        )

        parsed = tomllib.loads(text)
        self.assertEqual(
            parsed["message"][0]["translations"]["simplified_chinese"],
            "%d毫秒",
        )

    def test_dump_module_keeps_translation_lines_contiguous(self):
        text = i18n_store.dump_module(
            [
                (
                    i18n_store.Message("Anti Ping Smoothing"),
                    {
                        "simplified_chinese": "平滑预测",
                        "japanese": "アンチピングスムージング",
                        "traditional_chinese": "預測量",
                        "korean": "양",
                    },
                )
            ]
        )

        self.assertNotIn(
            'simplified_chinese = "平滑预测"\n\njapanese = ',
            text,
        )
        self.assertIn(
            "[message.translations]\n"
            'simplified_chinese = "平滑预测"\n'
            'traditional_chinese = "預測量"\n'
            'japanese = "アンチピングスムージング"\n'
            'korean = "양"',
            text,
        )

    def test_patch_module_store_removes_internal_translation_blank_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Anti Ping Smoothing"
[message.translations]
simplified_chinese = "平滑预测"

japanese = "アンチピングスムージング"
traditional_chinese = "預測量"

[[message]]
key = "Apply"
[message.translations]
simplified_chinese = "应用"
""",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch.object(i18n_store, "TRANSLATIONS_DIR", translations_dir):
                i18n_store.patch_module_store(
                    "menus",
                    {
                        ("Anti Ping Smoothing", ""): {
                            "simplified_chinese": "平滑预测",
                            "japanese": "アンチピングスムージング",
                            "traditional_chinese": "預測量",
                            "korean": "양",
                        },
                        ("Apply", ""): {
                            "simplified_chinese": "应用",
                        },
                    },
                )

            text = path.read_text(encoding="utf-8")
            self.assertIn(
                "[message.translations]\n"
                'simplified_chinese = "平滑预测"\n'
                'traditional_chinese = "預測量"\n'
                'japanese = "アンチピングスムージング"\n'
                'korean = "양"\n'
                "\n"
                "[[message]]",
                text,
            )
            tomllib.loads(text)

    def test_patch_module_store_keeps_blank_line_between_message_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Apply"
[message.translations]
simplified_chinese = "应用"

[[message]]
key = "Cancel"
[message.translations]
simplified_chinese = "取消"
""",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch.object(i18n_store, "TRANSLATIONS_DIR", translations_dir):
                i18n_store.patch_module_store(
                    "menus",
                    {
                        ("Apply", ""): {
                            "traditional_chinese": "套用",
                        },
                    },
                )

            text = path.read_text(encoding="utf-8")
            self.assertIn(
                'traditional_chinese = "套用"\n\n[[message]]\nkey = "Cancel"',
                text,
            )
            self.assertEqual(i18n_store.toml_format_errors(path), [])

    def test_toml_format_errors_rejects_adjacent_message_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "menus.toml"
            path.write_text(
                """[[message]]
key = "Apply"
[message.translations]
simplified_chinese = "应用"
[[message]]
key = "Cancel"
[message.translations]
simplified_chinese = "取消"
""",
                encoding="utf-8",
                newline="\n",
            )

            errors = i18n_store.toml_format_errors(path)

        self.assertEqual(len(errors), 1)
        self.assertIn("missing blank line before [[message]]", errors[0])

    def test_patch_module_store_rewrites_touched_block_with_stable_language_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Apply"
[message.translations]
polish = "Zastosuj"
simplified_chinese = "应用"
japanese = "適用"
""",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch.object(i18n_store, "TRANSLATIONS_DIR", translations_dir):
                i18n_store.patch_module_store(
                    "menus",
                    {
                        ("Apply", ""): {
                            "traditional_chinese": "套用",
                        },
                    },
                )

            text = path.read_text(encoding="utf-8")
            self.assertIn(
                "[message.translations]\n"
                'simplified_chinese = "应用"\n'
                'traditional_chinese = "套用"\n'
                'japanese = "適用"\n'
                'polish = "Zastosuj"',
                text,
            )
            tomllib.loads(text)

    def test_module_name_for_source_uses_code_boundaries(self):
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/game/client/components/menus_browser.cpp")
            ),
            "server_browser",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(Path("src/game/client/gameclient.cpp")),
            "loading",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/engine/shared/config_variables_qmclient.h")
            ),
            "qmclient",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/engine/shared/config_variables_tclient.h")
            ),
            "tclient",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/engine/shared/config_variables.h")
            ),
            "menus",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("E:/repo/src/game/client/QmUi/cards/QmCardCatalogGraphics.cpp")
            ),
            "qmclient",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path(
                    "src/game/client/QmUi/cards/QmCardSearch/"
                    "QmCardCatalogGraphics.inc"
                )
            ),
            "qmclient",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/game/client/components/qmclient/word_filter_list.cpp")
            ),
            "word_filter",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/game/client/components/qmclient/public_word_filter.cpp")
            ),
            "word_filter",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/game/client/QmUi/QmWikiEditor.cpp")
            ),
            "wiki_ui",
        )
        self.assertEqual(
            i18n_store.module_name_for_source(
                Path("src/game/client/QmUi/QmWikiStore.cpp")
            ),
            "wiki_ui",
        )

    def test_all_config_help_has_simplified_chinese_translation(self):
        config_paths = tuple(
            source_keys.PROJECT_ROOT / relative_path
            for relative_path in source_keys.CONFIG_MACRO_HELP_HEADERS
        )
        records = source_keys.collect_source_key_records(
            paths=config_paths,
            extra_strings=set(),
        )
        identities = tuple(sorted({record.identity() for record in records}))

        missing = i18n_store.missing_translations_for(
            i18n_store.load_language_store(),
            identities,
            "simplified_chinese",
        )

        self.assertEqual(missing, [])

    def test_f1_rebuilds_localized_config_help_from_config_metadata(self):
        console_source = (
            source_keys.PROJECT_ROOT
            / "src/game/client/components/console.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("BuildLocalizedConfigHelpText", console_source)
        self.assertIn("ConfigManager()->PossibleConfigVariables", console_source)
        self.assertIn("pVar->m_pHelpLocalizeKey", console_source)
        self.assertIn("Localize(pVar->m_pHelpLocalizeKey)", console_source)
        self.assertIn(
            'Localize("%s (default: %d, min: %d, max: %d)", "Config help")',
            console_source,
        )
        self.assertIn(
            'Localize("%s (default: \\"%s\\", max length: %d)", "Config help")',
            console_source,
        )

    def test_language_map_for_flattens_selected_language(self):
        store = {
            "menus": {
                ("Play", "Start menu"): {
                    "simplified_chinese": "开始游戏",
                    "traditional_chinese": "開始遊戲",
                }
            }
        }
        self.assertEqual(
            i18n_store.language_map_for(store, "simplified_chinese"),
            {("Play", "Start menu"): "开始游戏"},
        )

    def test_language_map_for_keeps_first_non_empty_across_modules(self):
        store = {
            "menus": {
                ("Spectate", ""): {
                    "spanish": "Observar",
                }
            },
            "qmclient": {
                ("Spectate", ""): {
                    "spanish": "Espectar",
                }
            },
        }

        self.assertEqual(
            i18n_store.language_map_for(store, "spanish"),
            {("Spectate", ""): "Observar"},
        )

    def test_language_map_for_fills_from_later_module_when_first_empty(self):
        store = {
            "menus": {
                ("Type", ""): {
                    "spanish": "",
                    "german": "Typ",
                }
            },
            "qmclient": {
                ("Type", ""): {
                    "spanish": "Tipo",
                }
            },
        }

        self.assertEqual(
            i18n_store.language_map_for(store, "spanish"),
            {("Type", ""): "Tipo"},
        )

    def test_translation_quality_rejects_source_setting_pseudo_translation(self):
        store = {
            "qmclient": {
                ("Auto fetch lyrics", ""): {
                    "spanish": "Auto fetch lyrics setting",
                    "simplified_chinese": "自动获取歌词",
                }
            }
        }

        errors = i18n_store.translation_quality_errors(store)

        self.assertTrue(
            any(
                "Auto fetch lyrics setting" in item
                or "pseudo-translation" in item
                or "setting" in item
                for item in errors
            ),
            f"expected '{{key}} setting' quality error; got: {errors}",
        )
        self.assertTrue(
            any("spanish" in item for item in errors),
            f"expected spanish failure; got: {errors}",
        )

    def test_translation_quality_errors_for_placeholder_order_swap(self):
        store = {
            "menus": {
                ("%s vs %d", ""): {
                    "spanish": "%d vs %s",
                }
            }
        }

        report = i18n_store.translation_quality_report(store)

        self.assertTrue(
            any("placeholder mismatch" in item for item in report.errors),
            report.errors,
        )
        self.assertFalse(
            any("placeholder mismatch" in item for item in report.warnings),
            report.warnings,
        )

    def test_store_integrity_errors_reports_cross_module_translation_conflict(self):
        store = {
            "menus": {
                ("Item", ""): {"spanish": "Elemento"},
            },
            "qmclient": {
                ("Item", ""): {"spanish": "Ítem"},
            },
        }

        errors = i18n_store.store_integrity_errors(store)

        self.assertTrue(
            any("Item" in item and "spanish" in item for item in errors),
            errors,
        )
        self.assertTrue(
            any("cross-module" in item or "conflict" in item for item in errors),
            errors,
        )

    def test_store_integrity_errors_reports_same_module_duplicate_message_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Apply"
[message.translations]
spanish = "Aplicar"

[[message]]
key = "Apply"
[message.translations]
spanish = "Aplicar de nuevo"
""",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch.object(i18n_store, "TRANSLATIONS_DIR", translations_dir):
                errors = i18n_store.store_integrity_errors({"menus": {}})

        self.assertTrue(
            any("duplicate" in item and "Apply" in item for item in errors),
            errors,
        )

    def test_store_integrity_errors_reports_mass_shared_translation(self):
        shared = "圓角分段數"
        store = {
            "qmclient": {
                (f"Key {index}", ""): {"traditional_chinese": shared}
                for index in range(5)
            }
        }

        errors = i18n_store.store_integrity_errors(store)

        self.assertTrue(
            any(
                "mass-shared" in item or "shared translation" in item
                for item in errors
            ),
            errors,
        )
        self.assertTrue(any("5" in item for item in errors), errors)
        self.assertTrue(any("traditional_chinese" in item for item in errors), errors)

    def test_store_integrity_errors_ignores_short_shared_translation(self):
        store = {
            "qmclient": {
                (f"Key {index}", ""): {"spanish": "On"}
                for index in range(5)
            }
        }

        errors = i18n_store.store_integrity_errors(store)

        self.assertFalse(
            any("mass-shared" in item or "shared translation" in item for item in errors),
            errors,
        )

    def test_store_integrity_errors_mass_shared_respects_active_identities(self):
        shared = "shared long text"
        store = {
            "qmclient": {
                (f"Key {index}", ""): {"spanish": shared}
                for index in range(6)
            }
        }

        errors = i18n_store.store_integrity_errors(
            store,
            active_identities={(f"Key {index}", "") for index in range(3)},
        )

        self.assertFalse(
            any("mass-shared" in item or "shared translation" in item for item in errors),
            errors,
        )

    def test_patch_module_store_deletes_language_when_translation_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            translations_dir = Path(tmp)
            path = translations_dir / "menus.toml"
            path.write_text(
                """[[message]]
key = "Apply"
[message.translations]
simplified_chinese = "应用"
spanish = "Aplicar"
""",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch.object(i18n_store, "TRANSLATIONS_DIR", translations_dir):
                i18n_store.patch_module_store(
                    "menus",
                    {
                        ("Apply", ""): {
                            "spanish": "",
                        },
                    },
                )

            text = path.read_text(encoding="utf-8")
            parsed = tomllib.loads(text)
            translations = parsed["message"][0]["translations"]
            self.assertNotIn("spanish", translations)
            self.assertEqual(translations["simplified_chinese"], "应用")


    def test_missing_translations_for_reports_identities_without_selected_language(
        self,
    ):
        store = {
            "menus": {
                ("Play", "Start menu"): {
                    "simplified_chinese": "开始游戏",
                },
                ("Quit", ""): {
                    "traditional_chinese": "結束",
                },
            }
        }
        self.assertEqual(
            i18n_store.missing_translations_for(
                store,
                [("Play", "Start menu"), ("Quit", ""), ("Open", "")],
                "simplified_chinese",
            ),
            [("Quit", ""), ("Open", "")],
        )

    def test_missing_translations_for_skips_source_like_tokens(self):
        self.assertEqual(
            i18n_store.missing_translations_for(
                {},
                [
                    ("DeepSeek API Key", ""),
                    ("OpenAI API Key", ""),
                    ("https://ddnet.org/discord", ""),
                    ("Active line font size", ""),
                ],
                "spanish",
            ),
            [
                ("DeepSeek API Key", ""),
                ("OpenAI API Key", ""),
                ("Active line font size", ""),
            ],
        )

    def test_translation_quality_rejects_cjk_source_fallback_for_non_chinese(self):
        store = {
            "qmclient": {
                ("DeepSeek 模型名称", ""): {
                    "spanish": "DeepSeek 模型名称",
                    "simplified_chinese": "DeepSeek模型名称",
                }
            }
        }

        errors = i18n_store.translation_quality_errors(store)

        self.assertTrue(
            any("spanish repeats CJK source key" in item for item in errors)
        )
        self.assertTrue(any("simplified_chinese typography" in item for item in errors))

    def test_translation_quality_rejects_english_source_fallback_for_non_chinese(self):
        store = {
            "qmclient": {
                ("Enable lyrics HUD overlay", ""): {
                    "spanish": "Enable lyrics HUD overlay",
                    "simplified_chinese": "启用歌词 HUD 叠加层",
                }
            }
        }

        errors = i18n_store.translation_quality_errors(store)

        self.assertTrue(
            any("spanish repeats English source key" in item for item in errors)
        )

    def test_translation_quality_rejects_title_case_brand_phrase_fallback(self):
        store = {
            "qmclient": {
                ("OpenAI API Key", ""): {
                    "spanish": "OpenAI API Key",
                    "simplified_chinese": "OpenAI API 密钥",
                }
            }
        }

        errors = i18n_store.translation_quality_errors(store)

        self.assertTrue(
            any("spanish repeats English source key" in item for item in errors),
            f"expected title-case fallback to be rejected; got: {errors}",
        )

    def test_translation_quality_rejects_simplified_chinese_terminology_mismatch(self):
        store = {
            "menus": {
                ("Grenade", ""): {
                    "simplified_chinese": "榴弹炮",
                }
            }
        }

        errors = i18n_store.translation_quality_errors(
            store,
            terminology_by_language={"simplified_chinese": {"Grenade": "榴弹枪"}},
        )

        self.assertTrue(any("terminology mismatch" in item for item in errors))
        self.assertTrue(any("榴弹枪" in item for item in errors))

    def test_translation_quality_can_be_limited_to_active_identities(self):
        store = {
            "qmclient": {
                ("Old key", ""): {
                    "spanish": "Old key",
                },
                ("Active key with words", ""): {
                    "spanish": "Active key with words",
                },
            }
        }

        errors = i18n_store.translation_quality_errors(
            store, active_identities={("Active key with words", "")}
        )

        self.assertEqual(len(errors), 1)
        self.assertIn("Active key with words", errors[0])

    def test_translation_quality_allows_source_like_tokens(self):
        store = {
            "menus": {
                ("auto", ""): {"spanish": "auto"},
                ("Shotgun", ""): {"portuguese": "Shotgun"},
                ("entity_bg (Workshop)", ""): {
                    "spanish": "entity_bg (Workshop)",
                    "japanese": "entity_bg (Workshop)",
                },
                ("https://ddnet.org/discord", ""): {
                    "spanish": "https://ddnet.org/discord"
                },
                ("Demo", ""): {"spanish": "Demo"},
                ("HUD", ""): {"spanish": "HUD"},
            }
        }

        self.assertEqual(i18n_store.translation_quality_errors(store), [])

    def test_translation_quality_allows_japanese_and_korean_cjk_text(self):
        store = {
            "menus": {
                ("Play", ""): {
                    "japanese": "プレイ",
                    "korean": "플레이",
                    "spanish": "Jugar",
                    "simplified_chinese": "开始游戏",
                }
            }
        }

        self.assertEqual(i18n_store.translation_quality_errors(store), [])

    def test_translation_quality_allows_cjk_names_inside_foreign_sentences(self):
        store = {
            "qmclient": {
                ("Example", ""): {
                    "turkish": "Pasta menüsü yeniden adlandırma listesi, örn: 璇梦1|璇梦2",
                    "simplified_chinese": "示例",
                }
            }
        }

        self.assertEqual(i18n_store.translation_quality_errors(store), [])

    def test_translation_quality_report_errors_for_placeholder_mismatch(self):
        store = {
            "menus": {
                ("Player %s joined", ""): {
                    "spanish": "El jugador se unió",
                    "simplified_chinese": "玩家 %s 加入",
                }
            }
        }

        report = i18n_store.translation_quality_report(store)

        self.assertTrue(
            any("placeholder mismatch" in item for item in report.errors),
            report.errors,
        )
        self.assertFalse(
            any("placeholder mismatch" in item for item in report.warnings),
            report.warnings,
        )

    def test_translation_quality_ignores_percentage_ranges_as_placeholders(self):
        store = {
            "menus": {
                ("Size of entity text data (20% to 100%)", ""): {
                    "simplified_chinese": "实体文本数据的大小（20% 到 100%）",
                    "brazilian_portuguese": "Tamanho dos dados de texto da entidade (20% a 100%)",
                    "polish": "Rozmiar danych tekstu entity (20% do 100%)",
                    "french": "Taille des données texte d'entité (20% à 100%)",
                }
            }
        }

        report = i18n_store.translation_quality_report(store)

        self.assertEqual(report.errors, [])
        self.assertFalse(
            any("placeholder mismatch" in item for item in report.warnings),
            report.warnings,
        )

    def test_translation_quality_checks_dynamic_printf_precision(self):
        store = {
            "menus": {
                ("Avg %.*f down %.*f up %.*f", ""): {
                    "german": "Durchschnitt %f runter %.*f rauf %.*f",
                }
            }
        }

        report = i18n_store.translation_quality_report(store)

        self.assertTrue(
            any("placeholder mismatch" in item for item in report.errors),
            report.errors,
        )
        self.assertFalse(
            any("placeholder mismatch" in item for item in report.warnings),
            report.warnings,
        )

    def test_translation_quality_does_not_limit_translation_length(self):
        store = {
            "menus": {
                ("Short label", ""): {
                    "spanish": "Esta traducción es deliberadamente demasiado larga para una etiqueta corta",
                }
            },
            "qmclient": {
                ("中文设置项", ""): {
                    "german": "Eine deutlich längere deutsche Übersetzung für die Einstellung",
                }
            },
        }

        report = i18n_store.translation_quality_report(store)

        self.assertEqual(report.errors, [])
        self.assertEqual(report.warnings, [])

    def test_pending_translation_is_one_explicit_warning(self):
        store = {
            "menus": {
                ("Player %s joined", ""): {
                    "japanese": "Pending japanese translation deadbeef",
                }
            }
        }

        report = i18n_store.translation_quality_report(store)

        self.assertEqual(report.errors, [])
        self.assertEqual(len(report.warnings), 1)
        self.assertIn("pending translation placeholder", report.warnings[0])
        self.assertNotIn("placeholder mismatch", report.warnings[0])

    def test_validate_module_calls_store_integrity_errors(self):
        validate_path = (
            Path(__file__).resolve().parents[1] / "validate.py"
        )
        source = validate_path.read_text(encoding="utf-8")
        self.assertIn("store_integrity_errors", source)

if __name__ == "__main__":
    unittest.main()
