#!/usr/bin/env python3
"""显式公网探测：真实隔离客户端通过正常 TLS 应用官方、镜像和 GitHub 列表。"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile
import threading
import time

try:
	from qmclient_scripts.integration.e2e_serverlist import STATUS
	from qmclient_scripts.integration.process_build import prepare_process_build, read_cmake_cache
	from qmclient_scripts.integration.process_harness import EXE_SUFFIX, Process
except ModuleNotFoundError:
	from e2e_serverlist import STATUS
	from process_build import prepare_process_build, read_cmake_cache
	from process_harness import EXE_SUFFIX, Process


SOURCES = {
	"official": "https://master1.ddnet.org/ddnet/15/servers.json",
	"mirror": "https://ddrace.cn/ddnet/15/servers.json",
	"github": "https://raw.githubusercontent.com/wxj881027/QmClient/serverlist-snapshot/servers.json",
	"doh": "https://master1.ddnet.org/ddnet/15/servers.json",
}


def probe(build: Path, root: Path, role: str, timeout: float) -> dict:
	artifacts = Path(tempfile.mkdtemp(prefix=f"{role}-", dir=root))
	url = SOURCES[role]
	(artifacts / "ddnet-serverlist-urls.cfg").write_text(url + "\n", encoding="utf-8")
	(artifacts / "storage.cfg").write_text(f"add_path .\nadd_path {build / 'data'}\n", encoding="utf-8")
	# 只清理本次子进程的代理与测试映射；使用真实公网地址和系统证书存储。
	env = {key: "" for key in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy", "QMCLIENT_TEST_SERVERLIST_ORIGIN")}
	env.update({"QMCLIENT_TEST_STORAGE_ROOT": str(artifacts), "QMCLIENT_TEST_HIDE_DIALOG": "1", "QMCLIENT_TEST_SERVERLIST_DOH": "1" if role == "doh" else ""})
	config = ["dbg_http 1", "loglevel 1", "stdout_output_level 1", "gfx_fullscreen 0", "gfx_screen_width 640", "gfx_screen_height 480", "snd_enable 0", "cl_show_welcome 0", "cl_skip_start_menu 1", "cl_save_settings 0", "cl_menu_map \"\"", "qm_steam_auto_launch 0", "http_allow_insecure 0", "qm_websocket_url ws://127.0.0.1:1/ws", "minimize"]
	client: Process | None = None
	result = {"role": role, "url": url, "tls_verification": True, "proxy": "direct", "artifacts": str(artifacts), "outcome": "ERROR"}
	try:
		client = Process(f"public-{role}", [str(build / f"DDNet{EXE_SUFFIX}"), *config], artifacts, fifo_command="cl_input_fifo", pipe_prefix="serverlist_public_", env=env)
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 45)
		client.command("qm_serverlist_refresh")
		deadline = time.monotonic() + timeout
		token = 0
		while time.monotonic() < deadline:
			token += 1
			client.command(f"qm_serverlist_status {token}")
			line = client.wait_for(lambda value: bool((match := STATUS.fullmatch(value)) and int(match[1]) == token), f"consumer token {token}", 5)
			match = STATUS.fullmatch(line)
			assert match
			state = {"count": int(match[2]), "revision": int(match[3]), "stale": int(match[4]), "error": int(match[5]), "first_name": match[6]}
			result["consumer"] = state
			if state["count"] > 0 and state["revision"] > 0 and state["first_name"]:
				if role == "doh" and not any(f"url='{url}' dns=encrypted" in value for value in client._lines):
					raise AssertionError("DoH probe did not enable the production encrypted DNS path")
				result["outcome"] = "REACHABLE"
				break
			if state["error"]:
				result["outcome"] = "NO_LIST"
				break
			threading.Event().wait(0.2)
		else:
			result["outcome"] = "DEADLINE"
		client.command("quit")
		code = client.wait_for_exit(15)
		if code != 0:
			raise RuntimeError(f"client exit code {code}")
	except Exception as error:
		result["outcome"] = "ERROR"
		result["error"] = str(error)
	finally:
		if client:
			client.stop()
			(artifacts / "client.log").write_text("\n".join(client._lines), encoding="utf-8")
			result["network_evidence"] = [line for line in client._lines if "serverbrowser_http" in line or (url in line and ("curl" in line or "http" in line))]
		(artifacts / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
	print(f"{role}: {result['outcome']}; consumer={result.get('consumer')}; artifacts={artifacts}", flush=True)
	return result


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("--live", action="store_true", help="明确允许访问公网；不用于离线 gate")
	parser.add_argument("--source", choices=sorted(SOURCES), nargs="+", default=list(SOURCES))
	parser.add_argument("--timeout", type=float, default=40, help="每项启动后网络观察期限，5 到 120 秒")
	args = parser.parse_args()
	if not args.live:
		parser.error("公网探测必须显式传入 --live")
	if not 5 <= args.timeout <= 120:
		parser.error("--timeout 必须在 5 到 120 秒之间")
	build = prepare_process_build(args.build_dir)
	cache = read_cmake_cache(build)
	if cache.get("QMCLIENT_TEST_STORAGE") != "ON" or cache.get("DEV") != "ON":
		raise RuntimeError("public probe requires DEV=ON and QMCLIENT_TEST_STORAGE=ON")
	tmp = Path(__file__).resolve().parents[2] / "tmp"
	tmp.mkdir(exist_ok=True)
	root = Path(tempfile.mkdtemp(prefix="public-serverlist-", dir=tmp))
	results = [probe(build, root, role, args.timeout) for role in args.source]
	(root / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
	print(f"report: {root / 'results.json'}", flush=True)
	# 公网不可达单独返回 2；客户端崩溃、观察点或编排失败返回 1。
	if any(result["outcome"] == "ERROR" for result in results):
		return 1
	return 0 if all(result["outcome"] == "REACHABLE" for result in results) else 2


if __name__ == "__main__":
	raise SystemExit(main())
