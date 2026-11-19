"""检查更新助手在父进程仍运行时能取消等待。仅使用无签名的临时附件。"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def close_wait_window(process: subprocess.Popen) -> None:
	user = ctypes.WinDLL("user32", use_last_error=True)
	user.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
	user.FindWindowExW.restype = wintypes.HWND
	user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
	user.GetWindowThreadProcessId.restype = wintypes.DWORD
	user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
	user.PostMessageW.restype = wintypes.BOOL

	deadline = time.monotonic() + 10
	while time.monotonic() < deadline and process.poll() is None:
		window = user.FindWindowExW(None, None, "QmClientUpdateWindow", None)
		while window:
			pid = wintypes.DWORD()
			user.GetWindowThreadProcessId(window, ctypes.byref(pid))
			if pid.value == process.pid:
				if not user.PostMessageW(window, 0x0010, 0, 0):  # WM_CLOSE
					raise ctypes.WinError(ctypes.get_last_error())
				return
			window = user.FindWindowExW(None, window, "QmClientUpdateWindow", None)
		time.sleep(0.02)
	raise AssertionError(f"未找到本次更新助手的窗口，退出状态为 {process.poll()}")


def scenario_cancel_while_parent_is_alive(updater: Path) -> None:
	with tempfile.TemporaryDirectory(prefix="qm-updater-wait-") as temporary:
		root = Path(temporary).resolve()
		save = root / "save"
		helper_directory = save / "qmclient"
		helper_directory.mkdir(parents=True)
		install = root / "install"
		install.mkdir()
		sentinel = install / "player.cfg"
		sentinel.write_bytes(b"keep")

		parent_pid = os.getpid()
		helper = helper_directory / f"QmClient-Updater-{parent_pid}.exe"
		shutil.copyfile(updater, helper)
		attachments = {
			"--package": "QmClient-windows.zip",
			"--package-signature": "QmClient-windows.zip.sig",
			"--manifest": "QmClient-windows-update.json",
			"--manifest-signature": "QmClient-windows-update.json.sig",
		}
		arguments = [str(helper), "--parent-pid", str(parent_pid), "--install", str(install)]
		paths = []
		for option, stem in attachments.items():
			path = save / f"{stem}.{parent_pid}.tmp"
			path.write_bytes(b"unsigned test fixture")
			paths.append(path)
			arguments.extend((option, str(path)))

		process = subprocess.Popen(arguments, cwd=root)
		try:
			close_wait_window(process)
			if process.wait(timeout=5) != 1:
				raise AssertionError("等待取消后，助手应以失败状态退出")
			if any(path.exists() for path in paths):
				raise AssertionError("等待取消后仍有下载附件残留")
			if list(install.iterdir()) != [sentinel] or sentinel.read_bytes() != b"keep":
				raise AssertionError("等待取消改变了安装目录")
		finally:
			if process.poll() is None:
				process.kill()
				process.wait(timeout=5)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--updater", type=Path, required=True)
	args = parser.parse_args()
	if os.name != "nt":
		parser.error("需要 Windows")
	updater = args.updater.resolve()
	if not updater.is_file():
		parser.error("需要工作区构建出的更新助手")
	scenario_cancel_while_parent_is_alive(updater)


if __name__ == "__main__":
	main()
