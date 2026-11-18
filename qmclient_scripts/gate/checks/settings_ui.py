#!/usr/bin/env python3
"""设置页统一 UI 迁移合同检查。"""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector


REPO_ROOT = Path(__file__).resolve().parents[3]
TRIGGER_PATHS = {
	"src/game/client/QmUi/QmScroll.h",
	"src/game/client/QmUi/QmCardRegistry.cpp",
	"src/game/client/QmUi/QmScroll.cpp",
	"src/game/client/QmUi/SettingsPageLayout.h",
	"src/game/client/QmUi/UiForms.cpp",
	"src/game/client/QmUi/UiForms.h",
	"src/game/client/components/menus.cpp",
	"src/game/client/components/menus_settings.cpp",
	"src/game/client/components/menus_settings7.cpp",
	"src/game/client/components/menus_settings_assets.cpp",
	"src/game/client/components/menus_settings_controls.cpp",
	"src/game/client/components/qmclient/menus_qmclient.cpp",
	"src/game/client/components/tclient/menus_tclient.cpp",
	"src/game/client/components/tclient/menus_tclient_internal.h",
	"src/game/client/components/tclient/menus_tclient_preview.cpp",
	"src/game/client/components/qmclient/menus_qmclient_internal.h",
	"src/game/client/components/qmclient/menus_qmclient_search.cpp",
	"src/game/client/components/qmclient/menus_qmclient_popups.cpp",
	"src/game/client/QmUi/SettingsMenuWidgets.cpp",
	"src/game/client/QmUi/SettingsMenuPrewarm.cpp",
	"src/game/client/QmUi/SettingsRuntimeCache.cpp",
}

TRIGGER_PREFIXES = ("src/game/client/QmUi/SettingsCard", "src/game/client/QmUi/cards/QmCardCatalog")


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return bool(TRIGGER_PATHS.intersection(normalized)) or any(path.startswith(TRIGGER_PREFIXES) for path in normalized)


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("设置页统一 UI 迁移合同", "改动范围未触及设置页统一 UI 合同文件")
		return
	if dry_run:
		results.add("INFO", "设置页统一 UI 迁移合同", "DryRun，仅展示命令")
		return
	code, out = runner.run_python(
		str(REPO_ROOT / "qmclient_scripts" / "gate" / "check_settings_ui_migration.py"),
		"--all",
		title="设置页统一 UI 迁移合同",
		check=False,
	)
	if code != 0:
		results.add("FAIL", "设置页统一 UI 迁移合同", out)
		return
	results.add("PASS", "设置页统一 UI 迁移合同", "所有已迁移设置页合同通过")

	# 条件行动画：g_Config 开关包住行声明却没有 reveal 作用域时，展开/收起会瞬变。
	# 这条放在闸门里，靠机器记住，而不是靠每次改动时想起来。
	code, out = runner.run_python(
		str(REPO_ROOT / "qmclient_scripts" / "check_settings_reveal.py"),
		title="设置页条件行动画",
		check=False,
	)
	if code != 0:
		results.add("FAIL", "设置页条件行动画", out)
		return
	results.add("PASS", "设置页条件行动画", "所有条件行都接入了展开/收起动画")

	# 间距/字号/圆角的裸字面量预算：只减不增。新增一处写死值就会顶破预算。
	code, out = runner.run_python(
		str(REPO_ROOT / "qmclient_scripts" / "check_ui_literals.py"),
		title="设置页 UI 字面量预算",
		check=False,
	)
	if code != 0:
		results.add("FAIL", "设置页 UI 字面量预算", out)
		return
	results.add("PASS", "设置页 UI 字面量预算", "裸字面量未超出预算")
