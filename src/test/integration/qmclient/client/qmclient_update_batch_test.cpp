#include <game/client/components/qmclient/update_package.h>
#include <game/client/components/qmclient/update_request.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using CTasks = qm_update::CDownloadTasks;
	using CFileCleanup = qm_update::CPackageFileCleanup;

	// 只替换 HTTP 传输，文件接收、关闭/重命名与清理回调使用生产实现。
	class CAttachmentResponse : public IHttpRequest
	{
	public:
		int m_Aborts = 0;
		bool m_ThrowOnAbort = false;
		explicit CAttachmentResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override
		{
			++m_Aborts;
			IHttpRequest::Abort();
			if(m_ThrowOnAbort)
				throw std::runtime_error("attachment cancellation failed");
		}
		void Reply()
		{
			m_StatusCode = 200;
			EHttpState State = EHttpState::DONE;
			if(!BeforeInit() || OnData("verified", 8) != 8)
				State = EHttpState::ERROR;
			// 模拟取消后传输才完成，回调应在实际文件落盘后再次回收。
			OnCompletionInternal(State);
		}
	};

	class QmUpdateBatch : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_Storage = m_Info.CreateTestStorage();
		CTasks m_Tasks;
		std::vector<CTasks::EFile> m_vPrepared;
		std::vector<std::shared_ptr<CAttachmentResponse>> m_vRequests;
		std::shared_ptr<qm_update::CPackageDownload> m_Package;
		CTasks::EFile m_FailBefore = CTasks::NUM_FILES;
		CTasks::EFile m_FailAfterRequest = CTasks::NUM_FILES;
		CTasks::EFile m_FailAfterStart = CTasks::NUM_FILES;
		CTasks::EFile m_NullAt = CTasks::NUM_FILES;
		CTasks::EFile m_CompleteDuringPreparation = CTasks::NUM_FILES;
		bool m_UnknownFailure = false;
		bool m_UsePackage = false;

		void Prepare(CTasks::EFile File, std::shared_ptr<IHttpRequest> &Request, std::shared_ptr<CFileCleanup> &Cleanup)
		{
			m_vPrepared.push_back(File);
			if(File == m_FailBefore)
				throw std::bad_alloc();
			if(File == m_NullAt)
				return;
			if(m_UsePackage && File == CTasks::PACKAGE)
			{
				m_Package = std::make_shared<qm_update::CPackageDownload>("https://github.com/wxj881027/QmClient/releases/download/v3.4/package.zip", m_Storage.get(), "package.tmp", 8 * 1024 * 1024, [](const std::shared_ptr<IHttpRequest> &) {}, [](std::shared_ptr<IJob>) { return false; }, [this](const std::string &Url) { auto Result = std::make_shared<CAttachmentResponse>(Url.c_str()); m_vRequests.push_back(Result); return Result; });
				Request = m_Package;
				return;
			}
			auto Response = std::make_shared<CAttachmentResponse>("https://update.test/attachment");
			Request = Response;
			m_vRequests.push_back(Response);
			if(File == m_FailAfterRequest)
				throw std::length_error("attachment preparation capacity exhausted");
			const std::string Path = "attachment-" + std::to_string(File);
			Request->WriteToFile(m_Storage.get(), Path.c_str(), IStorage::TYPE_SAVE);
			char aAbsolute[IO_MAX_PATH_LENGTH];
			m_Storage->GetCompletePath(IStorage::TYPE_SAVE, Path.c_str(), aAbsolute, sizeof(aAbsolute));
			Cleanup = std::make_shared<CFileCleanup>(aAbsolute);
			Request->SetProgressCallback(Cleanup);
			if(File == m_CompleteDuringPreparation)
				Response->Reply();
			if(File == m_FailAfterStart)
			{
				if(m_UnknownFailure)
					throw 17;
				throw std::bad_alloc();
			}
		}
		bool Start()
		{
			return m_Tasks.TryStart([&](CTasks::EFile File, std::shared_ptr<IHttpRequest> &Request, std::shared_ptr<CFileCleanup> &Cleanup) { Prepare(File, Request, Cleanup); });
		}
		bool Exists(const std::shared_ptr<CAttachmentResponse> &Request)
		{
			if(Request->Dest()[0] == '\0')
				return false;
			char aAbsolute[IO_MAX_PATH_LENGTH];
			m_Storage->GetCompletePath(IStorage::TYPE_SAVE, Request->Dest(), aAbsolute, sizeof(aAbsolute));
			return fs_is_file(aAbsolute);
		}
		void ExpectCancelled()
		{
			EXPECT_FALSE(m_Tasks.Any());
			EXPECT_FALSE(m_Tasks.AllDone());
			for(const auto &Request : m_vRequests)
				EXPECT_TRUE(Request->IsAbortRequested());
		}
	};
}

TEST_F(QmUpdateBatch, EmptyBatchHasNoCompletionOrBusyState)
{
	EXPECT_FALSE(m_Tasks.Any());
	EXPECT_FALSE(m_Tasks.AllDone());
	EXPECT_TRUE(m_Tasks.CanStart(false, false));
	EXPECT_FALSE(m_Tasks.CanStart(true, false));
}

TEST_F(QmUpdateBatch, SuccessfulPreparationPublishesFourOrderedRequests)
{
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_vPrepared, (std::vector<CTasks::EFile>{CTasks::PACKAGE, CTasks::PACKAGE_SIGNATURE, CTasks::MANIFEST, CTasks::MANIFEST_SIGNATURE}));
	ASSERT_EQ(m_vRequests.size(), 4u);
	EXPECT_EQ(m_Tasks.Get(CTasks::PACKAGE), m_vRequests[0]);
	EXPECT_EQ(m_Tasks.Get(CTasks::MANIFEST_SIGNATURE), m_vRequests[3]);
	EXPECT_TRUE(m_Tasks.Any());
	EXPECT_FALSE(m_Tasks.AllDone());
	for(size_t Index = 0; Index < m_vRequests.size() - 1; ++Index)
		m_vRequests[Index]->Reply();
	EXPECT_FALSE(m_Tasks.AllDone());
	m_vRequests.back()->Reply();
	EXPECT_TRUE(m_Tasks.AllDone());
}

TEST_F(QmUpdateBatch, RepeatedPreparationLeavesPublishedRequestsUntouched)
{
	ASSERT_TRUE(Start());
	const auto Original = m_Tasks.Get(CTasks::PACKAGE);
	int Calls = 0;
	EXPECT_FALSE(m_Tasks.TryStart([&](CTasks::EFile, std::shared_ptr<IHttpRequest> &, std::shared_ptr<CFileCleanup> &) { ++Calls; }));
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(m_Tasks.Get(CTasks::PACKAGE), Original);
	EXPECT_FALSE(Original->IsAbortRequested());
}

TEST_F(QmUpdateBatch, FailureBeforeFirstRequestLeavesBatchRetryable)
{
	m_FailBefore = CTasks::PACKAGE;
	EXPECT_FALSE(Start());
	EXPECT_FALSE(m_Tasks.Any());
	EXPECT_TRUE(m_vRequests.empty());
	EXPECT_TRUE(m_Tasks.CanStart(false, false));
}

TEST_F(QmUpdateBatch, FailureBeforeSignatureCancelsThePublishedPackageRequest)
{
	m_FailBefore = CTasks::PACKAGE_SIGNATURE;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 1u);
	ExpectCancelled();
	EXPECT_EQ(m_vPrepared.size(), 2u);
}

TEST_F(QmUpdateBatch, FailureStartingFinalAttachmentCancelsEveryPublishedRequest)
{
	m_FailAfterStart = CTasks::MANIFEST_SIGNATURE;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 4u);
	ExpectCancelled();
}

TEST_F(QmUpdateBatch, NullManifestStopsPreparationAndCancelsEarlierRequests)
{
	m_NullAt = CTasks::MANIFEST;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 2u);
	EXPECT_EQ(m_vPrepared.size(), 3u);
	ExpectCancelled();
}

TEST_F(QmUpdateBatch, FailureBeforeCleanupOwnershipStillCancelsTheRequest)
{
	m_FailAfterRequest = CTasks::PACKAGE_SIGNATURE;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 2u);
	ExpectCancelled();
	EXPECT_STREQ(m_vRequests.back()->Dest(), "");
}

TEST_F(QmUpdateBatch, UnknownStartFailureUsesTheSameBatchRollback)
{
	m_FailAfterStart = CTasks::MANIFEST;
	m_UnknownFailure = true;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 3u);
	ExpectCancelled();
}

TEST_F(QmUpdateBatch, LaterPreparationFailureDiscardsAlreadyCompletedAttachment)
{
	m_CompleteDuringPreparation = CTasks::PACKAGE_SIGNATURE;
	m_FailBefore = CTasks::MANIFEST;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_vRequests.size(), 2u);
	EXPECT_EQ(m_vRequests[1]->State(), EHttpState::DONE);
	EXPECT_FALSE(Exists(m_vRequests[1]));
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_FALSE(m_Tasks.Any());
}

TEST_F(QmUpdateBatch, ResetRemovesAttachmentsThatCompleteAfterCancellation)
{
	ASSERT_TRUE(Start());
	m_Tasks.Reset();
	m_Tasks.Reset();
	for(const auto &Request : m_vRequests)
	{
		EXPECT_EQ(Request->m_Aborts, 1);
		Request->Reply();
		EXPECT_FALSE(Exists(Request));
	}
	EXPECT_FALSE(m_Tasks.Any());
}

TEST_F(QmUpdateBatch, SuccessfulReleasePreservesFilesForTheInstaller)
{
	ASSERT_TRUE(Start());
	for(const auto &Request : m_vRequests)
		Request->Reply();
	ASSERT_TRUE(m_Tasks.AllDone());
	m_Tasks.Reset();
	EXPECT_FALSE(m_Tasks.Any());
	for(const auto &Request : m_vRequests)
	{
		EXPECT_EQ(Request->m_Aborts, 0);
		EXPECT_TRUE(Exists(Request));
	}
}

TEST_F(QmUpdateBatch, ExplicitDiscardRemovesCompletedOwnedAttachments)
{
	ASSERT_TRUE(Start());
	for(const auto &Request : m_vRequests)
		Request->Reply();
	m_Tasks.Reset(true);
	for(const auto &Request : m_vRequests)
		EXPECT_FALSE(Exists(Request));
	EXPECT_FALSE(m_Tasks.Any());
}

TEST_F(QmUpdateBatch, OneCancellationExceptionDoesNotSkipTheOtherAttachments)
{
	ASSERT_TRUE(Start());
	m_vRequests[1]->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(m_Tasks.Reset());
	ExpectCancelled();
	for(const auto &Request : m_vRequests)
	{
		Request->Reply();
		EXPECT_FALSE(Exists(Request));
	}
}

TEST_F(QmUpdateBatch, ANewBatchCanStartAfterPartialPreparationFailure)
{
	m_FailBefore = CTasks::MANIFEST;
	EXPECT_FALSE(Start());
	const auto Previous = m_vRequests;
	m_vRequests.clear();
	m_FailBefore = CTasks::NUM_FILES;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_vRequests.size(), 4u);
	EXPECT_TRUE(m_Tasks.Any());
	for(const auto &Request : Previous)
		EXPECT_TRUE(Request->IsAbortRequested());
	for(const auto &Request : m_vRequests)
		EXPECT_FALSE(Request->IsAbortRequested());
}

TEST_F(QmUpdateBatch, CleanupOutlivesTheDestroyedBatch)
{
	auto Tasks = std::make_unique<CTasks>();
	ASSERT_TRUE(Tasks->TryStart([&](CTasks::EFile File, std::shared_ptr<IHttpRequest> &Request, std::shared_ptr<CFileCleanup> &Cleanup) { Prepare(File, Request, Cleanup); }));
	Tasks.reset();
	for(const auto &Request : m_vRequests)
	{
		EXPECT_TRUE(Request->IsAbortRequested());
		Request->Reply();
		EXPECT_FALSE(Exists(Request));
	}
}

TEST_F(QmUpdateBatch, RealPackageIsCancelledWhenSignaturePreparationFails)
{
	m_UsePackage = true;
	m_FailBefore = CTasks::PACKAGE_SIGNATURE;
	EXPECT_FALSE(Start());
	ASSERT_NE(m_Package, nullptr);
	EXPECT_EQ(m_Package->State(), EHttpState::ABORTED);
	ASSERT_EQ(m_vRequests.size(), 1u);
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
	EXPECT_FALSE(m_Tasks.Any());
	EXPECT_NO_THROW(m_Package->Poll(100));
}

TEST_F(QmUpdateBatch, ReleasePreservesCompletedFileAndCancelsOnlyPendingFiles)
{
	ASSERT_TRUE(Start());
	m_vRequests[0]->Reply();
	m_Tasks.Reset();
	EXPECT_TRUE(Exists(m_vRequests[0]));
	EXPECT_EQ(m_vRequests[0]->m_Aborts, 0);
	for(size_t Index = 1; Index < m_vRequests.size(); ++Index)
	{
		EXPECT_TRUE(m_vRequests[Index]->IsAbortRequested());
		m_vRequests[Index]->Reply();
		EXPECT_FALSE(Exists(m_vRequests[Index]));
	}
}
