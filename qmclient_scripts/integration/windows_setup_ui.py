"""通过真实 Windows 安装向导检查目录页，不操作正式安装。"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import subprocess
import threading
import time
from pathlib import Path


def assert_setup_directory_page(setup: Path, expected: Path, *, override: bool) -> None:
	_inspect_setup_wizard(setup, expected, override=override, completion=False)


def assert_setup_completion_page(setup: Path, install: Path) -> None:
	_inspect_setup_wizard(setup, install, override=True, completion=True)


def _inspect_setup_wizard(setup: Path, expected: Path, *, override: bool, completion: bool) -> None:
	# Setup 的加载器会产生子进程，窗口和清理范围均限定在本次进程树。
	kernel = ctypes.WinDLL("kernel32", use_last_error=True)
	user = ctypes.WinDLL("user32", use_last_error=True)
	callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

	class ProcessEntry(ctypes.Structure):
		_fields_ = [
			("size", wintypes.DWORD), ("usage", wintypes.DWORD),
			("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
			("module", wintypes.DWORD), ("threads", wintypes.DWORD),
			("parent", wintypes.DWORD), ("priority", wintypes.LONG),
			("flags", wintypes.DWORD), ("exe", wintypes.WCHAR * 260),
		]

	kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
	kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
	kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
	kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
	kernel.CloseHandle.argtypes = [wintypes.HANDLE]
	kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
	kernel.OpenProcess.restype = wintypes.HANDLE
	kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
	user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
	user.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]
	user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
	user.IsWindowVisible.argtypes = [wintypes.HWND]
	user.IsWindowEnabled.argtypes = [wintypes.HWND]
	user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
	user.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
	user.SendMessageTimeoutW.restype = ctypes.c_ssize_t
	startup = subprocess.STARTUPINFO()
	startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
	startup.wShowWindow = 7
	arguments = [str(setup), "/LANG=zh_cn", "/NORESTART", "/NOICONS"]
	if override:
		arguments.append(f"/DIR={expected}")
	process = subprocess.Popen(arguments, startupinfo=startup)
	known = {process.pid}

	def descendants() -> set[int]:
		snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
		if snapshot == ctypes.c_void_p(-1).value:
			raise ctypes.WinError(ctypes.get_last_error())
		try:
			entry = ProcessEntry()
			entry.size = ctypes.sizeof(entry)
			parents = {}
			available = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
			while available:
				parents[entry.pid] = entry.parent
				available = kernel.Process32NextW(snapshot, ctypes.byref(entry))
			while True:
				new = {pid for pid, parent in parents.items() if parent in known} - known
				if not new:
					return known
				known.update(new)
		finally:
			kernel.CloseHandle(snapshot)

	def text(handle: int, *, class_name: bool = False) -> str:
		buffer = ctypes.create_unicode_buffer(4096)
		if class_name:
			user.GetClassNameW(handle, buffer, len(buffer))
		else:
			result = ctypes.c_size_t()
			user.SendMessageTimeoutW(handle, 0x000D, len(buffer), ctypes.addressof(buffer), 2, 2000, ctypes.byref(result))
		return buffer.value

	deadline = time.monotonic() + (180 if completion else 30)
	last_next = None
	observed = []
	install_clicked = False
	try:
		while time.monotonic() < deadline:
			pids = descendants()
			windows = []
			@callback_type
			def collect_window(handle, _):
				pid = wintypes.DWORD()
				user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
				if pid.value in pids and text(handle, class_name=True) == "TWizardForm":
					windows.append(handle)
				return True
			user.EnumWindows(collect_window, 0)
			for window in windows:
				controls = []
				@callback_type
				def collect_control(handle, _):
					if user.IsWindowVisible(handle):
						controls.append((handle, text(handle, class_name=True), text(handle)))
					return True
				user.EnumChildWindows(window, collect_control, 0)
				observed = [(kind, value) for _, kind, value in controls]
				for handle, kind, value in controls:
					if not completion and kind.endswith("Edit") and value and Path(value) == expected:
						if not user.IsWindowEnabled(handle):
							raise AssertionError("installation directory cannot be edited")
						return
				buttons = [(handle, value) for handle, kind, value in controls if kind == "TNewButton" and user.IsWindowEnabled(handle)]
				if completion and any(value.replace("&", "").strip() in {"完成", "Finish"} for _, value in buttons):
					labels = [value for _, kind, value in controls if kind == "TNewStaticText"]
					if sum(value.count("退出安装程序") for value in labels) != 1:
						raise AssertionError(f"completion page must explain Finish exactly once: {labels}")
					if not any("QmClient 已安装到您的计算机。" in value for value in labels):
						raise AssertionError(f"completion page lost the installation result: {labels}")
					return
				for handle, value in buttons:
					if value.replace("&", "").strip() in {"安装", "Install"}:
						if not completion:
							raise AssertionError(f"Setup skipped the editable directory page: {observed}")
						if not install_clicked:
							install_clicked = True
							result = ctypes.c_size_t()
							user.SendMessageTimeoutW(handle, 0x00F5, 0, 0, 2, 2000, ctypes.byref(result))
				for handle, value in buttons:
					if value.replace("&", "").strip() in {"下一步", "Next >", "Next"} and last_next != observed:
						last_next = observed
						result = ctypes.c_size_t()
						user.SendMessageTimeoutW(handle, 0x00F5, 0, 0, 2, 2000, ctypes.byref(result))
			threading.Event().wait(0.05)
		raise AssertionError(f"Setup page not observed before timeout: completion={completion}, controls={observed}")
	finally:
		# 目录探针不点击安装；完成页探针不点击完成，防止启动普通客户端污染用户配置。
		for pid in descendants() - {process.pid}:
			handle = kernel.OpenProcess(1, False, pid)
			if handle:
				kernel.TerminateProcess(handle, 1)
				kernel.CloseHandle(handle)
		if process.poll() is None:
			process.kill()
		process.wait(timeout=10)
