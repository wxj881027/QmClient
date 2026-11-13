#include <game/map/render_layer.h>
#include <game/map/render_map.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <limits>

namespace
{
	class CScreenGraphics : public CIconBenchmarkGraphics
	{
	public:
		float m_X0 = 0, m_Y0 = 0, m_X1 = 64, m_Y1 = 64;
		int m_Begins = 0, m_Ends = 0, m_ImmediateDraws = 0, m_BufferVertices = 0, m_Borders = 0;
		bool HasTextureArraysSupport() const override { return false; }
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_X0;
			*pY0 = m_Y0;
			*pX1 = m_X1;
			*pY1 = m_Y1;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override {}
		void QuadsBegin() override { ++m_Begins; }
		void QuadsEnd() override { ++m_Ends; }
		void QuadsDrawTL(const CQuadItem *pArray, int Num) override { m_ImmediateDraws += Num; }
		void RenderTileLayer(int BufferContainerIndex, const ColorRGBA &Color, char **pOffsets, unsigned int *pCounts, size_t Count) override
		{
			for(size_t i = 0; i < Count; ++i)
				m_BufferVertices += pCounts[i];
		}
		void RenderBorderTiles(int BufferContainerIndex, const ColorRGBA &Color, char *pOffset, const vec2 &Offset, const vec2 &Scale, uint32_t DrawNum) override { ++m_Borders; }
	};

	// 只准备四个瓦片的索引元数据；可见范围、合并和边框调用由真实图层执行。
	class CBufferedLayer : public CRenderLayerTile
	{
	public:
		explicit CBufferedLayer(CMapItemLayerTilemap &Item, IGraphics &Graphics) :
			CRenderLayerTile(0, 0, 0, &Item)
		{
			CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
			m_VisualTiles.emplace();
			auto &Visuals = *m_VisualTiles;
			Visuals.m_Width = Visuals.m_Height = 2;
			Visuals.m_BufferContainerIndex = 1;
			Visuals.m_vTilesOfLayer.resize(4);
			for(int i = 0; i < 4; ++i)
			{
				Visuals.m_vTilesOfLayer[i].SetIndexBufferByteOffset(i);
				Visuals.m_vTilesOfLayer[i].Draw(true);
			}
			Visuals.m_BorderKillTile.Draw(true);
			Visuals.m_BorderTopLeft.Draw(true);
			Visuals.m_vBorderLeft.resize(2);
			Visuals.m_vBorderTop.resize(2);
		}
		void Buffered(bool Border = false)
		{
			CRenderLayerParams Params{};
			Params.m_RenderTileBorder = Border;
			RenderTileLayer(ColorRGBA(1, 1, 1, 1), Params);
		}
		void KillBorder() { RenderKillTileBorder(ColorRGBA(1, 1, 1, 1)); }
		void EmptyClip() { m_LayerClip = CClipRegion(0, 0, 0, 32); }
		size_t PreparedRowCapacity() const { return m_vpRenderTileLayerIndexOffsets.capacity(); }
	};
}

TEST(MapRenderScreenIntegration, EveryImmediateTileEntryRejectsInvalidScreenBeforeDeviceOrTextUse)
{
	CScreenGraphics Graphics;
	Graphics.m_X1 = std::numeric_limits<float>::infinity();
	CRenderMap Map;
	Map.Init(&Graphics, nullptr);
	const ColorRGBA Color(1, 1, 1, 1);
	Map.RenderTileRectangle(0, 0, 1, 1, 1, 1, 32, Color, LAYERRENDERFLAG_TRANSPARENT);
	Map.RenderTilemap(nullptr, 1, 1, 32, Color, LAYERRENDERFLAG_OPAQUE);
	Map.RenderTelemap(nullptr, 1, 1, 32, Color, LAYERRENDERFLAG_OPAQUE);
	Map.RenderSwitchmap(nullptr, 1, 1, 32, Color, LAYERRENDERFLAG_OPAQUE);
	Map.RenderTunemap(nullptr, 1, 1, 32, Color, LAYERRENDERFLAG_OPAQUE, nullptr);
	Map.RenderTeleOverlay(nullptr, 1, 1, 32, OVERLAYRENDERFLAG_TEXT, 1);
	Map.RenderSpeedupOverlay(nullptr, 1, 1, 32, OVERLAYRENDERFLAG_TEXT, 1);
	Map.RenderSwitchOverlay(nullptr, 1, 1, 32, OVERLAYRENDERFLAG_TEXT, 1);
	Map.RenderTuneOverlay(nullptr, 1, 1, 32, OVERLAYRENDERFLAG_TEXT, 1);
	EXPECT_EQ(Graphics.m_Begins, 0);
	EXPECT_EQ(Graphics.m_Ends, 0);
	EXPECT_EQ(Graphics.m_ImmediateDraws, 0);
}

TEST(MapRenderScreenIntegration, NormalImmediateTileStillDrawsAndBalancesItsBatch)
{
	CScreenGraphics Graphics;
	CRenderMap Map;
	Map.Init(&Graphics, nullptr);
	CTile Tile{};
	Tile.m_Index = 1;
	Tile.m_Flags = TILEFLAG_OPAQUE;
	Map.RenderTilemap(&Tile, 1, 1, 32, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_OPAQUE);
	EXPECT_EQ(Graphics.m_ImmediateDraws, 1);
	EXPECT_EQ(Graphics.m_Begins, 1);
	EXPECT_EQ(Graphics.m_Ends, 1);
}

TEST(MapRenderScreenIntegration, FiniteOutOfRangeViewportNeverStartsAnImmediateBatch)
{
	CScreenGraphics Graphics;
	Graphics.m_X1 = 1.0e30f;
	CRenderMap Map;
	Map.Init(&Graphics, nullptr);
	Map.RenderTilemap(nullptr, 1, 1, 32, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_OPAQUE);
	EXPECT_EQ(Graphics.m_Begins, 0);
}

TEST(MapRenderScreenIntegration, ImmediateFailureDoesNotLatchOutTheNextValidFrame)
{
	CScreenGraphics Graphics;
	CRenderMap Map;
	Map.Init(&Graphics, nullptr);
	Graphics.m_X1 = std::numeric_limits<float>::quiet_NaN();
	Map.RenderTileRectangle(0, 0, 1, 1, 1, 0, 32, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_TRANSPARENT);
	Graphics.m_X1 = 64;
	Map.RenderTileRectangle(0, 0, 1, 1, 1, 0, 32, ColorRGBA(1, 1, 1, 1), LAYERRENDERFLAG_TRANSPARENT);
	EXPECT_EQ(Graphics.m_ImmediateDraws, 1);
	EXPECT_EQ(Graphics.m_Begins, Graphics.m_Ends);
}

TEST(MapRenderScreenIntegration, BufferedLayerRejectsNonFiniteScreenWithoutSubmittingRows)
{
	CScreenGraphics Graphics;
	CMapItemLayerTilemap Item{};
	CBufferedLayer Layer(Item, Graphics);
	Graphics.m_X1 = std::numeric_limits<float>::infinity();
	Layer.Buffered(true);
	EXPECT_EQ(Graphics.m_BufferVertices, 0);
	EXPECT_EQ(Graphics.m_Borders, 0);
	EXPECT_EQ(Layer.PreparedRowCapacity(), 0U);
}

TEST(MapRenderScreenIntegration, NormalBufferedLayerSubmitsAllFourTileIndices)
{
	CScreenGraphics Graphics;
	CMapItemLayerTilemap Item{};
	CBufferedLayer Layer(Item, Graphics);
	Layer.Buffered();
	EXPECT_EQ(Graphics.m_BufferVertices, 24);
}

TEST(MapRenderScreenIntegration, EmptyVerticalMapIntersectionDoesNotReserveOffscreenRows)
{
	CScreenGraphics Graphics;
	Graphics.m_Y0 = -6.0e10f;
	Graphics.m_Y1 = -5.99999e10f;
	CMapItemLayerTilemap Item{};
	CBufferedLayer Layer(Item, Graphics);
	Layer.Buffered();
	EXPECT_EQ(Graphics.m_BufferVertices, 0);
	EXPECT_EQ(Layer.PreparedRowCapacity(), 0U);
}

TEST(MapRenderScreenIntegration, KillBorderRejectsNonFiniteScreenBeforeSubmittingGeometry)
{
	CScreenGraphics Graphics;
	Graphics.m_X0 = -std::numeric_limits<float>::infinity();
	CMapItemLayerTilemap Item{};
	CBufferedLayer Layer(Item, Graphics);
	Layer.KillBorder();
	EXPECT_EQ(Graphics.m_Borders, 0);
}

TEST(MapRenderScreenIntegration, EmptyLayerClipDoesNotSuppressTheIndependentMapBorder)
{
	CScreenGraphics Graphics;
	Graphics.m_X0 = Graphics.m_Y0 = -32;
	CMapItemLayerTilemap Item{};
	CBufferedLayer Layer(Item, Graphics);
	Layer.EmptyClip();
	Layer.Buffered(true);
	EXPECT_EQ(Graphics.m_BufferVertices, 0);
	EXPECT_GT(Graphics.m_Borders, 0);
}
