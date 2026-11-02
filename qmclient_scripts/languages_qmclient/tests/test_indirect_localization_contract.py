import unittest
from pathlib import Path

from qmclient_scripts.languages_qmclient import source_keys


PROJECT_ROOT = Path(__file__).resolve().parents[3]


class IndirectLocalizationContractTest(unittest.TestCase):
    # 缓存和动态状态通过变量调用 Localize；运行时缓存测试无法约束源码提取登记。
    # 本合同调用正式提取器检查登记结果，不验证页签渲染或语言切换行为。
    def test_cached_tab_labels_remain_registered_for_generation(self):
        groups = {
            "src/game/client/components/menus.cpp": {
                "Language", "General", "Player", "Tee", "Tee 0.7",
                "Appearance", "Controls", "Graphics", "Sound", "DDNet",
                "Assets", "TClient", "QmClient", "Search", "Profiles",
                "Configs", "Contributors",
            },
            "src/game/client/components/menus_settings.cpp": {
                "HUD", "Chat", "Name Plate", "Hook Collisions",
                "Info Messages", "Laser",
            },
            "src/game/client/components/menus_settings_assets.cpp": {
                "Entities", "Game", "Emoticons", "Particles", "HUD",
                "Mouse", "Direction Keys", "Strong Weak Hook",
                "Entity Background Image", "Other",
            },
            "src/game/client/components/tclient/menus_tclient.cpp": {
                "Settings", "Bind Wheel", "War List", "Chat Binds", "Status Bar",
            },
        }
        for relative_path, expected in groups.items():
            with self.subTest(source=relative_path):
                records = source_keys.collect_file_source_key_records(
                    PROJECT_ROOT / relative_path
                )
                actual = {record.identity() for record in records}
                self.assertFalse({(key, "") for key in expected} - actual)

    def test_static_failure_keys_remain_registered_for_generation(self):
        groups = {
            "src/game/client/components/qmclient/axiom_auto_login.h": {
                "Axiom auto login failed",
                "Axiom auto login failed: wrong password",
                "Axiom auto login failed: already in game",
                "Axiom auto login failed: invalid credentials",
            },
            "src/game/client/components/qmclient/update_client.cpp": {
                "Failed to check for updates",
                "Update sources are temporarily unavailable",
                "Failed to create the update working directory",
                "Automatic updates require Windows x64",
                "Update failed",
            },
        }
        for relative_path, expected in groups.items():
            with self.subTest(source=relative_path):
                records = source_keys.collect_file_source_key_records(
                    PROJECT_ROOT / relative_path
                )
                actual = {record.identity() for record in records}
                self.assertFalse({(key, "") for key in expected} - actual)


if __name__ == "__main__":
    unittest.main()
