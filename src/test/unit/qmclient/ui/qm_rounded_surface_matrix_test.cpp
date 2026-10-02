#include <game/client/QmUi/UiSurface.h>
#include <game/client/ui.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmNewUiMenuRenderSurfaceContract, RoundedSurfaceGeometryCoversUiScaleAndRetinaMatrix)
{
	constexpr float Aspect = 16.0f / 9.0f;
	const int aScales[] = {100, 125, 150, 200};
	const int aScreenWidths[] = {1920, 3840};
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const int aCornerMasks[] = {
		IGraphics::CORNER_ALL,
		IGraphics::CORNER_L,
		IGraphics::CORNER_R,
		IGraphics::CORNER_T,
		IGraphics::CORNER_B,
		IGraphics::CORNER_TL,
		IGraphics::CORNER_TR,
		IGraphics::CORNER_BL,
		IGraphics::CORNER_BR,
		IGraphics::CORNER_NONE,
	};
	for(const int Scale : aScales)
	{
		const float VirtualHeight = QmUiVirtualScreenHeight(Scale);
		const float VirtualWidth = VirtualHeight * Aspect;
		for(const int ScreenWidth : aScreenWidths)
		{
			const float PixelSize = VirtualWidth / (float)ScreenWidth;
			ASSERT_GT(PixelSize, 0.0f);
			SRoundedSurfaceParams Params;
			Params.m_Radius = 8.0f;
			Params.m_PixelSize = PixelSize;
			const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
			EXPECT_TRUE(Plan.m_UseSdf);
			const auto IsPixelAligned = [PixelSize](const float Value) {
				return std::abs(Value - std::round(Value / PixelSize) * PixelSize) < 1e-3f;
			};
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.x));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.y));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.w));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.h));
			EXPECT_FLOAT_EQ(Plan.m_Radius, std::min(Plan.m_Rect.w, Plan.m_Rect.h) * 0.5f);
			for(const int Corners : aCornerMasks)
			{
				const vec4 Radii = ResolveRoundedSurfaceCornerRadii(Plan.m_Radius, Corners);
				EXPECT_FLOAT_EQ(Radii.x, Corners & IGraphics::CORNER_TL ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.y, Corners & IGraphics::CORNER_TR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.z, Corners & IGraphics::CORNER_BR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.w, Corners & IGraphics::CORNER_BL ? Plan.m_Radius : 0.0f);
			}
			const SRoundedSurfacePlan Fallback = ResolveRoundedSurfacePlan(Rect, Params, false);
			EXPECT_FALSE(Fallback.m_UseSdf);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.x, Plan.m_Rect.x);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.y, Plan.m_Rect.y);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.w, Plan.m_Rect.w);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.h, Plan.m_Rect.h);
			EXPECT_FLOAT_EQ(Fallback.m_Radius, Plan.m_Radius);
		}
	}
}
