// 确认弹窗的结果契约：点确认/取消写 m_Result，Esc 或点外面关闭时保持 UNSET。
// 批 3 的迁移都靠轮询 m_Result，并且把「弹窗已经不在栈上」当成取消，所以这三条必须锁住。
#include <engine/keys.h>

#include <game/client/ui.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmConfirmPopupResult : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
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

		// 确认窗的按钮条贴底、左右等分：确认键在右侧。
		static CUIRect ConfirmButtonRect(const CUIRect &Popup)
		{
			CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
			Popup.HSplitBottom(CUi::SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
			ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, CUi::SConfirmPopupContext::POPUP_BUTTON_SPACING);
			return ConfirmButton;
		}
	};
}

TEST_F(QmConfirmPopupResult, ClickingConfirmWritesConfirmedResult)
{
	CUi::SConfirmPopupContext Context;
	Context.DefaultButtons();
	Context.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
	str_copy(Context.m_aMessage, "Delete this skin?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &Context);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	const CUIRect Button = ConfirmButtonRect(*pPopup);
	const vec2 Center{Button.x + Button.w * 0.5f, Button.y + Button.h * 0.5f};

	Frame(Center, false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	Frame(Center, true);
	Frame(Center, false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::CONFIRMED);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
}

TEST_F(QmConfirmPopupResult, CancelingWritesCanceledResult)
{
	CUi::SConfirmPopupContext Context;
	Context.DefaultButtons();
	str_copy(Context.m_aMessage, "Reset the controls?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &Context);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
	pPopup->HSplitBottom(CUi::SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
	ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, CUi::SConfirmPopupContext::POPUP_BUTTON_SPACING);
	const vec2 Center{CancelButton.x + CancelButton.w * 0.5f, CancelButton.y + CancelButton.h * 0.5f};

	Frame(Center, false);
	Frame(Center, true);
	Frame(Center, false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::CANCELED);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
}

TEST_F(QmConfirmPopupResult, EscapeClosesWithoutResultSoCallersTreatItAsCancel)
{
	CUi::SConfirmPopupContext Context;
	Context.DefaultButtons();
	str_copy(Context.m_aMessage, "Remove this friend?");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &Context);
	Frame({0.0f, 0.0f}, false);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&Context));
	Key(KEY_ESCAPE);
	Frame({0.0f, 0.0f}, false);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
	// 迁移点的轮询把「已经不在栈上」当取消：这里锁住这个前提，
	// 避免以后有人改成 Esc 也写 CANCELED 却不改调用方。
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
}
