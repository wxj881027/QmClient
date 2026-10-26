#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/skin7_refresh_test_engine.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <new>
#include <set>
#include <string>
#include <thread>

namespace
{
	// 只在真实存储调用完成后注入边界故障；目录、JSON、文件blob及作业仍走生产实现。
	class CScanFaultStorage : public IStorage
	{
		IStorage &m_Storage;

	public:
		const char *m_pFailDirectory = nullptr;
		const char *m_pFailName = nullptr;
		bool m_Unknown = false;
		bool m_ThrowAfterRead = false;
		std::optional<EFsListDirectoryResult> m_ForcedDirectoryResult;
		bool m_Triggered = false;
		CQmSkin7InfoJob *m_pAbortJob = nullptr;
		std::atomic<bool> m_AbortHit{false};
		explicit CScanFaultStorage(IStorage &Storage) :
			m_Storage(Storage) {}
		int NumPaths() const override { return m_Storage.NumPaths(); }
		bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
		bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
		EFsListDirectoryResult Enumerate(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser, bool Checked)
		{
			struct SContext
			{
				CScanFaultStorage &m_Storage;
				const char *m_pPath;
				FS_LISTDIR_CALLBACK m_Callback;
				void *m_pUser;
			} Context{*this, pPath, Callback, pUser};
			const auto Wrapped = [](const char *pName, int IsDir, int StorageType, void *pUser) {
				auto &Context = *static_cast<SContext *>(pUser);
				const int Result = Context.m_Callback(pName, IsDir, StorageType, Context.m_pUser);
				auto &Self = Context.m_Storage;
				if(Self.m_pFailDirectory && Self.m_pFailName && str_comp(Context.m_pPath, Self.m_pFailDirectory) == 0 && str_comp(pName, Self.m_pFailName) == 0)
				{
					Self.m_Triggered = true;
					if(Self.m_pAbortJob)
					{
						Self.m_pAbortJob->Abort();
						Self.m_AbortHit.store(true);
						return 1;
					}
					if(Self.m_Unknown)
						throw 7;
					throw std::bad_alloc();
				}
				return Result;
			};
			if(!Checked)
			{
				m_Storage.ListDirectory(Type, pPath, Wrapped, &Context);
				return EFsListDirectoryResult::SUCCESS;
			}
			const auto Result = m_Storage.ListDirectoryChecked(Type, pPath, Wrapped, &Context);
			if(m_ForcedDirectoryResult && Result == EFsListDirectoryResult::SUCCESS)
			{
				m_Triggered = true;
				return *m_ForcedDirectoryResult;
			}
			return Result;
		}
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			(void)Enumerate(Type, pPath, Callback, pUser, false);
		}
		EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			return Enumerate(Type, pPath, Callback, pUser, true);
		}
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Type, pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override { return m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size); }
		bool FileExists(const char *pName, int Type) override { return m_Storage.FileExists(pName, Type); }
		bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Type); }
		bool ReadFile(const char *pName, int Type, void **ppData, unsigned *pSize) override
		{
			const bool Result = m_Storage.ReadFile(pName, Type, ppData, pSize);
			if(m_ThrowAfterRead && str_comp(pName, "skins7/qm_info_fail.json") == 0)
				throw std::bad_alloc();
			return Result;
		}
		char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Type); }
		bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Type, pCreated, pModified); }
		bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Type, pSha, pCrc); }
		bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Type, pBuffer, Size); }
		size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Type, pEntries); }
		bool RemoveFile(const char *pName, int Type) override { return m_Storage.RemoveFile(pName, Type); }
		bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Type); }
		bool RenameFile(const char *pOld, const char *pNew, int Type, bool Remove) override { return m_Storage.RenameFile(pOld, pNew, Type, Remove); }
		bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Type); }
		void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override { m_Storage.GetCompletePath(Type, pName, pBuffer, Size); }
		bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
		bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_Storage.RenameBinaryFile(pOld, pNew); }
		const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPath(pName, pBuffer, Size); }
		const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPathAbsolute(pName, pBuffer, Size); }
	};

	bool DecodeThenThrow(const void *pData, size_t Size, const char *pPath, CImageInfo &Image)
	{
		if(!CImageLoader::LoadPng(pData, Size, pPath, Image))
			return false;
		throw std::bad_alloc();
	}

	bool DecodeBotThenThrow(const void *pData, size_t Size, const char *pPath, CImageInfo &Image)
	{
		const bool Result = CImageLoader::LoadPng(pData, Size, pPath, Image);
		if(Result && str_endswith(pPath, "/bot.png"))
			throw std::bad_alloc();
		return Result;
	}

	using TRequest = CQmSkin7RefreshRequest<CSkins7::CSkinPart>;
	using TTime = std::chrono::nanoseconds;

	class CQmSkin7ScanRetryTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CScanFaultStorage> m_pFault;
		CJobPool m_Pool;
		bool m_PoolActive = false;
		std::shared_ptr<CQmSkin7InfoJob> m_pJob;
		CSkin7RefreshTestEngine m_Engine;
		TRequest m_Request;
		CQmSkin7LoadCompletion m_Completion;
		int m_Callbacks = 0;

		void Write(const char *pName, const char *pBody)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pBody, str_length(pBody)), static_cast<unsigned>(str_length(pBody)));
			EXPECT_EQ(io_close(File), 0);
		}

		void WriteImage(const char *pName, size_t Width, size_t Height)
		{
			std::array<uint8_t, 12 * 5 * 4> aPixels;
			aPixels.fill(123);
			CImageInfo Image;
			Image.m_Width = Width;
			Image.m_Height = Height;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixels.data();
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, pName, Image));
		}

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			Write("skins7/qm_info_fail.json", R"({"skin":{}})");
			ASSERT_FALSE(HasFatalFailure());
			m_pFault = std::make_unique<CScanFaultStorage>(*m_pStorage);
			m_PoolActive = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolActive);
			m_Engine.m_pPool = &m_Pool;
			m_Completion.Set([&] { ++m_Callbacks; });
		}

		void Queue(CQmSkin7InfoJob::FDecodeAccessory Decoder = nullptr)
		{
			m_pJob = Decoder ? std::make_shared<CQmSkin7InfoJob>(m_pFault.get(), Decoder) : std::make_shared<CQmSkin7InfoJob>(m_pFault.get());
			ASSERT_TRUE(m_Pool.TryAdd(m_pJob));
			ASSERT_TRUE(WaitForJobDone(*m_pJob));
		}

		void FailRoot()
		{
			m_pFault->m_pFailDirectory = "skins7";
			m_pFault->m_pFailName = "qm_info_fail.json";
		}

		void ExpectEmptyResult()
		{
			EXPECT_FALSE(m_pJob->Succeeded());
			EXPECT_TRUE(m_pJob->Entries().empty());
			EXPECT_TRUE(m_pJob->Parts().empty());
			auto Hat = m_pJob->TakeXmasHat();
			auto Bot = m_pJob->TakeBot();
			EXPECT_EQ(Hat.m_pData, nullptr);
			EXPECT_EQ(Bot.m_pData, nullptr);
			Hat.Free();
			Bot.Free();
		}

		std::optional<TRequest::SPlan> TryStart(TTime Now)
		{
			return m_Request.TryStart(m_Engine, m_pFault.get(), CSkins7::SKINFLAG_STANDARD, Now);
		}

		void TearDown() override
		{
			if(m_PoolActive)
				m_Pool.Shutdown();
		}
	};
}

TEST_F(CQmSkin7ScanRetryTest, RootCallbackExceptionDiscardsPreviouslyParsedDescriptions)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_pFault->m_Triggered);
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, PartCallbackExceptionDiscardsDescriptionsAndDiscoveredParts)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("skins7/body", IStorage::TYPE_SAVE));
	Write("skins7/body/qm_info_fail.png", "part discovery only");
	m_pFault->m_pFailDirectory = "skins7/body";
	m_pFault->m_pFailName = "qm_info_fail.png";
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_pFault->m_Triggered);
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, ReadExceptionAfterNativeBlobAllocationRejectsWholeScan)
{
	m_pFault->m_ThrowAfterRead = true;
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, UnknownCppExceptionDoesNotPublishPartialResult)
{
	FailRoot();
	m_pFault->m_Unknown = true;
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, FailedScanCannotPublishAnEmptyAccessoryAsSuccessfulReplacement)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	int Next = 0;
	EXPECT_FALSE(m_pJob->PublishNextAccessory(Next, [](const auto &, bool) {
		ADD_FAILURE() << "failed scan reached accessory publication";
		return true;
	}));
	EXPECT_EQ(Next, 0);
}

TEST_F(CQmSkin7ScanRetryTest, SecondAccessoryExceptionDiscardsAlreadyPreparedHat)
{
	WriteImage("skins7/xmas_hat.png", 2, 4);
	WriteImage("skins7/bot.png", 12, 5);
	ASSERT_FALSE(HasFatalFailure());
	Queue(DecodeBotThenThrow);
	ASSERT_FALSE(HasFatalFailure());
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, MalformedIndividualJsonKeepsNormalSkipAndSuccessfulScan)
{
	Write("skins7/qm_invalid.json", "{");
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_pJob->Succeeded());
	const auto &Entries = m_pJob->Entries();
	EXPECT_NE(std::find_if(Entries.begin(), Entries.end(), [](const auto &Entry) { return Entry.m_Name == "qm_info_fail"; }), Entries.end());
	EXPECT_EQ(std::find_if(Entries.begin(), Entries.end(), [](const auto &Entry) { return Entry.m_Name == "qm_invalid"; }), Entries.end());
}

TEST_F(CQmSkin7ScanRetryTest, SuccessfulScanIsNotConvertedToRetryOrDetachedFromCallback)
{
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FALSE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	EXPECT_NE(m_pJob, nullptr);
	EXPECT_FALSE(m_Request.IsPending());
	EXPECT_TRUE(m_Completion.HasPending());
}

TEST_F(CQmSkin7ScanRetryTest, QueuedJobIsNotReadOrRetriedAsFinishedFailure)
{
	m_pJob = std::make_shared<CQmSkin7InfoJob>(m_pFault.get());
	EXPECT_FALSE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	EXPECT_NE(m_pJob, nullptr);
	EXPECT_FALSE(m_Request.IsPending());
}

TEST_F(CQmSkin7ScanRetryTest, FailedScanWaitsRetryDelayThenRealRescanRetainsCompletion)
{
	WriteImage("skins7/xmas_hat.png", 2, 4);
	ASSERT_FALSE(HasFatalFailure());
	Queue(DecodeThenThrow);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_FALSE(m_pJob->Succeeded());
	const TTime Now = std::chrono::seconds(10);
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, Now));
	EXPECT_EQ(m_pJob, nullptr);
	EXPECT_FALSE(m_Completion.HasPending());
	EXPECT_FALSE(TryStart(Now));
	EXPECT_FALSE(TryStart(Now + TRequest::SCAN_RETRY_DELAY - TTime(1)));
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	auto Plan = TryStart(Now + TRequest::SCAN_RETRY_DELAY);
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	EXPECT_TRUE(Plan->m_pJob->Succeeded());
	EXPECT_EQ(m_Callbacks, 0);
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7ScanRetryTest, ExistingNewRequestWinsFailedOldScanWithoutDelay)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	int NewCallbacks = 0;
	m_Request.Request([&] { ++NewCallbacks; });
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	m_pFault->m_pFailDirectory = nullptr;
	auto Plan = TryStart(TTime(0));
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	Plan->m_Callback();
	EXPECT_EQ(NewCallbacks, 1);
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7ScanRetryTest, NewUserRequestReplacesDelayedRetryAndCanStartImmediately)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	int NewCallbacks = 0;
	m_Request.Request([&] { ++NewCallbacks; });
	m_pFault->m_pFailDirectory = nullptr;
	auto Plan = TryStart(TTime(0));
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	Plan->m_Callback();
	EXPECT_EQ(NewCallbacks, 1);
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7ScanRetryTest, CancelDelayedRetryDoesNotSubmitNewJobOrReviveCompletion)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	m_Request.Cancel();
	EXPECT_FALSE(TryStart(std::chrono::seconds(1)));
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	EXPECT_FALSE(m_Completion.HasPending());
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7ScanRetryTest, AdmissionRejectionAfterDelayKeepsTransferredCallbackForLaterAcceptance)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	m_Engine.m_Accept = false;
	EXPECT_FALSE(TryStart(std::chrono::seconds(1)));
	EXPECT_TRUE(m_Request.IsPending());
	m_Engine.m_Accept = true;
	m_pFault->m_pFailDirectory = nullptr;
	auto Plan = TryStart(std::chrono::seconds(1));
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7ScanRetryTest, RetryWithoutCompletionCallbackStillStartsValidScan)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	m_Completion.Cancel();
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	m_pFault->m_pFailDirectory = nullptr;
	auto Plan = TryStart(std::chrono::seconds(1));
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	EXPECT_TRUE(Plan->m_pJob->Succeeded());
	EXPECT_FALSE(Plan->m_Callback);
}

TEST_F(CQmSkin7ScanRetryTest, RetryDeadlineSaturatesInsteadOfWrappingAtMaximumTime)
{
	FailRoot();
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime::max() - TTime(1)));
	EXPECT_FALSE(TryStart(TTime::max() - TTime(1)));
	m_pFault->m_pFailDirectory = nullptr;
	auto Plan = TryStart(TTime::max());
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	EXPECT_TRUE(Plan->m_pJob->Succeeded());
}

TEST_F(CQmSkin7ScanRetryTest, CancellationAfterNativeCallbackDiscardsPartialResultAfterWorkerJoins)
{
	FailRoot();
	m_pJob = std::make_shared<CQmSkin7InfoJob>(m_pFault.get());
	m_pFault->m_pAbortJob = m_pJob.get();
	ASSERT_TRUE(m_Pool.TryAdd(m_pJob));
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!m_pFault->m_AbortHit.load() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	const bool Hit = m_pFault->m_AbortHit.load();
	m_Pool.Shutdown();
	m_PoolActive = false;
	ASSERT_TRUE(Hit);
	EXPECT_EQ(m_pJob->State(), IJob::STATE_ABORTED);
	ExpectEmptyResult();
	EXPECT_FALSE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
}

TEST_F(CQmSkin7ScanRetryTest, NativeDirectoryFailureStatusRejectsParsedResultAndCanRecover)
{
	m_pFault->m_ForcedDirectoryResult = EFsListDirectoryResult::IO_FAILURE;
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_pFault->m_Triggered);
	ExpectEmptyResult();
	ASSERT_TRUE(m_Request.RetryFailedScan(m_pJob, m_Completion, TTime(0)));
	m_pFault->m_ForcedDirectoryResult.reset();
	auto Plan = TryStart(std::chrono::seconds(1));
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	EXPECT_TRUE(Plan->m_pJob->Succeeded());
}

TEST_F(CQmSkin7ScanRetryTest, NativeFileAtDirectoryPathIsNotAcceptedAsEmptySkinScan)
{
	ASSERT_TRUE(m_pStorage->RemoveFile("skins7/qm_info_fail.json", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->RemoveFolder("skins7", IStorage::TYPE_SAVE));
	Write("skins7", "not a directory");
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, UnsupportedCheckedBackendIsNotAcceptedAsCompleteScan)
{
	m_pFault->m_ForcedDirectoryResult = EFsListDirectoryResult::UNSUPPORTED;
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, UnexpectedCallbackStopIsNotAcceptedWithoutCancellation)
{
	m_pFault->m_ForcedDirectoryResult = EFsListDirectoryResult::STOPPED;
	Queue();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_pJob->State(), IJob::STATE_DONE);
	ExpectEmptyResult();
}

TEST_F(CQmSkin7ScanRetryTest, BaseStorageCheckedDefaultReportsUnsupportedWithoutLegacyTraversal)
{
	int Calls = 0;
	const auto Result = m_pFault->IStorage::ListDirectoryChecked(IStorage::TYPE_ALL, "skins7", [](const char *, int, int, void *pUser) {
		++*static_cast<int *>(pUser);
		return 0; }, &Calls);
	EXPECT_EQ(Result, EFsListDirectoryResult::UNSUPPORTED);
	EXPECT_EQ(Calls, 0);
}
