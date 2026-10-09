#include <engine/gfx/image_loader.h>
#include <engine/http.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/skin_source_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <vector>

namespace
{
	// 只替换 HTTP 传输；PNG 解码、缓存读取和校验后替换使用生产实现。
	class CTestSkinRequest : public IHttpRequest
	{
	public:
		explicit CTestSkinRequest(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		bool SkipByFileTimeEnabled() const { return m_SkipByFileTime; }
		void Finish(EHttpState State, int Status, const std::vector<uint8_t> &vBody)
		{
			m_StatusCode = Status;
			m_ResponseLength = vBody.size();
			m_pBuffer = static_cast<unsigned char *>(malloc(vBody.size() + 1));
			mem_copy(m_pBuffer, vBody.data(), vBody.size());
			m_BufferSize = vBody.size() + 1;
			if(Status == 304)
				m_IfModifiedSince = 0;
			else if(State == EHttpState::DONE)
			{
				IOHANDLE File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
				if(File == nullptr)
				{
					ADD_FAILURE() << "Unable to write HTTP response";
					m_State = EHttpState::ERROR;
					return;
				}
				io_write(File, vBody.data(), vBody.size());
				io_close(File);
			}
			m_State = State;
		}
	};

	std::unique_ptr<IHttpRequest> CreateTestSkinRequest(const char *pUrl)
	{
		return std::make_unique<CTestSkinRequest>(pUrl);
	}

	class CTestSkinHttp : public IHttp
	{
	public:
		struct SResponse
		{
			EHttpState m_State = EHttpState::DONE;
			int m_Status = 200;
			std::vector<uint8_t> m_vBody;
		};
		std::vector<SResponse> m_vResponses;
		std::vector<std::shared_ptr<CTestSkinRequest>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			auto pTestRequest = std::static_pointer_cast<CTestSkinRequest>(pRequest);
			const size_t Index = m_vRequests.size();
			m_vRequests.push_back(pTestRequest);
			// 即使旧实现意外发起网络请求，测试也会立即失败并结束，不依赖超时等待。
			if(Index >= m_vResponses.size())
			{
				ADD_FAILURE() << "Unexpected HTTP request";
				pTestRequest->Finish(EHttpState::ERROR, 0, {});
				return;
			}
			const auto &Response = m_vResponses[Index];
			pTestRequest->Finish(Response.m_State, Response.m_Status, Response.m_vBody);
		}
		bool HasIpresolveBug() const override { return false; }
	};

	// 素材预处理是下载任务的调用边界；测试保留解码像素供断言。
	bool PrepareTestSkin(const char *, SQmSkinSourceData &Data)
	{
		return Data.m_Info.m_Width == 64 && Data.m_Info.m_Height == 32;
	}

	class CTestSkinJob : public CQmSkinDownloadJob
	{
	public:
		CTestSkinJob(IStorage *pStorage, IHttp *pHttp, bool UseCache = true) :
			CQmSkinDownloadJob(pStorage, pHttp, "test skin", "https://skins.test/", PrepareTestSkin, UseCache, CreateTestSkinRequest) {}
		using CQmSkinDownloadJob::Run;
	};
}

class SkinSourceJob : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	std::unique_ptr<IStorage> m_pStorage;
	CTestSkinHttp m_Http;

	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		ASSERT_TRUE(m_pStorage->CreateFolder("downloadedskins", IStorage::TYPE_SAVE));
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

TEST_F(SkinSourceJob, ValidCachePublishesPixelsWithoutSubmittingHttp)
{
	WriteCache(Png());
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	ASSERT_TRUE(Job.m_UsedCachedSkin);
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 23);
}

TEST_F(SkinSourceJob, MissingCacheDownloadsAndPublishesValidatedPng)
{
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(45)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_STREQ(m_Http.m_vRequests.front()->Url(), "https://skins.test/test%20skin.png");
	EXPECT_FALSE(Job.m_UsedCachedSkin);
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 45);
	EXPECT_EQ(CacheRed(), 45);
}

TEST_F(SkinSourceJob, CorruptCacheFallsBackToDownload)
{
	WriteCache({'b', 'a', 'd'});
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(67)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_FALSE(Job.m_UsedCachedSkin);
	EXPECT_EQ(CacheRed(), 67);
}

TEST_F(SkinSourceJob, CacheRejectedByPreparationFallsBackToDownload)
{
	WriteCache(Png(23, 32));
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(89)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	EXPECT_FALSE(Job.m_UsedCachedSkin);
	EXPECT_EQ(CacheRed(), 89);
}

TEST_F(SkinSourceJob, BackgroundUpdatePublishesNewPixelsAndReplacesDiskCache)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(111)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_TRUE(Job.DownloadReady());
	Job.Run();
	EXPECT_FALSE(Job.m_UsedCachedSkin);
	ASSERT_NE(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData[0], 111);
	EXPECT_EQ(CacheRed(), 111);
}

TEST_F(SkinSourceJob, PendingBackgroundDownloadDoesNotBlockCacheJob)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::RUNNING, 0, {}});
	CTestSkinJob UpdateJob(m_pStorage.get(), &m_Http, false);
	UpdateJob.StartUpdate();
	EXPECT_FALSE(UpdateJob.DownloadReady());
	CTestSkinJob CacheJob(m_pStorage.get(), &m_Http);
	CacheJob.Run();
	ASSERT_TRUE(CacheJob.m_UsedCachedSkin);
	ASSERT_NE(CacheJob.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheJob.m_Data.m_Info.m_pData[0], 23);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	UpdateJob.Abort();
}

TEST_F(SkinSourceJob, CancelledBackgroundDownloadAbortsTransportAndPreservesCache)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::RUNNING, 0, {}});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	ASSERT_TRUE(Job.Abort());
	EXPECT_TRUE(m_Http.m_vRequests[0]->IsAbortRequested());
	Job.Run();
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}

TEST_F(SkinSourceJob, LoadedCacheCarriesFileTimestampUntilUpload)
{
	WriteCache(Png(23));
	time_t Created = 0, Modified = 0;
	ASSERT_TRUE(m_pStorage->RetrieveTimes("downloadedskins/test skin.png", IStorage::TYPE_SAVE, &Created, &Modified));
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	WriteCache(Png(45));
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

TEST_F(SkinSourceJob, CancelledBackgroundJobDoesNotSubmitDownload)
{
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	ASSERT_TRUE(Job.Abort());
	Job.StartUpdate();
	EXPECT_FALSE(Job.DownloadReady());
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(SkinSourceJob, FailedBackgroundUpdatePreservesDiskCache)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::ERROR, 0, {}});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_TRUE(Job.DownloadReady());
	Job.Run();
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}

TEST_F(SkinSourceJob, InvalidBackgroundResponseDoesNotOverwriteCache)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, {'b', 'a', 'd'}});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_TRUE(Job.DownloadReady());
	Job.Run();
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}

TEST_F(SkinSourceJob, PreparationFailureDoesNotPromoteDownloadedImage)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(45, 32)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_TRUE(Job.DownloadReady());
	Job.Run();
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}

TEST_F(SkinSourceJob, NotModifiedBackgroundUpdateKeepsPublishedSource)
{
	WriteCache(Png(23));
	m_Http.m_vResponses.push_back({EHttpState::DONE, 304, {}});
	CTestSkinJob Job(m_pStorage.get(), &m_Http, false);
	Job.StartUpdate();
	ASSERT_TRUE(Job.DownloadReady());
	Job.Run();
	EXPECT_TRUE(Job.m_NotModified);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_EQ(CacheRed(), 23);
}

TEST_F(SkinSourceJob, MissingCacheRetriesNotModifiedResponseWithoutFileTime)
{
	m_Http.m_vResponses.push_back({EHttpState::DONE, 304, {}});
	m_Http.m_vResponses.push_back({EHttpState::DONE, 200, Png(133)});
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_TRUE(m_Http.m_vRequests[0]->SkipByFileTimeEnabled());
	EXPECT_FALSE(m_Http.m_vRequests[1]->SkipByFileTimeEnabled());
	EXPECT_EQ(CacheRed(), 133);
}

TEST_F(SkinSourceJob, NotFoundDownloadProducesNoPixels)
{
	m_Http.m_vResponses.push_back({EHttpState::DONE, 404, {}});
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	Job.Run();
	EXPECT_TRUE(Job.m_NotFound);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
}

TEST_F(SkinSourceJob, CancelledJobDoesNotReadCacheOrSubmitHttp)
{
	WriteCache(Png());
	CTestSkinJob Job(m_pStorage.get(), &m_Http);
	ASSERT_TRUE(Job.Abort());
	Job.Run();
	EXPECT_FALSE(Job.m_UsedCachedSkin);
	EXPECT_EQ(Job.m_Data.m_Info.m_pData, nullptr);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}
