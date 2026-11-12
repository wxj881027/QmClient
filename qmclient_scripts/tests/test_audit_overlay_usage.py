from __future__ import annotations

import unittest

from qmclient_scripts import audit_overlay_usage


class OverlayUsageScanTest(unittest.TestCase):
    def test_non_centered_confirm_is_a_hard_error(self):
        result = audit_overlay_usage.scan_text(
            "Ui()->ShowPopupConfirm(X, Y, &s_Context, false);\n"
        )
        self.assertEqual([hit["rule"] for hit in result["hard"]], ["R1"])

    def test_centered_confirm_has_no_hard_error(self):
        result = audit_overlay_usage.scan_text(
            "Ui()->ShowPopupConfirm(X, Y, &s_Context);\n"
        )
        self.assertEqual(result["hard"], [])

    def test_multiline_confirm_arguments_are_still_checked(self):
        result = audit_overlay_usage.scan_text(
            "Ui()->ShowPopupConfirm(\n"
            "\tUi()->MouseX(),\n"
            "\tButton.y + Button.h + 5.0f,\n"
            "\t&s_Context,\n"
            "\tfalse);\n"
        )
        self.assertEqual([hit["rule"] for hit in result["hard"]], ["R1"])

    def test_yes_no_buttons_call_is_a_hard_error(self):
        result = audit_overlay_usage.scan_text("s_Context.YesNoButtons();\n")
        self.assertEqual([hit["rule"] for hit in result["hard"]], ["R2"])

    def test_yes_no_buttons_definition_is_not_a_call(self):
        result = audit_overlay_usage.scan_text(
            "void CUi::SConfirmPopupContext::YesNoButtons()\n"
            "{\n"
            '\tstr_copy(m_aPositiveButtonLabel, Localize("Yes"));\n'
            "}\n"
        )
        self.assertEqual(result["hard"], [])

    def test_legacy_popups_are_counted_but_definitions_and_render_callbacks_are_not(self):
        result = audit_overlay_usage.scan_text(
            "PopupConfirm(Localize(\"Disconnect\"), aMessage, Localize(\"Yes\"), Localize(\"No\"), &CMenus::PopupConfirmSwitchServer);\n"
            "PopupMessage(aTitle, aMessage, Localize(\"OK\"));\n"
            "void CMenus::PopupMessage(const char *pTitle, const char *pMessage)\n"
            "{\n"
            "}\n"
            "CUi::EPopupMenuFunctionResult CUi::PopupMessage(void *pContext, CUIRect View, bool Active)\n"
            "{\n"
            "}\n"
        )
        self.assertEqual(len(result["legacy"]), 2)

    def test_commented_out_legacy_call_is_ignored(self):
        result = audit_overlay_usage.scan_text(
            "// PopupConfirm(a, b, c, d, e);\n"
        )
        self.assertEqual(result["legacy"], [])

    def test_long_tooltip_literal_is_reported(self):
        long_text = "x" * (audit_overlay_usage.TOOLTIP_LONG_LITERAL + 1)
        result = audit_overlay_usage.scan_text(
            f'Tooltips().DoToolTip(&s_Tip, &Rect, "{long_text}");\n'
        )
        self.assertEqual(len(result["tooltips"]), 1)
        self.assertEqual(result["tooltips"][0]["rule"], "R4")

    def test_short_tooltip_literal_and_variables_are_not_reported(self):
        result = audit_overlay_usage.scan_text(
            'Tooltips().DoToolTip(&s_Tip, &Rect, "Download");\n'
            "Tooltips().DoToolTip(&s_Tip, &Rect, pLongDynamicText);\n"
        )
        self.assertEqual(result["tooltips"], [])

    def test_compare_reports_regressions_against_baseline(self):
        report = {
            "hard": [],
            "legacy": {"src/game/client/components/menus.cpp": [{}, {}, {}]},
            "tooltips": {},
        }
        baseline = {
            "legacy_per_file": {"src/game/client/components/menus.cpp": 2},
            "tooltips_per_file": {},
        }
        violations = audit_overlay_usage.compare(report, baseline)
        self.assertEqual(len(violations), 1)
        self.assertIn("新增 1 处", violations[0])


if __name__ == "__main__":
    unittest.main()
