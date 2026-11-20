#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <chrono>

namespace
{
	class CQuadGraphics : public CIconBenchmarkGraphics
	{
	public:
		bool IsQuadBufferingEnabled() override { return true; }
		void RenderQuadLayer(int BufferContainerIndex, SQuadRenderInfo *pInfo, size_t NumQuads, int QuadOffset, bool Grouped) override
		{
			m_Quads += NumQuads;
		}
	};

	class CMidpointEnvelope : public IEnvelopeEval
	{
		CMapBasedEnvelopePointAccess m_Points;

	public:
		explicit CMidpointEnvelope(CMap &Map) :
			m_Points(&Map) {}

		void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const override
		{
			if(EnvelopeIndex == 0)
				CRenderMap::RenderEvalEnvelope(&m_Points, std::chrono::milliseconds(500 + TimeOffsetMillis), Result, Channels);
		}
	};

	CQuad MakeQuad(int CenterX, int PosEnv)
	{
		CQuad Quad{};
		Quad.m_PosEnv = PosEnv;
		Quad.m_ColorEnv = -1;
		Quad.m_aPoints[0] = {i2fx(CenterX - 1), 0};
		Quad.m_aPoints[1] = {i2fx(CenterX + 1), 0};
		Quad.m_aPoints[2] = {i2fx(CenterX - 1), i2fx(60)};
		Quad.m_aPoints[3] = {i2fx(CenterX + 1), i2fx(60)};
		Quad.m_aPoints[4] = {i2fx(CenterX), i2fx(30)};
		for(auto &Color : Quad.m_aColors)
			Color = CColor(255, 255, 255, 255);
		return Quad;
	}

	class CMapQuadCulling : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void ExpectVisibleQuads(const std::vector<CQuad> &vQuads, const CEnvPointBezier_upstream (&aPoints)[2], size_t Expected)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "quads.map"));
			CMapItemEnvelope Env{};
			Env.m_Version = CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER;
			Env.m_Channels = 3;
			Env.m_NumPoints = 2;
			Writer.AddItem(MAPITEMTYPE_ENVELOPE, 0, sizeof(Env), &Env);
			Writer.AddItem(MAPITEMTYPE_ENVPOINTS, 0, sizeof(aPoints), aPoints);
			CMapItemLayerQuads Item{};
			Item.m_Image = -1;
			Item.m_NumQuads = static_cast<int>(vQuads.size());
			Item.m_Data = Writer.AddDataSwapped(vQuads.size() * sizeof(CQuad), vQuads.data());
			Writer.Finish();

			CMap Map;
			ASSERT_TRUE(Map.GetReader()->Open(m_pStorage.get(), "quads.map", IStorage::TYPE_SAVE));
			CMidpointEnvelope Eval(Map);
			auto pManager = std::make_shared<CEnvelopeManager>(&Eval, &Map);
			CQuadGraphics Graphics;
			CRenderLayerQuads Layer(0, 0, 0, &Item);
			Layer.OnInit(&Graphics, nullptr, nullptr, pManager, &Map, nullptr, true);
			ASSERT_TRUE(Layer.IsValid());
			Layer.Prepare().Finish();
			CRenderLayerParams Params{};
			Params.m_RenderType = RENDERTYPE_FULL_DESIGN;
			if(Layer.DoRender(Params))
				Layer.Render(Params);
			EXPECT_EQ(Graphics.m_Quads, Expected);
			Layer.Unload();
		}
	};
}

TEST_F(CMapQuadCulling, UnknownClusterBeforeOrAfterAnOffscreenClusterRemainsDrawable)
{
	CEnvPointBezier_upstream aPoints[2]{};
	const CQuad Outside = MakeQuad(-400, -1);
	const CQuad Unknown = MakeQuad(400, 1);
	for(bool UnknownFirst : {false, true})
	{
		SCOPED_TRACE(UnknownFirst);
		std::vector<CQuad> vQuads(gs_GraphicsMaxQuadsRenderCount, UnknownFirst ? Unknown : Outside);
		vQuads.push_back(UnknownFirst ? Outside : Unknown);
		ExpectVisibleQuads(vQuads, aPoints, UnknownFirst ? gs_GraphicsMaxQuadsRenderCount : 1);
	}
}

TEST_F(CMapQuadCulling, BezierRotationOutsideKeyframeBoundsKeepsQuadDrawable)
{
	const auto Quad = MakeQuad(-10, 0);
	CEnvPointBezier_upstream aPoints[2]{};
	aPoints[0].m_Curvetype = CURVETYPE_BEZIER;
	aPoints[1].m_Time = CFixedTime(1000);
	ExpectVisibleQuads({Quad}, aPoints, 0);
	aPoints[0].m_Bezier.m_aOutTangentDeltaY[2] = i2fx(180);
	ExpectVisibleQuads({Quad}, aPoints, 1);
	aPoints[0].m_Bezier.m_aOutTangentDeltaY[2] = 0;
	aPoints[1].m_Bezier.m_aInTangentDeltaY[2] = i2fx(180);
	ExpectVisibleQuads({Quad}, aPoints, 1);
	aPoints[0].m_Curvetype = CURVETYPE_LINEAR;
	ExpectVisibleQuads({Quad}, aPoints, 0);
}
