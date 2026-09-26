#!/usr/bin/env python3
"""QmClient Windows 喜庆崩溃窗口真实进程冒烟。

这里验证的是**运行期行为**，不是源码文本：

* 预览进程 `--qm-preview-crash-dialog <type>` 能真的弹出窗口；
* 窗口是节日崩溃窗口（类名 `QmClientFestiveCrashWindow`、标题 `QmClient · 好运还在`）；
* 报告正文已中文化（含【问题判断】/【建议操作】/【中文化报告】小节）；
* 窗口出现后自动播放环绕边框的粒子烟花（进程存活、窗口仍在、正文不变）；
* 画面真的在动：连续抓帧出现多个不同帧，且变化像素落在烟花区域；
* 动画会自行停止：帧内容重新稳定，且稳定帧与动画帧不同；
* “关闭报告”能正常结束窗口，进程返回码为 0，且不留残留窗口。

仅在 Windows 上运行（依赖 Win32 GDI 窗口）。其他平台直接跳过并返回 0，
因为该对话框本身只存在于 Windows 构建中。

运行：

```text
python qmclient_scripts/integration/crash_dialog_smoke.py <build-dir>
python qmclient_scripts/integration/crash_dialog_smoke.py <build-dir> crash_dialog_fatal
```

设置 `QM_SMOKE_ARTIFACTS_DIR=<dir>` 可把关键抓帧导出为 .bmp，便于人工查看。
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
import queue
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

try:
	from qmclient_scripts.integration.process_harness import EXE_SUFFIX, format_exit_code
except ModuleNotFoundError:
	from process_harness import EXE_SUFFIX, format_exit_code  # type: ignore[no-redef]


WINDOW_CLASS = "QmClientFestiveCrashWindow"
WINDOW_TITLE = "QmClient · 好运还在"
CLOSE_LABEL = "关闭报告"
REQUIRED_BUTTONS = ("打开崩溃报告", CLOSE_LABEL)
REQUIRED_REPORT_SECTIONS = ("【问题判断】", "【建议操作】", "【中文化报告】")

PREVIEW_TYPES = ("graphics", "assertion", "fatal", "hang")

WINDOW_TIMEOUT = 30.0
EXIT_TIMEOUT = 20.0
ANIMATION_SAMPLE_INTERVAL = 0.2
ANIMATION_STABLE_DEADLINE = 15.0
MIN_ANIMATION_FRAMES = 3
MIN_FIREWORKS_PIXELS = 24

WM_CLOSE = 0x0010
WM_GETTEXT = 0x000D
WM_COMMAND = 0x0111
BN_CLICKED = 0x0000
BM_CLICK = 0x00F5
SRCCOPY = 0x00CC0020
PW_RENDERFULLCONTENT = 0x00000002
DIB_RGB_COLORS = 0
BI_RGB = 0
RDW_INVALIDATE = 0x0001
RDW_UPDATENOW = 0x0100


@dataclass(frozen=True)
class WindowCapture:
	"""一次窗口抓帧：32bpp BGRA、自上而下。"""

	width: int
	height: int
	pixels: bytes

	def signature(self) -> str:
		# GDI 的 BGRA alpha 通道未定义，不能把它纳入帧稳定性判断。
		return hashlib.sha1(self.pixels[0::4] + self.pixels[1::4] + self.pixels[2::4]).hexdigest()

	def changed_pixels(self, other: WindowCapture) -> int:
		return _changed_pixels(self, other, 0, min(self.height, other.height))

	def is_uniform(self) -> bool:
		"""抓帧是否为纯色（用于识别“抓到空白窗口”这种环境失败）。"""
		total = len(self.pixels) // 4
		if total < 2:
			return True
		step = max(1, total // 256)
		samples = {self.pixels[offset:offset + 4] for offset in range(0, total * 4, step * 4)}
		return len(samples) <= 1


def _changed_pixels(first: WindowCapture, second: WindowCapture, row_start: int, row_end: int) -> int:
	if first.width != second.width:
		raise RuntimeError(f"capture size changed: {first.width}x{first.height} -> {second.width}x{second.height}")
	changed = 0
	stride = first.width * 4
	for row in range(max(0, row_start), row_end):
		offset = row * stride
		left = first.pixels[offset:offset + stride]
		right = second.pixels[offset:offset + stride]
		if left == right:
			continue
		for column in range(0, stride, 4):
			if left[column:column + 3] != right[column:column + 3]:
				changed += 1
	return changed


def _write_bmp(path: Path, capture: WindowCapture) -> None:
	row_size = ((capture.width * 3 + 3) // 4) * 4
	pixel_data = bytearray()
	for row in range(capture.height - 1, -1, -1):
		offset = row * capture.width * 4
		line = bytearray(row_size)
		for column in range(capture.width):
			source = offset + column * 4
			target = column * 3
			line[target] = capture.pixels[source]
			line[target + 1] = capture.pixels[source + 1]
			line[target + 2] = capture.pixels[source + 2]
		pixel_data += line
	file_header = b"BM" + (14 + 40 + len(pixel_data)).to_bytes(4, "little") + b"\0\0\0\0" + (14 + 40).to_bytes(4, "little")
	info_header = (
		(40).to_bytes(4, "little")
		+ capture.width.to_bytes(4, "little", signed=True)
		+ capture.height.to_bytes(4, "little", signed=True)
		+ (1).to_bytes(2, "little")
		+ (24).to_bytes(2, "little")
		+ (BI_RGB).to_bytes(4, "little")
		+ len(pixel_data).to_bytes(4, "little")
		+ b"\0" * 16
	)
	path.write_bytes(file_header + info_header + bytes(pixel_data))


class _Win32:
	"""Win32 API 绑定。只在 Windows 上实例化。"""

	def __init__(self) -> None:
		self.user32 = ctypes.WinDLL("user32", use_last_error=True)
		self.gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
		# 与被测窗口保持同一坐标空间；否则 GetWindowRect 会返回 DPI 虚拟化
		# 坐标，屏幕 BitBlt 在缩放桌面上会抓成“半桌面+半窗口”。
		set_dpi_context = getattr(self.user32, "SetProcessDpiAwarenessContext", None)
		if set_dpi_context is not None:
			set_dpi_context.argtypes = [ctypes.c_void_p]
			set_dpi_context.restype = ctypes.c_bool
			set_dpi_context(ctypes.c_void_p(-4))  # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		# BOOL 是 4 字节 int，不能用 c_bool 当回调返回类型。
		self._enum_windows_proc = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
		self.user32.EnumWindows.argtypes = [self._enum_windows_proc, ctypes.c_void_p]
		self.user32.EnumWindows.restype = ctypes.c_bool
		self.user32.EnumChildWindows.argtypes = [ctypes.c_void_p, self._enum_windows_proc, ctypes.c_void_p]
		self.user32.EnumChildWindows.restype = ctypes.c_bool
		self.user32.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
		self.user32.GetClassNameW.restype = ctypes.c_int
		self.user32.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
		self.user32.GetWindowTextW.restype = ctypes.c_int
		self.user32.GetWindowTextLengthW.argtypes = [ctypes.c_void_p]
		self.user32.GetWindowTextLengthW.restype = ctypes.c_int
		self.user32.IsWindow.argtypes = [ctypes.c_void_p]
		self.user32.IsWindow.restype = ctypes.c_bool
		self.user32.SetForegroundWindow.argtypes = [ctypes.c_void_p]
		self.user32.SetForegroundWindow.restype = ctypes.c_bool
		self.user32.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
		self.user32.GetClientRect.restype = ctypes.c_bool
		self.user32.GetWindowDC.argtypes = [ctypes.c_void_p]
		self.user32.GetWindowDC.restype = ctypes.c_void_p
		self.user32.PrintWindow.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
		self.user32.PrintWindow.restype = ctypes.c_bool
		self.user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
		self.user32.ReleaseDC.restype = ctypes.c_int
		self.user32.PrintWindow.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
		self.user32.PrintWindow.restype = ctypes.c_bool
		self.user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
		self.user32.SendMessageW.restype = ctypes.c_ssize_t
		self.user32.GetWindowRect.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
		self.user32.GetWindowRect.restype = ctypes.c_bool
		self.user32.RedrawWindow.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
		self.user32.RedrawWindow.restype = ctypes.c_bool
		self.user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
		self.user32.PostMessageW.restype = ctypes.c_bool
		self.user32.GetDlgCtrlID.argtypes = [ctypes.c_void_p]
		self.user32.GetDlgCtrlID.restype = ctypes.c_int
		self.user32.GetParent.argtypes = [ctypes.c_void_p]
		self.user32.GetParent.restype = ctypes.c_void_p
		self.gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
		self.gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
		self.gdi32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
		self.gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
		self.gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
		self.gdi32.SelectObject.restype = ctypes.c_void_p
		self.gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
		self.gdi32.DeleteObject.restype = ctypes.c_bool
		self.gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
		self.gdi32.DeleteDC.restype = ctypes.c_bool
		self.gdi32.BitBlt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
		self.gdi32.BitBlt.restype = ctypes.c_bool
		self.gdi32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
		self.gdi32.GetDIBits.restype = ctypes.c_int

	def child_windows(self, window: ctypes.c_void_p) -> list[ctypes.c_void_p]:
		children: list[ctypes.c_void_p] = []

		def callback(handle, _lparam):
			children.append(ctypes.c_void_p(handle))
			return True

		self.user32.EnumChildWindows(window, self._enum_windows_proc(callback), None)
		return children

	def top_level_windows(self) -> list[ctypes.c_void_p]:
		windows: list[ctypes.c_void_p] = []

		def callback(handle, _lparam):
			windows.append(ctypes.c_void_p(handle))
			return True

		self.user32.EnumWindows(self._enum_windows_proc(callback), None)
		return windows

	def class_name(self, window: ctypes.c_void_p) -> str:
		buffer = ctypes.create_unicode_buffer(256)
		self.user32.GetClassNameW(window, buffer, len(buffer))
		return buffer.value

	def text(self, window: ctypes.c_void_p) -> str:
		length = self.user32.GetWindowTextLengthW(window)
		buffer = ctypes.create_unicode_buffer(max(1, length) + 1)
		self.user32.GetWindowTextW(window, buffer, len(buffer))
		if buffer.value:
			return buffer.value
		# EDIT 控件正文可能很长，GetWindowTextW 拿不到时退回 WM_GETTEXT。
		wide = ctypes.create_unicode_buffer(32768)
		copied = self.user32.SendMessageW(window, WM_GETTEXT, len(wide), ctypes.cast(wide, ctypes.c_void_p).value or 0)
		return wide.value[:copied] if copied > 0 else wide.value

	def capture(self, window: ctypes.c_void_p) -> WindowCapture:
		class RECT(ctypes.Structure):
			_fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

		class BITMAPINFOHEADER(ctypes.Structure):
			_fields_ = [
				("biSize", ctypes.c_uint32),
				("biWidth", ctypes.c_int32),
				("biHeight", ctypes.c_int32),
				("biPlanes", ctypes.c_uint16),
				("biBitCount", ctypes.c_uint16),
				("biCompression", ctypes.c_uint32),
				("biSizeImage", ctypes.c_uint32),
				("biXPelsPerMeter", ctypes.c_int32),
				("biYPelsPerMeter", ctypes.c_int32),
				("biClrUsed", ctypes.c_uint32),
				("biClrImportant", ctypes.c_uint32),
			]

		class BITMAPINFO(ctypes.Structure):
			_fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", ctypes.c_uint32 * 3)]

		rect = RECT()
		if not self.user32.GetWindowRect(window, ctypes.byref(rect)):
			raise RuntimeError("GetWindowRect failed")
		width = rect.right - rect.left
		height = rect.bottom - rect.top
		if width <= 0 or height <= 0:
			raise RuntimeError(f"window has empty screen rect: {width}x{height}")
		screen_dc = self.user32.GetDC(None)
		if not screen_dc:
			raise RuntimeError("GetDC(NULL) failed")
		try:
			memory_dc = self.gdi32.CreateCompatibleDC(screen_dc)
			bitmap = self.gdi32.CreateCompatibleBitmap(screen_dc, width, height)
			if not memory_dc or not bitmap:
				raise RuntimeError("failed to create capture surface")
			previous = self.gdi32.SelectObject(memory_dc, bitmap)
			# 只抓窗口自身，避免圆角外的桌面动画把“空闲帧稳定性”误判为闪烁。
			# 某些远程桌面环境 PrintWindow 可能失败，再退回屏幕 DC。
			if not self.user32.PrintWindow(window, memory_dc, PW_RENDERFULLCONTENT):
				self.gdi32.BitBlt(memory_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY)
			info = BITMAPINFO()
			info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
			info.bmiHeader.biWidth = width
			info.bmiHeader.biHeight = -height  # 负高度：自上而下。
			info.bmiHeader.biPlanes = 1
			info.bmiHeader.biBitCount = 32
			info.bmiHeader.biCompression = BI_RGB
			buffer = (ctypes.c_char * (width * height * 4))()
			lines = self.gdi32.GetDIBits(memory_dc, bitmap, 0, height, buffer, ctypes.byref(info), DIB_RGB_COLORS)
			self.gdi32.SelectObject(memory_dc, previous)
			self.gdi32.DeleteObject(bitmap)
			self.gdi32.DeleteDC(memory_dc)
			if lines != height:
				raise RuntimeError(f"GetDIBits returned {lines} of {height} rows")
			return WindowCapture(width, height, bytes(buffer))
		finally:
			self.user32.ReleaseDC(None, screen_dc)


_WIN32: _Win32 | None = None


def win32() -> _Win32:
	global _WIN32
	if _WIN32 is None:
		if os.name != "nt":
			raise RuntimeError("crash dialog smoke requires Windows")
		_WIN32 = _Win32()
	return _WIN32


def find_crash_dialog(timeout: float = 0.0) -> ctypes.c_void_p | None:
	api = win32()
	deadline = time.monotonic() + timeout
	while True:
		for window in api.top_level_windows():
			if api.class_name(window) == WINDOW_CLASS and api.text(window) == WINDOW_TITLE:
				return window
		if time.monotonic() >= deadline:
			return None
		time.sleep(0.1)


class CrashDialogSession:
	"""一个 `--qm-preview-crash-dialog` 预览进程 + 它的节日崩溃窗口。"""

	def __init__(self, binary: Path, preview_type: str, artifacts_dir: Path | None = None):
		self._preview_type = preview_type
		self._artifacts_dir = artifacts_dir
		self._api = win32()
		self._process = subprocess.Popen(
			[str(binary), "--qm-preview-crash-dialog", preview_type],
			cwd=binary.parent,
			stdin=subprocess.DEVNULL,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			encoding="utf-8",
			errors="replace",
		)
		self._lines: list[str] = []
		self._events: queue.Queue[str] = queue.Queue()
		threading.Thread(target=self._read_output, name="crash-dialog-output", daemon=True).start()
		self._window = find_crash_dialog(WINDOW_TIMEOUT)
		if self._window is None:
			raise RuntimeError(f"crash dialog window did not appear within {WINDOW_TIMEOUT}s\n{self.output}")
		# 预览进程可能不是当前前台进程；屏幕抓帧必须先把报告窗口置前，
		# 否则某些远程桌面/DWM 环境会抓到后面的桌面壁纸。
		self._api.user32.SetForegroundWindow(self._window)
		# 客户端必须先把 EDIT/按钮摆好、再绘背景；创建完成后立刻抓帧会拿到空白桌面背景。
		self._wait_until_painted(8.0)
		time.sleep(0.35)

	def _read_output(self) -> None:
		assert self._process.stdout is not None
		for line in self._process.stdout:
			line = line.rstrip("\r\n")
			self._lines.append(line)
			self._events.put(line)

	@property
	def window(self) -> ctypes.c_void_p:
		assert self._window is not None
		return self._window

	@property
	def output(self) -> str:
		code = format_exit_code(self._process.returncode) if self._process.poll() is not None else "<running>"
		return f"exit={code}\n" + "\n".join(self._lines)

	def buttons(self) -> dict[str, ctypes.c_void_p]:
		buttons = {}
		for child in self._api.child_windows(self.window):
			# CreateWindowExW(L"BUTTON") 建出来的真实类名是 "Button"，比较时忽略大小写。
			if self._api.class_name(child).lower() == "button":
				buttons[self._api.text(child)] = child
		return buttons

	def details_text(self) -> str:
		for child in self._api.child_windows(self.window):
			if self._api.class_name(child).lower() == "edit":
				return self._api.text(child)
		raise RuntimeError(f"report details control not found\n{self.output}")

	def click(self, label: str) -> None:
		# 跨进程对自绘按钮 SendMessageW(BM_CLICK) 不触发 BN_CLICKED；直接向父窗口
		# 投递 WM_COMMAND，与按钮自己产生点击通知等价，且不依赖按钮 WndProc。
		buttons = self.buttons()
		button = buttons.get(label)
		if button is None:
			raise RuntimeError(f"button {label!r} not present (have {sorted(buttons)})\n{self.output}")
		button_id = self._api.user32.GetDlgCtrlID(button)
		parent = self._api.user32.GetParent(button) or self.window
		word = ctypes.c_uint32((button_id & 0xFFFF) | (BN_CLICKED << 16)).value
		if not self._api.user32.PostMessageW(parent, WM_COMMAND, word, ctypes.c_ssize_t(button.value).value):
			raise RuntimeError(f"PostMessageW failed for {label!r} (last error {ctypes.get_last_error()})\n{self.output}")

	def capture(self) -> WindowCapture:
		return self._api.capture(self.window)

	def is_alive(self) -> bool:
		return self._process.poll() is None and bool(self._api.user32.IsWindow(self.window))

	def _wait_until_painted(self, deadline_seconds: float) -> None:
		"""轮询抓帧直到非空白，给窗口留出 DWM 合成与首次绘制的时间。"""
		end = time.monotonic() + deadline_seconds
		while time.monotonic() < end:
			if self.capture().is_uniform():
				time.sleep(0.1)
				continue
			return
		raise RuntimeError(f"crash dialog window stayed blank for {deadline_seconds}s\n{self.output}")

	def save_artifact(self, name: str, capture: WindowCapture) -> None:
		if self._artifacts_dir is None:
			return
		self._artifacts_dir.mkdir(parents=True, exist_ok=True)
		_write_bmp(self._artifacts_dir / f"crash-dialog-{self._preview_type}-{name}.bmp", capture)

	def sample_frames(self, count: int, interval: float = ANIMATION_SAMPLE_INTERVAL) -> list[WindowCapture]:
		frames = []
		for _ in range(count):
			frames.append(self.capture())
			time.sleep(interval)
		return frames

	def wait_until_stable(self, deadline: float = ANIMATION_STABLE_DEADLINE) -> WindowCapture:
		"""等到连续两帧完全一致，说明定时器已停、动画结束。"""
		end = time.monotonic() + deadline
		previous = self.capture()
		while time.monotonic() < end:
			time.sleep(ANIMATION_SAMPLE_INTERVAL)
			current = self.capture()
			if current.signature() == previous.signature():
				return current
			if self._process.poll() is not None:
				raise RuntimeError(f"preview process exited during fireworks animation\n{self.output}")
			previous = current
		raise TimeoutError(f"fireworks animation did not stop within {deadline}s\n{self.output}")

	def close_report(self) -> int:
		self.click(CLOSE_LABEL)
		try:
			return self._process.wait(timeout=EXIT_TIMEOUT)
		except subprocess.TimeoutExpired as exc:
			raise TimeoutError(f"preview process did not exit after clicking {CLOSE_LABEL}\n{self.output}") from exc

	def cleanup(self) -> None:
		if self._process.poll() is None:
			if self._window is not None and self._api.user32.IsWindow(self._window):
				self._api.user32.SendMessageW(self._window, WM_CLOSE, 0, 0)
			try:
				self._process.wait(timeout=5)
			except subprocess.TimeoutExpired:
				self._process.kill()
				self._process.wait(timeout=10)


class CrashDialogEnvironment:
	def __init__(self, build_dir: Path):
		self._binary = build_dir.resolve() / f"DDNet{EXE_SUFFIX}"
		if not self._binary.is_file():
			raise RuntimeError(f"missing binary: {self._binary}")
		configured = os.environ.get("QM_SMOKE_ARTIFACTS_DIR")
		self._artifacts_dir = Path(configured).resolve() if configured else None

	@property
	def binary(self) -> Path:
		return self._binary

	def open(self, preview_type: str) -> CrashDialogSession:
		return CrashDialogSession(self._binary, preview_type, self._artifacts_dir)


def smoke_crash_dialog(env: CrashDialogEnvironment, preview_type: str) -> None:
	session = env.open(preview_type)
	try:
		buttons = session.buttons()
		missing = [label for label in REQUIRED_BUTTONS if label not in buttons]
		if missing:
			raise RuntimeError(f"{preview_type}: missing buttons {missing}, have {sorted(buttons)}\n{session.output}")
		if "放烟花" in buttons:
			raise RuntimeError(f"{preview_type}: obsolete manual fireworks button is still present")

		details = session.details_text()
		absent_sections = [section for section in REQUIRED_REPORT_SECTIONS if section not in details]
		if absent_sections:
			raise RuntimeError(f"{preview_type}: localized report sections missing {absent_sections}\n{session.output}")

		# 忽略首次激活和 DWM 合成阶段，只把窗口出现后的首帧作为基线。
		idle_frames = session.sample_frames(4, interval=0.15)
		baseline = idle_frames[-1]
		if baseline.is_uniform():
			raise RuntimeError(f"{preview_type}: captured a blank window, screenshots are not usable here")
		session.save_artifact("baseline", baseline)

		# 烟花在窗口出现后自动开始，连续抓帧必须看到多个爆炸阶段。
		frames = session.sample_frames(8)
		signatures = {frame.signature() for frame in frames}
		if len(signatures) < MIN_ANIMATION_FRAMES:
			raise RuntimeError(f"{preview_type}: expected at least {MIN_ANIMATION_FRAMES} distinct automatic fireworks frames, got {len(signatures)}")
		if not session.is_alive():
			raise RuntimeError(f"{preview_type}: preview process exited during automatic fireworks\n{session.output}")
		if session.details_text() != details:
			raise RuntimeError(f"{preview_type}: automatic fireworks changed the report contents")
		animated = max(frames, key=lambda frame: baseline.changed_pixels(frame))
		changed_pixels = baseline.changed_pixels(animated)
		if changed_pixels < MIN_FIREWORKS_PIXELS:
			raise RuntimeError(f"{preview_type}: only {changed_pixels} pixels changed, automatic particle animation is not visible")
		session.save_artifact("fireworks", animated)

		stopped = session.wait_until_stable()
		if stopped.signature() == animated.signature():
			raise RuntimeError(f"{preview_type}: frames kept changing, animation never actually ran")
		if not session.is_alive():
			raise RuntimeError(f"{preview_type}: preview process died during fireworks\n{session.output}")
		session.save_artifact("after", stopped)

		exit_code = session.close_report()
		if exit_code != 0:
			raise RuntimeError(f"{preview_type}: preview exited with {format_exit_code(exit_code)}\n{session.output}")
	finally:
		session.cleanup()

	if find_crash_dialog(timeout=2.0) is not None:
		raise RuntimeError(f"{preview_type}: crash dialog window survived closing the report")


def _scenario(preview_type: str) -> Callable[[CrashDialogEnvironment], None]:
	def run(env: CrashDialogEnvironment) -> None:
		smoke_crash_dialog(env, preview_type)

	run.__name__ = f"smoke_crash_dialog_{preview_type}"
	return run


SMOKE_TESTS: dict[str, Callable[[CrashDialogEnvironment], None]] = {f"crash_dialog_{name}": _scenario(name) for name in PREVIEW_TYPES}


def main() -> int:
	parser = argparse.ArgumentParser(description="Run QmClient crash dialog process smoke tests")
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=sorted(SMOKE_TESTS), nargs="?")
	args = parser.parse_args()

	if os.name != "nt":
		print("crash dialog smoke skipped: Windows-only")
		return 0

	tests = {args.test: SMOKE_TESTS[args.test]} if args.test else SMOKE_TESTS
	failed = 0
	for name, test in tests.items():
		env = CrashDialogEnvironment(args.build_dir)
		try:
			test(env)
		except Exception as exc:  # pylint: disable=broad-exception-caught
			failed += 1
			print(f"{name}: FAILED\n{exc}", file=sys.stderr)
		else:
			print(f"{name}: passed")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
