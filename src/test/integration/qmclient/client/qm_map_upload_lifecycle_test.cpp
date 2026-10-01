#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <game/client/components/qmclient/qm_map_upload.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	class CDeferredUploadEngine : public IEngine
	{
	public:
		std::shared_ptr<IJob> m_pJob;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { m_pJob = std::move(pJob); }
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void CompleteJob()
		{
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(m_pJob);
			Pool.Shutdown();
			EXPECT_EQ(m_pJob->State(), IJob::STATE_DONE);
			m_pJob.reset();
		}
	};

	class CUploadHttpRecorder : public IHttp
	{
	public:
		int m_Submissions = 0;
		void Run(std::shared_ptr<IHttpRequest>) override { ++m_Submissions; }
		bool HasIpresolveBug() const override { return false; }
	};
}

TEST(QmMapUploadLifecycle, CancelBeforeDeferredPreparationSubmitsNothingAndCanReset)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDeferredUploadEngine Engine;
	CUploadHttpRecorder Http;
	QmMapUpload::CUpload Upload;
	Upload.Start(pStorage.get(), &Http, &Engine, "https://upload.test/maps", "missing.map", IStorage::TYPE_SAVE, "player");
	ASSERT_TRUE(Upload.Busy());
	Upload.Cancel();
	Upload.Cancel();
	EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::CANCELLED);
	Upload.Reset();
	EXPECT_TRUE(Upload.Busy());
	Engine.CompleteJob();
	Upload.Poll();
	EXPECT_FALSE(Upload.Busy());
	EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::CANCELLED);
	EXPECT_EQ(Http.m_Submissions, 0);
	Upload.Reset();
	EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::IDLE);
}

TEST(QmMapUploadLifecycle, MissingIsolatedFileFailsPreparationWithoutHttpAndAllowsRetry)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDeferredUploadEngine Engine;
	CUploadHttpRecorder Http;
	QmMapUpload::CUpload Upload;
	for(int Attempt = 0; Attempt < 2; ++Attempt)
	{
		SCOPED_TRACE(Attempt);
		Upload.Start(pStorage.get(), &Http, &Engine, "https://upload.test/maps", "missing.map", IStorage::TYPE_SAVE, "player");
		ASSERT_TRUE(Upload.Busy());
		Engine.CompleteJob();
		Upload.Poll();
		EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::READ_FAILED);
		EXPECT_FALSE(Upload.Busy());
		EXPECT_EQ(Http.m_Submissions, 0);
	}
}
