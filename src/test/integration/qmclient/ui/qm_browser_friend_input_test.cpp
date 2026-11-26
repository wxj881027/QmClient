#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/qmclient/browser_friend_actions.h>
#include <game/client/qm_icon.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmBrowserFriendInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		const CUIRect m_Row{80.0f, 100.0f, 180.0f, 30.0f};
		int m_RowId = 0;
		int m_CopyId = 0;
		int m_RowResult = 0;
		int m_CopyResult = 0;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void BeginFrame(vec2 Position, int Key = 0)
		{
			const vec2 Pixels = Position * vec2(Graphics().WindowWidth(), Graphics().WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Key;
			m_Client.AdvanceFrame();
			m_Ui.StartCheck();
			m_Ui.Update();
		}

		void ProcessRow()
		{
			const auto Actions = QmBrowserFriendActions(m_Row, m_Ui.MouseHovered(&m_Row), true, true, false);
			m_RowResult = m_Ui.DoButtonLogic(&m_RowId, 0, &m_Row, Actions.RowButtonFlags(m_Ui.MousePos()));
			m_CopyResult = 0;
			if(Actions.m_Copy)
			{
				const CUIRect Hit = Actions.HitRect(*Actions.m_Copy);
				if(m_Ui.MouseButtonClicked(0) && m_Ui.MouseHovered(&Hit))
					EXPECT_NE(m_Ui.ActiveItem(), &m_RowId);
				m_CopyResult = ui_widget::DoIconButton(ui_widget::ControlContext(&m_Ui), &m_CopyId, EQmIcon::COPY, nullptr, 0, *Actions.m_Copy, Actions.ACTION_BUTTON_FLAGS, IGraphics::CORNER_ALL, true, std::nullopt, false, true, &Hit);
			}
			m_Ui.FinishCheck();
		}
	};
}

TEST_F(QmBrowserFriendInput, MovingFromHotRowAndPressingInSameFrameClicksOnlyCopyOnRelease)
{
	const auto Actions = QmBrowserFriendActions(m_Row, true, true, true, false);
	ASSERT_TRUE(Actions.m_Copy);
	const vec2 Target{Actions.m_Copy->Center().x, Actions.m_Copy->y - 1.0f};
	BeginFrame(m_Row.Center());
	ProcessRow();

	BeginFrame(Target, KEY_MOUSE_1);
	ASSERT_EQ(m_Ui.HotItem(), &m_RowId);
	ProcessRow();
	EXPECT_EQ(m_Ui.ActiveItem(), &m_CopyId);
	EXPECT_EQ(m_CopyResult, 0);
	EXPECT_EQ(m_RowResult, 0);

	BeginFrame(Target);
	ProcessRow();
	EXPECT_EQ(m_CopyResult, 1);
	EXPECT_EQ(m_RowResult, 0);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
}

TEST_F(QmBrowserFriendInput, ReleasingCopyOnRowCancelsWithoutSelectingServer)
{
	const auto Actions = QmBrowserFriendActions(m_Row, true, true, true, false);
	ASSERT_TRUE(Actions.m_Copy);
	BeginFrame(m_Row.Center());
	ProcessRow();
	BeginFrame(Actions.m_Copy->Center(), KEY_MOUSE_1);
	ProcessRow();
	ASSERT_EQ(m_Ui.ActiveItem(), &m_CopyId);

	BeginFrame(m_Row.Center());
	ProcessRow();
	EXPECT_EQ(m_CopyResult, 0);
	EXPECT_EQ(m_RowResult, 0);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
}

TEST_F(QmBrowserFriendInput, RightClickOnCopyStillOpensRowActions)
{
	const auto Actions = QmBrowserFriendActions(m_Row, true, true, true, false);
	ASSERT_TRUE(Actions.m_Copy);
	const vec2 Target = Actions.m_Copy->Center();
	BeginFrame(Target);
	ProcessRow();
	BeginFrame(Target, KEY_MOUSE_2);
	ProcessRow();
	ASSERT_EQ(m_Ui.ActiveItem(), &m_RowId);
	BeginFrame(Target);
	ProcessRow();
	EXPECT_EQ(m_RowResult, 2);
	EXPECT_EQ(m_CopyResult, 0);
}
