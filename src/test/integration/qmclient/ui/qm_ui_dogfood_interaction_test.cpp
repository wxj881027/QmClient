// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/UiDogfood.h>

#include <gtest/gtest.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmUiDogfoodInteraction : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUiV2Tree m_Tree;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame(vec2 Pointer = {0.0f, 0.0f}, bool LeftDown = false, int Key = 0)
		{
			const vec2 Pixels = Pointer * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = LeftDown ? KEY_MOUSE_1 : 0;
			m_Input.m_Modifier = Key == KEY_F;
			m_Input.m_RawPressedKey = Key;
			m_Ui.ClearHotkeys();
			if(Key == KEY_ESCAPE)
			{
				IInput::CEvent Event{};
				Event.m_Key = Key;
				Event.m_Flags = IInput::FLAG_PRESS;
				m_Ui.OnInput(Event);
			}

			m_Client.AdvanceFrame();
			m_Ui.StartCheck();
			m_Ui.Update();
			m_Ui.MapScreen();
			m_Tree.BeginFrame();

			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pTextRender = m_Ui.TextRender();
			Ctx.m_pAnim = &m_AnimRuntime;
			Ctx.m_pTree = &m_Tree;
			Ctx.m_ScopeHash = MakeUiScopeHash("qm_ui_dogfood");
			RenderQmUiDogfood(Ctx, {0.0f, 0.0f, 640.0f, 560.0f});

			m_Tree.EndFrame(m_AnimRuntime);
			m_Ui.FinishCheck();
		}

		void OpenModal(vec2 Button, int ReleaseKey = 0)
		{
			Frame(Button);
			Frame(Button, true);
			Frame(Button, false, ReleaseKey);
		}

		bool EscapePending() const { return m_Ui.HotkeyPressed(CUi::HOTKEY_ESCAPE); }
	};
} // namespace

TEST_F(QmUiDogfoodInteraction, SearchHotkeyStartsOneTextInput)
{
	Frame({}, false, KEY_F);
	EXPECT_NE(CLineInput::GetActiveInput(), nullptr);
	EXPECT_EQ(m_Input.m_TextInputStarts, 1);
	EXPECT_EQ(m_Input.m_TextInputStops, 0);
}

TEST_F(QmUiDogfoodInteraction, OpenModalBlocksUnderlyingSearch)
{
	// 左列打开模态的同一帧按 Ctrl+F，右列不会抢走焦点。
	OpenModal({75.0f, 474.0f}, KEY_F);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);

	// 右列搜索框仍在遮罩下绘制。
	const vec2 Search{400.0f, 191.0f};
	Frame(Search);
	Frame(Search, true);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Frame(Search);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);

	Frame({}, false, KEY_F);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);

	Frame({}, false, KEY_ESCAPE);
	EXPECT_FALSE(EscapePending());
}

TEST_F(QmUiDogfoodInteraction, EscapeDuringModalExitRemainsAvailable)
{
	g_Config.m_QmUiMotionLevel = 2;
	OpenModal({400.0f, 382.0f});

	Frame({}, false, KEY_ESCAPE);
	EXPECT_FALSE(EscapePending());

	Frame({}, false, KEY_ESCAPE);
	EXPECT_TRUE(EscapePending());
}
