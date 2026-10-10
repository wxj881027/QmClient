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
