#!/usr/bin/env python3

from __future__ import annotations

import ast
import re
import unittest
from pathlib import Path

from qmclient_scripts.languages_qmclient import (
    i18n_store,
    source_keys,
    translate_with_local_http,
    twlang_qmclient,
)


ROOT = Path(__file__).resolve().parents[3]
EDITOR_ROOT = ROOT / "src" / "game" / "editor"
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def _has_cjk(value: str) -> bool:
    return any("\u3400" <= char <= "\u9fff" for char in value)


def _editor_cpp_files() -> list[Path]:
    return sorted(
        path for path in EDITOR_ROOT.rglob("*") if path.suffix in {".cpp", ".h"}
    )


class EditorBilingualTest(unittest.TestCase):
    def test_editor_translation_placeholders_keep_source_order(self):
        editor_store = i18n_store.load_language_store()["editor"]
        mismatches = [
            (key, context, translations["simplified_chinese"])
            for (key, context), translations in editor_store.items()
            if translate_with_local_http.extract_placeholders(key)
            != translate_with_local_http.extract_placeholders(
                translations["simplified_chinese"]
            )
        ]
        self.assertEqual([], mismatches, "editor placeholder order changed")

    def test_non_simplified_runtime_languages_have_no_editor_entries(self):
        violations: list[str] = []
        for relative_path in twlang_qmclient.languages():
            path = ROOT / relative_path
            if path.name == "simplified_chinese.txt":
                continue
            for key, context in twlang_qmclient.translations(path):
                if context.startswith("Editor"):
                    violations.append(f"{path.name}: [{context}] {key}")
        self.assertEqual(
            [], violations, "non-Simplified-Chinese editors must fall back to English"
        )

    def test_editor_has_no_cjk_string_literals(self):
        violations: list[str] = []
        for path in _editor_cpp_files():
            content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
            for match in STRING_RE.finditer(content):
                try:
                    value = ast.literal_eval(f'"{match.group(1)}"')
                except (SyntaxError, ValueError):
                    continue
                if _has_cjk(value):
                    line = content.count("\n", 0, match.start()) + 1
                    violations.append(f"{path.relative_to(ROOT)}:{line}: {value}")
        self.assertEqual([], violations, "editor CJK string literals remain")

    def test_indirect_collaboration_status_keys_are_extractable(self):
        violations: list[str] = []
        pattern = re.compile(r'\bSetCollabStatus\s*\(\s*"([^"\\]*)"')
        for path in _editor_cpp_files():
            content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
            for match in pattern.finditer(content):
                if match.group(1).startswith("%"):
                    continue
                line = content.count("\n", 0, match.start()) + 1
                violations.append(f"{path.relative_to(ROOT)}:{line}")
        self.assertEqual(
            [], violations, "collaboration status keys must use Localizable"
        )

    def test_localize_does_not_initialize_mutable_character_arrays(self):
        invalid_initializers: list[str] = []
        pattern = re.compile(r"\bchar\s+\w+\s*\[[^]]*]\s*=\s*Localize\s*\(")
        for path in _editor_cpp_files():
            content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
            for match in pattern.finditer(content):
                line = content.count("\n", 0, match.start()) + 1
                invalid_initializers.append(f"{path.relative_to(ROOT)}:{line}")
        self.assertEqual(
            [],
            invalid_initializers,
            "Localize returns a pointer and cannot initialize a mutable char array",
        )

    def test_editor_localization_users_include_their_declarations(self):
        direct_includes = {
            EDITOR_ROOT / "explanations.cpp": "#include <game/localization.h>",
            ROOT
            / "src"
            / "test"
            / "unit"
            / "engine"
            / "editor_test.cpp": "#include <engine/shared/localization.h>",
        }
        for path, include in direct_includes.items():
            self.assertIn(include, path.read_text(encoding="utf-8-sig"), str(path))

    def test_language_reload_is_deferred_while_editor_is_open(self):
        gameclient = source_keys.read_source_text(
            ROOT / "src" / "game" / "client" / "gameclient.cpp"
        )
        body = source_keys.extract_function_body(
            gameclient, "CGameClient::HandleLanguageChanged"
        )
        editor_guard = body.find("if(g_Config.m_ClEditor)")
        consume_change = body.find("m_LanguageChanged = false;")
        self.assertGreaterEqual(editor_guard, 0, "editor language reload guard missing")
        self.assertGreater(
            consume_change,
            editor_guard,
            "language change must stay pending until the editor closes",
        )

    def test_editor_localize_keys_have_simplified_chinese_entries(self):
        records = []
        for path in _editor_cpp_files():
            content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
            records.extend(source_keys.extract_localize_key_records(content))
        identities = sorted({record.identity() for record in records})
        non_editor_contexts = [
            identity for identity in identities if not identity[1].startswith("Editor")
        ]
        self.assertEqual(
            [], non_editor_contexts, "editor keys must not reuse global contexts"
        )
        store = i18n_store.load_language_store()
        translations = i18n_store.language_map_for(store, "simplified_chinese")
        missing = [identity for identity in identities if identity not in translations]
        self.assertEqual([], missing, "editor keys lack simplified Chinese entries")

    def test_default_persisted_layer_names_are_english(self):
        expected = {
            "layer_front.cpp": ['str_copy(m_aName, "Front")'],
            "layer_game.cpp": ['str_copy(m_aName, "Game")'],
            "layer_speedup.cpp": [
                'str_copy(m_aName, "Speedup")',
                'str_copy(m_aName, "Speedup copy")',
            ],
            "layer_switch.cpp": [
                'str_copy(m_aName, "Switch")',
                'str_copy(m_aName, "Switch copy")',
            ],
            "layer_tele.cpp": [
                'str_copy(m_aName, "Tele")',
                'str_copy(m_aName, "Tele copy")',
            ],
            "layer_tune.cpp": [
                'str_copy(m_aName, "Tune")',
                'str_copy(m_aName, "Tune copy")',
            ],
            "map.cpp": ['str_copy(m_pGameGroup->m_aName, "Game")'],
        }
        for filename, snippets in expected.items():
            path = EDITOR_ROOT / "mapitems" / filename
            content = path.read_text(encoding="utf-8-sig")
            for snippet in snippets:
                self.assertIn(snippet, content, str(path))

        localized_persisted_names: list[str] = []
        for path in (EDITOR_ROOT / "mapitems").rglob("*.cpp"):
            content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
            for line_number, line in enumerate(content.splitlines(), 1):
                if re.search(
                    r"\bstr_(?:copy|format)\s*\(\s*[^,]*m_aName\s*,[^;]*Localize\s*\(",
                    line,
                ):
                    localized_persisted_names.append(
                        f"{path.relative_to(ROOT)}:{line_number}"
                    )
        self.assertEqual(
            [], localized_persisted_names, "persisted map names must stay English"
        )


if __name__ == "__main__":
    unittest.main()
