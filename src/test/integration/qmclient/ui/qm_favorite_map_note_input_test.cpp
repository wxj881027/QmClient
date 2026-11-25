#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/qmclient/favorite_map_note_input.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmFavoriteMapNoteInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		struct SRow
		{
			const char *m_pMap;
			std::string m_Note;
			CUIRect m_Rect;
			int m_NoteId = 0;
			int m_RemoveId = 0;
			bool m_Favorite = true;
		};

		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CQmFavoriteMapNoteInput m_Editor;
		SRow m_A{"A", "original A", {80.0f, 100.0f, 180.0f, 20.0f}};
		SRow m_B{"B", "original B", {80.0f, 134.0f, 180.0f, 20.0f}};
		bool m_EscapeReachedMenu = false;
		std::optional<CUIRect> m_Clip;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Save(const std::optional<SQmFavoriteMapNoteChange> &Change)
		{
			if(Change)
				(Change->m_Map == m_A.m_pMap ? m_A : m_B).m_Note = Change->m_Note;
		}

		CUIRect RemoveRect(const SRow &Row) const
		{
			return {Row.m_Rect.x + Row.m_Rect.w + 8.0f, Row.m_Rect.y, 20.0f, Row.m_Rect.h};
		}

		void RenderRow(SRow &Row, const IUiContext &Context)
		{
			if(!Row.m_Favorite)
				return;
			const CUIRect Remove = RemoveRect(Row);
			if(m_Ui.DoButtonLogic(&Row.m_RemoveId, 0, &Remove, BUTTONFLAG_LEFT))
			{
				Row.m_Favorite = false;
				if(m_Editor.IsEditing(Row.m_pMap))
					m_Editor.Cancel(m_Ui);
				return;
			}
			if(!m_Editor.IsEditing(Row.m_pMap))
				Save(m_Editor.Click(m_Ui, &Row.m_NoteId, Row.m_Rect, Row.m_pMap, Row.m_Note.c_str()));
			if(m_Editor.IsEditing(Row.m_pMap) && m_Editor.Render(Context, Row.m_Rect, &Row.m_RemoveId, "Map note"))
				Save(m_Editor.Commit(m_Ui));
		}

		void Frame(vec2 Position, bool Pressed = false)
		{
			const vec2 Pixels = Position * vec2(Graphics().WindowWidth(), Graphics().WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.StartCheck();
			m_Ui.Update();
			m_Editor.CancelOnEscape(m_Ui);
			const auto Context = ui_widget::ControlContext(&m_Ui);
			if(m_Clip)
				m_Ui.ClipEnable(&*m_Clip);
			RenderRow(m_A, Context);
			RenderRow(m_B, Context);
			if(m_Clip)
				m_Ui.ClipDisable();
			m_EscapeReachedMenu = m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE);
			m_Ui.FinishCheck();
			m_Ui.ClearHotkeys();
		}

		void Click(vec2 Position)
		{
			Frame(Position, true);
			Frame(Position);
		}

		void Open(SRow &Row)
		{
			const vec2 Position = Row.m_Rect.Center();
			Frame(Position);
			Click(Position);
			Click(Position);
		}

		bool Text(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			return m_Ui.OnInput(Event);
		}

		void PressKey(int Key)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_PRESS;
			Event.m_Key = Key;
			EXPECT_TRUE(m_Ui.OnInput(Event));
		}
	};
}

TEST_F(QmFavoriteMapNoteInput, SecondReleaseFocusesAndSelectsAllForImmediateTypingAndEnter)
{
	const vec2 Position = m_A.m_Rect.Center();
	Frame(Position);
	Click(Position);
	EXPECT_FALSE(m_Editor.IsEditing());
	Frame(Position, true);
	EXPECT_FALSE(m_Editor.IsEditing());
	Frame(Position);
	ASSERT_TRUE(m_Editor.IsEditing("A"));
	ASSERT_NE(CLineInput::GetActiveInput(), nullptr);
	EXPECT_EQ(CLineInput::GetActiveInput()->GetSelectionLength(), m_A.m_Note.size());

	ASSERT_TRUE(Text("replacement"));
	Frame(Position);
	EXPECT_EQ(m_A.m_Note, "original A");
	PressKey(KEY_RETURN);
	Frame(Position);
	EXPECT_EQ(m_A.m_Note, "replacement");
	EXPECT_FALSE(m_Editor.IsEditing());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFavoriteMapNoteInput, ClickingOutsideSavesTheDraft)
{
	Open(m_A);
	ASSERT_TRUE(Text("changed"));
	Click({350.0f, 230.0f});
	EXPECT_EQ(m_A.m_Note, "changed");
	EXPECT_FALSE(m_Editor.IsEditing());
}

TEST_F(QmFavoriteMapNoteInput, DoubleClickingAnotherNoteSavesTheFirstAndFocusesTheSecond)
{
	Open(m_A);
	ASSERT_TRUE(Text("changed A"));
	Open(m_B);
	EXPECT_EQ(m_A.m_Note, "changed A");
	EXPECT_TRUE(m_Editor.IsEditing("B"));
	ASSERT_TRUE(Text("changed B"));
	PressKey(KEY_RETURN);
	Frame(m_B.m_Rect.Center());
	EXPECT_EQ(m_B.m_Note, "changed B");
}

TEST_F(QmFavoriteMapNoteInput, EscapeDiscardsTheDraftAndDoesNotReachTheMenu)
{
	Open(m_A);
	ASSERT_TRUE(Text("discard this"));
	PressKey(KEY_ESCAPE);
	Frame(m_A.m_Rect.Center());

	EXPECT_EQ(m_A.m_Note, "original A");
	EXPECT_FALSE(m_EscapeReachedMenu);
	EXPECT_FALSE(m_Editor.IsEditing());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFavoriteMapNoteInput, ClearButtonKeepsEditingAndEnterRemovesTheNote)
{
	Open(m_A);
	const vec2 Clear = ui_widget::ResolveInputFieldLayout(m_A.m_Rect, false, true, 1.0f, 0.0f, 11.0f).m_ClearRect.Center();
	Frame(Clear);
	Click(Clear);
	ASSERT_TRUE(m_Editor.IsEditing("A"));
	ASSERT_NE(CLineInput::GetActiveInput(), nullptr);
	EXPECT_TRUE(CLineInput::GetActiveInput()->IsEmpty());
	EXPECT_EQ(m_A.m_Note, "original A");

	PressKey(KEY_RETURN);
	Frame(Clear);
	EXPECT_TRUE(m_A.m_Note.empty());
	EXPECT_FALSE(m_Editor.IsEditing());
}

TEST_F(QmFavoriteMapNoteInput, RemovingTheEditingFavoriteDiscardsItsDraft)
{
	Open(m_A);
	ASSERT_TRUE(Text("discard this"));
	const vec2 Remove = RemoveRect(m_A).Center();
	Frame(Remove);
	Frame(Remove, true);
	ASSERT_TRUE(m_Editor.IsEditing("A"));
	EXPECT_EQ(m_A.m_Note, "original A");
	Frame(Remove);

	EXPECT_FALSE(m_A.m_Favorite);
	EXPECT_EQ(m_A.m_Note, "original A");
	EXPECT_FALSE(m_Editor.IsEditing());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFavoriteMapNoteInput, FullyClippingTheInputSavesTheDraftAndReleasesFocus)
{
	Open(m_A);
	ASSERT_TRUE(Text("changed"));
	m_Clip = CUIRect{70.0f, 130.0f, 250.0f, 60.0f};
	Frame(m_B.m_Rect.Center());
	EXPECT_EQ(m_A.m_Note, "changed");
	EXPECT_FALSE(m_Editor.IsEditing());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}
