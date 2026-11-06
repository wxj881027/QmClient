#include <game/client/components/qmclient/friend_code_publication.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	using EStage = qm_friend_code::CImportPublication::EStage;

	class QmFriendCodePublication : public ::testing::Test
	{
	protected:
		qm_friend_code::CImportPublication m_Publication;
		bool m_HasPreview = true;
		int m_Replace = 1;
		int m_Imports = 0;
		int m_RosterRevision = 3;
		int m_BrowserCalls = 0;
		int m_FriendlistCalls = 0;
		int m_SaveCalls = 0;
		bool m_SaveAccepted = true;
		bool m_PreviewWasConsumed = true;
		EStage m_AllocateAt = EStage::IDLE;
		EStage m_LengthFailureAt = EStage::IDLE;
		std::array<int, 16> m_aEvents{};
		size_t m_EventCount = 0;
		std::vector<int> m_vScratch;

		bool Commit(bool Accepted = true)
		{
			return m_Publication.TryCommit(m_HasPreview, m_Replace, [&] {
				++m_Imports;
				if(Accepted)
					++m_RosterRevision;
				return Accepted;
			});
		}
		void Visit(EStage Stage, int Event, int &Calls)
		{
			++Calls;
			m_aEvents[m_EventCount++] = Event;
			m_PreviewWasConsumed = m_PreviewWasConsumed && !m_HasPreview && m_Replace == 0;
			if(m_AllocateAt == Stage)
			{
				m_vScratch.resize(1);
				m_vScratch.front() = 17;
			}
			if(m_LengthFailureAt == Stage)
				throw std::length_error("friend publication metadata exceeds capacity");
		}
		bool Publish(uint64_t Frame, bool Interactive = true)
		{
			return m_Publication.TryPublish(Frame, Interactive, [this] { Visit(EStage::BROWSER_UPDATE, 1, m_BrowserCalls); }, [this] { Visit(EStage::FRIENDLIST_UPDATE, 2, m_FriendlistCalls); }, [this] {
					Visit(EStage::SAVE, 3, m_SaveCalls);
					return m_SaveAccepted; });
		}
		void ExpectCommitted()
		{
			EXPECT_FALSE(m_HasPreview);
			EXPECT_EQ(m_Replace, 0);
			EXPECT_EQ(m_Imports, 1);
			EXPECT_EQ(m_RosterRevision, 4);
		}
	};
}

TEST(QmFriendCodePublicationState, NativeCreationFailureKeepsEmptyStateAndCanRetry)
{
	std::shared_ptr<qm_friend_code::CImportPublication> pState;
	bool Created = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Created = qm_friend_code::TryCreateUiState(pState);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Created);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1U);
	EXPECT_EQ(pState, nullptr);
	ASSERT_TRUE(qm_friend_code::TryCreateUiState(pState));
	ASSERT_NE(pState, nullptr);
	EXPECT_EQ(pState->Stage(), EStage::IDLE);
}

TEST(QmFriendCodePublicationState, ExistingStateIsReusedWithoutAllocatingOrResettingPublication)
{
	std::shared_ptr<qm_friend_code::CImportPublication> pState;
	ASSERT_TRUE(qm_friend_code::TryCreateUiState(pState));
	bool HasPreview = true;
	int Replace = 1;
	ASSERT_TRUE(pState->TryCommit(HasPreview, Replace, [] { return true; }));
	auto *pBefore = pState.get();
	bool Created = false, Triggered = true;
	size_t Attempts = 1;
	{
		CPredictionAllocationFailure Failure;
		Created = qm_friend_code::TryCreateUiState(pState);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Created);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0U);
	EXPECT_EQ(pState.get(), pBefore);
	EXPECT_EQ(pState->Stage(), EStage::BROWSER_UPDATE);
}

TEST_F(QmFriendCodePublication, RejectedImportKeepsPreviewAndDoesNotStartPublication)
{
	EXPECT_FALSE(Commit(false));
	EXPECT_EQ(m_Imports, 1);
	EXPECT_EQ(m_RosterRevision, 3);
	EXPECT_TRUE(m_HasPreview);
	EXPECT_EQ(m_Replace, 1);
	EXPECT_EQ(m_Publication.Stage(), EStage::IDLE);
	EXPECT_FALSE(Publish(1));
	EXPECT_EQ(m_EventCount, 0U);
	ASSERT_TRUE(Commit());
	ASSERT_TRUE(Publish(1));
	EXPECT_EQ(m_Imports, 2);
	EXPECT_EQ(m_RosterRevision, 4);
	EXPECT_FALSE(m_HasPreview);
	EXPECT_EQ(m_Replace, 0);
}

TEST_F(QmFriendCodePublication, MissingPreviewNeverInvokesImport)
{
	m_HasPreview = false;
	EXPECT_FALSE(Commit());
	EXPECT_EQ(m_Imports, 0);
	EXPECT_EQ(m_RosterRevision, 3);
	EXPECT_EQ(m_Publication.Stage(), EStage::IDLE);
}

TEST_F(QmFriendCodePublication, CommitConsumesPreviewBeforeAnyFalliblePublication)
{
	ASSERT_TRUE(Commit());
	ExpectCommitted();
	EXPECT_EQ(m_EventCount, 0U);
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
	m_AllocateAt = EStage::BROWSER_UPDATE;
	bool Published = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Published = Publish(1);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Published);
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(m_PreviewWasConsumed);
	ExpectCommitted();
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
}

TEST_F(QmFriendCodePublication, PendingPublicationPreventsAnotherImportOfNewPreview)
{
	ASSERT_TRUE(Commit());
	m_HasPreview = true;
	m_Replace = 1;
	EXPECT_FALSE(Commit());
	EXPECT_EQ(m_Imports, 1);
	EXPECT_EQ(m_RosterRevision, 4);
	EXPECT_TRUE(m_HasPreview);
	EXPECT_EQ(m_Replace, 1);
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
}

TEST_F(QmFriendCodePublication, SuccessPublishesAllStagesInOrderOnce)
{
	ASSERT_TRUE(Commit());
	ASSERT_TRUE(Publish(1));
	EXPECT_EQ(m_EventCount, 3U);
	EXPECT_EQ(m_aEvents[0], 1);
	EXPECT_EQ(m_aEvents[1], 2);
	EXPECT_EQ(m_aEvents[2], 3);
	EXPECT_EQ(m_Publication.Stage(), EStage::COMPLETE);
	EXPECT_FALSE(m_Publication.Pending());
	EXPECT_TRUE(m_Publication.Saved());
	EXPECT_TRUE(Publish(1));
	EXPECT_TRUE(Publish(2));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, BrowserAllocationFailureRetriesOnlyUnfinishedStages)
{
	ASSERT_TRUE(Commit());
	m_AllocateAt = EStage::BROWSER_UPDATE;
	bool Published = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Published = Publish(10);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Published);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 0);
	EXPECT_EQ(m_SaveCalls, 0);
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 2);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, FriendlistAllocationFailureDoesNotRepeatBrowserUpdate)
{
	ASSERT_TRUE(Commit());
	m_AllocateAt = EStage::FRIENDLIST_UPDATE;
	bool Published = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Published = Publish(10);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Published);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(m_Publication.Stage(), EStage::FRIENDLIST_UPDATE);
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 0);
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 2);
	EXPECT_EQ(m_SaveCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, SaveAllocationFailureRetriesOnlySaving)
{
	ASSERT_TRUE(Commit());
	m_AllocateAt = EStage::SAVE;
	bool Published = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Published = Publish(10);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Published);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(m_Publication.Stage(), EStage::SAVE);
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 1);
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 2);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, LengthFailureRetainsTheUnfinishedStageForRetry)
{
	ASSERT_TRUE(Commit());
	m_LengthFailureAt = EStage::FRIENDLIST_UPDATE;
	EXPECT_FALSE(Publish(10));
	EXPECT_EQ(m_Publication.Stage(), EStage::FRIENDLIST_UPDATE);
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_SaveCalls, 0);
	m_LengthFailureAt = EStage::IDLE;
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 2);
	EXPECT_EQ(m_SaveCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, SaveFalseKeepsTheCommittedRosterAndRetriesOnlySaving)
{
	ASSERT_TRUE(Commit());
	m_SaveAccepted = false;
	EXPECT_FALSE(Publish(10));
	EXPECT_EQ(m_Publication.Stage(), EStage::SAVE);
	ExpectCommitted();
	m_SaveAccepted = true;
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 2);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, RepeatedInteractiveDrawsDoNotRetryWithinTheSameFrame)
{
	ASSERT_TRUE(Commit());
	m_SaveAccepted = false;
	EXPECT_FALSE(Publish(10));
	m_SaveAccepted = true;
	EXPECT_FALSE(Publish(10));
	EXPECT_FALSE(Publish(10));
	EXPECT_EQ(m_SaveCalls, 1);
	EXPECT_EQ(m_Publication.Stage(), EStage::SAVE);
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_SaveCalls, 2);
	EXPECT_EQ(m_Imports, 1);
}

TEST_F(QmFriendCodePublication, MeasurementDoesNotAttemptOrConsumeTheInteractiveFrame)
{
	ASSERT_TRUE(Commit());
	EXPECT_FALSE(Publish(10, false));
	EXPECT_FALSE(Publish(10, false));
	EXPECT_EQ(m_EventCount, 0U);
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
	ASSERT_TRUE(Publish(10));
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	EXPECT_EQ(m_SaveCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, HiddenFramesKeepPendingSavingUntilTheNextInteractiveDraw)
{
	ASSERT_TRUE(Commit());
	m_SaveAccepted = false;
	EXPECT_FALSE(Publish(10));
	m_SaveAccepted = true;
	EXPECT_FALSE(Publish(100, false));
	EXPECT_EQ(m_SaveCalls, 1);
	ASSERT_TRUE(Publish(100));
	EXPECT_EQ(m_SaveCalls, 2);
	EXPECT_EQ(m_BrowserCalls, 1);
	EXPECT_EQ(m_FriendlistCalls, 1);
	ExpectCommitted();
}

TEST_F(QmFriendCodePublication, FrameZeroAllowsOneAttemptBeforeDeferringARepeat)
{
	ASSERT_TRUE(Commit());
	m_SaveAccepted = false;
	EXPECT_FALSE(Publish(0));
	m_SaveAccepted = true;
	EXPECT_FALSE(Publish(0));
	EXPECT_EQ(m_SaveCalls, 1);
	ASSERT_TRUE(Publish(1));
	EXPECT_EQ(m_SaveCalls, 2);
}

TEST_F(QmFriendCodePublication, CompletedPublicationDoesNotReimportItsConsumedPreview)
{
	ASSERT_TRUE(Commit());
	ASSERT_TRUE(Publish(10));
	EXPECT_FALSE(Commit());
	ExpectCommitted();
	EXPECT_EQ(m_Publication.Stage(), EStage::COMPLETE);
}

TEST_F(QmFriendCodePublication, FreshPreviewAfterCompletionStartsAnotherPublication)
{
	ASSERT_TRUE(Commit());
	ASSERT_TRUE(Publish(10));
	m_HasPreview = true;
	m_Replace = 1;
	ASSERT_TRUE(Commit());
	EXPECT_EQ(m_Imports, 2);
	EXPECT_EQ(m_RosterRevision, 5);
	EXPECT_EQ(m_Publication.Stage(), EStage::BROWSER_UPDATE);
	ASSERT_TRUE(Publish(11));
	EXPECT_EQ(m_BrowserCalls, 2);
	EXPECT_EQ(m_FriendlistCalls, 2);
	EXPECT_EQ(m_SaveCalls, 2);
}
