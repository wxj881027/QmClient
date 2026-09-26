// QmNewUi 菜单源码合同：gameplay 记分板与颜色域。运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuGameplayScoreboardContract, SettingsCardMigrationsKeepVersionPendingWhenExactMigrationFails)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const size_t ProfilesStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 2)");
	const size_t StatusBarStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 3)");
	const size_t TeeStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 4)");
	ASSERT_NE(ProfilesStart, std::string::npos);
	ASSERT_NE(StatusBarStart, std::string::npos);
	ASSERT_NE(TeeStart, std::string::npos);
	const std::string Profiles = Source.substr(ProfilesStart, StatusBarStart - ProfilesStart);
	const std::string StatusBar = Source.substr(StatusBarStart, TeeStart - StatusBarStart);
	const std::string Tee = Source.substr(TeeStart, Source.find("if(g_Config.m_QmCardLayoutVersion < 5)", TeeStart) - TeeStart);
	for(const std::string *pMigration : {&Profiles, &StatusBar, &Tee})
	{
		const size_t ShouldMigrate = pMigration->find("const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;");
		const size_t CandidateChanged = pMigration->find("const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout", ShouldMigrate);
		const size_t FailureGuard = pMigration->find("if(ShouldMigrate && !CandidateChanged)", CandidateChanged);
		const size_t Persist = pMigration->find("if(!PersistCandidate(Candidate, CandidateChanged))", FailureGuard);
		ASSERT_NE(ShouldMigrate, std::string::npos);
		ASSERT_NE(CandidateChanged, std::string::npos);
		ASSERT_NE(FailureGuard, std::string::npos);
		ASSERT_NE(Persist, std::string::npos);
		EXPECT_LT(ShouldMigrate, CandidateChanged);
		EXPECT_LT(CandidateChanged, FailureGuard);
		EXPECT_LT(FailureGuard, Persist);
	}
}

TEST(QmNewUiMenuGameplayScoreboardContract, NewOpacityControlsDoNotChainLegacyPanelOpacity)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");

	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClSettingsTabbarOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
}

TEST(QmNewUiMenuGameplayScoreboardContract, NewColorControlsUseIndependentUiDomains)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmUiColor)"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_EQ(MenusSource.find("ColorHSLA(g_Config.m_ClMenuPanelColor)"), std::string::npos);
	const std::string UpdateColors = FunctionBody(MenusSource, "void CMenus::UpdateColors()");
	const std::string RenderBackground = FunctionBody(MenusSource, "void CMenus::RenderBackground()");
	EXPECT_NE(UpdateColors.find("ColorHSLA(g_Config.m_UiColor, true)"), std::string::npos);
	EXPECT_NE(RenderBackground.find("ms_GuiColor.WithAlpha(1.0f)"), std::string::npos);
	EXPECT_EQ(RenderBackground.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(BrowserSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("ColorHSLA(g_Config.m_QmScoreboardColor)"), std::string::npos);
}

TEST(QmNewUiMenuGameplayScoreboardContract, ScoreboardBackgroundsUseScoreboardOpacity)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(Source.find("Color.a = ScoreboardUiAlpha(AlphaScale);"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity / 100.0f"), std::string::npos);
	EXPECT_NE(Source.find("ScoreboardDecorationColor(GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(ItemAlpha * 1.45f))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha))"), std::string::npos);
}

TEST(QmNewUiMenuGameplayScoreboardContract, ScoreboardDdTeamLabelUsesUnifiedBelowRowLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(RenderScoreboard.find("ResolveScoreboardTeamLabelLayout("), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("if(EndsDDTeam)"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TextRender()->Text(TeamLabelLayout.m_X, TeamLabelLayout.m_Y, TeamFontSize, aBuf);"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TeamLabelLayout.m_IconY"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("SCOREBOARD_TEAM_MODE_ICON_SIZE"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("TeamLabelLayout.m_Y,\n\t\t\t\t\tTeamFontSize"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("NumPlayers > 8"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("State.m_TeamStartX"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f"), std::string::npos);
}

TEST(QmNewUiMenuGameplayScoreboardContract, ScoreboardMediaButtonSymbolsFollowContentAlpha)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string Helper = FunctionBody(Source, "int DoScoreboardMediaIconButton(");

	EXPECT_NE(Helper.find("const float IconAlpha = std::clamp(ContentAlpha"), std::string::npos);
	EXPECT_NE(Helper.find("DefaultTextColor().WithMultipliedAlpha(IconAlpha)"), std::string::npos);
	EXPECT_NE(Helper.find("ColorRGBA(1.0f, 0.0f, 0.0f, IconAlpha)"), std::string::npos);
	EXPECT_NE(Helper.find("FontIcons::FONT_ICON_SLASH"), std::string::npos);
	// 计分板的三个 SMTC 播放控制按钮已按远程结果删除，助手只服务影子回放控制条。
	EXPECT_EQ(Source.find("s_SmtcPrevButton"), std::string::npos);
	EXPECT_EQ(Source.find("s_SmtcPlayButton"), std::string::npos);
	EXPECT_EQ(Source.find("s_SmtcNextButton"), std::string::npos);
	EXPECT_NE(Source.find("DoScoreboardMediaIconButton(Ui(), TextRender(), &s_GhostPlayButton"), std::string::npos);
	EXPECT_NE(Source.find("DoScoreboardMediaIconButton(Ui(), TextRender(), &s_GhostCloseButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcPrevButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcPlayButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcNextButton"), std::string::npos);
}

TEST(QmNewUiMenuGameplayScoreboardContract, ScoreboardUsesOneRowPlanAndDenseTeeLod)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string OnRender = FunctionBody(Source, "void CScoreboard::OnRender()");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(OnRender.find("BuildPlayerRowPlan"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TEE_PREVIEW_LAYER_BODY"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("for(int j ="), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("const bool HasWar ="), std::string::npos);

	// The rendering optimization must not alter point lookup or display behavior.
	EXPECT_NE(OnRender.find("m_PlayerPoints.EnsureQueried"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("m_PlayerPoints.GetPoints"), std::string::npos);
}
