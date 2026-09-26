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
