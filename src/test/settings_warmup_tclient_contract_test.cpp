// 设置预热源码合同：TClient 分栏加载器与缓存增量。运行时行为保留在 settings_warmup_test.cpp。
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>

TEST(SettingsWarmupTClientContract, TClientReadOnlyPathUsesIsolatedSectionLoaders)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();

	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);

	EXPECT_NE(RenderBody.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderBody.find("CSectionLoader &VisualFontLoader = ReadOnly ? s_VisualFontReadOnlyLoader : s_VisualFontLoader;"), std::string::npos);
	EXPECT_NE(RenderBody.find("CSectionLoader &RightSectionLoader = ReadOnly ? s_RightSectionReadOnlyLoader : s_RightSectionLoader;"), std::string::npos);
	const size_t LeftPrewarmPos = RenderBody.find("if(ReadOnly)\n\t\t{", RenderBody.find("VisualFontLoader.Register"));
	ASSERT_NE(LeftPrewarmPos, std::string::npos);
	const size_t RightColumnPos = RenderBody.find("// ***** RightView *****", LeftPrewarmPos);
	ASSERT_NE(RightColumnPos, std::string::npos);
	const std::string LeftPrewarmBody = RenderBody.substr(LeftPrewarmPos, RightColumnPos - LeftPrewarmPos);
	EXPECT_NE(LeftPrewarmBody.find("VisualFontLoader.Process();"), std::string::npos);

	const size_t RightPrewarmPos = RenderBody.find("if(ReadOnly)\n\t\t{", RenderBody.find("RightSectionLoader.Register"));
	ASSERT_NE(RightPrewarmPos, std::string::npos);
	const size_t RightElsePos = RenderBody.find("else\n\t\t{", RightPrewarmPos);
	ASSERT_NE(RightElsePos, std::string::npos);
	const std::string RightPrewarmBody = RenderBody.substr(RightPrewarmPos, RightElsePos - RightPrewarmPos);
	EXPECT_NE(RightPrewarmBody.find("RightSectionLoader.Process();"), std::string::npos);
	EXPECT_EQ(RightPrewarmBody.find("return;"), std::string::npos);
	const size_t VisibleDeckGuard = RenderBody.find("if(!ReadOnly)", RightPrewarmPos);
	const size_t VisibleDeckRender = RenderBody.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\"", VisibleDeckGuard);
	ASSERT_NE(VisibleDeckGuard, std::string::npos);
	ASSERT_NE(VisibleDeckRender, std::string::npos);
	EXPECT_LT(VisibleDeckGuard, VisibleDeckRender);
}

TEST(SettingsWarmupTClientContract, TClientSectionLoadersEnableDeferredFarMeasurement)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();

	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);

	const size_t LeftProgressivePos = RenderBody.find("VisualFontLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);");
	ASSERT_NE(LeftProgressivePos, std::string::npos);
	const size_t LeftMaxSectionsPos = RenderBody.find("VisualFontLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?", LeftProgressivePos);
	ASSERT_NE(LeftMaxSectionsPos, std::string::npos);
	const size_t LeftDeferredPos = RenderBody.find("VisualFontLoader.SetDeferredFarMeasurementEnabled(true);", LeftMaxSectionsPos);
	ASSERT_NE(LeftDeferredPos, std::string::npos);
	const size_t LeftViewportPos = RenderBody.find("LeftLoaderViewport.y -= ScrollOffset.y;", LeftDeferredPos);
	ASSERT_NE(LeftViewportPos, std::string::npos);
	const size_t LeftBeginPos = RenderBody.find("VisualFontLoader.Begin(LeftView, LeftLoaderViewport, 5.0f);", LeftViewportPos);
	ASSERT_NE(LeftBeginPos, std::string::npos);

	const size_t RightProgressivePos = RenderBody.find("RightSectionLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);");
	ASSERT_NE(RightProgressivePos, std::string::npos);
	const size_t RightMaxSectionsPos = RenderBody.find("RightSectionLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?", RightProgressivePos);
	ASSERT_NE(RightMaxSectionsPos, std::string::npos);
	const size_t RightDeferredPos = RenderBody.find("RightSectionLoader.SetDeferredFarMeasurementEnabled(true);", RightMaxSectionsPos);
	ASSERT_NE(RightDeferredPos, std::string::npos);
	const size_t RightViewportPos = RenderBody.find("RightLoaderViewport.y -= ScrollOffset.y;", RightDeferredPos);
	ASSERT_NE(RightViewportPos, std::string::npos);
	const size_t RightBeginPos = RenderBody.find("RightSectionLoader.Begin(RightView, RightLoaderViewport, 5.0f);", RightViewportPos);
	ASSERT_NE(RightBeginPos, std::string::npos);
	EXPECT_EQ(RenderBody.find(".m_ScrollY"), std::string::npos);
}

TEST(SettingsWarmupTClientContract, TClientVisibleLoadersDoNotHashAllConfigsEveryFrame)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();
	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderPos, std::string::npos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);
	EXPECT_NE(RenderBody.find("MakeSettingsSectionRuntimeKey(LeftView, Graphics(), false)"), std::string::npos);
	EXPECT_EQ(RenderBody.find("HashTClientSettingsConfig("), std::string::npos);
	EXPECT_EQ(RenderBody.find("MakeSettingsSectionRuntimeKey(RightView, Graphics())"), std::string::npos);
}

TEST(SettingsWarmupTClientContract, TClientCardMeasurementUsesCardLayoutRevision)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	// 卡片布局 revision 的接线是架构约束；配置变化后的行为应由运行时测试覆盖。
	EXPECT_NE(TClient.find("HashTClientSettingsCardLayout(s_aDeckCardSpecs[Index].first)"), std::string::npos);
}

TEST(SettingsWarmupTClientContract, TClientVisualSettingsUseStableTextIdsForPrebuildCoverage)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(TClient.find("DoSettingsLabelStreamed(TitleElement, &Label, Localize(\"Font\")"), std::string::npos);
	EXPECT_NE(TClient.find("\"tclient-cursor-title\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-hammer-mode\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-cursor-scale\""), std::string::npos);
	EXPECT_EQ(TClient.find("tclient-wheel-animate-ms"), std::string::npos);
	EXPECT_EQ(TClient.find("tclient-wheel-animate-off"), std::string::npos);
}
