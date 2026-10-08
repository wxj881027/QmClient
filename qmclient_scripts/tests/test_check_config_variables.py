from __future__ import annotations

from pathlib import Path
import re
import tempfile
import unittest

from qmclient_scripts import check_config_variables


class ConfigVariableUsageTest(unittest.TestCase):
    def setUp(self):
        self.pattern = re.compile(
            check_config_variables.generate_regex(
                "QmDefaultsProfileVersion", "qm_defaults_profile_version"
            )
        )

    def test_passed_config_objects_count_as_uses(self):
        for expression in (
            "Config.m_QmDefaultsProfileVersion = 1;",
            "pConfig->m_QmDefaultsProfileVersion >= 1",
            "g_Config.m_QmDefaultsProfileVersion",
            "Config()->m_QmDefaultsProfileVersion",
        ):
            with self.subTest(expression=expression):
                self.assertIsNotNone(self.pattern.search(expression))

    def test_config_member_pointers_count_as_uses(self):
        self.assertIsNotNone(
            self.pattern.search("&CConfig::m_QmDefaultsProfileVersion")
        )

    def test_similarly_named_members_do_not_count_as_uses(self):
        for expression in (
            "Config.m_QmDefaultsProfileVersionBackup",
            "Config.m_QmDefaultsProfileVersion2",
            "Config.m_OtherQmDefaultsProfileVersion",
        ):
            with self.subTest(expression=expression):
                self.assertIsNone(self.pattern.search(expression))

    def test_scan_keeps_reporting_genuinely_unused_variables(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "default_profile.h"
            source.write_text(
                "void Initialize(CConfig &Config) { Config.m_QmDefaultsProfileVersion = 1; }\n",
                encoding="utf-8",
            )
            unused = check_config_variables.find_config_variables(
                {
                    "QmDefaultsProfileVersion": "qm_defaults_profile_version",
                    "QmUnusedSetting": "qm_unused_setting",
                },
                source_root=directory,
            )
        self.assertEqual(unused, {"QmUnusedSetting"})


if __name__ == "__main__":
    unittest.main()
