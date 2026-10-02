from __future__ import annotations

from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]

WORKER = """
from pathlib import Path
import sys
from qmclient_scripts.cmake_windows import build_directory_lock
try:
    with build_directory_lock(Path(sys.argv[1]), timeout=float(sys.argv[2])):
        print('acquired', flush=True)
        sys.stdin.readline()
except TimeoutError as error:
    print(str(error), flush=True)
    sys.exit(3)
"""


class BuildDirectoryLockIntegrationTest(unittest.TestCase):
	def setUp(self):
		self.directory = tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp")
		self.addCleanup(self.directory.cleanup)
		self.build = Path(self.directory.name) / "build space"
		self.build.mkdir()

	def start_worker(self, directory: Path, timeout: float = 10):
		process = subprocess.Popen(
			[sys.executable, "-u", "-c", WORKER, str(directory), str(timeout)],
			cwd=REPO_ROOT,
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			encoding="utf-8",
			errors="replace",
		)
		self.addCleanup(self.stop_worker, process)
		events = queue.Queue()

		def read():
			for line in process.stdout:
				events.put(line.strip())

		threading.Thread(target=read, daemon=True).start()
		return process, events

	def stop_worker(self, process):
		if process.poll() is None:
			process.terminate()
		process.communicate(timeout=10)

	def release_worker(self, process):
		process.stdin.write("release\n")
		process.stdin.flush()
		self.assertEqual(process.wait(timeout=10), 0)

	def test_same_directory_waits_then_acquires_after_release(self):
		first, first_events = self.start_worker(self.build)
		self.assertEqual(first_events.get(timeout=10), "acquired")
		second, second_events = self.start_worker(self.build / ".." / "build space")
		self.assertIn("Waiting", second_events.get(timeout=10))
		self.assertIsNone(second.poll())
		self.release_worker(first)
		self.assertEqual(second_events.get(timeout=10), "acquired")
		self.release_worker(second)

	def test_different_build_directories_can_acquire_at_once(self):
		first, first_events = self.start_worker(self.build)
		self.assertEqual(first_events.get(timeout=10), "acquired")
		second, second_events = self.start_worker(self.build.parent / "another build")
		self.assertEqual(second_events.get(timeout=10), "acquired")
		self.release_worker(second)
		self.release_worker(first)

	def test_expired_wait_reports_failure_without_entering_transaction(self):
		first, first_events = self.start_worker(self.build)
		self.assertEqual(first_events.get(timeout=10), "acquired")
		second, second_events = self.start_worker(self.build, timeout=0)
		self.assertIn("Waiting", second_events.get(timeout=10))
		self.assertIn("Timed out", second_events.get(timeout=10))
		self.assertEqual(second.wait(timeout=10), 3)
		self.release_worker(first)

	def test_lock_handle_releases_after_holder_process_exits(self):
		first, first_events = self.start_worker(self.build)
		self.assertEqual(first_events.get(timeout=10), "acquired")
		first.terminate()
		first.wait(timeout=10)
		second, second_events = self.start_worker(self.build, timeout=0)
		self.assertEqual(second_events.get(timeout=10), "acquired")
		self.release_worker(second)


if __name__ == "__main__":
	unittest.main()
