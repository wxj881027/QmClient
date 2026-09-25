// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>
#include <engine/gfx/sprite_image.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
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

extern CDataContainer *g_pData;

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

TEST(SkinOutline, KeepsArtworkUntouchedAndUsesAlphaOfFillAndOutline)
{
	CImageInfo Image = MakeTestSkinImage(8, 4);
	SetTestPixel(Image, 1, 1, 0, 0, 0, 255);
	SetTestPixel(Image, 6, 1, 20, 80, 120, 255);
	CImageInfo Original = Image.DeepCopy();
	CQmSkinOutline Outline(Image, ivec2(0, 0), ivec2(4, 0), ivec2(4, 4), vec2(4, 4));
	CImageInfo Border = Outline.BuildImage(1);
	EXPECT_TRUE(Image.DataEquals(Original));
	// 两张素材的实心部分均挖空，新增边缘只保留白色 RGB 和轮廓透明度。
	EXPECT_FLOAT_EQ(Border.PixelColor(3, 3).a, 0.0f);
	EXPECT_FLOAT_EQ(Border.PixelColor(4, 3).a, 0.0f);
	EXPECT_EQ(Border.PixelColor(3, 2), ColorRGBA(1, 1, 1, 1));
	EXPECT_EQ(Border.PixelColor(5, 3), ColorRGBA(1, 1, 1, 1));
	Border.Free();
	Image.Free();
	Original.Free();
}

TEST(SkinOutline, WidthExpandsBeyondSpriteEdgesWithoutClipping)
{
	CImageInfo Image = MakeTestSkinImage(4, 4);
	SetTestPixel(Image, 0, 0, 0, 0, 0, 255);
	CQmSkinOutline Outline(Image, ivec2(0, 0), ivec2(0, 0), ivec2(4, 4), vec2(4, 4));
	CImageInfo Thin = Outline.BuildImage(1);
	CImageInfo Thick = Outline.BuildImage(3);
	EXPECT_EQ(Thin.m_Width, 8u);
	EXPECT_EQ(Thick.m_Width, 12u);
	EXPECT_FLOAT_EQ(Thick.PixelColor(1, 4).a, 1.0f);
	EXPECT_FLOAT_EQ(Thick.PixelColor(4, 4).a, 0.0f);
	EXPECT_FLOAT_EQ(Thick.PixelColor(0, 4).a, 0.0f);
	Thin.Free();
	Thick.Free();
	Image.Free();
}

TEST(SkinOutline, TransparentSkinsAndZeroWidthHaveNoVisibleBorder)
{
	CImageInfo Image = MakeTestSkinImage(4, 4);
	CQmSkinOutline Outline(Image, ivec2(0, 0), ivec2(0, 0), ivec2(4, 4), vec2(4, 4));
	CImageInfo Border = Outline.BuildImage(2);
	for(size_t Index = 3; Index < Border.DataSize(); Index += 4)
		EXPECT_EQ(Border.m_pData[Index], 0);
	CImageInfo Disabled = Outline.BuildImage(0);
	EXPECT_EQ(Disabled.m_pData, nullptr);
	Border.Free();
	Image.Free();
}

TEST(SkinOutline, LocalAndOtherPlayersHaveIndependentSwitches)
{
	EXPECT_TRUE(QmShouldDrawSkinOutline(4, 4, 7, true, false));
	EXPECT_TRUE(QmShouldDrawSkinOutline(7, 4, 7, true, false));
	EXPECT_FALSE(QmShouldDrawSkinOutline(9, 4, 7, true, false));
	EXPECT_FALSE(QmShouldDrawSkinOutline(4, 4, 7, false, true));
	EXPECT_TRUE(QmShouldDrawSkinOutline(9, 4, 7, false, true));
	EXPECT_FALSE(QmShouldDrawSkinOutline(-1, -1, -1, true, true));
	EXPECT_FALSE(QmShouldDrawSkinOutline(4, 4, 7, false, false));
}

TEST(SkinOutline, CircularBorderKeepsSoftAlphaAndExcludesDistantCorners)
{
	CImageInfo Image = MakeTestSkinImage(5, 5);
	SetTestPixel(Image, 2, 2, 0, 0, 0, 128);
	CQmSkinOutline Outline(Image, ivec2(0, 0), ivec2(0, 0), ivec2(5, 5), vec2(5, 5));
	CImageInfo Border = Outline.BuildImage(2);
	EXPECT_NEAR(Border.PixelColor(7, 5).a, 128.0f / 255.0f, 0.0001f);
	EXPECT_NEAR(Border.PixelColor(6, 6).a, 128.0f / 255.0f, 0.0001f);
	EXPECT_FLOAT_EQ(Border.PixelColor(7, 7).a, 0.0f);
	EXPECT_FLOAT_EQ(Border.PixelColor(5, 5).a, 0.0f);
	Border.Free();
	Image.Free();
}

TEST(SkinOutline, LowResolutionSkinStillHasAThinBorder)
{
	CImageInfo Image = MakeTestSkinImage(4, 4);
	for(size_t Index = 3; Index < Image.DataSize(); Index += 4)
		Image.m_pData[Index] = 255;
	CQmSkinOutline Outline(Image, ivec2(0, 0), ivec2(0, 0), ivec2(4, 4), vec2(64, 64));
	CImageInfo Border = Outline.BuildImage(1);
	EXPECT_EQ(Border.m_Width, 68u);
	EXPECT_FLOAT_EQ(Border.PixelColor(1, 2).a, 1.0f);
	EXPECT_FLOAT_EQ(Border.PixelColor(2, 2).a, 0.0f);
	Border.Free();
	Image.Free();
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

TEST(Skins, UnresolvedSkinStatesAreOnlyMissingOrFailed)
{
	using EState = CSkins::CSkinContainer::EState;

	// 只有“确定拿不到皮肤”的状态才算解析失败：这两种状态必须回退到 default 皮肤。
	EXPECT_TRUE(CSkins::CSkinContainer::IsUnresolved(EState::NOT_FOUND));
	EXPECT_TRUE(CSkins::CSkinContainer::IsUnresolved(EState::ERROR));

	// 仍在排队/加载中的皮肤不能回退，否则会用 default 皮肤覆盖掉即将加载完成的真实皮肤。
	EXPECT_FALSE(CSkins::CSkinContainer::IsUnresolved(EState::UNLOADED));
	EXPECT_FALSE(CSkins::CSkinContainer::IsUnresolved(EState::BACKGROUND_REQUESTED));
	EXPECT_FALSE(CSkins::CSkinContainer::IsUnresolved(EState::PENDING));
	EXPECT_FALSE(CSkins::CSkinContainer::IsUnresolved(EState::LOADING));

	// 已加载的皮肤自然不需要回退。
	EXPECT_FALSE(CSkins::CSkinContainer::IsUnresolved(EState::LOADED));
}

TEST(Skins, UnresolvedScanCoalescesNewFailuresAndSkipsStableStates)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CUnresolvedSkinScanState Scan;
	EXPECT_FALSE(Scan.Consume());
	for(const EState State : {EState::UNLOADED, EState::BACKGROUND_REQUESTED, EState::PENDING, EState::LOADING, EState::LOADED})
	{
		Scan.OnStateChange(EState::ERROR, State);
		EXPECT_FALSE(Scan.Consume());
	}

	Scan.OnStateChange(EState::LOADING, EState::ERROR);
	Scan.OnStateChange(EState::LOADING, EState::NOT_FOUND);
	EXPECT_TRUE(Scan.Consume());
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::ERROR, EState::ERROR);
	Scan.OnStateChange(EState::NOT_FOUND, EState::NOT_FOUND);
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::ERROR, EState::NOT_FOUND);
	EXPECT_TRUE(Scan.Consume());
}

TEST(Skins, UnresolvedScanPreservesPendingWorkAcrossRecoveryAndNewCallbackFailures)
{
	using EState = CSkins::CSkinContainer::EState;
	CSkins::CUnresolvedSkinScanState Scan;
	Scan.OnStateChange(EState::LOADING, EState::ERROR);
	Scan.OnStateChange(EState::ERROR, EState::PENDING);
	EXPECT_TRUE(Scan.Consume());
	EXPECT_FALSE(Scan.Consume());

	Scan.OnStateChange(EState::PENDING, EState::LOADING);
	Scan.OnStateChange(EState::LOADING, EState::ERROR);
	EXPECT_TRUE(Scan.Consume());
	Scan.OnStateChange(EState::UNLOADED, EState::NOT_FOUND);
	EXPECT_TRUE(Scan.Consume());
	EXPECT_FALSE(Scan.Consume());

	Scan.OnStateChange(EState::LOADING, EState::ERROR);
	Scan = {};
	EXPECT_FALSE(Scan.Consume());
	Scan.OnStateChange(EState::UNLOADED, EState::NOT_FOUND);
	EXPECT_TRUE(Scan.Consume());
}

TEST(Skins, UnresolvedScanIsConsumedBeforeCollectionAndCallbacksKeepTheirOrder)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string Collect = FunctionBody(Source, "void CSkins::CollectUnresolvedSkins()");
	const size_t Guard = Collect.find("if(!m_UnresolvedSkinScanState.Consume())");
	const size_t Loop = Collect.find("for(auto &[_, pSkinContainer] : m_Skins)");
	ASSERT_NE(Guard, std::string::npos);
	ASSERT_NE(Loop, std::string::npos);
	EXPECT_LT(Guard, Loop);
	const std::string Update = FunctionBody(Source, "void CSkins::OnUpdate()");
	EXPECT_LT(Update.find("CollectUnresolvedSkins();"), Update.find("for(const std::string &SkinName : m_vSkinsUnresolvedThisFrame)"));
	const std::string SetState = FunctionBody(Source, "void CSkins::CSkinContainer::SetState(EState State, ESettingsResourcePriority Priority)");
	EXPECT_NE(SetState.find("m_pSkins->m_UnresolvedSkinScanState.OnStateChange(OldState, State);"), std::string::npos);
	const std::string Shutdown = FunctionBody(Source, "void CSkins::OnShutdown()");
	EXPECT_NE(Shutdown.find("m_UnresolvedSkinScanState = {};"), std::string::npos);
}

TEST(Skins, UnknownSkinNameFallsBackToDefaultSkinOnlyWhenUnresolvable)
{
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = GameClientSource.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = GameClientSource.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = GameClientSource.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);

	// 名字未知时必须显式回退到 default 皮肤，不能静默留空后由 0.7 分支画出白 Tee。
	EXPECT_NE(RefreshSkinBody.find("const CSkins::CSkinContainer *pSkinContainer = m_Skins.LookupContainerOrNullptr(SkinDescriptor.m_aSkinName);"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("CSkinContainer::IsUnresolved(pSkinContainer->State())"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("pSkin = m_Skins.FindOrNullptr(\"default\");"), std::string::npos);
	// 回退路径不得走会重新发起加载请求的查找：否则每帧回退都在重试已经失败的皮肤下载。
	EXPECT_EQ(RefreshSkinBody.find("m_Skins.FindContainerOrNullptr(SkinDescriptor.m_aSkinName)"), std::string::npos);
	EXPECT_EQ(RefreshSkinBody.find("m_Skins.FindContainerImpl(SkinDescriptor.m_aSkinName)"), std::string::npos);
	// 仍然禁止回退到 m_Skins.Find：它会返回占位皮肤，把还没加载完的皮肤覆盖成空贴图。
	EXPECT_EQ(RefreshSkinBody.find("TeeInfo.Apply(m_Skins.Find("), std::string::npos);

	const std::string SkinsSource = ReadTestSourceFile("src/game/client/components/skins.cpp");
	// 下载失败/找不到是异步发生的：本次刷新时皮肤还在 LOADING，因此必须在状态落到 NOT_FOUND/ERROR
	// 之后再次通知，否则回退永远不会生效。
	EXPECT_NE(SkinsSource.find("CSkinContainer::IsUnresolved(pSkinContainer->m_State)"), std::string::npos);
	EXPECT_NE(SkinsSource.find("m_vSkinsUnresolvedThisFrame.push_back(pSkinContainer->Name());"), std::string::npos);
	EXPECT_NE(SkinsSource.find("for(const std::string &SkinName : m_vSkinsUnresolvedThisFrame)"), std::string::npos);
	EXPECT_NE(SkinsSource.find("GameClient()->OnSkinUpdate(SkinName.c_str());"), std::string::npos);
	// 每个失败状态只通知一次：否则每次皮肤更新都会重复回调，并反复重新请求已失败的皮肤。
	EXPECT_NE(SkinsSource.find("|| pSkinContainer->m_UnresolvedNotified)"), std::string::npos);
	EXPECT_NE(SkinsSource.find("pSkinContainer->m_UnresolvedNotified = true;"), std::string::npos);
	EXPECT_NE(SkinsSource.find("if(State != OldState)\n\t\tm_UnresolvedNotified = false;"), std::string::npos);
	const size_t OnUpdatePos = SkinsSource.find("void CSkins::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnUpdateEnd = SkinsSource.find("CSkins::CSkinLoadingStats CSkins::LoadingStats() const", OnUpdatePos);
	ASSERT_NE(OnUpdateEnd, std::string::npos);
	EXPECT_NE(SkinsSource.substr(OnUpdatePos, OnUpdateEnd - OnUpdatePos).find("m_vSkinsUnresolvedThisFrame.clear();"), std::string::npos);
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

TEST(Skins, UploadFrameBudgetRequiresResetBeforeAnotherUpload)
{
	CQmSkinUploadFrameBudget Budget;
	EXPECT_TRUE(Budget.TryConsume());
	EXPECT_FALSE(Budget.TryConsume());
	Budget.Reset();
	EXPECT_TRUE(Budget.TryConsume());
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
	EXPECT_EQ(pPrepared->Image(0, 11).m_pData, nullptr);
	Source.Free();
}

TEST(QmChatAvatar, MissingSkinUsesStableCircularTee)
{
	const auto First = QmChatAvatar::Render(nullptr, "player one");
	EXPECT_EQ(First, QmChatAvatar::Render(nullptr, "player one"));
	EXPECT_NE(First, QmChatAvatar::Render(nullptr, "player two"));
	EXPECT_EQ(First[3], 0);
	EXPECT_EQ(First[(40 * QmChatAvatar::SIZE + 40) * 4 + 3], 255);
}

TEST(QmChatAvatar, SnapshotKeepsOriginalSkinAndCustomColorsAfterReset)
{
	auto pSource = std::make_shared<QmChatAvatar::SSource>();
	auto &Body = pSource->m_aSprites[QmChatAvatar::BODY];
	Body.m_Width = Body.m_Height = 1;
	Body.m_vRgba = {255, 255, 255, 255};
	CTeeRenderInfo Info;
	Info.m_CustomColoredSkin = true;
	Info.m_ColorBody = ColorRGBA(0.25f, 0.50f, 0.75f, 1.0f);
	Info.m_ColorableRenderSkin.m_pChatAvatar = pSource;
	const auto pSnapshot = QmChatAvatar::Capture(Info);
	ASSERT_NE(pSnapshot, nullptr);
	Info.Reset();
	pSource.reset();
	const auto Rgba = QmChatAvatar::Render(pSnapshot.get(), "player");
	const size_t Pixel = (40 * QmChatAvatar::SIZE + 40) * 4;
	EXPECT_NEAR(Rgba[Pixel], 64, 1);
	EXPECT_NEAR(Rgba[Pixel + 1], 128, 1);
	EXPECT_NEAR(Rgba[Pixel + 2], 191, 1);
}

TEST(QmChatAvatar, SixupSnapshotUsesSelectedDummyAndPartColor)
{
	auto pBody = std::make_shared<QmChatAvatar::SSource>();
	pBody->m_aSprites[QmChatAvatar::BODY] = {1, 1, {255, 255, 255, 255}};
	CTeeRenderInfo Info;
	auto &Sixup = Info.m_aSixup[1];
	Sixup.m_apChatAvatarColorable[protocol7::SKINPART_BODY] = pBody;
	Sixup.m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Sixup.m_aColors[protocol7::SKINPART_BODY] = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
	EXPECT_EQ(QmChatAvatar::Capture(Info, 0), nullptr);
	const auto pSnapshot = QmChatAvatar::Capture(Info, 1);
	ASSERT_NE(pSnapshot, nullptr);
	Info.Reset();
	const auto Rgba = QmChatAvatar::Render(pSnapshot.get(), "player");
	const size_t Pixel = (40 * QmChatAvatar::SIZE + 40) * 4;
	EXPECT_EQ(Rgba[Pixel], 255);
	EXPECT_EQ(Rgba[Pixel + 1], 0);
	EXPECT_EQ(Rgba[Pixel + 2], 0);
}

TEST(QmChatAvatar, SpriteCopyHasBoundedSizeAndOwnsItsPixels)
{
	std::vector<uint8_t> vPixels(512 * 256 * 4, 255);
	CImageInfo Image;
	Image.m_Width = 512;
	Image.m_Height = 256;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	const auto Sprite = QmChatAvatar::CopySprite(Image, g_pData->m_aSprites[SPRITE_TEE_BODY]);
	EXPECT_EQ(Sprite.m_Width, 64);
	EXPECT_EQ(Sprite.m_Height, 64);
	EXPECT_EQ(Sprite.m_vRgba.size(), 64u * 64u * 4u);
	std::fill(vPixels.begin(), vPixels.end(), 0);
	ASSERT_FALSE(Sprite.Empty());
	EXPECT_EQ(Sprite.m_vRgba[0], 255);
	EXPECT_EQ(Sprite.m_vRgba[3], 255);
}

TEST(Skins, PreparedVisualsPreservePixelsAndSurviveDecodeBufferRelease)
{
	std::vector<uint8_t> vOriginal(256 * 128 * 4);
	std::vector<uint8_t> vColorable(vOriginal.size());
	for(size_t i = 0; i < vOriginal.size(); ++i)
	{
		vOriginal[i] = static_cast<uint8_t>((i * 17 + i / 1024) % 256);
		vColorable[i] = static_cast<uint8_t>((i * 31 + 11) % 256);
	}
	CImageInfo Original, Colorable;
	Original.m_Width = Colorable.m_Width = 256;
	Original.m_Height = Colorable.m_Height = 128;
	Original.m_Format = Colorable.m_Format = CImageInfo::FORMAT_RGBA;
	Original.m_pData = vOriginal.data();
	Colorable.m_pData = vColorable.data();
	const auto Prepared = QmPrepareSkinVisuals(Original, Colorable, g_pData->m_aSprites);
	CSkin Skin("prepared");
	Prepared.Apply(Skin);
	constexpr int aSprites[] = {SPRITE_TEE_BODY, SPRITE_TEE_BODY_OUTLINE, SPRITE_TEE_FOOT, SPRITE_TEE_FOOT_OUTLINE, SPRITE_TEE_EYE_NORMAL};
	for(size_t i = 0; i < std::size(aSprites); ++i)
	{
		const auto ExpectedOriginal = QmChatAvatar::CopySprite(Original, g_pData->m_aSprites[aSprites[i]]);
		const auto ExpectedColorable = QmChatAvatar::CopySprite(Colorable, g_pData->m_aSprites[aSprites[i]]);
		EXPECT_EQ(Skin.m_OriginalSkin.m_pChatAvatar->m_aSprites[i].m_vRgba, ExpectedOriginal.m_vRgba);
		EXPECT_EQ(Skin.m_ColorableSkin.m_pChatAvatar->m_aSprites[i].m_vRgba, ExpectedColorable.m_vRgba);
	}
	auto ExpectedOutline = QmCreateSkinOutline(Original, g_pData->m_aSprites[SPRITE_TEE_BODY], g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE], vec2(64, 64))->BuildImage(2);
	const auto ExpectedAvatar = Skin.m_OriginalSkin.m_pChatAvatar->m_aSprites[0].m_vRgba;
	std::fill(vOriginal.begin(), vOriginal.end(), 0);
	std::fill(vColorable.begin(), vColorable.end(), 0);
	EXPECT_EQ(Skin.m_OriginalSkin.m_pChatAvatar->m_aSprites[0].m_vRgba, ExpectedAvatar);
	auto ActualOutline = Skin.m_OriginalSkin.m_pBodyOutline->BuildImage(2);
	ASSERT_EQ(ActualOutline.DataSize(), ExpectedOutline.DataSize());
	EXPECT_EQ(mem_comp(ActualOutline.m_pData, ExpectedOutline.m_pData, ActualOutline.DataSize()), 0);
	ActualOutline.Free();
	ExpectedOutline.Free();
	CSkin Second("shared");
	Prepared.Apply(Second);
	EXPECT_EQ(Second.m_OriginalSkin.m_pChatAvatar, Skin.m_OriginalSkin.m_pChatAvatar);
	EXPECT_EQ(Second.m_OriginalSkin.m_pBodyOutline, Skin.m_OriginalSkin.m_pBodyOutline);
}

TEST(Skins, FinalizeBudgetStopsBurstsWithoutStarvingFirstReadySkin)
{
	using namespace std::chrono_literals;
	EXPECT_TRUE(QmSkinCanFinalize(0, 50ms, 1ms));
	EXPECT_TRUE(QmSkinCanFinalize(1, 999us, 1ms));
	EXPECT_FALSE(QmSkinCanFinalize(1, 1ms, 1ms));
	EXPECT_FALSE(QmSkinCanFinalize(12, 50ms, 1ms));
}

TEST(Skins, UploadProgressesEveryRenderWithoutWaitingForMaintenance)
{
	CQmSkinUploadFrameBudget Budget;
	int Uploaded = 0;
	// 144 FPS 下 24 帧约 167 ms，尚未到原来的 347 ms 容器维护周期。
	for(int Frame = 0; Frame < 24; ++Frame)
	{
		for(int Update = 0; Update < 100; ++Update)
		{
			if(Budget.TryConsume())
				++Uploaded;
		}
		EXPECT_EQ(Uploaded, Frame + 1);
		Budget.Reset();
	}
	EXPECT_EQ(Uploaded, 24);
}

TEST(Skins, UploadWaitsForRenderButWarmupCanAdvanceExplicitly)
{
	CQmSkinUploadFrameBudget Budget;
	ASSERT_TRUE(Budget.TryConsume());
	// 无渲染时，即使逻辑循环继续运转，也不能积攒纹理上传。
	for(int Update = 0; Update < 1000; ++Update)
		EXPECT_FALSE(Budget.TryConsume());
	// 启动预热没有普通渲染循环，由预热入口逐次重置额度。
	for(int Step = 0; Step < 24; ++Step)
	{
		Budget.Reset();
		EXPECT_TRUE(Budget.TryConsume());
		EXPECT_FALSE(Budget.TryConsume());
	}
}

TEST(Skins, PendingUploadResumesBeforeMaintenanceThrottle)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const auto Update = FunctionBody(Source, "void CSkins::OnUpdate()");
	const size_t Resume = Update.find("DrainSettingsSkinPreviewUpload(m_pSkinPreviewUpload");
	ASSERT_NE(Resume, std::string::npos);
	EXPECT_LT(Resume, Update.find("m_ContainerUpdateTime.has_value()"));
	EXPECT_EQ(Update.substr(0, Resume).find("LoadingStats()"), std::string::npos);
	EXPECT_EQ(Update.find("m_SkinUploadFrameBudget.Reset()"), std::string::npos);
	const auto Drain = FunctionBody(Source, "CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(");
	EXPECT_LT(Drain.find("m_SkinUploadFrameBudget.TryConsume()"), Drain.find("UploadNextSkinPreviewSprite("));
	for(const char *pSignature : {"void CSkins::OnRender()", "void CSkins::UpdateForSettingsWarmup()"})
		EXPECT_NE(FunctionBody(Source, pSignature).find("m_SkinUploadFrameBudget.Reset()"), std::string::npos);
}

TEST(Skins, FinalizationUsesPreparedVisualsAndSeparateTimeBudget)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const auto Prepare = FunctionBody(Source, "bool CSkins::PrepareSkinData(");
	EXPECT_NE(Prepare.find("QmPrepareSkinVisuals("), std::string::npos);
	for(const char *pSignature : {"void CSkins::LoadSkinFinish(", "void CSkins::FinishSkinPreviewUpload("})
	{
		const auto Finish = FunctionBody(Source, pSignature);
		EXPECT_EQ(Finish.find("QmCreateSkinOutline("), std::string::npos);
		EXPECT_EQ(Finish.find("CreateChatAvatarSource("), std::string::npos);
		EXPECT_NE(Finish.find("m_PreparedVisuals.Apply("), std::string::npos);
	}
	const auto Update = FunctionBody(Source, "void CSkins::OnUpdate()");
	EXPECT_NE(Update.find("UpdateFinishLoading(Stats, time_get_nanoseconds(), 1ms)"), std::string::npos);
	const auto Process = FunctionBody(Source, "CSkins::ESkinProcessResult CSkins::ProcessSkinContainer(");
	EXPECT_LT(Process.find("QmSkinCanFinalize("), Process.find("DrainSettingsSkinPreviewUpload("));
}

TEST(Skins, PreparedTexturesPreserveAllSpritePixelsAtNativeResolution)
{
	constexpr int aExpectedSprites[] = {SPRITE_TEE_BODY, SPRITE_TEE_BODY_OUTLINE, SPRITE_TEE_FOOT, SPRITE_TEE_FOOT_OUTLINE,
		SPRITE_TEE_HAND, SPRITE_TEE_HAND_OUTLINE, SPRITE_TEE_EYE_NORMAL, SPRITE_TEE_EYE_ANGRY,
		SPRITE_TEE_EYE_PAIN, SPRITE_TEE_EYE_HAPPY, SPRITE_TEE_EYE_DEAD, SPRITE_TEE_EYE_SURPRISE};
	for(const int Scale : {1, 2})
	{
		std::vector<uint8_t> vOriginal(256 * 128 * Scale * Scale * 4);
		std::vector<uint8_t> vColorable(vOriginal.size());
		for(size_t i = 0; i < vOriginal.size(); ++i)
		{
			vOriginal[i] = static_cast<uint8_t>((i * 17 + i / 1024) % 256);
			vColorable[i] = static_cast<uint8_t>((i * 31 + 11) % 256);
		}
		CImageInfo Original, Colorable;
		Original.m_Width = Colorable.m_Width = 256 * Scale;
		Original.m_Height = Colorable.m_Height = 128 * Scale;
		Original.m_Format = Colorable.m_Format = CImageInfo::FORMAT_RGBA;
		Original.m_pData = vOriginal.data();
		Colorable.m_pData = vColorable.data();
		auto pPrepared = QmPrepareSkinTextures(Original, Colorable, g_pData->m_aSprites);
		ASSERT_NE(pPrepared, nullptr);
		for(int Variant = 0; Variant < 2; ++Variant)
		{
			const CImageInfo &Source = Variant == 0 ? Original : Colorable;
			for(size_t Index = 0; Index < std::size(aExpectedSprites); ++Index)
			{
				ASSERT_EQ(CQmPreparedSkinTextures::SpriteId(Index), aExpectedSprites[Index]);
				const CDataSprite &Sprite = g_pData->m_aSprites[aExpectedSprites[Index]];
				const CImageInfo &Actual = pPrepared->Image(Variant, Index);
				const size_t UnitX = Source.m_Width / Sprite.m_pSet->m_Gridx;
				const size_t UnitY = Source.m_Height / Sprite.m_pSet->m_Gridy;
				ASSERT_EQ(Actual.m_Width, Sprite.m_W * UnitX);
				ASSERT_EQ(Actual.m_Height, Sprite.m_H * UnitY);
				ASSERT_EQ(Actual.m_Format, Source.m_Format);
				for(size_t Row = 0; Row < Actual.m_Height; ++Row)
				{
					const size_t Offset = ((Sprite.m_Y * UnitY + Row) * Source.m_Width + Sprite.m_X * UnitX) * 4;
					EXPECT_EQ(mem_comp(Actual.m_pData + Row * Actual.m_Width * 4, Source.m_pData + Offset, Actual.m_Width * 4), 0);
				}
			}
		}
		const uint8_t FirstPixel = pPrepared->Image(0, 0).m_pData[1];
		std::fill(vOriginal.begin(), vOriginal.end(), 0);
		CImageInfo Moved = std::move(pPrepared->Image(0, 0));
		EXPECT_EQ(pPrepared->Image(0, 0).m_pData, nullptr);
		pPrepared.reset();
		EXPECT_EQ(Moved.m_pData[1], FirstPixel);
		Moved.Free();
	}
}

TEST(Skins, SpriteExtractionRejectsInvalidGeometryAndPreservesPreviousResult)
{
	std::vector<uint8_t> vPixels(256 * 128 * 4, 37);
	CImageInfo Source;
	Source.m_Width = 256;
	Source.m_Height = 128;
	Source.m_Format = CImageInfo::FORMAT_RGBA;
	Source.m_pData = vPixels.data();
	CImageInfo Result;
	ASSERT_TRUE(ExtractSpriteImage(Source, &g_pData->m_aSprites[SPRITE_TEE_BODY], Result));
	auto *pOriginalData = Result.m_pData;
	CDataSprite Invalid = g_pData->m_aSprites[SPRITE_TEE_BODY];
	Invalid.m_X = Invalid.m_pSet->m_Gridx;
	EXPECT_FALSE(ExtractSpriteImage(Source, &Invalid, Result));
	EXPECT_EQ(Result.m_pData, pOriginalData);
	EXPECT_EQ(Result.m_pData[0], 37);
	EXPECT_FALSE(ExtractSpriteImage(Source, nullptr, Result));
	Source.m_Width = 255;
	EXPECT_FALSE(ExtractSpriteImage(Source, &g_pData->m_aSprites[SPRITE_TEE_BODY], Result));
	Source.m_pData = nullptr;
	EXPECT_FALSE(ExtractSpriteImage(Source, &g_pData->m_aSprites[SPRITE_TEE_BODY], Result));
	Result.Free();
}

TEST(Skins, PreparationReleasesAtlasesAndMainThreadMovesPreparedPixels)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const auto Prepare = FunctionBody(Source, "bool CSkins::PrepareSkinData(");
	const auto PrepareSprites = Prepare.find("QmPrepareSkinTextures(");
	ASSERT_NE(PrepareSprites, std::string::npos);
	EXPECT_NE(Prepare.find("Data.m_Info.Free();", PrepareSprites), std::string::npos);
	EXPECT_NE(Prepare.find("Data.m_InfoGrayscale.Free();", PrepareSprites), std::string::npos);
	const auto Finish = FunctionBody(Source, "void CSkins::LoadSkinFinish(");
	EXPECT_EQ(Finish.find("LoadSpriteTexture("), std::string::npos);
	EXPECT_NE(Finish.find("LoadTextureRawMove("), std::string::npos);
	const auto Process = FunctionBody(Source, "CSkins::ESkinProcessResult CSkins::ProcessSkinContainer(");
	EXPECT_NE(Process.find("m_Data.m_pPreparedTextures"), std::string::npos);
	const auto Download = FunctionBody(Source, "void CSkins::CSkinDownloadJob::Run()");
	EXPECT_NE(Download.find("bool Success = m_Data.m_pPreparedTextures != nullptr;"), std::string::npos);
}

TEST(Skins, SpriteExtractionPreservesNonRgbaFormatsUsedByOtherTextures)
{
	for(const auto Format : {CImageInfo::FORMAT_R, CImageInfo::FORMAT_RA, CImageInfo::FORMAT_RGB, CImageInfo::FORMAT_RGBA})
	{
		CImageInfo Source;
		Source.m_Width = 256;
		Source.m_Height = 128;
		Source.m_Format = Format;
		std::vector<uint8_t> vPixels(Source.DataSize());
		for(size_t Index = 0; Index < vPixels.size(); ++Index)
			vPixels[Index] = static_cast<uint8_t>(Index * 19 + Index / 257);
		Source.m_pData = vPixels.data();
		const CDataSprite &Sprite = g_pData->m_aSprites[SPRITE_TEE_EYE_SURPRISE];
		CImageInfo Result;
		ASSERT_TRUE(ExtractSpriteImage(Source, &Sprite, Result));
		EXPECT_EQ(Result.m_Format, Format);
		const size_t PixelSize = Source.PixelSize();
		for(size_t Row = 0; Row < Result.m_Height; ++Row)
		{
			const size_t Offset = ((Sprite.m_Y * 32 + Row) * Source.m_Width + Sprite.m_X * 32) * PixelSize;
			EXPECT_EQ(mem_comp(Result.m_pData + Row * Result.m_Width * PixelSize, Source.m_pData + Offset, Result.m_Width * PixelSize), 0);
		}
		Result.Free();
	}
}

TEST(Skins, FailedPreparedTexturesDoNotPublishPartialImages)
{
	CImageInfo Original = MakeTestSkinImage(256, 128);
	CImageInfo InvalidColorable;
	// 原色部件已经准备完时，灰度源失败也不能交付半张皮肤。
	EXPECT_EQ(QmPrepareSkinTextures(Original, InvalidColorable, g_pData->m_aSprites), nullptr);
	Original.Free();
}

TEST(Skins, InterruptedUploadsAreDiscardedBeforeRetryOrFailure)
{
	using CContainer = CSkins::CSkinContainer;
	using EState = CContainer::EState;
	// 刷新/来源切换会重新排队，抢占会卸载，失败会进入终态。
	for(const EState Next : {EState::PENDING, EState::BACKGROUND_REQUESTED, EState::UNLOADED, EState::ERROR, EState::NOT_FOUND})
		EXPECT_TRUE(CContainer::ShouldDiscardPendingUpload(EState::LOADING, Next));
	// 继续上传和成功发布必须保留已上传纹理。
	EXPECT_FALSE(CContainer::ShouldDiscardPendingUpload(EState::LOADING, EState::LOADING));
	EXPECT_FALSE(CContainer::ShouldDiscardPendingUpload(EState::LOADING, EState::LOADED));
	// 已发布皮肤仍由原有的卸载流程处理。
	EXPECT_FALSE(CContainer::ShouldDiscardPendingUpload(EState::LOADED, EState::PENDING));
	EXPECT_FALSE(CContainer::ShouldDiscardPendingUpload(EState::LOADED, EState::UNLOADED));
	EXPECT_FALSE(CContainer::ShouldDiscardPendingUpload(EState::PENDING, EState::LOADING));
}
