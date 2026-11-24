#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace
{
	class CEntityPassGraphics : public CIconBenchmarkGraphics
	{
		std::unordered_map<int, size_t> m_AttributeCounts;

	public:
		struct SDraw
		{
			ColorRGBA m_Color;
		};
		std::vector<SDraw> m_vDraws;

		CTextureHandle MakeTexture() { return CreateTextureHandle(m_NextId++); }
		bool IsTileBufferingEnabled() override { return true; }
		bool IsQuadBufferingEnabled() override { return true; }
		void WrapClamp() override {}
		void WrapNormal() override {}
		void TextureSet(CTextureHandle) override {}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = *pY0 = 0.0f;
			*pX1 = *pY1 = 64.0f;
		}

		int CreateBufferContainer(SBufferContainerInfo *pInfo) override
		{
			const int Container = CIconBenchmarkGraphics::CreateBufferContainer(pInfo);
			m_AttributeCounts.emplace(Container, pInfo->m_vAttributes.size());
			return Container;
		}
		void RenderTileLayer(int, const ColorRGBA &Color, char **, unsigned int *pDrawCounts, size_t NumOffsets) override
		{
			for(size_t i = 0; i < NumOffsets; ++i)
			{
				if(pDrawCounts[i] > 0)
				{
					m_vDraws.push_back({Color});
					return;
				}
			}
		}
		void TrianglesBegin() override {}
		void TrianglesEnd() override {}
		void QuadsSetSubsetFree(float, float, float, float, float, float, float, float, int) override {}
		void SetColorVertex(const CColorVertex *, size_t) override {}
	};

	class CEntityPassImages : public IMapImages
	{
	public:
		IGraphics::CTextureHandle Get(int) const override { return {}; }
		int Num() const override { return 0; }
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType) override { return {}; }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return {}; }
		IGraphics::CTextureHandle GetTuneColors() override { return {}; }
		IGraphics::CTextureHandle GetOverlayBottom() override { return {}; }
		IGraphics::CTextureHandle GetOverlayTop() override { return {}; }
		IGraphics::CTextureHandle GetOverlayCenter() override { return {}; }
	};

	class CEntityPassEnvelopeEval : public IEnvelopeEval
	{
		void EnvelopeEval(int, int, ColorRGBA &, size_t) const override {}
	};

	class MapEntityOverlayAlphaIntegration : public ::testing::Test
	{
	protected:
		CEntityPassGraphics m_Graphics;
		CEntityPassImages m_Images;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CMap m_Map;
		CRenderMap m_RenderMap;
		CEntityPassEnvelopeEval m_EnvEval;
		std::shared_ptr<CEnvelopeManager> m_pEnvelopes;
		CMapItemLayerTilemap m_Item{};
		std::unique_ptr<CRenderLayerEntityGame> m_pLayer;
		int m_OldFreezeAlpha = 100;
		int m_OldDeathAlpha = 100;
		int m_OldUnfreezeAlpha = 100;
		int m_OldDeepFreezeAlpha = 100;
		int m_OldDeepUnfreezeAlpha = 100;

		void SetUp() override
		{
			m_OldFreezeAlpha = g_Config.m_QmEntityOverlayFreezeAlpha;
			m_OldDeathAlpha = g_Config.m_QmEntityOverlayDeathAlpha;
			m_OldUnfreezeAlpha = g_Config.m_QmEntityOverlayUnfreezeAlpha;
			m_OldDeepFreezeAlpha = g_Config.m_QmEntityOverlayDeepFreezeAlpha;
			m_OldDeepUnfreezeAlpha = g_Config.m_QmEntityOverlayDeepUnfreezeAlpha;

			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "entity-overlay-alpha.map"));
			CTile aTiles[2] = {};
			aTiles[0].m_Index = TILE_FREEZE;
			aTiles[1].m_Index = TILE_DEATH;
			const int DataIndex = Writer.AddData(sizeof(aTiles), aTiles);
			ASSERT_GE(DataIndex, 0);
			ASSERT_TRUE(Writer.Finish());
			ASSERT_TRUE(m_Map.GetReader()->Open(m_pStorage.get(), "entity-overlay-alpha.map", IStorage::TYPE_SAVE));

			m_Item.m_Width = 2;
			m_Item.m_Height = 1;
			m_Item.m_Image = -1;
			m_Item.m_Color = CColor(255, 255, 255, 255);
			m_Item.m_ColorEnv = -1;
			m_Item.m_Flags = TILESLAYERFLAG_GAME;
			m_Item.m_Data = DataIndex;
			m_pEnvelopes = std::make_shared<CEnvelopeManager>(&m_EnvEval, &m_Map);
			m_RenderMap.Init(&m_Graphics, nullptr);
		}

		void TearDown() override
		{
			if(m_pLayer)
				m_pLayer->Unload();
			g_Config.m_QmEntityOverlayFreezeAlpha = m_OldFreezeAlpha;
			g_Config.m_QmEntityOverlayDeathAlpha = m_OldDeathAlpha;
			g_Config.m_QmEntityOverlayUnfreezeAlpha = m_OldUnfreezeAlpha;
			g_Config.m_QmEntityOverlayDeepFreezeAlpha = m_OldDeepFreezeAlpha;
			g_Config.m_QmEntityOverlayDeepUnfreezeAlpha = m_OldDeepUnfreezeAlpha;
		}

		void Prepare()
		{
			m_pLayer = std::make_unique<CRenderLayerEntityGame>(0, 0, TILESLAYERFLAG_GAME, &m_Item);
			m_pLayer->OnInit(&m_Graphics, nullptr, &m_RenderMap, m_pEnvelopes, &m_Map, &m_Images, true);
			ASSERT_TRUE(m_pLayer->IsValid());
			m_pLayer->Prepare().Finish();
		}

		std::vector<ColorRGBA> Draw(int OverlayPercent)
		{
			m_Graphics.m_vDraws.clear();
			CRenderLayerParams Params{};
			Params.m_RenderType = RENDERTYPE_FOREGROUND;
			Params.m_EntityOverlayVal = OverlayPercent;
			Params.m_RenderText = false;
			Params.m_RenderTileBorder = false;
			EXPECT_TRUE(m_pLayer->DoRender(Params));
			m_pLayer->Render(Params);

			std::vector<ColorRGBA> vColors;
			vColors.reserve(m_Graphics.m_vDraws.size());
			for(const auto &Draw : m_Graphics.m_vDraws)
				vColors.push_back(Draw.m_Color);
			return vColors;
		}
	};
} // namespace

TEST_F(MapEntityOverlayAlphaIntegration, CategoryOpacityScalesWithGlobalOverlay)
{
	g_Config.m_QmEntityOverlayDeathAlpha = 100;
	g_Config.m_QmEntityOverlayFreezeAlpha = 100;
	ASSERT_NO_FATAL_FAILURE(Prepare());

	const auto vColors = Draw(50);
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_FLOAT_EQ(vColors[0].a, 0.5f); // death
	EXPECT_FLOAT_EQ(vColors[1].a, 0.5f); // freeze
}

TEST_F(MapEntityOverlayAlphaIntegration, CategoryOpacityMultipliesOnTopOfGlobalOverlay)
{
	g_Config.m_QmEntityOverlayDeathAlpha = 100;
	g_Config.m_QmEntityOverlayFreezeAlpha = 60;
	ASSERT_NO_FATAL_FAILURE(Prepare());

	const auto vColors = Draw(50);
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_FLOAT_EQ(vColors[0].a, 0.5f); // death unchanged
	EXPECT_FLOAT_EQ(vColors[1].a, 0.3f); // 0.5 * 0.6
}
