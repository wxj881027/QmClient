#include <game/client/QmUi/QmFinishSummarySurface.h>
#include <game/client/animstate.h>
#include <game/client/components/qmclient/finish_summary_data.h>
#include <game/client/render.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <limits>

// 独立 UI 测试目标不链接角色和发光绘制；这些用例在调用它们前结束文字绘制。
const CAnimState *CAnimState::GetIdle()
{
	ADD_FAILURE() << "Unexpected tee animation";
	return nullptr;
}

void CRenderTools::GetRenderTeeOffsetToRenderedTee(const CAnimState *, const CTeeRenderInfo *, vec2 &)
{
	ADD_FAILURE() << "Unexpected tee offset";
}

void CRenderTools::RenderTee(const CAnimState *, const CTeeRenderInfo *, int, vec2, vec2, float) const
{
	ADD_FAILURE() << "Unexpected tee render";
}

void CRenderTools::RenderTextContainerWithEffects(STextContainerIndex, const SQmTextEffectRenderStyle &, float, float) const
{
	ADD_FAILURE() << "Unexpected text effect";
}

namespace
{
	class CFinishSummaryGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		bool m_TargetActive = false;
		int m_BeginCalls = 0;
		int m_EndCalls = 0;
		int m_BackgroundCalls = 0;
		int m_CompositeCalls = 0;

		bool IsRenderTargetSupported() const override { return true; }
		CRenderTargetHandle CreateRenderTarget(int, int) override { return CreateRenderTargetHandle(1); }
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override { pTarget->Invalidate(); }
		bool BeginRenderTarget(CRenderTargetHandle Target, ColorRGBA) override
		{
			++m_BeginCalls;
			if(!Target.IsValid() || m_TargetActive)
				return false;
			m_TargetActive = true;
			return true;
		}
		void EndRenderTarget() override
		{
			++m_EndCalls;
			m_TargetActive = false;
		}
		bool HasMediaIslandSdf() override { return true; }
		void RenderMediaIslandSdf(const SMediaIslandSdfParams &, CRenderTargetHandle) override { ++m_BackgroundCalls; }
		void DrawRenderTarget(CRenderTargetHandle, const SRenderTargetDrawParams &) override { ++m_CompositeCalls; }
	};

	class QmFinishSummarySurface : public qm_ui_test::CRealUiFixture
	{
	protected:
		CFinishSummaryGraphics m_Device;
		CRenderTools m_RenderTools{};
		qm_finish_summary::CSurface m_Surface;
		qm_finish_summary::CContentCache m_Content;
		QmFinishSummary::SSnapshot m_Snapshot;

		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_Device; }
		void TearDown() override
		{
			m_Surface.Reset(m_Device);
			qm_ui_test::CRealUiFixture::TearDown();
		}
		bool Render(float ContentAlpha, bool Closing = false)
		{
			qm_finish_summary::SSurfaceStyle Style;
			Style.m_ContentAlpha = ContentAlpha;
			Style.m_Closing = Closing;
			return m_Surface.Render(m_Device, *m_Ui.TextRender(), m_RenderTools, {100, 80, 480, 216},
				m_Snapshot, nullptr, m_Content, Style, 1.0, true, 0);
		}
	};
}

TEST_F(QmFinishSummarySurface, FailedContentRevisionLeavesTheTargetUntouchedAndCanRetry)
{
	m_Snapshot.m_Id = 1;
	m_Snapshot.m_Category = "Novice";
	ASSERT_TRUE(Render(0.0f));
	ASSERT_EQ(m_Device.m_BeginCalls, 1);
	ASSERT_EQ(m_Device.m_BackgroundCalls, 1);
	ASSERT_EQ(m_Device.m_CompositeCalls, 1);

	++m_Snapshot.m_Revision;
	m_Snapshot.m_Category.assign(96, 'x');
	bool Rendered, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Rendered = Render(0.0f);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Rendered);
	EXPECT_EQ(m_Device.m_BeginCalls, 1);
	EXPECT_EQ(m_Device.m_BackgroundCalls, 1);
	EXPECT_EQ(m_Device.m_CompositeCalls, 1);

	ASSERT_TRUE(Render(0.0f));
	EXPECT_EQ(m_Content.m_Type, m_Snapshot.m_Category);
	EXPECT_TRUE(m_Content.m_Completion.starts_with(m_Snapshot.m_Category));
	EXPECT_EQ(m_Device.m_CompositeCalls, 2);
}

TEST_F(QmFinishSummarySurface, MaximumPointsRemainInRangeAfterPresentation)
{
	m_Snapshot.m_Id = 1;
	m_Snapshot.m_Points = {std::numeric_limits<int64_t>::max(), QmFinishSummary::EStatus::READY};
	ASSERT_TRUE(Render(0.0f));
	EXPECT_TRUE(m_Content.m_Points.ends_with(" 9,223,372,036,854,775,807"));
}

TEST_F(QmFinishSummarySurface, FailedTextUploadRestoresCallerAndClosingRedrawsTheCard)
{
	m_Snapshot.m_Id = 1;
	ASSERT_TRUE(Render(0.0f));
	IGraphics &Graphics = m_Device;
	Graphics.MapScreen(11, 13, 971, 553);
	const CScreenRect Screen = Graphics.GetScreen();
	ITextRender &Text = *m_Ui.TextRender();
	const unsigned Flags = TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT;
	const ColorRGBA Color(0.2f, 0.3f, 0.4f, 0.5f), Outline(0.7f, 0.6f, 0.5f, 0.4f);
	Text.SetFontPreset(EFontPreset::ICON_FONT);
	Text.SetRenderFlags(Flags);
	Text.TextColor(Color);
	Text.TextOutlineColor(Outline);
	const ColorRGBA SavedColor = Text.GetTextColor();
	const size_t Buffers = m_Device.m_Buffers.size();
	m_Device.m_FailBufferContainerCreate = true;

	EXPECT_FALSE(Render(1.0f));
	ASSERT_FALSE(m_Device.m_FailBufferContainerCreate);
	EXPECT_FALSE(m_Device.m_TargetActive);
	EXPECT_EQ(m_Device.m_BeginCalls, 2);
	EXPECT_EQ(m_Device.m_EndCalls, 2);
	EXPECT_EQ(m_Device.m_CompositeCalls, 1);
	EXPECT_EQ(Graphics.GetScreen().m_TopLeft, Screen.m_TopLeft);
	EXPECT_EQ(Graphics.GetScreen().m_BottomRight, Screen.m_BottomRight);
	EXPECT_EQ(Text.GetFontPreset(), EFontPreset::ICON_FONT);
	EXPECT_EQ(Text.GetRenderFlags(), Flags);
	EXPECT_EQ(Text.GetTextColor(), SavedColor);
	EXPECT_EQ(Text.GetTextOutlineColor(), Outline);
	ASSERT_EQ(m_Device.m_Buffers.size(), Buffers + 1);
	Text.QmTextFrameEnd();
	EXPECT_EQ(m_Device.m_Buffers.size(), Buffers);

	ASSERT_TRUE(Render(0.0f, true));
	EXPECT_EQ(m_Device.m_BeginCalls, 3);
	EXPECT_EQ(m_Device.m_EndCalls, 3);
	EXPECT_EQ(m_Device.m_CompositeCalls, 2);
}
