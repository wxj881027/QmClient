#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/SettingsToggleInput.h>
#include <game/client/QmUi/UiConfigHint.h>
#include <game/client/QmUi/UiConfigHintText.h>
#include <game/client/QmUi/UiSwitchTipPolicy.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	// 只记录控件提交给气泡系统的参数。趟次、配置绑定和说明选择都走生产入口。
	struct SRecordingUi
	{
		bool m_RenderOnly = false;
		const void *m_pBoundId = nullptr;
		const void *m_pValue = nullptr;
		const char *m_pLabel = nullptr;
		const void *m_pDescriptionId = nullptr;
		const char *m_pDescription = nullptr;

		bool RenderOnly() const { return m_RenderOnly; }
		bool MouseHovered(const CUIRect *) const { return true; }
		bool DoButtonLogic(const void *, int, const CUIRect *, int) { return false; }
		SRecordingUi &Tooltips() { return *this; }
		void DoConfigTooltip(const void *, const CUIRect *, const void *) { ADD_FAILURE() << "Unexpected non-settings registration"; }
		void DoSettingsToolTipForConfig(const void *pId, const CUIRect *, const void *pValue, const CUIRect *, const void *, const char *pLabel)
		{
			m_pBoundId = pId;
			m_pValue = pValue;
			m_pLabel = pLabel;
		}
		void DoToolTipForRect(const void *pId, const CUIRect *, const char *pText)
		{
			EXPECT_EQ(m_pBoundId, pId);
			m_pDescriptionId = pId;
			m_pDescription = pText;
		}
		void DoToolTip(const void *pId, const CUIRect *pRect, const char *pText) { DoToolTipForRect(pId, pRect, pText); }
	};

	class SettingsSwitchTooltipIntegration : public testing::Test
	{
	protected:
		CConfig m_Previous = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole = CreateConsole(CFGFLAG_CLIENT);
		std::unique_ptr<IConfigManager> m_pConfig{CreateConfigManager()};

		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel->RegisterInterface(m_pStorage.get(), false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfig.get(), false);
			m_pConsole->Init();
			m_pConfig->Init();
		}
		void TearDown() override { g_Config = m_Previous; }
	};
}

TEST_F(SettingsSwitchTooltipIntegration, AuditedSwitchesLeaveOnlyTheSearchCommand)
{
	struct SRow
	{
		int *m_pValue;
		const char *m_pLabel;
	};
	for(const SRow &Row : {SRow{&g_Config.m_QmScoreboardOnDeath, "死亡后显示计分板"}, {&g_Config.m_QmUiCardRainbowTitles, "彩虹卡片标题"}, {&g_Config.m_QmFocusModeHideHud, "隐藏 HUD"}, {&g_Config.m_SndGun, "启用枪声"}, {&g_Config.m_ClShowKillMessages, "显示击杀消息"}, {&g_Config.m_QmTranslateAutoOutgoing, "自动翻译发送消息"}, {&g_Config.m_GfxHighDetail, "High Detail"}, {&g_Config.m_ClAutoDemoRecord, "Automatically record demos"}, {&g_Config.m_ClAutoScreenshot, "Automatically take game over screenshot"}, {&g_Config.m_QmChangeNameNearFinish, "Attempt to change your name when near finish"}})
	{
		SCOPED_TRACE(Row.m_pLabel);
		const char *pName = QmUiConfigCommand(g_Config, Row.m_pValue);
		ASSERT_NE(pName, nullptr);
		const auto *pVariable = m_pConfig->FindVariable(pName);
		ASSERT_NE(pVariable, nullptr);
		const char *pKey = pVariable->m_pHelpLocalizeKey;
		ASSERT_NE(pKey, nullptr);
		ASSERT_NE(*pKey, '\0');
		CUiConfigHintText Hint;
		Hint.SetCommands(pName);
		Hint.SetFallbackDescription(ui_switch_tip::Fallback(pName, pKey, Row.m_pLabel, pKey));
		EXPECT_STREQ(Hint.Text(false), "");
		EXPECT_STREQ(Hint.Text(true), pName);
		Hint.SetDescription("Controlled by Gores");
		EXPECT_STREQ(Hint.Text(false), "Controlled by Gores");
		Hint.SetDescription("");
		EXPECT_STREQ(Hint.Text(false), "");
	}
}

TEST_F(SettingsSwitchTooltipIntegration, MechanismDescriptionsSurviveInFullOnBothPages)
{
	for(const auto &[pName, pLabel] : {std::pair{"qm_auto_margin", "Auto margin"}, {"qm_nameplate_coord_x_align_hint_strict", "Strict alignment"}, {"qm_freeze_wakeup_popup", "Freeze wakeup popup"}, {"qm_blank_asset_fallback", "Blank asset auto fallback"}, {"qm_self_kill_enabled", "Self-kill protection"}, {"qm_solo_split_auto_team_lock", "Lock team"}, {"qm_unpred_others_in_freeze", "Dont predict other players if you are frozen"}})
	{
		SCOPED_TRACE(pName);
		const auto *pVariable = m_pConfig->FindVariable(pName);
		ASSERT_NE(pVariable, nullptr);
		const char *pKey = pVariable->m_pHelpLocalizeKey;
		ASSERT_NE(pKey, nullptr);
		ASSERT_NE(*pKey, '\0');
		CUiConfigHintText Hint;
		Hint.SetCommands(pName);
		Hint.SetFallbackDescription(ui_switch_tip::Fallback(pName, pKey, pLabel, pKey));
		EXPECT_STREQ(Hint.Text(false), pKey);
		EXPECT_EQ(Hint.Text(true), std::string(pKey) + "\n" + pName);
		EXPECT_STREQ(ui_switch_tip::Fallback(pName, pKey, pLabel, pLabel), "");
	}
}

TEST_F(SettingsSwitchTooltipIntegration, ConfigFiltersRegisterLabeledSearchCommandsDuringCardInput)
{
	const SSettingsToggleEntry aEntries[] = {
		{&g_Config.m_QmUiCompactList, "Compact", "Compact"},
		{&g_Config.m_QmUiOnlyModified, "Modified", "Modified"}};
	const char *apCommands[] = {"qm_ui_compact_list", "qm_ui_only_modified"};
	CSettingsContentLayoutCache Cache;
	const auto pLayout = Cache.Resolve({{"filters", "", 24.0f}}, 200.0f, 0, {});
	for(ESettingsContentPass Pass : {ESettingsContentPass::INPUT, ESettingsContentPass::DRAW})
	{
		CSettingsContentLayoutScope Scope(*pLayout, Pass);
		CUIRect Content{0, 0, 200, 24}, Row;
		CSettingsContentLayoutFlow Flow(Content);
		ASSERT_TRUE(Flow.Control("filters", 24.0f, Row));
		for(size_t i = 0; i < std::size(aEntries); ++i)
		{
			SCOPED_TRACE(apCommands[i]);
			SRecordingUi Ui;
			Ui.m_RenderOnly = Pass == ESettingsContentPass::DRAW;
			DoSettingsToggleButtonInput(Ui, Flow, aEntries[i], Row, Row, nullptr, true, Flow.Input());
			if(Ui.RenderOnly())
			{
				EXPECT_EQ(Ui.m_pBoundId, nullptr);
				continue;
			}
			ASSERT_EQ(Ui.m_pBoundId, aEntries[i].m_pValue);
			ASSERT_STREQ(Ui.m_pLabel, aEntries[i].m_pLabel);
			const char *pCommand = QmUiConfigCommand(g_Config, Ui.m_pValue);
			ASSERT_STREQ(pCommand, apCommands[i]);
			const auto *pVariable = m_pConfig->FindVariable(pCommand);
			ASSERT_NE(pVariable, nullptr);
			const char *pHelp = pVariable->m_pHelpLocalizeKey;
			ASSERT_NE(pHelp, nullptr);
			CUiConfigHintText Hint;
			Hint.SetCommands(pCommand);
			Hint.SetFallbackDescription(ui_switch_tip::Fallback(pCommand, pHelp, Ui.m_pLabel, pHelp));
			EXPECT_STREQ(Hint.Text(true), apCommands[i]);
			EXPECT_STREQ(Hint.Text(false), "");
		}
	}
}

TEST_F(SettingsSwitchTooltipIntegration, ScrollRowKeepsCommandWithOverrideSpecificOrFallbackHelp)
{
	int Id = 0;
	SSettingsToggleEntry Entry{&g_Config.m_QmScoreboardScroll, "Scroll mode", "Scroll mode", &Id};
	const CUIRect Row{0, 0, 200, 24};
	const char *pCursor = "Use the scoreboard cursor mode to scroll the list";
	const char *pOverride = "Controlled by Gores";
	struct SCase
	{
		const char *m_pOverride;
		const char *m_pEntryHelp;
		const char *m_pExpected;
	};
	for(const SCase &Case : {SCase{pOverride, pCursor, pOverride}, {nullptr, pCursor, pCursor}, {nullptr, "Enable scroll mode", nullptr}, {nullptr, nullptr, nullptr}})
	{
		SRecordingUi Ui;
		Entry.m_pTooltip = Case.m_pEntryHelp;
		DoSettingsToggleRow(Ui, Entry, Row, Case.m_pOverride, true, false, [&](const void *pId, const void *pValue, bool, bool Enabled) {
			EXPECT_EQ(Enabled, Case.m_pOverride == nullptr);
			Ui.DoSettingsToolTipForConfig(pId, &Row, pValue, &Row, nullptr, Entry.m_pLabel);
			return false;
		});
		const char *pCommand = QmUiConfigCommand(g_Config, Ui.m_pValue);
		ASSERT_EQ(Ui.m_pBoundId, &Id);
		ASSERT_STREQ(pCommand, "qm_scoreboard_scroll");
		EXPECT_STREQ(Ui.m_pDescription, Case.m_pExpected);
		if(Case.m_pExpected)
			EXPECT_EQ(Ui.m_pDescriptionId, Ui.m_pBoundId);
		const auto *pVariable = m_pConfig->FindVariable(pCommand);
		ASSERT_NE(pVariable, nullptr);
		const char *pHelp = pVariable->m_pHelpLocalizeKey;
		ASSERT_NE(pHelp, nullptr);
		CUiConfigHintText Hint;
		Hint.SetCommands(pCommand);
		Hint.SetFallbackDescription(ui_switch_tip::Fallback(pCommand, pHelp, Ui.m_pLabel, pHelp));
		Hint.SetDescription(Ui.m_pDescription);
		const char *pExpected = Case.m_pExpected ? Case.m_pExpected : pHelp;
		EXPECT_STREQ(Hint.Text(false), pExpected);
		EXPECT_EQ(Hint.Text(true), std::string(pExpected) + "\n" + pCommand);
	}
}
