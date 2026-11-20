#include <game/client/components/qmclient/map_preview_panel.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <array>
#include <new>

namespace
{
	class CPreviewGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		bool m_AcceptTarget = true;
		bool m_TargetActive = false;
		bool m_FailDraw = false;
		int m_BeginCalls = 0;
		int m_EndCalls = 0;
		int m_DrawCalls = 0;

		bool BeginRenderTarget(CRenderTargetHandle, ColorRGBA) override
		{
			++m_BeginCalls;
			if(!m_AcceptTarget)
				return false;
			m_TargetActive = true;
			return true;
		}
		void EndRenderTarget() override
		{
			++m_EndCalls;
			m_TargetActive = false;
		}
		void DrawRenderTarget(CRenderTargetHandle, const SRenderTargetDrawParams &) override
		{
			++m_DrawCalls;
			if(m_FailDraw)
				throw std::bad_alloc();
		}
	};

	class QmMapPreviewDrawScope : public qm_ui_test::CRealUiFixture
	{
	protected:
		CPreviewGraphics m_PreviewGraphics;
		const CUIRect m_OuterClip{10, 15, 240, 180};
		const CUIRect m_InnerClip{30, 35, 100, 80};
		std::array<float, 4> m_aCallerScreen{};
		CUIRect m_CallerGraphicsClip;

		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_PreviewGraphics; }
		void PrepareCaller()
		{
			m_PreviewGraphics.MapScreen(11, 13, 711, 613);
			m_PreviewGraphics.GetScreen(&m_aCallerScreen[0], &m_aCallerScreen[1], &m_aCallerScreen[2], &m_aCallerScreen[3]);
			m_Ui.ClipEnable(&m_OuterClip);
			m_Ui.ClipEnable(&m_InnerClip);
			m_CallerGraphicsClip = m_PreviewGraphics.m_ClipRect;
		}
		void ExpectCallerRestored()
		{
			std::array<float, 4> aScreen{};
			m_PreviewGraphics.GetScreen(&aScreen[0], &aScreen[1], &aScreen[2], &aScreen[3]);
			EXPECT_EQ(aScreen, m_aCallerScreen);
			ASSERT_TRUE(m_Ui.IsClipped());
			EXPECT_EQ(m_Ui.ClipArea()->x, m_InnerClip.x);
			EXPECT_EQ(m_Ui.ClipArea()->y, m_InnerClip.y);
			EXPECT_EQ(m_Ui.ClipArea()->w, m_InnerClip.w);
			EXPECT_EQ(m_Ui.ClipArea()->h, m_InnerClip.h);
			EXPECT_TRUE(m_PreviewGraphics.m_Clipped);
			EXPECT_EQ(m_PreviewGraphics.m_ClipRect.x, m_CallerGraphicsClip.x);
			EXPECT_EQ(m_PreviewGraphics.m_ClipRect.y, m_CallerGraphicsClip.y);
			EXPECT_EQ(m_PreviewGraphics.m_ClipRect.w, m_CallerGraphicsClip.w);
			EXPECT_EQ(m_PreviewGraphics.m_ClipRect.h, m_CallerGraphicsClip.h);
			m_Ui.ClipDisable();
			ASSERT_TRUE(m_Ui.IsClipped());
			EXPECT_EQ(m_Ui.ClipArea()->x, m_OuterClip.x);
			m_Ui.ClipDisable();
			EXPECT_FALSE(m_Ui.IsClipped());
		}
	};
}

TEST_F(QmMapPreviewDrawScope, SuccessfulFrameEndsItsTargetOnceAndRestoresTheCaller)
{
	PrepareCaller();
	{
		CQmMapPreviewDrawScope Scope(m_Ui);
		EXPECT_FALSE(m_Ui.IsClipped());
		ASSERT_TRUE(Scope.BeginRenderTarget({}, ColorRGBA()));
		m_PreviewGraphics.MapScreen(-100, -200, 100, 200);
		Scope.EndRenderTarget();
		EXPECT_FALSE(m_PreviewGraphics.m_TargetActive);
	}
	EXPECT_EQ(m_PreviewGraphics.m_BeginCalls, 1);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 1);
	ExpectCallerRestored();
}

TEST_F(QmMapPreviewDrawScope, RefusedTargetDoesNotEndAnotherTarget)
{
	PrepareCaller();
	m_PreviewGraphics.m_AcceptTarget = false;
	m_PreviewGraphics.m_TargetActive = true;
	{
		CQmMapPreviewDrawScope Scope(m_Ui);
		EXPECT_FALSE(Scope.BeginRenderTarget({}, ColorRGBA()));
		m_PreviewGraphics.MapScreen(0, 0, 64, 64);
	}
	EXPECT_EQ(m_PreviewGraphics.m_BeginCalls, 1);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 0);
	EXPECT_TRUE(m_PreviewGraphics.m_TargetActive);
	m_PreviewGraphics.m_TargetActive = false;
	ExpectCallerRestored();
}

TEST_F(QmMapPreviewDrawScope, ResourceFailureInsideTheTargetEndsItAndRestoresTheCaller)
{
	PrepareCaller();
	EXPECT_THROW(
		[&] {
			CQmMapPreviewDrawScope Scope(m_Ui);
			if(!Scope.BeginRenderTarget({}, ColorRGBA()))
				return;
			m_PreviewGraphics.MapScreen(0, 0, 128, 64);
			throw std::bad_alloc();
		}(),
		std::bad_alloc);
	EXPECT_FALSE(m_PreviewGraphics.m_TargetActive);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 1);
	ExpectCallerRestored();
}

TEST_F(QmMapPreviewDrawScope, UnrelatedFailureKeepsItsExceptionAfterRestoringTheCaller)
{
	PrepareCaller();
	EXPECT_THROW(
		[&] {
			CQmMapPreviewDrawScope Scope(m_Ui);
			if(!Scope.BeginRenderTarget({}, ColorRGBA()))
				return;
			m_PreviewGraphics.MapScreen(0, 0, 128, 64);
			throw 7;
		}(),
		int);
	EXPECT_FALSE(m_PreviewGraphics.m_TargetActive);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 1);
	ExpectCallerRestored();
}

TEST_F(QmMapPreviewDrawScope, DrawingTheCachedTargetFailureRestoresTheTemporaryClip)
{
	PrepareCaller();
	m_PreviewGraphics.m_FailDraw = true;
	const CUIRect Preview{35, 40, 50, 40};
	EXPECT_THROW(
		[&] {
			CQmMapPreviewDrawScope Scope(m_Ui);
			m_Ui.ClipEnable(&Preview);
			m_PreviewGraphics.DrawRenderTarget({}, {});
		}(),
		std::bad_alloc);
	EXPECT_EQ(m_PreviewGraphics.m_DrawCalls, 1);
	EXPECT_EQ(m_PreviewGraphics.m_BeginCalls, 0);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 0);
	ExpectCallerRestored();
}

TEST_F(QmMapPreviewDrawScope, ClipAllocationFailureRestoresTheSavedClipStack)
{
	PrepareCaller();
	const CUIRect Preview{35, 40, 50, 40};
	bool Triggered = false;
	EXPECT_THROW(
		[&] {
			CQmMapPreviewDrawScope Scope(m_Ui);
			CPredictionAllocationFailure Failure;
			try
			{
				m_Ui.ClipEnable(&Preview);
			}
			catch(const std::bad_alloc &)
			{
				Triggered = Failure.Triggered();
				throw;
			}
		}(),
		std::bad_alloc);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(m_PreviewGraphics.m_BeginCalls, 0);
	EXPECT_EQ(m_PreviewGraphics.m_EndCalls, 0);
	ExpectCallerRestored();
}
