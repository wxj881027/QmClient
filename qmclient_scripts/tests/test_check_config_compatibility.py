from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest

from qmclient_scripts import check_config_variables


DEFINITION = (
    'MACRO_CONFIG_INT(QmSaved, qm_saved, 0, 0, 1, '
    'CFGFLAG_CLIENT | CFGFLAG_SAVE, "old")\n'
)


class SavedConfigCompatibilityTest(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(__file__).resolve().parents[2] / "tmp"
        temporary_root.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="config-compat-", dir=temporary_root)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "src"
        self.source.mkdir()
        self.header = self.source / "settings.h"
        self.header.write_text(DEFINITION, encoding="utf-8")

    def rules(self):
        return {
            "QmSaved": {
                "script_name": "qm_saved",
                "kind": "INT",
                "reason": "旧界面已移除，继续保存旧配置。",
            }
        }

    def check(self, compatibility=None):
        output = io.StringIO()
        with redirect_stdout(output):
            result = check_config_variables.check_config_file(
                "qmclient", self.header,
                self.rules() if compatibility is None else compatibility,
                source_root=self.source,
            )
        return result, output.getvalue()

    def manifest(self):
        return {"schema_version": 1, "saved_client_configs": {"qmclient": self.rules()}}

    def load_manifest(self, data):
        path = self.root / "compatibility.json"
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        return check_config_variables.load_compatibility_rules(path)

    def test_saved_compatibility_is_reported_separately_from_active_use(self):
        result, output = self.check()
        self.assertEqual(result, 0)
        self.assertIn("仅保存兼容：'qm_saved'", output)
        self.assertIn("活动配置 0 个，保存兼容配置 1 个", output)

    def test_unrelated_unused_active_setting_still_fails(self):
        self.header.write_text(
            DEFINITION + DEFINITION.replace("QmSaved", "QmUnused").replace("qm_saved", "qm_unused"),
            encoding="utf-8",
        )
        result, output = self.check()
        self.assertEqual(result, 1)
        self.assertIn("未使用配置项：'qm_unused'", output)
        self.assertIn("仅保存兼容：'qm_saved'", output)

    def test_save_flag_alone_does_not_grant_compatibility(self):
        result, output = self.check({})
        self.assertEqual(result, 1)
        self.assertIn("未使用配置项：'qm_saved'", output)

    def test_removed_declaration_invalidates_compatibility(self):
        self.header.write_text(
            DEFINITION.replace("QmSaved", "QmOther").replace("qm_saved", "qm_other"),
            encoding="utf-8",
        )
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("对应声明已不存在", output)

    def test_changed_command_name_invalidates_compatibility(self):
        self.header.write_text(DEFINITION.replace("qm_saved", "qm_renamed"), encoding="utf-8")
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("命令名与兼容声明不匹配", output)

    def test_changed_type_invalidates_compatibility(self):
        self.header.write_text(
            'MACRO_CONFIG_COL(QmSaved, qm_saved, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "old")\n',
            encoding="utf-8",
        )
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("配置类型与兼容声明不匹配", output)

    def test_losing_save_flag_invalidates_compatibility(self):
        self.header.write_text(DEFINITION.replace(" | CFGFLAG_SAVE", ""), encoding="utf-8")
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("必须保留 CFGFLAG_CLIENT 和 CFGFLAG_SAVE", output)

    def test_losing_client_flag_invalidates_compatibility(self):
        self.header.write_text(DEFINITION.replace("CFGFLAG_CLIENT | ", ""), encoding="utf-8")
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("必须保留 CFGFLAG_CLIENT 和 CFGFLAG_SAVE", output)

    def test_save_flag_in_help_text_does_not_count(self):
        self.header.write_text(
            DEFINITION.replace(" | CFGFLAG_SAVE", "").replace('"old"', '"CFGFLAG_SAVE"'),
            encoding="utf-8",
        )
        result, _ = self.check()
        self.assertGreater(result, 0)

    def test_negated_flags_cannot_be_classified_as_saved(self):
        self.header.write_text(
            DEFINITION.replace("CFGFLAG_SAVE", "(CFGFLAG_SAVE & ~CFGFLAG_SAVE)"),
            encoding="utf-8",
        )
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("无法确认保存兼容标志", output)

    def test_runtime_reference_requires_explicit_reclassification(self):
        (self.source / "consumer.cpp").write_text(
            "int Read() { return g_Config.m_QmSaved; }", encoding="utf-8"
        )
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("重新出现静态值引用", output)

    def test_reclassified_active_setting_can_use_normal_check(self):
        (self.source / "consumer.cpp").write_text(
            "int Read() { return g_Config.m_QmSaved; }", encoding="utf-8"
        )
        result, output = self.check({})
        self.assertEqual(result, 0)
        self.assertIn("活动配置 1 个，保存兼容配置 0 个", output)

    def test_missing_file_cannot_hide_saved_declarations(self):
        self.header = self.source / "missing.h"
        result, _ = self.check()
        self.assertGreater(result, 0)

    def test_empty_file_cannot_hide_saved_declarations(self):
        self.header.write_text("", encoding="utf-8")
        result, output = self.check()
        self.assertGreater(result, 0)
        self.assertIn("对应声明已不存在", output)

    def test_macro_kinds_use_their_own_flag_positions(self):
        lines = [
            DEFINITION,
            'MACRO_CONFIG_COL(Color, color, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "color")',
            'MACRO_CONFIG_STR(Text, text, 64, "a,b", CFGFLAG_CLIENT | CFGFLAG_SAVE, "text")',
        ]
        declarations = check_config_variables.parse_config_declarations(lines)
        for code, kind in (("QmSaved", "INT"), ("Color", "COL"), ("Text", "STR")):
            with self.subTest(code=code):
                self.assertEqual(declarations[code][0], kind)
                self.assertEqual(
                    check_config_variables.config_flag_names(declarations[code][1]),
                    {"CFGFLAG_CLIENT", "CFGFLAG_SAVE"},
                )

    def test_quoted_commas_and_nested_defaults_keep_flag_boundary(self):
        lines = [
            r'MACRO_CONFIG_STR(Text, text, 64, "a,\"b(c)", (CFGFLAG_CLIENT | CFGFLAG_SAVE), "end")',
            'MACRO_CONFIG_COL(Color, color, Pack(1, 2), (CFGFLAG_CLIENT | CFGFLAG_SAVE), "end")',
        ]
        declarations = check_config_variables.parse_config_declarations(lines)
        for code in ("Text", "Color"):
            with self.subTest(code=code):
                self.assertEqual(
                    check_config_variables.config_flag_names(declarations[code][1]),
                    {"CFGFLAG_CLIENT", "CFGFLAG_SAVE"},
                )

    def test_manifest_preserves_explicit_source_group_and_reason(self):
        rules = self.load_manifest(self.manifest())
        self.assertEqual(set(rules), {"qmclient"})
        self.assertEqual(rules["qmclient"], self.rules())

    def test_manifest_rejects_unknown_source_group(self):
        data = self.manifest()
        data["saved_client_configs"] = {"unknown": self.rules()}
        with self.assertRaises(ValueError):
            self.load_manifest(data)

    def test_manifest_rejects_unsupported_schema(self):
        data = self.manifest()
        data["schema_version"] = 2
        with self.assertRaises(ValueError):
            self.load_manifest(data)

    def test_manifest_requires_a_nonempty_retirement_reason(self):
        data = self.manifest()
        data["saved_client_configs"]["qmclient"]["QmSaved"]["reason"] = " "
        with self.assertRaises(ValueError):
            self.load_manifest(data)

    def test_manifest_rejects_wildcards_and_invalid_command_names(self):
        for code, command in (("Qm*", "qm_saved"), ("QmSaved", "qm saved")):
            with self.subTest(code=code, command=command):
                rule = self.rules()["QmSaved"]
                rule["script_name"] = command
                data = {"schema_version": 1, "saved_client_configs": {"qmclient": {code: rule}}}
                with self.assertRaises(ValueError):
                    self.load_manifest(data)

    def test_manifest_rejects_unknown_macro_kind(self):
        data = self.manifest()
        data["saved_client_configs"]["qmclient"]["QmSaved"]["kind"] = "UNKNOWN"
        with self.assertRaises(ValueError):
            self.load_manifest(data)

    def test_manifest_rejects_unrecognized_rule_fields(self):
        data = self.manifest()
        data["saved_client_configs"]["qmclient"]["QmSaved"]["skip_check"] = True
        with self.assertRaises(ValueError):
            self.load_manifest(data)

    def test_missing_manifest_reports_failure_to_caller(self):
        with self.assertRaises(OSError):
            check_config_variables.load_compatibility_rules(self.root / "missing.json")


if __name__ == "__main__":
    unittest.main()
