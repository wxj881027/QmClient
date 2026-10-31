#include <engine/shared/qm_ime_provider.h>

#include <game/client/QmUi/QmConsoleUi.h>
#include <game/client/qm_ime_manager.h>

#include <test/support/qm_ime_test_provider.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

#include <utility>

namespace
{
	// 硬件边界只装配事件与读取快照，不实现输入会话、编辑或 UI 路由。
	class CImeInput final : public qm_ui_test::CTestInput
	{
	public:
		std::vector<CEvent> m_vEvents;
		CQmImeProviderSession m_Session{[this](const char *pText) {
			CEvent Event{};
			Event.m_Flags = FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			m_vEvents.push_back(Event);
		}};
		void SetImeProvider(IImeProvider *pProvider) override { m_Session.SetProvider(pProvider); }
		void SetImeEngineEnabled(bool Enabled) override { m_Session.SetEnabled(Enabled); }
		void StartTextInput() override { m_Session.Start(); }
		void StopTextInput() override { m_Session.Stop(); }
		const char *GetComposition() const override { return m_Session.State().m_Preedit.c_str(); }
		bool HasComposition() const override { return QmImeCompositionIsActive(GetCompositionLength() > 0, GetCandidateCount()); }
		int GetCompositionCursor() const override { return m_Session.State().m_Cursor; }
		int GetCompositionLength() const override { return m_Session.State().m_Preedit.size(); }
		int GetCandidateCount() const override { return m_Session.State().m_vCandidates.size(); }
		const char *GetCandidate(int Index) const override { return m_Session.State().m_vCandidates.at(Index).c_str(); }
		void Key(int Code, bool Press = true, bool Repeat = false)
		{
			if(m_Session.OnKey(Code, 0, Press, Repeat))
				return;
			CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = Press ? FLAG_PRESS : FLAG_RELEASE;
			m_vEvents.push_back(Event);
		}
	};

	class QmImeEngineInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		CTestImeProvider m_Provider;
		CImeInput m_Input;
		CLineInputBuffered<128> m_Field;
		CQmImeTextInputSession m_Focus;
		IInput *UiInput() override { return &m_Input; }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			CLineInput::SetTextInputAutoManaged(true);
			m_Input.SetImeProvider(&m_Provider);
			m_Input.SetImeEngineEnabled(true);
			m_Field.Activate(EInputPriority::UI);
			m_Focus.UpdateFocus(m_Input, &m_Field);
		}
		void Composition()
		{
			m_Provider.m_State.m_Composing = true;
			m_Provider.m_State.m_Preedit = "nihao";
			m_Provider.m_State.m_Cursor = 5;
			m_Provider.m_State.m_vCandidates = {"你好", "拟好"};
			m_Provider.m_OnKey = [this](int Key, int, bool Press, bool) {
				if(!Press || !m_Provider.m_State.m_Composing)
					return false;
				if(Key == KEY_1)
					m_Provider.m_Commit("你好");
				else if(Key == KEY_RETURN || Key == KEY_KP_ENTER)
					m_Provider.m_Commit("nihao");
				else if(Key != KEY_ESCAPE)
					return false;
				m_Provider.m_State = {};
				m_Provider.m_State.m_Active = true;
				return true;
			};
		}
		void Route()
		{
			const auto vEvents = std::move(m_Input.m_vEvents);
			m_Input.m_vEvents.clear();
			for(const auto &Event : vEvents)
				m_Ui.OnInput(Event);
		}
	};
}

namespace
{
	class CConsoleCompositionInput final : public qm_ui_test::CTestInput
	{
	public:
		std::string m_Preedit = "d";
		vec2 m_Anchor;
		float m_LineHeight = 0.0f;
		const char *GetComposition() const override { return m_Composing ? m_Preedit.c_str() : ""; }
		int GetCompositionCursor() const override { return GetCompositionLength(); }
		int GetCompositionLength() const override { return m_Composing ? static_cast<int>(m_Preedit.size()) : 0; }
		void SetCompositionWindowPosition(float X, float Y, float Height) override
		{
			m_Anchor = {X, Y};
			m_LineHeight = Height;
		}
	};

	class QmImeConsoleInputLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		CConsoleCompositionInput m_Input;
		CLineInputBuffered<512> m_Field;
		IInput *UiInput() override { return &m_Input; }

		void ExpectSharedFirstLine(float FontSize, float Width, bool Wrapped)
		{
			m_Field.Activate(EInputPriority::CONSOLE);
			const CUIRect InputAnchor{30.0f, 120.0f, 0.0f, 0.0f};
			const auto Bounds = m_Field.Render(&InputAnchor, FontSize, TEXTALIGN_BL, true, Width, 1.0f);
			const auto Prompt = QmConsoleUi::DrawInputPrompt(*m_Ui.TextRender(), Bounds, 3.0f, FontSize, ">");
			EXPECT_NEAR(Prompt.m_StartY, Bounds.m_Y, 0.001f) << "prompt must share the first input line's baseline";
			EXPECT_NEAR(Prompt.m_AlignedFontSize, m_Input.m_LineHeight * 600.0f / 720.0f, 0.001f);
			const vec2 Caret = m_Field.GetCaretPosition();
			EXPECT_NEAR(m_Input.m_Anchor.x, Caret.x * 720.0f / 600.0f, 0.001f);
			EXPECT_NEAR(m_Input.m_Anchor.y, (Caret.y + Prompt.m_AlignedFontSize * 0.5f) * 720.0f / 600.0f, 0.001f);
			if(Wrapped)
			{
				EXPECT_GT(Bounds.m_LineCount, 1);
				EXPECT_GT(Caret.y, Prompt.m_StartY);
			}
			else
				EXPECT_EQ(Bounds.m_LineCount, 1);
		}

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
			pGraphics->SetFramebufferSize(1280, 720);
			pGraphics->MapScreen(0, 0, 1066.6667f, 600);
		}
	};
}

TEST_F(QmImeConsoleInputLayout, PinyinPreeditAndPromptShareBaselineAtDifferentFontSizes)
{
	m_Input.m_Composing = true;
	for(float Size : {10.0f, 14.0f, 22.0f})
	{
		SCOPED_TRACE(Size);
		ExpectSharedFirstLine(Size, 500.0f, false);
	}
}

TEST_F(QmImeConsoleInputLayout, CommittedTextAndImeTransitionKeepPromptOnInputLine)
{
	m_Field.Set("echo ");
	ExpectSharedFirstLine(14.0f, 500.0f, false);
	m_Input.m_Composing = true;
	m_Input.m_Preedit = "ni hao";
	ExpectSharedFirstLine(14.0f, 500.0f, false);
	m_Input.m_Composing = false;
	m_Field.Set("echo 你好");
	ExpectSharedFirstLine(14.0f, 500.0f, false);
}

TEST_F(QmImeConsoleInputLayout, WrappedCompositionKeepsPromptAtFirstLineAndAnchorAtCaret)
{
	m_Field.Set("echo this long console command wraps before the composition ");
	m_Input.m_Composing = true;
	m_Input.m_Preedit = "ni hao";
	ExpectSharedFirstLine(14.0f, 85.0f, true);
}

TEST_F(QmImeEngineInput, NumberSelectionCommitsOnceToRealLineInput)
{
	Composition();
	EXPECT_STREQ(m_Input.GetComposition(), "nihao");
	EXPECT_STREQ(m_Input.GetCandidate(0), "你好");
	m_Input.Key(KEY_1);
	ASSERT_EQ(m_Input.m_vEvents.size(), 1u);
	EXPECT_EQ(m_Input.m_vEvents.front().m_Flags, IInput::FLAG_TEXT);
	Route();
	m_Input.Key(KEY_1, false);
	EXPECT_TRUE(m_Input.m_vEvents.empty());
	Route();
	EXPECT_STREQ(m_Field.GetString(), "你好");
	EXPECT_FALSE(m_Input.HasComposition());
}

TEST_F(QmImeEngineInput, ComposingEnterCommitsOriginalWithoutSubmittingUiThenIdleEnterSubmits)
{
	Composition();
	m_Input.Key(KEY_RETURN);
	Route();
	EXPECT_STREQ(m_Field.GetString(), "nihao");
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	m_Input.Key(KEY_RETURN, false);
	Route();
	m_Input.Key(KEY_RETURN);
	Route();
	EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
}

TEST_F(QmImeEngineInput, HeldEnterAfterOriginalCommitDoesNotSubmitUi)
{
	Composition();
	m_Input.Key(KEY_RETURN);
	Route();
	m_Input.Key(KEY_RETURN, true, true);
	Route();
	EXPECT_STREQ(m_Field.GetString(), "nihao");
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	m_Input.Key(KEY_RETURN, false);
	m_Input.Key(KEY_RETURN);
	Route();
	EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
}

TEST_F(QmImeEngineInput, EscapeCancelsCompositionAndKeepsUiFocus)
{
	m_Field.Set("原文");
	Composition();
	m_Input.Key(KEY_ESCAPE);
	Route();
	EXPECT_STREQ(m_Field.GetString(), "原文");
	EXPECT_EQ(CLineInput::GetActiveInput(), &m_Field);
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	EXPECT_FALSE(m_Input.HasComposition());
}

TEST_F(QmImeEngineInput, FocusLossDiscardsCompositionWithoutTextEvent)
{
	Composition();
	m_Input.m_Session.OnFocusLost();
	Route();
	EXPECT_STREQ(m_Field.GetString(), "");
	EXPECT_FALSE(m_Input.HasComposition());
}

TEST_F(QmImeEngineInput, SwitchingInputOwnerCancelsOldCompositionBeforeNextCommit)
{
	Composition();
	CLineInputBuffered<128> Other;
	m_Field.Deactivate();
	Other.Activate(EInputPriority::UI);
	m_Focus.UpdateFocus(m_Input, &Other);
	EXPECT_FALSE(m_Input.HasComposition());
	m_Provider.m_Commit("新输入框");
	Route();
	EXPECT_STREQ(m_Field.GetString(), "");
	EXPECT_STREQ(Other.GetString(), "新输入框");
	Other.Deactivate();
}
