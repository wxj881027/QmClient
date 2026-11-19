#include <game/client/components/qmclient/friend_last_seen.h>

#include <gtest/gtest.h>
#include <test/support/friend_last_seen_test_support.h>
#include <test/support/job_wait.h>
#include <test/support/regular_file_open_timeout.h>
#include <test/test.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if defined(CONF_FAMILY_UNIX)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	constexpr int64_t NOW = 1792800000;
	constexpr const char *CACHE_FILE = "qm_friend_last_seen.json";
	constexpr const char *OLD_CACHE = R"({"version":1,"players":[{"name":"Alice","date":"2026-10-20","checked_at":"100"}]})";
	constexpr const char *RESPONSE = R"({"recent_player_info":[{"name":"Alice","last_seen":"2026-10-23"}]})";

	class CFriendLastSeenServiceTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CFriendLastSeenTestFriends m_Friends;
		CFriendLastSeenTestEngine m_Engine;
		CFriendLastSeenTestHttp m_Http;
		CQmFriendLastSeenService m_Service{CreateFriendLastSeenTestRequest};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Friends.Set({"Alice", "Bob"});
		}
		void Update(int64_t Now = NOW, bool Enabled = true) { m_Service.Update(m_Friends, m_Engine, m_Http, Enabled, Now); }
		void Write(const char *pName, std::string_view Body)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Body.data(), static_cast<unsigned>(Body.size())), Body.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read(const char *pName)
		{
			void *pData = nullptr;
			unsigned Size = 0;
			const auto Result = m_pStorage->ReadFileCheckedBounded(pName, IStorage::TYPE_SAVE, &pData, &Size, 1024 * 1024);
			std::unique_ptr<void, decltype(&free)> Data(pData, free);
			EXPECT_EQ(Result, IStorage::EReadFileResult::SUCCESS);
			return Data ? std::string(static_cast<const char *>(Data.get()), Size) : "";
		}
		std::string Path(const char *pName)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pName, aPath, sizeof(aPath));
			return aPath;
		}
		void StartRequest()
		{
			ASSERT_NE(m_Service.Get("Alice", NOW), nullptr);
			Update();
			ASSERT_EQ(m_Http.m_Submissions, 1);
			ASSERT_NE(m_Http.m_pLast, nullptr);
		}
		void MakeDirty()
		{
			StartRequest();
			ASSERT_FALSE(HasFatalFailure());
			ASSERT_TRUE(m_Http.m_pLast->Finish("", 404));
			Update();
			ASSERT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
		}
	};

	class CFriendLastSeenServiceDeathTest : public CFriendLastSeenServiceTest
	{
	};

	class CBlockedFriendWorker : public IJob
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Released = false;
		void Run() override
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			m_Entered = true;
			m_Condition.notify_all();
			m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Released; });
		}

	public:
		bool WaitEntered()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Entered; });
		}
		void Release()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Released = true;
			m_Condition.notify_all();
		}
	};

	struct SReleaseFriendWorker
	{
		std::shared_ptr<CBlockedFriendWorker> m_pJob;
		~SReleaseFriendWorker() { m_pJob->Release(); }
	};

	class COneOccupiedFriendCandidate : public CFriendLastSeenTestStorage
	{
		bool m_Created = false;

	public:
		using CFriendLastSeenTestStorage::CFriendLastSeenTestStorage;
		char m_aOccupied[128]{};
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer = nullptr, int Size = 0) override
		{
			if((Flags & IOFLAG_WRITE) && !m_Created)
			{
				m_Created = true;
				str_copy(m_aOccupied, pName);
				IOHANDLE Other = m_Storage.OpenFile(pName, IOFLAG_WRITE | IOFLAG_EXCLUSIVE, Type);
				if(!Other)
					return nullptr;
				(void)io_write(Other, "other-owner", 11);
				(void)io_close(Other);
			}
			return CFriendLastSeenTestStorage::OpenFile(pName, Flags, Type, pBuffer, Size);
		}
	};

	class COccupiedFriendCandidates : public CFriendLastSeenTestStorage
	{
	public:
		using CFriendLastSeenTestStorage::CFriendLastSeenTestStorage;
		IOHANDLE OpenFile(const char *pName, int Flags, int, char * = nullptr, int = 0) override
		{
			++m_OpenCalls;
			str_copy(m_aLastCandidate, pName);
			m_UsedExclusive = (Flags & IOFLAG_EXCLUSIVE) != 0;
			return nullptr;
		}
		bool FileExists(const char *, int) override { return true; }
	};

#if defined(CONF_PLATFORM_LINUX) && !defined(CONF_PLATFORM_ANDROID)
	class CBufferedFlushFailedFriendStorage : public CFriendLastSeenTestStorage
	{
		char m_aBuffer[4096]{};

	public:
		using CFriendLastSeenTestStorage::CFriendLastSeenTestStorage;
		bool m_DeviceOpened = false;
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer = nullptr, int Size = 0) override
		{
			IOHANDLE Candidate = CFriendLastSeenTestStorage::OpenFile(pName, Flags, Type, pBuffer, Size);
			if(!Candidate)
				return nullptr;
			if(io_close(Candidate) != 0)
				return nullptr;
			IOHANDLE Full = io_open("/dev/full", IOFLAG_WRITE);
			if(!Full)
				return nullptr;
			if(setvbuf(static_cast<FILE *>(Full), m_aBuffer, _IOFBF, sizeof(m_aBuffer)) != 0)
			{
				(void)io_close(Full);
				return nullptr;
			}
			m_DeviceOpened = true;
			return Full;
		}
	};
#endif

#if defined(CONF_FAMILY_UNIX)
	class CSpecialFriendCandidate : public CFriendLastSeenTestStorage
	{
		bool m_Created = false;

	public:
		using CFriendLastSeenTestStorage::CFriendLastSeenTestStorage;
		bool m_Fifo = false;
		bool m_CreateSucceeded = false;
		char m_aSpecial[128]{};
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer = nullptr, int Size = 0) override
		{
			if((Flags & IOFLAG_WRITE) && !m_Created)
			{
				m_Created = true;
				str_copy(m_aSpecial, pName);
				char aPath[IO_MAX_PATH_LENGTH];
				m_Storage.GetCompletePath(Type, pName, aPath, sizeof(aPath));
				if(m_Fifo)
					m_CreateSucceeded = mkfifo(aPath, 0600) == 0;
				else
				{
					char aTarget[IO_MAX_PATH_LENGTH];
					m_Storage.GetCompletePath(Type, CACHE_FILE, aTarget, sizeof(aTarget));
					const std::unique_ptr<char, decltype(&free)> pAbsolute(realpath(aTarget, nullptr), free);
					m_CreateSucceeded = pAbsolute && symlink(pAbsolute.get(), aPath) == 0;
				}
				if(!m_CreateSucceeded)
					return nullptr;
			}
			return CFriendLastSeenTestStorage::OpenFile(pName, Flags, Type, pBuffer, Size);
		}
	};
#endif
}

TEST_F(CFriendLastSeenServiceTest, CompletedResponseUsesNativePoolAndKeepsTheNormalRequestSettings)
{
	ASSERT_TRUE(m_Engine.Start());
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_STREQ(m_Http.m_pLast->m_aHeader, "Accept: application/json");
	EXPECT_EQ(m_Http.m_pLast->TimeoutSettings().m_ConnectTimeoutMs, 5000);
	EXPECT_EQ(m_Http.m_pLast->RequestTimeoutMs(), 30000);
	EXPECT_EQ(m_Http.m_pLast->ResponseLimit(), 8 * 1024 * 1024);
	ASSERT_TRUE(m_Http.m_pLast->Finish(RESPONSE));
	Update();
	const auto pJob = m_Engine.m_pLast.lock();
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Update(NOW + 1);
	ASSERT_NE(m_Service.Find("Alice"), nullptr);
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-23");
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::READY);
	EXPECT_EQ(m_Http.m_Submissions, 1);
}

TEST_F(CFriendLastSeenServiceTest, RejectedParseAdmissionKeepsResponseUntilAWorkerCanAcceptIt)
{
	ASSERT_TRUE(m_Engine.Start());
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Http.m_pLast->Finish(RESPONSE));
	m_Engine.m_RejectNext = true;
	Update();
	EXPECT_EQ(m_Engine.m_Offers, 1);
	EXPECT_EQ(m_Engine.m_Accepted, 0);
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::LOADING);
	Update(NOW + 1);
	const auto pJob = m_Engine.m_pLast.lock();
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	Update(NOW + 1);
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-23");
	EXPECT_EQ(m_Engine.m_Offers, 2);
	EXPECT_EQ(m_Engine.m_Accepted, 1);
	EXPECT_EQ(m_Http.m_Submissions, 1);
}

TEST_F(CFriendLastSeenServiceTest, CancelledAcceptedJobKeepsInputAliveAndCannotPublishIntoSameNameRetry)
{
	ASSERT_TRUE(m_Engine.Start());
	const auto pBlocker = std::make_shared<CBlockedFriendWorker>();
	SReleaseFriendWorker Release{pBlocker};
	ASSERT_TRUE(m_Engine.TryAddJob(pBlocker));
	ASSERT_TRUE(pBlocker->WaitEntered());
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Http.m_pLast->Finish(RESPONSE));
	const std::weak_ptr<IHttpRequest> OldInput = m_Http.m_pLast;
	Update();
	auto pOldJob = m_Engine.m_pLast.lock();
	ASSERT_NE(pOldJob, nullptr);
	ASSERT_EQ(pOldJob->State(), IJob::STATE_QUEUED);
	m_Service.CancelRequests();
	ASSERT_NE(m_Service.Get("Alice", NOW + 2), nullptr);
	Update(NOW + 2);
	ASSERT_EQ(m_Http.m_Submissions, 2);
	EXPECT_FALSE(OldInput.expired());
	pBlocker->Release();
	ASSERT_TRUE(WaitForJobFinished(*pOldJob));
	Update(NOW + 2);
	EXPECT_TRUE(m_Service.Find("Alice")->m_Date.empty());
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::LOADING);
	pOldJob.reset();
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!OldInput.expired() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	EXPECT_TRUE(OldInput.expired());
	ASSERT_TRUE(m_Http.m_pLast->Finish(R"({"recent_player_info":[{"name":"Alice","last_seen":"2026-10-24"}]})"));
	Update(NOW + 2);
	const auto pNewJob = m_Engine.m_pLast.lock();
	ASSERT_NE(pNewJob, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pNewJob));
	Update(NOW + 3);
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-24");
}

TEST_F(CFriendLastSeenServiceTest, DisableStopsNetworkAndThrowingAbortStillAllowsReenable)
{
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	const auto pOld = m_Http.m_pLast;
	pOld->m_ThrowAbort = true;
	Update(NOW + 1, false);
	Update(NOW + 2, false);
	EXPECT_EQ(pOld->m_AbortCalls, 1);
	EXPECT_EQ(m_Http.m_Submissions, 1);
	ASSERT_NE(m_Service.Get("Alice", NOW + 2), nullptr);
	Update(NOW + 2);
	EXPECT_EQ(m_Http.m_Submissions, 2);
	EXPECT_NE(m_Http.m_pLast, pOld);
}

TEST_F(CFriendLastSeenServiceTest, DestructionContainsAbortExceptions)
{
	auto pService = std::make_unique<CQmFriendLastSeenService>(CreateFriendLastSeenTestRequest);
	ASSERT_NE(pService->Get("Alice", NOW), nullptr);
	pService->Update(m_Friends, m_Engine, m_Http, true, NOW);
	const auto pRequest = m_Http.m_pLast;
	ASSERT_NE(pRequest, nullptr);
	pRequest->m_ThrowAbort = true;
	EXPECT_NO_THROW(pService.reset());
	EXPECT_EQ(pRequest->m_AbortCalls, 1);
}

TEST_F(CFriendLastSeenServiceTest, TransportAdmissionErrorUsesBackoffAndRetainsOldDate)
{
	Write(CACHE_FILE, OLD_CACHE);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Service.Load(*m_pStorage, NOW));
	m_Http.m_FailAdmission = true;
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	Update();
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::ERROR);
	EXPECT_EQ(m_Service.Find("Alice")->m_Date, "2026-10-20");
	EXPECT_EQ(m_Service.Find("Alice")->m_RetryAt, NOW + CQmFriendLastSeenCache::RETRY_SECONDS);
	ASSERT_NE(m_Service.Get("Bob", NOW + 59), nullptr);
	Update(NOW + 59);
	EXPECT_EQ(m_Http.m_Submissions, 1);
	ASSERT_NE(m_Service.Get("Bob", NOW + 60), nullptr);
	Update(NOW + 60);
	EXPECT_EQ(m_Http.m_Submissions, 2);
	EXPECT_NE(str_find(m_Http.m_pLast->Url(), "player=Bob"), nullptr);
}

TEST_F(CFriendLastSeenServiceTest, UnsupportedAndResourceFailedCheckedLoadsNeverUseRawOpen)
{
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_UnsupportedRead = true;
	EXPECT_FALSE(m_Service.Load(Storage, NOW));
	Storage.m_UnsupportedRead = false;
	Storage.m_ThrowRead = true;
	EXPECT_FALSE(m_Service.Load(Storage, NOW));
	EXPECT_EQ(Storage.m_OpenCalls, 0);
}

TEST_F(CFriendLastSeenServiceTest, InvalidOrOversizedCheckedLoadKeepsCurrentInputAndCache)
{
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	const auto pOldRequest = m_Http.m_pLast;
	const auto *pOldEntry = m_Service.Find("Alice");
	Write(CACHE_FILE, "invalid");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FALSE(m_Service.Load(*m_pStorage, NOW));
	EXPECT_EQ(m_Service.Find("Alice"), pOldEntry);
	EXPECT_EQ(pOldRequest->m_AbortCalls, 0);
	Write(CACHE_FILE, std::string(1024 * 1024 + 1, ' '));
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FALSE(m_Service.Load(*m_pStorage, NOW));
	EXPECT_EQ(pOldRequest->m_AbortCalls, 0);
	ASSERT_TRUE(pOldRequest->Finish("", 404));
	Update();
	EXPECT_EQ(m_Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
}

TEST_F(CFriendLastSeenServiceTest, SuccessfulLoadCancelsOldTransportEvenIfAbortThrows)
{
	StartRequest();
	ASSERT_FALSE(HasFatalFailure());
	const auto pOld = m_Http.m_pLast;
	pOld->m_ThrowAbort = true;
	Write(CACHE_FILE, R"({"version":1,"players":[{"name":"Bob","date":"2026-10-24","checked_at":"100"}]})");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Service.Load(*m_pStorage, NOW));
	EXPECT_EQ(pOld->m_AbortCalls, 1);
	EXPECT_EQ(m_Service.Find("Alice"), nullptr);
	ASSERT_NE(m_Service.Find("Bob"), nullptr);
	EXPECT_EQ(m_Service.Find("Bob")->m_Date, "2026-10-24");
	EXPECT_EQ(m_Http.m_Submissions, 1);
}

TEST_F(CFriendLastSeenServiceTest, SaveUsesExclusiveCandidateAndLeavesPreviousFixedTempUntouched)
{
	Write(CACHE_FILE, OLD_CACHE);
	Write("qm_friend_last_seen.json.tmp", "foreign-temp");
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	ASSERT_TRUE(m_Service.Save(Storage));
	EXPECT_TRUE(Storage.m_UsedExclusive);
	EXPECT_FALSE(Storage.m_AllowedDestinationRemoval);
	EXPECT_EQ(Storage.m_Renames, 1);
	EXPECT_EQ(Storage.m_Removes, 0);
	EXPECT_EQ(Read("qm_friend_last_seen.json.tmp"), "foreign-temp");
	CQmFriendLastSeenCache Loaded;
	ASSERT_TRUE(Loaded.Load(Read(CACHE_FILE), NOW));
	ASSERT_NE(Loaded.Find("Alice"), nullptr);
	EXPECT_EQ(Loaded.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
}

TEST_F(CFriendLastSeenServiceTest, FailedWriteRemovesOnlyOwnedCandidateAndPreservesPreviousCache)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_ReadOnlyWrite = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
	EXPECT_EQ(Storage.m_Renames, 0);
	EXPECT_EQ(Storage.m_Removes, 1);
	EXPECT_FALSE(m_pStorage->FileExists(Storage.m_aLastCandidate, IStorage::TYPE_SAVE));
}

TEST_F(CFriendLastSeenServiceTest, RejectedReplacementPreservesOldCacheAndCanRetryWithNewCandidate)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_RejectRename = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
	EXPECT_FALSE(Storage.m_AllowedDestinationRemoval);
	EXPECT_EQ(Storage.m_Removes, 1);
	const std::string FirstCandidate = Storage.m_aLastCandidate;
	EXPECT_FALSE(m_pStorage->FileExists(FirstCandidate.c_str(), IStorage::TYPE_SAVE));
	Storage.m_RejectRename = false;
	ASSERT_TRUE(m_Service.Save(Storage));
	EXPECT_NE(Storage.m_aLastCandidate, FirstCandidate);
	CQmFriendLastSeenCache Loaded;
	EXPECT_TRUE(Loaded.Load(Read(CACHE_FILE), NOW));
}

TEST_F(CFriendLastSeenServiceTest, FailedOpenDoesNotRemoveAnUnownedPath)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_RejectWrite = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 1);
	EXPECT_EQ(Storage.m_Removes, 0);
	EXPECT_EQ(Storage.m_Renames, 0);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
}

TEST_F(CFriendLastSeenServiceTest, OccupiedCandidateKeepsItsContentsWhileAnotherCandidatePublishes)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	COneOccupiedFriendCandidate Storage(*m_pStorage);
	ASSERT_TRUE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 2);
	EXPECT_EQ(Storage.m_Removes, 0);
	EXPECT_EQ(Read(Storage.m_aOccupied), "other-owner");
}

TEST_F(CFriendLastSeenServiceTest, RepeatedOccupiedCandidatesHaveABoundedAttemptCount)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	COccupiedFriendCandidates Storage(*m_pStorage);
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 32);
	EXPECT_TRUE(Storage.m_UsedExclusive);
	EXPECT_EQ(Storage.m_Removes, 0);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
}

TEST_F(CFriendLastSeenServiceTest, CleanupExceptionDoesNotEscapeOrDeletePreviousCache)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_RejectRename = true;
	Storage.m_ThrowRemove = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_Removes, 1);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
	EXPECT_TRUE(m_pStorage->FileExists(Storage.m_aLastCandidate, IStorage::TYPE_SAVE));
}

TEST_F(CFriendLastSeenServiceTest, OverlongWholePathFailsBeforeOpeningAnyCandidate)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_OverlongPath = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 0);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
}

TEST_F(CFriendLastSeenServiceTest, PathAndOpenResourceFailuresPreserveOldCacheWithoutCleanupOfUnownedFiles)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_ThrowCompletePath = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 0);
	Storage.m_ThrowCompletePath = false;
	Storage.m_ThrowOpen = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	EXPECT_EQ(Storage.m_OpenCalls, 1);
	EXPECT_EQ(Storage.m_Removes, 0);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
}

TEST_F(CFriendLastSeenServiceTest, UnknownBackendExceptionAfterReplacementRemainsVisibleWithoutFalseRollback)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CFriendLastSeenTestStorage Storage(*m_pStorage);
	Storage.m_ThrowAfterRename = true;
	EXPECT_THROW(m_Service.Save(Storage), std::bad_alloc);
	EXPECT_EQ(Storage.m_Renames, 1);
	EXPECT_FALSE(m_pStorage->FileExists(Storage.m_aLastCandidate, IStorage::TYPE_SAVE));
	CQmFriendLastSeenCache Loaded;
	ASSERT_TRUE(Loaded.Load(Read(CACHE_FILE), NOW));
	ASSERT_NE(Loaded.Find("Alice"), nullptr);
	EXPECT_EQ(Loaded.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
}

#if defined(CONF_PLATFORM_LINUX) && !defined(CONF_PLATFORM_ANDROID)
TEST_F(CFriendLastSeenServiceTest, BufferedFullDeviceFailurePreservesOldCacheAndCleansOwnedCandidate)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CBufferedFlushFailedFriendStorage Storage(*m_pStorage);
	EXPECT_FALSE(m_Service.Save(Storage));
	ASSERT_TRUE(Storage.m_DeviceOpened);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
	EXPECT_EQ(Storage.m_Renames, 0);
	EXPECT_EQ(Storage.m_Removes, 1);
	EXPECT_FALSE(m_pStorage->FileExists(Storage.m_aLastCandidate, IStorage::TYPE_SAVE));
}
#endif

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST_F(CFriendLastSeenServiceDeathTest, CacheFifoWithoutWriterIsRejectedWithinChildDeadline)
{
	const std::string FifoPath = Path(CACHE_FILE);
	ASSERT_EQ(mkfifo(FifoPath.c_str(), 0600), 0);
	EXPECT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			_exit(m_Service.Load(*m_pStorage, NOW) ? 1 : 0);
		},
		::testing::ExitedWithCode(0), "");
}

TEST_F(CFriendLastSeenServiceDeathTest, PreviousFixedTempFifoCannotBlockSaving)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	const std::string FifoPath = Path("qm_friend_last_seen.json.tmp");
	ASSERT_EQ(mkfifo(FifoPath.c_str(), 0600), 0);
	EXPECT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			_exit(m_Service.Save(*m_pStorage) ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
	struct stat Info{};
	ASSERT_EQ(lstat(FifoPath.c_str(), &Info), 0);
	EXPECT_TRUE(S_ISFIFO(Info.st_mode));
	CQmFriendLastSeenCache Loaded;
	EXPECT_TRUE(Loaded.Load(Read(CACHE_FILE), NOW));
}

TEST_F(CFriendLastSeenServiceDeathTest, ExistingCandidateFifoIsNeitherOpenedBlockingNorRemoved)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			CSpecialFriendCandidate Storage(*m_pStorage);
			Storage.m_Fifo = true;
			const bool Saved = m_Service.Save(Storage);
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, Storage.m_aSpecial, aPath, sizeof(aPath));
			struct stat Info{};
			const bool Preserved = lstat(aPath, &Info) == 0 && S_ISFIFO(Info.st_mode);
			(void)m_pStorage->RemoveFile(Storage.m_aSpecial, IStorage::TYPE_SAVE);
			_exit(!Saved && Storage.m_CreateSucceeded && Preserved && Storage.m_Removes == 0 ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
}

TEST_F(CFriendLastSeenServiceTest, CandidateSymlinkCannotTruncateThePreviousCache)
{
	Write(CACHE_FILE, OLD_CACHE);
	MakeDirty();
	ASSERT_FALSE(HasFatalFailure());
	CSpecialFriendCandidate Storage(*m_pStorage);
	Storage.m_RejectRename = true;
	EXPECT_FALSE(m_Service.Save(Storage));
	ASSERT_TRUE(Storage.m_CreateSucceeded);
	EXPECT_EQ(Read(CACHE_FILE), OLD_CACHE);
	struct stat Info{};
	ASSERT_EQ(lstat(Path(Storage.m_aSpecial).c_str(), &Info), 0);
	EXPECT_TRUE(S_ISLNK(Info.st_mode));
	EXPECT_EQ(Storage.m_OpenCalls, 2);
	EXPECT_EQ(Storage.m_Removes, 1);
}
#endif
