#include <game/client/components/qmclient/skin7_load_completion.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>

TEST(Skin7LoadCompletion, EmptyNotificationIsAlreadyDrained)
{
	CQmSkin7LoadCompletion Completion;
	EXPECT_FALSE(Completion.HasPending());
	EXPECT_TRUE(Completion.Dispatch(false));
}

TEST(Skin7LoadCompletion, SuccessfulCallbackIsConsumedWithoutReplay)
{
	CQmSkin7LoadCompletion Completion;
	int Calls = 0;
	Completion.Set([&] { ++Calls; });
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 1);
	EXPECT_FALSE(Completion.HasPending());
}

TEST(Skin7LoadCompletion, LoadingDefersCallbackWithoutConsumingIt)
{
	CQmSkin7LoadCompletion Completion;
	int Calls = 0;
	Completion.Set([&] { ++Calls; });
	EXPECT_FALSE(Completion.Dispatch(true));
	EXPECT_EQ(Calls, 0);
	EXPECT_TRUE(Completion.HasPending());
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 1);
}

TEST(Skin7LoadCompletion, CallbackFailureRetainsSameCapturedStateForRetry)
{
	CQmSkin7LoadCompletion Completion;
	int Observed = 0;
	Completion.Set([Attempts = 0, &Observed]() mutable {
		Observed = ++Attempts;
		if(Attempts == 1)
			throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	EXPECT_TRUE(Completion.HasPending());
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Observed, 2);
	EXPECT_FALSE(Completion.HasPending());
}

TEST(Skin7LoadCompletion, RepeatedFailuresDoNotLoseCallback)
{
	CQmSkin7LoadCompletion Completion;
	bool Fail = true;
	int Calls = 0;
	Completion.Set([&] {
		++Calls;
		if(Fail)
			throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	EXPECT_FALSE(Completion.Dispatch(false));
	Fail = false;
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 3);
}

TEST(Skin7LoadCompletion, CancelDropsFailedNotificationBeforeNextAttempt)
{
	CQmSkin7LoadCompletion Completion;
	int Calls = 0;
	Completion.Set([&] {
		++Calls;
		throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	Completion.Cancel();
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 1);
	EXPECT_FALSE(Completion.HasPending());
}

TEST(Skin7LoadCompletion, ReplacementDropsFailedOldNotification)
{
	CQmSkin7LoadCompletion Completion;
	int OldCalls = 0;
	int NewCalls = 0;
	Completion.Set([&] {
		++OldCalls;
		throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	Completion.Set([&] { ++NewCalls; });
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(OldCalls, 1);
	EXPECT_EQ(NewCalls, 1);
}

TEST(Skin7LoadCompletion, ReentrantDispatchDoesNotRunSameCallbackTwice)
{
	CQmSkin7LoadCompletion Completion;
	int Calls = 0;
	Completion.Set([&] {
		++Calls;
		EXPECT_FALSE(Completion.Dispatch(false));
	});
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 1);
}

TEST(Skin7LoadCompletion, ReentrantReplacementIsPreservedAfterOldCallbackSucceeds)
{
	CQmSkin7LoadCompletion Completion;
	int NewCalls = 0;
	Completion.Set([&] {
		Completion.Set([&] { ++NewCalls; });
		EXPECT_FALSE(Completion.Dispatch(false));
	});
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(NewCalls, 0);
	EXPECT_TRUE(Completion.HasPending());
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(NewCalls, 1);
}

TEST(Skin7LoadCompletion, ReentrantReplacementIsNotOverwrittenByOldCallbackFailure)
{
	CQmSkin7LoadCompletion Completion;
	int OldCalls = 0;
	int NewCalls = 0;
	Completion.Set([&] {
		++OldCalls;
		Completion.Set([&] { ++NewCalls; });
		throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(OldCalls, 1);
	EXPECT_EQ(NewCalls, 1);
}

TEST(Skin7LoadCompletion, ReentrantCancelPreventsFailedOldCallbackFromReturning)
{
	CQmSkin7LoadCompletion Completion;
	int Calls = 0;
	Completion.Set([&] {
		++Calls;
		Completion.Cancel();
		throw std::bad_alloc();
	});
	EXPECT_FALSE(Completion.Dispatch(false));
	EXPECT_FALSE(Completion.HasPending());
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_EQ(Calls, 1);
}

TEST(Skin7LoadCompletion, ActiveCaptureSurvivesReentrantCancelUntilCallbackReturns)
{
	CQmSkin7LoadCompletion Completion;
	auto pOwner = std::make_shared<int>(42);
	std::weak_ptr<int> Weak = pOwner;
	Completion.Set([pOwner, &Completion, &Weak] {
		Completion.Cancel();
		EXPECT_FALSE(Weak.expired());
		EXPECT_EQ(*pOwner, 42);
	});
	pOwner.reset();
	EXPECT_TRUE(Completion.Dispatch(false));
	EXPECT_TRUE(Weak.expired());
}

TEST(Skin7LoadCompletion, CancelReleasesPendingCaptureWithoutInvokingIt)
{
	CQmSkin7LoadCompletion Completion;
	auto pOwner = std::make_shared<int>(42);
	std::weak_ptr<int> Weak = pOwner;
	Completion.Set([pOwner] { ADD_FAILURE() << "cancelled callback executed with " << *pOwner; });
	pOwner.reset();
	Completion.Cancel();
	EXPECT_TRUE(Weak.expired());
}
