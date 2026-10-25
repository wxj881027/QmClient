#include <base/fs.h>

#include <engine/http_response_validation.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace
{
	class CFileResultRequest : public IHttpRequest
	{
	public:
		explicit CFileResultRequest(IStorage *pStorage) :
			IHttpRequest("https://file-result.test/body")
		{
			WriteToFileAndMemory(pStorage, "body.bin", IStorage::TYPE_SAVE);
			LogProgress(HTTPLOG::NONE);
		}
		void Header(const char *) override {}
		bool Start()
		{
			if(!BeforeInit())
				return false;
			m_State = EHttpState::RUNNING;
			return true;
		}
		size_t Feed(std::string_view Body) { return OnData(Body.data(), Body.size()); }
		void Complete(EHttpState State = EHttpState::DONE, int Status = 200)
		{
			m_StatusCode = Status;
			OnCompletionInternal(State);
		}
		const char *TemporaryPath() const { return m_aDestAbsoluteTmp; }
		bool m_CloseFails = false;
		bool m_RenameThrows = false;
		bool m_RenameFails = false;
		bool m_RemoveThrows = false;
		bool m_ReadThrows = false;
		bool m_ReadThrowsAfterAllocation = false;
		bool m_ReadFails = false;
		bool m_AllowDestinationRemoval = true;
		int m_CloseCalls = 0;
		int m_RenameCalls = 0;
		int m_ReadCalls = 0;

	protected:
		// 只替换外部文件边界，完成、校验和清理状态仍走真实基类。
		int CloseResponseFile(IOHANDLE File) noexcept override
		{
			++m_CloseCalls;
			const int Result = IHttpRequest::CloseResponseFile(File);
			return m_CloseFails ? 1 : Result;
		}
		int RenameResponseFile(bool AllowDestinationRemoval) override
		{
			++m_RenameCalls;
			m_AllowDestinationRemoval = AllowDestinationRemoval;
			if(m_RenameThrows)
				throw std::bad_alloc();
			if(m_RenameFails)
				return 1;
			return IHttpRequest::RenameResponseFile(AllowDestinationRemoval);
		}
		int RemoveResponseFile() override
		{
			if(m_RemoveThrows)
				throw std::bad_alloc();
			return IHttpRequest::RemoveResponseFile();
		}
		bool ReadResponseFile(void **ppResult, unsigned *pLength) override
		{
			++m_ReadCalls;
			if(m_ReadThrows)
				throw std::bad_alloc();
			if(m_ReadFails)
				return false;
			const bool Result = IHttpRequest::ReadResponseFile(ppResult, pLength);
			if(m_ReadThrowsAfterAllocation)
				throw std::bad_alloc();
			return Result;
		}
	};

	class CFileCompletion : public IHttpRequest::IProgressCallback
	{
	public:
		int m_Calls = 0;
		EHttpState m_State = EHttpState::QUEUED;
		void OnProgress() override {}
		void OnCompletion(EHttpState State) override
		{
			++m_Calls;
			m_State = State;
		}
	};

	class HttpFileResult : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			IOHANDLE File = m_pStorage->OpenFile("body.bin", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "old", 3), 3u);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Cached()
		{
			void *pData = nullptr;
			unsigned Length = 0;
			if(!m_pStorage->ReadFile("body.bin", IStorage::TYPE_SAVE, &pData, &Length))
			{
				ADD_FAILURE() << "cannot read cache";
				return {};
			}
			std::unique_ptr<void, decltype(&free)> Buffer(pData, free);
			return std::string(static_cast<const char *>(pData), Length);
		}
		void Receive(CFileResultRequest &Request)
		{
			ASSERT_TRUE(Request.Start());
			ASSERT_EQ(Request.Feed("new"), 3u);
		}
		void ExpectTerminal(CFileResultRequest &Request, const CFileCompletion &Consumer, EHttpState State)
		{
			EXPECT_TRUE(Request.Done());
			EXPECT_EQ(Request.State(), State);
			EXPECT_EQ(Consumer.m_State, State);
			EXPECT_EQ(Consumer.m_Calls, 1);
			EXPECT_EQ(Request.m_CloseCalls, 1);
		}
	};
}

TEST_F(HttpFileResult, SuccessfulCompletionPublishesFileMemoryAndDigest)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.ExpectSha256(sha256("new", 3));
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	ExpectTerminal(Request, Consumer, EHttpState::DONE);
	EXPECT_EQ(Cached(), "new");
	unsigned char *pData;
	size_t Length;
	Request.Result(&pData, &Length);
	EXPECT_EQ(std::string(reinterpret_cast<char *>(pData), Length), "new");
	EXPECT_EQ(Request.ResultSha256(), sha256("new", 3));
	EXPECT_FALSE(Request.m_AllowDestinationRemoval);
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath()));
}

TEST_F(HttpFileResult, RenameExceptionStillPublishesFailureOnce)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RenameThrows = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	EXPECT_NO_THROW(Request.Complete());
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_EQ(Cached(), "old");
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath()));
}

TEST_F(HttpFileResult, FailedPublicationDisallowsDestinationRemoval)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RenameFails = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_FALSE(Request.m_AllowDestinationRemoval);
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CleanupExceptionDoesNotPreventErrorPublication)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RemoveThrows = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	EXPECT_NO_THROW(Request.Complete(EHttpState::ERROR));
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CleanupExceptionPreservesCancellation)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RemoveThrows = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	EXPECT_NO_THROW(Request.Complete(EHttpState::ABORTED));
	ExpectTerminal(Request, Consumer, EHttpState::ABORTED);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CloseFailureConsumesHandleAndSkipsPublication)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_CloseFails = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath()));
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CloseFailurePreservesCancellation)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_CloseFails = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete(EHttpState::ABORTED);
	ExpectTerminal(Request, Consumer, EHttpState::ABORTED);
	EXPECT_EQ(Request.m_RenameCalls, 0);
}

TEST_F(HttpFileResult, RenameAndCleanupExceptionsStillReachTerminalState)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RenameThrows = true;
	Request.m_RemoveThrows = true;
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	EXPECT_NO_THROW(Request.Complete());
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CachedResponseReadsExistingFileWithoutRenaming)
{
	CFileResultRequest Request(m_pStorage.get());
	ASSERT_TRUE(Request.Start());
	Request.Complete(EHttpState::DONE, 304);
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	unsigned char *pData;
	size_t Length;
	Request.Result(&pData, &Length);
	EXPECT_EQ(std::string(reinterpret_cast<char *>(pData), Length), "old");
	EXPECT_EQ(Cached(), "old");
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_EQ(Request.m_ReadCalls, 1);
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath()));
}

TEST_F(HttpFileResult, CachedReadExceptionStillPublishesFailure)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_ReadThrows = true;
	ASSERT_TRUE(Request.Start());
	EXPECT_NO_THROW(Request.Complete(EHttpState::DONE, 304));
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Cached(), "old");
	EXPECT_EQ(Request.m_ReadCalls, 1);
}

TEST_F(HttpFileResult, CachedReadExceptionAfterBufferTransferStillPublishesFailure)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_ReadThrowsAfterAllocation = true;
	ASSERT_TRUE(Request.Start());
	EXPECT_NO_THROW(Request.Complete(EHttpState::DONE, 304));
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CachedReadFailurePublishesError)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_ReadFails = true;
	ASSERT_TRUE(Request.Start());
	Request.Complete(EHttpState::DONE, 304);
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, MissingCachedFileDoesNotBecomeSuccessfulEmptyResponse)
{
	CFileResultRequest Request(m_pStorage.get());
	ASSERT_TRUE(Request.Start());
	ASSERT_TRUE(m_pStorage->RemoveFile("body.bin", IStorage::TYPE_SAVE));
	Request.Complete(EHttpState::DONE, 304);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 0);
}

TEST_F(HttpFileResult, CachedCandidateCleanupExceptionSkipsReadAndPublishesError)
{
	CFileCompletion Consumer;
	CFileResultRequest Request(m_pStorage.get());
	Request.SetProgressCallback(&Consumer);
	Request.m_RemoveThrows = true;
	ASSERT_TRUE(Request.Start());
	EXPECT_NO_THROW(Request.Complete(EHttpState::DONE, 304));
	ExpectTerminal(Request, Consumer, EHttpState::ERROR);
	EXPECT_EQ(Request.m_ReadCalls, 0);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, AcceptedValidationPublishesCandidateOnce)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	EXPECT_EQ(Cached(), "old");
	CHttpResponseValidation Validation(Request);
	EXPECT_TRUE(Validation.Commit());
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_FALSE(Request.m_AllowDestinationRemoval);
	EXPECT_EQ(Cached(), "new");
}

TEST_F(HttpFileResult, ValidationRenameExceptionFailsWithoutRetry)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	Request.m_RenameThrows = true;
	CHttpResponseValidation Validation(Request);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath()));
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, FailedValidationPublicationDisallowsDestinationRemoval)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	Request.m_RenameFails = true;
	CHttpResponseValidation Validation(Request);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_FALSE(Request.m_AllowDestinationRemoval);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, ValidationRenameAndCleanupExceptionsStillConsumeDecision)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	Request.m_RenameThrows = true;
	Request.m_RemoveThrows = true;
	CHttpResponseValidation Validation(Request);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.m_RenameCalls, 1);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, RejectionCleanupExceptionCannotEscapeValidationDestructor)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_NO_FATAL_FAILURE(Receive(Request));
	Request.Complete();
	Request.m_RemoveThrows = true;
	{
		CHttpResponseValidation Validation(Request);
	}
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, UnconsumedRequestDestructionRemovesCandidate)
{
	std::string Temporary;
	{
		CFileResultRequest Request(m_pStorage.get());
		Request.ValidateBeforeOverwrite(true);
		ASSERT_NO_FATAL_FAILURE(Receive(Request));
		Request.Complete();
		Temporary = Request.TemporaryPath();
		EXPECT_TRUE(fs_is_file(Temporary.c_str()));
	}
	EXPECT_FALSE(fs_is_file(Temporary.c_str()));
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CachedValidationNeverRenamesOverExistingFile)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_TRUE(Request.Start());
	Request.Complete(EHttpState::DONE, 304);
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	CHttpResponseValidation Validation(Request);
	EXPECT_TRUE(Validation.Commit());
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, CachedValidationCleanupExceptionFailsWithoutRenaming)
{
	CFileResultRequest Request(m_pStorage.get());
	Request.ValidateBeforeOverwrite(true);
	ASSERT_TRUE(Request.Start());
	Request.Complete(EHttpState::DONE, 304);
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	Request.m_RemoveThrows = true;
	CHttpResponseValidation Validation(Request);
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Request.m_RenameCalls, 0);
	EXPECT_EQ(Cached(), "old");
}

TEST_F(HttpFileResult, FreshRequestCanPublishAfterFailedPublication)
{
	{
		CFileResultRequest Failed(m_pStorage.get());
		Failed.m_RenameThrows = true;
		ASSERT_NO_FATAL_FAILURE(Receive(Failed));
		EXPECT_NO_THROW(Failed.Complete());
		EXPECT_EQ(Failed.State(), EHttpState::ERROR);
	}
	CFileResultRequest Retry(m_pStorage.get());
	ASSERT_NO_FATAL_FAILURE(Receive(Retry));
	Retry.Complete();
	EXPECT_EQ(Retry.State(), EHttpState::DONE);
	EXPECT_EQ(Cached(), "new");
}
