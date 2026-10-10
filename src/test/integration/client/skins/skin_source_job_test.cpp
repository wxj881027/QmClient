#include <engine/gfx/image_loader.h>
#include <engine/http.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/skin_source_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	// 只替换 HTTP 传输；校验后的磁盘替换使用 IHttpRequest 的生产实现。
	class CTestSkinRequest : public IHttpRequest
	{
	public:
		explicit CTestSkinRequest(IStorage *pStorage) :
			IHttpRequest("https://skins.test/test%20skin.png")
		{
			WriteToFileAndMemory(pStorage, "downloadedskins/test skin.png", IStorage::TYPE_SAVE);
			ValidateBeforeOverwrite(true);
			m_State = EHttpState::RUNNING;
		}
		void Header(const char *) override {}
		void Finish(const std::vector<uint8_t> &vBody, int Status = 200)
		{
			m_StatusCode = Status;
			m_ResponseLength = vBody.size();
			m_pBuffer = static_cast<unsigned char *>(malloc(vBody.size() + 1));
			if(!vBody.empty())
				mem_copy(m_pBuffer, vBody.data(), vBody.size());
			m_BufferSize = vBody.size() + 1;
			IOHANDLE File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			if(!vBody.empty())
				io_write(File, vBody.data(), vBody.size());
			io_close(File);
			m_State = EHttpState::DONE;
		}
	};

	class CTestSkinHttp : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CTestSkinRequest>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CTestSkinRequest>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	// 有界双向 barrier 固定“预处理已完成、返回前取消”的交错，无任意 sleep。
	class CPrepareBarrier
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Released = false;

	public:
		bool Enter()
		{
			std::unique_lock Lock(m_Mutex);
			m_Entered = true;
			m_Condition.notify_all();
			return m_Condition.wait_for(Lock, std::chrono::seconds(5), [this] { return m_Released; });
		}
		bool WaitUntilEntered()
		{
			std::unique_lock Lock(m_Mutex);
			return m_Condition.wait_for(Lock, std::chrono::seconds(5), [this] { return m_Entered; });
		}
		void Release()
		{
			std::lock_guard Lock(m_Mutex);
			m_Released = true;
			m_Condition.notify_all();
		}
	};
	thread_local CPrepareBarrier *s_pPrepareBarrier = nullptr;

	// 预处理边界调用真实精灵提取，HasData 必须对应预备纹理而非只有解码像素。
	bool PrepareTestSkin(const char *, SQmSkinSourceData &Data)
	{
		if(Data.m_Info.m_Width != 64 || Data.m_Info.m_Height != 32)
			return false;
		Data.m_SourceWidth = Data.m_Info.m_Width;
		Data.m_SourceHeight = Data.m_Info.m_Height;
		Data.m_pPreparedTextures = QmPrepareSkinTextures(Data.m_Info, Data.m_Info, g_pData->m_aSprites);
		return s_pPrepareBarrier == nullptr || s_pPrepareBarrier->Enter();
	}

	class CTestSkinJob : public CQmSkinDownloadJob
	{
		CPrepareBarrier *m_pBarrier;

	public:
		CTestSkinJob(IStorage *pStorage, std::shared_ptr<IHttpRequest> pResponse = nullptr, CPrepareBarrier *pBarrier = nullptr, TPrepare Prepare = PrepareTestSkin) :
			CQmSkinDownloadJob(pStorage, "test skin", Prepare, std::move(pResponse)),
			m_pBarrier(pBarrier) {}
		void Run() override
		{
			s_pPrepareBarrier = m_pBarrier;
			CQmSkinDownloadJob::Run();
			s_pPrepareBarrier = nullptr;
		}
	};
}

class SkinSourceJob : public ::testing::Test
{
protected:
	using EResult = CQmSkinDownloadSession::EResult;
	CTestInfo m_Info;
	std::unique_ptr<IStorage> m_pStorage;
	CTestSkinHttp m_Http;
	CPrepareBarrier m_Barrier;
	CJobPool m_Pool;
	bool m_PoolRunning = false;
	std::shared_ptr<CTestSkinJob> m_pDecodedJob;

	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		ASSERT_TRUE(m_pStorage->CreateFolder("downloadedskins", IStorage::TYPE_SAVE));
		m_Pool.Init(1);
		m_PoolRunning = true;
	}
	void StopPool()
	{
		m_Barrier.Release();
		if(m_PoolRunning)
		{
			m_Pool.Shutdown();
			m_PoolRunning = false;
		}
	}
	void TearDown() override { StopPool(); }

	std::shared_ptr<CTestSkinRequest> Response(const std::vector<uint8_t> &vBody, int Status = 200)
	{
		auto pRequest = std::make_shared<CTestSkinRequest>(m_pStorage.get());
		pRequest->Finish(vBody, Status);
		return pRequest;
	}
	std::unique_ptr<CQmSkinDownloadSession> Session(CPrepareBarrier *pBarrier = nullptr)
	{
		return std::make_unique<CQmSkinDownloadSession>(m_Http, "https://skins.test/test%20skin.png", "", std::make_shared<CTestSkinJob>(m_pStorage.get()), [this](const char *, bool) { return std::make_shared<CTestSkinRequest>(m_pStorage.get()); }, [this, pBarrier](std::shared_ptr<IHttpRequest> pResponse) {
				m_pDecodedJob = std::make_shared<CTestSkinJob>(m_pStorage.get(), std::move(pResponse), pBarrier);
				return m_pDecodedJob; }, [this](std::shared_ptr<IJob> pJob) { m_Pool.Add(std::move(pJob)); });
	}
	bool PollUntil(CQmSkinDownloadSession &Session, EResult Expected)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do
		{
			if(Session.Poll() == Expected)
				return true;
			std::this_thread::yield();
		} while(std::chrono::steady_clock::now() < Deadline);
		return false;
	}
	bool WaitForRequest(CQmSkinDownloadSession &Session)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while(m_Http.m_vRequests.empty() && std::chrono::steady_clock::now() < Deadline)
		{
			Session.Poll();
			std::this_thread::yield();
		}
		return !m_Http.m_vRequests.empty();
	}

	std::vector<uint8_t> Png(uint8_t Red = 23, size_t Width = 64)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = 32;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(calloc(Image.DataSize(), 1));
		Image.m_pData[0] = Red;
		Image.m_pData[3] = 255;
		CByteBufferWriter Writer;
		EXPECT_TRUE(CImageLoader::SavePng(Writer, Image));
		Image.Free();
		return {Writer.Data(), Writer.Data() + Writer.Size()};
	}

	void WriteCache(const std::vector<uint8_t> &vData)
	{
		IOHANDLE File = m_pStorage->OpenFile("downloadedskins/test skin.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		io_write(File, vData.data(), vData.size());
		io_close(File);
	}

	uint8_t CacheRed()
	{
		void *pData = nullptr;
		unsigned Size = 0;
		EXPECT_TRUE(m_pStorage->ReadFile("downloadedskins/test skin.png", IStorage::TYPE_SAVE, &pData, &Size));
		CImageInfo Image;
		EXPECT_TRUE(CImageLoader::LoadPng(pData, Size, "test cache", Image));
		free(pData);
		const uint8_t Red = Image.m_pData != nullptr ? Image.m_pData[0] : 0;
		Image.Free();
		return Red;
	}
};

TEST_F(SkinSourceJob, ValidCachePublishesPreparedTextures)
{
	WriteCache(Png());
	CTestSkinJob Job(m_pStorage.get());
	Job.Run();
	ASSERT_TRUE(Job.HasData());
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 23);
	EXPECT_TRUE(Job.m_Data.m_pPreparedTextures->Available(0, 0));
	EXPECT_EQ(Job.m_Data.m_SourceWidth, 64u);
	EXPECT_EQ(Job.m_Data.m_SourceHeight, 32u);
}

TEST_F(SkinSourceJob, MissingCacheProducesNoData)
{
	CTestSkinJob Job(m_pStorage.get());
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
}

TEST_F(SkinSourceJob, CorruptCacheProducesNoData)
{
	WriteCache({'b', 'a', 'd'});
	CTestSkinJob Job(m_pStorage.get());
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
}

TEST_F(SkinSourceJob, CacheRejectedByPreparationProducesNoData)
{
	WriteCache(Png(23, 32));
	CTestSkinJob Job(m_pStorage.get());
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
}

TEST_F(SkinSourceJob, LoadedCacheCarriesFileTimestampUntilUpload)
{
	WriteCache(Png(23));
	time_t Created = 0, Modified = 0;
	ASSERT_TRUE(m_pStorage->RetrieveTimes("downloadedskins/test skin.png", IStorage::TYPE_SAVE, &Created, &Modified));
	CTestSkinJob Job(m_pStorage.get());
	Job.Run();
	WriteCache(Png(45));
	ASSERT_TRUE(Job.HasData());
	ASSERT_TRUE(Job.m_Data.m_LastModified.has_value());
	EXPECT_EQ(Job.m_Data.m_LastModified.value(), Modified);
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 23);
	EXPECT_EQ(CacheRed(), 45);
}

TEST_F(SkinSourceJob, SourceTimestampSupportsAllStoragePathsAndClearsMissingFiles)
{
	WriteCache(Png());
	time_t Created = 0, Modified = 0;
	ASSERT_TRUE(m_pStorage->RetrieveTimes("downloadedskins/test skin.png", IStorage::TYPE_SAVE, &Created, &Modified));
	SQmSkinSourceData Data;
	Data.ReadLastModified(m_pStorage.get(), "downloadedskins/test skin.png", IStorage::TYPE_ALL);
	ASSERT_TRUE(Data.m_LastModified.has_value());
	EXPECT_EQ(Data.m_LastModified.value(), Modified);
	Data.ReadLastModified(m_pStorage.get(), "downloadedskins/missing.png", IStorage::TYPE_ALL);
	EXPECT_FALSE(Data.m_LastModified.has_value());
}

TEST_F(SkinSourceJob, CompletedResponseDecodesWithoutReplacingCache)
{
	WriteCache(Png(23));
	auto pResponse = Response(Png(45));
	CTestSkinJob Job(m_pStorage.get(), pResponse);
	Job.Run();
	ASSERT_TRUE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 45);
	EXPECT_EQ(pResponse->State(), EHttpState::DONE);
	EXPECT_FALSE(Job.m_Data.m_LastModified.has_value());
	EXPECT_EQ(CacheRed(), 23);
	pResponse->OnValidation(false);
}

TEST_F(SkinSourceJob, ResponseBufferRemainsAliveAfterCallerReleasesRequest)
{
	auto pResponse = Response(Png(67));
	std::weak_ptr<IHttpRequest> WeakResponse = pResponse;
	{
		CTestSkinJob Job(m_pStorage.get(), pResponse);
		pResponse->OnValidation(false);
		pResponse.reset();
		EXPECT_FALSE(WeakResponse.expired());
		Job.Run();
		ASSERT_TRUE(Job.HasData());
		EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 67);
	}
	EXPECT_TRUE(WeakResponse.expired());
}

TEST_F(SkinSourceJob, RunningResponseDoesNotWaitOrReadCache)
{
	WriteCache(Png());
	auto pResponse = std::make_shared<CTestSkinRequest>(m_pStorage.get());
	CTestSkinJob Job(m_pStorage.get(), pResponse);
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(pResponse->State(), EHttpState::RUNNING);
	EXPECT_FALSE(pResponse->IsAbortRequested());
}

TEST_F(SkinSourceJob, NonSuccessfulResponseDoesNotDecodeOrReadCache)
{
	WriteCache(Png());
	auto pResponse = Response(Png(45), 404);
	CTestSkinJob Job(m_pStorage.get(), pResponse);
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
	pResponse->OnValidation(false);
}

TEST_F(SkinSourceJob, EmptyResponseProducesNoData)
{
	auto pResponse = Response({});
	CTestSkinJob Job(m_pStorage.get(), pResponse);
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	pResponse->OnValidation(false);
}

TEST_F(SkinSourceJob, CancelledCacheJobDoesNotReadPixels)
{
	WriteCache(Png());
	CTestSkinJob Job(m_pStorage.get());
	ASSERT_TRUE(Job.Abort());
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
}

TEST_F(SkinSourceJob, CancelledResponseJobLeavesValidationToSession)
{
	WriteCache(Png());
	auto pResponse = Response(Png(45));
	CTestSkinJob Job(m_pStorage.get(), pResponse);
	ASSERT_TRUE(Job.Abort());
	Job.Run();
	EXPECT_FALSE(Job.HasData());
	EXPECT_EQ(pResponse->State(), EHttpState::DONE);
	EXPECT_FALSE(pResponse->IsAbortRequested());
	EXPECT_EQ(CacheRed(), 23);
	pResponse->OnValidation(false);
}

TEST_F(SkinSourceJob, SessionDownloadsWhenRealCacheJobHasNoData)
{
	auto pSession = Session();
	ASSERT_TRUE(WaitForRequest(*pSession));
	m_Http.m_vRequests[0]->Finish(Png(45));
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	auto pReady = std::static_pointer_cast<CQmSkinSourceJob>(pSession->TakeReadyJob());
	ASSERT_NE(pReady, nullptr);
	ASSERT_TRUE(pReady->HasData());
	EXPECT_EQ(pReady->m_Data.m_Info.m_pData[0], 45);
	EXPECT_EQ(CacheRed(), 45);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
}

TEST_F(SkinSourceJob, SessionReplacesCacheOnlyAfterRealResponsePreparation)
{
	WriteCache(Png(23));
	auto pSession = Session(&m_Barrier);
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	auto pCache = std::static_pointer_cast<CQmSkinSourceJob>(pSession->TakeReadyJob());
	ASSERT_NE(pCache, nullptr);
	EXPECT_EQ(pCache->m_Data.m_Info.m_pData[0], 23);
	m_Http.m_vRequests[0]->Finish(Png(111));
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_TRUE(m_Barrier.WaitUntilEntered());
	EXPECT_EQ(CacheRed(), 23);
	m_Barrier.Release();
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	auto pReady = std::static_pointer_cast<CQmSkinSourceJob>(pSession->TakeReadyJob());
	ASSERT_NE(pReady, nullptr);
	EXPECT_EQ(pReady->m_Data.m_Info.m_pData[0], 111);
	EXPECT_EQ(CacheRed(), 111);
	EXPECT_EQ(pCache->m_Data.m_Info.m_pData[0], 23);
}

TEST_F(SkinSourceJob, InvalidResponsePreservesPublishedCache)
{
	WriteCache(Png(23));
	auto pSession = Session();
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	auto pCache = pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish({'b', 'a', 'd'});
	ASSERT_TRUE(PollUntil(*pSession, EResult::DONE));
	EXPECT_TRUE(pCache->HasData());
	EXPECT_FALSE(m_pDecodedJob->HasData());
	EXPECT_EQ(m_pDecodedJob->m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
	EXPECT_EQ(m_Http.m_vRequests[0]->State(), EHttpState::ERROR);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
}

TEST_F(SkinSourceJob, PreparationFailureDoesNotPromoteDownloadedImage)
{
	WriteCache(Png(23));
	auto pSession = Session();
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(Png(45, 32));
	ASSERT_TRUE(PollUntil(*pSession, EResult::DONE));
	EXPECT_FALSE(m_pDecodedJob->HasData());
	EXPECT_EQ(m_pDecodedJob->m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
}

TEST_F(SkinSourceJob, CancelDuringPreparationRejectsResponseAndNeverDeliversPixels)
{
	WriteCache(Png(23));
	auto pSession = Session(&m_Barrier);
	ASSERT_TRUE(PollUntil(*pSession, EResult::READY));
	auto pCache = pSession->TakeReadyJob();
	m_Http.m_vRequests[0]->Finish(Png(45));
	EXPECT_EQ(pSession->Poll(), EResult::WAITING);
	ASSERT_TRUE(m_Barrier.WaitUntilEntered());
	pSession->Cancel();
	// Cancel 调用真实 OnValidation(false)，请求状态变化时 CPU job 仍停在 prepare 内。
	EXPECT_EQ(m_Http.m_vRequests[0]->State(), EHttpState::ERROR);
	EXPECT_EQ(m_pDecodedJob->State(), IJob::STATE_ABORTED);
	StopPool();
	EXPECT_FALSE(m_pDecodedJob->HasData());
	EXPECT_EQ(m_pDecodedJob->m_Data.m_Info.m_pData, nullptr);
	EXPECT_TRUE(pCache->HasData());
	EXPECT_EQ(CacheRed(), 23);
	EXPECT_EQ(pSession->Poll(), EResult::DONE);
	EXPECT_EQ(pSession->TakeReadyJob(), nullptr);
	pSession->Cancel();
}

TEST_F(SkinSourceJob, DecodedPixelsWithoutPreparedTexturesAreNotReady)
{
	WriteCache(Png());
	CTestSkinJob Job(m_pStorage.get(), nullptr, nullptr,
		[](const char *, SQmSkinSourceData &) { return true; });
	Job.Run();
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_FALSE(Job.HasData());
}

TEST_F(SkinSourceJob, CancelDuringCachePreparationDoesNotPublishPixels)
{
	WriteCache(Png());
	auto pJob = std::make_shared<CTestSkinJob>(m_pStorage.get(), nullptr, &m_Barrier);
	m_Pool.Add(pJob);
	ASSERT_TRUE(m_Barrier.WaitUntilEntered());
	EXPECT_TRUE(pJob->Abort());
	StopPool();
	EXPECT_FALSE(pJob->HasData());
	EXPECT_EQ(pJob->m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}
