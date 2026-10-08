#include <engine/client/rounded_rect_geometry.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/ui.h>

#include <gtest/gtest.h>

TEST(QmRoundedSurface, QuantizedGeometryClampsRadiusAndInvalidSizes)
{
	const SRoundedRectGeometry Geometry = ResolveRoundedRectGeometry(0.24f, 0.74f, 10.32f, 4.19f, 3.9f, 0.5f);
	EXPECT_NEAR(Geometry.m_X, 0.0f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Y, 0.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_W, 10.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_H, 4.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Rounding, 2.25f, 1e-6f);
	const SRoundedRectGeometry SmallGeometry = ResolveRoundedRectGeometry(0.24f, 0.24f, 0.51f, 0.51f, 0.4f, 0.5f);
	EXPECT_NEAR(SmallGeometry.m_W, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_H, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_Rounding, 0.5f, 1e-6f);
	const SRoundedRectGeometry InvalidGeometry = ResolveRoundedRectGeometry(0.5f, 0.5f, 0.0f, 4.0f, 3.0f, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_X, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Y, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_W, 0.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_H, 4.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Rounding, 0.0f);
}

TEST(QmRoundedSurface, SurfacePlanQuantizesPhysicalPixels)
{
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	SRoundedSurfaceParams SdfParams;
	SdfParams.m_Radius = 8.0f;
	SdfParams.m_BorderWidth = 0.6f;
	SdfParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan Sdf = ResolveRoundedSurfacePlan(Rect, SdfParams, true);
	EXPECT_TRUE(Sdf.m_UseSdf);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.x, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.y, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.w, 20.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.h, 10.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Radius, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_BorderWidth, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_PixelSize, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.x, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.y, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.z, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.w, 5.0f);
	SRoundedSurfaceParams NonIntegerPixelParams;
	NonIntegerPixelParams.m_Radius = 3.9f;
	NonIntegerPixelParams.m_BorderWidth = 0.6f;
	NonIntegerPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan NonIntegerPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.74f, 10.32f, 4.19f}, NonIntegerPixelParams, true);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.x, 0.0f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.y, 0.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.w, 10.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.h, 4.5f, 1e-6f);
	SRoundedSurfaceParams OnePhysicalPixelParams;
	OnePhysicalPixelParams.m_Radius = 0.4f;
	OnePhysicalPixelParams.m_BorderWidth = 0.4f;
	OnePhysicalPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan OnePhysicalPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.24f, 0.51f, 0.51f}, OnePhysicalPixelParams, true);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.w, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.h, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Radius, 0.5f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_BorderWidth, 0.5f, 1e-6f);
}

TEST(QmRoundedSurface, PartialCornersBordersAndFallbackRemainBounded)
{
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const auto ExpectCornerRadii = [](const SRoundedSurfacePlan &Plan, const float Tl, const float Tr, const float Br, const float Bl) {
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, Tl);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, Tr);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, Br);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, Bl);
	};

	SRoundedSurfaceParams PartialParams;
	PartialParams.m_Radius = 4.0f;
	PartialParams.m_BorderWidth = 1.0f;
	PartialParams.m_PixelSize = 0.5f;
	PartialParams.m_Corners = IGraphics::CORNER_R;
	const SRoundedSurfacePlan Partial = ResolveRoundedSurfacePlan(Rect, PartialParams, true);
	EXPECT_TRUE(Partial.m_UseSdf);
	ExpectCornerRadii(Partial, 0.0f, 4.0f, 4.0f, 0.0f);
	SRoundedSurfaceParams LeftParams;
	LeftParams.m_Radius = 4.0f;
	LeftParams.m_BorderWidth = 1.0f;
	LeftParams.m_PixelSize = 0.5f;
	LeftParams.m_Corners = IGraphics::CORNER_L;
	const SRoundedSurfacePlan Left = ResolveRoundedSurfacePlan(Rect, LeftParams, true);
	EXPECT_TRUE(Left.m_UseSdf);
	ExpectCornerRadii(Left, 4.0f, 0.0f, 0.0f, 4.0f);
	const auto ExpectMask = [&](const int Corners, const float Tl, const float Tr, const float Br, const float Bl) {
		SRoundedSurfaceParams Params;
		Params.m_Radius = 4.0f;
		Params.m_BorderWidth = 1.0f;
		Params.m_PixelSize = 0.5f;
		Params.m_Corners = Corners;
		const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
		EXPECT_TRUE(Plan.m_UseSdf);
		ExpectCornerRadii(Plan, Tl, Tr, Br, Bl);
	};
	ExpectMask(IGraphics::CORNER_T, 4.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_B, 0.0f, 0.0f, 4.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_TL, 4.0f, 0.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_TR, 0.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BR, 0.0f, 0.0f, 4.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BL, 0.0f, 0.0f, 0.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_NONE, 0.0f, 0.0f, 0.0f, 0.0f);
	SRoundedSurfaceParams WideBorderParams;
	WideBorderParams.m_Radius = 2.0f;
	WideBorderParams.m_BorderWidth = 4.0f;
	WideBorderParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan WideBorder = ResolveRoundedSurfacePlan(Rect, WideBorderParams, true);
	EXPECT_FLOAT_EQ(WideBorder.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(WideBorder.m_BorderWidth, 4.0f);
	SRoundedSurfaceParams SwallowedInteriorParams;
	SwallowedInteriorParams.m_Radius = 8.0f;
	SwallowedInteriorParams.m_BorderWidth = 9.0f;
	SwallowedInteriorParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan SwallowedInterior = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 6.0f, 4.0f}, SwallowedInteriorParams, true);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_BorderWidth, 2.0f);
	SRoundedSurfaceParams UnsupportedParams;
	UnsupportedParams.m_Radius = 4.0f;
	UnsupportedParams.m_BorderWidth = 1.0f;
	UnsupportedParams.m_PixelSize = 0.0f;
	const SRoundedSurfacePlan Unsupported = ResolveRoundedSurfacePlan(Rect, UnsupportedParams, false);
	EXPECT_FALSE(Unsupported.m_UseSdf);
	EXPECT_FLOAT_EQ(Unsupported.m_PixelSize, 0.0001f);
}
