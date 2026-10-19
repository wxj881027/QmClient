#include <engine/shared/config.h>

#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/UiSurface.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace
{

	// 只记录图形设备边界，以三角形覆盖和颜色混合观察生产绘制结果。
	class CSurfaceGraphics : public CIconBenchmarkGraphics
	{
		struct STriangle
		{
			std::array<vec2, 3> m_aPoints;
			std::array<ColorRGBA, 3> m_aColors;
		};
		struct SRect
		{
			CUIRect m_Rect;
			ColorRGBA m_Color;
			int m_Corners;
			float m_Radius;
		};
		std::array<ColorRGBA, 4> m_aColors{};
		std::vector<STriangle> m_vTriangles;
		std::vector<SRect> m_vRects;

		static float Cross(vec2 A, vec2 B) { return A.x * B.y - A.y * B.x; }

	public:
		bool m_HasSdf = false;
		int m_AntialiasDraws = 0;
		std::vector<SRoundedRectSdfParams> m_vSdf;

		bool HasRoundedRectSdf() override { return m_HasSdf; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override { m_vSdf.push_back(Params); }
		// 外缘羽化由设备实现；内部颜色探针不模拟 GPU 抗锯齿。
		void DrawRoundedRectAntialias(float X, float Y, float W, float H, float Radius, int Corners, const ColorRGBA &Color) override { ++m_AntialiasDraws; }
		void QuadsBegin() override { SetColor(ColorRGBA(1, 1, 1, 1)); }
		void SetColor(ColorRGBA Color) override { m_aColors.fill(Color); }
		void SetColor(float R, float G, float B, float A) override { SetColor(ColorRGBA(R, G, B, A)); }
		void SetColorVertex(const CColorVertex *pArray, size_t Num) override
		{
			for(size_t i = 0; i < Num; ++i)
				m_aColors.at(pArray[i].m_Index) = ColorRGBA(pArray[i].m_R, pArray[i].m_G, pArray[i].m_B, pArray[i].m_A);
		}
		void QuadsDrawFreeform(const CFreeformItem *pArray, int Num) override
		{
			for(int i = 0; i < Num; ++i)
			{
				const CFreeformItem &Quad = pArray[i];
				const std::array<vec2, 4> aPoints = {vec2(Quad.m_X0, Quad.m_Y0), vec2(Quad.m_X1, Quad.m_Y1), vec2(Quad.m_X2, Quad.m_Y2), vec2(Quad.m_X3, Quad.m_Y3)};
				m_vTriangles.push_back({{aPoints[0], aPoints[1], aPoints[3]}, {m_aColors[0], m_aColors[1], m_aColors[3]}});
				m_vTriangles.push_back({{aPoints[0], aPoints[3], aPoints[2]}, {m_aColors[0], m_aColors[3], m_aColors[2]}});
			}
		}
		void DrawRect(float X, float Y, float W, float H, ColorRGBA Color, int Corners, float Rounding) override
		{
			m_vRects.push_back({{X, Y, W, H}, Color, Corners, Rounding});
		}

		ColorRGBA Sample(vec2 Point) const
		{
			ColorRGBA Result(0, 0, 0, 0);
			const auto Blend = [&](ColorRGBA Color) {
				Result.r = Color.r * Color.a + Result.r * (1.0f - Color.a);
				Result.g = Color.g * Color.a + Result.g * (1.0f - Color.a);
				Result.b = Color.b * Color.a + Result.b * (1.0f - Color.a);
				Result.a = Color.a + Result.a * (1.0f - Color.a);
			};
			for(const SRect &Draw : m_vRects)
			{
				if(!Draw.m_Rect.Inside(Point))
					continue;
				const vec2 Local = Point - Draw.m_Rect.TopLeft();
				const bool Right = Local.x > Draw.m_Rect.w * 0.5f;
				const bool Bottom = Local.y > Draw.m_Rect.h * 0.5f;
				const int Corner = Bottom ? (Right ? CORNER_BR : CORNER_BL) : (Right ? CORNER_TR : CORNER_TL);
				const float Radius = Draw.m_Corners & Corner ? Draw.m_Radius : 0.0f;
				const vec2 Distance(std::max(0.0f, Radius - std::min(Local.x, Draw.m_Rect.w - Local.x)),
					std::max(0.0f, Radius - std::min(Local.y, Draw.m_Rect.h - Local.y)));
				if(dot(Distance, Distance) <= Radius * Radius)
					Blend(Draw.m_Color);
			}
			for(const STriangle &Triangle : m_vTriangles)
			{
				const vec2 A = Triangle.m_aPoints[0], B = Triangle.m_aPoints[1], C = Triangle.m_aPoints[2];
				const float Area = Cross(B - A, C - A);
				if(std::abs(Area) < 0.000001f)
					continue;
				const float U = Cross(B - Point, C - Point) / Area;
				const float V = Cross(C - Point, A - Point) / Area;
				const float W = 1.0f - U - V;
				if(U < 0.0f || V < 0.0f || W < 0.0f)
					continue;
				const auto &aColors = Triangle.m_aColors;
				Blend(ColorRGBA(aColors[0].r * U + aColors[1].r * V + aColors[2].r * W,
					aColors[0].g * U + aColors[1].g * V + aColors[2].g * W,
					aColors[0].b * U + aColors[1].b * V + aColors[2].b * W,
					aColors[0].a * U + aColors[1].a * V + aColors[2].a * W));
			}
			if(Result.a > 0.0f)
			{
				Result.r /= Result.a;
				Result.g /= Result.a;
				Result.b /= Result.a;
			}
			return Result;
		}
	};

	class CQmRoundedSurfaceRender : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;

	protected:
		CSurfaceGraphics m_Graphics;
		const CUIRect m_Rect{0, 0, 30, 20};
		SRoundedSurfaceParams m_Params;

		void SetUp() override
		{
			g_Config.m_QmRectCornerSegments = 16;
			m_Params.m_Radius = 4.0f;
			m_Params.m_BorderWidth = 1.0f;
			m_Params.m_PixelSize = 1.0f;
		}
		void TearDown() override { g_Config = m_PreviousConfig; }

		static void ExpectColor(ColorRGBA Actual, ColorRGBA Expected)
		{
			EXPECT_NEAR(Actual.r, Expected.r, 0.00001f);
			EXPECT_NEAR(Actual.g, Expected.g, 0.00001f);
			EXPECT_NEAR(Actual.b, Expected.b, 0.00001f);
			EXPECT_NEAR(Actual.a, Expected.a, 0.00001f);
		}
	};

}

TEST_F(CQmRoundedSurfaceRender, TransparentFillLeavesInteriorUnpainted)
{
	ASSERT_TRUE(DrawRoundedSurface(&m_Graphics, m_Rect, ColorRGBA(1, 0, 0, 0), ColorRGBA(0, 0, 1, 0.7f), m_Params));
	ExpectColor(m_Graphics.Sample(vec2(10.37f, 6.23f)), ColorRGBA(0, 0, 0, 0));
	ExpectColor(m_Graphics.Sample(vec2(10.37f, 0.43f)), ColorRGBA(0, 0, 1, 0.7f));
	EXPECT_EQ(m_Graphics.m_AntialiasDraws, 1);
}

TEST_F(CQmRoundedSurfaceRender, TranslucentFillDoesNotBlendWithBorderUnderneath)
{
	const ColorRGBA Fill(1, 0, 0, 0.25f);
	for(const ColorRGBA Border : {ColorRGBA(1, 1, 1, 0.08f), ColorRGBA(0, 0, 0, 0.7f), ColorRGBA(0, 0, 1, 1), ColorRGBA(0, 0, 0, 0)})
	{
		CSurfaceGraphics Graphics;
		ASSERT_TRUE(DrawRoundedSurface(&Graphics, m_Rect, Fill, Border, m_Params));
		ExpectColor(Graphics.Sample(vec2(10.37f, 6.23f)), Fill);
	}
}

TEST_F(CQmRoundedSurfaceRender, BorderCoversEdgesForEachCornerMaskAndQuality)
{
	const ColorRGBA Fill(1, 0, 0, 0.25f), Border(0, 0, 1, 0.7f);
	for(int Segments : {8, 16, 48})
	{
		g_Config.m_QmRectCornerSegments = Segments;
		for(int Corners = 0; Corners <= IGraphics::CORNER_ALL; ++Corners)
		{
			SCOPED_TRACE(::testing::Message() << "segments=" << Segments << " corners=" << Corners);
			CSurfaceGraphics Graphics;
			m_Params.m_Corners = Corners;
			ASSERT_TRUE(DrawRoundedSurface(&Graphics, m_Rect, Fill, Border, m_Params));
			ExpectColor(Graphics.Sample(vec2(10.37f, 6.23f)), Fill);
			ExpectColor(Graphics.Sample(vec2(1.43f, 9.13f)), Fill);
			ExpectColor(Graphics.Sample(vec2(10.37f, 0.43f)), Border);
			ExpectColor(Graphics.Sample(vec2(29.57f, 9.13f)), Border);
			ExpectColor(Graphics.Sample(vec2(10.37f, 19.57f)), Border);
			ExpectColor(Graphics.Sample(vec2(0.43f, 9.13f)), Border);
			for(const auto &Corner : std::array<std::pair<int, vec2>, 4>{{{IGraphics::CORNER_TL, vec2(0, 0)}, {IGraphics::CORNER_TR, vec2(30, 0)}, {IGraphics::CORNER_BR, vec2(30, 20)}, {IGraphics::CORNER_BL, vec2(0, 20)}}})
			{
				const vec2 Direction(Corner.second.x > 0 ? -1.0f : 1.0f, Corner.second.y > 0 ? -1.0f : 1.0f);
				const vec2 Point = Corner.second + Direction * (Corners & Corner.first ? vec2(1.21f, 1.17f) : vec2(0.43f, 1.17f));
				ExpectColor(Graphics.Sample(Point), Border);
				const vec2 Outside = Corner.second + Direction * vec2(0.31f, 0.27f);
				ExpectColor(Graphics.Sample(Outside), Corners & Corner.first ? ColorRGBA(0, 0, 0, 0) : Border);
			}
		}
	}
}

TEST_F(CQmRoundedSurfaceRender, BorderWiderThanRadiusKeepsInnerSquareClear)
{
	m_Params.m_Radius = 1.0f;
	m_Params.m_BorderWidth = 3.0f;
	const ColorRGBA Fill(1, 0, 0, 0.25f), Border(0, 0, 1, 0.7f);
	ASSERT_TRUE(DrawRoundedSurface(&m_Graphics, m_Rect, Fill, Border, m_Params));
	ExpectColor(m_Graphics.Sample(vec2(3.13f, 3.27f)), Fill);
	ExpectColor(m_Graphics.Sample(vec2(2.73f, 3.27f)), Border);
}

TEST_F(CQmRoundedSurfaceRender, PixelScalePreservesSeparateFillAndBorderRegions)
{
	const ColorRGBA Fill(1, 0, 0, 0.25f), Border(0, 0, 1, 0.7f);
	for(float PixelSize : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(PixelSize);
		CSurfaceGraphics Graphics;
		m_Params.m_PixelSize = PixelSize;
		m_Params.m_Radius = 4.0f * PixelSize;
		m_Params.m_BorderWidth = PixelSize;
		ASSERT_TRUE(DrawRoundedSurface(&Graphics, CUIRect{0, 0, 30 * PixelSize, 20 * PixelSize}, Fill, Border, m_Params));
		ExpectColor(Graphics.Sample(vec2(10.37f, 6.23f) * PixelSize), Fill);
		ExpectColor(Graphics.Sample(vec2(10.37f, 0.43f) * PixelSize), Border);
		ExpectColor(Graphics.Sample(vec2(1.21f, 1.17f) * PixelSize), Border);
	}
}

TEST_F(CQmRoundedSurfaceRender, BorderConsumingSmallInteriorPaintsOnlyBorder)
{
	m_Params.m_Radius = 8.0f;
	m_Params.m_BorderWidth = 9.0f;
	const ColorRGBA Border(0, 0, 1, 0.7f);
	ASSERT_TRUE(DrawRoundedSurface(&m_Graphics, CUIRect{0, 0, 6, 4}, ColorRGBA(1, 0, 0, 0.25f), Border, m_Params));
	ExpectColor(m_Graphics.Sample(vec2(3.13f, 2.27f)), Border);
}

TEST_F(CQmRoundedSurfaceRender, ZeroBorderKeepsFillAcrossSurface)
{
	m_Params.m_BorderWidth = 0.0f;
	const ColorRGBA Fill(1, 0, 0, 0.25f);
	ASSERT_TRUE(DrawRoundedSurface(&m_Graphics, m_Rect, Fill, ColorRGBA(0, 0, 1, 1), m_Params));
	ExpectColor(m_Graphics.Sample(vec2(10.37f, 6.23f)), Fill);
	ExpectColor(m_Graphics.Sample(vec2(10.37f, 0.43f)), Fill);
}

TEST_F(CQmRoundedSurfaceRender, SdfReceivesIndependentFillAndBorderAlpha)
{
	m_Graphics.m_HasSdf = true;
	m_Params.m_Corners = IGraphics::CORNER_R;
	ASSERT_TRUE(DrawRoundedSurface(&m_Graphics, m_Rect, ColorRGBA(1, 0, 0, 0.25f), ColorRGBA(0, 0, 1, 0.7f), m_Params));
	ASSERT_EQ(m_Graphics.m_vSdf.size(), 1u);
	const auto &Sdf = m_Graphics.m_vSdf[0];
	EXPECT_EQ(Sdf.m_FillColor, vec4(1, 0, 0, 0.25f));
	EXPECT_EQ(Sdf.m_BorderColor, vec4(0, 0, 1, 0.7f));
	EXPECT_EQ(Sdf.m_CornerRadii, vec4(0, 4, 4, 0));
	EXPECT_FLOAT_EQ(Sdf.m_Params.x, 1.0f);
}

TEST_F(CQmRoundedSurfaceRender, TranslucentGradientDoesNotHaveBorderBehindInterior)
{
	for(bool HasSdf : {false, true})
	{
		CSurfaceGraphics Graphics;
		Graphics.m_HasSdf = HasSdf;
		SQmColorGradient Gradient;
		Gradient.m_NumColors = 2;
		Gradient.m_aColors[0] = ColorRGBA(1, 0, 0, 0.4f);
		Gradient.m_aColors[1] = ColorRGBA(0, 1, 0, 0.4f);
		ASSERT_TRUE(DrawRoundedGradientSurface(&Graphics, m_Rect, Gradient, 0.5f, ColorRGBA(0, 0, 1, 0.7f), m_Params));
		const vec2 Point(10.37f, 6.23f);
		if(HasSdf)
		{
			ASSERT_EQ(Graphics.m_vSdf.size(), 1u);
			const auto &Sdf = Graphics.m_vSdf[0];
			EXPECT_FLOAT_EQ(Sdf.m_GradientParams2.x, 1.0f);
			EXPECT_FLOAT_EQ(Sdf.m_GradientParams0.w, Gradient.m_Range);
			EXPECT_FLOAT_EQ(Sdf.m_GradientParams1.w, 2.0f);
			EXPECT_EQ(Sdf.m_aGradientColors[0], vec4(1, 0, 0, 0.2f));
			EXPECT_EQ(Sdf.m_aGradientColors[1], vec4(0, 1, 0, 0.2f));
			EXPECT_FLOAT_EQ(Sdf.m_BorderColor.a, 0.7f);
		}
		else
		{
			ExpectColor(Graphics.Sample(Point), Gradient.Sample((Point - vec2(1, 1)) / vec2(28, 18)).WithMultipliedAlpha(0.5f));
		}
	}
}
