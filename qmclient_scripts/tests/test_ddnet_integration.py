from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "integration/ddnet_integration.py"
SPEC = importlib.util.spec_from_file_location("qm_ddnet_integration", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class FinishMessageTest(unittest.TestCase):
    def test_server_and_demo_logs_accept_localized_finishes(self) -> None:
        for source in ("chat", "chat/server"):
            for suffix in (" finished in: 0 minute(s) 1.00 second(s)", " 完成了地图，用时：0 分 1.00 秒"):
                with self.subTest(source=source, suffix=suffix):
                    self.assertTrue(
                        RUNNER.is_finish_message(
                            f"{source}: *** client1{suffix}", source, ("client1",)
                        )
                    )

    def test_other_players_and_other_log_sources_do_not_satisfy_demo_wait(self) -> None:
        for line in (
            "chat/server: *** client2 finished in: 1 second",
            "chat: *** client1 完成了地图，用时：1 秒",
            "chat/server: *** client10 finished in: 1 second",
            "chat/server: client1: I finished in: 1 second",
        ):
            with self.subTest(line=line):
                self.assertFalse(RUNNER.is_finish_message(line, "chat/server", ("client1",)))


if __name__ == "__main__":
    unittest.main()
