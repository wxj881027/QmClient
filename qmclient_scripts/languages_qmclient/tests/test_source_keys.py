# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from qmclient_scripts.languages_qmclient import source_keys


class SourceKeysTest(unittest.TestCase):
    def test_extracts_localize_first_argument_literals(self):
        content = 'Localize("Open"); Localize("Close", "Menu");'
        self.assertEqual(
            source_keys.extract_localize_keys(source_keys.strip_cpp_comments(content)),
            {("Open", ""), ("Close", "Menu")},
        )

    def test_extracts_qm_card_registry_titles_and_descriptions(self):
        content = "\n".join(
            (
                'static const std::vector<SCardDefault> s_aDefaults = {',
                '{"qm:camera_view", "visual", ECardColumn::Right, 0, "Camera view", "camera keywords"},',
                '{"deck:graphics-display", "graphics", ECardColumn::Left, 0,',
                ' Localizable("Graphics display"), "monitor keywords",',
                ' Localizable("Window and monitor")},',
                '};',
            )
        )

        records = source_keys.extract_qm_card_registry_records(content)

        self.assertEqual(
            {(record.key, record.category) for record in records},
            {
                ("Camera view", "card_registry"),
                ("Graphics display", "card_registry"),
                ("Window and monitor", "card_registry"),
            },
        )

    def test_extracts_qm_runtime_card_titles_and_subtitles(self):
        content = "\n".join(
            (
                "AddCard(",
                '    EQmModuleId::CameraView, "qm:camera_view",',
                '    "Camera & FOV", "Adjust game camera and FOV settings",',
                "    Render);",
            )
        )

        records = source_keys.extract_qm_runtime_card_records(content)

        self.assertEqual(
            {record.key for record in records},
            {"Camera & FOV", "Adjust game camera and FOV settings"},
        )

    def test_extracts_qmclient_localized_wrapper_labels(self):
        path = Path("src/game/client/components/qmclient/menus_qmclient.cpp")
        content = "\n".join(
            (
                'RenderCheckbox(&Config, "Show blocked words in console", &Config);',
                'RenderValue("qmclient-duration", "Weapon switch duration", &Id, &Value, 0, 100);',
                'RenderCheckbox(Column, &Config, "qmclient-hide-hud", "Hide HUD");',
                'RenderValue("qmclient-short-duration", "Duration", &Id, &Value, 0, 100);',
                'RenderValue("qmclient-days", "Days", &Id, &Value, 0, 100, "d");',
            )
        )

        records = source_keys.extract_known_indirect_records(path, content)

        self.assertEqual(
            {record.key for record in records},
            {
                "Show blocked words in console",
                "Weapon switch duration",
                "Hide HUD",
                "Duration",
                "Days",
            },
        )

    def test_extracts_tclient_dynamic_card_titles(self):
        path = Path("src/game/client/components/tclient/menus_tclient.cpp")
        content = "\n".join(
            (
                'AddCard("deck:tclient-warlist-entries", "War Entries", 360.0f, Render);',
                'AddCard("deck:tclient-info-links", "TClient Links", 100.0f, Render);',
                'AddCard("deck:tclient-settings", "Settings", 100.0f, Render);',
            )
        )

        records = source_keys.extract_known_indirect_records(path, content)

        self.assertEqual(
            {record.key for record in records},
            {"War Entries", "TClient Links", "Settings"},
        )

    def test_extracts_qm_music_hook_registry_labels(self):
        path = Path("src/game/client/components/qmclient/qm_music_hook_registry.h")
        content = "\n".join(
            (
                "static const SQmMusicHookEntry aEntries[] = {",
                '{&g_Config.m_QmNeteaseHookEnable, "Enable Netease music Hook", "Enable Netease music Hook", L"cloudmusic.exe"},',
                '{&g_Config.m_QmSodaHookEnable, "Enable SodaMusic Hook", "Enable SodaMusic Hook", L"SodaMusic.exe"},',
                '{&g_Config.m_QmSpotifyEnable, "Enable Spotify lyrics", "Enable Spotify lyrics", L"Spotify.exe"},',
                "};",
            )
        )

        records = source_keys.extract_known_indirect_records(path, content)

        self.assertEqual(
            {record.key for record in records},
            {"Enable Netease music Hook", "Enable SodaMusic Hook", "Enable Spotify lyrics"},
        )

    def test_extracts_concatenated_literals_in_first_argument(self):
        content = 'Localize("Demo " "Player");'
        self.assertEqual(
            source_keys.extract_localize_keys(source_keys.strip_cpp_comments(content)),
            {("Demo ", ""), ("Player", "")},
        )

    def test_preserves_newline_escape_as_language_file_key(self):
        content = r'Localize("%d\n(%d/%d)");'
        self.assertEqual(
            source_keys.extract_localize_keys(source_keys.strip_cpp_comments(content)),
            {(r"%d\n(%d/%d)", "")},
        )

    def test_ignores_commented_localize_calls(self):
        content = '// Localize("Nope")\nLocalize("Yes")'
        self.assertEqual(
            source_keys.extract_localize_keys(source_keys.strip_cpp_comments(content)),
            {("Yes", "")},
        )

    def test_ignores_localize_text_inside_string_literal(self):
        content = 'EXPECT_NE(Source.find("Localize(\\"Nope\\")"), std::string::npos);'
        self.assertEqual(
            source_keys.extract_localize_keys(source_keys.strip_cpp_comments(content)),
            set(),
        )

    def test_extracts_register_help_text(self):
        content = (
            'Console()->Register("cmd", "", CFGFLAG_CLIENT, Fn, this, "Help text");'
        )
        self.assertEqual(
            source_keys.extract_register_help_strings(
                source_keys.strip_cpp_comments(content)
            ),
            {"Help text"},
        )

    def test_register_help_strings_should_use_english_source_keys(self):
        content = (
            'Console()->Register("rules", "", CFGFLAG_CHAT | CFGFLAG_SERVER, Fn, this, '
            '"Show the server rules");'
        )
        self.assertEqual(
            source_keys.extract_register_help_strings(
                source_keys.strip_cpp_comments(content)
            ),
            {"Show the server rules"},
        )

    def test_console_cmdlist_help_uses_english_source_key(self):
        path = Path("src/engine/shared/console.cpp")
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        help_strings = source_keys.extract_register_help_strings(content)

        self.assertIn("List all commands which are accessible for users", help_strings)
        self.assertNotIn("列出普通玩家可用的所有命令", help_strings)

    def test_collects_category_summary_from_source_file(self):
        records = source_keys.collect_source_key_records(
            paths=(Path(__file__),),
            extra_strings={"Extra test key"},
        )

        summary = source_keys.summarize_source_key_records(records)

        self.assertGreaterEqual(summary.localize_or_localizable, 1)
        self.assertEqual(summary.extra, 1)
        self.assertEqual(summary.total_unique, len({record.key for record in records}))

    def test_audit_marks_notification_aliases_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root
                / "src"
                / "game"
                / "client"
                / "components"
                / "qmclient"
                / "hud_notifications"
                / "hud_notification_static_rules.h"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'X("你正在发起投票，请等当前投票结束后再试", '
                '"You are running a vote, please try again after the vote is done!")\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        must_i18n_texts = {record.text for record in report.must_i18n}
        self.assertIn("你正在发起投票，请等当前投票结束后再试", business_texts)
        self.assertIn(
            "You are running a vote, please try again after the vote is done!",
            must_i18n_texts,
        )

    def test_audit_marks_semantic_notification_matchers_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root
                / "src"
                / "game"
                / "client"
                / "components"
                / "qmclient"
                / "hud_notifications"
                / "hud_notification_static_alias_rules.h"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                "#define QM_HUD_NOTIFICATION_STATIC_ALIAS_RULES(X) \\\n"
                '\tX("你现在会收到私聊消息", WhispersOn)\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        self.assertIn("你现在会收到私聊消息", business_texts)

    def test_audit_marks_assertions_and_command_templates_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "foo.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                "\n".join(
                    [
                        'dbg_assert_failed("Client state %d is invalid for RenderMenubar");',
                        'static_assert(true, "Metadata table out of sync");',
                        'str_format(aCmd, sizeof(aCmd), "auth_add %s admin %s", pUser, pPassword);',
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        self.assertIn("Client state %d is invalid for RenderMenubar", business_texts)
        self.assertIn("Metadata table out of sync", business_texts)
        self.assertIn("auth_add %s admin %s", business_texts)

    def test_audit_marks_external_parser_diagnostics_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root
                / "src"
                / "game"
                / "client"
                / "components"
                / "qmclient"
                / "translate"
                / "translate_parse.cpp"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'str_copy(Out.m_aError, "No choices in response", sizeof(Out.m_aError));\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        self.assertIn("No choices in response", business_texts)

    def test_audit_marks_bind_commands_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root
                / "src"
                / "game"
                / "client"
                / "components"
                / "menus_settings_controls.cpp"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                '{EBindOptionGroup::MOVEMENT, Localizable("Pause"), "say /pause"},\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        must_i18n_texts = {record.text for record in report.must_i18n}
        self.assertIn("say /pause", business_texts)
        self.assertIn("Pause", must_i18n_texts)

    def test_audit_marks_localized_constant_aliases_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "hud.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'constexpr const char *pLine1 = "practice mode";\n'
                "TextRender()->Text(0.0f, 0.0f, 10.0f, Localize(pLine1), -1.0f);\n",
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        self.assertIn("practice mode", business_texts)

    def test_audit_marks_preview_and_format_templates_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "chat.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'static const SPreviewLine s_aPreviewLines[] = {{"Server", "Welcome to QmClient"}};\n'
                'str_format(aCount, sizeof(aCount), "%s（/%s %s）", pHelp, pName, pParams);\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        self.assertIn("Welcome to QmClient", business_texts)
        self.assertIn("%s（/%s %s）", business_texts)

    def test_extracts_statusbar_item_labels_and_descriptions(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "game"
            / "client"
            / "components"
            / "tclient"
            / "statusbar.h"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        keys = {record.key for record in records}

        self.assertIn("Snapshot Latency", keys)
        self.assertIn("Displays server snapshot latency", keys)
        self.assertNotIn("u", keys)

    def test_extracts_tclient_cached_section_titles(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "game"
            / "client"
            / "components"
            / "tclient"
            / "menus_tclient.cpp"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        keys = {record.key for record in records}

        self.assertIn("Visual: Nameplates", keys)
        self.assertIn("Tee status bar", keys)

    def test_extracts_qmclient_config_descriptions(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables_qmclient.h"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        keys = {record.key for record in records}

        self.assertGreaterEqual(len(keys), 400)
        self.assertTrue(all(record.source == path for record in records))
        self.assertNotIn("qm_scoreboard_points", keys)
        self.assertFalse(any(source_keys.has_cjk(key) for key in keys))

    def test_extracts_tclient_config_descriptions(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables_tclient.h"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        keys = {record.key for record in records}

        self.assertGreaterEqual(len(keys), 150)
        self.assertTrue(all(record.source == path for record in records if record.source))
        self.assertFalse(any(source_keys.has_cjk(key) for key in keys))

    def test_module_name_for_tclient_config_header(self):
        from qmclient_scripts.languages_qmclient import i18n_store

        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables_tclient.h"
        )
        self.assertEqual(i18n_store.module_name_for_source(path), "tclient")
        qm_path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables_qmclient.h"
        )
        self.assertEqual(i18n_store.module_name_for_source(qm_path), "qmclient")

    def test_extracts_ddnet_config_descriptions(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables.h"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        keys = {record.key for record in records}

        self.assertGreaterEqual(len(keys), 500)
        self.assertTrue(all(record.source == path for record in records if record.source))
        self.assertFalse(any(source_keys.has_cjk(key) for key in keys))

    def test_module_name_for_ddnet_config_header(self):
        from qmclient_scripts.languages_qmclient import i18n_store

        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables.h"
        )
        self.assertEqual(i18n_store.module_name_for_source(path), "menus")

    def test_extracts_asset_editor_blend_modes_with_context(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "game"
            / "client"
            / "components"
            / "menus.h"
        )
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        records = source_keys.extract_known_indirect_records(path, content)
        identities = {record.identity() for record in records}

        self.assertIn(("Screen", "Assets editor blend mode"), identities)
        self.assertIn(("Overlay", "Assets editor blend mode"), identities)

    def test_audit_marks_dynamic_localize_context_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "menus.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'const char *pName = Localize(GetName(), "Dynamic context");\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("Dynamic context", business_texts)
        self.assertNotIn("Dynamic context", review_texts)

    def test_audit_marks_display_format_shells_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "menus.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'str_format(aBuf, sizeof(aBuf), "[%d]  ", Index);\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("[%d]  ", business_texts)
        self.assertNotIn("[%d]  ", review_texts)

    def test_audit_marks_chat_preview_samples_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root / "src" / "game" / "client" / "components" / "menus_settings.cpp"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'SetPreviewLine(PREVIEW_TEAM, 11, "Your Teammate", "Let\\\'s speedrun this!", FLAG_TEAM, 0);\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("Your Teammate", business_texts)
        self.assertIn("Let's speedrun this!", business_texts)
        self.assertNotIn("Let's speedrun this!", review_texts)

    def test_audit_marks_localized_layout_measurement_keys_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = (
                root
                / "src"
                / "game"
                / "client"
                / "components"
                / "menus_settings_assets.cpp"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'const float Width = ComputeToolbarButtonWidth("Assets directory");\n'
                'DoButton_Menu(&s_Id, Localize("Assets directory"), 0, &Button);\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        must_i18n_texts = {record.text for record in report.must_i18n}
        self.assertIn("Assets directory", business_texts)
        self.assertIn("Assets directory", must_i18n_texts)

    def test_audit_marks_test_strings_as_test_only(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "test" / "sample_test.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('EXPECT_STREQ("test sample text", "test sample text");\n')

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        test_texts = {record.text for record in report.test_only}
        self.assertIn("test sample text", test_texts)

    def test_audit_marks_unknown_client_strings_for_review(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "foo.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('const char *pLabel = "Review this user-facing string";\n')

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        review_texts = {record.text for record in report.needs_review}
        self.assertIn("Review this user-facing string", review_texts)

    def test_audit_marks_settings_registry_metadata_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "QmUi" / "QmCardRegistry.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                '{"deck:sample", "general", ECardColumn::Left, 0, "Sample", "sample search keyword"};\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("sample search keyword", business_texts)
        self.assertNotIn("sample search keyword", review_texts)

    def test_audit_marks_tutorial_command_and_perf_fragments_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            menus = root / "src" / "game" / "client" / "components" / "menus.cpp"
            browser = root / "src" / "game" / "client" / "components" / "menus_browser.cpp"
            menus.parent.mkdir(parents=True, exist_ok=True)
            menus.write_text('str_copy(aMotd, "sv_motd ");\n', encoding="utf-8")
            menus.write_text(
                'str_copy(aMotd, "sv_motd ");\nconst char *pFragment = ", SettingsPerfContextName(), ";\n',
                encoding="utf-8",
            )
            browser.write_text(
                'StartSettingsPerfScrollWindow("server", SettingsPerfContextName(), "page", "none");\n',
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("sv_motd ", business_texts)
        self.assertIn("server", business_texts)
        self.assertNotIn("sv_motd ", review_texts)
        self.assertNotIn("server", review_texts)

    def test_audit_marks_obvious_machine_literals_as_business_data(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "foo.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                "\n".join(
                    [
                        'log_info("qmclient", "event=sample duration_ms=%.3f");',
                        'Writer.WriteAttribute("version");',
                        'static constexpr const char *PATH = "qmclient/map_notes.json";',
                        'const char *pLabel = "Review this user-facing string";',
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        business_texts = {record.text for record in report.business_data}
        review_texts = {record.text for record in report.needs_review}
        self.assertIn("event=sample duration_ms=%.3f", business_texts)
        self.assertIn("qmclient/map_notes.json", business_texts)
        self.assertNotIn("version", review_texts)
        self.assertIn("Review this user-facing string", review_texts)

    def test_audit_marks_cjk_source_keys_as_violation(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            path = root / "src" / "game" / "client" / "components" / "foo.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('Localize("中文提示");\n', encoding="utf-8")

            report = source_keys.build_string_audit_report(paths=(root / "src",))

        violation_texts = {record.text for record in report.violation}
        self.assertIn("中文提示", violation_texts)

    def test_audit_accepts_qmclient_config_descriptions_as_indirect_i18n(self):
        path = (
            source_keys.PROJECT_ROOT
            / "src"
            / "engine"
            / "shared"
            / "config_variables_qmclient.h"
        )
        report = source_keys.build_string_audit_report(paths=(path,))

        must_i18n_texts = {record.text for record in report.must_i18n}
        violation_texts = {record.text for record in report.violation}
        self.assertGreaterEqual(len(must_i18n_texts), 400)
        self.assertFalse(any(source_keys.has_cjk(text) for text in must_i18n_texts))
        self.assertFalse(any(source_keys.has_cjk(text) for text in violation_texts))

    def test_audit_report_round_trips_through_json_file(self):
        report = source_keys.StringAuditReport(
            must_i18n=[
                source_keys.StringAuditRecord(
                    Path("src/game/client/components/foo.cpp"),
                    10,
                    "Play game",
                    "must_i18n",
                    "active source key (localize_or_localizable)",
                )
            ],
            business_data=[],
            test_only=[],
            needs_review=[],
            violation=[],
        )

        with TemporaryDirectory() as tmpdir:
            report_path = Path(tmpdir) / "audit.json"
            source_keys.write_string_audit_report(report_path, report)
            loaded = source_keys.read_string_audit_report(report_path)

        self.assertEqual(loaded.must_i18n[0].line, 10)
        self.assertEqual(loaded.must_i18n[0].text, "Play game")
        self.assertEqual(loaded.summary()["must_i18n"], 1)

    def test_source_record_cache_replaces_only_changed_files(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            unchanged = root / "src" / "game" / "client" / "unchanged.cpp"
            changed = root / "src" / "game" / "client" / "changed.cpp"
            deleted = root / "src" / "game" / "client" / "deleted.cpp"
            changed.parent.mkdir(parents=True, exist_ok=True)
            unchanged.write_text('Localize("Keep me");\n', encoding="utf-8")
            changed.write_text('Localize("New key");\n', encoding="utf-8")

            old_records = [
                source_keys.SourceKeyRecord(
                    "Keep me", "localize_or_localizable", unchanged, "", 1
                ),
                source_keys.SourceKeyRecord(
                    "Old key", "localize_or_localizable", changed, "", 1
                ),
                source_keys.SourceKeyRecord(
                    "Deleted key", "localize_or_localizable", deleted, "", 1
                ),
                source_keys.SourceKeyRecord("Extra test key", "extra", None, "", 0),
            ]

            merged = source_keys.merge_source_key_records_for_changed_files(
                old_records,
                changed_files=(changed, deleted),
                extra_strings={"Extra test key"},
            )

        identities = {record.identity() for record in merged}
        self.assertIn(("Keep me", ""), identities)
        self.assertIn(("New key", ""), identities)
        self.assertIn(("Extra test key", ""), identities)
        self.assertNotIn(("Old key", ""), identities)
        self.assertNotIn(("Deleted key", ""), identities)

    def test_source_record_cache_round_trips_json_file(self):
        with TemporaryDirectory() as tmpdir:
            cache_path = Path(tmpdir) / "records.json"
            source = Path(tmpdir) / "src" / "game" / "client" / "foo.cpp"
            records = [
                source_keys.SourceKeyRecord(
                    "Play", "localize_or_localizable", source, "Menu", 12
                ),
                source_keys.SourceKeyRecord("Extra", "extra", None, "", 0),
            ]

            source_keys.write_source_record_cache(cache_path, records)
            loaded = source_keys.read_source_record_cache(cache_path)

        self.assertEqual(set(records), set(loaded))

    def test_audit_report_replaces_only_changed_files(self):
        with TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            changed = root / "src" / "game" / "client" / "changed.cpp"
            kept = root / "src" / "game" / "client" / "kept.cpp"

            cached = source_keys.StringAuditReport(
                must_i18n=[
                    source_keys.StringAuditRecord(
                        kept,
                        1,
                        "Keep",
                        "must_i18n",
                        "active source key (localize_or_localizable)",
                    ),
                    source_keys.StringAuditRecord(
                        changed,
                        1,
                        "Old",
                        "must_i18n",
                        "active source key (localize_or_localizable)",
                    ),
                ],
                business_data=[],
                test_only=[],
                needs_review=[],
                violation=[],
            )

            changed.parent.mkdir(parents=True, exist_ok=True)
            changed.write_text('Localize("New");\n', encoding="utf-8")

            merged = source_keys.merge_string_audit_report_for_changed_files(
                cached, (changed,)
            )

        texts = {record.text for record in merged.must_i18n}
        self.assertIn("Keep", texts)
        self.assertIn("New", texts)
        self.assertNotIn("Old", texts)

    def test_extracts_browser_localize_keys_for_offline_and_local_server_ui(self):
        path = Path("src/game/client/components/menus_browser.cpp")
        content = source_keys.strip_cpp_comments(source_keys.read_source_text(path))
        keys = source_keys.extract_localize_keys(content)

        self.assertIn(("Clan Members", ""), keys)
        self.assertIn(("Offline", ""), keys)
        self.assertIn(("No local servers found (ports %d-%d)", ""), keys)
        self.assertIn(("Start and connect to local server", ""), keys)


if __name__ == "__main__":
    unittest.main()
