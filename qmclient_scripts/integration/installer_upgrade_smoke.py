#!/usr/bin/env python3
"""验证 Windows Setup 覆盖旧安装、清理旧随包文件及卸载保留用户文件。"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OBSOLETE_BUNDLED_FILES = (
	"data/qmclient/font_catalog.json",
	"data/qmclient/chat_emojis/agree.png",
	"data/qmclient/chat_emojis/angry.png",
	"data/qmclient/chat_emojis/awkward.png",
	"data/qmclient/chat_emojis/cute.png",
	"data/qmclient/chat_emojis/dead.png",
	"data/qmclient/chat_emojis/hehe.png",
	"data/qmclient/chat_emojis/insult.png",
	"data/qmclient/chat_emojis/kneel.png",
	"data/qmclient/chat_emojis/love.png",
	"data/qmclient/chat_emojis/no.png",
	"data/qmclient/chat_emojis/oppose.png",
	"data/qmclient/chat_emojis/question.png",
	"data/qmclient/chat_emojis/shocked.png",
	"data/qmclient/chat_emojis/smell.png",
	"data/qmclient/chat_emojis/support.png",
	"data/qmclient/chat_emojis/surrender.png",
	"data/qmclient/friendlinks/ddnet.png",
	"data/qmclient/friendlinks/ddrace.png",
	"data/qmclient/friendlinks/ddstats.png",
	"data/qmclient/friendlinks/qmclient.png",
	"data/qmclient/friendlinks/shengyan.png",
	"data/qmclient/friendlinks/teedata.png",
	"data/audio/qm_festive_haoyunlai.mp3",
	"data/qmclient/icons/qm_icons_regular_msdf.json",
	"data/qmclient/icons/qm_icons_regular_msdf.png",
	"data/qmclient/icons/qm_icons_bold_msdf.json",
	"data/qmclient/icons/qm_icons_bold_msdf.png",
	"data/qmclient/icons/qm_icons_light_msdf.json",
	"data/qmclient/icons/qm_icons_light_msdf.png",
	"data/qmclient/icons/qm_icons_fill_msdf.json",
	"data/qmclient/icons/qm_icons_fill_msdf.png",
	"data/themes/auto.png",
	"data/themes/autumn.png",
	"data/themes/heavens.png",
	"data/themes/jungle.png",
	"data/themes/newyear.png",
	"data/themes/none.png",
	"data/themes/rand.png",
	"data/themes/winter.png",
	"data/shader/textured_msdf.frag",
	"data/shader/textured_msdf.vert",
	"data/shader/vulkan/textured_msdf.frag",
	"data/shader/vulkan/textured_msdf.vert",
	"data/shader/vulkan/textured_msdf.frag.spv",
	"data/shader/vulkan/textured_msdf.vert.spv",
	"data/fonts/霞鹜文楷/LXGWWenKai-Regular.ttf",
	"data/fonts/Phosphor/Phosphor-Duotone.ttf",
	"data/qmclient/icons/qm_icons_duotone_msdf.json",
	"data/qmclient/icons/qm_icons_duotone_msdf.png",
)


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
	# 在执行安装前固定完整载荷清单；同一清单用于覆盖升级与卸载验证。
	payload_hashes = {path.relative_to(payload).as_posix(): file_digest(path) for path in sorted(payload.rglob("*")) if path.is_file()}
	if not payload_hashes:
		raise AssertionError("Setup payload contains no files")
	for required in (
		"DDNet.exe",
		"DDNet-Server.exe",
		"qm-nmt-helper.exe",
		"qm-soda-helper.exe",
		"qm-music-helper.exe",
		"qm-nmt-hook64.dll",
		"qm-nmt-bootstrap.dll",
		"data/shader/vulkan/procedural_ring.vert.spv",
		"data/shader/vulkan/procedural_ring.frag.spv",
	):
		if required not in payload_hashes:
			raise AssertionError(f"Setup payload is missing required runtime file: {required}")
	(workspace / "payload-sha256.json").write_text(json.dumps(payload_hashes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
	print(f"verifying {len(payload_hashes)} payload files, including {sum(name.endswith('.spv') for name in payload_hashes)} Vulkan shaders", flush=True)
	for executable, name in ((previous, "previous"), (current, "current")):
		if name == "current":
			if not (install / "QmClient-Setup.ini").is_file():
				raise AssertionError("previous Setup did not create the installed marker")
			# 损坏旧载荷中的同名文件，证明每项都由本次 Setup 实际覆盖。
			for relative in payload_hashes:
				old_path = install / relative
				if old_path.is_file():
					old_path.write_bytes(b"old payload file to replace")
			for relative in OBSOLETE_BUNDLED_FILES:
				old_resource = install / relative
				old_resource.parent.mkdir(parents=True, exist_ok=True)
				old_resource.write_bytes(b"obsolete bundled resource")
			(install / "user-owned.txt").write_bytes(b"preserve this file")
			old_asset = install / "data/qmclient/gui_logo.png"
			old_asset.write_bytes(b"old asset to replace")
		run_process([str(executable), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS", f"/DIR={install}", f"/LOG={workspace / (name + '.log')}"])
	for relative, expected_digest in payload_hashes.items():
		installed_path = install / relative
		if not installed_path.is_file():
			raise AssertionError(f"upgrade did not install {relative}")
		if file_digest(installed_path) != expected_digest:
			raise AssertionError(f"upgrade did not replace {relative}")
	for relative in OBSOLETE_BUNDLED_FILES:
		if (install / relative).exists():
			raise AssertionError(f"obsolete bundled resource survived Setup upgrade: {relative}")
	if (install / "user-owned.txt").read_bytes() != b"preserve this file":
		raise AssertionError("upgrade changed a user-owned file")
	run_process([str(install / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/LOG={workspace / 'uninstall.log'}"])
	for relative in payload_hashes:
		if (install / relative).exists():
			raise AssertionError(f"uninstall left installed payload file: {relative}")
	if (install / "QmClient-Setup.ini").exists():
		raise AssertionError("uninstall left installed marker")
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
