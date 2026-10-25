#include <engine/shared/http_request_completion.h>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
	class CCompletionRequest : public IHttpRequest
	{
	public:
		CCompletionRequest() :
			IHttpRequest("https://completion.test/body") { LogProgress(HTTPLOG::NONE); }
		void Header(const char *) override {}
		void Complete(EHttpState State = EHttpState::DONE) { OnCompletionInternal(State); }
		void CompleteCached() { PublishCompletion(EHttpState::DONE); }
		bool Progress() { return PublishProgress(); }
	};

	class CConsumer : public IHttpRequest::IProgressCallback
	{
	public:
		int m_Calls = 0;
		bool m_Throw = false;
		bool m_ThrowProgress = false;
		int m_ProgressCalls = 0;
		EHttpState m_Observed = EHttpState::QUEUED;
		std::function<void()> m_Action;
		void OnProgress() override
		{
			++m_ProgressCalls;
			if(m_ThrowProgress)
				throw std::runtime_error("progress consumer failed");
		}
		void OnCompletion(EHttpState State) override
		{
			++m_Calls;
			m_Observed = State;
			if(m_Action)
				m_Action();
			if(m_Throw)
				throw std::runtime_error("completion consumer failed");
		}
	};

	class CSelfClearingConsumer : public IHttpRequest::IProgressCallback
	{
		IHttpRequest &m_Request;
		bool &m_Destroyed;
		bool &m_AliveAfterClear;

	public:
		CSelfClearingConsumer(IHttpRequest &Request, bool &Destroyed, bool &AliveAfterClear) :
			m_Request(Request), m_Destroyed(Destroyed), m_AliveAfterClear(AliveAfterClear) {}
		~CSelfClearingConsumer() override { m_Destroyed = true; }
		void OnProgress() override { Clear(); }
		void OnCompletion(EHttpState) override { Clear(); }
		void Clear()
		{
			auto *pDestroyed = &m_Destroyed;
			auto *pAliveAfterClear = &m_AliveAfterClear;
			auto *pRequest = &m_Request;
			pRequest->SetProgressCallback(nullptr);
			*pAliveAfterClear = !*pDestroyed;
		}
	};
}

TEST(HttpRequestCompletion, FreshRequestHasNoCompletion)
{
	CHttpRequestCompletion Completion;
	EXPECT_EQ(Completion.State(), EHttpState::QUEUED);
}

TEST(HttpRequestCompletion, NonCancellationNotificationsKeepLatestState)
{
	for(const auto First : {EHttpState::DONE, EHttpState::ERROR})
	{
		CHttpRequestCompletion Completion;
		const auto Last = First == EHttpState::DONE ? EHttpState::ERROR : EHttpState::DONE;
		Completion.Publish(First);
		Completion.Publish(Last);
		EXPECT_EQ(Completion.State(), Last);
	}
}

TEST(HttpRequestCompletion, CancellationHasPriorityRegardlessOfPublicationOrder)
{
	for(const auto Other : {EHttpState::DONE, EHttpState::ERROR})
	{
		for(const bool AbortFirst : {false, true})
		{
			CHttpRequestCompletion Completion;
			Completion.Publish(AbortFirst ? EHttpState::ABORTED : Other);
			Completion.Publish(AbortFirst ? Other : EHttpState::ABORTED);
			Completion.Publish(EHttpState::ABORTED);
			EXPECT_EQ(Completion.State(), EHttpState::ABORTED);
		}
	}
}

TEST(HttpRequestCompletion, IndependentRequestsDoNotShareCancellation)
{
	CHttpRequestCompletion First, Second;
	First.Publish(EHttpState::ABORTED);
	Second.Publish(EHttpState::DONE);
	EXPECT_EQ(First.State(), EHttpState::ABORTED);
	EXPECT_EQ(Second.State(), EHttpState::DONE);
}

TEST(HttpRequestCompletion, ConcurrentTerminalNotificationsPreserveCancellation)
{
	CHttpRequestCompletion Completion;
	std::thread Success([&] { Completion.Publish(EHttpState::DONE); });
	std::thread Failure([&] { Completion.Publish(EHttpState::ERROR); });
	std::thread Cancel([&] { Completion.Publish(EHttpState::ABORTED); });
	Success.join();
	Failure.join();
	Cancel.join();
	EXPECT_EQ(Completion.State(), EHttpState::ABORTED);
}

TEST(HttpCompletionConsumer, ExceptionAfterSuccessfulTransferPublishesErrorWithoutRepeatingConsumer)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_Throw = true;
	Request.SetProgressCallback(&Consumer);
	EXPECT_NO_THROW(Request.Complete());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::DONE);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionConsumer, ExceptionWhileReportingTransportErrorRetainsError)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_Throw = true;
	Request.SetProgressCallback(&Consumer);
	EXPECT_NO_THROW(Request.Complete(EHttpState::ERROR));
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionConsumer, ExceptionWhileReportingCancellationKeepsCancelledState)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_Throw = true;
	Request.SetProgressCallback(&Consumer);
	EXPECT_NO_THROW(Request.Complete(EHttpState::ABORTED));
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
}

TEST(HttpCompletionConsumer, SuccessfulConsumerRunsBeforeGlobalCompletionPublication)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	bool DoneInsideConsumer = true;
	Consumer.m_Action = [&] { DoneInsideConsumer = Request.Done(); };
	Request.SetProgressCallback(&Consumer);
	Request.Complete();
	EXPECT_FALSE(DoneInsideConsumer);
	EXPECT_EQ(Request.State(), EHttpState::DONE);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionConsumer, OwnedConsumerSurvivesClearingItsOwnRegistration)
{
	CCompletionRequest Request;
	bool Destroyed = false, AliveAfterClear = false;
	Request.SetProgressCallback(std::make_shared<CSelfClearingConsumer>(Request, Destroyed, AliveAfterClear));
	Request.Complete();
	EXPECT_TRUE(AliveAfterClear);
	EXPECT_TRUE(Destroyed);
	EXPECT_EQ(Request.State(), EHttpState::DONE);
}

TEST(HttpCompletionConsumer, ConsumerCanCompleteAnotherRequestBeforeFirstPublication)
{
	CConsumer Consumer;
	CCompletionRequest First, Second;
	Consumer.m_Action = [&] { Second.Complete(); };
	First.SetProgressCallback(&Consumer);
	First.Complete();
	EXPECT_EQ(First.State(), EHttpState::DONE);
	EXPECT_EQ(Second.State(), EHttpState::DONE);
}

TEST(HttpCompletionConsumer, CachedCompletionUsesSameConsumerFailureBoundary)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_Throw = true;
	Request.SetProgressCallback(&Consumer);
	EXPECT_NO_THROW(Request.CompleteCached());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Calls, 1);
}

TEST(HttpCompletionConsumer, ProgressFailureStopsRepeatedConsumerAndCannotPublishSuccess)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_ThrowProgress = true;
	Request.SetProgressCallback(&Consumer);
	EXPECT_FALSE(Request.Progress());
	EXPECT_FALSE(Request.Progress());
	EXPECT_EQ(Consumer.m_ProgressCalls, 1);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Consumer.m_Observed, EHttpState::ERROR);
}

TEST(HttpCompletionConsumer, OwnedProgressConsumerSurvivesClearingItsOwnRegistration)
{
	CCompletionRequest Request;
	bool Destroyed = false, AliveAfterClear = false;
	Request.SetProgressCallback(std::make_shared<CSelfClearingConsumer>(Request, Destroyed, AliveAfterClear));
	EXPECT_TRUE(Request.Progress());
	EXPECT_TRUE(AliveAfterClear);
	EXPECT_TRUE(Destroyed);
	Request.Complete();
	EXPECT_EQ(Request.State(), EHttpState::DONE);
}

TEST(HttpCompletionConsumer, CancellationIsPreservedAfterProgressConsumerFailure)
{
	CConsumer Consumer;
	CCompletionRequest Request;
	Consumer.m_ThrowProgress = true;
	Request.SetProgressCallback(&Consumer);
	ASSERT_FALSE(Request.Progress());
	Request.Abort();
	Request.Complete(EHttpState::ABORTED);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
}
