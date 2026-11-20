#include <engine/http.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/browser_map_preview_loader.h>
#include <game/client/components/qmclient/map_preview_panel.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	// 只替换传输。文件创建、写入、取消清理与终态发布使用真实 IHttpRequest。
	class CPreviewDownloadRequest : public IHttpRequest
	{
	public:
		explicit CPreviewDownloadRequest(const char *pUrl) :
			IHttpRequest(pUrl) { LogProgress(HTTPLOG::NONE); }
		void Header(const char *) override {}
		bool Start()
		{
			if(!BeforeInit())
			{
				OnCompletionInternal(EHttpState::ERROR);
				return false;
			}
			m_State = EHttpState::RUNNING;
			return true;
		}
		void Complete(std::string_view Body = {}, EHttpState State = EHttpState::DONE, int Progress = 100)
		{
			m_StatusCode = 200;
			if(State == EHttpState::DONE && !Body.empty())
				EXPECT_EQ(OnData(Body.data(), Body.size()), Body.size());
			m_Progress = Progress;
			OnCompletionInternal(State);
		}
		bool OwnsFile() const { return m_File != nullptr; }
		bool SkipsByFileTime() const { return m_SkipByFileTime; }
		bool UsesLocalFileTime() const { return m_IfModifiedSince >= 0; }
		const char *TemporaryPath() const { return m_aDestAbsoluteTmp; }
	};

	class CPreviewDownloadHttp : public IHttp
	{
	public:
		enum class ESubmissionFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
		};
		std::vector<std::shared_ptr<CPreviewDownloadRequest>> m_vRequests;
		std::weak_ptr<CPreviewDownloadRequest> m_LastRejected;
		ESubmissionFailure m_SubmissionFailure = ESubmissionFailure::NONE;
		bool m_RejectNext = false;
		bool m_DeferNext = false;
		bool m_OverlappedFileOwners = false;

		~CPreviewDownloadHttp() override
		{
			for(const auto &pRequest : m_vRequests)
				if(!pRequest->Done())
					pRequest->Complete({}, EHttpState::ABORTED);
		}
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			auto pActual = std::static_pointer_cast<CPreviewDownloadRequest>(pRequest);
			const auto Failure = std::exchange(m_SubmissionFailure, ESubmissionFailure::NONE);
			if(Failure != ESubmissionFailure::NONE)
			{
				// 提交失败发生在接管前，成功接管后不再抛资源异常。
				m_LastRejected = pActual;
				if(Failure == ESubmissionFailure::ALLOCATION)
					throw std::bad_alloc();
				throw std::length_error("preview submission capacity");
			}
			for(const auto &pPrevious : m_vRequests)
				if(pPrevious->OwnsFile() && str_comp(pPrevious->TemporaryPath(), pActual->TemporaryPath()) == 0)
					m_OverlappedFileOwners = true;
			m_vRequests.push_back(pActual);
			if(std::exchange(m_RejectNext, false))
				pActual->Complete({}, EHttpState::ERROR);
			else if(!std::exchange(m_DeferNext, false))
				pActual->Start();
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class MapPreviewDownloadState : public ::testing::Test
	{
	protected:
		enum class EFactoryFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CPreviewDownloadHttp m_Http;
		CQmMapPreviewDownloadState m_State;
		EFactoryFailure m_FactoryFailure = EFactoryFailure::NONE;
		int m_Creations = 0;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
		}
		bool Ensure(const char *pName = "A")
		{
			return m_State.Ensure(pName, "https://preview.test", m_Http, m_pStorage.get(), [&](const char *pUrl) -> std::unique_ptr<IHttpRequest> {
				++m_Creations;
				switch(std::exchange(m_FactoryFailure, EFactoryFailure::NONE))
				{
				case EFactoryFailure::ALLOCATION: throw std::bad_alloc();
				case EFactoryFailure::LENGTH: throw std::length_error("preview request preparation limit");
				case EFactoryFailure::NONE: return std::make_unique<CPreviewDownloadRequest>(pUrl);
				}
				throw std::logic_error("unexpected factory failure");
			});
		}
		void PrimeIndex()
		{
			ASSERT_TRUE(Ensure());
			ASSERT_EQ(m_Http.m_vRequests.size(), 1U);
			m_Http.m_vRequests[0]->Complete("<a href=\"A_11111111.map\">a</a><a href=\"B_22222222.map\">b</a>");
		}
		void StartMap()
		{
			PrimeIndex();
			if(HasFatalFailure())
				return;
			ASSERT_TRUE(Ensure());
			ASSERT_EQ(m_Http.m_vRequests.size(), 2U);
			ASSERT_TRUE(m_Http.m_vRequests[1]->OwnsFile());
		}
		void CompleteWithValidMap(const std::shared_ptr<CPreviewDownloadRequest> &pRequest)
		{
			void *pData = nullptr;
			unsigned Size = 0;
			ASSERT_TRUE(m_pStorage->ReadFile("maps/dm1.map", IStorage::TYPE_ALL, &pData, &Size));
			ASSERT_GT(Size, 0U);
			pRequest->Complete({static_cast<const char *>(pData), Size});
			free(pData);
		}
	};
}

TEST_F(MapPreviewDownloadState, ClosingAndReopeningTheSameTargetReusesItsLiveRequest)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	auto pOriginal = m_Http.m_vRequests[1];
	EXPECT_TRUE(Ensure());
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Http.m_vRequests.size(), 2U);
	EXPECT_FALSE(pOriginal->IsAbortRequested());
	EXPECT_TRUE(pOriginal->OwnsFile());
}

TEST_F(MapPreviewDownloadState, SwitchingAwayAndBackWaitsUntilTheOldSameFileOwnerIsTerminal)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	auto pOld = m_Http.m_vRequests[1];
	EXPECT_TRUE(Ensure("B"));
	EXPECT_TRUE(pOld->IsAbortRequested());
	EXPECT_TRUE(pOld->OwnsFile());
	EXPECT_TRUE(Ensure("A"));
	EXPECT_EQ(m_Http.m_vRequests.size(), 2U);
	EXPECT_FALSE(m_State.HasFailed("A"));
	pOld->Complete({}, EHttpState::ABORTED);
	EXPECT_FALSE(pOld->OwnsFile());
	EXPECT_TRUE(Ensure("A"));
	ASSERT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_STREQ(m_Http.m_vRequests[2]->TemporaryPath(), pOld->TemporaryPath());
	EXPECT_TRUE(m_Http.m_vRequests[2]->OwnsFile());
	EXPECT_FALSE(m_Http.m_OverlappedFileOwners);
}

TEST_F(MapPreviewDownloadState, RetryWhileCancellationIsPendingDoesNotOpenASecondWriter)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	auto pOld = m_Http.m_vRequests[1];
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Http.m_vRequests.size(), 2U);
	EXPECT_TRUE(pOld->OwnsFile());
	pOld->Complete({}, EHttpState::ABORTED);
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_FALSE(m_Http.m_OverlappedFileOwners);
}

TEST_F(MapPreviewDownloadState, StopKeepsTheCacheOwnerUntilTheFileRequestAcknowledgesCancellation)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	auto pOld = m_Http.m_vRequests[1];
	EXPECT_FALSE(m_State.Stop());
	EXPECT_FALSE(m_State.Stop());
	EXPECT_TRUE(pOld->OwnsFile());
	pOld->Complete({}, EHttpState::ABORTED);
	EXPECT_TRUE(m_State.Stop());
	EXPECT_FALSE(pOld->OwnsFile());
}

TEST_F(MapPreviewDownloadState, IndexFactoryAllocationFailureBecomesRetryableWithoutSubmittingARequest)
{
	m_FactoryFailure = EFactoryFailure::ALLOCATION;
	EXPECT_FALSE(Ensure());
	EXPECT_TRUE(m_State.HasFailed("A"));
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 1U);
	EXPECT_STREQ(m_Http.m_vRequests[0]->Url(), "https://preview.test/");
}

TEST_F(MapPreviewDownloadState, MapFactoryFailurePreservesThePreparedIndexForRetry)
{
	PrimeIndex();
	ASSERT_FALSE(HasFatalFailure());
	m_FactoryFailure = EFactoryFailure::ALLOCATION;
	EXPECT_FALSE(Ensure());
	EXPECT_TRUE(m_State.HasFailed("A"));
	EXPECT_EQ(m_Http.m_vRequests.size(), 1U);
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 2U);
	EXPECT_STREQ(m_Http.m_vRequests[1]->Url(), "https://preview.test/A_11111111.map");
}

TEST_F(MapPreviewDownloadState, RequestPreparationLengthFailureUsesTheSameRetryState)
{
	PrimeIndex();
	ASSERT_FALSE(HasFatalFailure());
	m_FactoryFailure = EFactoryFailure::LENGTH;
	EXPECT_FALSE(Ensure());
	EXPECT_TRUE(m_State.HasFailed("A"));
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Http.m_vRequests.size(), 2U);
}

TEST_F(MapPreviewDownloadState, HttpSubmissionRejectionIsTerminalAndCanBeRetried)
{
	PrimeIndex();
	ASSERT_FALSE(HasFatalFailure());
	m_Http.m_RejectNext = true;
	EXPECT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 2U);
	EXPECT_EQ(m_Http.m_vRequests[1]->State(), EHttpState::ERROR);
	EXPECT_TRUE(m_State.HasFailed("A"));
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_TRUE(m_Http.m_vRequests.back()->SkipsByFileTime());
}

TEST_F(MapPreviewDownloadState, SubmissionResourceFailureBeforeHandoffCanRetryTheSameOrAnotherMap)
{
	for(const auto Failure : {CPreviewDownloadHttp::ESubmissionFailure::ALLOCATION, CPreviewDownloadHttp::ESubmissionFailure::LENGTH})
	{
		for(const char *pRetryName : {"A", "B"})
		{
			SCOPED_TRACE(pRetryName);
			SCOPED_TRACE(static_cast<int>(Failure));
			CQmMapPreviewDownloadState State;
			const auto Create = [](const char *pUrl) -> std::unique_ptr<IHttpRequest> { return std::make_unique<CPreviewDownloadRequest>(pUrl); };
			const auto EnsureMap = [&](const char *pName) { return State.Ensure(pName, "https://preview.test", m_Http, m_pStorage.get(), Create); };
			ASSERT_TRUE(EnsureMap("A"));
			m_Http.m_vRequests.back()->Complete("<a href=\"A_11111111.map\">a</a><a href=\"B_22222222.map\">b</a>");
			const size_t BeforeSubmission = m_Http.m_vRequests.size();
			m_Http.m_SubmissionFailure = Failure;
			EXPECT_FALSE(EnsureMap("A"));
			EXPECT_TRUE(State.HasFailed("A"));
			EXPECT_EQ(m_Http.m_vRequests.size(), BeforeSubmission);
			EXPECT_TRUE(m_Http.m_LastRejected.expired());
			EXPECT_TRUE(State.Stop());
			State.Retry();
			ASSERT_TRUE(EnsureMap(pRetryName));
			ASSERT_EQ(m_Http.m_vRequests.size(), BeforeSubmission + 1);
			EXPECT_EQ(m_Http.m_vRequests.back()->State(), EHttpState::RUNNING);
			EXPECT_TRUE(m_Http.m_vRequests.back()->OwnsFile());
			m_Http.m_vRequests.back()->Complete({}, EHttpState::ABORTED);
		}
	}
	EXPECT_FALSE(m_Http.m_OverlappedFileOwners);
}

TEST_F(MapPreviewDownloadState, AcceptedQueuedRequestStaysOwnedUntilCancellationIsAcknowledged)
{
	PrimeIndex();
	ASSERT_FALSE(HasFatalFailure());
	m_Http.m_DeferNext = true;
	ASSERT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 2U);
	auto pQueued = m_Http.m_vRequests.back();
	ASSERT_EQ(pQueued->State(), EHttpState::QUEUED);
	EXPECT_TRUE(Ensure("B"));
	EXPECT_TRUE(pQueued->IsAbortRequested());
	EXPECT_FALSE(m_State.Stop());
	m_State.Retry();
	EXPECT_TRUE(Ensure("B"));
	EXPECT_EQ(m_Http.m_vRequests.size(), 2U);
	pQueued->Complete({}, EHttpState::ABORTED);
	ASSERT_TRUE(Ensure("B"));
	EXPECT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_EQ(m_Http.m_vRequests.back()->State(), EHttpState::RUNNING);
}

TEST_F(MapPreviewDownloadState, SuccessfulHttpWithAnUnreadableMapBecomesRetryableAndCanRecover)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_Http.m_vRequests[1]->SkipsByFileTime());
	m_Http.m_vRequests[1]->Complete("not a map");
	const auto FailedRequest = m_State.ValidationRequest("A");
	CQmBrowserMapPreviewLoadJob Failed(m_pStorage.get(), "A", "", false);
	Failed.Run();
	ASSERT_FALSE(Failed.m_Success);
	m_State.LocalLoadResult(FailedRequest, Failed.m_Success);
	EXPECT_TRUE(m_State.HasFailed("A"));
	EXPECT_FALSE(Ensure());
	m_State.Retry();
	EXPECT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_FALSE(m_Http.m_vRequests[2]->SkipsByFileTime());
	EXPECT_FALSE(m_Http.m_vRequests[2]->UsesLocalFileTime());
	CompleteWithValidMap(m_Http.m_vRequests[2]);
	ASSERT_FALSE(HasFatalFailure());
	CQmBrowserMapPreviewLoadJob Recovered(m_pStorage.get(), "A", "", false);
	Recovered.Run();
	ASSERT_TRUE(Recovered.m_Success);
	m_State.LocalLoadResult(m_State.ValidationRequest("A"), Recovered.m_Success);
	EXPECT_FALSE(m_State.HasFailed("A"));
	m_State.Retry();
	ASSERT_TRUE(Ensure());
	EXPECT_TRUE(m_Http.m_vRequests.back()->SkipsByFileTime());
	EXPECT_TRUE(m_Http.m_vRequests.back()->UsesLocalFileTime());
}

TEST_F(MapPreviewDownloadState, ALoadStartedBeforeHttpCompletionCannotMarkTheDownloadedFileAsUnreadable)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	const auto OldLoadRequest = m_State.ValidationRequest("A");
	EXPECT_TRUE(OldLoadRequest.expired());
	m_Http.m_vRequests[1]->Complete("not a map");
	m_State.LocalLoadResult(OldLoadRequest, false);
	EXPECT_FALSE(m_State.HasFailed("A"));
	m_State.LocalLoadResult(m_State.ValidationRequest("A"), false);
	EXPECT_TRUE(m_State.HasFailed("A"));
}

TEST_F(MapPreviewDownloadState, APreviousDownloadLoadResultCannotFailItsReplacementRequest)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	m_Http.m_vRequests[1]->Complete("old");
	const auto Previous = m_State.ValidationRequest("A");
	m_State.Retry();
	ASSERT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 3U);
	m_Http.m_vRequests[2]->Complete("new");
	m_State.LocalLoadResult(Previous, false);
	EXPECT_FALSE(m_State.HasFailed("A"));
	m_State.LocalLoadResult(m_State.ValidationRequest("A"), false);
	EXPECT_TRUE(m_State.HasFailed("A"));
}

TEST_F(MapPreviewDownloadState, CompletedRequestWithUnknownLengthWakesTheLocalRecheckOnce)
{
	StartMap();
	ASSERT_FALSE(HasFatalFailure());
	m_Http.m_vRequests[1]->Complete("body", EHttpState::DONE, 0);
	EXPECT_TRUE(m_State.TakeCompletedDownload("A"));
	EXPECT_FALSE(m_State.TakeCompletedDownload("A"));
	EXPECT_FALSE(m_State.TakeCompletedDownload("B"));
	EXPECT_FALSE(m_State.ValidationRequest("A").expired());
}

TEST_F(MapPreviewDownloadState, CancelledIndexCompletionDoesNotReplaceTheNewIndexRequest)
{
	ASSERT_TRUE(Ensure());
	auto pOld = m_Http.m_vRequests[0];
	m_State.Retry();
	EXPECT_TRUE(pOld->IsAbortRequested());
	ASSERT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 2U);
	pOld->Complete({}, EHttpState::ABORTED);
	m_Http.m_vRequests[1]->Complete("<a href=\"A_11111111.map\">a</a>");
	EXPECT_TRUE(Ensure());
	ASSERT_EQ(m_Http.m_vRequests.size(), 3U);
	EXPECT_STREQ(m_Http.m_vRequests[2]->Url(), "https://preview.test/A_11111111.map");
	EXPECT_FALSE(m_State.HasFailed("A"));
}
