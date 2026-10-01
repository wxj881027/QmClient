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

TEST(SkinsContract, PrewarmPlayerPreviewReadyRequiresSelectedAndVisibleSourcesLoaded)
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

	EXPECT_NE(PrewarmBody.find("pSelectedContainer"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("SelectedReady"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("VisibleReadyCount"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("SettingsPreviewCacheContentHash()"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("DiskCacheArtifactsValid"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("FindTextures(CacheKey).has_value()"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("State == CSkinContainer::EState::LOADED"), std::string::npos);
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

TEST(SkinsContract, TeePrewarmNoLongerUsesImmediateBoolPath)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PrewarmByNamesPos = Source.find("void CSkins::PrewarmByNames(const std::vector<std::string> &vNames, bool Immediate)");
	ASSERT_NE(PrewarmByNamesPos, std::string::npos);
	const size_t PrewarmReadyPos = Source.find("bool CSkins::PrewarmPlayerPreviewReady(int Dummy, int MaxEntries, bool ProgressiveListReady)", PrewarmByNamesPos);
	ASSERT_NE(PrewarmReadyPos, std::string::npos);
	const std::string PrewarmByNamesBody = Source.substr(PrewarmByNamesPos, PrewarmReadyPos - PrewarmByNamesPos);

	EXPECT_EQ(PrewarmByNamesBody.find("RequestLoad(Immediate)"), std::string::npos);
	EXPECT_NE(PrewarmByNamesBody.find("Immediate ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::PREFETCH"), std::string::npos);

	const size_t FindImplPos = Source.find("const CSkins::CSkinContainer *CSkins::FindContainerImpl(const char *pName)");
	ASSERT_NE(FindImplPos, std::string::npos);
	const size_t FindOrNullptrPos = Source.find("const CSkin *CSkins::FindOrNullptr(const char *pName)", FindImplPos);
	ASSERT_NE(FindOrNullptrPos, std::string::npos);
	const std::string FindImplBody = Source.substr(FindImplPos, FindOrNullptrPos - FindImplPos);
	EXPECT_NE(FindImplBody.find("ExistingSkin->second->RequestLoad(true);"), std::string::npos);
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

TEST(SkinsContract, SkinRefreshKeepsExistingListWhileNewPlanLoads)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t RefreshPos = Source.find("void CSkins::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)");
	ASSERT_NE(RefreshPos, std::string::npos);
	const size_t StatsPos = Source.find("CSkins::CSkinLoadingStats CSkins::LoadingStats() const", RefreshPos);
	ASSERT_NE(StatsPos, std::string::npos);
	const std::string RefreshBody = Source.substr(RefreshPos, StatsPos - RefreshPos);

	EXPECT_EQ(RefreshBody.find("m_SkinList.m_vSkins.clear();"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("m_SkinList.m_UnfilteredCount = 0;"), std::string::npos);
	EXPECT_NE(RefreshBody.find("str_comp(pSkinContainer->Name(), \"default\") == 0"), std::string::npos);
	EXPECT_NE(RefreshBody.find("continue;"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING"), std::string::npos);
	EXPECT_NE(RefreshBody.find("pSkinContainer->SetState(pSkinContainer->DetermineInitialState());"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin.reset();"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("m_SkinsUsageList.clear();"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("m_SkinsBackgroundList.clear();"), std::string::npos);
}

TEST(SkinsContract, TeeSkinRefreshClearsListPreviewCacheBeforeReloadingSkinTextures)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t RefreshBranch = Menus.find("if(!RenderOnly && ShouldRefresh)");
	ASSERT_NE(RefreshBranch, std::string::npos);
	const size_t RefreshSkins = Menus.find("GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);", RefreshBranch);
	ASSERT_NE(RefreshSkins, std::string::npos);
	const std::string RefreshBody = Menus.substr(RefreshBranch, RefreshSkins - RefreshBranch);

	EXPECT_NE(Menus.find("void ClearSettingsTeeListPreviewCache()"), std::string::npos);
	EXPECT_NE(RefreshBody.find("ClearSettingsTeeListPreviewCache();"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListPreviewCacheKeysCoverPreviewVariantsAndStayBounded)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t CachePos = Menus.find("struct SSettingsTeeListPreviewCache");
	ASSERT_NE(CachePos, std::string::npos);
	const size_t CacheEnd = Menus.find("SSettingsTeeListPreviewCache gs_TeeListPreviewCache;", CachePos);
	ASSERT_NE(CacheEnd, std::string::npos);
	const std::string CacheBody = Menus.substr(CachePos, CacheEnd - CachePos);

	EXPECT_NE(CacheBody.find("QM_TEE_PREVIEW_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(CacheBody.find("static std::string Key(const char *pSkinName, int Dummy, bool UseCustomColor, int ColorBody, int ColorFeet, int Emote)"), std::string::npos);
	EXPECT_NE(CacheBody.find("pSkinName != nullptr ? pSkinName : \"\""), std::string::npos);
	EXPECT_NE(CacheBody.find("Dummy,"), std::string::npos);
	EXPECT_NE(CacheBody.find("UseCustomColor ? 1 : 0,"), std::string::npos);
	EXPECT_NE(CacheBody.find("ColorBody,"), std::string::npos);
	EXPECT_NE(CacheBody.find("ColorFeet,"), std::string::npos);
	EXPECT_NE(CacheBody.find("Emote);"), std::string::npos);

	const size_t PreviewKeyPos = Menus.find("const std::string PreviewCacheKey = SSettingsTeeListPreviewCache::Key(");
	ASSERT_NE(PreviewKeyPos, std::string::npos);
	const size_t PreviewKeyEnd = Menus.find(";", PreviewKeyPos);
	ASSERT_NE(PreviewKeyEnd, std::string::npos);
	const std::string PreviewKeyCall = Menus.substr(PreviewKeyPos, PreviewKeyEnd - PreviewKeyPos);
	EXPECT_NE(PreviewKeyCall.find("m_Dummy"), std::string::npos);
	EXPECT_NE(PreviewKeyCall.find("EntryUseCustomColor"), std::string::npos);
	EXPECT_NE(PreviewKeyCall.find("EntryColorBody"), std::string::npos);
	EXPECT_NE(PreviewKeyCall.find("EntryColorFeet"), std::string::npos);
	EXPECT_NE(PreviewKeyCall.find("*pEmote"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListLoadingEntriesUseDefaultSkinFallbackWithLoadingIndicator)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t RenderTeePos = Menus.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Menus.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Menus.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("State == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("RenderSkinStatus(Item.m_Rect, pSkinContainer, SkinListEntry.ErrorTooltipId(), PreviewCacheReady);"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("RenderSettingsSkinListPlaceholder"), std::string::npos);
}

TEST(SkinsContract, AbortedLocalSkinLoadJobStopsBeforeExpensiveRefreshWork)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t RunPos = Source.find("void CSkins::CSkinLoadJob::Run()");
	ASSERT_NE(RunPos, std::string::npos);
	const size_t DownloadRunPos = Source.find("void CSkins::CSkinDownloadJob::Run()", RunPos);
	ASSERT_NE(DownloadRunPos, std::string::npos);
	const std::string RunBody = Source.substr(RunPos, DownloadRunPos - RunPos);

	const size_t ReadFilePos = RunBody.find("Storage()->ReadFile(aPath, m_StorageType");
	const size_t DecodePos = RunBody.find("CImageLoader::LoadPng(pFileData, FileSize, aPath, m_Data.m_Info)");
	const size_t PreparePos = RunBody.find("PrepareSkinData(m_aName, m_Data)");
	ASSERT_NE(ReadFilePos, std::string::npos);
	ASSERT_NE(DecodePos, std::string::npos);
	ASSERT_NE(PreparePos, std::string::npos);

	EXPECT_LT(RunBody.find("if(State() == IJob::STATE_ABORTED)"), ReadFilePos);
	EXPECT_LT(RunBody.find("if(State() == IJob::STATE_ABORTED)", ReadFilePos), DecodePos);
	EXPECT_LT(RunBody.find("if(State() == IJob::STATE_ABORTED)", DecodePos), PreparePos);
}

TEST(SkinsContract, AsyncSkinListKeepsQueuedColorVariantsSelectable)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string Header = HeaderBuffer.str();

	std::ifstream SourceFile(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();

	std::ifstream MenuFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenuFile.good());
	std::stringstream MenuBuffer;
	MenuBuffer << MenuFile.rdbuf();
	const std::string MenuSource = MenuBuffer.str();

	EXPECT_NE(Header.find("struct SColorKey"), std::string::npos);
	EXPECT_NE(Header.find("const std::optional<SColorKey> &ColorKey() const"), std::string::npos);
	EXPECT_NE(Header.find("CSkinList &SkinList(int Dummy);"), std::string::npos);

	EXPECT_NE(Source.find("MakeSkinListColorKey(QueueEntry.m_UseCustomColor"), std::string::npos);
	EXPECT_NE(Source.find("m_vPendingSkinListMergeEntries = std::move(Result.m_Plan.m_vEntries);"), std::string::npos);
	EXPECT_NE(Source.find("Entry.m_ColorKey.has_value() ? std::make_optional(MakeSkinListColorKey(Entry.m_ColorKey.value())) : std::nullopt"), std::string::npos);
	EXPECT_NE(Source.find("MakeSkinListEntry(SkinIt->second.get(), ColorKey)"), std::string::npos);

	EXPECT_NE(MenuSource.find("SelectedSkinEntry.ColorKey().has_value()"), std::string::npos);
	EXPECT_NE(MenuSource.find("*pUseCustomColor = SelectedColorKey.m_UseCustomColor ? 1 : 0;"), std::string::npos);
	EXPECT_NE(MenuSource.find("*pColorBody = SelectedColorKey.m_ColorBody;"), std::string::npos);
	EXPECT_NE(MenuSource.find("*pColorFeet = SelectedColorKey.m_ColorFeet;"), std::string::npos);
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
