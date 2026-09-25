// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

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
