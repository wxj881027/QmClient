// 弹窗接管指针期间的提示来源判定：弹窗自己的控件仍可悬浮，被挡住的下层页面不行。
// 这条契约由 CTooltips 的绘制门槛与 CUi::TooltipSourceHovered 共同保证，
// 任何一侧退回“只看 UnderlyingPointerInputBlocked”都会让弹窗内提示整体消失。
#include <engine/keys.h>

#include <game/client/ui.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmPopupTooltipSource : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUi::SConfirmPopupContext m_Popup;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame(vec2 Position)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
			m_Ui.RenderPopupMenus();
		}

		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}
	};
} // namespace

TEST_F(QmPopupTooltipSource, BlockingPopupKeepsItsOwnControlsHoverable)
{
	m_Popup.DefaultButtons();
	str_copy(m_Popup.m_aMessage, "Delete this skin?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &m_Popup);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Popup);
	ASSERT_NE(pPopup, nullptr);

	const vec2 Center = pPopup->Center();
	Frame(Center);
	ASSERT_TRUE(m_Ui.PointerInputBlocked());
	EXPECT_TRUE(m_Ui.MouseInsideOpenPopupMenu());

	const CUIRect Inside{pPopup->x + 4.0f, pPopup->y + 4.0f, pPopup->w - 8.0f, pPopup->h - 8.0f};
	// 弹窗自己登记的来源：矩形落在弹窗内即可悬浮。
	EXPECT_TRUE(m_Ui.TooltipSourceHovered(&Inside, true));
	// 下层控件即使矩形和弹窗重合，也不能借弹窗的区域透出提示。
	EXPECT_FALSE(m_Ui.TooltipSourceHovered(&Inside, false));
}

TEST_F(QmPopupTooltipSource, BlockingPopupStillSuppressesUnderlyingSources)
{
	m_Popup.DefaultButtons();
	str_copy(m_Popup.m_aMessage, "Delete this skin?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &m_Popup);
	ASSERT_NE(m_Ui.GetPopupMenuRect(&m_Popup), nullptr);

	const CUIRect Underlying{10.0f, 10.0f, 40.0f, 20.0f};
	Frame(Underlying.Center());
	ASSERT_TRUE(m_Ui.PointerInputBlocked());
	EXPECT_FALSE(m_Ui.MouseInsideOpenPopupMenu());
	// 指针就在这块矩形里，但它在弹窗外：仍按旧的阻断语义不出提示。
	EXPECT_FALSE(m_Ui.TooltipSourceHovered(&Underlying, false));
}

TEST_F(QmPopupTooltipSource, ClosingPopupRestoresUnderlyingSources)
{
	m_Popup.DefaultButtons();
	str_copy(m_Popup.m_aMessage, "Delete this skin?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &m_Popup);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	Key(KEY_ESCAPE);
	const CUIRect Underlying{10.0f, 10.0f, 40.0f, 20.0f};
	Frame(Underlying.Center());
	ASSERT_FALSE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_FALSE(m_Ui.PointerInputBlocked());
	EXPECT_TRUE(m_Ui.TooltipSourceHovered(&Underlying, false));
}
