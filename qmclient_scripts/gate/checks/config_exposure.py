# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""配置暴露审计：新增 Qm 配置必须接线到设置页，或在基线里登记原因。"""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]
TRIGGER_PATHS = {
	"src/engine/shared/config_variables.h",
	"src/engine/shared/config_variables_tclient.h",
	"src/engine/shared/config_variables_qmclient.h",
	"qmclient_scripts/config_exposure_baseline.json",
	"qmclient_scripts/check_config_exposure.py",
}
TRIGGER_PREFIXES = (
	"src/game/client/QmUi/",
	"src/game/client/components/qmclient/",
	"src/game/client/components/tclient/",
	"src/game/client/components/menus",
	"src/game/client/components/hud_editor",
	"src/game/editor/popups",
)


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return bool(TRIGGER_PATHS.intersection(normalized)) or any(
		path.startswith(prefix) for path in normalized for prefix in TRIGGER_PREFIXES
	)


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("配置暴露审计", "改动范围未触及配置声明或设置页入口")
		return
	if dry_run:
		results.add("INFO", "配置暴露审计", "DryRun，仅展示命令")
		return
	code, out = runner.run_python(
		str(REPO_ROOT / "qmclient_scripts" / "check_config_exposure.py"),
		title="配置暴露审计",
		check=False,
	)
	if code != 0:
		results.add("FAIL", "配置暴露审计", out)
		return
	results.add("PASS", "配置暴露审计", "新增配置均有设置页入口或基线登记")
