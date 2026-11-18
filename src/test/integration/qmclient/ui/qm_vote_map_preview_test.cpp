// 投票页「预览地图」入口的真实交互闭环：图标按钮 → 公共地图预览面板 → Esc 关闭。
// 面板本体与服务器列表共用 CQmMapPreviewPanel；这里用 nullptr 客户端只验证外壳，
// 相机拖拽/缩放由 qm_browser_map_preview_input_test.cpp 直接覆盖。
#include <engine/keys.h>

#include <game/client/QmUi/QmVoteMapPreview.h>
#include <game/client/components/qmclient/map_preview_panel.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmVoteMapPreviewInteraction : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CQmMapPreviewPanel m_Panel;
		CButtonContainer m_Button;
		CQmMapPreviewPanel::SContext m_Context;
		CUIRect m_ButtonRect{40.0f, 40.0f, 26.0f, 26.0f};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			str_copy(m_Context.m_Target.m_aMapName, "Multea", sizeof(m_Context.m_Target.m_aMapName));
			str_copy(m_Context.m_Target.m_aCaption, "author · 4 / 5 · 21", sizeof(m_Context.m_Target.m_aCaption));
			Frame({10.0f, 10.0f}, false);
		}

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		vec2 ButtonCenter() const { return m_ButtonRect.Center(); }

		bool DrawButton(const bool Enabled = true)
		{
			return m_Panel.DoIconButton(ui_widget::ControlContext(&m_Ui), &m_Button, &m_Context, m_ButtonRect, m_Context.m_Target, Enabled);
		}

		// 按下 → 松开才算一次点击；先画一帧让按钮进入热区。
		void ClickButton(const bool Enabled = true)
		{
			Frame(ButtonCenter(), false);
			DrawButton(Enabled);
			Frame(ButtonCenter(), true);
			DrawButton(Enabled);
			Frame(ButtonCenter(), false);
			DrawButton(Enabled);
		}

		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}
	};
}

TEST_F(QmVoteMapPreviewInteraction, ClickingTheIconButtonOpensTheSharedPanel)
{
	Frame(ButtonCenter(), false);
	EXPECT_FALSE(DrawButton());
	Frame(ButtonCenter(), true);
	EXPECT_FALSE(DrawButton());
	Frame(ButtonCenter(), false);
	EXPECT_TRUE(DrawButton());
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Context));
	// 与服务器列表同一块二级面板矩形。
	const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*m_Ui.Screen());
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Context);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_FLOAT_EQ(pPopup->w, PanelRect.w);
	EXPECT_FLOAT_EQ(pPopup->h, PanelRect.h);
}

TEST_F(QmVoteMapPreviewInteraction, EscapeClosesThePanelAndTheButtonCanReopenIt)
{
	ClickButton();
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Context));

	Key(KEY_ESCAPE);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Context));

	ClickButton();
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Context));
}

TEST_F(QmVoteMapPreviewInteraction, DisabledButtonWithoutATargetNeverOpensThePanel)
{
	Frame(ButtonCenter(), false);
	DrawButton(false);
	Frame(ButtonCenter(), true);
	DrawButton(false);
	Frame(ButtonCenter(), false);
	EXPECT_FALSE(DrawButton(false));
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Context));
}

TEST(QmVoteMapPreviewCaption, MissingValuesBecomePlaceholders)
{
	qm_vote_map_preview::SInfo Info;
	char aBuf[192];
	qm_vote_map_preview::FormatCaption(Info, aBuf, sizeof(aBuf));
	ASSERT_NE(str_find(aBuf, "—"), nullptr);

	Info.m_Mapper = "Nudelsaft";
	Info.m_Stars = 4;
	Info.m_Points = 21;
	qm_vote_map_preview::FormatCaption(Info, aBuf, sizeof(aBuf));
	EXPECT_NE(str_find(aBuf, "Nudelsaft"), nullptr);
	EXPECT_NE(str_find(aBuf, "4 / 5"), nullptr);
	EXPECT_NE(str_find(aBuf, "21"), nullptr);
}
