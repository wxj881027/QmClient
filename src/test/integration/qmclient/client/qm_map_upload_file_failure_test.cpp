#include <base/detect.h>
#include <base/thread.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/qm_map_upload.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <memory>
#include <utility>

#if defined(CONF_PLATFORM_LINUX)
#include <test/support/prediction_allocation_failure.h>
#include <unistd.h>

#include <filesystem>
#include <new>
#include <system_error>
#endif

namespace
{
	class CUploadEngine final : public IEngine
	{
		CJobPool &m_Pool;

	public:
		std::shared_ptr<IJob> m_pJob;

		explicit CUploadEngine(CJobPool &Pool) :
			m_Pool(Pool)
		{
		}

		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			if(!m_Pool.TryAdd(pJob))
				return false;
			m_pJob = std::move(pJob);
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class CUploadHttpRecorder final : public IHttp
	{
	public:
		int m_Submissions = 0;
		void Run(std::shared_ptr<IHttpRequest>) override { ++m_Submissions; }
		bool HasIpresolveBug() const override { return false; }
	};

	class QmMapUploadFileFailure : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void ExpectReadFailure(const char *pPath, THREAD_CREATE pfnCreate = thread_try_init)
		{
			CJobPool Pool;
			ASSERT_TRUE(Pool.TryInit(1, pfnCreate));
			CUploadEngine Engine(Pool);
			CUploadHttpRecorder Http;
			QmMapUpload::CUpload Upload;
			Upload.Start(m_pStorage.get(), &Http, &Engine, "https://upload.invalid/maps", pPath, IStorage::TYPE_SAVE, "player");
			ASSERT_NE(Engine.m_pJob, nullptr);
			ASSERT_TRUE(WaitForJobFinished(*Engine.m_pJob));
			Pool.Shutdown();
			Upload.Poll();
			EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::READ_FAILED);
			EXPECT_FALSE(Upload.Busy());
			EXPECT_EQ(Http.m_Submissions, 0);
		}
	};

#if defined(CONF_PLATFORM_LINUX)
	void *StartWorkerWithReadAllocationFailure(void (*pfnEntry)(void *), void *pUser, const char *pName) noexcept
	{
		struct SWorker
		{
			void (*m_pfnEntry)(void *);
			void *m_pUser;
		};
		auto *pWorker = new(std::nothrow) SWorker{pfnEntry, pUser};
		if(!pWorker)
			return nullptr;
		void *pThread = thread_try_init([](void *pData) {
			const std::unique_ptr<SWorker> pOwned(static_cast<SWorker *>(pData));
			bool Triggered;
			{
				// Linux 打开和测长使用 C 接口，作业的第一次 C++ 分配用于读取缓冲。
				CPredictionAllocationFailure Failure;
				pOwned->m_pfnEntry(pOwned->m_pUser);
				Triggered = Failure.Triggered();
			}
			EXPECT_TRUE(Triggered);
		},
			pWorker, pName);
		if(!pThread)
			delete pWorker;
		return pThread;
	}
#endif
}

TEST_F(QmMapUploadFileFailure, DirectoryWithMapNameIsRejectedWithoutHttp)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("directory.map", IStorage::TYPE_SAVE));
	ExpectReadFailure("directory.map");
}

#if defined(CONF_PLATFORM_LINUX)
TEST_F(QmMapUploadFileFailure, ReadBufferAllocationFailureClosesTheMap)
{
	constexpr const char *pDescriptorDir = "/proc/self/fd";
	if(access(pDescriptorDir, R_OK | X_OK) != 0)
		GTEST_SKIP() << "Process descriptors are unavailable";

	const char *pMap = "buffer.map";
	IOHANDLE File = m_pStorage->OpenFile(pMap, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "DATA", 4), 4u);
	ASSERT_EQ(io_close(File), 0);

	ASSERT_NO_FATAL_FAILURE(ExpectReadFailure(pMap, StartWorkerWithReadAllocationFailure));

	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pMap, aPath, sizeof(aPath));
	for(const auto &Entry : std::filesystem::directory_iterator(pDescriptorDir))
	{
		std::error_code Error;
		EXPECT_FALSE(std::filesystem::equivalent(Entry.path(), aPath, Error)) << Entry.path();
	}
}
#endif
