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

static vec2 ComputeRenderedTeeMid(const CTeeRenderInfo &Info)
{
	const CAnimState *pIdle = CAnimState::GetIdle();
	float AnimScale, BaseSize;
	CRenderTools::GetRenderTeeAnimScaleAndBaseSize(&Info, AnimScale, BaseSize);
	const vec2 BodyPos = vec2(pIdle->GetBody()->m_X, pIdle->GetBody()->m_Y) * AnimScale;
	const float AssumedScale = BaseSize / 64.0f;
	vec2 BodyOffset;
	float BodyWidth, BodyHeight;
	CRenderTools::GetRenderTeeBodySize(pIdle, &Info, BodyOffset, BodyWidth, BodyHeight);
	vec2 FeetOffset;
	float FeetWidth, FeetHeight;
	CRenderTools::GetRenderTeeFeetSize(pIdle, &Info, FeetOffset, FeetWidth, FeetHeight);
	const vec2 FeetPos[2] = {
		vec2(pIdle->GetFrontFoot()->m_X, pIdle->GetFrontFoot()->m_Y) * AnimScale,
		vec2(pIdle->GetBackFoot()->m_X, pIdle->GetBackFoot()->m_Y) * AnimScale,
	};
	float MinX = -32.0f * AssumedScale + BodyPos.x + BodyOffset.x;
	float MaxX = MinX + BodyWidth;
	for(const vec2 &FootPos : FeetPos)
	{
		const float FootMinX = -32.0f * AssumedScale + FootPos.x + FeetOffset.x;
		MinX = minimum(MinX, FootMinX);
		MaxX = maximum(MaxX, FootMinX + FeetWidth);
	}
	float MinY = -32.0f * AssumedScale + BodyPos.y + BodyOffset.y;
	float MaxY = MinY + BodyHeight;
	for(const vec2 &FootPos : FeetPos)
	{
		MaxY = maximum(MaxY, -16.0f * AssumedScale + FootPos.y + FeetOffset.y + FeetHeight);
	}
	return vec2(MinX + (MaxX - MinX) / 2.0f, MinY + (MaxY - MinY) / 2.0f);
}

static void SetBeastLikeMetrics(CTeeRenderInfo &Info)
{
	Info.m_Size = 64.0f;
	Info.m_SkinMetrics.m_Body.m_Width = 80;
	Info.m_SkinMetrics.m_Body.m_Height = 82;
	Info.m_SkinMetrics.m_Body.m_OffsetX = 16;
	Info.m_SkinMetrics.m_Body.m_OffsetY = 14;
	Info.m_SkinMetrics.m_Body.m_MaxWidth = 96;
	Info.m_SkinMetrics.m_Body.m_MaxHeight = 96;
	Info.m_SkinMetrics.m_Feet.m_Width = 44;
	Info.m_SkinMetrics.m_Feet.m_Height = 23;
	Info.m_SkinMetrics.m_Feet.m_OffsetX = 20;
	Info.m_SkinMetrics.m_Feet.m_OffsetY = 9;
	Info.m_SkinMetrics.m_Feet.m_MaxWidth = 64;
	Info.m_SkinMetrics.m_Feet.m_MaxHeight = 32;
}

static void SetTestPixel(CImageInfo &Image, size_t x, size_t y, uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t Alpha)
{
	const size_t Offset = (y * Image.m_Width + x) * Image.PixelSize();
	Image.m_pData[Offset] = Red;
	Image.m_pData[Offset + 1] = Green;
	Image.m_pData[Offset + 2] = Blue;
	Image.m_pData[Offset + 3] = Alpha;
}

static CImageInfo MakeTestSkinImage(size_t Width, size_t Height, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
{
	CImageInfo Image;
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = Format;
	Image.m_pData = static_cast<uint8_t *>(calloc(Image.DataSize(), 1));
	return Image;
}

TEST(Skins, UsageTrackingSkipsAlwaysLoadedStates)
{
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_FALSE(CSkins::CSkinContainer::TracksUsage(EState::PENDING, true));
	EXPECT_FALSE(CSkins::CSkinContainer::TracksUsage(EState::LOADING, true));
	EXPECT_FALSE(CSkins::CSkinContainer::TracksUsage(EState::LOADED, true));

	EXPECT_TRUE(CSkins::CSkinContainer::TracksUsage(EState::PENDING, false));
	EXPECT_TRUE(CSkins::CSkinContainer::TracksUsage(EState::LOADING, false));
	EXPECT_TRUE(CSkins::CSkinContainer::TracksUsage(EState::LOADED, false));
	EXPECT_FALSE(CSkins::CSkinContainer::TracksUsage(EState::UNLOADED, false));
}

TEST(Skins, AlwaysLoadedStateTransitionsNeverTouchUsageList)
{
	using EState = CSkins::CSkinContainer::EState;

	for(const EState State : {EState::PENDING, EState::LOADING, EState::LOADED})
	{
		const auto Clean = CSkins::CSkinContainer::UsageTrackingUpdate(State, true, false);
		EXPECT_FALSE(Clean.m_ShouldTouch);
		EXPECT_FALSE(Clean.m_ShouldErase);

		const auto Polluted = CSkins::CSkinContainer::UsageTrackingUpdate(State, true, true);
		EXPECT_FALSE(Polluted.m_ShouldTouch);
		EXPECT_TRUE(Polluted.m_ShouldErase);
	}
}

TEST(Skins, UsageListEntriesThatCannotBeUnloadedAreDiscarded)
{
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_TRUE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(false, EState::LOADED, false));
	EXPECT_TRUE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, EState::LOADED, true));
	EXPECT_TRUE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, EState::NOT_FOUND, false));
	EXPECT_FALSE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, EState::PENDING, false));
	EXPECT_FALSE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, EState::LOADING, false));
	EXPECT_FALSE(CSkins::CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, EState::LOADED, false));
}

TEST(Skins, OnlyNotFoundTransitionsRequireSkinListRefresh)
{
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_FALSE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::UNLOADED, EState::PENDING));
	EXPECT_FALSE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::PENDING, EState::LOADING));
	EXPECT_FALSE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::LOADING, EState::LOADED));
	EXPECT_FALSE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::ERROR, EState::UNLOADED));

	EXPECT_TRUE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::LOADING, EState::NOT_FOUND));
	EXPECT_TRUE(CSkins::CSkinContainer::StateChangeRequiresListRefresh(EState::NOT_FOUND, EState::PENDING));
}

TEST(Skins, RegularStateTransitionsEnterAndLeaveUsageList)
{
	using EState = CSkins::CSkinContainer::EState;

	for(const EState State : {EState::PENDING, EState::LOADING, EState::LOADED})
	{
		const auto MissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(State, false, false);
		EXPECT_TRUE(MissingEntry.m_ShouldTouch);
		EXPECT_FALSE(MissingEntry.m_ShouldErase);

		const auto ExistingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(State, false, true);
		EXPECT_FALSE(ExistingEntry.m_ShouldTouch);
		EXPECT_FALSE(ExistingEntry.m_ShouldErase);
	}

	for(const EState State : {EState::UNLOADED, EState::ERROR, EState::NOT_FOUND})
	{
		const auto ExistingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(State, false, true);
		EXPECT_FALSE(ExistingEntry.m_ShouldTouch);
		EXPECT_TRUE(ExistingEntry.m_ShouldErase);
	}
}

TEST(Skins, ImmediateRequestLoadShouldTouchUsageTrackingData)
{
	using EState = CSkins::CSkinContainer::EState;
	const auto MissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::PENDING, false, false);
	EXPECT_TRUE(MissingEntry.m_ShouldTouch);
	EXPECT_FALSE(MissingEntry.m_ShouldErase);
}

TEST(Skins, BackgroundRequestDoesNotTouchPriorityUsageTrackingData)
{
	using EState = CSkins::CSkinContainer::EState;
	const auto MissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::PENDING, false, false, ESettingsResourcePriority::BACKGROUND);
	EXPECT_FALSE(MissingEntry.m_ShouldTouch);
	EXPECT_FALSE(MissingEntry.m_ShouldErase);

	const auto ExistingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::LOADED, false, true, ESettingsResourcePriority::BACKGROUND);
	EXPECT_FALSE(ExistingEntry.m_ShouldTouch);
	EXPECT_TRUE(ExistingEntry.m_ShouldErase);
}

TEST(Skins, HighPriorityBackgroundRequestedTouchesUsageTrackingData)
{
	using EState = CSkins::CSkinContainer::EState;

	const auto VisibleMissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::BACKGROUND_REQUESTED, false, false, ESettingsResourcePriority::VISIBLE);
	EXPECT_TRUE(VisibleMissingEntry.m_ShouldTouch);
	EXPECT_FALSE(VisibleMissingEntry.m_ShouldErase);

	const auto PrefetchMissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::BACKGROUND_REQUESTED, false, false, ESettingsResourcePriority::PREFETCH);
	EXPECT_TRUE(PrefetchMissingEntry.m_ShouldTouch);
	EXPECT_FALSE(PrefetchMissingEntry.m_ShouldErase);

	const auto BackgroundMissingEntry = CSkins::CSkinContainer::UsageTrackingUpdate(EState::BACKGROUND_REQUESTED, false, false, ESettingsResourcePriority::BACKGROUND);
	EXPECT_FALSE(BackgroundMissingEntry.m_ShouldTouch);
	EXPECT_FALSE(BackgroundMissingEntry.m_ShouldErase);
}

TEST(Skins, TeeBackgroundDrainSeparatesRequestedBacklogFromAdmittedQueue)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("BACKGROUND_REQUESTED"), std::string::npos);
	EXPECT_NE(Source.find("size_t m_NumBackgroundRequested = 0;"), std::string::npos);
	EXPECT_NE(Source.find("return !ExistsInSkinMap || (!TracksUsage(State, AlwaysLoaded) && State != EState::BACKGROUND_REQUESTED);"), std::string::npos);
}

TEST(Skins, LoadingStatsRealInflightExcludesBackgroundRequested)
{
	CSkins::CSkinLoadingStats Stats;
	Stats.m_NumBackgroundRequested = 999;
	Stats.m_NumPending = 7;
	Stats.m_NumLoading = 11;

	EXPECT_EQ(Stats.RealInflight(), 18u);
	EXPECT_FALSE(Stats.AdmissionInvariantViolated(18));
	EXPECT_TRUE(Stats.AdmissionInvariantViolated(17));
}

TEST(Skins, TeeBackgroundRequestsWaitForAdmissionBeforePending)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("if(m_State == EState::UNLOADED)\n\t\t\tSetState(EState::BACKGROUND_REQUESTED, ESettingsResourcePriority::BACKGROUND);"), std::string::npos);
	EXPECT_NE(Source.find("if(m_State == EState::UNLOADED || m_State == EState::BACKGROUND_REQUESTED)"), std::string::npos);
	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("if(Stats.m_NumPending == 0 && pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)"), std::string::npos);
	EXPECT_NE(StartLoading.find("if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)"), std::string::npos);
	EXPECT_NE(StartLoading.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING, Admission.m_PromotePriority);"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumBackgroundRequested--;"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumPending++;"), std::string::npos);
}

TEST(Skins, TeeSkinListVirtualizationKeepsTotalListLength)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettings", RenderTeePos + 1);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("s_ListBox.DoStart(TeeSkinListRowHeight, vSkinList.size(), TeeSkinListItemsPerRow, 2, OldSelected, &MainView);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("s_ListBox.SkipItems("), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("int RowsRendered = 0;"), std::string::npos);
	const size_t RowsRendered = RenderTeeBody.find("++RowsRendered;");
	EXPECT_NE(RowsRendered, std::string::npos);
	EXPECT_LT(RenderTeeBody.find("if(RowStart)"), RowsRendered);
	EXPECT_EQ(RenderTeeBody.find("const int RowsRendered = RowsIterated;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=list_frame page=settings:tee"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("rows_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d"), std::string::npos);
}

TEST(Skins, TeeSkinListVirtualizationUsesFourColumnContract)
{
	const std::string JobsSource = ReadTestSourceFile("src/game/client/components/settings_resource_jobs.cpp");
	const std::string Source = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("constexpr int TeeSkinListItemsPerRow = 4;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("s_ListBox.DoStart(TeeSkinListRowHeight, vSkinList.size(), TeeSkinListItemsPerRow"), std::string::npos);
	const size_t VisibleRange = RenderTeeBody.find("SettingsSkinListVisibleRangeForScroll(");
	ASSERT_NE(VisibleRange, std::string::npos);
	const size_t VisibleRangeEnd = RenderTeeBody.find(");", VisibleRange);
	ASSERT_NE(VisibleRangeEnd, std::string::npos);
	const std::string VisibleRangeCall = RenderTeeBody.substr(VisibleRange, VisibleRangeEnd - VisibleRange);
	const size_t ScrollOffset = VisibleRangeCall.find("s_ListBox.ScrollOffsetY()");
	const size_t ViewHeight = VisibleRangeCall.find("s_ListBox.ViewHeight()");
	const size_t RowHeight = VisibleRangeCall.find("TeeSkinListRowHeight");
	const size_t ItemsPerRow = VisibleRangeCall.find("TeeSkinListItemsPerRow");
	const size_t ListSize = VisibleRangeCall.find("(int)vSkinList.size()");
	const size_t OverscanRows = VisibleRangeCall.find("1");
	ASSERT_NE(ScrollOffset, std::string::npos);
	ASSERT_NE(ViewHeight, std::string::npos);
	ASSERT_NE(RowHeight, std::string::npos);
	ASSERT_NE(ItemsPerRow, std::string::npos);
	ASSERT_NE(ListSize, std::string::npos);
	ASSERT_NE(OverscanRows, std::string::npos);
	EXPECT_LT(ScrollOffset, ViewHeight);
	EXPECT_LT(ViewHeight, RowHeight);
	EXPECT_LT(RowHeight, ItemsPerRow);
	EXPECT_LT(ItemsPerRow, ListSize);
	EXPECT_LT(ListSize, OverscanRows);
	EXPECT_NE(JobsSource.find("constexpr int TeeSkinListItemsPerRow = 4;"), std::string::npos);
}

TEST(Skins, TeeSkinListSortModeKeepsFavoritesPinnedThenUsesOfficialDate)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/skins.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string ConfigSource = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string MenusI18nSource = ReadTestSourceFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");
	const size_t ComparePos = Source.find("bool CSkins::CSkinListEntry::operator<");
	const size_t ScanJobPos = Source.find("int CSkins::CSkinDirectoryScanJob::ScanCallback");
	const size_t RenderTeePos = MenusSource.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	const size_t RenderTeeEnd = MenusSource.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(ComparePos, std::string::npos);
	ASSERT_NE(ScanJobPos, std::string::npos);
	ASSERT_NE(RenderTeePos, std::string::npos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string CompareBody = Source.substr(ComparePos, 1200);
	const std::string ScanJobBody = Source.substr(ScanJobPos, 900);
	const std::string RenderTeeBody = MenusSource.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinSortMode, qm_skin_sort_mode, 0, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinShowMetadata, qm_skin_show_metadata, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Header.find("time_t m_LastModified"), std::string::npos);
	EXPECT_NE(Header.find("time_t LastModified() const"), std::string::npos);
	EXPECT_NE(Header.find("int m_OfficialReleaseDate"), std::string::npos);
	EXPECT_NE(Header.find("int OfficialReleaseDate() const"), std::string::npos);
	EXPECT_NE(Header.find("char m_aOfficialCreator"), std::string::npos);
	EXPECT_NE(Header.find("const char *OfficialCreator() const"), std::string::npos);
	EXPECT_NE(Source.find("ListDirectoryInfo(IStorage::TYPE_ALL, pDirectory"), std::string::npos);
	EXPECT_NE(Source.find("ScanDirectory(\"skins\", CSkinContainer::EType::LOCAL);"), std::string::npos);
	EXPECT_NE(Source.find("ScanDirectory(\"downloadedskins\", CSkinContainer::EType::DOWNLOAD);"), std::string::npos);
	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_URL = \"https://ddnet.org/skins/skin/skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_CACHE_PATH = \"downloadedskins/official_skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("QueueOfficialSkinIndexRequest();"), std::string::npos);
	EXPECT_NE(Source.find("LoadOfficialSkinIndexCache();"), std::string::npos);
	EXPECT_NE(Source.find("json_string_get(json_object_get(pEntry, \"date\"))"), std::string::npos);
	EXPECT_NE(Source.find("json_string_get(json_object_get(pEntry, \"creator\"))"), std::string::npos);
	EXPECT_NE(ScanJobBody.find("pInfo->m_TimeModified"), std::string::npos);
	EXPECT_NE(CompareBody.find("g_Config.m_QmSkinSortMode"), std::string::npos);
	EXPECT_NE(CompareBody.find("OfficialReleaseDate()"), std::string::npos);
	EXPECT_NE(CompareBody.find("LastModified()"), std::string::npos);
	EXPECT_NE(CompareBody.find("OfficialReleaseDate > OtherOfficialReleaseDate"), std::string::npos);
	EXPECT_NE(CompareBody.find("LastModified() > Other.m_pSkinContainer->LastModified()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinSortMode"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Skin sort\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Name\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Time\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("SortModeControl = NextPrefixRow();"), std::string::npos);
	const size_t SortChangePos = RenderTeeBody.find("if(g_Config.m_QmSkinSortMode != SkinSortModeNew)");
	ASSERT_NE(SortChangePos, std::string::npos);
	const size_t SortChangeEnd = RenderTeeBody.find("Button = NextPrefixRow();", SortChangePos);
	ASSERT_NE(SortChangeEnd, std::string::npos);
	const std::string SortChangeBody = RenderTeeBody.substr(SortChangePos, SortChangeEnd - SortChangePos);
	EXPECT_NE(SortChangeBody.find("GameClient()->m_Skins.RebuildSkinListPlan();"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("GameClient()->m_Skins.Refresh"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("ClearSettingsTeeListPreviewCache"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("SkinList(m_Dummy).ForceRefresh"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Show skin date and author\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("g_Config.m_QmSkinSortMode == 1 && g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinShowMetadata != 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("pSkinContainer->OfficialCreator()"), std::string::npos);
	const size_t TimeTranslationPos = MenusI18nSource.find("key = \"Time\"");
	ASSERT_NE(TimeTranslationPos, std::string::npos);
	const size_t TimeTranslationEnd = MenusI18nSource.find("[[message]]", TimeTranslationPos + 1);
	const std::string TimeTranslationBody = MenusI18nSource.substr(TimeTranslationPos, TimeTranslationEnd - TimeTranslationPos);
	EXPECT_NE(TimeTranslationBody.find("simplified_chinese = \"时间\""), std::string::npos);
	EXPECT_EQ(TimeTranslationBody.find("simplified_chinese = \"用时\""), std::string::npos);
	const size_t ToolbarCommentPos = RenderTeeBody.find("// Layout bottom controls and use remainder for skin selector");
	ASSERT_NE(ToolbarCommentPos, std::string::npos);
	const std::string BottomToolbarBody = RenderTeeBody.substr(ToolbarCommentPos);
	EXPECT_EQ(BottomToolbarBody.find("SkinSortModeControlWidth"), std::string::npos);
	EXPECT_NE(BottomToolbarBody.find("const float SkinControlLabelPadding = TeeMetrics.m_LineSpacing * 3.0f;"), std::string::npos);
	const size_t RefreshRightPos = BottomToolbarBody.find("ControlsArea.VSplitRight(SkinRefreshButtonWidth, &ControlsArea, &RefreshButton);");
	const size_t EditButtonPos = BottomToolbarBody.find("SplitSkinToolbarLeft(ControlsArea, EditTextureButtonWidth, &EditTextureButton);");
	ASSERT_NE(RefreshRightPos, std::string::npos);
	ASSERT_NE(EditButtonPos, std::string::npos);
	EXPECT_LT(RefreshRightPos, EditButtonPos);
	const size_t FavoritePos = CompareBody.find("m_Favorite");
	const size_t SortModePos = CompareBody.find("g_Config.m_QmSkinSortMode");
	const size_t ModifiedPos = CompareBody.find("LastModified()");
	ASSERT_NE(FavoritePos, std::string::npos);
	ASSERT_NE(SortModePos, std::string::npos);
	ASSERT_NE(ModifiedPos, std::string::npos);
	EXPECT_LT(FavoritePos, SortModePos);
	EXPECT_LT(SortModePos, ModifiedPos);
	EXPECT_NE(CompareBody.find("if(g_Config.m_QmSkinSortMode == 1)"), std::string::npos);
	EXPECT_NE(CompareBody.find("if(m_Favorite && !Other.m_Favorite)"), std::string::npos);
}

TEST(Skins, OfficialSkinIndexCreatesMissingDownloadEntries)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t ApplyIndexPos = Source.find("bool CSkins::ApplyOfficialSkinIndexJson(const char *pJson, size_t JsonSize)");
	ASSERT_NE(ApplyIndexPos, std::string::npos);
	const size_t ApplyIndexEnd = Source.find("void CSkins::ProcessSkinListPlanJob()", ApplyIndexPos);
	ASSERT_NE(ApplyIndexEnd, std::string::npos);
	const std::string ApplyIndexBody = Source.substr(ApplyIndexPos, ApplyIndexEnd - ApplyIndexPos);

	EXPECT_NE(ApplyIndexBody.find("CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::DOWNLOAD, IStorage::TYPE_SAVE);"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("pSkinContainer->SetState(pSkinContainer->DetermineInitialState());"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("ExistingSkin = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("SetOfficialReleaseDate(ReleaseDate)"), std::string::npos);
	EXPECT_EQ(ApplyIndexBody.find("if(ExistingSkin == m_Skins.end())\n\t\t\t\tcontinue;"), std::string::npos);
}

TEST(Skins, OfficialSkinReleaseDateParserAcceptsIsoDateOnly)
{
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("2026-06-14"), 20260614);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("2015-01-29"), 20150129);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey(nullptr), 0);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("20260614"), 0);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("2026/06/14"), 0);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("2026-6-14"), 0);
	EXPECT_EQ(CSkins::ParseOfficialSkinReleaseDateKey("2026-0x-14"), 0);
}

TEST(Skins, TeeSkinListStableIdleAvoidsFullBackgroundScan)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(Source.find("bool m_BackgroundRequestScanComplete = false;"), std::string::npos);
	EXPECT_NE(Source.find("uint64_t m_BackgroundRequestScanRevision = std::numeric_limits<uint64_t>::max();"), std::string::npos);
	EXPECT_NE(Source.find("uint64_t m_FullListSettledRevision = std::numeric_limits<uint64_t>::max();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("VisibleSourceSettled && BackgroundRequestBudget > 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("!gs_TeeSettingsPageState.m_BackgroundRequestScanComplete"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("SkinStatsBeforeBackgroundRequest.m_NumUnloaded > 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = true;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=tee_skin_background_scan"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("items_total=%d items_scanned=%d items_skipped_visible=%d requests_issued=%d complete=%d budget=%d dur_ms=%.3f block_reason=%s"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=tee_skin_list_prescan"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool NeedFullListSourceState = g_Config.m_QmSettingsPrewarm != 0;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_SelectedIndexRevision != SkinList.Revision()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool NeedFullListSettledScan = NeedFullListSourceState && gs_TeeSettingsPageState.m_FullListSettledRevision != SkinList.Revision();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("if(NeedFullListSettledScan)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("m_BackgroundRequestScanRevision != SkinList.Revision()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_SelectedIndex = NewSelected;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("visual_ready_count=%d"), std::string::npos);
}

TEST(Skins, TeeSkinListSeparatesVisualReadyFromSourceSettled)
{
	std::ifstream JobsFile(TestSourcePath("src/game/client/components/settings_resource_jobs.cpp"));
	ASSERT_TRUE(JobsFile.good());
	std::stringstream JobsBuffer;
	JobsBuffer << JobsFile.rdbuf();
	const std::string JobsSource = JobsBuffer.str();

	EXPECT_NE(JobsSource.find("bool SettingsSkinListEntryVisualReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady)"), std::string::npos);
	EXPECT_NE(JobsSource.find("bool SettingsSkinListEntrySourceSettled(bool SourceReady, bool TerminalFailure)"), std::string::npos);
	EXPECT_NE(JobsSource.find("return SourceReady || TerminalFailure || PreviewCacheReady;"), std::string::npos);
	EXPECT_NE(JobsSource.find("return SourceReady || TerminalFailure;"), std::string::npos);

	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();
	const size_t RenderTeePos = MenusSource.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = MenusSource.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = MenusSource.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("VisibleVisualReadyCount"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("VisibleSourceSettledCount"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool VisibleSourceSettled = VisibleSourceSettledCount == (int)vVisibleSkinIndices.size();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("m_SettingsHighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("FrameContext.m_HighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("m_SettingsHighPrioritySettled = VisibleSettled;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("FrameContext.m_HighPrioritySettled = VisibleSettled;"), std::string::npos);
}

TEST(Skins, TeeStartLoadingFallbackSweepIsBoundedAndLogged)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string HeaderSource = HeaderBuffer.str();
	EXPECT_NE(HeaderSource.find("m_FallbackSweepScanned"), std::string::npos);
	EXPECT_NE(HeaderSource.find("m_FallbackSweepStarted"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsSourceFallbackSweepCursor"), std::string::npos);

	std::ifstream SourceFile(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();
	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(Source.find("event=skin_start_loading_fallback_sweep"), std::string::npos);
	EXPECT_NE(Source.find("items_total=%d items_scanned=%d items_started=%d items_skipped=%d invoked=%d dur_ms=%.3f reason=%s"), std::string::npos);
	EXPECT_NE(StartLoading.find("LogSettingsSkinStartLoadingFallbackSweepEvent("), std::string::npos);
	EXPECT_NE(StartLoading.find("disabled_explicit_queues"), std::string::npos);
	EXPECT_EQ(StartLoading.find("std::advance("), std::string::npos);
	EXPECT_EQ(StartLoading.find("for(auto &[_, pSkinContainer] : m_Skins)\n\t{"), std::string::npos);
}

TEST(Skins, SettingsAssetsListVirtualizationKeepsTotalListLength)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t LocalListPos = Source.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalListPos, std::string::npos);
	const size_t WorkshopListPos = Source.find("if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory) && WorkshopHudView.h > 0.0f)", LocalListPos);
	ASSERT_NE(WorkshopListPos, std::string::npos);
	const std::string LocalListBody = Source.substr(LocalListPos, WorkshopListPos - LocalListPos);
	const size_t WorkshopListEnd = Source.find("if(ui_widget::InputField(AssetsSearchCtx", WorkshopListPos);
	ASSERT_NE(WorkshopListEnd, std::string::npos);
	const std::string WorkshopListBody = Source.substr(WorkshopListPos, WorkshopListEnd - WorkshopListPos);

	EXPECT_NE(LocalListBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(LocalListBody.find("OldSelected = SelectedCustomAssetIndex(s_CurCustomTab, SearchListSize);"), std::string::npos);
	EXPECT_NE(LocalListBody.find("s_ListBox.SkipItems("), std::string::npos);
	EXPECT_NE(LocalListBody.find("assets_local_list_frame"), std::string::npos);
	EXPECT_EQ(LocalListBody.find("for(size_t i = 0; i < SearchListSize; ++i)"), std::string::npos);

	EXPECT_NE(WorkshopListBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("OldCombinedSelected = SelectedCombinedAssetIndex(s_CurCustomTab, vVisibleLocalAssetIndices);"), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("s_WorkshopAssetsListBox.SkipItems("), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("abs(WorkshopVisibleRange.m_FirstItem - PreviousFirstVisibleCombinedIndex)"), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("assets_workshop_list_frame"), std::string::npos);
	EXPECT_EQ(WorkshopListBody.find("for(size_t ListIndex = 0; ListIndex < CombinedCount; ++ListIndex)"), std::string::npos);
}

TEST(Skins, TeePriorityRequestsReclaimBackgroundRequestedBeforeAdmittedBackgroundWork)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ReclaimBackground = Source.find("bool CSkins::ReclaimBackgroundSkinForPriorityRequest");
	ASSERT_NE(ReclaimBackground, std::string::npos);
	const size_t ReclaimBackgroundEnd = Source.find("\n}", ReclaimBackground);
	ASSERT_NE(ReclaimBackgroundEnd, std::string::npos);
	const std::string ReclaimBody = Source.substr(ReclaimBackground, ReclaimBackgroundEnd - ReclaimBackground);

	const size_t BackgroundRequestedPos = ReclaimBody.find("if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)");
	const size_t PendingBranchPos = ReclaimBody.find("if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&");
	ASSERT_NE(BackgroundRequestedPos, std::string::npos);
	ASSERT_NE(PendingBranchPos, std::string::npos);
	EXPECT_LT(BackgroundRequestedPos, PendingBranchPos);
}

TEST(Skins, SettingsResourcePriorityOnlyUpgradesTowardVisible)
{
	EXPECT_TRUE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::PREFETCH, ESettingsResourcePriority::BACKGROUND));
	EXPECT_TRUE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::PREFETCH));
	EXPECT_TRUE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::BACKGROUND));

	EXPECT_FALSE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::BACKGROUND, ESettingsResourcePriority::PREFETCH));
	EXPECT_FALSE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::PREFETCH, ESettingsResourcePriority::VISIBLE));
	EXPECT_FALSE(CSkins::CSkinContainer::SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::VISIBLE));
}

TEST(Skins, PriorityRequestsCanReclaimBackgroundLoadingSlots)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ReclaimBackground = Source.find("bool CSkins::ReclaimBackgroundSkinForPriorityRequest");
	ASSERT_NE(ReclaimBackground, std::string::npos);
	const size_t ReclaimBackgroundEnd = Source.find("\n}", ReclaimBackground);
	ASSERT_NE(ReclaimBackgroundEnd, std::string::npos);
	const std::string ReclaimBody = Source.substr(ReclaimBackground, ReclaimBackgroundEnd - ReclaimBackground);
	EXPECT_NE(ReclaimBody.find("CSkinContainer::EState::LOADING"), std::string::npos);
	EXPECT_NE(ReclaimBody.find("m_pLoadJob->Abort()"), std::string::npos);

	const size_t StartLoadJob = Source.find("auto StartLoadJob = [&]");
	ASSERT_NE(StartLoadJob, std::string::npos);
	const size_t StartLoadJobEnd = Source.find("\n\t};", StartLoadJob);
	ASSERT_NE(StartLoadJobEnd, std::string::npos);
	const std::string StartLoadBody = Source.substr(StartLoadJob, StartLoadJobEnd - StartLoadJob);
	EXPECT_NE(StartLoadBody.find("ReclaimBackgroundSkinForPriorityRequest"), std::string::npos);
	EXPECT_NE(StartLoadBody.find("Stats = LoadingStats();"), std::string::npos);
	EXPECT_EQ(StartLoadBody.find("Priority != ESettingsResourcePriority::BACKGROUND"), std::string::npos);
}

TEST(Skins, PrioritizedLoadQueueKeepsOriginalRequestPriority)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("StartLoadJob(It->second.get(), It->second->m_LoadPriority)"), std::string::npos);
	EXPECT_EQ(StartLoading.find("StartLoadJob(It->second.get(), ESettingsResourcePriority::VISIBLE)"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumPending + Stats.m_NumLoading"), std::string::npos);
	EXPECT_EQ(StartLoading.find("Stats.m_NumPending + Stats.m_NumLoaded + Stats.m_NumLoading"), std::string::npos);
}

TEST(Skins, TeeSettingsScrollBudgetFeedsFinalizeAndUploadLimits)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t FinalizeBudget = Source.find("return pGameClient->m_Skins.SettingsFinalizeBudgetForFrame();");
	EXPECT_NE(FinalizeBudget, std::string::npos);

	const size_t UploadBudget = Source.find("return pGameClient->m_Skins.SettingsGpuUploadFrameBudgetForFrame();");
	EXPECT_NE(UploadBudget, std::string::npos);

	const size_t ImmediateScrollContext = Source.find("return SettingsBuildFrameContext(PersistentContext.m_ScrollActive, ImmediateScrollInput, PersistentContext.m_PostScrollRecoveryFrames);");
	EXPECT_NE(ImmediateScrollContext, std::string::npos);

	const size_t ScrollRegionMouseDown = Source.find("(pGameClient->Input()->KeyPress(KEY_MOUSE_1) && pUi->HotScrollRegion() != nullptr)");
	EXPECT_NE(ScrollRegionMouseDown, std::string::npos);
}

TEST(Skins, GpuUploadLimiterResetsBeforeSkinUpdateConsumesBudget)
{
	std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t OnUpdatePos = Source.find("void CGameClient::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnRenderPos = Source.find("void CGameClient::OnRender()");
	ASSERT_NE(OnRenderPos, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, OnRenderPos - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("m_Skins.PrepareSettingsThroughputForFrame();"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_GpuUploadLimiter.OnFrameStart(FrameGpuUploadLimit);"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("const int FrameGpuUploadLimit = m_Menus.SettingsGpuUploadLimitForFrame(TeeSettingsActive, AssetsSettingsActive, m_Skins.SettingsGpuUploadLimiterUnitsForFrame());"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_Menus.ResetSettingsFrameBudgetForFrame(TeeSettingsActive, AssetsSettingsActive, FrameSkinUploadBudget);"), std::string::npos);

	const size_t OnRenderEnd = Source.find("const ColorRGBA ClearColor", OnRenderPos);
	ASSERT_NE(OnRenderEnd, std::string::npos);
	const std::string OnRenderPreamble = Source.substr(OnRenderPos, OnRenderEnd - OnRenderPos);
	EXPECT_EQ(OnRenderPreamble.find("m_GpuUploadLimiter.OnFrameStart();"), std::string::npos);
}

TEST(Skins, SettingsWarmupBypassesPeriodicSkinUpdateThrottle)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t WarmupPos = Source.find("void CSkins::UpdateForSettingsWarmup()");
	ASSERT_NE(WarmupPos, std::string::npos);
	const size_t PreparePos = Source.find("void CSkins::PrepareSettingsThroughputForFrame()", WarmupPos);
	ASSERT_NE(PreparePos, std::string::npos);
	const std::string WarmupBody = Source.substr(WarmupPos, PreparePos - WarmupPos);

	EXPECT_NE(WarmupBody.find("m_ContainerUpdateTime.reset();"), std::string::npos);
	EXPECT_NE(WarmupBody.find("OnUpdate();"), std::string::npos);
}

TEST(Skins, ManagedTeeRenderInfoSkipsInvalidSixupSkinNames)
{
	std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("NormalizeSixupSkinName(SkinDescriptor.m_aSkinName"), std::string::npos);
	EXPECT_NE(Source.find("CSkin::IsValidName(pManagedTeeRenderInfo->m_SkinDescriptor.m_aSkinName)"), std::string::npos);
	EXPECT_NE(Source.find("m_Skins.FindOrNullptr(CSkin::IsValidName(SkinDescriptor.m_aSkinName) ? SkinDescriptor.m_aSkinName : \"default\")"), std::string::npos);
}

TEST(Skins, ManagedTeeRenderInfoDefersUnloadedSkinInsteadOfApplyingFallback)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);

	EXPECT_NE(RefreshSkinBody.find("pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(false);"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("m_Skins.FindOrNullptr(CSkin::IsValidName(SkinDescriptor.m_aSkinName) ? SkinDescriptor.m_aSkinName : \"default\")"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("bool SixReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SixReady = TeeInfo.SixDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SevenReady = TeeInfo.SevenDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("DescriptorRenderInfoReady = SixReady || SevenReady;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(DescriptorRenderInfoReady);"), std::string::npos);
	EXPECT_EQ(RefreshSkinBody.find("TeeInfo.Apply(m_Skins.Find("), std::string::npos);
}

TEST(Skins, SkinRefreshDoesNotFloodPendingQueueBeforeVisibleRequests)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t RefreshPos = Source.find("void CSkins::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)");
	ASSERT_NE(RefreshPos, std::string::npos);
	const size_t LoadingStatsPos = Source.find("CSkins::CSkinLoadingStats CSkins::LoadingStats() const", RefreshPos);
	ASSERT_NE(LoadingStatsPos, std::string::npos);
	const std::string RefreshBody = Source.substr(RefreshPos, LoadingStatsPos - RefreshPos);

	EXPECT_NE(RefreshBody.find("if(pSkinContainer->m_pLoadJob)"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin->m_ColorableSkin.Unload(Graphics());"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin.reset();"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING, ESettingsResourcePriority::VISIBLE);"), std::string::npos);
	EXPECT_NE(RefreshBody.find("if(pSkinContainer->m_State != CSkinContainer::EState::LOADED)"), std::string::npos);
	EXPECT_NE(RefreshBody.find("pSkinContainer->SetState(pSkinContainer->DetermineInitialState());"), std::string::npos);
	EXPECT_NE(RefreshBody.find("LoadSkinDirect(\"default\");"), std::string::npos);
}

TEST(Skins, SkinTransitionDefersKeyUntilDescriptorRenderInfoIsReady)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(Header.find("bool DescriptorRenderInfoReady() const"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("m_RenderInfo.Valid()"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady"), UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"));
}

TEST(Skins, SkinTransitionUsesDefaultKeyWhenInitialDescriptorIsNotReady)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(UpdateRenderInfoBody.find("CSkinDescriptor RenderSkinDescriptor = SkinDescriptor;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && m_RenderInfo.Valid())"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("else if(!DescriptorRenderInfoReady)"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const float OriginalSize = NewRenderInfo.m_Size;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("BuildDefaultSkinDescriptor(RenderSkinDescriptor);"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo))\n\t\t\tNewRenderInfo.Reset();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"), std::string::npos);
	EXPECT_EQ(UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, SkinDescriptor);"), std::string::npos);
}

TEST(Skins, DefaultFallbackNeverAppliesTheUntexturedPlaceholder)
{
	EXPECT_FALSE(CTeeRenderInfo::IsDrawableTextureState(false, false));
	EXPECT_FALSE(CTeeRenderInfo::IsDrawableTextureState(true, true));
	EXPECT_TRUE(CTeeRenderInfo::IsDrawableTextureState(true, false));
	EXPECT_FALSE(CTeeRenderInfo::AreTextureVariantsDrawableState(true, false, false, false));
	EXPECT_FALSE(CTeeRenderInfo::AreTextureVariantsDrawableState(true, false, true, true));
	EXPECT_FALSE(CTeeRenderInfo::AreTextureVariantsDrawableState(false, false, true, false));
	EXPECT_TRUE(CTeeRenderInfo::AreTextureVariantsDrawableState(true, false, true, false));

	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t ApplyDefaultPos = GameClientSource.find("bool ApplyDefaultSkin(CGameClient *pGameClient, CTeeRenderInfo &Info)");
	ASSERT_NE(ApplyDefaultPos, std::string::npos);
	const size_t CopyColorsPos = GameClientSource.find("void CopySkinColorsOnly", ApplyDefaultPos);
	ASSERT_NE(CopyColorsPos, std::string::npos);
	const std::string ApplyDefaultBody = GameClientSource.substr(ApplyDefaultPos, CopyColorsPos - ApplyDefaultPos);
	EXPECT_NE(ApplyDefaultBody.find("m_Skins.FindOrNullptr(\"default\")"), std::string::npos);
	EXPECT_EQ(ApplyDefaultBody.find("m_Skins.Find(\"default\")"), std::string::npos);
	EXPECT_NE(ApplyDefaultBody.find("return Info.SixDescriptorReady() || Info.SevenDescriptorReady();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo))\n\t\t\tNewRenderInfo.Reset();"), std::string::npos);

	const std::string RenderSource = ReadTestSourceFile("src/game/client/render.cpp");
	const size_t RenderTeePos = RenderSource.find("void CRenderTools::RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const std::string RenderTeeBody = FunctionBody(RenderSource, "void CRenderTools::RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale");
	ASSERT_FALSE(RenderTeeBody.empty());
	EXPECT_NE(RenderTeeBody.find("const bool SixupBodyValid"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool SixBodyValid"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("CTeeRenderInfo::IsDrawableTexture"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("else if(SixBodyValid)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("else\n\t\treturn;"), std::string::npos);
	EXPECT_LT(RenderTeeBody.find("else if(SixBodyValid)"), RenderTeeBody.find("Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);"));
	const std::string RenderHeader = ReadTestSourceFile("src/game/client/render.h");
	EXPECT_NE(RenderHeader.find("return IsValid && !IsNullTexture;"), std::string::npos);
	const std::string RenderTee7Body = FunctionBody(RenderSource, "void CRenderTools::RenderTee7(");
	ASSERT_FALSE(RenderTee7Body.empty());
	EXPECT_NE(RenderTee7Body.find("IsDrawableTexture(EyesTexture)"), std::string::npos);
	const std::string RenderTee6Body = FunctionBody(RenderSource, "void CRenderTools::RenderTee6(");
	ASSERT_FALSE(RenderTee6Body.empty());
	EXPECT_NE(RenderTee6Body.find("if(!CTeeRenderInfo::IsDrawableTexture(*pFeetTexture))"), std::string::npos);
	EXPECT_NE(RenderTee6Body.find("m_Skins.FindOrNullptr(g_Config.m_TcWhiteFeetSkin)"), std::string::npos);
	EXPECT_EQ(RenderTee6Body.find("m_Skins.Find(g_Config.m_TcWhiteFeetSkin)"), std::string::npos);
}

TEST(Skins, DefaultFallbackUsesSixupDefaultSkinColorsInsteadOfColorlessStandardParts)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string SixupDefaultBody = FunctionBody(Source, "void ApplySixupDefaultSkin(");
	ASSERT_FALSE(SixupDefaultBody.empty());
	// 0.7 默认外观来自默认皮肤（data/skins7/default.json）：standard 部件 + 自身配色。
	// 只套用无色 standard 部件会让回退 Tee 渲染成白 Tee，加入服务器与主播模式都会看到。
	EXPECT_NE(SixupDefaultBody.find("m_Skins7.FindSkin(\"default\", false)"), std::string::npos);
	EXPECT_NE(SixupDefaultBody.find("pDefaultSkin->m_apParts[Part]"), std::string::npos);
	EXPECT_NE(SixupDefaultBody.find("pDefaultSkin->m_aUseCustomColors[Part] != 0"), std::string::npos);
	EXPECT_NE(SixupDefaultBody.find("(int)pDefaultSkin->m_aPartColors[Part]"), std::string::npos);
	EXPECT_NE(SixupDefaultBody.find("m_Skins7.ApplyColorTo(Info.m_aSixup[Dummy]"), std::string::npos);

	const size_t ApplyDefaultPos = Source.find("bool ApplyDefaultSkin(CGameClient *pGameClient, CTeeRenderInfo &Info)");
	ASSERT_NE(ApplyDefaultPos, std::string::npos);
	const size_t CopyColorsPos = Source.find("void CopySkinColorsOnly", ApplyDefaultPos);
	ASSERT_NE(CopyColorsPos, std::string::npos);
	const std::string ApplyDefaultBody = Source.substr(ApplyDefaultPos, CopyColorsPos - ApplyDefaultPos);
	EXPECT_NE(ApplyDefaultBody.find("ApplySixupDefaultSkin(pGameClient, Info);"), std::string::npos);
}

TEST(Skins, StreamerManagedTeeRenderInfoUsesDefaultSkinInsteadOfWhiteTee)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string Body = FunctionBody(Source, "std::shared_ptr<CManagedTeeRenderInfo> CGameClient::CreateManagedTeeRenderInfo(const CClientData &Client)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("if(!ApplyDefaultSkin(this, TeeRenderInfo))"), std::string::npos);
	EXPECT_NE(Body.find("TeeRenderInfo.Reset();"), std::string::npos);
	EXPECT_NE(Body.find("TeeRenderInfo.m_Size = Client.m_RenderInfo.m_Size;"), std::string::npos);
	EXPECT_NE(Body.find("BuildDefaultSkinDescriptor(SkinDescriptor);"), std::string::npos);
}

TEST(Skins, StreamerFallbackCancelsAnyRealSkinTransition)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition");
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const size_t ProgressPos = Source.find("float CGameClient::CClientData::SkinChangeTransitionProgress", UpdateTransitionPos);
	ASSERT_NE(ProgressPos, std::string::npos);
	const std::string Body = Source.substr(UpdateTransitionPos, ProgressPos - UpdateTransitionPos);
	const size_t StreamerGuard = Body.find("if(m_pGameClient != nullptr && m_pGameClient->ShouldHideStreamerSkin(m_ClientId))");
	const size_t ResolveAction = Body.find("ResolveSkinChangeTransitionAction(");
	ASSERT_NE(StreamerGuard, std::string::npos);
	ASSERT_NE(ResolveAction, std::string::npos);
	EXPECT_LT(StreamerGuard, ResolveAction);
	EXPECT_NE(Body.find("m_SkinTransitionPreviousRenderInfo.Reset();", StreamerGuard), std::string::npos);
	EXPECT_NE(Body.find("m_SkinTransitionStart.reset();", StreamerGuard), std::string::npos);
}

TEST(Skins, StreamerSkinPrivacyStateChangesRefreshActiveManagedClientsImmediately)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t OnUpdatePos = Source.find("void CGameClient::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnUpdateEnd = Source.find("int CGameClient::RenderThrottleRefreshRate() const", OnUpdatePos);
	ASSERT_NE(OnUpdateEnd, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, OnUpdateEnd - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("RefreshStreamerSkinPrivacyAfterStateChange();"), std::string::npos);

	const size_t RefreshPos = Source.find("void CGameClient::RefreshStreamerSkinPrivacyAfterStateChange()");
	ASSERT_NE(RefreshPos, std::string::npos);
	const size_t RefreshEnd = Source.find("int CGameClient::RenderThrottleRefreshRate() const", RefreshPos);
	ASSERT_NE(RefreshEnd, std::string::npos);
	const std::string RefreshBody = Source.substr(RefreshPos, RefreshEnd - RefreshPos);
	EXPECT_NE(RefreshBody.find("m_LastStreamerHideSkins == g_Config.m_QmStreamerHideSkins"), std::string::npos);
	EXPECT_NE(RefreshBody.find("m_LastStreamerFriendsRevision == FriendsRevision"), std::string::npos);
	EXPECT_NE(RefreshBody.find("m_aLastStreamerLocalIds[0] == m_aLocalIds[0]"), std::string::npos);
	EXPECT_NE(RefreshBody.find("Friends()->IsFriend(ClientData.m_aName, ClientData.m_aClan, true)"), std::string::npos);
	EXPECT_NE(RefreshBody.find("ClientData.UpdateRenderInfo();"), std::string::npos);
}

TEST(Skins, SkinTransitionKeepsPreviousSkinBaseWhileDescriptorIsPending)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && m_RenderInfo.Valid())"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), std::string::npos);
	EXPECT_EQ(UpdateRenderInfoBody.find("return;\n\t\t}"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && m_RenderInfo.Valid())"), UpdateRenderInfoBody.find("// force team colors"));
	EXPECT_LT(UpdateRenderInfoBody.find("// force team colors"), UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"));
}

TEST(Skins, SkinTransitionKeepsPendingSkinColorsWhileReusingPreviousSkinBase)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(Source.find("void CopySkinColorsOnly(CTeeRenderInfo &Target, const CTeeRenderInfo &Source)"), std::string::npos);
	EXPECT_NE(Source.find("Target.m_CustomColoredSkin = Source.m_CustomColoredSkin;"), std::string::npos);
	EXPECT_NE(Source.find("Target.m_aSixup[Dummy].m_aUseCustomColors[Part] = Source.m_aSixup[Dummy].m_aUseCustomColors[Part];"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const CTeeRenderInfo SkinProperties = NewRenderInfo;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("CopySkinColorsOnly(NewRenderInfo, SkinProperties);"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), UpdateRenderInfoBody.find("CopySkinColorsOnly(NewRenderInfo, SkinProperties);"));
}

TEST(Skins, ManagedTeeRenderInfoAllowsTeeworldsCompatibilitySkinWithoutSixBody)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);

	EXPECT_NE(RefreshSkinBody.find("bool SixReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("bool SevenReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SixReady = TeeInfo.SixDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SevenReady = TeeInfo.SevenDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("DescriptorRenderInfoReady = SixReady || SevenReady;"), std::string::npos);
	EXPECT_EQ(RefreshSkinBody.find("DescriptorRenderInfoReady = DescriptorRenderInfoReady && SevenReady;"), std::string::npos);
}

TEST(Skins, ManagedTeeReadinessCoversAllSelectableTextureVariantsAndDummies)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	EXPECT_NE(Header.find("return AreTextureVariantsDrawable(m_OriginalRenderSkin.m_Body, m_ColorableRenderSkin.m_Body);"), std::string::npos);
	EXPECT_NE(Header.find("std::all_of(std::begin(m_aSixup), std::end(m_aSixup)"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_BODY"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_HANDS"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_FEET"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_EYES"), std::string::npos);
	EXPECT_NE(Header.find("Sixup.RequiredPartTextureVariantsDrawable()"), std::string::npos);
}

TEST(Skins, HandRenderingNeverBindsNullOrIncompleteTextureSets)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/players.cpp");
	const size_t RenderHandPos = Source.find("void CPlayers::RenderHand(");
	const size_t RenderHand7Pos = Source.find("void CPlayers::RenderHand7(", RenderHandPos);
	ASSERT_NE(RenderHandPos, std::string::npos);
	ASSERT_NE(RenderHand7Pos, std::string::npos);
	const std::string RenderHandBody = Source.substr(RenderHandPos, RenderHand7Pos - RenderHandPos);
	EXPECT_NE(RenderHandBody.find("CTeeRenderInfo::IsDrawableTexture(pInfo->m_aSixup"), std::string::npos);
	EXPECT_NE(RenderHandBody.find("CTeeRenderInfo::IsDrawableTexture(SkinTextures.m_HandsOutline)"), std::string::npos);
	EXPECT_NE(RenderHandBody.find("CTeeRenderInfo::IsDrawableTexture(SkinTextures.m_Hands)"), std::string::npos);
	EXPECT_EQ(RenderHandBody.find("PartTexture(protocol7::SKINPART_HANDS).IsValid()"), std::string::npos);
}

TEST(Skins, SixupCompletedJobsAreConsumedEveryUpdate)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/skins7.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins7.cpp");
	EXPECT_NE(Header.find("void OnUpdate() override;"), std::string::npos);
	const size_t OnUpdatePos = Source.find("void CSkins7::OnUpdate()");
	const size_t InitPlaceholderPos = Source.find("void CSkins7::InitPlaceholderSkinParts()", OnUpdatePos);
	ASSERT_NE(OnUpdatePos, std::string::npos);
	ASSERT_NE(InitPlaceholderPos, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, InitPlaceholderPos - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("ProcessCompletedJobs();"), std::string::npos);
	const size_t ProcessPos = Source.find("void CSkins7::ProcessCompletedJobs()");
	const size_t ScanDataPos = Source.find("class CSkinScanData", ProcessPos);
	ASSERT_NE(ProcessPos, std::string::npos);
	ASSERT_NE(ScanDataPos, std::string::npos);
	const std::string ProcessBody = Source.substr(ProcessPos, ScanDataPos - ProcessPos);
	EXPECT_NE(ProcessBody.find("GpuUploadLimiter()->CanUpload(2)"), std::string::npos);
	EXPECT_NE(ProcessBody.find("Part.m_OriginalTexture = Graphics()->LoadTextureRaw"), std::string::npos);
	EXPECT_NE(ProcessBody.find("Part.m_ColorableTexture = Graphics()->LoadTextureRawMove"), std::string::npos);
	const size_t FirstUploadCount = ProcessBody.find("GpuUploadLimiter()->OnUploaded();");
	ASSERT_NE(FirstUploadCount, std::string::npos);
	EXPECT_NE(ProcessBody.find("GpuUploadLimiter()->OnUploaded();", FirstUploadCount + 1), std::string::npos);
	EXPECT_LT(ProcessBody.find("Iter = m_PendingSkinPartJobs.erase(Iter);"), ProcessBody.find("m_SkinLoadedCallback();"));
	EXPECT_NE(Source.find("void CSkins7::RebuildSkins()"), std::string::npos);
	EXPECT_NE(Source.find("RebuildSkins();\n\t\tm_Loading = false;"), std::string::npos);
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	EXPECT_NE(GameClientSource.find("const auto ProgressCallback = [this, SkinStartLoadTime]()"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_Skins7.Refresh([this, ProgressCallback]()"), std::string::npos);
	EXPECT_NE(GameClientSource.find("RefreshSkin(pManagedTeeRenderInfo);"), std::string::npos);
}

TEST(Skins, ManagedTeeRefreshClearsTextureBranchesMissingFromDescriptor)
{
	CTeeRenderInfo Info;
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SIX);
	EXPECT_FALSE(Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY]);

	Info.m_SkinMetrics.m_Body.m_Width = 42;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SEVEN);
	EXPECT_EQ(Info.m_SkinMetrics.m_Body.m_Width, std::numeric_limits<int>::lowest());

	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Info.m_SkinMetrics.m_Body.m_Width = 42;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SIX | CSkinDescriptor::FLAG_SEVEN);
	EXPECT_TRUE(Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY]);
	EXPECT_EQ(Info.m_SkinMetrics.m_Body.m_Width, 42);

	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);
	EXPECT_NE(RefreshSkinBody.find("TeeInfo.ResetMissingDescriptorBranches(SkinDescriptor.m_Flags);"), std::string::npos);
}

TEST(Skins, SkinQueueIntervalUsesMilliseconds)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueInterval, qm_skin_queue_interval, 600, 1, 120000"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueInterval, qm_dummy_skin_queue_interval, 600, 1, 120000"), std::string::npos);
	EXPECT_NE(Config.find("Skin queue switch interval (ms)"), std::string::npos);
	EXPECT_NE(Source.find("static constexpr int SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 1000;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("std::chrono::milliseconds(QueueInterval)"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("QueueInterval * 1000"), std::string::npos);
	EXPECT_NE(UpdateBody.find("const int QueueInterval = maximum(1, SkinQueueIntervalVar(Dummy));"), std::string::npos);
	EXPECT_EQ(Source.find("SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 10;"), std::string::npos);
	EXPECT_EQ(Config.find("皮肤队列切换间隔（0.1 秒）"), std::string::npos);

	const std::string ClientSource = ReadTestSourceFile("src/engine/client/client.cpp");
	EXPECT_EQ(ClientSource.find("g_Config.m_QmSkinQueueInterval *= 10"), std::string::npos);
	EXPECT_EQ(ClientSource.find("g_Config.m_QmDummySkinQueueInterval *= 10"), std::string::npos);
}

TEST(Skins, SkinQueueRotationUsesExplicitEnableSwitchAndBoundedInterval)
{
	// Intent only: rotation has an explicit enable switch and a bounded (1..120000ms)
	// interval, and Update gates on the enable flag. Widget/layout details are
	// intentionally not asserted here.
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueEnabled, qm_skin_queue_enabled, 1, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueEnabled, qm_dummy_skin_queue_enabled, 1, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueLength, qm_skin_queue_length, 20, 0, 1024"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueLength, qm_dummy_skin_queue_length, 20, 0, 1024"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Enable skin queue rotation\")"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_NE(Menus.find("ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 1, 120000, IntervalInputGroup, QueueIntervalOptions);"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueInterval = maximum(QueueIntervalInput.GetInteger(), 1);"), std::string::npos);
	EXPECT_NE(UpdateBody.find("!SkinQueueEnabledVar(Dummy)"), std::string::npos);
	EXPECT_NE(UpdateBody.find("m_aSkinQueueLastUpdate[Dummy].reset();"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("QueueInterval <= 0"), std::string::npos);
}

TEST(Skins, DisabledSkinQueuePreventsMapRotateAutoApply)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t SyncPos = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)");
	ASSERT_NE(SyncPos, std::string::npos);
	const size_t SyncEnd = Source.find("void CSkins::UpdateUnloadSkins", SyncPos);
	ASSERT_NE(SyncEnd, std::string::npos);
	const std::string SyncBody = Source.substr(SyncPos, SyncEnd - SyncPos);

	EXPECT_NE(SyncBody.find("m_aSkinQueueElapsed[Dummy] = 0ns;"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_aSkinQueueLastUpdate[Dummy].reset();"), std::string::npos);
	EXPECT_NE(SyncBody.find("if(SkinQueueEnabledVar(Dummy))"), std::string::npos);
	EXPECT_NE(SyncBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_LT(SyncBody.find("if(SkinQueueEnabledVar(Dummy))"), SyncBody.find("ApplySkinQueueCurrent(Dummy);"));
}

TEST(Skins, SkinQueueCatchUpAppliesOnlyFinalStepOncePerFrame)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(UpdateBody.find("const int64_t StepsElapsed ="), std::string::npos);
	EXPECT_NE(UpdateBody.find("m_aSkinQueueElapsed[Dummy] -= Interval * StepsElapsed;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("QueueIndex = (QueueIndex + (int)(StepsElapsed % QueueActiveCount)) % QueueActiveCount;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("while(m_aSkinQueueElapsed[Dummy] >= Interval)"), std::string::npos);
}

TEST(Skins, TeeRenderInfoValidityIncludesSixupBodyTexture)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	const size_t ValidPos = Header.find("bool Valid() const");
	ASSERT_NE(ValidPos, std::string::npos);
	const size_t ManagedInfoPos = Header.find("class CManagedTeeRenderInfo", ValidPos);
	ASSERT_NE(ManagedInfoPos, std::string::npos);
	const std::string ValidBody = Header.substr(ValidPos, ManagedInfoPos - ValidPos);

	EXPECT_NE(ValidBody.find("m_OriginalRenderSkin.m_Body"), std::string::npos);
	EXPECT_NE(ValidBody.find("IsDrawableTexture("), std::string::npos);
	EXPECT_NE(ValidBody.find("m_aSixup"), std::string::npos);
	EXPECT_NE(ValidBody.find("protocol7::SKINPART_BODY"), std::string::npos);
	EXPECT_NE(ValidBody.find("IsDrawableTexture(Sixup.PartTexture(protocol7::SKINPART_BODY))"), std::string::npos);
}

TEST(Skins, BackgroundRequestedStatusUsesLoadingIndicator)
{
	using EIndicator = CSkins::CSkinContainer::EStatusIndicator;
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::UNLOADED), EIndicator::LOADING);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::BACKGROUND_REQUESTED), EIndicator::LOADING);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::PENDING), EIndicator::LOADING);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::LOADING), EIndicator::LOADING);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::NOT_FOUND), EIndicator::NOT_FOUND);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::ERROR), EIndicator::ERROR);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(EState::LOADED), EIndicator::NONE);
}

TEST(Skins, TeeSkinUploadRequiresWholeSourceTextureBudgetBeforeUpload)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ProcessSkinPos = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer");
	ASSERT_NE(ProcessSkinPos, std::string::npos);
	const size_t LoadingStatsPos = Source.find("CSkins::CSkinLoadingStats CSkins::LoadingStats() const", ProcessSkinPos);
	ASSERT_NE(LoadingStatsPos, std::string::npos);
	const std::string ProcessSkinBody = Source.substr(ProcessSkinPos, LoadingStatsPos - ProcessSkinPos);

	EXPECT_NE(ProcessSkinBody.find("CanUpload(SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)"), std::string::npos);
	EXPECT_NE(ProcessSkinBody.find("UploadBudget.m_MaxGpuUploads = 1;"), std::string::npos);
	EXPECT_NE(ProcessSkinBody.find("SettingsResourceConsumeGpuUpload(UploadBudget, SettingsFrameBudgetOrNull(GameClient()))"), std::string::npos);
	EXPECT_NE(ProcessSkinBody.find("LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), \"gpu_upload_budget\""), std::string::npos);
	EXPECT_NE(ProcessSkinBody.find("LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), \"max_per_frame\""), std::string::npos);
	EXPECT_NE(Source.find("static constexpr int SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS = 24;"), std::string::npos);
	EXPECT_NE(Source.find("event=%s skin=%s artifact=source width=%d height=%d bytes=%d dur_ms=%.3f uploads=%d"), std::string::npos);
	EXPECT_NE(Source.find("LogSettingsSkinSourceStageEvent(\"upload_done\""), std::string::npos);
}

TEST(Skins, TeeSettingsListUsesIdleBackgroundRequestsAfterVisibleSettle)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("RequestLoad(ESettingsResourcePriority::VISIBLE)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("RequestLoad(ESettingsResourcePriority::PREFETCH)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("static std::vector<size_t> s_vVisibleSkinIndices;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("vVisibleSkinIndices.reserve(vSkinList.size())"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("vVisibleSkinIndices.push_back(i);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("for(auto It = vVisibleSkinIndices.rbegin(); It != vVisibleSkinIndices.rend(); ++It)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("std::binary_search(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), BackgroundIndex)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("std::find(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), BackgroundIndex)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("std::find(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), i)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool RequestWindowScrollBlocked = SkinListScrollInteraction || s_SkinListScrollCooldownFrames > 0;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool VisibleSourceSettled = VisibleSourceSettledCount == (int)vVisibleSkinIndices.size();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const int DefaultBackgroundRequestBudget = Throughput.m_BackgroundRequestBudget;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const auto BackgroundBudgetDecision = SettingsSkinBackgroundRequestBudgetDecision({"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const int BackgroundRequestBudget = BackgroundBudgetDecision.m_RequestBudget;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("vSkinList[BackgroundIndex].RequestLoad(ESettingsResourcePriority::BACKGROUND);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);"), std::string::npos);
}

TEST(Skins, TeeSourcePathEmitsRequestAndFrameCapPerfLogs)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("event=source_request skin=%s priority=%s state=%s"), std::string::npos);
	EXPECT_NE(Source.find("event=source_wait skin=%s artifact=source reason=%s remaining_uploads=%d max_uploads=%d"), std::string::npos);
	EXPECT_NE(Source.find("event=frame_cap gpu_cap=%d finalize_cap=%d loading_visible_cap=%d loading_other_cap=%d"), std::string::npos);
	EXPECT_NE(Source.find("LogSettingsSkinSourceRequestEvent(pSkinContainer->Name(), Priority, pSkinContainer->m_State);"), std::string::npos);
	EXPECT_NE(Source.find("LogSettingsSkinFrameCapEvent(GameClient());"), std::string::npos);
}

TEST(Skins, TeeSourcePathCapsActiveLoadingBeforeQueueFuse)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("const bool BackgroundDrainActive = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_BackgroundDrainActive"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int CountFuseLimit = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_CountFuseLimit"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int NormalLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_NormalLoadingWindow"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int VisibleLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleLoadingWindow"), std::string::npos);
	EXPECT_NE(StartLoading.find("m_SettingsSourceAdmissionTelemetry.m_VisibleReserve = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleReserve : 8;"), std::string::npos);
	EXPECT_NE(StartLoading.find("const auto Admission = DetermineAdmission(pSkinContainer, Priority);"), std::string::npos);
	EXPECT_NE(StartLoading.find("const auto SourceAdmission = SettingsSkinSourceAdmissionDecision({"), std::string::npos);
	EXPECT_NE(StartLoading.find("Admission.m_pBlockReason = SettingsSkinSourceAdmissionBlockReasonName(SourceAdmission.m_BlockReason);"), std::string::npos);
	EXPECT_NE(StartLoading.find("const bool CountFuseApplies = Admission.m_CountFuseApplies;"), std::string::npos);
	EXPECT_NE(StartLoading.find("LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), Admission.m_pBlockReason"), std::string::npos);
}

TEST(Skins, TeeBackgroundWindowUsesRealDecodeJobSaturationSignal)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PreparePos = Source.find("void CSkins::PrepareSettingsThroughputForFrame()");
	ASSERT_NE(PreparePos, std::string::npos);
	const size_t NextFunctionPos = Source.find("void CSkins::ClampSkinQueueIndex(int Dummy)", PreparePos);
	ASSERT_NE(NextFunctionPos, std::string::npos);
	const std::string PrepareBody = Source.substr(PreparePos, NextFunctionPos - PreparePos);

	EXPECT_NE(PrepareBody.find("int LoadingJobsAwaitingResult = 0;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("int LoadingJobsReadyForMainThread = 0;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("if(!pSkinContainer->m_pLoadJob->Done())"), std::string::npos);
	EXPECT_NE(PrepareBody.find("const bool DecodeJobsSaturated ="), std::string::npos);
	EXPECT_NE(PrepareBody.find("LoadingJobsReadyForMainThread == 0"), std::string::npos);
	EXPECT_NE(PrepareBody.find("m_SettingsThroughputControllerOutput = SettingsSkinThroughputControllerStep({"), std::string::npos);
}

TEST(Skins, TeeFinishLoadingKeepsPriorityBeforeBackgroundSweep)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t FinishLoadingPos = Source.find("void CSkins::UpdateFinishLoading(");
	ASSERT_NE(FinishLoadingPos, std::string::npos);
	const size_t FinishLoadingEnd = Source.find("void CSkins::RefreshEventSkins()", FinishLoadingPos);
	ASSERT_NE(FinishLoadingEnd, std::string::npos);
	const std::string FinishLoading = Source.substr(FinishLoadingPos, FinishLoadingEnd - FinishLoadingPos);

	const size_t UsageListPos = FinishLoading.find("for(const std::string &SkinName : vUsageSnapshot)");
	const size_t DeferBackgroundPos = FinishLoading.find("if(SettingsSkinFinalizeShouldDeferBackgroundSweep(ProcessedHighPrioritySkin, SkinsProcessedThisFrame, MaxSkinsPerFrame))");
	const size_t BackgroundListPos = FinishLoading.find("for(const std::string &SkinName : vBackgroundSnapshot)");
	const size_t FallbackSweepPos = FinishLoading.find("for(auto &[_, pSkinContainer] : m_Skins)");

	ASSERT_NE(UsageListPos, std::string::npos);
	ASSERT_NE(DeferBackgroundPos, std::string::npos);
	ASSERT_NE(BackgroundListPos, std::string::npos);
	ASSERT_NE(FallbackSweepPos, std::string::npos);
	EXPECT_LT(UsageListPos, DeferBackgroundPos);
	EXPECT_LT(DeferBackgroundPos, BackgroundListPos);
	EXPECT_LT(BackgroundListPos, FallbackSweepPos);
}

TEST(Skins, TeeSettingsListEmitsRequestWindowPerfLogs)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("controller_reason=%s"), std::string::npos);
	EXPECT_NE(Source.find("frame_time_avg_ms=%.3f render_frame_time_ms=%.3f admission_underfed=%d underfed_streak=%d"), std::string::npos);
	EXPECT_NE(Source.find("visible_reserve_effective=%d"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);"), std::string::npos);
	EXPECT_NE(Source.find("event=work_drain page=settings:tee kind=merge count=%llu bytes=%d dur_ms=%.3f stop=%s source=list_drain_summary scope=session"), std::string::npos);
	EXPECT_NE(Source.find("uploads_done_total=%llu loaded_total=%llu uploads_per_sec=%.3f loaded_per_sec=%.3f"), std::string::npos);
	EXPECT_NE(Source.find("max_requested=%d max_pending=%d max_loading=%d max_real_inflight=%d count_fuse_limit=%d"), std::string::npos);
	EXPECT_NE(Source.find("total_requested=%llu total_admitted=%llu total_started=%llu"), std::string::npos);
	EXPECT_NE(Source.find("num_loading_window_waits=%d num_gpu_budget_waits=%d num_queue_fuse_waits=%d full_list_ready=%d final_real_inflight=%d"), std::string::npos);
	EXPECT_NE(Source.find("last_wait_reason=%s last_dynamic_decision=%s last_request_budget_block_reason=%s"), std::string::npos);
	EXPECT_NE(Source.find("event=admission_invariant_violation pending=%d loading=%d real_inflight=%d count_fuse_limit=%d"), std::string::npos);
	EXPECT_NE(Source.find("if(gs_TeeListDrainPerfSession.m_Active)\n\t\t\tLogTeeListDrainSummary(Client(), GameClient()->m_Skins, GameClient()->m_Skins.LoadingStats(), false, RefreshNowNs);"), std::string::npos);
	EXPECT_NE(Source.find("BeginTeeListDrainPerfSession(GameClient()->m_Skins, RefreshNowNs);"), std::string::npos);
	EXPECT_NE(Source.find("m_SettingsHighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_NE(Source.find("if(PerfDebugEnabled() &&"), std::string::npos);
	EXPECT_NE(Source.find("if(m_SettingsRuntimeMetadata.m_LastPage != SETTINGS_TEE)"), std::string::npos);
}

TEST(Skins, PrewarmPlayerPreviewReadyRequiresSelectedAndVisibleSourcesLoaded)
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

TEST(Skins, PrewarmPlayerPreviewReadyNoLongerBuildsPreviewCacheKeys)
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

TEST(Skins, TeeSettingsRequestsNoLongerPromoteToPendingAtRequestSite)
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

TEST(Skins, TeePrewarmNoLongerUsesImmediateBoolPath)
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

TEST(Skins, SourceResidencyNoLongerDependsOnPreviewCachePins)
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

TEST(Skins, SkinListWaitsForCompletePlanInsteadOfSeedingPlaceholderEntry)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("SeedVisibleSkinListIfEmpty"), std::string::npos);
	EXPECT_NE(Source.find("m_SkinList.m_vSkins = std::move(m_vPendingSkinListEntries);"), std::string::npos);
}

TEST(Skins, SkinRefreshKeepsExistingListWhileNewPlanLoads)
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

TEST(Skins, TeeSkinRefreshClearsListPreviewCacheBeforeReloadingSkinTextures)
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

TEST(Skins, TeeSkinListPreviewCacheKeysCoverPreviewVariantsAndStayBounded)
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

TEST(Skins, TeeSkinListLoadingEntriesUseDefaultSkinFallbackWithLoadingIndicator)
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

TEST(Skins, AbortedLocalSkinLoadJobStopsBeforeExpensiveRefreshWork)
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

TEST(Skins, AsyncSkinListKeepsQueuedColorVariantsSelectable)
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

TEST(Skins, DirectoryScanMergesLocalAndDownloadedSkinsWithLocalPriority)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ProcessDirectoryPos = Source.find("void CSkins::ProcessSkinDirectoryScanJob()");
	ASSERT_NE(ProcessDirectoryPos, std::string::npos);
	const size_t ProcessListPlanPos = Source.find("void CSkins::ProcessSkinListPlanJob()", ProcessDirectoryPos);
	ASSERT_NE(ProcessListPlanPos, std::string::npos);
	const std::string ProcessDirectoryBody = Source.substr(ProcessDirectoryPos, ProcessListPlanPos - ProcessDirectoryPos);

	EXPECT_NE(Source.find("ScanDirectory(\"skins\", CSkinContainer::EType::LOCAL);"), std::string::npos);
	EXPECT_NE(Source.find("ScanDirectory(\"downloadedskins\", CSkinContainer::EType::DOWNLOAD);"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("const bool KeepExistingLocalSkin ="), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->Type() == CSkinContainer::EType::LOCAL"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("Entry.m_Type == CSkinContainer::EType::DOWNLOAD"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("if(KeepExistingLocalSkin)"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->m_StorageType = Entry.m_StorageType;"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->m_Type = Entry.m_Type;"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("CSkinContainer SkinContainer(this, Entry.m_Name.c_str(), Entry.m_Type, Entry.m_StorageType);"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->SetLastModified(Entry.m_LastModified);"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->m_pLoadJob->Abort();"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("if(OldState == CSkinContainer::EState::LOADED && pSkinContainer->m_pSkin)"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->m_pSkin.reset();"), std::string::npos);
	EXPECT_NE(ProcessDirectoryBody.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING, OldPriority);"), std::string::npos);
}

TEST(Skins, SkinDataPreparationBuildsMergedMetricsWithoutGraphics)
{
	CImageInfo Image = MakeTestSkinImage(64, 32);
	SetTestPixel(Image, 1, 2, 12, 24, 48, 255);
	SetTestPixel(Image, 26, 4, 255, 0, 0, 255);
	SetTestPixel(Image, 28, 6, 255, 0, 0, 255);
	SetTestPixel(Image, 49, 10, 0, 255, 0, 255);
	SetTestPixel(Image, 52, 19, 0, 255, 0, 255);

	const CSkins::SSkinSpriteSpec Body{8, 4, 0, 0, 3, 3};
	const CSkins::SSkinSpriteSpec BodyOutline{8, 4, 3, 0, 3, 3};
	const CSkins::SSkinSpriteSpec Feet{8, 4, 6, 1, 2, 1};
	const CSkins::SSkinSpriteSpec FeetOutline{8, 4, 6, 2, 2, 1};
	CSkins::SSkinDataPlan Plan;

	EXPECT_TRUE(CSkins::BuildSkinDataPlan(Image, Body, BodyOutline, Feet, FeetOutline, Plan));

	EXPECT_EQ(Plan.m_Body.m_Width, 4);
	EXPECT_EQ(Plan.m_Body.m_Height, 5);
	EXPECT_EQ(Plan.m_Body.m_OffsetX, 1);
	EXPECT_EQ(Plan.m_Body.m_OffsetY, 2);
	EXPECT_EQ(Plan.m_Body.m_MaxWidth, 24);
	EXPECT_EQ(Plan.m_Body.m_MaxHeight, 24);
	EXPECT_EQ(Plan.m_Feet.m_Width, 4);
	EXPECT_EQ(Plan.m_Feet.m_Height, 2);
	EXPECT_EQ(Plan.m_Feet.m_OffsetX, 1);
	EXPECT_EQ(Plan.m_Feet.m_OffsetY, 2);
	EXPECT_EQ(Plan.m_Feet.m_MaxWidth, 16);
	EXPECT_EQ(Plan.m_Feet.m_MaxHeight, 8);

	Image.Free();
}

TEST(Skins, SkinDataPreparationUsesOutlineMetricsWhenFillIsEmpty)
{
	CImageInfo Image = MakeTestSkinImage(64, 32);
	SetTestPixel(Image, 27, 5, 255, 0, 0, 255);
	SetTestPixel(Image, 29, 7, 255, 0, 0, 255);
	SetTestPixel(Image, 50, 18, 0, 255, 0, 255);
	SetTestPixel(Image, 53, 21, 0, 255, 0, 255);

	const CSkins::SSkinSpriteSpec Body{8, 4, 0, 0, 3, 3};
	const CSkins::SSkinSpriteSpec BodyOutline{8, 4, 3, 0, 3, 3};
	const CSkins::SSkinSpriteSpec Feet{8, 4, 6, 1, 2, 1};
	const CSkins::SSkinSpriteSpec FeetOutline{8, 4, 6, 2, 2, 1};
	CSkins::SSkinDataPlan Plan;

	EXPECT_TRUE(CSkins::BuildSkinDataPlan(Image, Body, BodyOutline, Feet, FeetOutline, Plan));

	EXPECT_EQ(Plan.m_Body.m_Width, 3);
	EXPECT_EQ(Plan.m_Body.m_Height, 3);
	EXPECT_EQ(Plan.m_Body.m_OffsetX, 3);
	EXPECT_EQ(Plan.m_Body.m_OffsetY, 5);
	EXPECT_EQ(Plan.m_Feet.m_Width, 4);
	EXPECT_EQ(Plan.m_Feet.m_Height, 4);
	EXPECT_EQ(Plan.m_Feet.m_OffsetX, 2);
	EXPECT_EQ(Plan.m_Feet.m_OffsetY, 2);

	Image.Free();
}

TEST(Skins, RenderedTeeOffsetKeepsGlobalHorizontalOffsetStable)
{
	CTeeRenderInfo Info;
	SetBeastLikeMetrics(Info);

	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
	const vec2 Mid = ComputeRenderedTeeMid(Info);

	EXPECT_FLOAT_EQ(OffsetToMid.x, 0.0f);
	EXPECT_NEAR(OffsetToMid.y + Mid.y, 0.0f, 0.0001f);
}

TEST(Skins, SkinQueueEntrySixupDataParticipatesInEquality)
{
	CSkins::CSkinQueueEntry Base;
	Base.m_SkinName = "cammostripes";
	Base.m_UseCustomColor = true;
	Base.m_ColorBody = 123;
	Base.m_ColorFeet = 456;
	Base.m_HasSixup = true;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		str_copy(Base.m_aaSixupSkinPartNames[Part], "standard", sizeof(Base.m_aaSixupSkinPartNames[Part]));
		Base.m_aSixupUseCustomColors[Part] = 0;
		Base.m_aSixupSkinPartColors[Part] = Part;
	}

	CSkins::CSkinQueueEntry Same = Base;
	EXPECT_TRUE(Base == Same);

	CSkins::CSkinQueueEntry DifferentPartName = Base;
	str_copy(DifferentPartName.m_aaSixupSkinPartNames[0], "kitty", sizeof(DifferentPartName.m_aaSixupSkinPartNames[0]));
	EXPECT_FALSE(Base == DifferentPartName);

	CSkins::CSkinQueueEntry DifferentUseCustomColor = Base;
	DifferentUseCustomColor.m_aSixupUseCustomColors[1] = 1;
	EXPECT_FALSE(Base == DifferentUseCustomColor);

	CSkins::CSkinQueueEntry DifferentPartColor = Base;
	DifferentPartColor.m_aSixupSkinPartColors[2] = 999;
	EXPECT_FALSE(Base == DifferentPartColor);
}

TEST(Skins, SkinQueueEntryEqualityIgnoresColorsWhenUseCustomColorIsFalse)
{
	// Intent: when UseCustomColor is false the body/feet colors are unused, so
	// equality must NOT depend on them — two "default color" entries with
	// different unused color fields are the same skin.
	CSkins::CSkinQueueEntry A;
	A.m_SkinName = "default";
	A.m_UseCustomColor = false;
	A.m_ColorBody = 100;
	A.m_ColorFeet = 200;
	A.m_HasSixup = false;

	CSkins::CSkinQueueEntry B = A;
	B.m_ColorBody = 999;
	B.m_ColorFeet = 1;
	EXPECT_TRUE(A == B);

	// Flip to custom color: now the colors must participate in equality.
	CSkins::CSkinQueueEntry C = A;
	C.m_UseCustomColor = true;
	CSkins::CSkinQueueEntry D = C;
	D.m_ColorBody = 999;
	EXPECT_FALSE(C == D);

	CSkins::CSkinQueueEntry E = C;
	E.m_ColorFeet = 999;
	EXPECT_FALSE(C == E);
}

TEST(Skins, SkinQueueEntryEqualityComparesSkinNameAndFlags)
{
	CSkins::CSkinQueueEntry Base;
	Base.m_SkinName = "default";
	Base.m_UseCustomColor = false;
	Base.m_HasSixup = false;

	CSkins::CSkinQueueEntry DifferentName = Base;
	DifferentName.m_SkinName = "other";
	EXPECT_FALSE(Base == DifferentName);

	CSkins::CSkinQueueEntry DifferentUseCustomColor = Base;
	DifferentUseCustomColor.m_UseCustomColor = true;
	EXPECT_FALSE(Base == DifferentUseCustomColor);

	CSkins::CSkinQueueEntry DifferentHasSixup = Base;
	DifferentHasSixup.m_HasSixup = true;
	EXPECT_FALSE(Base == DifferentHasSixup);
}

TEST(Skins, SkinQueuePresetKindDeterminesProtection)
{
	CSkins::CSkinQueuePreset UserPreset;
	UserPreset.m_Kind = CSkins::CSkinQueuePreset::EKind::USER;
	EXPECT_EQ(UserPreset.Kind(), CSkins::CSkinQueuePreset::EKind::USER);
	EXPECT_FALSE(UserPreset.IsProtected());

	CSkins::CSkinQueuePreset ServerPreset;
	ServerPreset.m_Kind = CSkins::CSkinQueuePreset::EKind::SERVER;
	EXPECT_EQ(ServerPreset.Kind(), CSkins::CSkinQueuePreset::EKind::SERVER);
	EXPECT_TRUE(ServerPreset.IsProtected());

	// Default-constructed preset is USER (not protected) — matches the built-in
	// Default preset being user-editable.
	CSkins::CSkinQueuePreset DefaultCtor;
	EXPECT_FALSE(DefaultCtor.IsProtected());
}

TEST(Skins, IsSkinQueuePresetWritableExcludesServerAndOutOfRange)
{
	// Presets layout: [0]=Default, [1]=Server, [2..4]=user. Total 5.
	constexpr size_t kCount = 5;
	EXPECT_FALSE(CSkins::IsSkinQueuePresetWritable(-1, kCount)); // nothing applied
	EXPECT_TRUE(CSkins::IsSkinQueuePresetWritable(0, kCount)); // Default (writable)
	EXPECT_FALSE(CSkins::IsSkinQueuePresetWritable((int)CSkins::SKIN_QUEUE_SERVER_PRESET, kCount)); // Server (dynamic, not writable)
	EXPECT_TRUE(CSkins::IsSkinQueuePresetWritable(2, kCount)); // user preset
	EXPECT_TRUE(CSkins::IsSkinQueuePresetWritable(4, kCount)); // last user preset
	EXPECT_FALSE(CSkins::IsSkinQueuePresetWritable((int)kCount, kCount)); // == count (out of range)
	EXPECT_FALSE(CSkins::IsSkinQueuePresetWritable(99, kCount)); // far out of range
}

TEST(Skins, NextAppliedPresetIndexAfterRemoveHandlesAllBranches)
{
	// Removed preset was the applied one → no preset applied anymore.
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(3, 3), -1);
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(0, 0), -1);
	// Applied preset was above the removed one → shifts down to keep pointing at it.
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(5, 2), 4);
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(2, 1), 1);
	// Applied preset was below the removed one → index unchanged.
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(1, 2), 1);
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(0, 2), 0);
	// Nothing was applied → stays nothing.
	EXPECT_EQ(CSkins::NextAppliedPresetIndexAfterRemove(-1, 2), -1);
}

TEST(Skins, MapPlayerSkinQueueSyncReplacesCurrentQueue)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t SyncPos = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)");
	ASSERT_NE(SyncPos, std::string::npos);
	const size_t SyncEnd = Source.find("bool CSkins::IsInSkinQueue", SyncPos);
	ASSERT_NE(SyncEnd, std::string::npos);
	const std::string SyncBody = Source.substr(SyncPos, SyncEnd - SyncPos);
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);
	const size_t ApplyPresetPos = Source.find("bool CSkins::ApplySkinQueuePreset(size_t PresetIndex, int Dummy)");
	ASSERT_NE(ApplyPresetPos, std::string::npos);
	const size_t ApplyPresetEnd = Source.find("bool CSkins::RemoveSkinQueuePreset", ApplyPresetPos);
	ASSERT_NE(ApplyPresetEnd, std::string::npos);
	const std::string ApplyPresetBody = Source.substr(ApplyPresetPos, ApplyPresetEnd - ApplyPresetPos);

	// Sync replaces the queue in place from map players (no grow-only merge).
	EXPECT_EQ(SyncBody.find("std::vector<CSkinQueueEntry> vMapSkins"), std::string::npos);
	EXPECT_EQ(SyncBody.find("SyncSkinQueueEntriesInPlace("), std::string::npos);
	EXPECT_NE(SyncBody.find("Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);"), std::string::npos);
	EXPECT_NE(SyncBody.find("std::array<CSkinQueueEntry, MAX_CLIENTS> aMapSkins"), std::string::npos);
	EXPECT_NE(Source.find("#include <array>"), std::string::npos);
	// Sync only runs in server-rotation mode, so the queue is attributed to the
	// Server preset and mirrored into the Server preset's template (never the
	// Default preset at index 0).
	EXPECT_NE(SyncBody.find("m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue = Queue;"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)SKIN_QUEUE_SERVER_PRESET;"), std::string::npos);
	EXPECT_EQ(SyncBody.find("m_vSkinQueuePresets[0].m_Queue"), std::string::npos);
	// The content-matching fallback was removed; Applied is set explicitly.
	EXPECT_EQ(SyncBody.find("SkinQueueCurrentPresetIndex"), std::string::npos);
	EXPECT_EQ(Source.find("FindMatchingSkinQueuePresetIndex"), std::string::npos);
	// ApplySkinQueuePreset copies the preset into the playing queue, marks it
	// clean, and resets the rotation timer (click-to-apply model).
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueue[Dummy] = Presets[PresetIndex].m_Queue;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("SkinQueueIndexVar(Dummy) = 0;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)PresetIndex;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueueDirty[Dummy] = false;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("m_aActiveSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("SyncSkinQueueEntriesInPlace("), std::string::npos);
	EXPECT_EQ(UpdateBody.find("TrimSkinQueueToLimit(Dummy);"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("TrimSkinQueueToLimit(Dummy);"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("SkinQueueLengthVar(Dummy)"), std::string::npos);
}

TEST(Skins, SkinQueuePresetsAreSelectableEditableQueues)
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
	const size_t ApplyPresetPos = Source.find("bool CSkins::ApplySkinQueuePreset(size_t PresetIndex, int Dummy)");
	ASSERT_NE(ApplyPresetPos, std::string::npos);
	const size_t ApplyPresetEnd = Source.find("bool CSkins::RemoveSkinQueuePreset", ApplyPresetPos);
	ASSERT_NE(ApplyPresetEnd, std::string::npos);
	const std::string ApplyPresetBody = Source.substr(ApplyPresetPos, ApplyPresetEnd - ApplyPresetPos);
	const size_t ClearPos = Source.find("void CSkins::ClearSkinQueue(int Dummy)");
	ASSERT_NE(ClearPos, std::string::npos);
	const size_t ClearEnd = Source.find("bool CSkins::SaveSkinQueueToAppliedPreset", ClearPos);
	ASSERT_NE(ClearEnd, std::string::npos);
	const std::string ClearBody = Source.substr(ClearPos, ClearEnd - ClearPos);

	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string Menus = MenusBuffer.str();

	// Preset model: Default(0, USER) + Server(1, SERVER) built-ins, then user presets.
	EXPECT_NE(Header.find("static constexpr size_t SKIN_QUEUE_DEFAULT_PRESET = 0;"), std::string::npos);
	EXPECT_NE(Header.find("static constexpr size_t SKIN_QUEUE_SERVER_PRESET = 1;"), std::string::npos);
	EXPECT_NE(Header.find("bool IsBuiltInSkinQueuePreset(size_t PresetIndex) const"), std::string::npos);
	EXPECT_NE(Header.find("int AppliedSkinQueuePresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_NE(Header.find("bool SkinQueueDirty(int Dummy) const"), std::string::npos);
	EXPECT_NE(Header.find("bool SaveSkinQueueToAppliedPreset(int Dummy)"), std::string::npos);
	EXPECT_NE(Header.find("bool AddActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Header.find("bool RemoveActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Header.find("void MoveActiveSkinQueueItem("), std::string::npos);
	EXPECT_NE(Header.find("bool ApplySkinQueueIndex(size_t QueueIndex, int Dummy)"), std::string::npos);
	EXPECT_NE(Header.find("void TrimActiveSkinQueueToLimit("), std::string::npos);
	EXPECT_NE(Header.find("std::array<int, NUM_DUMMIES> m_aAppliedSkinQueuePresetIndex"), std::string::npos);
	EXPECT_NE(Header.find("std::array<bool, NUM_DUMMIES> m_aSkinQueueDirty"), std::string::npos);
	EXPECT_NE(Header.find("std::vector<CSkinQueuePreset> m_vSkinQueuePresets"), std::string::npos);
	EXPECT_EQ(Header.find("std::array<std::vector<CSkinQueuePreset>, NUM_DUMMIES> m_aSkinQueuePresets"), std::string::npos);
	// Removed: the old "active/edit-state" preset selection model.
	EXPECT_EQ(Header.find("int ActiveSkinQueuePresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("bool SelectSkinQueuePreset(size_t PresetIndex, int Dummy)"), std::string::npos);
	EXPECT_EQ(Header.find("void ClearSkinQueuePresetSelection(int Dummy)"), std::string::npos);
	EXPECT_EQ(Header.find("const std::vector<CSkinQueueEntry> &ActiveSkinQueue(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("int SkinQueueCurrentPresetIndex(int Dummy) const"), std::string::npos);
	EXPECT_EQ(Header.find("std::array<int, NUM_DUMMIES> m_aActiveSkinQueuePresetIndex"), std::string::npos);

	EXPECT_NE(Source.find("std::fill(m_aAppliedSkinQueuePresetIndex.begin(), m_aAppliedSkinQueuePresetIndex.end(), -1);"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Default preset\", {}, CSkinQueuePreset::EKind::USER});"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Server preset\", {}, CSkinQueuePreset::EKind::SERVER});"), std::string::npos);
	EXPECT_EQ(Source.find("std::fill(m_aActiveSkinQueuePresetIndex.begin(), m_aActiveSkinQueuePresetIndex.end(), -1);"), std::string::npos);
	EXPECT_EQ(Source.find("ActiveSkinQueueMutable"), std::string::npos);
	EXPECT_EQ(Source.find("FindMatchingSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(Source.find("m_aSkinQueuePresets[Dummy]"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Preset %d\")"), std::string::npos);
	EXPECT_EQ(Source.find("\"Preset %d\""), Source.find("Localize(\"Preset %d\")") + strlen("Localize("));

	// Apply = click-to-apply; presets are read-only templates until Save/Save-As.
	EXPECT_NE(ApplyPresetBody.find("if(PresetIndex == SKIN_QUEUE_SERVER_PRESET)"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueue[Dummy] = Presets[PresetIndex].m_Queue;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = (int)PresetIndex;"), std::string::npos);
	EXPECT_NE(ApplyPresetBody.find("m_aSkinQueueDirty[Dummy] = false;"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("m_aActiveSkinQueuePresetIndex"), std::string::npos);
	EXPECT_EQ(ApplyPresetBody.find("if(PresetIndex < 2)"), std::string::npos);

	// Save writes the playing queue back to the applied preset; Server is not writable.
	EXPECT_NE(Source.find("m_vSkinQueuePresets[PresetIndex].m_Queue = m_aSkinQueue[Dummy];"), std::string::npos);
	EXPECT_NE(Source.find("if(!IsSkinQueuePresetWritable(PresetIndex, m_vSkinQueuePresets.size()))"), std::string::npos);

	// Clear empties the playing queue, keeps Applied (clear-then-save writes back), marks dirty.
	EXPECT_NE(ClearBody.find("m_aSkinQueue[Dummy].clear();"), std::string::npos);
	EXPECT_NE(ClearBody.find("SkinQueueRotateMapVar(Dummy) = 0;"), std::string::npos);
	EXPECT_NE(ClearBody.find("m_aSkinQueueDirty[Dummy] = true;"), std::string::npos);
	EXPECT_EQ(ClearBody.find("m_aAppliedSkinQueuePresetIndex[Dummy] = -1;"), std::string::npos);

	// UI: preset bar uses Save / Save-as / Rename / Delete; clicking a preset applies it.
	EXPECT_NE(Menus.find("Localize(\"Save\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Save as\")"), std::string::npos);
	EXPECT_NE(Menus.find("SaveSkinQueueToAppliedPreset(QueueDummy)"), std::string::npos);
	EXPECT_NE(Menus.find("AddSkinQueuePresetFromCurrent(QueueDummy)"), std::string::npos);
	EXPECT_NE(Menus.find("const int AppliedPresetIndex = GameClient()->m_Skins.AppliedSkinQueuePresetIndex(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("const bool QueueDirty = GameClient()->m_Skins.SkinQueueDirty(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("const auto &SkinQueue = GameClient()->m_Skins.SkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueDirty ? \"● \""), std::string::npos);
	EXPECT_NE(Menus.find("QueueTitleLabelProps.m_DisallowNewline = true;"), std::string::npos);
	EXPECT_NE(Menus.find("QueueTitleLabelProps.m_MinimumFontSize = 6.0f;"), std::string::npos);
	EXPECT_EQ(Menus.find("CurrentQueueRect"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueHeader.VSplitLeft(QueueHeader.w * 0.48f"), std::string::npos);
	EXPECT_NE(Menus.find("DoSettingsButton_CheckBox(SETTINGS_TEE, -1, -1, &QueueEnabled, QueueDummy ? \"tee-dummy-skin-queue-enabled\" : \"tee-player-skin-queue-enabled\", aQueueLabel, QueueEnabled, &QueueHeader"), std::string::npos);
	EXPECT_NE(Menus.find("DoSettingsButton_CheckBox(SETTINGS_TEE, -1, -1, &QueueEnabled, QueueDummy ? \"tee-dummy-skin-queue-enabled\" : \"tee-player-skin-queue-enabled\", aQueueLabel"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Enable skin queue\"), QueueEnabled"), std::string::npos);
	EXPECT_EQ(Menus.find("CUIRect QueueEnabledRect"), std::string::npos);
	EXPECT_NE(Menus.find("ResolveSettingsTeeQueuePanelGeometry(TeeMetrics, (int)SkinQueue.size(), (int)vQueuePresets.size())"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueueListSurfaceHeight"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueuePresetHeight"), std::string::npos);
	EXPECT_NE(Menus.find("QueueListBody.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueListBody);"), std::string::npos);
	EXPECT_NE(Menus.find("QueueGeometry.m_QueueListViewportHeight"), std::string::npos);
	EXPECT_NE(Menus.find("s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Menus.find("s_QueueListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Menus.find("s_PresetListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Menus.find("QueueList.HSplitTop(TeeMetrics.m_LineHeight, &QueueListHeader"), std::string::npos);
	EXPECT_NE(Menus.find("TeeMetrics.m_ButtonHeight), &QueueListHeaderLabel, &ClearQueueRect"), std::string::npos);
	EXPECT_NE(Menus.find("CurrentQueueLabelProps.m_MaxWidth = QueueListHeaderLabel.w;"), std::string::npos);
	EXPECT_NE(Menus.find("Ui()->DoLabel(&QueueListHeaderLabel, aCurrentQueueLabel"), std::string::npos);
	EXPECT_EQ(Menus.find("DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, \"tee_queue_list_label\", &QueueListHeaderLabel, Localize(\"Skin queue\")"), std::string::npos);
	EXPECT_NE(Menus.find("s_TeeClearCurrentSkinQueueButton"), std::string::npos);
	EXPECT_NE(Menus.find("IsBuiltInSkinQueuePreset(i)"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ApplySkinQueuePreset((size_t)SelectPresetIndex, QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ClearSkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.MoveActiveSkinQueueItem("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.RemoveActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.AddActiveSkinQueue("), std::string::npos);
	EXPECT_NE(Menus.find("GameClient()->m_Skins.ApplySkinQueueIndex((size_t)ApplyQueueIndex, QueueDummy);"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Queue preset: %s\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Custom\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Default preset\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Rotate all server player skins\")"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Clear current queue\")"), std::string::npos);
	EXPECT_NE(Menus.find("static ui_widget::SNumericFieldState s_aQueueIntervalStates[NUM_DUMMIES];"), std::string::npos);
	EXPECT_NE(Menus.find("IUiContext TeeSkinQueueIntervalCtx;"), std::string::npos);
	EXPECT_NE(Menus.find("TeeSkinQueueIntervalCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_skin_queue_interval_text_input\");"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_CommitPolicy = ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT;"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_NE(Menus.find("ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 1, 120000, IntervalInputGroup, QueueIntervalOptions);"), std::string::npos);
	EXPECT_EQ(Menus.find("Ui()->DoEditBox(&QueueIntervalInput, &IntervalInput"), std::string::npos);
	EXPECT_NE(Source.find("m_vSkinQueuePresets.push_back({\"Server preset\", {}, CSkinQueuePreset::EKind::SERVER});"), std::string::npos);
	// Removed UI: Apply/Save-current buttons, the select/cancel-select calls, and the
	// old edit-state wiring. Clicking a preset now applies it directly.
	EXPECT_EQ(Menus.find("const int ActivePresetIndex = GameClient()->m_Skins.ActiveSkinQueuePresetIndex(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("const auto &SkinQueue = GameClient()->m_Skins.ActiveSkinQueue(QueueDummy);"), std::string::npos);
	EXPECT_EQ(Menus.find("SkinQueueCurrentPresetIndex(QueueDummy)"), std::string::npos);
	EXPECT_EQ(Menus.find("SelectSkinQueuePreset("), std::string::npos);
	EXPECT_EQ(Menus.find("ClearSkinQueuePresetSelection("), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Apply\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Apply this preset to the current queue\")"), std::string::npos);
	EXPECT_EQ(Menus.find("if(SelectPresetIndex == 0 || SelectPresetIndex == 1)"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Editing: %s\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Editing: current queue\")"), std::string::npos);
	EXPECT_EQ(Menus.find("Localize(\"Queue capacity\")"), std::string::npos);
	EXPECT_EQ(Menus.find("return Localize(vQueuePresets[PresetIndex].m_Name.c_str());"), std::string::npos);
	EXPECT_EQ(Source.find("if(PresetIndex == 1)"), std::string::npos);
	EXPECT_EQ(Source.find("m_aActiveSkinQueuePresetIndex[Dummy] = -1;"), std::string::npos);
	EXPECT_EQ(Source.find("m_vSkinQueuePresets[PresetIndex].IsProtected()"), std::string::npos);
}

TEST(Skins, SkinQueuePresetCompatibilityKeepsLimitAndMigratesLegacyDummyPresetCommands)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t AddQueuePos = Source.find("bool CSkins::AddSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)");
	ASSERT_NE(AddQueuePos, std::string::npos);
	const size_t AddQueueEnd = Source.find("bool CSkins::AddActiveSkinQueue", AddQueuePos);
	ASSERT_NE(AddQueueEnd, std::string::npos);
	const std::string AddQueueBody = Source.substr(AddQueuePos, AddQueueEnd - AddQueuePos);
	const size_t AddActivePos = AddQueueEnd;
	const size_t AddActiveEnd = Source.find("bool CSkins::RemoveSkinQueue", AddActivePos);
	ASSERT_NE(AddActiveEnd, std::string::npos);
	const std::string AddActiveBody = Source.substr(AddActivePos, AddActiveEnd - AddActivePos);
	const size_t AddPresetItemPos = Source.find("bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)");
	ASSERT_NE(AddPresetItemPos, std::string::npos);
	const size_t AddPresetItemEnd = Source.find("bool CSkins::AddSkinQueuePresetFromCurrent", AddPresetItemPos);
	ASSERT_NE(AddPresetItemEnd, std::string::npos);
	const std::string AddPresetItemBody = Source.substr(AddPresetItemPos, AddPresetItemEnd - AddPresetItemPos);
	const size_t AddPresetPos = Source.find("bool CSkins::AddSkinQueuePreset(const char *pName, int Dummy)");
	ASSERT_NE(AddPresetPos, std::string::npos);
	ASSERT_LT(AddPresetPos, AddPresetItemPos);
	const std::string AddPresetBody = Source.substr(AddPresetPos, AddPresetItemPos - AddPresetPos);
	const size_t DummyPresetPos = Source.find("void CSkins::ConAddDummySkinQueuePreset(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetPos, std::string::npos);
	const size_t DummyPresetEnd = Source.find("void CSkins::ConAddSkinQueuePresetItem", DummyPresetPos);
	ASSERT_NE(DummyPresetEnd, std::string::npos);
	const std::string DummyPresetBody = Source.substr(DummyPresetPos, DummyPresetEnd - DummyPresetPos);
	const size_t DummyPresetItemPos = Source.find("void CSkins::ConAddDummySkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetItemPos, std::string::npos);
	const size_t DummyPresetItemEnd = Source.find("void CSkins::ConAddSkinQueuePresetItemEx", DummyPresetItemPos);
	ASSERT_NE(DummyPresetItemEnd, std::string::npos);
	const std::string DummyPresetItemBody = Source.substr(DummyPresetItemPos, DummyPresetItemEnd - DummyPresetItemPos);
	const size_t DummyPresetItemExPos = Source.find("void CSkins::ConAddDummySkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData)");
	ASSERT_NE(DummyPresetItemExPos, std::string::npos);
	const size_t DummyPresetItemExEnd = Source.find("void CSkins::ConfigSaveCallback", DummyPresetItemExPos);
	ASSERT_NE(DummyPresetItemExEnd, std::string::npos);
	const std::string DummyPresetItemExBody = Source.substr(DummyPresetItemExPos, DummyPresetItemExEnd - DummyPresetItemExPos);
	const size_t SavePos = Source.find("void CSkins::OnQueueConfigSave(IConfigManager *pConfigManager)");
	ASSERT_NE(SavePos, std::string::npos);
	const std::string SaveBody = Source.substr(SavePos);

	EXPECT_NE(AddQueueBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));"), std::string::npos);
	EXPECT_NE(AddQueueBody.find("if((int)Queue.size() >= Limit)"), std::string::npos);
	// AddActiveSkinQueue forwards to AddSkinQueue (the limit check lives in the latter).
	EXPECT_NE(AddActiveBody.find("return AddSkinQueue(pName, UseCustomColor, ColorBody, ColorFeet, Dummy);"), std::string::npos);
	EXPECT_EQ(AddActiveBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(Source.find("SKIN_QUEUE_PRESET_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("Presets.size() >= SKIN_QUEUE_PRESET_HARD_LIMIT"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("std::find_if(Presets.begin(), Presets.end()"), std::string::npos);
	EXPECT_NE(AddPresetBody.find("str_comp(Preset.m_Name.c_str(), aPresetName) == 0"), std::string::npos);
	EXPECT_NE(AddPresetItemBody.find("const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));"), std::string::npos);
	EXPECT_NE(AddPresetItemBody.find("if((int)Queue.size() >= Limit)"), std::string::npos);

	EXPECT_NE(DummyPresetBody.find("AddSkinQueuePreset("), std::string::npos);
	EXPECT_NE(DummyPresetItemBody.find("AddSkinQueuePresetItem("), std::string::npos);
	EXPECT_NE(DummyPresetItemExBody.find("AddSkinQueuePresetItem("), std::string::npos);
	EXPECT_EQ(DummyPresetBody.find("Ignoring legacy dummy skin queue preset"), std::string::npos);
	EXPECT_EQ(DummyPresetItemBody.find("Ignoring legacy dummy skin queue preset item"), std::string::npos);
	EXPECT_EQ(DummyPresetItemExBody.find("Ignoring legacy dummy skin queue preset item"), std::string::npos);
	EXPECT_EQ(SaveBody.find("add_dummy_skin_queue_preset"), std::string::npos);
	EXPECT_EQ(SaveBody.find("QueuePresetIndex < 2"), std::string::npos);
	EXPECT_NE(SaveBody.find("QueuePresetIndex != SKIN_QUEUE_DEFAULT_PRESET"), std::string::npos);
	EXPECT_NE(SaveBody.find("WriteQueueEntry(QueueSkin, false, (int)QueuePresetIndex);"), std::string::npos);
}

TEST(Skins, WebPSaveRoundTripPreservesImageShape)
{
	CImageInfo Image = MakeTestSkinImage(4, 4);
	SetTestPixel(Image, 0, 0, 255, 0, 0, 255);
	SetTestPixel(Image, 3, 0, 0, 255, 0, 255);
	SetTestPixel(Image, 0, 3, 0, 0, 255, 255);
	SetTestPixel(Image, 3, 3, 255, 255, 255, 255);

	CByteBufferWriter Writer;
	EXPECT_TRUE(CImageLoader::SaveWebP(Writer, Image));

	CImageInfo Reloaded;
	EXPECT_TRUE(CImageLoader::LoadWebP(Writer.Data(), Writer.Size(), "skins-test-webp", Reloaded));
	EXPECT_EQ(Reloaded.m_Width, 4u);
	EXPECT_EQ(Reloaded.m_Height, 4u);
	EXPECT_EQ(Reloaded.m_Format, CImageInfo::FORMAT_RGBA);

	Reloaded.Free();
	Image.Free();
}

TEST(Skins, TeePreviewLayerFlagsAreDistinctBits)
{
	EXPECT_NE(TEE_PREVIEW_LAYER_BODY_OUTLINE, TEE_PREVIEW_LAYER_BODY);
	EXPECT_NE(TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE, TEE_PREVIEW_LAYER_BACK_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE, TEE_PREVIEW_LAYER_FRONT_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_OUTLINE, TEE_PREVIEW_LAYER_BODY);
	EXPECT_NE(TEE_PREVIEW_LAYER_BODY, TEE_PREVIEW_LAYER_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_FEET, TEE_PREVIEW_LAYER_EYES);
}

TEST(Skins, TeePreviewLayerFlagsDefaultToAllLayers)
{
	EXPECT_EQ(ResolveTeePreviewLayers(0), TEE_PREVIEW_LAYER_ALL);
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BODY_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BODY));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_FEET));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_EYES));
}

TEST(Skins, TeePreviewLayerFlagsRespectExplicitMask)
{
	const int Mask = TEE_PREVIEW_LAYER_BODY | TEE_PREVIEW_LAYER_FEET;
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY));
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_FEET));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_OUTLINE));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_EYES));
}

TEST(Skins, TeePreviewLayerOutlineMasksDoNotSelectFillLayers)
{
	const int Mask = TEE_PREVIEW_LAYER_BODY_OUTLINE | TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE;
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BACK_FEET));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE));
}
