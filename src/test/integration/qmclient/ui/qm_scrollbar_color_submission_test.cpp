// 真实绘制入口与图形设备边界协作，记录提交颜色以约束透明度回归。
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>

#include <vector>

namespace
{
	class CScrollbarColorGraphics : public CIconBenchmarkGraphics
	{
	public:
		std::vector<SRoundedRectSdfParams> m_vSurfaces;
		bool HasRoundedRectSdf() override { return true; }
		void ClipEnable(int, int, int, int) override {}
		void ClipDisable() override {}
		void RenderRoundedRectSdf(const SRoundedRectSdfParams &Params) override
		{
			m_vSurfaces.push_back(Params);
		}
	};

	class QmScrollbarColorSubmission : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;

	protected:
		CScrollbarColorGraphics m_Graphics;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, nullptr};
		CUi m_Ui;
		SUiTheme m_Theme{};
		int m_HandleId = 0;
		CUIRect m_Rect{0.0f, 0.0f, 8.0f, 60.0f};

		void SetUp() override
		{
			g_Config = CConfig();
			m_Ui.Init(&m_Kernel);
			m_Theme.m_Accent = ColorRGBA(0.9f, 0.1f, 0.1f, 0.4f);
		}

		void TearDown() override
		{
			// 本 fixture 不创建文字容器，恢复 UI 的静态设备绑定即可。
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}

		IUiContext Context()
		{
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pTheme = &m_Theme;
			return Ctx;
		}

		void DrawHandle(float BackgroundAlpha, bool Enabled = true, const ColorRGBA *pInner = nullptr)
		{
			m_Graphics.m_vSurfaces.clear();
			m_Ui.SetBackgroundAlphaScale(BackgroundAlpha);
			CUiScopedSurfaceText Surface(nullptr, ColorRGBA(0, 0, 0, 1));
			ui_widget::DrawScrollbarHandle(Context(), &m_HandleId, m_Rect, Enabled, pInner);
		}
	};
}

TEST_F(QmScrollbarColorSubmission, BackgroundOpacityDoesNotDimForegroundOrBorder)
{
	DrawHandle(1.0f);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	const auto Baseline = m_Graphics.m_vSurfaces.front();
	EXPECT_FLOAT_EQ(Baseline.m_FillColor.a, 0.4f);
	for(float Alpha : {0.0f, 0.28f, 0.6f, 1.0f})
	{
		SCOPED_TRACE(Alpha);
		DrawHandle(Alpha);
		ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
		EXPECT_EQ(m_Graphics.m_vSurfaces.front().m_FillColor, Baseline.m_FillColor);
		EXPECT_EQ(m_Graphics.m_vSurfaces.front().m_BorderColor, Baseline.m_BorderColor);
		EXPECT_FLOAT_EQ(m_Ui.BackgroundAlphaScale(), Alpha);
	}
}

TEST_F(QmScrollbarColorSubmission, DisabledStateKeepsItsOwnAttenuationAndCanRecover)
{
	DrawHandle(0.28f, false);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vSurfaces.front().m_FillColor.a, 0.4f * 0.65f);
	DrawHandle(0.28f, true);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vSurfaces.front().m_FillColor.a, 0.4f);
}

TEST_F(QmScrollbarColorSubmission, PressFeedbackSurvivesBackgroundScopeAndReleaseRestoresBorder)
{
	DrawHandle(1.0f);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	const auto Idle = m_Graphics.m_vSurfaces.front();
	m_Ui.SetActiveItem(&m_HandleId);
	DrawHandle(0.28f);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	EXPECT_GT(m_Graphics.m_vSurfaces.front().m_BorderColor.a, Idle.m_BorderColor.a);
	EXPECT_EQ(m_Graphics.m_vSurfaces.front().m_FillColor, Idle.m_FillColor);
	m_Ui.SetActiveItem(nullptr);
	DrawHandle(0.28f);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
	EXPECT_EQ(m_Graphics.m_vSurfaces.front().m_BorderColor, Idle.m_BorderColor);
}

TEST_F(QmScrollbarColorSubmission, InnerMarkerRetainsCallerAlpha)
{
	const ColorRGBA Inner(0.2f, 0.7f, 0.9f, 0.35f);
	DrawHandle(0.28f, true, &Inner);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 2u);
	EXPECT_EQ(m_Graphics.m_vSurfaces.back().m_FillColor, vec4(Inner.r, Inner.g, Inner.b, Inner.a));
}

TEST_F(QmScrollbarColorSubmission, HorizontalSliderStillScalesTrackAndFillOnly)
{
	m_Ui.SetBackgroundAlphaScale(0.28f);
	CUiScopedSurfaceText Surface(nullptr, ColorRGBA(0, 0, 0, 1));
	ui_widget::RenderHorizontalSlider(Context(), &m_HandleId, {0, 0, 120, 20}, 0.5f);
	ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 3u);
	EXPECT_FLOAT_EQ(m_Graphics.m_vSurfaces[0].m_FillColor.a, 0.25f * 0.28f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vSurfaces[1].m_FillColor.a, 0.4f * 0.85f * 0.28f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vSurfaces[2].m_FillColor.a, 0.4f);
}

TEST_F(QmScrollbarColorSubmission, ScrollRegionRailUsesBackgroundAlphaAndRestoresAfterScope)
{
	for(float Alpha : {1.0f, 0.28f, 0.0f, 1.0f})
	{
		SCOPED_TRACE(Alpha);
		m_Graphics.m_vSurfaces.clear();
		m_Ui.SetBackgroundAlphaScale(Alpha);
		CScrollRegion Region;
		Region.SetContentHeightForNextFrame(300.0f);
		CScrollRegionParams Params;
		Params.m_RailBgColor = ColorRGBA(0.8f, 0.7f, 0.6f, 0.25f);
		CUIRect View{0, 0, 200, 100};
		vec2 Offset;
		Region.Begin(&View, &Offset, &Params);
		ASSERT_EQ(m_Graphics.m_vSurfaces.size(), 1u);
		EXPECT_EQ(m_Graphics.m_vSurfaces.front().m_FillColor, vec4(0.8f, 0.7f, 0.6f, 0.25f * Alpha));
		// 此测试验证 Begin 的背景绘制，不调用需要客户端帧时钟的 End。
		m_Ui.ClipDisable();
	}
}
