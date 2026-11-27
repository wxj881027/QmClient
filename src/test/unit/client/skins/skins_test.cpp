// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

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

TEST(Skins, AlwaysLoadedSkinsRecoverOnlyFromTransientStates)
{
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_TRUE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::UNLOADED));
	EXPECT_TRUE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::PENDING));
	EXPECT_TRUE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::BACKGROUND_REQUESTED));

	EXPECT_FALSE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::LOADING));
	EXPECT_FALSE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::LOADED));
	// 终态失败不自动重试，避免缺少内建素材时每帧重复解码。
	EXPECT_FALSE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::ERROR));
	EXPECT_FALSE(CSkins::CSkinContainer::AlwaysLoadedSkinNeedsImmediateLoad(EState::NOT_FOUND));
}

TEST(Skins, BuiltInSkinsUseLocalContainerSource)
{
	using EType = CSkins::CSkinContainer::EType;

	// 内建皮肤随客户端发布，绝不能被建成下载容器，否则会停在 PENDING 且没有加载入口。
	EXPECT_EQ(CSkins::PreferredContainerType("default"), EType::LOCAL);
	EXPECT_EQ(CSkins::PreferredContainerType("x_ninja"), EType::LOCAL);
	EXPECT_EQ(CSkins::PreferredContainerType("x_spec"), EType::LOCAL);
	EXPECT_EQ(CSkins::PreferredContainerType("bluekitty"), EType::LOCAL);

	// 非内建名字仍按下载来源建容器。
	EXPECT_EQ(CSkins::PreferredContainerType("some_community_skin"), EType::DOWNLOAD);
}

TEST(SkinSources, ScanPrefersUserSkinOverPackagedCaseVariant)
{
	const int Save = IStorage::TYPE_SAVE;
	const int Data = IStorage::TYPE_SAVE + 1;

	EXPECT_FALSE(CSkins::PreferScannedSkin("paladin", Save, 1, "PaladiN", Data, 1));
	EXPECT_TRUE(CSkins::PreferScannedSkin("PaladiN", Data, 1, "paladin", Save, 1));
	EXPECT_TRUE(CSkins::PreferScannedSkin("paladin", Save, 1, "PaladiN", Save, 1));
	EXPECT_FALSE(CSkins::PreferScannedSkin("PaladiN", Save, 1, "paladin", Save, 1));
	EXPECT_TRUE(CSkins::PreferScannedSkin("paladin", Save, 0, "PaladiN", Save, 1));
	EXPECT_FALSE(CSkins::PreferScannedSkin("PaladiN", Save, 1, "paladin", Save, 0));
}

TEST(SkinSources, LocalLoadUsesCanonicalDiskName)
{
	using EType = CSkins::CSkinContainer::EType;
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("PaladiN", 1));

	EXPECT_STREQ(CSkins::ResolveLoadName(EType::LOCAL, "paladin", Index), "PaladiN");
	EXPECT_STREQ(CSkins::ResolveLoadName(EType::LOCAL, "unknown", Index), "unknown");
	EXPECT_STREQ(CSkins::ResolveLoadName(EType::DOWNLOAD, "paladin", Index), "paladin");
}

TEST(SkinSources, ListFilterMatchesSourcesIndependentlyOfDownloadToggles)
{
	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::NONE, ESkinListFilter::ALL));
	EXPECT_FALSE(SkinListFilterMatches(ESkinSourceMask::NONE, ESkinListFilter::OFFICIAL));

	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::OFFICIAL, ESkinListFilter::OFFICIAL));
	EXPECT_FALSE(SkinListFilterMatches(ESkinSourceMask::COMMUNITY, ESkinListFilter::OFFICIAL));
	EXPECT_FALSE(SkinListFilterMatches(ESkinSourceMask::DDSTATS, ESkinListFilter::OFFICIAL));
	EXPECT_FALSE(SkinListFilterMatches(ESkinSourceMask::CUSTOM, ESkinListFilter::OFFICIAL));

	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::COMMUNITY, ESkinListFilter::COMMUNITY));
	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::DDSTATS, ESkinListFilter::DDSTATS));
	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::CUSTOM, ESkinListFilter::CUSTOM));
	EXPECT_TRUE(SkinListFilterMatches(ESkinSourceMask::VANILLA, ESkinListFilter::VANILLA));

	// 来源目录是并集：重名皮肤同时属于多个目录时，每个筛选都必须保留它。
	const ESkinSourceMask Shared = ESkinSourceMask::OFFICIAL | ESkinSourceMask::COMMUNITY |
				       ESkinSourceMask::DDSTATS | ESkinSourceMask::CUSTOM | ESkinSourceMask::VANILLA;
	EXPECT_TRUE(SkinListFilterMatches(Shared, ESkinListFilter::OFFICIAL));
	EXPECT_TRUE(SkinListFilterMatches(Shared, ESkinListFilter::COMMUNITY));
	EXPECT_TRUE(SkinListFilterMatches(Shared, ESkinListFilter::DDSTATS));
	EXPECT_TRUE(SkinListFilterMatches(Shared, ESkinListFilter::CUSTOM));
	EXPECT_TRUE(SkinListFilterMatches(Shared, ESkinListFilter::VANILLA));
}

TEST(SkinSources, SourceMasksKeepCustomAndIntrinsicRoleTogether)
{
	EXPECT_EQ(SkinSourceMaskFromSource(ESkinSource::CUSTOM_OFFICIAL), ESkinSourceMask::OFFICIAL | ESkinSourceMask::CUSTOM);
	EXPECT_EQ(SkinSourceMaskFromSource(ESkinSource::CUSTOM_COMMUNITY), ESkinSourceMask::COMMUNITY | ESkinSourceMask::CUSTOM);
	EXPECT_EQ(SkinSourceMaskFromSource(ESkinSource::DDSTATS), ESkinSourceMask::DDSTATS);

	const ESkinSourceMask DdStatsCustom = ESkinSourceMask::DDSTATS | ESkinSourceMask::CUSTOM;
	EXPECT_TRUE(SkinListFilterMatches(DdStatsCustom, ESkinListFilter::DDSTATS));
	EXPECT_TRUE(SkinListFilterMatches(DdStatsCustom, ESkinListFilter::CUSTOM));
	EXPECT_FALSE(SkinListFilterMatches(DdStatsCustom, ESkinListFilter::OFFICIAL));
}

TEST(SkinSources, ListFilterFallsBackToAllForUnknownConfigurationValues)
{
	EXPECT_EQ(SkinListFilterFromInt(-1), ESkinListFilter::ALL);
	EXPECT_EQ(SkinListFilterFromInt((int)ESkinListFilter::COUNT), ESkinListFilter::ALL);
	EXPECT_EQ(SkinListFilterFromInt(1), ESkinListFilter::OFFICIAL);
	EXPECT_EQ(SkinListFilterFromInt(5), ESkinListFilter::VANILLA);
}

TEST(SkinSources, LegacyVanillaOnlyMapsToVisibleVanillaFilter)
{
	EXPECT_EQ(ResolveSkinListFilter((int)ESkinListFilter::ALL, true), ESkinListFilter::VANILLA);
	EXPECT_EQ(ResolveSkinListFilter((int)ESkinListFilter::ALL, false), ESkinListFilter::ALL);
	EXPECT_EQ(ResolveSkinListFilter((int)ESkinListFilter::COMMUNITY, true), ESkinListFilter::COMMUNITY);
	EXPECT_EQ(ResolveSkinListFilter(-1, true), ESkinListFilter::VANILLA);

	EXPECT_TRUE(VanillaSkinsOnlyActive((int)ESkinListFilter::VANILLA, false));
	EXPECT_TRUE(VanillaSkinsOnlyActive((int)ESkinListFilter::ALL, true));
	EXPECT_FALSE(VanillaSkinsOnlyActive((int)ESkinListFilter::COMMUNITY, true));
}

TEST(SkinSources, ListPlanWaitsForEveryInitialCatalogSource)
{
	SSkinListSourceSettleState State;
	State.m_HasInitialDirectoryScan = true;
	EXPECT_TRUE(SkinListSourcesSettled(State));

	State.m_DirectoryScanPending = true;
	EXPECT_FALSE(SkinListSourcesSettled(State));
	State.m_DirectoryScanPending = false;

	State.m_CatalogMergePending = true;
	EXPECT_FALSE(SkinListSourcesSettled(State));
	State.m_CatalogMergePending = false;

	State.m_CatalogRequestPending = true;
	EXPECT_FALSE(SkinListSourcesSettled(State));
	State.m_CatalogRequestPending = false;

	State.m_HasInitialDirectoryScan = false;
	EXPECT_FALSE(SkinListSourcesSettled(State));
}

TEST(SkinSources, DownloadGateFollowsSourceToggles)
{
	const int Official = g_Config.m_ClDownloadSkins;
	const int Community = g_Config.m_ClDownloadCommunitySkins;
	const int DdStats = g_Config.m_QmDownloadDdStatsSkins;
	g_Config.m_ClDownloadSkins = 0;
	g_Config.m_ClDownloadCommunitySkins = 1;
	g_Config.m_QmDownloadDdStatsSkins = 0;

	EXPECT_FALSE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::OFFICIAL));
	EXPECT_FALSE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::CUSTOM_OFFICIAL));
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::COMMUNITY));
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::CUSTOM_COMMUNITY));
	EXPECT_FALSE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::DDSTATS));
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::UNKNOWN));
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSourceMask::NONE));

	g_Config.m_ClDownloadSkins = 0;
	g_Config.m_ClDownloadCommunitySkins = 0;
	g_Config.m_QmDownloadDdStatsSkins = 1;
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::DDSTATS));
	EXPECT_TRUE(CSkins::CSkinContainer::SourceDownloadAllowed(ESkinSource::UNKNOWN));

	g_Config.m_ClDownloadSkins = Official;
	g_Config.m_ClDownloadCommunitySkins = Community;
	g_Config.m_QmDownloadDdStatsSkins = DdStats;
}

TEST(SkinSources, RequestSourceMaskRestrictsDownloadSlots)
{
	using Mask = ESkinSourceMask;

	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::NONE, ESkinSource::OFFICIAL));
	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::NONE, ESkinSource::COMMUNITY));
	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::NONE, ESkinSource::DDSTATS));

	const Mask SharedDdStats = Mask::DDSTATS | Mask::COMMUNITY;
	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(SharedDdStats, ESkinSource::DDSTATS));
	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(SharedDdStats, ESkinSource::COMMUNITY));
	EXPECT_FALSE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(SharedDdStats, ESkinSource::OFFICIAL));

	EXPECT_TRUE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::OFFICIAL, ESkinSource::OFFICIAL));
	EXPECT_FALSE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::OFFICIAL, ESkinSource::COMMUNITY));
	EXPECT_FALSE(CSkins::CSkinContainer::RequestSourcesAllowDownloadSlot(Mask::OFFICIAL, ESkinSource::DDSTATS));
}

TEST(Skins, CachedDownloadRemainsVisibleWhenNetworkDownloadIsDisabled)
{
	using EType = CSkins::CSkinContainer::EType;
	using EState = CSkins::CSkinContainer::EState;

	EXPECT_EQ(CSkins::CSkinContainer::InitialStateForSource(EType::DOWNLOAD, false, true, false, true), EState::UNLOADED);
	EXPECT_EQ(CSkins::CSkinContainer::InitialStateForSource(EType::DOWNLOAD, false, true, false, false), EState::NOT_FOUND);
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

// 合影皮肤闸门：只有终态才算"等下去没意义"，其余状态都还必须继续等。
// 这条分派错了会表现为两种真实故障——把可恢复状态当终态（成片用到 default），
// 或把终态当可恢复（渲染被拖到超时）。
TEST(Skins, PoseSkinWaitIsOnlyFutileForTerminalStates)
{
	using EState = CSkins::CSkinContainer::EState;

	for(const EState State : {EState::ERROR, EState::NOT_FOUND})
		EXPECT_TRUE(CSkins::PoseSkinWaitIsFutile(State)) << static_cast<int>(State);

	for(const EState State : {EState::UNLOADED, EState::BACKGROUND_REQUESTED, EState::PENDING, EState::LOADING, EState::LOADED})
		EXPECT_FALSE(CSkins::PoseSkinWaitIsFutile(State)) << static_cast<int>(State);
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

TEST(SkinCatalog, OfficialJsonKeepsNormalAndCommunityMembershipIndependent)
{
	const char aJson[] =
		R"({"skins":[)"
		R"({"name":"NormalSkin","type":"normal","date":"2025-01-02","creator":"Alice"},)"
		R"({"name":"CommunitySkin","type":"community","date":"2026-03-04","creator":"Bob"},)"
		R"({"name":"Unmanaged","type":"unknown","date":"2026-03-04","creator":"Nobody"}]})";
	const auto vEntries = qm_skin_catalog::ParseDdnetJson(aJson, sizeof(aJson) - 1);
	ASSERT_EQ(vEntries.size(), 2u);

	EXPECT_EQ(vEntries[0].m_Name, "NormalSkin");
	EXPECT_TRUE(SkinSourceMaskHas(vEntries[0].m_DisplaySources, ESkinSourceMask::OFFICIAL));
	EXPECT_FALSE(SkinSourceMaskHas(vEntries[0].m_DisplaySources, ESkinSourceMask::COMMUNITY));
	EXPECT_EQ(vEntries[0].m_OfficialReleaseDate, 20250102);
	EXPECT_EQ(vEntries[0].m_OfficialCreator, "Alice");

	EXPECT_EQ(vEntries[1].m_Name, "CommunitySkin");
	EXPECT_TRUE(SkinSourceMaskHas(vEntries[1].m_DisplaySources, ESkinSourceMask::COMMUNITY));
	EXPECT_FALSE(SkinSourceMaskHas(vEntries[1].m_DisplaySources, ESkinSourceMask::OFFICIAL));
	EXPECT_EQ(vEntries[1].m_OfficialReleaseDate, 20260304);
	EXPECT_EQ(vEntries[1].m_OfficialCreator, "Bob");
}

TEST(SkinCatalog, DdStatsHtmlDecodesEntityAndQuotedNames)
{
	const char aHtml[] =
		R"(<div skin_name="Gold&#43;Muster"></div>)"
		R"(<div skin_name="GunFun&#39;s Skin"></div>)"
		R"(<div skin_name="m&amp;mred"></div>)";
	const auto vNames = qm_skin_catalog::ParseDdStatsCatalogNames(aHtml);
	ASSERT_EQ(vNames.size(), 3u);
	EXPECT_EQ(vNames[0], "Gold+Muster");
	EXPECT_EQ(vNames[1], "GunFun's Skin");
	EXPECT_EQ(vNames[2], "m&mred");
}

TEST(SkinCatalog, DirectoryHtmlDecodesUrlNamesAndRejectsNonPngLinks)
{
	const char aHtml[] =
		R"(<a href="Gold%2BMuster.png">x</a>)"
		R"(<a href='GunFun%27s%20Skin.png?cache=1'>x</a>)"
		R"(<a href="notes.png.txt">x</a>)";
	const auto vNames = qm_skin_catalog::ParseDirectoryCatalogNames(aHtml);
	ASSERT_EQ(vNames.size(), 2u);
	EXPECT_EQ(vNames[0], "Gold+Muster");
	EXPECT_EQ(vNames[1], "GunFun's Skin");
}

TEST(SkinCatalog, SameNameKeepsEveryCatalogMembership)
{
	qm_skin_catalog::TEntryMap Entries;
	qm_skin_catalog::MergeEntry(Entries, {"SharedSkin", ESkinSourceMask::OFFICIAL, ESkinSourceMask::OFFICIAL, 20250101, "Alice"});
	qm_skin_catalog::MergeEntry(Entries, {"sharedskin", ESkinSourceMask::COMMUNITY, ESkinSourceMask::COMMUNITY, 20260202, "Bob"});
	qm_skin_catalog::MergeEntry(Entries, {"SharedSkin", ESkinSourceMask::DDSTATS | ESkinSourceMask::CUSTOM,
						     ESkinSourceMask::DDSTATS, 0, ""});

	ASSERT_EQ(Entries.size(), 1u);
	const auto &Entry = Entries.begin()->second;
	EXPECT_EQ(Entry.m_Name, "SharedSkin");
	EXPECT_TRUE(SkinListFilterMatches(Entry.m_DisplaySources, ESkinListFilter::OFFICIAL));
	EXPECT_TRUE(SkinListFilterMatches(Entry.m_DisplaySources, ESkinListFilter::COMMUNITY));
	EXPECT_TRUE(SkinListFilterMatches(Entry.m_DisplaySources, ESkinListFilter::DDSTATS));
	EXPECT_TRUE(SkinListFilterMatches(Entry.m_DisplaySources, ESkinListFilter::CUSTOM));
	EXPECT_TRUE(SkinSourceMaskHas(Entry.m_RequestSources, ESkinSourceMask::DDSTATS));
	EXPECT_EQ(Entry.m_OfficialReleaseDate, 20260202);
	EXPECT_EQ(Entry.m_OfficialCreator, "Bob");
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

TEST(Skins, MetricsResetRestoresExtrema)
{
	CSkin::CSkinMetrics Metrics;
	Metrics.m_Body.m_Width = 80;
	Metrics.m_Body.m_Height = 70;
	Metrics.m_Body.m_OffsetX = 5;
	Metrics.m_Body.m_OffsetY = 4;
	Metrics.m_Body.m_MaxWidth = 96;
	Metrics.m_Body.m_MaxHeight = 88;

	Metrics.Reset();
	Metrics.m_Body.m_Width = 20;
	Metrics.m_Body.m_Height = 18;
	Metrics.m_Body.m_OffsetX = 12;
	Metrics.m_Body.m_OffsetY = 11;
	Metrics.m_Body.m_MaxWidth = 32;
	Metrics.m_Body.m_MaxHeight = 30;

	EXPECT_EQ((int)Metrics.m_Body.m_Width, 20);
	EXPECT_EQ((int)Metrics.m_Body.m_Height, 18);
	EXPECT_EQ((int)Metrics.m_Body.m_OffsetX, 12);
	EXPECT_EQ((int)Metrics.m_Body.m_OffsetY, 11);
	EXPECT_EQ((int)Metrics.m_Body.m_MaxWidth, 32);
	EXPECT_EQ((int)Metrics.m_Body.m_MaxHeight, 30);
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

TEST(Skins, ShuffleQueueRetainsEveryEntryAndCurrentColoredSkin)
{
	std::vector<CSkins::CSkinQueueEntry> Queue(12);
	for(size_t i = 0; i < Queue.size(); ++i)
	{
		Queue[i].m_SkinName = "same-name";
		Queue[i].m_UseCustomColor = true;
		Queue[i].m_ColorBody = (int)i;
		Queue[i].m_ColorFeet = (int)i + 100;
		Queue[i].m_HasSixup = true;
		Queue[i].m_aSixupSkinPartColors[0] = (int)i + 200;
	}
	const auto Original = Queue;
	const int Index = CSkins::ShuffleSkinQueueEntries(Queue, 5, 1234);
	ASSERT_GE(Index, 0);
	ASSERT_LT(Index, (int)Queue.size());
	EXPECT_EQ(Queue[Index], Original[5]);
	EXPECT_NE(Queue, Original);
	for(const auto &Entry : Original)
		EXPECT_EQ(std::count(Queue.begin(), Queue.end(), Entry), 1);
}

TEST(Skins, ShuffleQueueHandlesEmptySingleAndInvalidSelection)
{
	std::vector<CSkins::CSkinQueueEntry> Queue;
	EXPECT_EQ(CSkins::ShuffleSkinQueueEntries(Queue, 99, 7), 0);
	Queue.emplace_back();
	Queue[0].m_SkinName = "default";
	const auto Entry = Queue[0];
	EXPECT_EQ(CSkins::ShuffleSkinQueueEntries(Queue, -1, 7), 0);
	EXPECT_EQ(Queue[0], Entry);
	Queue.emplace_back();
	Queue[1].m_SkinName = "other";
	const auto Last = Queue.back();
	const int Index = CSkins::ShuffleSkinQueueEntries(Queue, 99, 7);
	EXPECT_EQ(Queue[Index], Last);
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

TEST(Skins, FinalizeBudgetAlwaysLetsFirstSkinProgress)
{
	using namespace std::chrono_literals;
	// 即使预算已被超支，首个就绪皮肤仍必须完成，否则低帧率下加载会完全停滞。
	EXPECT_TRUE(QmSkinCanFinalize(0, 50ms, 1ms));
	// 已有进展后按时间预算让出主线程。
	EXPECT_TRUE(QmSkinCanFinalize(1, 999us, 1ms));
	EXPECT_FALSE(QmSkinCanFinalize(1, 1ms, 1ms));
	EXPECT_FALSE(QmSkinCanFinalize(2, 5ms, 1ms));
}

TEST(Skins, PartialUploadIsDiscardedOnCancellationOrFailure)
{
	using EState = CSkins::CSkinContainer::EState;
	EXPECT_TRUE(CSkins::CSkinContainer::ShouldDiscardPendingUpload(EState::LOADING, EState::UNLOADED));
	EXPECT_TRUE(CSkins::CSkinContainer::ShouldDiscardPendingUpload(EState::LOADING, EState::ERROR));
	EXPECT_FALSE(CSkins::CSkinContainer::ShouldDiscardPendingUpload(EState::LOADING, EState::LOADED));
	EXPECT_FALSE(CSkins::CSkinContainer::ShouldDiscardPendingUpload(EState::PENDING, EState::UNLOADED));
}

TEST(SkinUploadBudget, AllowsMultipleSpritesUntilTimeIsExhausted)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	ASSERT_TRUE(Budget.CanUpload(10));
	Budget.RecordUpload(200us);
	EXPECT_TRUE(Budget.CanUpload(10));
	Budget.RecordUpload(799us);
	EXPECT_TRUE(Budget.CanUpload(10));
	Budget.RecordUpload(1us);
	EXPECT_FALSE(Budget.CanUpload(10));
}

TEST(SkinUploadBudget, SharesTimeAcrossUpdatesAndResetsForNextFrame)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	Budget.RecordUpload(1200us, true, 4096);
	Budget.RecordUpload(800us, false, 4096);
	EXPECT_FALSE(Budget.CanUpload(10));
	EXPECT_EQ(Budget.Attempts(), 2);
	EXPECT_EQ(Budget.Submitted(), 1);
	EXPECT_EQ(Budget.PreparedBytes(), 4096u);
	EXPECT_EQ(Budget.UploadTime(), 2ms);
	Budget.Reset();
	EXPECT_TRUE(Budget.CanUpload(10));
	EXPECT_EQ(Budget.Attempts(), 0);
	EXPECT_EQ(Budget.Submitted(), 0);
	EXPECT_EQ(Budget.PreparedBytes(), 0u);
	EXPECT_EQ(Budget.UploadTime(), 0ns);
}

TEST(SkinUploadBudget, HonorsConfiguredLimit)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	Budget.Reset(2ms);
	Budget.RecordUpload(1500us);
	EXPECT_TRUE(Budget.CanUpload(10));
	Budget.RecordUpload(600us);
	EXPECT_FALSE(Budget.CanUpload(10));
	Budget.Reset(3ms);
	Budget.RecordUpload(2500us);
	EXPECT_TRUE(Budget.CanUpload(10));
}

TEST(SkinUploadBudget, FastSubmissionsShareTheCountLimitUntilRender)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	EXPECT_FALSE(Budget.CanUpload(0));
	ASSERT_TRUE(Budget.CanUpload(2));
	Budget.RecordUpload(0ns);
	ASSERT_TRUE(Budget.CanUpload(2));
	Budget.RecordUpload(0ns);
	EXPECT_FALSE(Budget.CanUpload(2));
	Budget.Reset();
	EXPECT_TRUE(Budget.CanUpload(2));
}

TEST(SkinUploadBudget, ChecksTheNextSpriteAgainstRemainingBytesBeforeSubmission)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	const size_t Limit = CQmSkinUploadFrameBudget::MAX_PREPARED_BYTES_PER_FRAME;
	Budget.RecordUpload(0ns, true, Limit - 64);
	EXPECT_FALSE(Budget.CanUpload(10, 65));
	ASSERT_TRUE(Budget.CanUpload(10, 64));
	Budget.RecordUpload(0ns, true, 64);
	EXPECT_FALSE(Budget.CanUpload(10, 1));
}

TEST(SkinUploadBudget, OversizedSpriteCanProgressAloneOnNextFrame)
{
	using namespace std::chrono_literals;
	CQmSkinUploadFrameBudget Budget;
	const size_t Oversized = CQmSkinUploadFrameBudget::MAX_PREPARED_BYTES_PER_FRAME + 1;
	Budget.RecordUpload(0ns, true, 1);
	EXPECT_FALSE(Budget.CanUpload(10, Oversized));
	Budget.Reset();
	ASSERT_TRUE(Budget.CanUpload(10, Oversized));
	Budget.RecordUpload(0ns, true, Oversized);
	EXPECT_FALSE(Budget.CanUpload(10, 0));
	Budget.Reset();
	EXPECT_TRUE(Budget.CanUpload(10, 1));
}

TEST(SkinUploadBudget, ActiveTeeRequestsUseShortPollingInterval)
{
	using namespace std::chrono_literals;
	EXPECT_EQ(QmSkinContainerUpdateInterval(300ms, true), 25ms);
	EXPECT_EQ(QmSkinContainerUpdateInterval(300ms, false), 300ms);
}

TEST(Skins, TimeSortKeepsCatalogOrderStableAfterDownload)
{
	using EOrder = ESkinSortOrder;
	// 收藏始终置顶，不随时间排序改变。
	EXPECT_EQ(SkinListValueOrder(true, false, 0, 0, 0, 0, false, false, true), EOrder::LESS);
	EXPECT_EQ(SkinListValueOrder(false, true, 0, 0, 0, 0, false, false, true), EOrder::GREATER);
	// 时间排序先比较发布日期。
	EXPECT_EQ(SkinListValueOrder(false, false, 20240101, 20231231, 0, 0, false, false, true), EOrder::LESS);
	// 目录来源条目：下载刷新 LastModified 不参与排序，否则每下载一张就会重排整表。
	EXPECT_EQ(SkinListValueOrder(false, false, 0, 0, 1700000000, 0, false, false, true), EOrder::EQUAL_KEYS);
	// 两个本地文件才用修改时间。
	EXPECT_EQ(SkinListValueOrder(false, false, 0, 0, 1700000000, 1600000000, true, true, true), EOrder::LESS);
	// 名称排序下不消费时间字段，统一交给名称比较。
	EXPECT_EQ(SkinListValueOrder(false, false, 20240101, 20231231, 0, 0, false, false, false), EOrder::EQUAL_KEYS);
}

TEST(Skins, ModifiedLoadedLocalSourceReturnsToPending)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Before{false, 0, 100};
	const SQmSkinSourceIdentity After{false, 0, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::LOADED, false, true, true), EState::PENDING);
	EXPECT_FALSE(CSkins::CSkinContainer::SourceRefreshState(After, After, EState::LOADED, false, true, true).has_value());
}

TEST(Skins, UserStorageOverrideAndLocalReplacementRequestReload)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Bundled{false, 1, 100};
	const SQmSkinSourceIdentity User{false, 0, 100};
	const SQmSkinSourceIdentity Downloaded{true, 0, 100};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Bundled, User, EState::LOADED, false, true, true), EState::PENDING);
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Downloaded, User, EState::LOADED, false, true, true), EState::PENDING);
}

TEST(Skins, SourceChangeRestartsLoadingAndPreservesPendingRequests)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Before{false, 0, 100};
	const SQmSkinSourceIdentity After{false, 0, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::LOADING, false, true, true), EState::PENDING);
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::PENDING, false, true, true), EState::PENDING);
}

TEST(Skins, LocalReplacementKeepsSkinRequestWaitingForAdmission)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Downloaded{true, IStorage::TYPE_SAVE, 100};
	const SQmSkinSourceIdentity Local{false, IStorage::TYPE_SAVE, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Downloaded, Local, EState::BACKGROUND_REQUESTED, false, true, true), EState::BACKGROUND_REQUESTED);
}

TEST(Skins, SourceChangeLeavesUnrequestedSkinsUnloadedAndRespectsDisabledDownloads)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Before{true, 0, 100};
	const SQmSkinSourceIdentity After{true, 0, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::UNLOADED, false, true, true), EState::UNLOADED);
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::NOT_FOUND, false, true, false), EState::NOT_FOUND);
}

TEST(Skins, UnresolvedNotificationIsTriggeredOnlyByNewFailures)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CUnresolvedSkinScanState Scan;
	Scan.OnStateChange(EState::PENDING, EState::LOADING);
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::LOADING, EState::ERROR);
	EXPECT_TRUE(Scan.Consume());
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::ERROR, EState::ERROR);
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::ERROR, EState::PENDING);
	Scan.OnStateChange(EState::PENDING, EState::NOT_FOUND);
	EXPECT_TRUE(Scan.Consume());
}

TEST(Skins, PreparedTexturesKeepValidSpritesWhenOneSpriteIsOutOfBounds)
{
	CImageInfo Source = MakeTestSkinImage(4, 4);
	SetTestPixel(Source, 0, 0, 23, 45, 67, 255);
	CDataSpriteset Set{};
	Set.m_Gridx = 2;
	Set.m_Gridy = 2;
	std::array<CDataSprite, SPRITE_TEE_EYE_SURPRISE + 1> aSprites{};
	for(CDataSprite &Sprite : aSprites)
	{
		Sprite.m_pSet = &Set;
		Sprite.m_pName = "prepared_test";
		Sprite.m_W = 1;
		Sprite.m_H = 1;
	}
	aSprites[SPRITE_TEE_EYE_SURPRISE].m_X = 2;

	auto pPrepared = QmPrepareSkinTextures(Source, Source, aSprites.data());
	ASSERT_NE(pPrepared, nullptr);
	EXPECT_TRUE(pPrepared->Available(0, 0));
	EXPECT_TRUE(pPrepared->Available(1, 0));
	EXPECT_EQ(pPrepared->Image(0, 0).m_Width, 2u);
	EXPECT_EQ(pPrepared->Image(0, 0).m_pData[0], 23);
	EXPECT_FALSE(pPrepared->Available(0, 11));
	EXPECT_FALSE(pPrepared->Available(1, 11));
	EXPECT_EQ(pPrepared->PreparedBytesForSprite(0), 16u);
	EXPECT_EQ(pPrepared->PreparedBytesForSprite(12), 16u);
	EXPECT_EQ(pPrepared->PreparedBytesForSprite(11), 0u);
	EXPECT_EQ(pPrepared->PreparedBytesForSprite(23), 0u);
	EXPECT_EQ(pPrepared->Image(0, 11).m_pData, nullptr);
	Source.Free();
}

TEST(Skins, LoadedRefreshCanFinalizeWithoutInitialLoads)
{
	CSkins::CSkinLoadingStats Stats;
	Stats.AddState(CSkins::CSkinContainer::EState::LOADED);
	EXPECT_FALSE(Stats.ShouldFinishLoading(false));
	// 更新上传保持 LOADED，推进不应依赖初次加载计数。
	EXPECT_TRUE(Stats.ShouldFinishLoading(true));
	EXPECT_EQ(Stats.m_NumLoading, 0u);
	EXPECT_EQ(Stats.m_NumLoaded, 1u);
	Stats.AddState(CSkins::CSkinContainer::EState::LOADING);
	EXPECT_TRUE(Stats.ShouldFinishLoading(false));
}

TEST(Skins, DisabledDownloadCanBeReplacedByLocalSourceInOneRefresh)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Downloaded{true, IStorage::TYPE_SAVE, 100};
	const SQmSkinSourceIdentity Local{false, IStorage::TYPE_SAVE, 100};
	EXPECT_EQ(CSkins::CSkinContainer::InitialStateForSource(CSkins::CSkinContainer::EType::DOWNLOAD, false, true, false), EState::NOT_FOUND);
	const auto State = CSkins::CSkinContainer::SourceRefreshState(Downloaded, Local, EState::NOT_FOUND, false, true, false);
	ASSERT_TRUE(State.has_value());
	EXPECT_EQ(State.value(), EState::UNLOADED);
	EXPECT_FALSE(CSkins::CSkinContainer::SourceRefreshState(Local, Local, State.value(), false, true, false).has_value());
}

TEST(Skins, ChangedDownloadSourceStillRespectsDisabledDownloads)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Before{true, IStorage::TYPE_SAVE, 100};
	const SQmSkinSourceIdentity After{true, IStorage::TYPE_SAVE, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::LOADED, false, true, false), EState::NOT_FOUND);
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Before, After, EState::BACKGROUND_REQUESTED, false, true, false), EState::NOT_FOUND);
}

TEST(Skins, LocalSourceReplacementStillRespectsVanillaRestriction)
{
	using EState = CSkins::CSkinContainer::EState;
	const SQmSkinSourceIdentity Downloaded{true, IStorage::TYPE_SAVE, 100};
	const SQmSkinSourceIdentity Local{false, IStorage::TYPE_SAVE, 101};
	EXPECT_EQ(CSkins::CSkinContainer::SourceRefreshState(Downloaded, Local, EState::NOT_FOUND, false, false, false), EState::NOT_FOUND);
	EXPECT_EQ(CSkins::CSkinContainer::InitialStateForSource(CSkins::CSkinContainer::EType::LOCAL, true, false, false), EState::PENDING);
}

TEST(Skins, ReloadErrorRestoresPublishedSkinAndCountsItOnce)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CSkinLoadingStats Stats;
	Stats.AddState(EState::LOADING);
	const EState State = Stats.FinishLoadingFailure(EState::LOADING, true, false);
	EXPECT_EQ(State, EState::LOADED);
	EXPECT_EQ(Stats.m_NumLoading, 0u);
	EXPECT_EQ(Stats.m_NumLoaded, 1u);
	EXPECT_EQ(Stats.m_NumError, 0u);
	EXPECT_TRUE(CSkins::CSkinContainer::TracksUsage(State, false));
	EXPECT_EQ(Stats.FinishLoadingFailure(State, true, false), EState::LOADED);
	EXPECT_EQ(Stats.m_NumLoaded, 1u);
	EXPECT_EQ(Stats.m_NumLoading, 0u);
}

TEST(Skins, ReloadNotFoundRestoresPublishedSkinInsteadOfHidingIt)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CSkinLoadingStats Stats;
	Stats.AddState(EState::LOADING);
	const EState State = Stats.FinishLoadingFailure(EState::LOADING, true, true);
	EXPECT_EQ(State, EState::LOADED);
	EXPECT_EQ(Stats.m_NumLoading, 0u);
	EXPECT_EQ(Stats.m_NumLoaded, 1u);
	EXPECT_EQ(Stats.m_NumNotFound, 0u);
	EXPECT_EQ(CSkins::CSkinContainer::StatusIndicator(State), CSkins::CSkinContainer::EStatusIndicator::NONE);
}

TEST(Skins, InitialLoadErrorWithoutPublishedSkinRemainsError)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CSkinLoadingStats Stats;
	Stats.AddState(EState::LOADING);
	EXPECT_EQ(Stats.FinishLoadingFailure(EState::LOADING, false, false), EState::ERROR);
	EXPECT_EQ(Stats.m_NumLoading, 0u);
	EXPECT_EQ(Stats.m_NumLoaded, 0u);
	EXPECT_EQ(Stats.m_NumError, 1u);
	EXPECT_EQ(Stats.m_NumNotFound, 0u);
}

TEST(Skins, InitialLoadNotFoundWithoutPublishedSkinRemainsNotFound)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CSkinLoadingStats Stats;
	Stats.AddState(EState::LOADING);
	EXPECT_EQ(Stats.FinishLoadingFailure(EState::LOADING, false, true), EState::NOT_FOUND);
	EXPECT_EQ(Stats.m_NumLoading, 0u);
	EXPECT_EQ(Stats.m_NumLoaded, 0u);
	EXPECT_EQ(Stats.m_NumError, 0u);
	EXPECT_EQ(Stats.m_NumNotFound, 1u);
}

TEST(Skins, PreviewSpriteUploadsShareSettingsBudgetAcrossDrainCalls)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(24);
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 1;
	CSkins::CSkinPreviewUploadBudget FirstDrain(24, &Limiter, &FrameBudget);
	ASSERT_TRUE(FirstDrain.Consume());
	FirstDrain.Commit();
	EXPECT_EQ(FrameBudget.m_MaxGpuUploads, 0);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 1);
	EXPECT_FALSE(FirstDrain.Consume());
	CSkins::CSkinPreviewUploadBudget SecondDrain(23, &Limiter, &FrameBudget);
	EXPECT_FALSE(SecondDrain.Consume());
	EXPECT_EQ(Limiter.UploadsThisFrame(), 1);
	// 下一帧恢复共享额度后可继续上传，耗尽后的尝试没有多扣全局计数。
	Limiter.OnFrameStart(24);
	FrameBudget = {};
	FrameBudget.m_MaxGpuUploads = 1;
	CSkins::CSkinPreviewUploadBudget NextFrame(23, &Limiter, &FrameBudget);
	ASSERT_TRUE(NextFrame.Consume());
	NextFrame.Commit();
	EXPECT_FALSE(NextFrame.Consume());
	EXPECT_EQ(Limiter.UploadsThisFrame(), 1);
}

TEST(Skins, ExhaustedGlobalUploadBudgetDoesNotConsumeSettingsAllowance)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(0);
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 1;
	CSkins::CSkinPreviewUploadBudget Budget(24, &Limiter, &FrameBudget);
	EXPECT_FALSE(Budget.Consume());
	EXPECT_EQ(FrameBudget.m_MaxGpuUploads, 1);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 0);
	Limiter.OnFrameStart(1);
	ASSERT_TRUE(Budget.Consume());
	Budget.Commit();
	EXPECT_EQ(FrameBudget.m_MaxGpuUploads, 0);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 1);
}

TEST(Skins, PreviewUploadsWithoutSettingsPageRespectRemainingSpriteCount)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(24);
	CSkins::CSkinPreviewUploadBudget Budget(2, &Limiter, nullptr);
	ASSERT_TRUE(Budget.Consume());
	Budget.Commit();
	ASSERT_TRUE(Budget.Consume());
	Budget.Commit();
	EXPECT_FALSE(Budget.Consume());
	EXPECT_EQ(Limiter.UploadsThisFrame(), 2);
}
