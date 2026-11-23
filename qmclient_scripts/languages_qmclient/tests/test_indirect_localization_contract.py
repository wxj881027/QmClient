import unittest
from pathlib import Path

from qmclient_scripts.languages_qmclient import source_keys


PROJECT_ROOT = Path(__file__).resolve().parents[3]
CARD_REGISTRY_SOURCE = "src/game/client/QmUi/QmCardRegistry.cpp"


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

    # 卡片表把标题/描述写成裸字面量，运行时由 Localize(Default.m_pTitle/m_pDescription) 取值。
    # 这些用例调用正式提取器检查 active key 登记，不验证卡片渲染或语言切换行为。
    def test_card_registry_titles_and_descriptions_remain_registered_for_generation(self):
        titles = {
            "Chat completion appearance",
            "?!Stuck keys!?",
            "Collision hitbox",
            "QmClient",
        }
        descriptions = {
            "Colors for chat command and emoji completion",
            "QmClient information and project links",
            "Highlight held hammer and hook inputs while frozen",
        }
        records = source_keys.collect_file_source_key_records(
            PROJECT_ROOT / CARD_REGISTRY_SOURCE
        )
        actual = {record.identity() for record in records}
        self.assertFalse({(key, "") for key in titles | descriptions} - actual)

    def test_card_registry_search_metadata_is_not_registered_as_reference_text(self):
        metadata = {
            # 卡 id、搜索关键词、导航 tab 和 .inc 文件名都不是翻译源文案。
            "qm:info",
            "deck:graphics-display",
            "qmclient info",
            "credits-qmclient",
            "appearance-name-plate",
            "spectator freeview",
        }
        records = source_keys.collect_file_source_key_records(
            PROJECT_ROOT / CARD_REGISTRY_SOURCE
        )
        keys = {record.key for record in records}
        self.assertFalse(metadata & keys)

    def test_card_registry_audit_keeps_titles_translatable_and_metadata_classified(self):
        report = source_keys.build_string_audit_report(
            paths=(PROJECT_ROOT / CARD_REGISTRY_SOURCE,)
        )
        must_i18n = {(record.line, record.text) for record in report.must_i18n}
        business_data = {(record.line, record.text) for record in report.business_data}

        self.assertIn("Chat completion appearance", {text for _, text in must_i18n})
        # 同一条字面量不能同时被登记成可翻译文案和元数据。
        self.assertFalse(must_i18n & business_data)
        self.assertIn("qm:info", {text for _, text in business_data})
        self.assertIn("qmclient info", {text for _, text in business_data})


if __name__ == "__main__":
    unittest.main()
