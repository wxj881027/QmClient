#include <game/client/components/qmclient/qm_hook_coll_tuning.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>

TEST(QmHookCollTuning, PredictedPlayerKeepsOwnTuningEvenWithoutLocalPlayer)
{
	const auto Owner = QmHookCollTuningClient(true, 3, -1);
	ASSERT_TRUE(Owner);
	EXPECT_EQ(*Owner, 3);
}

TEST(QmHookCollTuning, UnpredictedPlayerUsesValidLocalTuning)
{
	const auto Owner = QmHookCollTuningClient(false, 3, 7);
	ASSERT_TRUE(Owner);
	EXPECT_EQ(*Owner, 7);
}

TEST(QmHookCollTuning, MissingLocalPlayerRequestsConnectionFallback)
{
	EXPECT_FALSE(QmHookCollTuningClient(false, 3, -1));
}

TEST(QmHookCollTuning, OutOfRangeSelectedLocalPlayerRequestsConnectionFallback)
{
	for(const int LocalId : {std::numeric_limits<int>::min(), static_cast<int>(MAX_CLIENTS), std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(LocalId);
		EXPECT_FALSE(QmHookCollTuningClient(false, 3, LocalId));
	}
}

TEST(QmHookCollTuning, FirstAndLastPlayerAreBothValidTuningOwners)
{
	for(const int Id : {0, MAX_CLIENTS - 1})
	{
		SCOPED_TRACE(Id);
		const auto Predicted = QmHookCollTuningClient(true, Id, -1);
		const auto Local = QmHookCollTuningClient(false, 3, Id);
		ASSERT_TRUE(Predicted);
		ASSERT_TRUE(Local);
		EXPECT_EQ(*Predicted, Id);
		EXPECT_EQ(*Local, Id);
	}
}

TEST(QmHookCollTuning, InvalidPredictedOwnerDoesNotReadAnotherPlayersTuning)
{
	for(const int RenderedId : {-1, static_cast<int>(MAX_CLIENTS)})
	{
		SCOPED_TRACE(RenderedId);
		EXPECT_FALSE(QmHookCollTuningClient(true, RenderedId, 7));
	}
}
