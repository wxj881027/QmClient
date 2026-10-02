#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(SectionLoaderCleanup, HeaderNoLongerExposesRecordedTargetCacheApi)
{
	std::ifstream File(TestSourcePath("src/game/client/components/section_loader.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("m_CanCacheStaticLayer"), std::string::npos);
	EXPECT_EQ(Source.find("m_RenderStaticLayerFn"), std::string::npos);
	EXPECT_EQ(Source.find("m_RenderInteractiveLayerFn"), std::string::npos);
	EXPECT_EQ(Source.find("m_ShouldRenderInteractiveLayerFn"), std::string::npos);
	EXPECT_EQ(Source.find("m_CacheValid"), std::string::npos);
	EXPECT_EQ(Source.find("m_RenderTarget"), std::string::npos);
	EXPECT_EQ(Source.find("m_RenderTargetWidth"), std::string::npos);
	EXPECT_EQ(Source.find("m_RenderTargetHeight"), std::string::npos);
	EXPECT_EQ(Source.find("m_StaticCachePadding"), std::string::npos);
	EXPECT_EQ(Source.find("PrewarmStaticRenderTargets"), std::string::npos);
	EXPECT_EQ(Source.find("SetGraphicsForCache"), std::string::npos);
	EXPECT_EQ(Source.find("SetLiveStaticCacheRecordingEnabled"), std::string::npos);
	EXPECT_EQ(Source.find("SetRenderTargetSupportedForTests"), std::string::npos);
	EXPECT_EQ(Source.find("MarkCacheValidForTests"), std::string::npos);
	EXPECT_EQ(Source.find("IsCacheValidForTests"), std::string::npos);
	EXPECT_EQ(Source.find("InvalidateSectionByName"), std::string::npos);
	EXPECT_EQ(Source.find("PrewarmSectionByName"), std::string::npos);
	EXPECT_EQ(Source.find("DrawCachedSectionByName"), std::string::npos);
	EXPECT_EQ(Source.find("MakeRenderTargetCacheRectForTests"), std::string::npos);
	EXPECT_EQ(Source.find("m_LiveStaticCacheRecordingEnabled"), std::string::npos);
}

TEST(SectionLoaderCleanup, HeaderNoLongerExposesCardHeightOrScrollTruth)
{
	std::ifstream File(TestSourcePath("src/game/client/components/section_loader.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("CachedHeightForStableCardId"), std::string::npos);
	EXPECT_EQ(Source.find("m_ScrollY"), std::string::npos);
	EXPECT_NE(Source.find("void Begin(CUIRect MainView, CUIRect Viewport, float TimeBudgetMs);"), std::string::npos);
}
