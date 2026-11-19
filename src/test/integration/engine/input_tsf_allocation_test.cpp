#include <engine/client/input_tsf.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_tsf_sink_test.h>

using CQmTsfAllocationTest = qm_tsf_test::CQmTsfLifecycleTest;

TEST_F(CQmTsfAllocationTest, FailedBeginKeepsNativeUiAndAllowsTheSameElementToBeginAgain)
{
	// 逐个拒绝本次 Begin 的分配，涵盖元素节点和候选状态；上限只防止无界重试。
	for(size_t BeforeFailure = 0; BeforeFailure < 32; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		BOOL Show = FALSE;
		HRESULT Result;
		bool Failed;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			Result = m_pSink->BeginUIElement(ELEMENT_ID, &Show);
			Failed = Failure.Triggered();
		}
		ASSERT_EQ(Result, S_OK);
		if(!Failed)
		{
			ASSERT_GT(BeforeFailure, 0u);
			ASSERT_FALSE(Show);
			ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
			EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
			return;
		}
		ASSERT_TRUE(Show);
		ASSERT_TRUE(m_State.AcceptsSdlCandidates());
		ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
		ASSERT_EQ(m_pElement->m_StringCalls, 0);
	}
	FAIL() << "Begin did not recover within the allocation probe limit";
}

TEST_F(CQmTsfAllocationTest, SnapshotAllocationFailureRestoresNativeUiUntilElementEnds)
{
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	ASSERT_FALSE(m_State.Candidates().m_vCandidates.empty());

	HRESULT Result;
	bool Failed;
	{
		CPredictionAllocationFailure Failure;
		Result = m_pSink->UpdateUIElement(ELEMENT_ID);
		Failed = Failure.Triggered();
	}
	ASSERT_TRUE(Failed);
	ASSERT_EQ(Result, S_OK);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_FALSE(m_State.AcceptsSdlCandidates());

	ASSERT_EQ(m_pSink->EndUIElement(ELEMENT_ID), S_OK);
	BeginCandidates();
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID), S_OK);
	EXPECT_FALSE(m_pElement->m_Shown);
	EXPECT_EQ(m_State.Candidates().m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
}

TEST_F(CQmTsfAllocationTest, DisableRestoresAndReleasesAllElementsWithoutAllocating)
{
	BeginCandidates();
	auto pSecond = qm_tsf_test::CreateCandidates();
	m_pManager->m_Elements.emplace(ELEMENT_ID + 1, pSecond);
	BOOL Show = TRUE;
	ASSERT_EQ(m_pSink->BeginUIElement(ELEMENT_ID + 1, &Show), S_OK);
	ASSERT_FALSE(Show);
	pSecond->m_Shown = Show;
	ASSERT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID + 1), S_OK);
	ASSERT_FALSE(m_State.Candidates().m_vCandidates.empty());

	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		m_pSink->Disable();
		m_pSink->Shutdown();
		Attempts = Failure.Attempts();
	}
	EXPECT_EQ(Attempts, 0u);
	EXPECT_TRUE(m_pElement->m_Shown);
	EXPECT_TRUE(pSecond->m_Shown);
	EXPECT_EQ(m_pElement->m_ShowCalls, 1);
	EXPECT_EQ(pSecond->m_ShowCalls, 1);
	EXPECT_TRUE(m_State.Candidates().m_vCandidates.empty());
	const int PreviousReads = pSecond->m_StringCalls;
	EXPECT_EQ(m_pSink->UpdateUIElement(ELEMENT_ID + 1), S_OK);
	EXPECT_EQ(pSecond->m_StringCalls, PreviousReads);
}

TEST_F(CQmTsfAllocationTest, SinkAllocationFailureLeavesTextServicesInactive)
{
	CQmTsfUiLess TextServices;
	bool Activated;
	bool Rejected;
	{
		CPredictionAllocationFailure Failure;
		Activated = TextServices.Activate(m_State);
		Rejected = Failure.Triggered();
	}
	ASSERT_TRUE(Rejected);
	EXPECT_FALSE(Activated);
	EXPECT_FALSE(TextServices.IsActive());
	TextServices.SetTextInputActive(true);
	TextServices.Deactivate();
	EXPECT_TRUE(m_State.AcceptsSdlCandidates());
}
