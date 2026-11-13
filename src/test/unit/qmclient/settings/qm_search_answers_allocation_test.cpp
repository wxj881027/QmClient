#include <game/client/components/qmclient/search_answers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstdlib>
#include <exception>
#include <limits>
#include <new>
#include <string>

namespace
{
	constexpr const char *NEW_QUERY = "同一语音房间走远后声音变小怎么办？";

	struct SSearchAttempt
	{
		bool m_Success;
		bool m_Triggered;
		size_t m_Attempts;
	};

	SSearchAttempt SearchWithFailure(size_t BeforeFailure, const char *pQuery, qm_search_answers::SResult &Out)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		const bool Success = qm_search_answers::TrySearch(pQuery, Out);
		return {Success, Failure.Triggered(), Failure.Attempts()};
	}
}

TEST(QmSearchAnswersAllocation, EveryQueryPreparationFailureKeepsTheCompleteOldResultAndCanRetry)
{
	const auto Previous = qm_search_answers::Search("截图");
	const auto Expected = qm_search_answers::Search(NEW_QUERY);
	ASSERT_NE(Previous.m_pAnswer, nullptr);
	ASSERT_NE(Expected.m_pAnswer, nullptr);
	EXPECT_STREQ(Expected.m_pAnswer->m_pId, "voice-distance");
	qm_search_answers::SResult Trace;
	const auto Traced = SearchWithFailure(std::numeric_limits<size_t>::max(), NEW_QUERY, Trace);
	ASSERT_TRUE(Traced.m_Success);
	ASSERT_GT(Traced.m_Attempts, 0u);
	ASSERT_LE(Traced.m_Attempts, 4096u);
	size_t Rejected = 0;
	for(size_t Index = 0; Index < Traced.m_Attempts; ++Index)
	{
		SCOPED_TRACE(Index);
		auto Out = Previous;
		const auto *pCandidates = Out.m_vCandidates.data();
		const auto Capacity = Out.m_vCandidates.capacity();
		const auto Attempt = SearchWithFailure(Index, NEW_QUERY, Out);
		ASSERT_TRUE(Attempt.m_Triggered);
		if(!Attempt.m_Success)
		{
			++Rejected;
			EXPECT_EQ(Out.m_pAnswer, Previous.m_pAnswer);
			EXPECT_EQ(Out.m_vCandidates, Previous.m_vCandidates);
			EXPECT_EQ(Out.m_vCandidates.data(), pCandidates);
			EXPECT_EQ(Out.m_vCandidates.capacity(), Capacity);
		}
		else
		{
			// stable_sort 可以在临时缓冲拒收后原地完成；成功仍须发布完整结果。
			EXPECT_EQ(Out.m_pAnswer, Expected.m_pAnswer);
			EXPECT_EQ(Out.m_vCandidates, Expected.m_vCandidates);
		}
		ASSERT_TRUE(qm_search_answers::TrySearch(NEW_QUERY, Out));
		EXPECT_EQ(Out.m_pAnswer, Expected.m_pAnswer);
		EXPECT_EQ(Out.m_vCandidates, Expected.m_vCandidates);
	}
	EXPECT_GT(Rejected, 0u);
}

TEST(QmSearchAnswersAllocation, FailedReplacementPreservesAmbiguousCandidatesAndTheirStorage)
{
	auto Out = qm_search_answers::Search("没声音");
	ASSERT_EQ(Out.m_pAnswer, nullptr);
	ASSERT_EQ(Out.m_vCandidates.size(), 3u);
	const auto Previous = Out.m_vCandidates;
	const auto *pCandidates = Out.m_vCandidates.data();
	const auto Capacity = Out.m_vCandidates.capacity();
	const auto Attempt = SearchWithFailure(0, NEW_QUERY, Out);
	ASSERT_TRUE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Success);
	EXPECT_EQ(Out.m_pAnswer, nullptr);
	EXPECT_EQ(Out.m_vCandidates, Previous);
	EXPECT_EQ(Out.m_vCandidates.data(), pCandidates);
	EXPECT_EQ(Out.m_vCandidates.capacity(), Capacity);
	ASSERT_TRUE(qm_search_answers::TrySearch(NEW_QUERY, Out));
	ASSERT_NE(Out.m_pAnswer, nullptr);
	EXPECT_STREQ(Out.m_pAnswer->m_pId, "voice-distance");
}

TEST(QmSearchAnswersAllocation, EmptyQueriesPublishEmptyResultsWhenPreparationSucceeds)
{
	const char *apQueries[] = {nullptr, "", "　？！"};
	for(const char *pQuery : apQueries)
	{
		auto Out = qm_search_answers::Search("截图");
		ASSERT_NE(Out.m_pAnswer, nullptr);
		const auto Attempt = SearchWithFailure(std::numeric_limits<size_t>::max(), pQuery, Out);
		EXPECT_TRUE(Attempt.m_Success);
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_EQ(Out.m_pAnswer, nullptr);
		EXPECT_TRUE(Out.m_vCandidates.empty());
	}
}

TEST(QmSearchAnswersAllocation, InvalidOrOversizedQueryCommitsEmptyInsteadOfReportingResourceFailure)
{
	const std::string Oversized(513, '?');
	for(const char *pQuery : {"截图\xff", Oversized.c_str()})
	{
		auto Out = qm_search_answers::Search("截图");
		const auto Attempt = SearchWithFailure(std::numeric_limits<size_t>::max(), pQuery, Out);
		EXPECT_TRUE(Attempt.m_Success);
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_EQ(Out.m_pAnswer, nullptr);
		EXPECT_TRUE(Out.m_vCandidates.empty());
	}
}

TEST(QmSearchAnswersAllocation, ByValueSearchStillPropagatesResourceFailureAndCanRetry)
{
	(void)qm_search_answers::Search(NEW_QUERY);
	bool Triggered = false;
	bool Caught = false;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			(void)qm_search_answers::Search(NEW_QUERY);
		}
		catch(const std::bad_alloc &)
		{
			Caught = true;
		}
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_TRUE(Caught);
	const auto Retried = qm_search_answers::Search(NEW_QUERY);
	ASSERT_NE(Retried.m_pAnswer, nullptr);
	EXPECT_STREQ(Retried.m_pAnswer->m_pId, "voice-distance");
}

#if GTEST_HAS_DEATH_TEST
namespace
{
	class QmSearchAnswersAllocationDeathTest : public ::testing::Test
	{
		std::string m_PreviousStyle;

	protected:
		void SetUp() override
		{
			m_PreviousStyle = GTEST_FLAG_GET(death_test_style);
			GTEST_FLAG_SET(death_test_style, "threadsafe");
		}
		void TearDown() override { GTEST_FLAG_SET(death_test_style, m_PreviousStyle); }
	};
}

TEST_F(QmSearchAnswersAllocationDeathTest, ColdAnswerTableCanRetryAfterItsFirstAllocationFails)
{
	EXPECT_EXIT(
		{
			std::set_terminate([] { std::_Exit(6); });
			bool Triggered = false;
			bool Caught = false;
			{
				CPredictionAllocationFailure Failure;
				try
				{
					(void)qm_search_answers::Answers();
				}
				catch(const std::bad_alloc &)
				{
					Caught = true;
				}
				Triggered = Failure.Triggered();
			}
			if(!Triggered || !Caught)
				std::_Exit(2);
			try
			{
				const auto &Answers = qm_search_answers::Answers();
				const auto Result = qm_search_answers::Search("截图");
				if(Answers.empty() || Result.m_pAnswer == nullptr || std::string(Result.m_pAnswer->m_pId) != "screenshots")
					std::_Exit(3);
			}
			catch(...)
			{
				std::_Exit(4);
			}
			std::_Exit(0);
		},
		::testing::ExitedWithCode(0), "");
}

TEST_F(QmSearchAnswersAllocationDeathTest, FirstSearchWithPrewarmedAnswersKeepsOutputAndCanRetry)
{
	EXPECT_EXIT(
		{
			std::set_terminate([] { std::_Exit(6); });
			const auto &Answers = qm_search_answers::Answers();
			if(Answers.empty())
				std::_Exit(2);
			qm_search_answers::SResult Out;
			Out.m_pAnswer = &Answers.front();
			Out.m_vCandidates.push_back(Out.m_pAnswer);
			const auto *pOld = Out.m_pAnswer;
			const auto *pStorage = Out.m_vCandidates.data();
			const auto Attempt = SearchWithFailure(0, "截图", Out);
			if(!Attempt.m_Triggered || Attempt.m_Success || Out.m_pAnswer != pOld ||
				Out.m_vCandidates.size() != 1 || Out.m_vCandidates.front() != pOld || Out.m_vCandidates.data() != pStorage)
				std::_Exit(3);
			try
			{
				if(!qm_search_answers::TrySearch("截图", Out) || Out.m_pAnswer == nullptr || std::string(Out.m_pAnswer->m_pId) != "screenshots")
					std::_Exit(4);
			}
			catch(...)
			{
				std::_Exit(5);
			}
			std::_Exit(0);
		},
		::testing::ExitedWithCode(0), "");
}
#endif
