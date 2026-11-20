#include <base/hash.h>

#include <engine/http.h>

#include <gtest/gtest.h>
#include <test/support/http_response_allocation_probe.h>

#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	class CBodyRequest : public IHttpRequest
	{
	public:
		CHttpResponseAllocationProbe m_Allocation;
		CBodyRequest() :
			IHttpRequest("https://response.test/body") { LogProgress(HTTPLOG::NONE); }
		void Header(const char *) override {}
		size_t Feed(std::string_view Body) { return OnData(Body.data(), Body.size()); }
		size_t FeedSize(const char *pBody, size_t Size) { return OnData(pBody, Size); }
		void Complete(EHttpState State = EHttpState::DONE) { OnCompletionInternal(State); }
		bool Ready() { return BeforeInit(); }
		bool Skip() { return ShouldSkipRequest(); }
		void ArrangeFileMode()
		{
			m_WriteToMemory = true;
			m_WriteToFile = true;
		}
		// 只安排不可实际分配的长度边界，不复写接收或增长算法。
		void ArrangeReceivedLength(uint64_t Length) { m_ResponseLength = Length; }

	private:
		void *ReallocateResponseBuffer(size_t Size) noexcept override { return m_Allocation.Reallocate(m_pBuffer, Size); }
	};

	std::string Result(IHttpRequest &Request)
	{
		unsigned char *pBytes = nullptr;
		size_t Size = 0;
		Request.Result(&pBytes, &Size);
		return Size ? std::string(reinterpret_cast<const char *>(pBytes), Size) : std::string();
	}
}

TEST(HttpResponseBuffer, InitialAllocationFailureRejectsBodyAndSuccessfulTransportCannotPublishDone)
{
	CBodyRequest Request;
	Request.m_Allocation.m_Fail = true;
	EXPECT_EQ(Request.Feed("body"), 0u);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 1);
}

TEST(HttpResponseBuffer, FailedGrowthPreservesPreviouslyReceivedSample)
{
	CBodyRequest Request;
	Request.ResponseSample(4096);
	const std::string Prefix(800, 'p');
	ASSERT_EQ(Request.Feed(Prefix), Prefix.size());
	Request.m_Allocation.m_Fail = true;
	EXPECT_EQ(Request.Feed(std::string(500, 'x')), 0u);
	Request.Complete();
	ASSERT_EQ(Request.State(), EHttpState::ERROR);
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Request.ResultResponseSample(&pBytes, &Size);
	ASSERT_EQ(Size, Prefix.size());
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), Prefix);
}

TEST(HttpResponseBuffer, RepeatedDataAfterAllocationFailureDoesNotRetryOrAllocate)
{
	CBodyRequest Request;
	Request.m_Allocation.m_Fail = true;
	ASSERT_EQ(Request.Feed("body"), 0u);
	Request.m_Allocation.m_Fail = false;
	EXPECT_EQ(Request.Feed("replacement"), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 1);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, IndependentRequestCanSucceedAfterAnotherAllocationFails)
{
	CBodyRequest Failed;
	Failed.m_Allocation.m_Fail = true;
	ASSERT_EQ(Failed.Feed("first"), 0u);
	Failed.Complete();
	CBodyRequest Healthy;
	ASSERT_EQ(Healthy.Feed("next"), 4u);
	Healthy.Complete();
	ASSERT_EQ(Healthy.State(), EHttpState::DONE);
	EXPECT_EQ(Result(Healthy), "next");
	EXPECT_EQ(Failed.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, MultipleGrowthsPreserveCompleteBytesAndDigest)
{
	CBodyRequest Request;
	const std::string First(900, 'a'), Second(1500, 'b'), Third(2000, 'c');
	const auto Expected = First + Second + Third;
	Request.ExpectSha256(sha256(Expected.data(), Expected.size()));
	EXPECT_EQ(Request.Feed(First), First.size());
	EXPECT_EQ(Request.Feed(Second), Second.size());
	EXPECT_EQ(Request.Feed(Third), Third.size());
	Request.Complete();
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	EXPECT_EQ(Result(Request), Expected);
	EXPECT_EQ(Request.ResultSha256(), sha256(Expected.data(), Expected.size()));
}

TEST(HttpResponseBuffer, EmptyBodyNeedsNoAllocationAndKeepsEmptyDigest)
{
	CBodyRequest Request;
	EXPECT_EQ(Request.Feed({}), 0u);
	Request.Complete();
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	EXPECT_TRUE(Result(Request).empty());
	EXPECT_EQ(Request.m_Allocation.m_Requests, 0);
	EXPECT_EQ(Request.ResultSha256(), sha256("", 0));
}

TEST(HttpResponseBuffer, ResponseLimitRejectsCumulativeDataBeforeAllocation)
{
	CBodyRequest Request;
	Request.MaxResponseSize(4);
	ASSERT_EQ(Request.Feed("123"), 3u);
	EXPECT_EQ(Request.Feed("45"), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 1);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, ResponseLengthAdditionOverflowRejectsBeforeReadingInput)
{
	CBodyRequest Request;
	Request.ArrangeReceivedLength(std::numeric_limits<uint64_t>::max() - 1);
	EXPECT_EQ(Request.Feed("xx"), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 0);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, MemoryLengthBeyondNativeAddressSizeRejectsWithoutAllocation)
{
	CBodyRequest Request;
	Request.ArrangeReceivedLength(std::numeric_limits<size_t>::max());
	EXPECT_EQ(Request.Feed("x"), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 0);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, CapacityDoublingNearNativeLimitDoesNotWrapOrLoop)
{
	CBodyRequest Request;
	Request.m_Allocation.m_Fail = true;
	// 分配器必定拒绝，生产代码不得在拒绝前读取这段不可实际提供的正文。
	EXPECT_EQ(Request.FeedSize("x", std::numeric_limits<size_t>::max()), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 1);
	EXPECT_EQ(Request.m_Allocation.m_LastSize, std::numeric_limits<size_t>::max());
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBuffer, CompleteSampleStopsInputWithoutMarkingReceiveFailure)
{
	CBodyRequest Request;
	Request.ResponseSample(4);
	EXPECT_EQ(Request.Feed("prefix and ignored tail"), 0u);
	EXPECT_EQ(Request.Feed("later tail"), 0u);
	Request.Complete();
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Request.ResultResponseSample(&pBytes, &Size);
	ASSERT_EQ(Size, 4u);
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), "pref");
}

TEST(HttpResponseBuffer, SampleAllocationFailureCannotMasqueradeAsCompletePrefix)
{
	CBodyRequest Request;
	Request.ResponseSample(4);
	Request.m_Allocation.m_Fail = true;
	EXPECT_EQ(Request.Feed("prefix"), 0u);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	unsigned char *pBytes = nullptr;
	size_t Size = 7;
	Request.ResultResponseSample(&pBytes, &Size);
	EXPECT_EQ(Size, 0u);
	EXPECT_EQ(pBytes, nullptr);
}

TEST(HttpResponseBuffer, CancellationStateIsPreservedAfterReceiveFailure)
{
	CBodyRequest Request;
	Request.m_Allocation.m_Fail = true;
	ASSERT_EQ(Request.Feed("body"), 0u);
	Request.Abort();
	Request.Complete(EHttpState::ABORTED);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
}

TEST(HttpResponseBufferBudget, ZeroCapacityRejectsAllocationButAllowsEmptyResponse)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(0);
	CBodyRequest Empty;
	ASSERT_TRUE(Empty.SetResponseBufferBudget(pBudget));
	Empty.Complete();
	EXPECT_EQ(Empty.State(), EHttpState::DONE);
	CBodyRequest Body;
	ASSERT_TRUE(Body.SetResponseBufferBudget(pBudget));
	EXPECT_EQ(Body.Feed("x"), 0u);
	EXPECT_EQ(Body.m_Allocation.m_Requests, 0);
	EXPECT_EQ(pBudget->Used(), 0u);
}

TEST(HttpResponseBufferBudget, NativeSizeLimitRejectsOverflowAndCanBeReusedAfterRelease)
{
	CHttpResponseBufferBudget Budget(std::numeric_limits<size_t>::max());
	ASSERT_TRUE(Budget.TryAcquire(std::numeric_limits<size_t>::max()));
	EXPECT_FALSE(Budget.TryAcquire(1));
	EXPECT_EQ(Budget.Used(), std::numeric_limits<size_t>::max());
	Budget.Release(std::numeric_limits<size_t>::max());
	EXPECT_EQ(Budget.Used(), 0u);
	EXPECT_TRUE(Budget.TryAcquire(1));
	Budget.Release(1);
}

TEST(HttpResponseBufferBudget, SharedCapacityRejectsSecondRequestBeforeAllocator)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	CBodyRequest First, Second;
	ASSERT_TRUE(First.SetResponseBufferBudget(pBudget));
	ASSERT_TRUE(Second.SetResponseBufferBudget(pBudget));
	ASSERT_EQ(First.Feed("a"), 1u);
	EXPECT_EQ(pBudget->Used(), 1024u);
	EXPECT_EQ(Second.Feed("b"), 0u);
	EXPECT_EQ(Second.m_Allocation.m_Requests, 0);
	Second.Complete();
	EXPECT_EQ(Second.State(), EHttpState::ERROR);
	EXPECT_EQ(pBudget->Used(), 1024u);
}

TEST(HttpResponseBufferBudget, GrowthChargesOnlyAdditionalCapacity)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(2048);
	CBodyRequest Request;
	ASSERT_TRUE(Request.SetResponseBufferBudget(pBudget));
	ASSERT_EQ(Request.Feed(std::string(900, 'a')), 900u);
	EXPECT_EQ(pBudget->Used(), 1024u);
	ASSERT_EQ(Request.Feed(std::string(500, 'b')), 500u);
	EXPECT_EQ(pBudget->Used(), 2048u);
	Request.Complete();
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	EXPECT_EQ(Result(Request), std::string(900, 'a') + std::string(500, 'b'));
}

TEST(HttpResponseBufferBudget, FailedInitialAllocationReturnsReservationToOtherRequest)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	CBodyRequest Failed;
	ASSERT_TRUE(Failed.SetResponseBufferBudget(pBudget));
	Failed.m_Allocation.m_Fail = true;
	EXPECT_EQ(Failed.Feed("body"), 0u);
	EXPECT_EQ(pBudget->Used(), 0u);
	CBodyRequest Healthy;
	ASSERT_TRUE(Healthy.SetResponseBufferBudget(pBudget));
	EXPECT_EQ(Healthy.Feed("next"), 4u);
}

TEST(HttpResponseBufferBudget, FailedGrowthReturnsAdditionalReservationAndKeepsOldCharge)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(2048);
	auto pRequest = std::make_unique<CBodyRequest>();
	ASSERT_TRUE(pRequest->SetResponseBufferBudget(pBudget));
	ASSERT_EQ(pRequest->Feed(std::string(900, 'a')), 900u);
	pRequest->m_Allocation.m_Fail = true;
	EXPECT_EQ(pRequest->Feed(std::string(500, 'b')), 0u);
	EXPECT_EQ(pBudget->Used(), 1024u);
	pRequest.reset();
	EXPECT_EQ(pBudget->Used(), 0u);
}

TEST(HttpResponseBufferBudget, BudgetRejectionKeepsReceivedSampleUntilRequestDestruction)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	CBodyRequest Request;
	Request.ResponseSample(4096);
	ASSERT_TRUE(Request.SetResponseBufferBudget(pBudget));
	const std::string Prefix(900, 'p');
	ASSERT_EQ(Request.Feed(Prefix), Prefix.size());
	EXPECT_EQ(Request.Feed(std::string(500, 'x')), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 1);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Request.ResultResponseSample(&pBytes, &Size);
	ASSERT_EQ(Size, Prefix.size());
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), Prefix);
	EXPECT_EQ(pBudget->Used(), 1024u);
}

TEST(HttpResponseBufferBudget, CompletedResponseReturnsCapacityOnlyAfterLastOwnerReleasesIt)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	auto pRequest = std::make_shared<CBodyRequest>();
	ASSERT_TRUE(pRequest->SetResponseBufferBudget(pBudget));
	ASSERT_EQ(pRequest->Feed("body"), 4u);
	pRequest->Complete();
	auto pOtherOwner = pRequest;
	pRequest.reset();
	EXPECT_EQ(pBudget->Used(), 1024u);
	EXPECT_EQ(Result(*pOtherOwner), "body");
	pOtherOwner.reset();
	EXPECT_EQ(pBudget->Used(), 0u);
}

TEST(HttpResponseBufferBudget, AbortDoesNotRefundStillOwnedBuffer)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	auto pRequest = std::make_unique<CBodyRequest>();
	ASSERT_TRUE(pRequest->SetResponseBufferBudget(pBudget));
	ASSERT_EQ(pRequest->Feed("body"), 4u);
	pRequest->Abort();
	pRequest->Complete(EHttpState::ABORTED);
	EXPECT_EQ(pBudget->Used(), 1024u);
	pRequest.reset();
	EXPECT_EQ(pBudget->Used(), 0u);
}

TEST(HttpResponseBufferBudget, ExistingBufferCannotBeReassignedToAnotherBudget)
{
	auto pOriginal = std::make_shared<CHttpResponseBufferBudget>(1024);
	auto pOther = std::make_shared<CHttpResponseBufferBudget>(1024);
	auto pRequest = std::make_unique<CBodyRequest>();
	ASSERT_TRUE(pRequest->SetResponseBufferBudget(pOriginal));
	ASSERT_EQ(pRequest->Feed("body"), 4u);
	EXPECT_FALSE(pRequest->SetResponseBufferBudget(pOther));
	EXPECT_FALSE(pRequest->SetResponseBufferBudget(nullptr));
	EXPECT_EQ(pOriginal->Used(), 1024u);
	EXPECT_EQ(pOther->Used(), 0u);
	pRequest.reset();
	EXPECT_EQ(pOriginal->Used(), 0u);
}

TEST(HttpResponseBufferBudget, FinishedEmptyRequestCannotChangeBudget)
{
	CBodyRequest Request;
	Request.Complete();
	EXPECT_FALSE(Request.SetResponseBufferBudget(std::make_shared<CHttpResponseBufferBudget>(1024)));
}

TEST(HttpResponseBufferBudget, UnconfiguredRequestIgnoresAnotherRequestsExhaustedBudget)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	CBodyRequest Limited, Original;
	ASSERT_TRUE(Limited.SetResponseBufferBudget(pBudget));
	ASSERT_EQ(Limited.Feed("a"), 1u);
	ASSERT_EQ(Original.Feed("unlimited"), 9u);
	Original.Complete();
	EXPECT_EQ(Original.State(), EHttpState::DONE);
	EXPECT_EQ(Result(Original), "unlimited");
	EXPECT_EQ(pBudget->Used(), 1024u);
}

TEST(HttpResponseBufferBudget, FileModeCannotAcquireOrBypassMemoryOnlyBudget)
{
	auto pBudget = std::make_shared<CHttpResponseBufferBudget>(1024);
	CBodyRequest File;
	File.ArrangeFileMode();
	EXPECT_FALSE(File.SetResponseBufferBudget(pBudget));
	CBodyRequest Changed;
	ASSERT_TRUE(Changed.SetResponseBufferBudget(pBudget));
	Changed.ArrangeFileMode();
	EXPECT_FALSE(Changed.Skip());
	EXPECT_FALSE(Changed.Ready());
	EXPECT_EQ(pBudget->Used(), 0u);
}
