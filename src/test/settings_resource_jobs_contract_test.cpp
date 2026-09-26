#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <sstream>

TEST(SettingsResourceJobsContract, RuntimePrewarmCallsitesRequireVisibleIdleSettingsPage)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	EXPECT_EQ(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\ttrue,"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\tm_MenuPage == PAGE_SETTINGS,"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\tm_GamePage == PAGE_SETTINGS,"), std::string::npos);
	EXPECT_NE(Menus.find("m_SettingsPageSwitchActive || TransitionActive"), std::string::npos);
}

TEST(SettingsResourceJobsContract, IdlePrewarmSkipsImmediateModeRenderPasses)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t PrewarmPos = Source.find("void CMenus::PrewarmVisibleSettingsResources(CUIRect MainView)");
	ASSERT_NE(PrewarmPos, std::string::npos);
	const size_t PrewarmEnd = Source.find("bool CMenus::OnCursorMove", PrewarmPos);
	ASSERT_NE(PrewarmEnd, std::string::npos);
	const std::string PrewarmBody = Source.substr(PrewarmPos, PrewarmEnd - PrewarmPos);
	EXPECT_EQ(PrewarmBody.find("RenderSettingsTClient(ContentView, true)"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("RenderSettingsQmClient(ContentView, false, true)"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f)"), std::string::npos);
}
