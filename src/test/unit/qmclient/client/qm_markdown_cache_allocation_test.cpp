#include <game/client/components/qmclient/markdown_cache_writer.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>
#include <stdexcept>
#include <string>

TEST(QmMarkdownCacheAllocation, CandidateAndJobFailuresAllowLaterAdmission)
{
	const std::string Path(128, 'p'), Markdown(1024, 'x');
	for(size_t BeforeFailure : {0u, 1u, 2u})
	{
		SCOPED_TRACE(BeforeFailure);
		CQmMarkdownCacheWriter Writer;
		bool Accepted, Triggered;
		int Submissions = 0;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			Accepted = Writer.TryEnqueue(Path, 1, 1, Markdown, [&](auto) { ++Submissions; return true; });
			Triggered = Fault.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Accepted);
		EXPECT_EQ(Submissions, 0);
		EXPECT_FALSE(Writer.NeedsRetry());
		std::shared_ptr<IJob> pJob;
		EXPECT_TRUE(Writer.TryEnqueue(Path, 1, 2, Markdown, [&](auto pCandidate) { pJob = std::move(pCandidate); return true; }));
		EXPECT_NE(pJob, nullptr);
	}
}

TEST(QmMarkdownCacheAllocation, ResourceExceptionFromAdmissionAllowsRetry)
{
	for(bool LengthError : {false, true})
	{
		CQmMarkdownCacheWriter Writer;
		EXPECT_FALSE(Writer.TryEnqueue("cache.json", 1, 1, "first", [LengthError](auto) -> bool {
			if(LengthError)
				throw std::length_error("admission");
			throw std::bad_alloc();
		}));
		int Submissions = 0;
		std::shared_ptr<IJob> pJob;
		EXPECT_TRUE(Writer.TryEnqueue("cache.json", 1, 2, "second", [&](auto pCandidate) { ++Submissions; pJob = std::move(pCandidate); return true; }));
		EXPECT_EQ(Submissions, 1);
	}
}

TEST(QmMarkdownCacheAllocation, UnknownAdmissionExceptionPropagatesWithoutLatching)
{
	CQmMarkdownCacheWriter Writer;
	EXPECT_THROW(Writer.TryEnqueue("cache.json", 1, 1, "first", [](auto) -> bool { throw std::runtime_error("unexpected"); }), std::runtime_error);
	int Submissions = 0;
	std::shared_ptr<IJob> pJob;
	EXPECT_TRUE(Writer.TryEnqueue("cache.json", 1, 2, "second", [&](auto pCandidate) { ++Submissions; pJob = std::move(pCandidate); return true; }));
	EXPECT_EQ(Submissions, 1);
}
