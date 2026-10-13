// 输入事件经过真实 UI 帧与弹窗栈，再由真实滚动区改变内容偏移。
#include <engine/keys.h>

#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmConsoleSettingsScrollIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestUiClient m_Client;
		qm_ui_test::CTestInput m_Input;
		CScrollRegion m_ParentScroll;
		CScrollRegion m_ChildScroll;
		SPopupMenuId m_ParentId;
		SPopupMenuId m_ChildId;
		CUIRect m_ParentRect{40.0f, 40.0f, 360.0f, 240.0f};
		CUIRect m_ChildRect{70.0f, 70.0f, 200.0f, 150.0f};
		float m_ContentHeight = 900.0f;
		vec2 m_ParentOffset{};
		bool m_ParentActive = false;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 消除帧间惯性，失活帧的偏移变化只能来自本帧错误消费。
			g_Config.m_UiSmoothScrollTime = 0;
			MoveMouse({100.0f, 100.0f});
		}

		void MoveMouse(vec2 Position)
		{
			const auto *pScreen = m_Ui.Screen();
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / pScreen->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
		}

		void DrawRegion(CScrollRegion &Region, SPopupMenuId &Id, CUIRect View, bool Active, vec2 &Offset)
		{
			auto Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, 1.0f);
			Params.m_Interactive = Active;
			Params.m_WheelOwnerPriority = EUiWheelOwnerPriority::POPUP;
			Params.m_pWheelOwnerId = &Id;
			Params.m_WheelOwnerPreRegistered = true;
			m_Ui.RegisterWheelOwner(&Id, EUiWheelOwnerPriority::POPUP, View, Active && m_ContentHeight > View.h);
			Region.Begin(&View, &Offset, &Params);
			View.y += Offset.y;
			View.h = m_ContentHeight;
			Region.AddRect(View);
			Region.End();
		}

		vec2 DrawResizingRegion(float ContentExtent, CUIRect View, bool Horizontal = false, bool Premeasure = true, bool HideScrollbar = false)
		{
			m_Client.AdvanceFrame();
			CScrollRegionParams Params;
			Params.m_ScrollHorizontal = Horizontal;
			Params.m_HideScrollbar = HideScrollbar;
			if(Premeasure)
				m_ParentScroll.SetContentHeightForNextFrame(ContentExtent);
			vec2 Offset;
			m_ParentScroll.Begin(&View, &Offset, &Params);
			View.x += Offset.x;
			View.y += Offset.y;
			if(Horizontal)
				View.w = ContentExtent;
			else
				View.h = ContentExtent;
			m_ParentScroll.AddRect(View);
			m_ParentScroll.End();
			return Offset;
		}

		static CUi::EPopupMenuFunctionResult Parent(void *pContext, CUIRect View, bool Active)
		{
			auto *pThis = static_cast<QmConsoleSettingsScrollIntegration *>(pContext);
			pThis->m_ParentActive = Active;
			pThis->DrawRegion(pThis->m_ParentScroll, pThis->m_ParentId, View, Active, pThis->m_ParentOffset);
			return CUi::POPUP_KEEP_OPEN;
		}

		static CUi::EPopupMenuFunctionResult Child(void *pContext, CUIRect View, bool Active)
		{
			auto *pThis = static_cast<QmConsoleSettingsScrollIntegration *>(pContext);
			vec2 Offset;
			pThis->DrawRegion(pThis->m_ChildScroll, pThis->m_ChildId, View, Active, Offset);
			return CUi::POPUP_KEEP_OPEN;
		}

		void Open(bool Nested = false)
		{
			SPopupMenuProperties Props;
			Props.m_Animate = false;
			Props.m_AutoReposition = false;
			Props.m_BlockUnderlyingPointerInput = true;
			Props.m_BlockUnderlyingScroll = true;
			const auto &Rect = Nested ? m_ChildRect : m_ParentRect;
			m_Ui.DoPopupMenu(Nested ? &m_ChildId : &m_ParentId, Rect.x, Rect.y, Rect.w, Rect.h,
				this, Nested ? Child : Parent, Props);
		}

		void Frame(int WheelKey = 0, bool UpdateWhileDisabled = false)
		{
			m_Client.AdvanceFrame();
			m_Ui.SetEnabled(true);
			if(WheelKey != 0)
			{
				IInput::CEvent Event{};
				Event.m_Key = WheelKey;
				Event.m_Flags = IInput::FLAG_PRESS;
				ASSERT_TRUE(m_Ui.OnInput(Event));
			}
			m_Ui.SetEnabled(!UpdateWhileDisabled);
			m_Ui.Update();
			// Update 已从真实输入热键开启 ownership；再次调用必须保持本帧状态。
			m_Ui.BeginWheelOwnershipFrame();
			m_Ui.SetEnabled(true);
			m_Ui.RenderPopupMenus();
		}
	};
}

TEST_F(QmConsoleSettingsScrollIntegration, WheelEventMovesContentAndNextFrameReturnsScrolledOffset)
{
	Open();
	Frame();
	ASSERT_TRUE(m_ParentScroll.ContentOverflows());
	ASSERT_FLOAT_EQ(m_ParentScroll.State().Offset(), 0.0f);
	Frame(KEY_MOUSE_WHEEL_DOWN);
	ASSERT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	ASSERT_GT(m_ParentScroll.State().Offset(), 0.0f);
	const float Scrolled = m_ParentScroll.State().Offset();
	Frame();
	EXPECT_FALSE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_FLOAT_EQ(m_ParentOffset.y, -Scrolled);
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), Scrolled);
	Frame(KEY_MOUSE_WHEEL_UP);
	EXPECT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_LT(m_ParentScroll.State().Offset(), Scrolled);
}

TEST_F(QmConsoleSettingsScrollIntegration, NestedPopupLeavesInactiveParentUnchangedAndScrollsChild)
{
	Open();
	Frame();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	const float ParentOffset = m_ParentScroll.State().Offset();
	Open(true);
	Frame();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	EXPECT_FALSE(m_ParentActive);
	EXPECT_FALSE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), ParentOffset);
	EXPECT_TRUE(m_ChildScroll.WheelConsumedThisFrame());
	EXPECT_GT(m_ChildScroll.State().Offset(), 0.0f);
	ASSERT_TRUE(m_Ui.CloseTopPopupMenu());
	Frame(KEY_MOUSE_WHEEL_DOWN);
	EXPECT_TRUE(m_ParentActive);
	EXPECT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_GT(m_ParentScroll.State().Offset(), ParentOffset);
}

TEST_F(QmConsoleSettingsScrollIntegration, ClosingAndReopeningRestoresWheelWithoutStaleOwner)
{
	Open();
	Frame();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	ASSERT_GT(m_ParentScroll.State().Offset(), 0.0f);
	m_Ui.ClosePopupMenus();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	float Delta = 0.0f;
	EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_ParentId, &Delta));
	m_ParentScroll.Reset();
	Open();
	Frame();
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 0.0f);
	Frame(KEY_MOUSE_WHEEL_DOWN);
	EXPECT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_GT(m_ParentScroll.State().Offset(), 0.0f);
}

TEST_F(QmConsoleSettingsScrollIntegration, FittingContentDoesNotConsumeAndExpandedContentRecovers)
{
	m_ContentHeight = 40.0f;
	Open();
	Frame();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	EXPECT_FALSE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 0.0f);
	m_ContentHeight = 900.0f;
	Frame();
	Frame(KEY_MOUSE_WHEEL_DOWN);
	EXPECT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_GT(m_ParentScroll.State().Offset(), 0.0f);
}

TEST_F(QmConsoleSettingsScrollIntegration, DisabledUiUpdatePreservesWheelAcceptedDuringConsoleInput)
{
	Open();
	Frame();
	m_Ui.SetEnabled(false);
	Frame(KEY_MOUSE_WHEEL_DOWN, true);
	ASSERT_TRUE(m_ParentScroll.WheelConsumedThisFrame());
	ASSERT_GT(m_ParentScroll.State().Offset(), 0.0f);
	const float Scrolled = m_ParentScroll.State().Offset();
	Frame(0, true);
	EXPECT_FALSE(m_ParentScroll.WheelConsumedThisFrame());
	EXPECT_FLOAT_EQ(m_ParentOffset.y, -Scrolled);
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundDrawPreservesPopupButtonPressUntilOwnerRelease)
{
	struct SButton
	{
		CUi *m_pUi;
		int m_Id = 0;
		int m_Clicks = 0;
	} Button{&m_Ui};
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	Props.m_AutoReposition = false;
	Props.m_BlockUnderlyingPointerInput = true;
	m_Ui.DoPopupMenu(&m_ParentId, 40, 40, 200, 150, &Button, [](void *pContext, CUIRect View, bool Active) {
		auto *pButton = static_cast<SButton *>(pContext);
		if(Active && pButton->m_pUi->DoButtonLogic(&pButton->m_Id, 0, &View, BUTTONFLAG_LEFT))
			++pButton->m_Clicks;
		return CUi::POPUP_KEEP_OPEN; }, Props);
	const auto OwnerFrame = [&]() {
		m_Client.AdvanceFrame();
		m_Ui.StartCheck();
		m_Ui.Update();
		m_Ui.RenderPopupMenus();
		m_Ui.FinishCheck();
	};
	OwnerFrame();
	m_Input.m_HeldKey = KEY_MOUSE_1;
	OwnerFrame();
	ASSERT_EQ(m_Ui.ActiveItem(), &Button.m_Id);
	m_Ui.BeginBackgroundRender();
	EXPECT_FALSE(m_Ui.DoButtonLogic(&Button.m_Id, 0, &m_ParentRect, BUTTONFLAG_LEFT));
	m_Ui.EndBackgroundRender();
	EXPECT_EQ(m_Ui.ActiveItem(), &Button.m_Id);
	OwnerFrame();
	EXPECT_EQ(m_Ui.ActiveItem(), &Button.m_Id);
	m_Input.m_HeldKey = 0;
	OwnerFrame();
	EXPECT_EQ(Button.m_Clicks, 1);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundPremeasuredShrinkClampsReturnedOffsetAndKeepsLastRowVisible)
{
	const CUIRect View{40, 40, 240, 240};
	for(bool Horizontal : {false, true})
	{
		for(float ContentExtent : {400.0f, 200.0f, 0.0f})
		{
			SCOPED_TRACE(Horizontal);
			SCOPED_TRACE(ContentExtent);
			m_ParentScroll.Reset();
			DrawResizingRegion(900.0f, View, Horizontal);
			m_ParentScroll.SetScrollOffsetY(-600.0f);
			ASSERT_FLOAT_EQ(m_ParentScroll.State().Offset(), 600.0f);
			m_Ui.BeginBackgroundRender();
			const vec2 Offset = DrawResizingRegion(ContentExtent, View, Horizontal);
			m_Ui.EndBackgroundRender();
			const float Expected = ContentExtent == 400.0f ? 160.0f : 0.0f;
			EXPECT_FLOAT_EQ(Horizontal ? Offset.x : Offset.y, -Expected);
			EXPECT_FLOAT_EQ(Horizontal ? Offset.y : Offset.x, 0.0f);
			EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), Expected);
			if(ContentExtent > 0.0f)
			{
				CUIRect LastRow = *m_ParentScroll.ClipRect();
				if(Horizontal)
				{
					LastRow.x += ContentExtent - 20.0f + Offset.x;
					LastRow.w = 20.0f;
				}
				else
				{
					LastRow.y += ContentExtent - 20.0f + Offset.y;
					LastRow.h = 20.0f;
				}
				EXPECT_FALSE(m_ParentScroll.RectClipped(LastRow));
			}
		}
	}
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundViewportExpansionClampsBeforeContentLayout)
{
	for(bool Horizontal : {false, true})
	{
		SCOPED_TRACE(Horizontal);
		m_ParentScroll.Reset();
		CUIRect View{40, 40, 240, 240};
		DrawResizingRegion(900.0f, View, Horizontal);
		m_ParentScroll.SetScrollOffsetY(-600.0f);
		m_Ui.BeginBackgroundRender();
		if(Horizontal)
			View.w = 800.0f;
		else
			View.h = 800.0f;
		const vec2 Offset = DrawResizingRegion(900.0f, View, Horizontal);
		EXPECT_FLOAT_EQ(Horizontal ? Offset.x : Offset.y, -100.0f);
		if(Horizontal)
			View.w = 1000.0f;
		else
			View.h = 1000.0f;
		const vec2 FittingOffset = DrawResizingRegion(900.0f, View, Horizontal);
		m_Ui.EndBackgroundRender();
		EXPECT_FLOAT_EQ(Horizontal ? FittingOffset.x : FittingOffset.y, 0.0f);
		EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 0.0f);
	}
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundContentMeasuredAtEndClampsAndNextFrameUsesCorrectedOffset)
{
	const CUIRect View{40, 40, 240, 240};
	for(bool Horizontal : {false, true})
	{
		SCOPED_TRACE(Horizontal);
		m_ParentScroll.Reset();
		DrawResizingRegion(900.0f, View, Horizontal);
		m_ParentScroll.SetScrollOffsetY(-600.0f);
		m_Ui.BeginBackgroundRender();
		DrawResizingRegion(400.0f, View, Horizontal, false);
		EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 160.0f);
		const vec2 Offset = DrawResizingRegion(400.0f, View, Horizontal, false);
		EXPECT_FLOAT_EQ(Horizontal ? Offset.x : Offset.y, -160.0f);
		DrawResizingRegion(200.0f, View, Horizontal, false);
		EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 0.0f);
		m_Ui.EndBackgroundRender();
		// 内容重新增长并恢复前台后，不应恢复尺寸改变前的越界偏移。
		const vec2 Restored = DrawResizingRegion(900.0f, View, Horizontal);
		EXPECT_FLOAT_EQ(Horizontal ? Restored.x : Restored.y, 0.0f);
	}
}

TEST_F(QmConsoleSettingsScrollIntegration, ReadOnlyValidOffsetPreservesPendingRevealUntilForeground)
{
	const CUIRect View{40, 40, 240, 240};
	for(bool HideScrollbar : {false, true})
	{
		for(bool MeasurementOnly : {false, true})
		{
			SCOPED_TRACE(HideScrollbar);
			SCOPED_TRACE(MeasurementOnly);
			m_ParentScroll.Reset();
			DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
			m_ParentScroll.SetScrollOffsetY(-200.0f);
			m_ParentScroll.ScrollRelativeDirect(50.0f);
			if(MeasurementOnly)
				m_Ui.BeginRenderOnly();
			else
				m_Ui.BeginBackgroundRender();
			const vec2 Offset = DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
			if(MeasurementOnly)
				m_Ui.EndRenderOnly();
			else
				m_Ui.EndBackgroundRender();
			EXPECT_FLOAT_EQ(Offset.y, -200.0f);
			EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 200.0f);
			DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
			EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 250.0f);
		}
	}
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundValidOffsetPreservesPausedScrollAnimation)
{
	const CUIRect View{40, 40, 240, 240};
	DrawResizingRegion(900.0f, View);
	m_ParentScroll.SetScrollOffsetY(-100.0f);
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 240.0f;
	Metrics.m_ContentSize = 900.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.2f);
	m_ParentScroll.State().ScrollTo(400.0f, Metrics, Config);
	ASSERT_TRUE(m_ParentScroll.Animating());
	const float Velocity = m_ParentScroll.State().Velocity();
	m_Ui.BeginBackgroundRender();
	const vec2 Offset = DrawResizingRegion(900.0f, View);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Offset.y, -100.0f);
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 100.0f);
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Velocity(), Velocity);
	EXPECT_TRUE(m_ParentScroll.Animating());
	DrawResizingRegion(900.0f, View);
	EXPECT_GT(m_ParentScroll.State().Offset(), 100.0f);
}

TEST_F(QmConsoleSettingsScrollIntegration, NestedMeasurementDoesNotClampBackgroundScrollState)
{
	const CUIRect View{40, 40, 240, 240};
	DrawResizingRegion(900.0f, View);
	m_ParentScroll.SetScrollOffsetY(-600.0f);
	m_Ui.BeginBackgroundRender();
	m_Ui.BeginRenderOnly();
	DrawResizingRegion(400.0f, View);
	m_Ui.EndRenderOnly();
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 600.0f);
	const vec2 Offset = DrawResizingRegion(400.0f, View);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(Offset.y, -160.0f);
	EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 160.0f);
}

TEST_F(QmConsoleSettingsScrollIntegration, BackgroundModeChangeSettlesOnlyAfterForegroundReturns)
{
	const CUIRect View{40, 40, 240, 240};
	g_Config.m_UiSmoothScrollTime = 200;
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = View.h;
	Metrics.m_ContentSize = 900.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.2f);
	for(bool HideScrollbar : {false, true})
	{
		SCOPED_TRACE(HideScrollbar);
		g_Config.m_QmUiScrollMode = 1;
		m_ParentScroll.Reset();
		DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
		m_ParentScroll.SetScrollOffsetY(-100.0f);
		m_ParentScroll.State().ScrollTo(400.0f, Metrics, Config);
		DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
		ASSERT_TRUE(m_ParentScroll.Animating());
		const float OffsetBeforeCover = m_ParentScroll.State().Offset();
		const float VelocityBeforeCover = m_ParentScroll.State().Velocity();
		ASSERT_GT(OffsetBeforeCover, 100.0f);
		ASSERT_GT(VelocityBeforeCover, 0.0f);
		g_Config.m_QmUiScrollMode = 0;
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			const vec2 Offset = DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
			EXPECT_FLOAT_EQ(Offset.y, -OffsetBeforeCover);
			EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), OffsetBeforeCover);
			EXPECT_FLOAT_EQ(m_ParentScroll.State().Velocity(), VelocityBeforeCover);
			EXPECT_TRUE(m_ParentScroll.Animating());
		}
		DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
		EXPECT_FLOAT_EQ(m_ParentScroll.State().Offset(), 400.0f);
		EXPECT_FLOAT_EQ(m_ParentScroll.State().Velocity(), 0.0f);
		EXPECT_FALSE(m_ParentScroll.Animating());

		g_Config.m_QmUiScrollMode = 1;
		m_ParentScroll.ScrollRelativeDirect(50.0f);
		DrawResizingRegion(900.0f, View, false, true, HideScrollbar);
		EXPECT_GT(m_ParentScroll.State().Offset(), 400.0f);
		EXPECT_LT(m_ParentScroll.State().Offset(), 450.0f);
		EXPECT_TRUE(m_ParentScroll.Animating());
	}
}
