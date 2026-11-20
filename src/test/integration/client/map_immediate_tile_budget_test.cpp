#include <game/map/render_layer.h>
#include <game/map/render_map.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <stdexcept>
#include <vector>

namespace
{
	class CImmediateTileGraphics : public CIconBenchmarkGraphics
	{
	public:
		float m_Min = 0.0f;
		float m_Max = 32.0f;
		int m_Begins = 0;
		int m_Ends = 0;
		std::vector<CQuadItem> m_vDraws;

		bool HasTextureArraysSupport() const override { return false; }
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = *pY0 = m_Min;
			*pX1 = *pY1 = m_Max;
		}
		void QuadsBegin() override { ++m_Begins; }
		void QuadsEnd() override { ++m_Ends; }
		void QuadsSetSubsetFree(float, float, float, float, float, float, float, float, int) override {}
		void BlendNone() override {}
		void BlendNormal() override {}
		void QuadsDrawTL(const CQuadItem *pArray, int Num) override
		{
			// 回归时在第一次异常密集的提交里结束测试，避免测试自身卡住。
			if(m_vDraws.size() + Num > 32)
				throw std::length_error("unexpected repeated tile draws");
			m_vDraws.insert(m_vDraws.end(), pArray, pArray + Num);
		}
	};

	class CImmediateDesignLayer : public CRenderLayerTile
	{
	public:
		CImmediateDesignLayer(CMapItemLayerTilemap &Item, CTile *pTiles, CImmediateTileGraphics &Graphics, CRenderMap &RenderMap) :
			CRenderLayerTile(0, 0, 0, &Item)
		{
			CRenderComponent::OnInit(&Graphics, nullptr, &RenderMap);
			m_pTiles = pTiles;
		}

		void Draw(ERenderType RenderType, bool TileAndQuadBuffering)
		{
			m_TileAndQuadBuffering = TileAndQuadBuffering;
			CRenderLayerParams Params{};
			Params.m_RenderType = RenderType;
			Params.m_RenderTileBorder = true;
			RenderTileLayerNoTileBuffer(ColorRGBA(1, 1, 1, 1), Params);
		}
	};

	enum class ETilemap
	{
		DESIGN,
		TELE,
		SWITCH,
		TUNE,
	};

	class MapImmediateTilemapBudgetIntegration : public ::testing::TestWithParam<ETilemap>
	{
	protected:
		CImmediateTileGraphics m_Graphics;
		CRenderMap m_RenderMap;

		void SetUp() override { m_RenderMap.Init(&m_Graphics, nullptr); }

		void DrawExtendedTilemap()
		{
			const ColorRGBA Color(1, 1, 1, 1);
			const int Flags = TILERENDERFLAG_EXTEND | TILERENDERFLAG_LIMIT_IMMEDIATE_WORK | LAYERRENDERFLAG_TRANSPARENT;
			switch(GetParam())
			{
			case ETilemap::DESIGN:
			{
				CTile Tile{};
				Tile.m_Index = TILE_SOLID;
				m_RenderMap.RenderTilemap(&Tile, 1, 1, 32.0f, Color, Flags);
				break;
			}
			case ETilemap::TELE:
			{
				CTeleTile Tile{};
				Tile.m_Type = TILE_TELEIN;
				m_RenderMap.RenderTelemap(&Tile, 1, 1, 32.0f, Color, Flags);
				break;
			}
			case ETilemap::SWITCH:
			{
				CSwitchTile Tile{};
				Tile.m_Type = TILE_SWITCHOPEN;
				m_RenderMap.RenderSwitchmap(&Tile, 1, 1, 32.0f, Color, Flags);
				break;
			}
			case ETilemap::TUNE:
			{
				CTuneTile Tile{};
				Tile.m_Type = TILE_TUNE;
				m_RenderMap.RenderTunemap(&Tile, 1, 1, 32.0f, Color, Flags, nullptr);
				break;
			}
			}
		}
	};
}

TEST_P(MapImmediateTilemapBudgetIntegration, WideViewportStillDrawsTheMapWithoutRepeatingItsEdges)
{
	m_Graphics.m_Min = -64000.0f;
	m_Graphics.m_Max = 64000.0f;
	ASSERT_NO_THROW(DrawExtendedTilemap());
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 1U);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[0].m_X, 0.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws[0].m_Y, 0.0f);
	EXPECT_EQ(m_Graphics.m_Begins, 1);
	EXPECT_EQ(m_Graphics.m_Ends, 1);
}

TEST_P(MapImmediateTilemapBudgetIntegration, NormalViewportStillRepeatsTheEdges)
{
	DrawExtendedTilemap();
	// 一格视口按原来的规则向四周各扩一格。
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 9U);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws.front().m_X, -32.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws.front().m_Y, -32.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws.back().m_X, 32.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vDraws.back().m_Y, 32.0f);
}

INSTANTIATE_TEST_SUITE_P(Tilemaps, MapImmediateTilemapBudgetIntegration, ::testing::Values(ETilemap::DESIGN, ETilemap::TELE, ETilemap::SWITCH, ETilemap::TUNE));

TEST(MapImmediateTileBudgetIntegration, LargeDesignLayerOnlyUsesTheBudgetForInteractiveDrawing)
{
	CImmediateTileGraphics Graphics;
	CRenderMap RenderMap;
	RenderMap.Init(&Graphics, nullptr);
	constexpr int Side = 1536;
	Graphics.m_Max = Side * 32.0f;
	CTile EmptyTile{};
	EmptyTile.m_Skip = 254;
	std::vector<CTile> vTiles(Side * Side, EmptyTile);
	vTiles.front() = {};
	vTiles.front().m_Index = TILE_SOLID;
	CMapItemLayerTilemap Item{};
	Item.m_Width = Item.m_Height = Side;
	CImmediateDesignLayer Layer(Item, vTiles.data(), Graphics, RenderMap);

	ASSERT_NO_THROW(Layer.Draw(RENDERTYPE_BACKGROUND, true));
	ASSERT_EQ(Graphics.m_Begins, 0);
	ASSERT_NO_THROW(Layer.Draw(RENDERTYPE_FULL_DESIGN, true));
	ASSERT_EQ(Graphics.m_Begins, 0);
	EXPECT_TRUE(Graphics.m_vDraws.empty());

	Layer.Draw(RENDERTYPE_FULL_DESIGN, false);
	ASSERT_EQ(Graphics.m_vDraws.size(), 4U);
	EXPECT_FLOAT_EQ(Graphics.m_vDraws.back().m_X, 0.0f);
	EXPECT_FLOAT_EQ(Graphics.m_vDraws.back().m_Y, 0.0f);
	EXPECT_EQ(Graphics.m_Begins, 2);
	EXPECT_EQ(Graphics.m_Ends, 2);
}

TEST(MapImmediateTileBudgetIntegration, SkippedDeathBorderDrawsAgainWhenTheViewportShrinks)
{
	CImmediateTileGraphics Graphics;
	CRenderMap RenderMap;
	RenderMap.Init(&Graphics, nullptr);
	Graphics.m_Min = -64000.0f;
	Graphics.m_Max = 64000.0f;
	const auto RenderDeathBorder = [&] {
		RenderMap.RenderTileRectangle(0, 0, 1, 1, TILE_AIR, TILE_DEATH, 32.0f, ColorRGBA(1, 1, 1, 1), TILERENDERFLAG_LIMIT_IMMEDIATE_WORK | LAYERRENDERFLAG_TRANSPARENT);
	};

	ASSERT_NO_THROW(RenderDeathBorder());
	ASSERT_EQ(Graphics.m_Begins, 0);
	Graphics.m_Min = 0.0f;
	Graphics.m_Max = 32.0f;
	RenderDeathBorder();
	EXPECT_EQ(Graphics.m_vDraws.size(), 8U);
	EXPECT_EQ(Graphics.m_Begins, 1);
	EXPECT_EQ(Graphics.m_Ends, 1);
}
