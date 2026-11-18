#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace
{
	class CLayerTextureGraphics : public CIconBenchmarkGraphics
	{
		CTextureHandle m_BoundTexture;
		std::unordered_map<int, size_t> m_AttributeCounts;

		void RecordDraw(int Container)
		{
			m_vDraws.push_back({m_BoundTexture.Id(), m_AttributeCounts.at(Container)});
		}

	public:
		struct SDraw
		{
			int m_Texture;
			size_t m_Attributes;
		};
		std::vector<SDraw> m_vDraws;
		bool m_QuadBuffering = true;
		int m_ImmediateQuads = 0;

		CTextureHandle MakeTexture() { return CreateTextureHandle(m_NextId++); }
		bool IsTileBufferingEnabled() override { return true; }
		bool IsQuadBufferingEnabled() override { return m_QuadBuffering; }
		void WrapClamp() override {}
		void WrapNormal() override {}
		void TextureSet(CTextureHandle Texture) override { m_BoundTexture = Texture; }
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
		void RenderQuadLayer(int Container, SQuadRenderInfo *, size_t, int, bool) override { RecordDraw(Container); }
		void RenderTileLayer(int Container, const ColorRGBA &, char **, unsigned int *, size_t) override { RecordDraw(Container); }
		void TrianglesBegin() override {}
		void TrianglesEnd() override {}
		void QuadsSetSubsetFree(float, float, float, float, float, float, float, float, int) override {}
		void QuadsDrawFreeform(const CFreeformItem *, int Count) override { m_ImmediateQuads += Count; }
	};

	class CLayerTextureImages : public IMapImages
	{
	public:
		IGraphics::CTextureHandle m_Image;
		IGraphics::CTextureHandle m_Arrow;
		IGraphics::CTextureHandle m_Bottom;
		IGraphics::CTextureHandle m_Top;

		IGraphics::CTextureHandle Get(int) const override { return m_Image; }
		int Num() const override { return 1; }
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType) override { return {}; }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return m_Arrow; }
		IGraphics::CTextureHandle GetTuneColors() override { return {}; }
		IGraphics::CTextureHandle GetOverlayBottom() override { return m_Bottom; }
		IGraphics::CTextureHandle GetOverlayTop() override { return m_Top; }
		IGraphics::CTextureHandle GetOverlayCenter() override { return {}; }
	};

	constexpr int HIDDEN_ENVELOPE = 1;

	class CLayerResourceEnvelopeEval : public IEnvelopeEval
	{
		void EnvelopeEval(int, int Envelope, ColorRGBA &Result, size_t Channels) const override
		{
			if(Envelope == HIDDEN_ENVELOPE && Channels == 4)
				Result.a = 0.0f;
		}
	};

	class CReplaceableQuadCache : public CMap
	{
		int m_ReplacedIndex = -1;
		std::array<CQuad, 2> m_aReplacement;

	public:
		void RetireQuadData(int Index)
		{
			auto *pPrevious = static_cast<CQuad *>(CMap::GetDataSwapped(Index));
			ASSERT_NE(pPrevious, nullptr);
			for(size_t i = 0; i < m_aReplacement.size(); ++i)
			{
				m_aReplacement[i] = pPrevious[i];
				// 旧缓冲继续存活。将已退役的内容设为不可见，便于安全观察绘制是否仍使用它。
				pPrevious[i].m_ColorEnv = HIDDEN_ENVELOPE;
			}
			m_ReplacedIndex = Index;
		}

		void *GetDataSwapped(int Index) override
		{
			return Index == m_ReplacedIndex ? m_aReplacement.data() : CMap::GetDataSwapped(Index);
		}
	};

	class MapLayerRenderResourcesIntegration : public ::testing::Test
	{
	protected:
		CLayerTextureGraphics m_Graphics;
		CLayerTextureImages m_Images;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CReplaceableQuadCache m_Map;
		CRenderMap m_RenderMap;
		CLayerResourceEnvelopeEval m_EnvEval;
		std::shared_ptr<CEnvelopeManager> m_pEnvelopes;
		CMapItemLayerQuads m_QuadItem{};
		CMapItemLayerQuads m_AnimatedQuadItem{};
		CMapItemLayerTilemap m_SpeedupItem{};
		std::unique_ptr<CRenderLayer> m_pLayer;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "layer-resources.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

			CQuad Quad{};
			Quad.m_PosEnv = Quad.m_ColorEnv = -1;
			Quad.m_aPoints[1] = {i2fx(32), 0};
			Quad.m_aPoints[2] = {0, i2fx(32)};
			Quad.m_aPoints[3] = {i2fx(32), i2fx(32)};
			Quad.m_aPoints[4] = {i2fx(16), i2fx(16)};
			for(auto &Color : Quad.m_aColors)
				Color = CColor(255, 255, 255, 255);
			m_QuadItem.m_Image = 0;
			m_QuadItem.m_NumQuads = 1;
			m_QuadItem.m_Data = Writer.AddDataSwapped(sizeof(Quad), &Quad);

			CQuad aAnimatedQuads[2] = {Quad, Quad};
			aAnimatedQuads[1].m_ColorEnv = 0;
			m_AnimatedQuadItem.m_Image = -1;
			m_AnimatedQuadItem.m_NumQuads = 2;
			m_AnimatedQuadItem.m_Data = Writer.AddDataSwapped(sizeof(aAnimatedQuads), aAnimatedQuads);

			CSpeedupTile aTiles[4]{};
			aTiles[0].m_Type = TILE_SPEED_BOOST;
			aTiles[0].m_Force = 3;
			aTiles[0].m_MaxSpeed = 6;
			m_SpeedupItem.m_Width = m_SpeedupItem.m_Height = 2;
			m_SpeedupItem.m_Image = -1;
			m_SpeedupItem.m_Color = CColor(255, 255, 255, 255);
			m_SpeedupItem.m_ColorEnv = -1;
			m_SpeedupItem.m_Speedup = Writer.AddData(sizeof(aTiles), aTiles);

			ASSERT_TRUE(Writer.Finish());
			ASSERT_TRUE(m_Map.GetReader()->Open(m_pStorage.get(), "layer-resources.map", IStorage::TYPE_SAVE));
			m_pEnvelopes = std::make_shared<CEnvelopeManager>(&m_EnvEval, &m_Map);
			m_RenderMap.Init(&m_Graphics, nullptr);
		}

		void TearDown() override
		{
			if(m_pLayer)
				m_pLayer->Unload();
		}

		void Prepare()
		{
			m_pLayer->OnInit(&m_Graphics, nullptr, &m_RenderMap, m_pEnvelopes, &m_Map, &m_Images, true);
			ASSERT_TRUE(m_pLayer->IsValid());
			m_pLayer->Prepare().Finish();
		}

		void Draw()
		{
			m_Graphics.m_vDraws.clear();
			m_Graphics.m_ImmediateQuads = 0;
			CRenderLayerParams Params{};
			Params.m_RenderType = RENDERTYPE_FOREGROUND;
			Params.m_EntityOverlayVal = 50;
			Params.m_RenderText = true;
			ASSERT_TRUE(m_pLayer->DoRender(Params));
			m_pLayer->Render(Params);
		}
	};
}

TEST_F(MapLayerRenderResourcesIntegration, MissingQuadImageUsesTheUntexturedVertexLayout)
{
	m_pLayer = std::make_unique<CRenderLayerQuads>(0, 0, 0, &m_QuadItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());

	const auto &vDraws = m_Graphics.m_vDraws;
	ASSERT_EQ(vDraws.size(), 1u);
	EXPECT_EQ(vDraws[0].m_Texture, -1);
	EXPECT_EQ(vDraws[0].m_Attributes, 2u);
	EXPECT_EQ(m_Graphics.m_BufferBytes, 4u * (4 * sizeof(float) + 4 * sizeof(uint8_t)));
}

TEST_F(MapLayerRenderResourcesIntegration, LoadedQuadImageKeepsItsTextureCoordinates)
{
	m_Images.m_Image = m_Graphics.MakeTexture();
	m_pLayer = std::make_unique<CRenderLayerQuads>(0, 0, 0, &m_QuadItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());

	const auto &vDraws = m_Graphics.m_vDraws;
	ASSERT_EQ(vDraws.size(), 1u);
	EXPECT_EQ(vDraws[0].m_Texture, m_Images.m_Image.Id());
	EXPECT_EQ(vDraws[0].m_Attributes, 3u);
	EXPECT_EQ(m_Graphics.m_BufferBytes, 4u * (6 * sizeof(float) + 4 * sizeof(uint8_t)));
}

TEST_F(MapLayerRenderResourcesIntegration, UngroupedBufferedQuadsUseTheCurrentMapData)
{
	m_pLayer = std::make_unique<CRenderLayerQuads>(0, 0, 0, &m_AnimatedQuadItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());
	ASSERT_EQ(m_Graphics.m_vDraws.size(), 1u);

	ASSERT_NO_FATAL_FAILURE(m_Map.RetireQuadData(m_AnimatedQuadItem.m_Data));
	ASSERT_NO_FATAL_FAILURE(Draw());
	EXPECT_EQ(m_Graphics.m_vDraws.size(), 1u);
}

TEST_F(MapLayerRenderResourcesIntegration, ImmediateQuadsUseTheCurrentMapData)
{
	m_Graphics.m_QuadBuffering = false;
	m_pLayer = std::make_unique<CRenderLayerQuads>(0, 0, 0, &m_AnimatedQuadItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());
	ASSERT_EQ(m_Graphics.m_ImmediateQuads, 2);

	ASSERT_NO_FATAL_FAILURE(m_Map.RetireQuadData(m_AnimatedQuadItem.m_Data));
	ASSERT_NO_FATAL_FAILURE(Draw());
	EXPECT_EQ(m_Graphics.m_ImmediateQuads, 2);
}

TEST_F(MapLayerRenderResourcesIntegration, SpeedupNumberBatchesKeepUvsWhenArrowTextureIsMissing)
{
	m_Images.m_Bottom = m_Graphics.MakeTexture();
	m_Images.m_Top = m_Graphics.MakeTexture();
	m_pLayer = std::make_unique<CRenderLayerEntitySpeedup>(0, 0, 0, &m_SpeedupItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());

	const auto &vDraws = m_Graphics.m_vDraws;
	ASSERT_EQ(vDraws.size(), 3u);
	EXPECT_EQ(vDraws[0].m_Texture, -1);
	EXPECT_EQ(vDraws[0].m_Attributes, 1u);
	EXPECT_EQ(vDraws[1].m_Texture, m_Images.m_Bottom.Id());
	EXPECT_EQ(vDraws[1].m_Attributes, 2u);
	EXPECT_EQ(vDraws[2].m_Texture, m_Images.m_Top.Id());
	EXPECT_EQ(vDraws[2].m_Attributes, 2u);
}

TEST_F(MapLayerRenderResourcesIntegration, SpeedupNumberBatchCanDrawAgainWhenItsAtlasReturns)
{
	m_Images.m_Arrow = m_Graphics.MakeTexture();
	m_Images.m_Top = m_Graphics.MakeTexture();
	m_pLayer = std::make_unique<CRenderLayerEntitySpeedup>(0, 0, 0, &m_SpeedupItem);
	ASSERT_NO_FATAL_FAILURE(Prepare());
	ASSERT_NO_FATAL_FAILURE(Draw());

	const auto &vDraws = m_Graphics.m_vDraws;
	ASSERT_EQ(vDraws.size(), 2u);
	EXPECT_EQ(vDraws[0].m_Texture, m_Images.m_Arrow.Id());
	EXPECT_EQ(vDraws[1].m_Texture, m_Images.m_Top.Id());

	m_Images.m_Bottom = m_Graphics.MakeTexture();
	ASSERT_NO_FATAL_FAILURE(Draw());

	ASSERT_EQ(vDraws.size(), 3u);
	EXPECT_EQ(vDraws[1].m_Texture, m_Images.m_Bottom.Id());
	EXPECT_EQ(vDraws[1].m_Attributes, 2u);
	EXPECT_EQ(vDraws[2].m_Texture, m_Images.m_Top.Id());
}
