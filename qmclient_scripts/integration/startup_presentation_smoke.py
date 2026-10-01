#!/usr/bin/env python3
"""验证真实客户端的启动字体和首次窗口呈现；所有数据留在工作区 tmp。"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tempfile
import time

try:
	from qmclient_scripts.integration.process_harness import Process
except ModuleNotFoundError:
	from process_harness import Process


class StartupEnvironment:
	def __init__(self, build_dir: Path):
		self.build_dir = build_dir.resolve()
		self.binary = self.build_dir / ("DDNet.exe" if sys.platform == "win32" else "DDNet")
		if not self.binary.is_file():
			raise RuntimeError(f"missing client: {self.binary}")
		artifact_root = Path(__file__).resolve().parents[2] / "tmp" / "startup_capture_font_fix"
		artifact_root.mkdir(parents=True, exist_ok=True)
		self.temp_dir = Path(tempfile.mkdtemp(prefix="case_", dir=artifact_root))
		(self.temp_dir / "qmclient" / "fonts").mkdir(parents=True, exist_ok=True)
		(self.temp_dir / "storage.cfg").write_text(f"add_path .\nadd_path {self.build_dir / 'data'}\n", encoding="utf-8")
		self.client: Process | None = None

	def start(self, config: list[str]) -> Process:
		arguments = [str(self.binary), "gfx_fullscreen 0", "gfx_screen_width 960", "gfx_screen_height 540",
			"gfx_backend OpenGL", "qm_graphics_mode 0", "qm_graphics_trace 1", "gfx_backgroundrender 1",
			"cl_save_settings 0", "cl_show_welcome 0", "qm_auto_update 0", "qm_steam_auto_launch 0",
			"cl_languagefile languages/simplified_chinese.txt", "tc_custom_font DejaVu Sans",
			"tc_custom_font_weight 900", *config]
		self.client = Process("client", arguments, self.temp_dir, fifo_command="cl_input_fifo", pipe_prefix="qmclient_startup_")
		return self.client

	def finish_startup(self) -> None:
		assert self.client is not None
		self.client.command("echo qm_startup_ready")
		self.client.wait_for(lambda line: "qm_startup_ready" in line, "main loop ready", 30)

	def quit(self) -> None:
		assert self.client is not None
		self.client.command("quit")
		if self.client.wait_for_exit(20) != 0:
			raise AssertionError("client did not exit normally")

	def close(self) -> None:
		if self.client is not None:
			self.client.kill()
			(self.temp_dir / "client.log").write_text("\n".join(self.client._lines) + "\n", encoding="utf-8")


def first_cjk_glyph(client: Process) -> tuple[str, int]:
	line = client.wait_for(lambda value: "event=first_cjk_glyph" in value, "first rendered CJK glyph", 30)
	match = re.search(r"family='([^']+)' style='[^']*' weight=(-?\d+)", line)
	if match is None:
		raise AssertionError(f"invalid glyph diagnostic: {line}")
	return match.group(1), int(match.group(2))


def smoke_cjk_default_weight(env: StartupEnvironment) -> None:
	client = env.start(["tc_custom_font_cjk Noto Sans SC", "tc_custom_font_weight_cjk 400"])
	family, weight = first_cjk_glyph(client)
	if (family, weight) != ("Noto Sans SC", 400):
		raise AssertionError(f"first CJK glyph used {family!r} weight={weight}; expected the configured CJK weight 400")
	env.finish_startup()
	env.quit()


def smoke_cjk_nondefault_weight(env: StartupEnvironment) -> None:
	client = env.start(["tc_custom_font_cjk Noto Sans SC", "tc_custom_font_weight_cjk 550"])
	family, weight = first_cjk_glyph(client)
	if (family, weight) != ("Noto Sans SC", 550):
		raise AssertionError(f"first CJK glyph used {family!r} weight={weight}; expected configured weight 550")
	env.finish_startup()
	env.quit()


def smoke_missing_cjk_family(env: StartupEnvironment) -> None:
	client = env.start(["tc_custom_font_cjk QmClient Missing Test Font"])
	family, weight = first_cjk_glyph(client)
	if (family, weight) != ("Noto Sans SC", 900):
		raise AssertionError(f"missing custom CJK face did not follow the configured default chain: {family!r}, {weight}")
	env.finish_startup()
	env.quit()


def smoke_cjk_custom_family(env: StartupEnvironment) -> None:
	client = env.start(["tc_custom_font_cjk Source Han Sans SC"])
	family, _ = first_cjk_glyph(client)
	if family != "Source Han Sans SC":
		raise AssertionError(f"first CJK glyph used {family!r}; expected the configured CJK family")
	env.finish_startup()
	env.quit()


def smoke_shared_font_weight(env: StartupEnvironment) -> None:
	client = env.start(["tc_custom_font Noto Sans SC", "tc_custom_font_cjk Noto Sans SC", "tc_custom_font_weight_cjk 400"])
	family, weight = first_cjk_glyph(client)
	if (family, weight) != ("Noto Sans SC", 900):
		raise AssertionError(f"a shared Latin/CJK face used {family!r} weight={weight}; expected Latin weight 900")
	env.finish_startup()
	env.quit()


def check_window_presentation(env: StartupEnvironment, config: list[str]) -> None:
	client = env.start(["gfx_fullscreen 2", "gfx_backgroundrender 0", *config])
	client.wait_for(lambda line: "event=startup_show" in line, "visible startup window", 30)
	env.finish_startup()
	client.command("screenshot")
	deadline = time.monotonic() + 15
	while not list((env.temp_dir / "screenshots").glob("*.png")):
		if not client.is_alive() or time.monotonic() >= deadline:
			raise AssertionError("visible client did not produce a screenshot")
		time.sleep(0.05)
	env.quit()
	shows = [line for line in client._lines if "event=startup_show" in line]
	if len(shows) != 1:
		raise AssertionError(f"startup window was shown {len(shows)} times")
	match = re.search(r"flags=(\d+)", shows[0])
	if match is None or int(match.group(1)) & 4 == 0 or int(match.group(1)) & (8 | 64):
		raise AssertionError(f"startup window was hidden or minimized: {shows[0]}")


def smoke_window_presentation(env: StartupEnvironment) -> None:
	check_window_presentation(env, [])


def smoke_vulkan_window_presentation(env: StartupEnvironment) -> None:
	check_window_presentation(env, ["qm_graphics_mode 1", "gfx_backend Vulkan"])


SMOKE_TESTS = {
	"cjk_default_weight": smoke_cjk_default_weight,
	"cjk_custom_family": smoke_cjk_custom_family,
	"cjk_nondefault_weight": smoke_cjk_nondefault_weight,
	"missing_cjk_family": smoke_missing_cjk_family,
	"shared_font_weight": smoke_shared_font_weight,
	"window_presentation": smoke_window_presentation,
	"vulkan_window_presentation": smoke_vulkan_window_presentation,
}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=sorted(SMOKE_TESTS), nargs="?")
	args = parser.parse_args()
	tests = {args.test: SMOKE_TESTS[args.test]} if args.test else SMOKE_TESTS
	failed = 0
	for name, test in tests.items():
		env = StartupEnvironment(args.build_dir)
		try:
			test(env)
		except Exception as exc:
			failed += 1
			print(f"{name}: FAILED: {exc}", file=sys.stderr)
		else:
			print(f"{name}: passed")
		finally:
			env.close()
			print(f"artifacts: {env.temp_dir}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
