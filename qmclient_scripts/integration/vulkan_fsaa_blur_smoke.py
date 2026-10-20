#!/usr/bin/env python3
"""真实 Vulkan FSAA 模糊回归：捕获启用、开关重入、连续截图和退出资源释放。

需要可见桌面、支持 Vulkan MSAA 的 GPU 与 Vulkan SDK 验证层。
所有截图与日志放隔离目录；图片保留供视觉验收，不以图片不同冒充模糊有效。
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys
import struct
import time

try:
	from qmclient_scripts.integration.startup_presentation_smoke import StartupEnvironment
except ModuleNotFoundError:
	from startup_presentation_smoke import StartupEnvironment


def focus_client_window(client) -> bool:
	import ctypes
	from ctypes import wintypes

	user32 = ctypes.WinDLL("user32", use_last_error=True)
	kernel32 = ctypes.WinDLL("kernel32")
	callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
	user32.GetForegroundWindow.restype = wintypes.HWND
	user32.IsWindowVisible.argtypes = [wintypes.HWND]
	user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
	user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
	user32.SetForegroundWindow.argtypes = [wintypes.HWND]
	user32.BringWindowToTop.argtypes = [wintypes.HWND]
	user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
	user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
	kernel32.GetCurrentThreadId.restype = wintypes.DWORD
	windows = []

	@callback_type
	def visit(window, _):
		owner = wintypes.DWORD()
		user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
		if owner.value == client._process.pid and user32.IsWindowVisible(window):
			title = ctypes.create_unicode_buffer(256)
			user32.GetWindowTextW(window, title, len(title))
			# SDL 的探测窗口不能被 ShowWindow 意外显示并抢走客户端焦点。
			if title.value and title.value != "Temp Window":
				windows.append(window)
		return True

	user32.EnumWindows(visit, 0)
	for window in windows:
		thread = kernel32.GetCurrentThreadId()
		foreground = user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), None)
		# 仅聚焦本场景开发实例；临时连接前台输入队列后立即恢复，避免 Windows 拒绝聚焦。
		attached = user32.AttachThreadInput(thread, foreground, True) if foreground else False
		try:
			user32.ShowWindow(window, 9)
			user32.BringWindowToTop(window)
			user32.SetForegroundWindow(window)
		finally:
			if attached:
				user32.AttachThreadInput(thread, foreground, False)
		if user32.GetForegroundWindow() == window:
			return True
	return False


def smoke_fsaa_blur(env: StartupEnvironment, samples: int, render_threads: int = 3) -> None:
	# 断言只依赖本机 GPU 与文件；启动 HTTP 请求快速失败，不依赖公网响应。
	for key in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
		os.environ[key] = "http://127.0.0.1:1"
	os.environ["no_proxy"] = os.environ["NO_PROXY"] = ""
	client = env.start([
		"gfx_fullscreen 0", "gfx_backend Vulkan", "qm_graphics_mode 1",
		f"gfx_render_thread_count {render_threads}",
		f"gfx_fsaa_samples {samples}", "qm_gaussian_blur 1", "dbg_gfx 4",
		"ui_page 16", "cl_skip_start_menu 1", "qm_sponsor_nudge 0",
	])
	env.finish_startup()
	client.wait_for(lambda line: "VK_LAYER_KHRONOS_validation" in line, "Vulkan validation layer", 30)
	client.wait_for(lambda line: "capture probe: active" in line and f"multisample={int(samples > 0)}" in line, "MSAA backbuffer capture", 30)
	for index, enabled in enumerate((1, 0, 1)):
		client.command(f"qm_gaussian_blur {enabled}")
		if index == 2:
			# 重建 framebuffer 后必须安全复用此前按交换链图片缓存的 secondary。
			client.command("gfx_screen_height 620")
		marker = f"qm_fsaa_blur_step_{index}"
		client.command(f"echo {marker}")
		client.wait_for(lambda line: marker in line, "blur toggle consumed", 10)
		# 文件名按秒生成，避开同秒覆盖并让截图动画完成；不用于等待 GPU 完成。
		time.sleep(1.1)
		# 每个步骤只接受新截图；wait_for 会搜索旧日志，不能用于重复截图事件。
		start = len(client._lines)
		deadline = time.monotonic() + 20
		retry = 0.0
		while not any("Saved screenshot to '" in line for line in client._lines[start:]):
			if not client.is_alive() or time.monotonic() >= deadline:
				raise AssertionError(f"FSAA {samples}, blur step {index}: fresh screenshot missing")
			if time.monotonic() >= retry:
				if sys.platform == "win32":
					focus_client_window(client)
				client.command("screenshot")
				retry = time.monotonic() + 1
			time.sleep(0.05)
	# 读取 PNG 尺寸确认确实经历了窗口/交换链重建，而非只消费配置命令。
	latest = max((env.temp_dir / "screenshots").glob("*.png"), key=lambda path: path.stat().st_mtime_ns)
	if struct.unpack(">II", latest.read_bytes()[16:24]) != (960, 620):
		raise AssertionError("post-resize screenshot does not match requested drawable extent")
	env.quit()
	# 退出码 0 不能证明 GPU 成功；必须检查验证层和设备丢失日志。
	# 上游仍向 vkCreateDevice 传 device layer，单独排除此已知注册问题。
	errors = [line for line in client._lines if
		("validation error:" in line and "vkCreateDevice()" not in line) or
		"device lost" in line.lower() or "frame submit failed" in line]
	if errors:
		raise AssertionError("\n".join(errors))


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("--validation-layer", type=Path, required=True, help="SDK directory containing VkLayer_khronos_validation.json")
	parser.add_argument("--samples", type=int, nargs="+", choices=(0, 2, 4, 8), default=[0, 4, 8, 4])
	parser.add_argument("--render-threads", type=int, nargs="+", choices=(1, 3), default=[1, 3])
	args = parser.parse_args()
	if not (args.validation_layer / "VkLayer_khronos_validation.json").is_file():
		parser.error("Vulkan validation layer manifest missing")
	os.environ["VK_LAYER_PATH"] = str(args.validation_layer.resolve())
	for render_threads in args.render_threads:
		for samples in args.samples:
			env = StartupEnvironment(args.build_dir)
			try:
				smoke_fsaa_blur(env, samples, render_threads)
				print(f"FSAA {samples}, threads {render_threads}: capture, blur on/off/on, fresh screenshots and cleanup passed")
			finally:
				env.close()
				print(f"artifacts: {env.temp_dir}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
