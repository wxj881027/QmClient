#include <game/client/QmUi/QmIslandSurface.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <vector>

namespace
{
	class CIslandFallbackGraphics final : public CIconBenchmarkGraphics
	{
	public:
		std::vector<CFreeformItem> m_vRingQuads;
		std::vector<float> m_vFillAlphas;

		bool HasMediaIslandSdf() override { return false; }
		bool HasRoundedRectSdf() override { return true; }
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override
		{
			m_vFillAlphas.push_back(Params.m_FillColor.w);
		}
		void QuadsDrawFreeform(const CFreeformItem *pQuads, int Num) override
		{
			m_vRingQuads.insert(m_vRingQuads.end(), pQuads, pQuads + Num);
		}
	};

	SHudMediaIslandSdfRenderState MakeOutlineRingState()
	{
		SHudMediaIslandSdfRenderState State;
		State.m_MainRect = {20.0f, 30.0f, 120.0f, 24.0f};
		State.m_MainRadius = State.m_MainRect.h * 0.5f;
		State.m_MainCorners = IGraphics::CORNER_ALL;
		State.m_BackgroundColor = ColorRGBA(0.10f, 0.11f, 0.14f, 0.82f);
		State.m_OutlineRingThickness = 2.0f;
		State.m_OutlineRingOffset = 2.5f;
		State.m_ItemCount = 1;
		auto &Item = State.m_Items[0];
		Item.m_Center = State.m_MainRect.Center();
		Item.m_Radii = State.m_MainRect.Size() * 0.5f;
		Item.m_ContentAlpha = 1.0f;
		Item.m_CountdownProgress = 0.5f;
		Item.m_RingColor = ColorRGBA(0.3f, 0.8f, 0.4f, 1.0f);
		State.m_Rect = QmHudMediaIslandSdfOuterRect(State);
		return State;
	}

	void ExpectFirstRingQuadCoversBothTriangles(const CIslandFallbackGraphics &Graphics)
	{
		ASSERT_FALSE(Graphics.m_vRingQuads.empty());
		const auto &Quad = Graphics.m_vRingQuads.front();
		const vec2 Edge1(Quad.m_X1 - Quad.m_X0, Quad.m_Y1 - Quad.m_Y0);
		const vec2 Edge2(Quad.m_X2 - Quad.m_X0, Quad.m_Y2 - Quad.m_Y0);
		const vec2 Diagonal(Quad.m_X3 - Quad.m_X0, Quad.m_Y3 - Quad.m_Y0);
		// Freeform 的两个三角形应沿对角线分居两侧，沿屏幕坐标顺时针绕行。
		EXPECT_GT(Edge1.x * Diagonal.y - Edge1.y * Diagonal.x, 0.0f);
		EXPECT_GT(Diagonal.x * Edge2.y - Diagonal.y * Edge2.x, 0.0f);
	}
}

TEST(QmIslandSurfaceFallback, OutlineRingUsesNonOverlappingTriangles)
{
	CIslandFallbackGraphics Graphics;
	ASSERT_TRUE(qm_island::Render(&Graphics, MakeOutlineRingState()));
	ExpectFirstRingQuadCoversBothTriangles(Graphics);
}

TEST(QmIslandSurfaceFallback, CircularRingUsesNonOverlappingTriangles)
{
	CIslandFallbackGraphics Graphics;
	auto State = MakeOutlineRingState();
	State.m_OutlineRingThickness = 0.0f;
	State.m_RingRadius = 8.0f;
	State.m_RingThickness = 2.0f;
	State.m_Items[0].m_Center = {160.0f, 42.0f};
	State.m_Items[0].m_Radii = {6.0f, 6.0f};
	State.m_Rect = QmHudMediaIslandSdfOuterRect(State);

	ASSERT_TRUE(qm_island::Render(&Graphics, State));
	ExpectFirstRingQuadCoversBothTriangles(Graphics);
}

TEST(QmIslandSurfaceFallback, CoincidentOutlineItemKeepsTheBodyTranslucent)
{
	CIslandFallbackGraphics Graphics;
	const auto State = MakeOutlineRingState();
	ASSERT_TRUE(qm_island::Render(&Graphics, State));
	ASSERT_EQ(Graphics.m_vFillAlphas.size(), 1u);
	EXPECT_FLOAT_EQ(Graphics.m_vFillAlphas.front(), State.m_BackgroundColor.a);
}
