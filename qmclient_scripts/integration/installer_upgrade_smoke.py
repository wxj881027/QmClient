#!/usr/bin/env python3
"""验证 Windows Setup 覆盖旧安装、清理旧随包文件及卸载保留用户文件。"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def run_process(arguments: list[str]) -> None:
	completed = subprocess.run(arguments, check=False, timeout=180)
	if completed.returncode != 0:
		raise RuntimeError(f"installer exited with {completed.returncode}: {arguments[0]}")


def file_digest(path: Path) -> str:
	with path.open("rb") as source:
		return hashlib.file_digest(source, "sha256").hexdigest()


def smoke_setup_upgrade(previous: Path, current: Path, payload: Path, workspace: Path) -> None:
	# 编译输入须使用独立 SetupAppId，不能修改正式安装的卸载登记。
	install = workspace / "installed"
	for executable, name in ((previous, "previous"), (current, "current")):
		if name == "current":
			if not (install / "QmClient-Setup.ini").is_file():
				raise AssertionError("previous Setup did not create the installed marker")
			(install / "DDNet.exe").write_bytes(b"old executable to replace")
			old_font = install / "data/fonts/霞鹜文楷/LXGWWenKai-Regular.ttf"
			old_font.parent.mkdir(parents=True, exist_ok=True)
			old_font.write_bytes(b"obsolete bundled font")
			(install / "user-owned.txt").write_bytes(b"preserve this file")
			old_asset = install / "data/qmclient/gui_logo.png"
			old_asset.write_bytes(b"old asset to replace")
		run_process([str(executable), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS", f"/DIR={install}", f"/LOG={workspace / (name + '.log')}"])
	for relative in ("DDNet.exe", "DDNet-Server.exe", "data/qmclient/gui_logo.png"):
		if file_digest(install / relative) != file_digest(payload / relative):
			raise AssertionError(f"upgrade did not replace {relative}")
	if (install / "data/fonts/霞鹜文楷/LXGWWenKai-Regular.ttf").exists():
		raise AssertionError("obsolete bundled font survived Setup upgrade")
	if (install / "user-owned.txt").read_bytes() != b"preserve this file":
		raise AssertionError("upgrade changed a user-owned file")
	run_process([str(install / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/LOG={workspace / 'uninstall.log'}"])
	if (install / "DDNet.exe").exists() or (install / "QmClient-Setup.ini").exists():
		raise AssertionError("uninstall left installed executable or marker")
	if (install / "user-owned.txt").read_bytes() != b"preserve this file":
		raise AssertionError("uninstall removed a user-owned file")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--previous", type=Path, required=True)
	parser.add_argument("--current", type=Path, required=True)
	parser.add_argument("--payload", type=Path, required=True)
	args = parser.parse_args()
	if sys.platform != "win32":
		parser.error("Windows is required")
	workspace = REPO_ROOT / "tmp" / f"setup-upgrade-{uuid.uuid4().hex}"
	workspace.mkdir(parents=True)
	print(f"artifacts: {workspace}", flush=True)
	smoke_setup_upgrade(args.previous.resolve(strict=True), args.current.resolve(strict=True), args.payload.resolve(strict=True), workspace)
	print("Setup install, upgrade and uninstall: passed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
