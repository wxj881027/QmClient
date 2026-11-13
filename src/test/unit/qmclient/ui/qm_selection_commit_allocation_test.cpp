#include <game/client/QmUi/QmPopupSource.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	class QmSelectionCommitAllocation : public ::testing::Test
	{
	protected:
		int m_Context = 0;
		std::vector<std::weak_ptr<SQmUiSelectionCommit>> m_vPending;
		std::shared_ptr<SQmUiSelectionCommit> m_pCommit;

		bool Register(const void *pContext, const char *pCardId)
		{
			return QmTryRegisterSelectionCommit(pContext, pCardId, 12, m_vPending, m_pCommit);
		}
	};
}

TEST_F(QmSelectionCommitAllocation, SuccessPublishesTheSameTrackedHandleWithItsSource)
{
	ASSERT_TRUE(Register(&m_Context, "mode-card"));
	ASSERT_TRUE(m_pCommit);
	ASSERT_EQ(m_vPending.size(), 1u);
	EXPECT_EQ(m_vPending[0].lock(), m_pCommit);
	EXPECT_TRUE(m_pCommit->m_Source.Matches(&m_Context, "mode-card"));
	EXPECT_FALSE(m_pCommit->m_Source.Expired(12, false));
	EXPECT_TRUE(m_pCommit->m_Source.Expired(13, false));
	EXPECT_FALSE(m_pCommit->m_Cancelled);
	// 来源关闭通过已登记的弱引用取消同一份选择。
	m_vPending[0].lock()->m_Cancelled = true;
	EXPECT_TRUE(m_pCommit->m_Cancelled);
}

TEST_F(QmSelectionCommitAllocation, HandleAllocationFailureKeepsOutputEmptyAndAllowsRetry)
{
	bool Accepted = true;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure(0);
		Accepted = Register(&m_Context, "mode-card");
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Accepted);
	EXPECT_FALSE(m_pCommit);
	EXPECT_TRUE(m_vPending.empty());
	ASSERT_TRUE(Register(&m_Context, "mode-card"));
	ASSERT_EQ(m_vPending.size(), 1u);
	EXPECT_EQ(m_vPending[0].lock(), m_pCommit);
}

TEST_F(QmSelectionCommitAllocation, TrackingAllocationFailureDoesNotPublishAnUntrackedHandle)
{
	const auto pExisting = std::make_shared<SQmUiSelectionCommit>();
	m_vPending.emplace_back(pExisting);
	while(m_vPending.size() < m_vPending.capacity())
		m_vPending.emplace_back(pExisting);
	const auto PreviousCount = m_vPending.size();
	bool Accepted = true;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure(1);
		Accepted = Register(&m_Context, "mode-card");
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Accepted);
	EXPECT_FALSE(m_pCommit);
	ASSERT_EQ(m_vPending.size(), PreviousCount);
	for(const auto &Pending : m_vPending)
		EXPECT_EQ(Pending.lock(), pExisting);
	EXPECT_FALSE(pExisting->m_Cancelled);
	ASSERT_TRUE(Register(&m_Context, "mode-card"));
	ASSERT_EQ(m_vPending.size(), PreviousCount + 1);
	EXPECT_EQ(m_vPending.back().lock(), m_pCommit);
}

TEST_F(QmSelectionCommitAllocation, ExistingCommitIsNeitherReallocatedNorRegisteredTwice)
{
	ASSERT_TRUE(Register(&m_Context, "original-card"));
	const auto pOriginal = m_pCommit;
	bool Accepted = false;
	bool Triggered = true;
	{
		CPredictionAllocationFailure Failure(0);
		Accepted = Register(&m_Context, "replacement-card");
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Accepted);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(m_pCommit, pOriginal);
	EXPECT_EQ(m_vPending.size(), 1u);
	EXPECT_TRUE(m_pCommit->m_Source.Matches(&m_Context, "original-card"));
}

TEST_F(QmSelectionCommitAllocation, InputsWithoutACardSourceKeepTheUntrackedSelectionPath)
{
	for(int Kind : {0, 1})
	{
		SCOPED_TRACE(Kind);
		bool Accepted = false;
		bool Triggered = true;
		{
			CPredictionAllocationFailure Failure(0);
			Accepted = Register(Kind == 0 ? nullptr : &m_Context, Kind == 0 ? "mode-card" : nullptr);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Accepted);
		EXPECT_FALSE(Triggered);
		EXPECT_FALSE(m_pCommit);
		EXPECT_TRUE(m_vPending.empty());
	}
}
