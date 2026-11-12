// 确认弹窗的结果契约：点确认/取消写 m_Result，Esc 或点外面关闭时保持 UNSET。
// 批 3 的迁移都靠轮询 m_Result，并且把「弹窗已经不在栈上」当成取消，所以这三条必须锁住。
#include <engine/keys.h>

#include <game/client/QmUi/UiTokens.h>
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

		void ShowDisconnectPanel(CUi::SConfirmPopupContext &Context)
		{
			Context.DefaultButtons();
			Context.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
			str_copy(Context.m_aTitle, "Disconnect");
			str_copy(Context.m_aPositiveButtonLabel, "Disconnect");
			str_copy(Context.m_aMessage, "Are you sure that you want to disconnect?");
			m_Ui.ShowPopupConfirm(0.0f, 0.0f, &Context);
			UiGraphics()->m_vRoundedSubmissions.clear();
			Frame({0.0f, 0.0f}, false);
		}

		// 从真实绘制提交获取按钮坐标，不在测试中重新计算面板布局。
		CUIRect RenderedPanelButton(bool Confirm)
		{
			const auto &vSubmissions = UiGraphics()->m_vRoundedSubmissions;
			const vec4 Rect = vSubmissions.at(vSubmissions.size() - (Confirm ? 1 : 2)).m_Rect;
			return {Rect.x, Rect.y, Rect.z, Rect.w};
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

TEST_F(QmConfirmPopupResult, TitledPanelKeepsReadableSpacingAndConfirmsFromRenderedButton)
{
	CUi::SConfirmPopupContext Context;
	ShowDisconnectPanel(Context);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_GE(pPopup->w, ui_token::confirm_dialog::MIN_WIDTH);
	EXPECT_NEAR(pPopup->Center().x, m_Ui.Screen()->Center().x, 0.01f);
	EXPECT_NEAR(pPopup->Center().y, m_Ui.Screen()->Center().y, 0.01f);
	const CUIRect Confirm = RenderedPanelButton(true);
	const CUIRect Cancel = RenderedPanelButton(false);
	// 面板布局落在浮点坐标上，进场动画与 UiScale 换算会带来不足一像素的舍入；
	// 允许 1px 误差，仍能挡住「按钮贴边/挤在一起」这类真实回退。
	constexpr float SubPixelSlack = 1.0f;
	EXPECT_GE(Confirm.h, ui_token::confirm_dialog::BUTTON_HEIGHT - SubPixelSlack);
	EXPECT_GE(Cancel.x - pPopup->x, ui_token::spacing::LG - SubPixelSlack);
	EXPECT_GE(Confirm.x - (Cancel.x + Cancel.w), ui_token::spacing::SM - SubPixelSlack);
	EXPECT_GE(pPopup->y + pPopup->h - (Confirm.y + Confirm.h), ui_token::spacing::LG - SubPixelSlack);
	const auto &ConfirmSurface = UiGraphics()->m_vRoundedSubmissions.back();
	EXPECT_FLOAT_EQ(ConfirmSurface.m_FillColor.r, ui_token::color::DANGER.r);
	EXPECT_FLOAT_EQ(ConfirmSurface.m_FillColor.g, ui_token::color::DANGER.g);
	EXPECT_FLOAT_EQ(ConfirmSurface.m_FillColor.b, ui_token::color::DANGER.b);

	Frame(Confirm.Center(), false);
	Frame(Confirm.Center(), true);
	Frame(Confirm.Center(), false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::CONFIRMED);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
}

TEST_F(QmConfirmPopupResult, CancelingTitledPanelDoesNotConfirmAndReopenClearsResult)
{
	CUi::SConfirmPopupContext Context;
	ShowDisconnectPanel(Context);
	const CUIRect Cancel = RenderedPanelButton(false);
	Frame(Cancel.Center(), false);
	Frame(Cancel.Center(), true);
	Frame(Cancel.Center(), false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::CANCELED);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));

	ShowDisconnectPanel(Context);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	EXPECT_TRUE(m_Ui.IsPopupOpen(&Context));
}

TEST_F(QmConfirmPopupResult, TitledPanelIgnoresEnterAndEscapeClosesAsCancel)
{
	CUi::SConfirmPopupContext Context;
	ShowDisconnectPanel(Context);
	Key(KEY_RETURN);
	Frame({0.0f, 0.0f}, false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	EXPECT_TRUE(m_Ui.IsPopupOpen(&Context));

	Key(KEY_ESCAPE);
	Frame({0.0f, 0.0f}, false);
	EXPECT_EQ(Context.m_Result, CUi::SConfirmPopupContext::UNSET);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Context));
}

TEST_F(QmConfirmPopupResult, TitledPanelGrowsForLongWarningWithinViewport)
{
	CUi::SConfirmPopupContext Context;
	ShowDisconnectPanel(Context);
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	const float ShortHeight = pPopup->h;
	m_Ui.ClosePopupMenu(&Context);
	for(int i = 0; i < 4; ++i)
		str_append(Context.m_aMessage, "\n\nThere are unsaved changes in the touch controls editor. Saving first is recommended.");
	m_Ui.ShowPopupConfirm(0.0f, 0.0f, &Context);
	pPopup = m_Ui.GetPopupMenuRect(&Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_GT(pPopup->h, ShortHeight);
	EXPECT_LE(pPopup->w, ui_token::confirm_dialog::MAX_WIDTH);
	EXPECT_GE(pPopup->y, m_Ui.Screen()->y);
	EXPECT_LE(pPopup->y + pPopup->h, m_Ui.Screen()->y + m_Ui.Screen()->h);
}
