#include <game/client/components/qmclient/nameplate_update.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmNameplateUpdate, LayoutBecomesReadyOnlyAfterCompletePreparation)
{
	CQmNameplateUpdate Update;
	EXPECT_FALSE(Update.Ready());
	bool ReadyDuringPreparation = true;
	EXPECT_TRUE(Update.Run([&] {
		ReadyDuringPreparation = Update.Ready();
		return true;
	}));
	EXPECT_FALSE(ReadyDuringPreparation);
	EXPECT_TRUE(Update.Ready());
}

TEST(QmNameplateUpdate, ExplicitlyIncompletePreparationCanRetry)
{
	CQmNameplateUpdate Update;
	EXPECT_FALSE(Update.Run([] { return false; }));
	EXPECT_FALSE(Update.Ready());
	EXPECT_TRUE(Update.Run([] { return true; }));
	EXPECT_TRUE(Update.Ready());
}

TEST(QmNameplateUpdate, AllocationFailureReturnsNotReady)
{
	CQmNameplateUpdate Update;
	EXPECT_FALSE(Update.Run([]() -> bool { throw std::bad_alloc(); }));
	EXPECT_FALSE(Update.Ready());
	EXPECT_TRUE(Update.Run([] { return true; }));
}

TEST(QmNameplateUpdate, ContainerLengthFailureReturnsNotReady)
{
	CQmNameplateUpdate Update;
	EXPECT_FALSE(Update.Run([]() -> bool { throw std::length_error("nameplate"); }));
	EXPECT_FALSE(Update.Ready());
	EXPECT_TRUE(Update.Run([] { return true; }));
}

TEST(QmNameplateUpdate, FailedRefreshDoesNotExposeThePreviouslyReadyLayout)
{
	CQmNameplateUpdate Update;
	ASSERT_TRUE(Update.Run([] { return true; }));
	EXPECT_FALSE(Update.Run([]() -> bool { throw std::bad_alloc(); }));
	EXPECT_FALSE(Update.Ready());
}

TEST(QmNameplateUpdate, UnexpectedExceptionPropagatesWithLayoutDisabled)
{
	CQmNameplateUpdate Update;
	ASSERT_TRUE(Update.Run([] { return true; }));
	EXPECT_THROW(Update.Run([]() -> bool { throw 7; }), int);
	EXPECT_FALSE(Update.Ready());
}

TEST(QmNameplateUpdate, ResetRequiresAnotherCompleteUpdate)
{
	CQmNameplateUpdate Update;
	ASSERT_TRUE(Update.Run([] { return true; }));
	Update.Reset();
	Update.Reset();
	EXPECT_FALSE(Update.Ready());
	EXPECT_TRUE(Update.Run([] { return true; }));
}

TEST(QmNameplateUpdate, AdmissionAndResetDoNotAllocate)
{
	bool Completed = false, Ready = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		CQmNameplateUpdate Update;
		Completed = Update.Run([] { return true; });
		Update.Reset();
		Ready = Update.Ready();
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Completed);
	EXPECT_FALSE(Ready);
	EXPECT_FALSE(Triggered);
}
