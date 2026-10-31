#include <base/logger.h>
#include <engine/shared/config.h>
#include <engine/shared/qm_ime_policy.h>
#include <engine/shared/qm_ime_provider.h>

#include <test/support/qm_ime_test_provider.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	class QmImeProvider : public ::testing::Test
	{
	protected:
		std::vector<std::string> m_vCommits;
		CTestImeProvider m_Provider;
		int m_PreviousTrace = 0;
		CQmImeProviderSession m_Session{[this](const char *pText) { m_vCommits.emplace_back(pText); }};
		void SetUp() override
		{
			m_PreviousTrace = g_Config.m_QmImeTrace;
			g_Config.m_QmImeTrace = 0;
			m_Session.SetProvider(&m_Provider);
			m_Session.SetEnabled(true);
			m_Session.Start();
		}
		void TearDown() override { g_Config.m_QmImeTrace = m_PreviousTrace; }
	};
}

TEST_F(QmImeProvider, RepeatedStartDoesNotCreateAnotherActiveSession)
{
	m_Session.Start();
	EXPECT_TRUE(m_Session.IsActive());
	EXPECT_EQ(m_Provider.m_Activations, 1);
}

TEST_F(QmImeProvider, ModeChangeCancelsCompositionAndReactivatesOnlyWhenRequested)
{
	m_Provider.m_State.m_Preedit = "ni";
	m_Session.SetEnabled(false);
	EXPECT_FALSE(m_Session.IsActive());
	EXPECT_TRUE(m_Session.State().m_Preedit.empty());
	EXPECT_TRUE(m_vCommits.empty());
	m_Session.SetEnabled(true);
	EXPECT_TRUE(m_Session.IsActive());
	EXPECT_EQ(m_Provider.m_Activations, 2);
	m_Session.Stop();
	m_Session.SetEnabled(false);
	m_Session.SetEnabled(true);
	EXPECT_FALSE(m_Session.IsActive());
}

TEST_F(QmImeProvider, StoppedRequestStaysStoppedUntilExplicitStart)
{
	m_Session.Stop();
	m_Session.OnFrame();
	EXPECT_FALSE(m_Session.TextInputRequested());
	EXPECT_FALSE(m_Session.IsActive());
	EXPECT_EQ(m_Provider.m_Activations, 1);
	m_Session.Start();
	EXPECT_TRUE(m_Session.TextInputRequested());
	EXPECT_TRUE(m_Session.IsActive());
	EXPECT_EQ(m_Provider.m_Activations, 2);
}

TEST_F(QmImeProvider, TraceDistinguishesProviderLossFromStoppedRequestWithoutLoggingText)
{
	CMemoryLogger Logger;
	CLogScope Scope(&Logger);
	g_Config.m_QmImeTrace = 1;
	m_Provider.Deactivate();
	m_Provider.m_State.m_Preedit = "测试用的私人文本";
	m_Session.OnKey(KEY_A, 0, true, false);
	const std::string Lost = Logger.ConcatenatedLines();
	EXPECT_NE(Lost.find("requested=1 provider_enabled=1 provider_active=0 active=0"), std::string::npos);
	EXPECT_EQ(Lost.find(m_Provider.m_State.m_Preedit), std::string::npos);
	m_Session.Stop();
	EXPECT_NE(Logger.ConcatenatedLines().find("event=stop enabled=1 requested=0"), std::string::npos);
}

TEST_F(QmImeProvider, DisabledTraceEmitsNoSessionDiagnostics)
{
	CMemoryLogger Logger;
	CLogScope Scope(&Logger);
	m_Session.OnKey(KEY_A, 0, true, false);
	m_Session.Stop();
	m_Session.Start();
	m_Session.OnFrame();
	EXPECT_TRUE(Logger.Lines().empty());
}

TEST_F(QmImeProvider, ConsumedPressDropsItsReleaseEvenIfDecoderDoesNotHandleRelease)
{
	m_Provider.m_OnKey = [](int, int, bool Press, bool) { return Press; };
	EXPECT_TRUE(m_Session.OnKey(KEY_A, 0, true, false));
	EXPECT_TRUE(m_Session.OnKey(KEY_A, 0, false, false));
	EXPECT_FALSE(m_Session.OnKey(KEY_A, 0, false, false));
}

TEST_F(QmImeProvider, UnconsumedPressAlwaysForwardsItsRelease)
{
	m_Provider.m_OnKey = [](int, int, bool Press, bool) { return !Press; };
	EXPECT_FALSE(m_Session.OnKey(KEY_LCTRL, 0, true, false));
	EXPECT_FALSE(m_Session.OnKey(KEY_LCTRL, 0, false, false));
}

TEST_F(QmImeProvider, StopKeepsConsumedKeyPairUntilThePhysicalRelease)
{
	m_Provider.m_OnKey = [](int, int, bool, bool) { return true; };
	ASSERT_TRUE(m_Session.OnKey(KEY_RETURN, 0, true, false));
	m_Session.Stop();
	EXPECT_TRUE(m_Session.OnKey(KEY_RETURN, 0, true, true));
	EXPECT_TRUE(m_Session.OnKey(KEY_RETURN, 0, false, false));
	EXPECT_FALSE(m_Session.OnKey(KEY_RETURN, 0, true, false));
}

TEST_F(QmImeProvider, FocusLossCancelsWithoutCommitAndClearsConsumedKeys)
{
	m_Provider.m_State.m_Preedit = "ni";
	m_Provider.m_OnKey = [](int, int, bool Press, bool) { return Press; };
	m_Session.OnKey(KEY_LSHIFT, 0, true, false);
	m_Session.OnFocusLost();
	EXPECT_EQ(m_Provider.m_FocusLosses, 1);
	EXPECT_TRUE(m_Session.State().m_Preedit.empty());
	EXPECT_TRUE(m_vCommits.empty());
	EXPECT_FALSE(m_Session.OnKey(KEY_LSHIFT, 0, false, false));
}

TEST_F(QmImeProvider, DecoderCommitsStayOrderedAndEmptyCommitsAreIgnored)
{
	m_Provider.m_OnKey = [this](int, int, bool, bool) {
		m_Provider.m_Commit("");
		m_Provider.m_Commit("你好");
		m_Provider.m_Commit("世界");
		return true;
	};
	m_Session.OnKey(KEY_1, 0, true, false);
	EXPECT_EQ(m_vCommits, (std::vector<std::string>{"你好", "世界"}));
}

TEST_F(QmImeProvider, StoppedSessionRejectsLateDecoderCommit)
{
	m_Session.Stop();
	m_Provider.m_Commit("迟到");
	EXPECT_TRUE(m_vCommits.empty());
}

TEST_F(QmImeProvider, DetachRemovesBorrowedCallback)
{
	m_Session.SetProvider(nullptr);
	EXPECT_FALSE(static_cast<bool>(m_Provider.m_Commit));
	EXPECT_FALSE(m_Session.IsActive());
	EXPECT_FALSE(m_Session.OnKey(KEY_A, 0, true, false));
}

TEST_F(QmImeProvider, FailedActivationLeavesKeysAvailableForSystemPath)
{
	m_Session.Stop();
	m_Provider.m_RefuseActivation = true;
	m_Session.Start();
	EXPECT_FALSE(m_Session.IsActive());
	EXPECT_TRUE(m_Session.TextInputRequested());
	EXPECT_FALSE(m_Session.OnKey(KEY_A, 0, true, false));
}

TEST_F(QmImeProvider, FrameFailureClearsExposedComposition)
{
	m_Provider.m_State.m_Preedit = "ni";
	m_Provider.m_OnFrame = [this] { m_Provider.Deactivate(); };
	m_Session.OnFrame();
	EXPECT_FALSE(m_Session.State().m_Active);
	EXPECT_TRUE(m_Session.State().m_Preedit.empty());
}

TEST_F(QmImeProvider, FrameCanFinishDeploymentWithoutAnotherInputActivation)
{
	m_Session.Stop();
	m_Provider.m_RefuseActivation = true;
	m_Session.Start();
	m_Provider.m_OnFrame = [this] { m_Provider.m_State.m_Active = true; };
	m_Session.OnFrame();
	EXPECT_TRUE(m_Session.IsActive());
}

TEST_F(QmImeProvider, SnapshotKeepsUtf8CursorAndCandidatePageTogether)
{
	m_Provider.m_State.m_Composing = true;
	m_Provider.m_State.m_Preedit = "你好ni";
	m_Provider.m_State.m_Cursor = 6;
	m_Provider.m_State.m_vCandidates = {"你", "拟"};
	m_Provider.m_State.m_Selected = 1;
	m_Provider.m_State.m_PageStart = 9;
	m_Provider.m_State.m_PageSize = 9;
	m_Provider.m_State.m_TotalCount = 12;
	CQmImeCandidateState Candidates;
	Candidates.EndComposition();
	Candidates.UpdateFromEngine(m_Session.State());
	EXPECT_EQ(m_Session.State().m_Cursor, 6);
	EXPECT_EQ(Candidates.Candidates().m_vCandidates, m_Provider.m_State.m_vCandidates);
	EXPECT_EQ(Candidates.Candidates().m_SelectedIndex, 1);
	EXPECT_EQ(Candidates.Candidates().m_PageStart, 9);
	EXPECT_EQ(Candidates.Candidates().m_TotalCount, 12);
}

TEST(QmImeProviderState, InactiveSnapshotClearsOldCandidatePage)
{
	CQmImeCandidateState Candidates;
	SQmImeProviderState State;
	State.m_Active = true;
	State.m_vCandidates = {"你好"};
	State.m_Selected = 20;
	Candidates.UpdateFromEngine(State);
	EXPECT_EQ(Candidates.Candidates().m_SelectedIndex, -1);
	State.m_Active = false;
	Candidates.UpdateFromEngine(State);
	EXPECT_TRUE(Candidates.Candidates().m_vCandidates.empty());
	EXPECT_EQ(Candidates.Candidates().m_PageSize, 0);
}

TEST(QmImeKeyMapping, UsLettersRespectShiftAndCapsLock)
{
	EXPECT_EQ(QmImeKeyToKeysym(KEY_A, 0), 'a');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_Z, QM_IME_KEYMASK_SHIFT), 'Z');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_A, QM_IME_KEYMASK_LOCK), 'A');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_A, QM_IME_KEYMASK_SHIFT | QM_IME_KEYMASK_LOCK), 'a');
}

TEST(QmImeKeyMapping, NumberRowAndShiftedPunctuationUseUsLayout)
{
	EXPECT_EQ(QmImeKeyToKeysym(KEY_1, 0), '1');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_9, 0), '9');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_0, 0), '0');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_1, QM_IME_KEYMASK_SHIFT), '!');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_0, QM_IME_KEYMASK_SHIFT), ')');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_SLASH, 0), '/');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_SLASH, QM_IME_KEYMASK_SHIFT), '?');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_BACKSLASH, 0), '\\');
	EXPECT_EQ(QmImeKeyToKeysym(KEY_APOSTROPHE, QM_IME_KEYMASK_SHIFT), '"');
}

TEST(QmImeKeyMapping, EditingAndPagingKeysUseX11Keysyms)
{
	const std::pair<int, int> aCases[] = {
		{KEY_RETURN, 0xff0d}, {KEY_KP_ENTER, 0xff0d}, {KEY_BACKSPACE, 0xff08},
		{KEY_ESCAPE, 0xff1b}, {KEY_DELETE, 0xffff}, {KEY_HOME, 0xff50},
		{KEY_LEFT, 0xff51}, {KEY_UP, 0xff52}, {KEY_RIGHT, 0xff53}, {KEY_DOWN, 0xff54},
		{KEY_PAGEUP, 0xff55}, {KEY_PAGEDOWN, 0xff56}, {KEY_END, 0xff57}};
	for(const auto &[Key, Keysym] : aCases)
	{
		SCOPED_TRACE(Key);
		EXPECT_EQ(QmImeKeyToKeysym(Key, 0), Keysym);
	}
	EXPECT_EQ(QmImeKeyToKeysym(KEY_F10, 0), 0);
}

TEST(QmImeKeyMapping, ShortcutsAndTabPassThroughDuringComposition)
{
	for(const int Modifier : {QM_IME_KEYMASK_CONTROL, QM_IME_KEYMASK_ALT, QM_IME_KEYMASK_GUI})
		EXPECT_TRUE(QmImeKeyShouldPassThrough(KEY_V, Modifier, true));
	EXPECT_TRUE(QmImeKeyShouldPassThrough(KEY_TAB, 0, true));
	EXPECT_TRUE(QmImeKeyShouldPassThrough(KEY_RETURN, 0, false));
	EXPECT_FALSE(QmImeKeyShouldPassThrough(KEY_RETURN, 0, true));
	EXPECT_TRUE(QmImeKeyShouldPassThrough(KEY_BACKSPACE, 0, false));
	EXPECT_FALSE(QmImeKeyShouldPassThrough(KEY_BACKSPACE, 0, true));
}

TEST(QmImeCandidatePage, FirstPageShiftsRimeCandidatesInsteadOfDroppingThem)
{
	// 领域词 3 + Rime 9：首页 3+6，第二页补回被挤出的 Rime 第 7–9 项。
	const auto First = QmImeComputeCandidatePage(3, 9, 0, 9);
	EXPECT_EQ(First.m_PageStart, 0);
	EXPECT_EQ(First.m_ShownDomain, 3);
	EXPECT_EQ(First.m_RimeStart, 0);
	EXPECT_EQ(First.m_ShownRime, 6);
	EXPECT_EQ(First.m_PageCount, 2);

	const auto Second = QmImeComputeCandidatePage(3, 9, 1, 9);
	EXPECT_EQ(Second.m_PageStart, 9);
	EXPECT_EQ(Second.m_ShownDomain, 0);
	EXPECT_EQ(Second.m_RimeStart, 6);
	EXPECT_EQ(Second.m_ShownRime, 3);
}

TEST(QmImeCandidatePage, EveryRimeCandidateAppearsExactlyOnceAcrossPages)
{
	for(const int DomainCount : {0, 1, 3, 5, 9, 12})
	{
		for(const int RimeCount : {0, 1, 8, 9, 10, 20, 27})
		{
			SCOPED_TRACE(::testing::Message() << "domain=" << DomainCount << " rime=" << RimeCount);
			int Seen[27] = {};
			int DomainSeen = 0;
			const int PageCount = QmImeComputeCandidatePage(DomainCount, RimeCount, 0, 9).m_PageCount;
			for(int PageIndex = 0; PageIndex < PageCount; ++PageIndex)
			{
				const auto Page = QmImeComputeCandidatePage(DomainCount, RimeCount, PageIndex, 9);
				if(PageIndex == 0)
					DomainSeen += Page.m_ShownDomain;
				else
					EXPECT_EQ(Page.m_ShownDomain, 0);
				for(int Offset = 0; Offset < Page.m_ShownRime; ++Offset)
				{
					const int Global = Page.m_RimeStart + Offset;
					ASSERT_GE(Global, 0);
					ASSERT_LT(Global, RimeCount);
					++Seen[Global];
				}
			}
			EXPECT_EQ(DomainSeen, std::min(DomainCount, 9));
			for(int Index = 0; Index < RimeCount; ++Index)
				EXPECT_EQ(Seen[Index], 1) << "rime index " << Index;
		}
	}
}

TEST(QmImeCandidatePage, DomainOnlyFirstPageAndPageIndexIsClamped)
{
	const auto First = QmImeComputeCandidatePage(5, 20, 0, 9);
	EXPECT_EQ(First.m_ShownDomain, 5);
	EXPECT_EQ(First.m_ShownRime, 4);
	EXPECT_EQ(First.m_PageCount, 3);
	EXPECT_EQ(QmImeComputeCandidatePage(5, 20, 1, 9).m_RimeStart, 4);
	EXPECT_EQ(QmImeComputeCandidatePage(5, 20, 2, 9).m_RimeStart, 13);
	EXPECT_EQ(QmImeComputeCandidatePage(5, 20, 99, 9).m_PageStart, 18);
	EXPECT_EQ(QmImeComputeCandidatePage(0, 0, 0, 9).m_PageCount, 1);
	EXPECT_EQ(QmImeComputeCandidatePage(0, 0, 0, 9).m_ShownRime, 0);
}
