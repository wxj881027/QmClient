from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from qmclient_scripts import check_config_exposure


class ParseConfigHeaderTest(unittest.TestCase):
    def test_parses_single_line_and_multiline_macros(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "config_variables_test.h"
            header.write_text(
                "MACRO_CONFIG_INT(QmAlpha, qm_alpha, 1, 0, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, \"Alpha\")\n"
                "MACRO_CONFIG_STR(QmBeta, qm_beta, 32, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE,\n"
                "                \"Beta with a description that wraps to the next line\")\n"
                "MACRO_CONFIG_INT(ClGamma, cl_gamma, 0, 0, 1, CFGFLAG_SERVER, \"Gamma\")\n",
                encoding="utf-8",
            )
            variables = check_config_exposure.parse_config_header(header)

        self.assertEqual([variable.name for variable in variables], ["qm_alpha", "qm_beta", "cl_gamma"])
        self.assertEqual([variable.code for variable in variables], ["QmAlpha", "QmBeta", "ClGamma"])
        self.assertTrue(variables[0].client_facing)
        self.assertFalse(variables[2].client_facing)
        self.assertTrue(variables[0].requires_exposure)
        self.assertFalse(variables[2].requires_exposure)


class ExposureMatchTest(unittest.TestCase):
    def test_word_boundary_keeps_similarly_named_members_apart(self):
        variables = [
            check_config_exposure.SConfigVar("h", "INT", "QmAlpha", "qm_alpha", "CFGFLAG_CLIENT")
        ]
        self.assertEqual(
            check_config_exposure.find_exposed_codes(variables, "g_Config.m_QmAlpha = 1;"),
            {"QmAlpha"},
        )
        self.assertEqual(
            check_config_exposure.find_exposed_codes(variables, "g_Config.m_QmAlphaBackup = 1;"),
            set(),
        )


class AuditTest(unittest.TestCase):
    def _variables(self):
        return [
            check_config_exposure.SConfigVar("h", "INT", "QmExposed", "qm_exposed", "CFGFLAG_CLIENT"),
            check_config_exposure.SConfigVar("h", "INT", "QmHidden", "qm_hidden", "CFGFLAG_CLIENT"),
            check_config_exposure.SConfigVar("h", "INT", "QmInternal", "qm_internal", "CFGFLAG_CLIENT"),
            check_config_exposure.SConfigVar("h", "INT", "ClUpstream", "cl_upstream", "CFGFLAG_CLIENT"),
            check_config_exposure.SConfigVar("h", "INT", "SvServer", "sv_server", "CFGFLAG_SERVER"),
        ]

    def test_splits_owned_upstream_and_server_only(self):
        baseline = {
            "qm_internal": check_config_exposure.SBaselineEntry(check_config_exposure.CATEGORY_INTERNAL, "state"),
        }
        result = check_config_exposure.audit(
            self._variables(), "g_Config.m_QmExposed;", baseline
        )

        self.assertEqual(
            [variable.name for variable in result.unexposed_owned], ["qm_hidden", "qm_internal"]
        )
        self.assertEqual([variable.name for variable in result.unexposed_upstream], ["cl_upstream"])
        self.assertEqual([variable.name for variable in result.unregistered], ["qm_hidden"])
        self.assertEqual(result.stale, [])

    def test_stale_baseline_entries_are_reported(self):
        baseline = {
            "qm_exposed": check_config_exposure.SBaselineEntry(check_config_exposure.CATEGORY_PENDING_UI, ""),
            "qm_deleted": check_config_exposure.SBaselineEntry(check_config_exposure.CATEGORY_PENDING_UI, ""),
        }
        result = check_config_exposure.audit(self._variables(), "g_Config.m_QmExposed;", baseline)

        self.assertEqual(
            result.stale,
            ["qm_deleted（配置已不存在）", "qm_exposed（已接线，请从基线移除）"],
        )

    def test_default_category_marks_migration_state_as_internal(self):
        self.assertEqual(
            check_config_exposure.default_category("qm_card_layout_version"),
            check_config_exposure.CATEGORY_INTERNAL,
        )
        self.assertEqual(
            check_config_exposure.default_category("qm_scoreboard_sort_mode"),
            check_config_exposure.CATEGORY_PENDING_UI,
        )


if __name__ == "__main__":
    unittest.main()
