#include <test/support/qm_tsf_sink_test.h>

using qm_tsf_test::CQmTsfLifecycleTest;
using qm_tsf_test::CreateCandidates;

TEST_F(CQmTsfLifecycleTest, CandidateUpdatesPopulateCustomUiUntilElementEnds)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_FALSE(m_State.AcceptsSdlCandidates());

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, FailedSystemFallbackAllowsNextSuccessfulCandidateRead)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_ShowResult = E_FAIL;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_EQ(m_pElement->m_ShowCalls, 1);
	ASSERT_FALSE(m_pElement->m_Shown);
	ASSERT_TRUE(m_State.Candidates().m_vCandidates.empty());

	m_pElement->m_FailedStringIndex = -1;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_FALSE(m_pElement->m_Shown);
}

TEST_F(CQmTsfLifecycleTest, FailedSystemFallbackRetriesWhenCandidateReadStillFails)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_ShowResult = E_FAIL;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_FALSE(m_pElement->m_Shown);

	m_pElement->m_ShowResult = S_OK;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_pElement->m_ShowCalls, 2);
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, SuccessfulSystemFallbackSuppressesNativeAndSdlCandidatesUntilEnd)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_TRUE(m_pElement->m_Shown);

	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"旧候选"};
	NativeCandidates.m_PageSize = NativeCandidates.m_TotalCount = 1;
	m_State.UpdateFromNative(NativeCandidates, true);
	const char *apSdlCandidates[] = {"1 旧候选"};
	m_State.UpdateFromSdl(apSdlCandidates, 1, 0, true);
	m_pElement->m_FailedStringIndex = -1;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_TRUE(m_pElement->m_Shown);

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	m_State.UpdateFromNative(NativeCandidates, true);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, NativeCandidates.m_vCandidates);
}

TEST_F(CQmTsfLifecycleTest, BeginInterruptedBySessionRestartLeavesOldElementNative)
{
	m_pManager->m_OnGetUiElement = [this] {
		m_pSink->Disable();
		m_pSink->Enable(m_State);
	};
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	EXPECT_EQ(Show, TRUE);

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_pElement->m_StringCalls, 0);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());

	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
}

TEST_F(CQmTsfLifecycleTest, OldReadCannotOverwriteRestartedElementWithSameIdAndPointer)
{
	BeginCandidates();
	m_pElement->m_vStrings = {L"旧"};
	m_pElement->m_OnGetString = [this] {
		EXPECT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
		BeginCandidates();
		m_pElement->m_vStrings = {L"新"};
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_FALSE(m_pElement->m_Shown);
}

TEST_F(CQmTsfLifecycleTest, NestedUpdateKeepsTheNewerCandidateSnapshot)
{
	BeginCandidates();
	m_pElement->m_vStrings = {L"旧"};
	m_pElement->m_OnGetString = [this] {
		m_pElement->m_vStrings = {L"新"};
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);
}

TEST_F(CQmTsfLifecycleTest, DisableClearsCandidatesAndRejectsLateCallbacks)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_FALSE(m_State.Candidates().m_vCandidates.empty());
	m_pSink->Disable();
	ASSERT_TRUE(m_State.Candidates().m_vCandidates.empty());
	ASSERT_TRUE(m_pElement->m_Shown);
	const int PreviousReadCount = m_pElement->m_StringCalls;

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);

	EXPECT_EQ(Show, TRUE);
	EXPECT_EQ(m_pElement->m_StringCalls, PreviousReadCount);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, RepeatedEnablePreservesCurrentCandidateOwnership)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pSink->Enable(m_State);

	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
	EXPECT_FALSE(m_State.AcceptsSdlCandidates());
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);

	m_pElement->m_vStrings = {L"新"};
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
}

TEST_F(CQmTsfLifecycleTest, DisablingOldSessionPreservesElementReopenedDuringRestore)
{
	BeginCandidates();
	const auto pOldElement = m_pElement;
	pOldElement->m_OnShow = [this] {
		m_pSink->Enable(m_State);
		m_pElement = CreateCandidates();
		m_pElement->m_vStrings = {L"新"};
		m_pManager->m_Elements[ELEMENT_ID] = m_pElement;
		BeginCandidates();
		ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	m_pSink->Disable();

	EXPECT_TRUE(pOldElement->m_Shown);
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	m_pSink->Disable();
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, ShutdownPreventsLateCallbacksFromReactivatingTheSink)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pElement->m_OnShow = [this] {
		m_pSink->Enable(m_State);
	};
	m_pSink->Shutdown();
	const int PreviousReadCount = m_pElement->m_StringCalls;
	m_pSink->Enable(m_State);
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(Show, TRUE);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_EQ(m_pElement->m_StringCalls, PreviousReadCount);
}

TEST_F(CQmTsfLifecycleTest, UnknownUiElementKeepsItsNativeWindow)
{
	m_pElement->m_IsCandidateList = false;
	BOOL Show = FALSE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID, &Show), S_OK);
	EXPECT_EQ(Show, TRUE);

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_EQ(m_pElement->m_StringCalls, 0);
	EXPECT_EQ(m_pElement->m_ShowCalls, 0);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, SystemFallbackIgnoresSynchronousUpdateFromShow)
{
	BeginCandidates();
	m_pElement->m_FailedStringIndex = 0;
	m_pElement->m_OnShow = [this] {
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};

	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);

	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
}

TEST_F(CQmTsfLifecycleTest, ElementReleaseCannotClearNewCandidatesWithTheSameId)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	m_pElement->m_OnFinalRelease = [this] {
		m_pElement = CreateCandidates();
		m_pElement->m_vStrings = {L"新"};
		m_pManager->m_Elements.emplace(ELEMENT_ID, m_pElement);
		BeginCandidates();
		EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	};
	// 只让 sink 持有旧元素，使 End 释放其 COM 引用时触发新一轮通知。
	m_pManager->m_Elements.erase(ELEMENT_ID);
	m_pElement.Reset();

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);

	ASSERT_NE(m_pElement.Get(), nullptr);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"新"}));
	EXPECT_FALSE(m_pElement->m_Shown);
}
