// 真实名字组件、UI 排版与文字引擎协作，只替换 GPU 提交边界。
#include <game/client/QmUi/QmAuthorName.h>
#include <game/client/components/qmclient/qm_sponsor_authors.h>

#include <test/support/qm_real_ui_fixture.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	class CAuthorNameGraphics final : public qm_ui_test::CRealUiTestGraphics
	{
		std::unordered_map<int, std::vector<unsigned char>> m_BufferData;
		CUIRect m_SavedProjection;

	public:
		struct STextGeometry
		{
			CUIRect m_Projection;
			std::vector<unsigned char> m_vVertices;
		};
		std::vector<STextGeometry> m_vGeometry;
		std::vector<SRenderTargetDrawParams> m_vTargetsDrawn;
		std::vector<float> m_vBlurSigma;
		int m_TargetsCreated = 0;
		bool m_BlurSupported = true;
		bool m_BlurSucceeds = true;

		CUIRect Projection() const
		{
			float X0, Y0, X1, Y1;
			GetScreen(&X0, &Y0, &X1, &Y1);
			return {X0, Y0, X1 - X0, Y1 - Y0};
		}
		int CreateBufferObject(size_t Size, void *pData, int Flags, bool Moved = false) override
		{
			// 原样记录上传的字形网格，不重新实现文字布局或解析引擎私有顶点格式。
			const auto *pBytes = static_cast<unsigned char *>(pData);
			std::vector<unsigned char> vData(pBytes, pBytes + Size);
			const int Id = CRealUiTestGraphics::CreateBufferObject(Size, pData, Flags, Moved);
			m_BufferData.emplace(Id, std::move(vData));
			return Id;
		}
		void DeleteBufferContainer(int &Container, bool DestroyBuffers = true) override
		{
			if(const auto It = m_Containers.find(Container); DestroyBuffers && It != m_Containers.end())
				m_BufferData.erase(It->second);
			CRealUiTestGraphics::DeleteBufferContainer(Container, DestroyBuffers);
		}
		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			m_vGeometry.push_back({Projection(), m_BufferData.at(m_Containers.at(Container))});
			CRealUiTestGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
		bool IsRenderTargetGaussianBlurSupported() const override { return m_BlurSupported; }
		CRenderTargetHandle CreateRenderTarget(int Width, int Height) override
		{
			EXPECT_GT(Width, 0);
			EXPECT_GT(Height, 0);
			++m_TargetsCreated;
			return CreateRenderTargetHandle(m_NextId++);
		}
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override { pTarget->Invalidate(); }
		bool BeginRenderTarget(CRenderTargetHandle Target, ColorRGBA) override
		{
			EXPECT_TRUE(Target.IsValid());
			m_SavedProjection = Projection();
			return true;
		}
		void EndRenderTarget() override
		{
			MapScreen(m_SavedProjection.x, m_SavedProjection.y, m_SavedProjection.x + m_SavedProjection.w, m_SavedProjection.y + m_SavedProjection.h);
		}
		bool GaussianBlurRenderTarget(CRenderTargetHandle, const std::array<CRenderTargetHandle, DUAL_KAWASE_PYRAMID_LEVELS> &, CRenderTargetHandle, const SGaussianBlurParams &Params) override
		{
			m_vBlurSigma.push_back(Params.m_Sigma);
			return m_BlurSucceeds;
		}
		void DrawRenderTarget(CRenderTargetHandle, const SRenderTargetDrawParams &Params) override { m_vTargetsDrawn.push_back(Params); }
	};

	class QmAuthorNameBlur : public qm_ui_test::CRealUiFixture
	{
	protected:
		CAuthorNameGraphics m_Graphics;
		qm_ui_test::CTestUiClient m_Client;
		CQmAuthorName m_Name;
		// 鼠标留在原点、名字在旁边；激活指针会提高模糊强度，但不会触发悬停替换。
		const CUIRect m_Label{2.35f, 2.65f, 70.0f, 24.0f};
		static constexpr float FONT_SIZE = 13.37f;

		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_Graphics; }
		IClient *UiClient() override { return &m_Client; }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			m_Graphics.SetFramebufferSize(1280, 720);
			m_Graphics.MapScreen(0, 0, 1066.6667f, 600);
		}
		void TearDown() override
		{
			m_Name.Reset(&m_Graphics);
			CRealUiFixture::TearDown();
		}
		void RenderName(bool PointerActive)
		{
			m_Client.AdvanceFrame();
			m_Name.Render(&m_Ui, &m_Graphics, QmSponsorAuthors::Authors()[2], m_Label, FONT_SIZE, PointerActive);
		}
		std::vector<unsigned char> OrdinaryNameGeometry()
		{
			SLabelProperties Props;
			Props.m_MaxWidth = m_Label.w;
			Props.m_DisallowNewline = true;
			Props.m_StopAtEnd = true;
			m_Ui.DoLabel(&m_Label, "夏日", FONT_SIZE, TEXTALIGN_MC, Props);
			return m_Graphics.m_vGeometry.back().m_vVertices;
		}
	};

	void ExpectSameRect(const CUIRect &Actual, const CUIRect &Expected)
	{
		EXPECT_FLOAT_EQ(Actual.x, Expected.x);
		EXPECT_FLOAT_EQ(Actual.y, Expected.y);
		EXPECT_FLOAT_EQ(Actual.w, Expected.w);
		EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	}
}

TEST_F(QmAuthorNameBlur, ChangingBlurKeepsScreenLayoutAndTargetPosition)
{
	const auto vExpected = OrdinaryNameGeometry();
	ASSERT_FALSE(vExpected.empty());
	const CUIRect OriginalProjection = m_Graphics.Projection();
	RenderName(false);
	ASSERT_EQ(m_Graphics.m_vTargetsDrawn.size(), 1u);
	const auto FirstTarget = m_Graphics.m_vTargetsDrawn.front();
	ASSERT_EQ(m_Graphics.m_vBlurSigma.size(), 1u);
	const float FirstSigma = m_Graphics.m_vBlurSigma.front();
	EXPECT_EQ(m_Graphics.m_vGeometry.back().m_vVertices, vExpected);
	const CUIRect FirstSource = m_Graphics.m_vGeometry.back().m_Projection;
	const int TargetsCreated = m_Graphics.m_TargetsCreated;
	EXPECT_GT(TargetsCreated, 0);
	for(const bool PointerActive : {true, false})
	{
		SCOPED_TRACE(PointerActive);
		RenderName(PointerActive);
		const auto &Target = m_Graphics.m_vTargetsDrawn.back();
		ExpectSameRect({Target.m_X, Target.m_Y, Target.m_W, Target.m_H}, {FirstTarget.m_X, FirstTarget.m_Y, FirstTarget.m_W, FirstTarget.m_H});
		EXPECT_EQ(m_Graphics.m_vGeometry.back().m_vVertices, vExpected);
		ExpectSameRect(m_Graphics.m_vGeometry.back().m_Projection, FirstSource);
		EXPECT_EQ(m_Graphics.m_TargetsCreated, TargetsCreated);
		ExpectSameRect(m_Graphics.Projection(), OriginalProjection);
		if(PointerActive)
			EXPECT_GT(m_Graphics.m_vBlurSigma.back(), FirstSigma);
		else
			EXPECT_FLOAT_EQ(m_Graphics.m_vBlurSigma.back(), FirstSigma);
	}
}

TEST_F(QmAuthorNameBlur, UnsupportedBlurKeepsTheConvolutionCenteredOnTheOrdinaryName)
{
	const auto vExpected = OrdinaryNameGeometry();
	ASSERT_FALSE(vExpected.empty());
	const CUIRect Original = m_Graphics.Projection();
	m_Graphics.m_BlurSupported = false;
	for(const bool PointerActive : {false, true})
	{
		SCOPED_TRACE(PointerActive);
		m_Graphics.m_vGeometry.clear();
		RenderName(PointerActive);
		ASSERT_EQ(m_Graphics.m_vGeometry.size(), 9u);
		for(size_t i = 0; i < m_Graphics.m_vGeometry.size(); ++i)
		{
			const auto &Sample = m_Graphics.m_vGeometry[i];
			const auto &Opposite = m_Graphics.m_vGeometry[8 - i];
			EXPECT_EQ(Sample.m_vVertices, vExpected);
			EXPECT_NEAR((Sample.m_Projection.x + Opposite.m_Projection.x) * 0.5f, Original.x, 0.0001f);
			EXPECT_NEAR((Sample.m_Projection.y + Opposite.m_Projection.y) * 0.5f, Original.y, 0.0001f);
		}
		ExpectSameRect(m_Graphics.m_vGeometry[4].m_Projection, Original);
		ExpectSameRect(m_Graphics.Projection(), Original);
	}
	EXPECT_EQ(m_Graphics.m_TargetsCreated, 0);
}

TEST_F(QmAuthorNameBlur, FailedBlurReusesTheSameGlyphsAndRestoresTheProjection)
{
	const auto vExpected = OrdinaryNameGeometry();
	ASSERT_FALSE(vExpected.empty());
	const CUIRect Original = m_Graphics.Projection();
	m_Graphics.m_BlurSucceeds = false;
	m_Graphics.m_vGeometry.clear();
	RenderName(true);
	ASSERT_EQ(m_Graphics.m_vGeometry.size(), 10u);
	for(const auto &Sample : m_Graphics.m_vGeometry)
		EXPECT_EQ(Sample.m_vVertices, vExpected);
	EXPECT_TRUE(m_Graphics.m_vTargetsDrawn.empty());
	ExpectSameRect(m_Graphics.Projection(), Original);
}
