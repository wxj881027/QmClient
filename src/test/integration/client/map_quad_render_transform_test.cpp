#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	class CQuadTransformGraphics : public CIconBenchmarkGraphics
	{
	public:
		size_t m_BufferedQuads = 0;
		std::optional<CFreeformItem> m_ImmediateQuad;

		bool IsQuadBufferingEnabled() override { return true; }
		void RenderQuadLayer(int BufferContainerIndex, SQuadRenderInfo *pQuadInfo, size_t QuadNum, int QuadOffset, bool Grouped) override
		{
			m_BufferedQuads += QuadNum;
		}
		void TrianglesBegin() override {}
		void TrianglesEnd() override {}
		void QuadsSetSubsetFree(float, float, float, float, float, float, float, float, int) override {}
		void SetColorVertex(const CColorVertex *, size_t) override {}
		void QuadsDrawFreeform(const CFreeformItem *pItems, int Num) override
		{
			ASSERT_EQ(Num, 1);
			m_ImmediateQuad = pItems[0];
		}
	};

	enum class EChangingOffset
	{
		COLOR,
		POSITION,
	};

	class MapQuadBufferedOffsetIntegration : public ::testing::TestWithParam<EChangingOffset>
	{
	};

	class CQuadTransformEnvelope : public IEnvelopeEval
	{
	public:
		float m_RotationDegrees = 0.0f;
		mutable std::vector<std::pair<int, int>> m_vRequests;

		void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const override
		{
			m_vRequests.emplace_back(EnvelopeIndex, TimeOffsetMillis);
			if(Channels == 3)
				Result.b = m_RotationDegrees;
		}
	};
}

TEST_P(MapQuadBufferedOffsetIntegration, DistinctIntegerOffsetsReachTheEnvelopeEvaluator)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "quad-envelope-offsets.map"));

	constexpr int FirstOffset = 1 << 24;
	std::array<CQuad, 2> aQuads{};
	for(auto &Quad : aQuads)
	{
		Quad.m_ColorEnv = 0;
		Quad.m_PosEnv = 1;
		Quad.m_ColorEnvOffset = Quad.m_PosEnvOffset = FirstOffset;
	}
	if(GetParam() == EChangingOffset::COLOR)
		++aQuads[1].m_ColorEnvOffset;
	else
		++aQuads[1].m_PosEnvOffset;
	CMapItemLayerQuads Item{};
	Item.m_Image = -1;
	Item.m_NumQuads = static_cast<int>(aQuads.size());
	Item.m_Data = Writer.AddDataSwapped(sizeof(aQuads), aQuads.data());
	ASSERT_GE(Item.m_Data, 0);
	ASSERT_TRUE(Writer.Finish());

	CMap Map;
	ASSERT_TRUE(Map.GetReader()->Open(pStorage.get(), "quad-envelope-offsets.map", IStorage::TYPE_SAVE));
	CQuadTransformEnvelope Envelope;
	auto pEnvelopeManager = std::make_shared<CEnvelopeManager>(&Envelope, &Map);
	CQuadTransformGraphics Graphics;
	CRenderLayerQuads Layer(0, 0, 0, &Item);
	Layer.OnInit(&Graphics, nullptr, nullptr, pEnvelopeManager, &Map, nullptr, true);
	ASSERT_TRUE(Layer.IsValid());
	Layer.Prepare().Finish();
	CRenderLayerParams Params{};
	Params.m_RenderType = RENDERTYPE_FULL_DESIGN;
	EXPECT_TRUE(Layer.DoRender(Params));
	Layer.Render(Params);
	Layer.Unload();

	EXPECT_EQ(Graphics.m_BufferedQuads, aQuads.size());
	const std::vector<std::pair<int, int>> vExpected = {
		{0, FirstOffset}, {1, FirstOffset},
		{0, aQuads[1].m_ColorEnvOffset}, {1, aQuads[1].m_PosEnvOffset}};
	EXPECT_EQ(Envelope.m_vRequests, vExpected);
}

INSTANTIATE_TEST_SUITE_P(EnvelopeKinds, MapQuadBufferedOffsetIntegration,
	::testing::Values(EChangingOffset::COLOR, EChangingOffset::POSITION),
	[](const ::testing::TestParamInfo<EChangingOffset> &Info) {
		return Info.param == EChangingOffset::COLOR ? "Color" : "Position";
	});

TEST(MapQuadRenderTransformIntegration, ImmediateRotationKeepsCoordinatesOutsideTheFixedPointRange)
{
	CQuadTransformGraphics Graphics;
	CRenderMap Renderer;
	Renderer.Init(&Graphics, nullptr);
	CQuadTransformEnvelope Envelope;
	Envelope.m_RotationDegrees = 90.0f;
	CQuad Quad{};
	Quad.m_PosEnv = 0;
	Quad.m_ColorEnv = -1;
	Quad.m_aPoints[0] = {-i2fx(1500000), 0};
	Quad.m_aPoints[4] = {i2fx(1500000), 0};

	Renderer.ForceRenderQuads(&Quad, 1, &Envelope, 1.0f);

	ASSERT_TRUE(Graphics.m_ImmediateQuad.has_value());
	EXPECT_FLOAT_EQ(Graphics.m_ImmediateQuad->m_Y0, -3000000.0f);
}

TEST(MapQuadRenderTransformIntegration, ImmediateRotationPreservesNormalFixedPointTruncation)
{
	CQuadTransformGraphics Graphics;
	CRenderMap Renderer;
	Renderer.Init(&Graphics, nullptr);
	CQuadTransformEnvelope Envelope;
	Envelope.m_RotationDegrees = 30.0f;
	CQuad Quad{};
	Quad.m_PosEnv = 0;
	Quad.m_ColorEnv = -1;
	Quad.m_aPoints[0] = {3, 0};

	Renderer.ForceRenderQuads(&Quad, 1, &Envelope, 1.0f);

	ASSERT_TRUE(Graphics.m_ImmediateQuad.has_value());
	EXPECT_FLOAT_EQ(Graphics.m_ImmediateQuad->m_X0, fx2f(2));
	EXPECT_FLOAT_EQ(Graphics.m_ImmediateQuad->m_Y0, fx2f(1));
}

TEST(MapQuadRenderTransformIntegration, ImmediateRotationSkipsNonFiniteEnvelopeResults)
{
	CQuadTransformGraphics Graphics;
	CRenderMap Renderer;
	Renderer.Init(&Graphics, nullptr);
	CQuadTransformEnvelope Envelope;
	Envelope.m_RotationDegrees = std::numeric_limits<float>::quiet_NaN();
	CQuad Quad{};

	Renderer.ForceRenderQuads(&Quad, 1, &Envelope, 1.0f);

	EXPECT_FALSE(Graphics.m_ImmediateQuad.has_value());
}
