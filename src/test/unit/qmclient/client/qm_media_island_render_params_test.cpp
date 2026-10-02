// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/hud_frozen_tee_state.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/tclient/pet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

TEST(QmHudMediaIslandSdfBounds, PaddingContainsSmoothUnionAndFeatherOverflow)
{
	constexpr float ScreenPixelSize = 0.5f;
	const float StrongSmoothUnion = QmHudMediaIslandBlobBlend(8.0f, 1.0f);
	const float RequiredOverflow = StrongSmoothUnion * 0.25f + ScreenPixelSize * 0.9f;

	SHudMediaIslandSdfRenderState LeftSatelliteState;
	LeftSatelliteState.m_ItemCount = 1;
	LeftSatelliteState.m_Items[0].m_SmoothUnion = StrongSmoothUnion;
	LeftSatelliteState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_GE(QmHudMediaIslandSdfPadding(LeftSatelliteState), RequiredOverflow);

	SHudMediaIslandSdfRenderState RightSatelliteState;
	RightSatelliteState.m_HasRightCapsule = true;
	RightSatelliteState.m_RightCapsule.m_SmoothUnion = StrongSmoothUnion;
	RightSatelliteState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_GE(QmHudMediaIslandSdfPadding(RightSatelliteState), RequiredOverflow);

	SHudMediaIslandSdfRenderState RestingState;
	RestingState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_FLOAT_EQ(QmHudMediaIslandSdfPadding(RestingState), 1.5f);

	SHudMediaIslandSdfRenderState ShadowState;
	ShadowState.m_ScreenPixelSize = ScreenPixelSize;
	ShadowState.m_OuterShadowSize = 3.0f;
	EXPECT_GE(QmHudMediaIslandSdfPadding(ShadowState), 3.0f + ScreenPixelSize * 0.9f);
}

TEST(QmHudMediaIslandSdfBounds, OuterRectKeepsEveryLiquidEdgeInsideTheQuad)
{
	SHudMediaIslandSdfRenderState State;
	State.m_MainRect = {10.0f, 10.0f, 20.0f, 10.0f};
	State.m_ItemCount = 1;
	State.m_Items[0].m_Center = vec2(4.0f, 15.0f);
	State.m_Items[0].m_Radii = vec2(4.0f, 5.0f);
	State.m_Items[0].m_SmoothUnion = 8.0f;
	State.m_HasRightCapsule = true;
	State.m_RightCapsule.m_Rect = {32.0f, 10.0f, 8.0f, 10.0f};
	State.m_RightCapsule.m_SmoothUnion = 8.0f;
	State.m_ScreenPixelSize = 0.5f;

	const float Padding = QmHudMediaIslandSdfPadding(State);
	const CUIRect OuterRect = QmHudMediaIslandSdfOuterRect(State);
	EXPECT_FLOAT_EQ(OuterRect.x, -Padding);
	EXPECT_FLOAT_EQ(OuterRect.y, 10.0f - Padding);
	EXPECT_FLOAT_EQ(OuterRect.x + OuterRect.w, 40.0f + Padding);
	EXPECT_FLOAT_EQ(OuterRect.y + OuterRect.h, 20.0f + Padding);
}

TEST(QmHudMediaIslandBackdrop, TransparentOpacityIncludesPureBlurAndSkipsOpaqueBackground)
{
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(0, true));
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(1, true));
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(99, true));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(100, true));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(0, false));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(99, false));
}

TEST(QmHudMediaIslandBackdrop, RefreshesEveryNewFrameAndReusesTheSameFrameAttempt)
{
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(10, 0, false));
	// 同一次主循环里的重复绘制复用结果，下一次绘制不受循环限速方式影响。
	EXPECT_FALSE(QmHudMediaIslandShouldRefreshBackdropBlur(10, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(11, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(12, 11, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(13, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(9, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(0, UINT64_MAX, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRefreshBackdropBlur(0, 0, true));
}

TEST(QmHudMediaIslandBackdrop, MapsTheAnimatedOuterRectToTheCapturedScreenTexture)
{
	const CUIRect OuterRect = {120.0f, 30.0f, 80.0f, 40.0f};
	const CUIRect ScreenRect = {0.0f, 0.0f, 400.0f, 200.0f};
	const vec4 BackdropUv = QmHudMediaIslandBackdropUv(OuterRect, ScreenRect);

	EXPECT_FLOAT_EQ(BackdropUv.x, 0.3f);
	EXPECT_FLOAT_EQ(BackdropUv.y, 0.85f);
	EXPECT_FLOAT_EQ(BackdropUv.z, 0.2f);
	EXPECT_FLOAT_EQ(BackdropUv.w, -0.2f);
	const vec4 InvalidBackdropUv = QmHudMediaIslandBackdropUv(OuterRect, CUIRect());
	EXPECT_FLOAT_EQ(InvalidBackdropUv.x, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.y, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.z, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.w, 0.0f);
}

TEST(QmHudMediaIslandSdfGpuPacking, CopiesAllShapeAndAnimationInputs)
{
	SHudMediaIslandSdfRenderState State;
	State.m_Rect = {1.0f, 2.0f, 80.0f, 24.0f};
	State.m_MainRect = {10.0f, 2.0f, 50.0f, 20.0f};
	State.m_MainRadius = 10.0f;
	State.m_MainCorners = IGraphics::CORNER_T | IGraphics::CORNER_BR;
	State.m_MainDisabledCornerRadius = 2.0f;
	State.m_ItemCount = 1;
	State.m_Items[0].m_Center = vec2(5.0f, 12.0f);
	State.m_Items[0].m_Radii = vec2(8.0f, 9.0f);
	State.m_Items[0].m_SmoothUnion = 3.0f;
	State.m_Items[0].m_ContentAlpha = 0.8f;
	State.m_Items[0].m_ContentScale = 0.7f;
	State.m_Items[0].m_CountdownProgress = 0.6f;
	State.m_Items[0].m_RingColor = ColorRGBA(0.1f, 0.9f, 1.0f, 0.7f);
	State.m_HasRightCapsule = true;
	State.m_RightCapsule.m_Rect = {62.0f, 2.0f, 20.0f, 20.0f};
	State.m_RightCapsule.m_Radius = 10.0f;
	State.m_RightCapsule.m_SmoothUnion = 4.0f;
	State.m_RingRadius = 6.0f;
	State.m_RingThickness = 1.5f;
	State.m_BackgroundColor = ColorRGBA(0.02f, 0.03f, 0.05f, 0.9f);
	State.m_ScreenPixelSize = 0.5f;
	State.m_OuterShadowSize = 1.0f;
	State.m_OuterShadowOpacity = 0.14f;
	State.m_BackdropUv = vec4(0.1f, 0.9f, 0.2f, -0.3f);

	IGraphics::SMediaIslandSdfParams Params;
	ASSERT_TRUE(QmHudMediaIslandBuildGpuSdfParams(State, Params));
	EXPECT_EQ(Params.ItemCount(), 1);
	EXPECT_TRUE(Params.HasRightCapsule());
	EXPECT_EQ(Params.MainCorners(), State.m_MainCorners);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RECT].z, 80.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].x, 1.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].y, 0.14f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].z, 0.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].w, 0.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].x, State.m_BackdropUv.x);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].y, State.m_BackdropUv.y);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].z, State.m_BackdropUv.z);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].w, State.m_BackdropUv.w);
	EXPECT_FLOAT_EQ(Params.Item(0, 0).z, 8.0f);
	EXPECT_FLOAT_EQ(Params.Item(0, 1).w, 0.6f);
	EXPECT_FLOAT_EQ(Params.Item(0, 2).g, 0.9f);
	EXPECT_FLOAT_EQ(Params.Item(0, 2).a, 0.7f);
}

TEST(QmMediaIslandGpuSdfContract, UsesFixedStd140FriendlyParameterBlock)
{
	static_assert(IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS == 12);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_RESERVED == 7);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_ITEM_STRIDE == 3);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV == 44);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_COUNT == 45);
	static_assert(sizeof(vec4) == sizeof(float) * 4);
	static_assert(sizeof(IGraphics::SMediaIslandSdfParams) == IGraphics::SMediaIslandSdfParams::DATA_COUNT * sizeof(vec4));

	IGraphics::SMediaIslandSdfParams Params;
	Params.Clear();
	Params.SetItemCount(12);
	Params.SetHasRightCapsule(true);
	Params.SetMainCorners(IGraphics::CORNER_ALL);
	EXPECT_EQ(Params.ItemCount(), 12);
	EXPECT_TRUE(Params.HasRightCapsule());
	EXPECT_EQ(Params.MainCorners(), IGraphics::CORNER_ALL);

	Params.SetItemCount(13);
	EXPECT_EQ(Params.ItemCount(), IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS);
}
