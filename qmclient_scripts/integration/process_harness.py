#!/usr/bin/env python3
"""QmClient 真实进程编排 harness。

这里只提供“启动进程、读日志、发控制台命令、管理临时工作目录”的通用能力，
不包含任何具体断言。冒烟（`qmclient_smoke.py`）与端到端（`e2e_*.py`）场景
共用本模块，避免各自复制一份进程编排逻辑。

断言纪律（见 `README.md`）：场景必须从外部可观察结果断言启动、连接、日志、
退出和失败回退；客户端或服务端崩溃必须失败，不得用放宽超时或忽略退出码掩盖。
"""

from __future__ import annotations

import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time
from collections.abc import Callable


EXE_SUFFIX = ".exe" if os.name == "nt" else ""

# 命名管道名/临时目录前缀。两个用途共用一个前缀，便于失败时辨认残留物归属。
DEFAULT_TEMP_PREFIX = "qmclient_smoke_"


def log_message(line: str) -> str:
	parts = line.split(" ", 3)
	if len(parts) == 4 and parts[2] in {"D", "I", "W", "E"}:
		return parts[3]
	return line


def format_exit_code(code: int | None) -> str:
	if code is None:
		return "<running>"
	if os.name == "nt" and code >= 0:
		return f"{code} (0x{code:08X})"
	return str(code)


# 兼容旧调用点（qmclient_smoke 曾以 _format_exit_code 命名导出）。
_format_exit_code = format_exit_code


class Process:
	def __init__(self, name: str, args: list[str], cwd: Path, fifo_command: str | None = None, pipe_prefix: str = DEFAULT_TEMP_PREFIX, env: dict[str, str] | None = None):
		self.name = name
		self._fifo_path: str | None = None
		self._fifo = None
		if fifo_command is not None:
			self._fifo_path = self._create_fifo(name, cwd, pipe_prefix)
			args = [*args, f"{fifo_command} {self._fifo_path}"]
		self._process = subprocess.Popen(
			args,
			cwd=cwd,
			env={**os.environ, **env} if env else None,
			stdin=subprocess.DEVNULL,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			encoding="utf-8",
			errors="replace",
		)
		self._lines: list[str] = []
		self._events: queue.Queue[str] = queue.Queue()
		threading.Thread(target=self._read_output, name=f"{name}-output", daemon=True).start()

	@staticmethod
	def _create_fifo(name: str, cwd: Path, pipe_prefix: str = DEFAULT_TEMP_PREFIX) -> str:
		# 返回引擎配置值：Windows 下 CFifo::Init 会自行补上 "\\\\.\\pipe\\" 前缀，
		# 因此这里只能给纯管道名；给出完整路径会导致前缀翻倍、管道永远建不出来。
		if os.name == "nt":
			return f"{pipe_prefix}{os.getpid()}_{name}_{time.monotonic_ns()}"
		path = cwd / f"{name}.fifo"
		os.mkfifo(path)
		return str(path)

	def _open_fifo(self, timeout: float = 10.0) -> None:
		if self._fifo_path is None:
			raise RuntimeError(f"{self.name}: command FIFO was not enabled")
		if os.name != "nt":
			self._fifo = open(self._fifo_path, "w", buffering=1, encoding="utf-8")
			return
		# Windows 命名管道在目标进程创建之前打开会得到 ERROR_FILE_NOT_FOUND。
		# 进程启动与管道创建之间存在竞态，因此这里做有界重试，而不是假设管道已就绪。
		pipe_path = rf"\\.\pipe\{self._fifo_path}"
		deadline = time.monotonic() + timeout
		while True:
			try:
				self._fifo = open(pipe_path, "w", buffering=1, encoding="utf-8")
				return
			except FileNotFoundError:
				if self._process.poll() is not None:
					raise RuntimeError(f"{self.name}: exited before the command pipe was created")
				if time.monotonic() >= deadline:
					raise TimeoutError(f"{self.name}: command pipe {pipe_path} was not created within {timeout}s")
				time.sleep(0.05)

	def command(self, command: str) -> None:
		if self._fifo is None:
			self._open_fifo()
		assert self._fifo is not None
		self._fifo.write(f"{command}\n")

	def _read_output(self) -> None:
		assert self._process.stdout is not None
		for line in self._process.stdout:
			line = line.rstrip("\r\n")
			self._lines.append(line)
			self._events.put(line)

	def wait_for(self, predicate: Callable[[str], bool], description: str, timeout: float) -> str:
		for raw_line in self._lines:
			line = log_message(raw_line)
			if predicate(line):
				return line

		deadline = time.monotonic() + timeout
		while True:
			remaining = deadline - time.monotonic()
			if remaining <= 0:
				output = "\n".join(self._lines)
				raise TimeoutError(f"{self.name}: timed out waiting for {description}\n{output}")
			try:
				raw_line = self._events.get(timeout=min(remaining, 0.1))
			except queue.Empty as exc:
				if self._process.poll() is not None:
					output = "\n".join(self._lines)
					raise RuntimeError(f"{self.name}: exited with {format_exit_code(self._process.returncode)} while waiting for {description}\n{output}") from exc
				continue
			line = log_message(raw_line)
			if predicate(line):
				return line
			if self._process.poll() is not None:
				raise RuntimeError(f"{self.name}: exited with {format_exit_code(self._process.returncode)} while waiting for {description}")

	def is_alive(self) -> bool:
		"""进程是否仍在运行（用于断言弹窗阻塞期间进程没有被看门狗结束）。"""
		return self._process.poll() is None

	def stop(self) -> None:
		if self._fifo is not None:
			self._fifo.close()
			self._fifo = None
		if self._process.poll() is None:
			self._process.terminate()
		try:
			self._process.wait(timeout=10)
		except subprocess.TimeoutExpired:
			self._process.kill()
			self._process.wait(timeout=10)
		if os.name != "nt" and self._fifo_path is not None:
			Path(self._fifo_path).unlink(missing_ok=True)

	def wait_for_exit(self, timeout: float = 10) -> int:
		try:
			return self._process.wait(timeout=timeout)
		except subprocess.TimeoutExpired as exc:
			raise TimeoutError(f"{self.name}: timed out waiting for exit") from exc

	def kill(self) -> None:
		"""强制结束进程但不清理 FIFO（用于模拟崩溃）。"""
		if self._process.poll() is None:
			self._process.kill()
			self._process.wait(timeout=10)


class ProcessEnvironment:
	"""一对 DDNet 客户端/服务端进程 + 独立临时工作目录。"""

	def __init__(self, build_dir: Path, temp_prefix: str = DEFAULT_TEMP_PREFIX):
		self._build_dir = build_dir.resolve()
		self._temp_prefix = temp_prefix
		self._client_binary = self._build_dir / f"DDNet{EXE_SUFFIX}"
		self._server_binary = self._build_dir / f"DDNet-Server{EXE_SUFFIX}"
		for binary in (self._client_binary, self._server_binary):
			if not binary.is_file():
				raise RuntimeError(f"missing binary: {binary}")
		self._temp_dir = Path(tempfile.mkdtemp(prefix=temp_prefix, dir=self._build_dir))
		# 客户端启动阶段会把 bundled icon fonts 安装到这个存储子目录。
		# 预先创建父目录，避免最小冒烟被环境准备错误掩盖。
		(self._temp_dir / "qmclient" / "fonts").mkdir(parents=True, exist_ok=True)
		self._server: Process | None = None
		self._client: Process | None = None
		self._server_port: int | None = None
		data_dir = self._build_dir / "data"
		(self._temp_dir / "storage.cfg").write_text(f"add_path .\nadd_path {data_dir}\n", encoding="utf-8")

	def start_server(self) -> int:
		self._server = Process(
			"server",
			[str(self._server_binary), "sv_register 0"],
			self._temp_dir,
			fifo_command="sv_input_fifo",
			pipe_prefix=self._temp_prefix,
		)
		self._server.wait_for(lambda line: line.startswith("server: version"), "server startup", 10)
		line = self._server.wait_for(lambda value: value.startswith("server: using port "), "server port", 10)
		self._server_port = int(line.removeprefix("server: using port "))
		return self._server_port

	def start_client(self, config: list[str], connect: bool = True, connect_address: str | None = None, env: dict[str, str] | None = None) -> Process:
		"""启动客户端。

		`connect=True` 时追加 `connect localhost:<port>`（需要已启动服务端）；
		也可以通过 `connect_address` 指定连接目标；`connect=False` 时只启动进程，
		用于连接失败回退等场景。`env` 传入额外的进程环境变量（如测试专用开关）。
		"""
		arguments = [
			str(self._client_binary),
			"gfx_fullscreen 0",
			"cl_save_settings 0",
			*config,
		]
		if connect:
			if connect_address is None:
				assert self._server is not None
				assert self._server_port is not None
				connect_address = f"localhost:{self._server_port}"
			arguments.append(f"connect {connect_address}")
		self._client = Process("client", arguments, self._temp_dir, fifo_command="cl_input_fifo", pipe_prefix=self._temp_prefix, env=env)
		self._client.wait_for(lambda line: line.startswith("client: version"), "client startup", 15)
		return self._client

	def connect_client(self, config: list[str]) -> None:
		self.start_client(config, connect=True)
		assert self._server is not None
		self._server.wait_for(lambda line: line.startswith("server: player has entered the game"), "client connection", 15)

	@property
	def temp_dir(self) -> Path:
		return self._temp_dir

	@property
	def build_dir(self) -> Path:
		return self._build_dir

	@property
	def server(self) -> Process:
		assert self._server is not None
		return self._server

	@property
	def client(self) -> Process:
		assert self._client is not None
		return self._client

	@property
	def server_port(self) -> int | None:
		return self._server_port

	def path(self, *parts: str) -> Path:
		"""临时工作目录下的路径（E2E 用它校验落盘产物）。"""
		return self._temp_dir.joinpath(*parts)

	def close(self, keep_temp: bool = False) -> None:
		if self._client is not None:
			self._client.stop()
		if self._server is not None:
			self._server.stop()
		if not keep_temp:
			shutil.rmtree(self._temp_dir, ignore_errors=True)

	def process_tails(self, lines: int = 25) -> list[tuple[str, list[str]]]:
		"""已启动进程的输出尾部。

		场景失败时打印它，避免为了看一行日志（例如 "Nameplate MSDF ready: N page(s)"）
		反复重跑整个真实进程场景。未启动的进程直接跳过。
		"""
		tails: list[tuple[str, list[str]]] = []
		for label, process in (("server", self._server), ("client", self._client)):
			if process is not None and process._lines:
				tails.append((label, process._lines[-lines:]))
		return tails


# 兼容旧命名：既有冒烟场景与 runner 单测都以 SmokeEnvironment 引用该类。
SmokeEnvironment = ProcessEnvironment
