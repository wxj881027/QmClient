#include <game/client/components/tclient/bindchat.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <algorithm>
#include <vector>

namespace
{
	class QmChatBindNameInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CBindChat::CBindDefault m_First{}, m_Second{};
		CUIRect m_Rect{10, 10, 160, 20};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Focus(CBindChat::CBindDefault &Field)
		{
			m_Ui.SetActiveItem(&Field.m_LineInput);
			m_Ui.DoEditBox(&Field.m_LineInput, &m_Rect, 12);
			ASSERT_TRUE(Field.m_LineInput.IsActive());
		}

		bool Text(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			return m_Ui.OnInput(Event);
		}

		CBindChat::CBind Binding(const char *pName)
		{
			CBindChat::CBind Result{};
			str_copy(Result.m_aName, pName);
			str_copy(Result.m_aCommand, "echo bind");
			return Result;
		}
	};
}

TEST_F(QmChatBindNameInput, UnfocusedInputCopiesTheCurrentNameWithoutBorrowingIt)
{
	auto Bind = Binding("!old");
	m_First.PrepareNameInput(Bind.m_aName);
	EXPECT_NE(m_First.m_LineInput.GetString(), Bind.m_aName);
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!old");
	str_copy(Bind.m_aName, "!updated");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!old");
	m_First.PrepareNameInput(Bind.m_aName);
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!updated");
	EXPECT_EQ(m_First.m_LineInput.GetLength(), 8u);
}

TEST_F(QmChatBindNameInput, ActiveInputRemainsWritableAfterBindingVectorReallocation)
{
	std::vector<CBindChat::CBind> vBindings{Binding("!old")};
	m_First.PrepareNameInput(vBindings[0].m_aName);
	const char *pInputBuffer = m_First.m_LineInput.GetString();
	Focus(m_First);
	vBindings.reserve(vBindings.capacity() + 8);
	// 下一次卡片Prepare之前，实际CUi已可能分发键盘输入。
	ASSERT_TRUE(Text("x"));
	EXPECT_EQ(m_First.m_LineInput.GetString(), pInputBuffer);
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!oldx");
	EXPECT_STREQ(vBindings[0].m_aName, "!old");
}

TEST_F(QmChatBindNameInput, ActiveInputRemainsWritableAfterAllBindingsAreDestroyed)
{
	std::vector<CBindChat::CBind> vBindings{Binding("!old")};
	m_First.PrepareNameInput(vBindings[0].m_aName);
	Focus(m_First);
	std::vector<CBindChat::CBind>().swap(vBindings);
	ASSERT_TRUE(Text("x"));
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!oldx");
	m_First.PrepareNameInput("");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!oldx");
	EXPECT_TRUE(m_First.m_LineInput.IsActive());
}

TEST_F(QmChatBindNameInput, ReorderingBindingsDoesNotRedirectInputIntoAnotherEntry)
{
	std::vector<CBindChat::CBind> vBindings{Binding("!a"), Binding("!b")};
	m_First.PrepareNameInput(vBindings[0].m_aName);
	Focus(m_First);
	std::swap(vBindings[0], vBindings[1]);
	ASSERT_TRUE(Text("x"));
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!ax");
	EXPECT_STREQ(vBindings[0].m_aName, "!b");
	EXPECT_STREQ(vBindings[1].m_aName, "!a");
}

TEST_F(QmChatBindNameInput, PreparingAnActiveFieldPreservesItsUnpublishedInput)
{
	m_First.PrepareNameInput("!old");
	Focus(m_First);
	ASSERT_TRUE(Text("x"));
	m_First.PrepareNameInput("!external");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!oldx");
	EXPECT_EQ(m_First.m_LineInput.GetCursorOffset(), 5u);
}

TEST_F(QmChatBindNameInput, LosingFocusSynchronizesTheCurrentBindingOrItsRemoval)
{
	m_First.PrepareNameInput("!old");
	Focus(m_First);
	ASSERT_TRUE(Text("x"));
	ASSERT_TRUE(m_Ui.DoEditBox(&m_First.m_LineInput, &m_Rect, 12));
	m_Ui.ReleaseActiveTextInput(&m_First.m_LineInput);
	m_First.PrepareNameInput("!external");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!external");
	EXPECT_EQ(m_First.m_LineInput.GetLength(), 9u);
	m_First.PrepareNameInput("");
	EXPECT_TRUE(m_First.m_LineInput.IsEmpty());
	EXPECT_EQ(m_First.m_LineInput.GetCursorOffset(), 0u);
}

TEST_F(QmChatBindNameInput, UnboundDefaultsKeepIndependentEditingBuffers)
{
	m_First.PrepareNameInput("");
	m_Second.PrepareNameInput("");
	EXPECT_NE(m_First.m_LineInput.GetString(), m_Second.m_LineInput.GetString());
	Focus(m_First);
	ASSERT_TRUE(Text("!new"));
	m_Second.PrepareNameInput("");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!new");
	EXPECT_TRUE(m_Second.m_LineInput.IsEmpty());
}

TEST_F(QmChatBindNameInput, DefaultCopiesBeforeFirstUseBindTheirOwnFinalStorage)
{
	CBindChat::CBindDefault Prototype{};
	std::vector<CBindChat::CBindDefault> vDefaults(2, Prototype);
	vDefaults[0].PrepareNameInput("!first");
	vDefaults[1].PrepareNameInput("!second");
	EXPECT_EQ(vDefaults[0].m_LineInput.GetString(), vDefaults[0].m_aEditName);
	EXPECT_EQ(vDefaults[1].m_LineInput.GetString(), vDefaults[1].m_aEditName);
	EXPECT_NE(vDefaults[0].m_LineInput.GetString(), vDefaults[1].m_LineInput.GetString());
	EXPECT_STREQ(vDefaults[0].m_LineInput.GetString(), "!first");
	EXPECT_STREQ(vDefaults[1].m_LineInput.GetString(), "!second");
}

TEST_F(QmChatBindNameInput, FocusLossDoesNotDiscardAnEditWaitingForItsCard)
{
	m_First.PrepareNameInput("!old");
	Focus(m_First);
	ASSERT_TRUE(Text("x"));
	m_Ui.ReleaseActiveTextInput(&m_First.m_LineInput);
	ASSERT_TRUE(m_First.m_LineInput.HasPendingChanges());
	m_First.PrepareNameInput("!external");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!oldx");
	EXPECT_TRUE(m_First.m_LineInput.HasPendingChanges());
	EXPECT_TRUE(m_Ui.DoEditBox(&m_First.m_LineInput, &m_Rect, 12));
	EXPECT_FALSE(m_First.m_LineInput.HasPendingChanges());
	m_First.PrepareNameInput("!external");
	EXPECT_STREQ(m_First.m_LineInput.GetString(), "!external");
}
