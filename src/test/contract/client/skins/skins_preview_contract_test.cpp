// Skins 静态源码合同：skins_preview_contract_test.cpp.
// 源码合同测试：皮肤资源、菜单集成和队列策略。运行时行为保留在 skins_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

extern CDataContainer *g_pData;

static std::string FunctionBody(const std::string &Source, const std::string &Signature)
{
	const size_t FunctionStart = Source.find(Signature);
	EXPECT_NE(FunctionStart, std::string::npos) << Signature;
	const size_t BodyStart = Source.find("{", FunctionStart);
	EXPECT_NE(BodyStart, std::string::npos) << Signature;
	int Depth = 0;
	for(size_t Index = BodyStart; Index < Source.size(); ++Index)
	{
		if(Source[Index] == '{')
			++Depth;
		else if(Source[Index] == '}')
		{
			--Depth;
			if(Depth == 0)
				return Source.substr(BodyStart, Index - BodyStart);
		}
	}
	ADD_FAILURE() << Signature;
	return {};
}

TEST(SkinsContract, PrewarmPlayerPreviewReadyNoLongerBuildsPreviewCacheKeys)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PrewarmPos = Source.find("bool CSkins::PrewarmPlayerPreviewReady(int Dummy, int MaxEntries, bool ProgressiveListReady)");
	ASSERT_NE(PrewarmPos, std::string::npos);
	const size_t PrewarmEnd = Source.find("void CSkins::QueueSkinListPlanJob(int Dummy)", PrewarmPos);
	ASSERT_NE(PrewarmEnd, std::string::npos);
	const std::string PrewarmBody = Source.substr(PrewarmPos, PrewarmEnd - PrewarmPos);

	EXPECT_EQ(PrewarmBody.find("SSettingsSkinPreviewCacheKey CacheKey"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("ColorBody"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("ColorFeet"), std::string::npos);
}

TEST(SkinsContract, TeeSettingsRequestsNoLongerPromoteToPendingAtRequestSite)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t RequestLoadPos = Source.find("void CSkins::CSkinContainer::RequestLoad(ESettingsResourcePriority Priority)");
	ASSERT_NE(RequestLoadPos, std::string::npos);
	const size_t RequestLoadEnd = Source.find("CSkins::CSkinContainer::EState CSkins::CSkinContainer::DetermineInitialState() const", RequestLoadPos);
	ASSERT_NE(RequestLoadEnd, std::string::npos);
	const std::string RequestLoadBody = Source.substr(RequestLoadPos, RequestLoadEnd - RequestLoadPos);

	EXPECT_NE(RequestLoadBody.find("const bool TeeSettingsActive = ActiveSettingsTeePage(m_pSkins->GameClient());"), std::string::npos);
	EXPECT_NE(RequestLoadBody.find("SetState(EState::BACKGROUND_REQUESTED, Priority);"), std::string::npos);
	EXPECT_EQ(RequestLoadBody.find("SetState(EState::PENDING, Priority);"), std::string::npos);
}

TEST(SkinsContract, SourceResidencyNoLongerDependsOnPreviewCachePins)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t UpdateUnload = Source.find("void CSkins::UpdateUnloadSkins");
	ASSERT_NE(UpdateUnload, std::string::npos);
	EXPECT_EQ(Source.find("IsSettingsPreviewCachePinned()", UpdateUnload), std::string::npos);

	const size_t ReclaimBackground = Source.find("bool CSkins::ReclaimBackgroundSkinForPriorityRequest");
	ASSERT_NE(ReclaimBackground, std::string::npos);
	EXPECT_EQ(Source.find("IsSettingsPreviewCachePinned()", ReclaimBackground), std::string::npos);
	EXPECT_NE(Source.find("if(pSkinContainer->m_State == CSkinContainer::EState::LOADED)"), ReclaimBackground);
	EXPECT_NE(Source.find("continue;"), ReclaimBackground);
	EXPECT_EQ(Source.find("NumPendingLoadingLoaded"), std::string::npos);
}

TEST(SkinsContract, SkinListWaitsForCompletePlanInsteadOfSeedingPlaceholderEntry)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("SeedVisibleSkinListIfEmpty"), std::string::npos);
	EXPECT_NE(Source.find("m_SkinList.m_vSkins = std::move(m_vPendingSkinListEntries);"), std::string::npos);
}

TEST(SkinsContract, PrepareSkinDataResetsMetricsBeforeWritingPlan)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string PrepareSkinData = FunctionBody(Source, "bool CSkins::PrepareSkinData(const char *pName, CSkinLoadData &Data)");
	const size_t ResetPos = PrepareSkinData.find("Data.m_Metrics.Reset();");
	const size_t FirstWritePos = PrepareSkinData.find("Data.m_Metrics.m_Body.m_Width = Plan.m_Body.m_Width;");

	ASSERT_NE(ResetPos, std::string::npos);
	ASSERT_NE(FirstWritePos, std::string::npos);
	EXPECT_LT(ResetPos, FirstWritePos);
}
