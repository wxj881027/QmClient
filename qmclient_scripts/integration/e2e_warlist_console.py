#!/usr/bin/env python3
"""在 Windows 隔离测试客户端验证战斗名单的控制台分组参数。"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import uuid

try:
	from qmclient_scripts.integration.e2e_config_persistence import REPO_ROOT, start_client
except ModuleNotFoundError:
	from e2e_config_persistence import REPO_ROOT, start_client


def scenario_warlist_removal_requires_the_named_group(binary: Path, workspace: Path) -> None:
	root = workspace / "named-group"
	warlist = root / "qmclient" / "qmclient_warlist.cfg"
	warlist.parent.mkdir(parents=True)
	kept = [
		r'add_war_entry "none" "KeepName" "" "quoted \"note\""',
		'add_war_entry "none" "" "KeepClan" "first\tsecond"',
	]
	removed = [
		'add_war_entry "none" "DropName" "" "remove"',
		'add_war_entry "none" "" "DropClan" "remove"',
	]
	warlist.write_text("\n".join(kept + removed) + "\n", encoding="utf-8")

	client = start_client(binary, root, "warlist-named-group")
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		client.command("remove_war_name missing KeepName")
		client.command("remove_war_clan missing KeepClan")
		client.command("remove_war_name none DropName")
		client.command("remove_war_clan none DropClan")
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("client did not exit normally")
		saved = [line for line in warlist.read_text(encoding="utf-8").splitlines() if line.startswith("add_war_entry ")]
		if saved != kept:
			raise AssertionError(f"unexpected saved war list entries: {saved!r}")
	finally:
		client.stop()


def scenario_warlist_clan_commands_reject_noninteger_group_indices(binary: Path, workspace: Path) -> None:
	client = start_client(binary, workspace / "clan-indices", "warlist-clan-indices")
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		for command in ("war_clan_index", "remove_war_clan_index"):
			group = f"{command}_typo"
			client.command(f"{command} {group} Player")
			client.wait_for(lambda line: f"{group} is not a valid integer." in line, f"{command} integer validation", 10)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("client did not exit normally")
	finally:
		client.stop()


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path, help="Windows 隔离测试构建目录")
	args = parser.parse_args()
	if os.name != "nt":
		parser.error("this runner requires the Windows isolated test client")
	build_dir = args.build_dir.resolve()
	binary = build_dir / "DDNet.exe"
	if not binary.is_relative_to(REPO_ROOT) or not binary.is_file():
		parser.error("client executable must exist inside this workspace")
	cache = build_dir / "CMakeCache.txt"
	cache_lines = cache.read_text(encoding="utf-8").splitlines() if cache.is_file() else []
	if "DEV:BOOL=ON" not in cache_lines or "QMCLIENT_TEST_STORAGE:BOOL=ON" not in cache_lines:
		parser.error("requires DEV=ON and QMCLIENT_TEST_STORAGE=ON; refusing to use real user storage")
	workspace = REPO_ROOT / "tmp" / f"warlist-console-{uuid.uuid4().hex}"
	scenario_warlist_removal_requires_the_named_group(binary, workspace)
	scenario_warlist_clan_commands_reject_noninteger_group_indices(binary, workspace)
	print(f"war list console scenarios passed; artifacts: {workspace}")


if __name__ == "__main__":
	main()
