#include <game/client/components/qmclient/skin7_load_completion.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	class CQmSkin7CompletionLifecycleTest : public ::testing::Test
	{
	protected:
		CQmSkin7ResourceLifecycle m_Lifecycle;
		CQmSkin7LoadCompletion m_Completion;
		bool m_Loading = true;
		std::chrono::nanoseconds m_LastRefresh{0};
		int m_Callbacks = 0;
		int m_Diagnostics = 0;

		void SetUp() override
		{
			ASSERT_TRUE(m_Lifecycle.BeginRefresh(IClient::STATE_ONLINE, true));
			m_Completion.Set([&] { ++m_Callbacks; });
		}

		bool Complete()
		{
			return m_Completion.Complete(m_Loading, m_Lifecycle, m_LastRefresh, std::chrono::nanoseconds(100), [&] { ++m_Diagnostics; });
		}
	};
}

TEST_F(CQmSkin7CompletionLifecycleTest, CompletionCommitsReadyAndTimeBeforeDispatch)
{
	ASSERT_TRUE(Complete());
	EXPECT_FALSE(m_Loading);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(100));
	EXPECT_EQ(m_Lifecycle.Update(IClient::STATE_OFFLINE, false), CQmSkin7ResourceLifecycle::EAction::NONE);
	EXPECT_EQ(m_Callbacks, 0);
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7CompletionLifecycleTest, DiagnosticFailureDoesNotLoseReadyStateTimeOrCallback)
{
	EXPECT_TRUE(m_Completion.Complete(m_Loading, m_Lifecycle, m_LastRefresh, std::chrono::nanoseconds(100), [] { throw std::bad_alloc(); }));
	EXPECT_FALSE(m_Loading);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(100));
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 1);
	EXPECT_EQ(m_Lifecycle.Update(IClient::STATE_OFFLINE, false), CQmSkin7ResourceLifecycle::EAction::NONE);
}

TEST_F(CQmSkin7CompletionLifecycleTest, RepeatedCompletionDoesNotRepeatDiagnosticOrChangeTimestamp)
{
	ASSERT_TRUE(Complete());
	EXPECT_FALSE(m_Completion.Complete(m_Loading, m_Lifecycle, m_LastRefresh, std::chrono::nanoseconds(200), [&] { ++m_Diagnostics; }));
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(100));
	EXPECT_EQ(m_Diagnostics, 1);
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7CompletionLifecycleTest, FailedNotificationCanRetryWithoutRecommittingResources)
{
	int Attempts = 0;
	m_Completion.Set([&] {
		if(++Attempts == 1)
			throw std::bad_alloc();
		++m_Callbacks;
	});
	ASSERT_TRUE(Complete());
	EXPECT_FALSE(m_Completion.Dispatch(m_Loading));
	EXPECT_FALSE(m_Loading);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(100));
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 1);
	EXPECT_EQ(m_Diagnostics, 1);
}

TEST_F(CQmSkin7CompletionLifecycleTest, CallbackStartingNewLoadKeepsNewStateAndDefersNewNotification)
{
	int NewCallbacks = 0;
	m_Completion.Set([&] {
		++m_Callbacks;
		EXPECT_TRUE(m_Lifecycle.BeginRefresh(IClient::STATE_ONLINE, true));
		m_Loading = true;
		m_Completion.Set([&] { ++NewCallbacks; });
		EXPECT_FALSE(m_Completion.Dispatch(m_Loading));
	});
	ASSERT_TRUE(Complete());
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_TRUE(m_Loading);
	EXPECT_EQ(m_Callbacks, 1);
	EXPECT_EQ(NewCallbacks, 0);
	ASSERT_TRUE(m_Completion.Complete(m_Loading, m_Lifecycle, m_LastRefresh, std::chrono::nanoseconds(200), [] {}));
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(NewCallbacks, 1);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(200));
}

TEST_F(CQmSkin7CompletionLifecycleTest, DiagnosticReentryCannotBeOverwrittenByOldCompletion)
{
	EXPECT_TRUE(m_Completion.Complete(m_Loading, m_Lifecycle, m_LastRefresh, std::chrono::nanoseconds(100), [&] {
		EXPECT_TRUE(m_Lifecycle.BeginRefresh(IClient::STATE_ONLINE, true));
		m_Loading = true;
		throw std::bad_alloc();
	}));
	EXPECT_TRUE(m_Loading);
	EXPECT_FALSE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 0);
	EXPECT_EQ(m_Lifecycle.Update(IClient::STATE_OFFLINE, false), CQmSkin7ResourceLifecycle::EAction::CANCEL);
}

TEST_F(CQmSkin7CompletionLifecycleTest, CancelledResourcesDoNotAcceptLateCompletionOrNotify)
{
	m_Lifecycle.Reset();
	m_Loading = false;
	m_Completion.Cancel();
	EXPECT_FALSE(Complete());
	EXPECT_EQ(m_Diagnostics, 0);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(0));
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7CompletionLifecycleTest, LeavingReadyCacheCancelsFailedNotificationWithoutDroppingResources)
{
	int Attempts = 0;
	m_Completion.Set([&] {
		++Attempts;
		throw std::bad_alloc();
	});
	ASSERT_TRUE(Complete());
	EXPECT_FALSE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(m_Lifecycle.Update(IClient::STATE_OFFLINE, false), CQmSkin7ResourceLifecycle::EAction::NONE);
	m_Completion.Cancel();
	EXPECT_FALSE(m_Completion.HasPending());
	EXPECT_EQ(m_Lifecycle.Update(IClient::STATE_ONLINE, true), CQmSkin7ResourceLifecycle::EAction::NONE);
	EXPECT_TRUE(m_Completion.Dispatch(m_Loading));
	EXPECT_EQ(Attempts, 1);
	EXPECT_EQ(m_LastRefresh, std::chrono::nanoseconds(100));
}
