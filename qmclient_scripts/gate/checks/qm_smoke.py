"""Run QmClient-owned process tests when explicitly requested."""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]


def run(results: ResultCollector, included: list[str], dry_run: bool = False, build_dir: str = "cmake-build-release") -> None:
	python_cmd = runner.resolve_python_cmd()
	if not python_cmd:
		results.add("WARN", "QmClient 进程测试", "未找到可用 Python，已跳过")
		return
	tests = (
		("冒烟", REPO_ROOT / "qmclient_scripts" / "integration" / "qmclient_smoke.py", "QmClient process smoke tests"),
		("端到端", REPO_ROOT / "qmclient_scripts" / "integration" / "e2e_qmclient.py", "QmClient process end-to-end tests"),
	)
	outputs: list[str] = []
	for label, script, title in tests:
		command = [python_cmd, str(script), build_dir]
		if dry_run:
			outputs.append(f"{label}: DryRun: " + " ".join(command))
			continue
		code, output = runner.run(command, title=title, check=False)
		outputs.append(output)
		if code != 0:
			results.add("FAIL", "QmClient 进程测试", "\n".join(outputs))
			return
	if dry_run:
		results.add("INFO", "QmClient 进程测试", "\n".join(outputs))
	else:
		results.add("PASS", "QmClient 进程测试", "\n".join(outputs))
