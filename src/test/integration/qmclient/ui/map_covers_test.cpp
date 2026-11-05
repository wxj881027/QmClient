#include <base/mem.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <game/client/components/qmclient/map_covers.h>

#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <utility>

namespace
{
	constexpr const char *COVER_URL = "https://ddnet.org/ranks/maps/Alpha.png";

	class CResponse : public IHttpRequest
	{
	public:
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_Aborted = true; }
		void Fail() { m_State = EHttpState::ERROR; }
		void Finish(const CByteBufferWriter &Png)
		{
			free(m_pBuffer);
			m_ResponseLength = m_BufferSize = Png.Size();
			m_pBuffer = static_cast<unsigned char *>(malloc(Png.Size()));
			ASSERT_NE(m_pBuffer, nullptr);
			mem_copy(m_pBuffer, Png.Data(), Png.Size());
			const IOHANDLE File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Png.Data(), Png.Size()), Png.Size());
			io_close(File);
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CResponse>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class CDeferredEngine : public IEngine
	{
	public:
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { m_vJobs.push_back(std::move(pJob)); }
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void CompleteJobs()
		{
			CJobPool Pool;
			Pool.Init(1);
			for(const auto &pJob : m_vJobs)
				Pool.Add(pJob);
			for(const auto &pJob : m_vJobs)
				EXPECT_TRUE(WaitForJobDone(*pJob));
			Pool.Shutdown();
			m_vJobs.clear();
		}
	};

	class CGraphicsRecorder : public CIconBenchmarkGraphics
	{
	public:
		int m_Unloads = 0;
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			EXPECT_TRUE(pHandle->IsValid());
			++m_Unloads;
			pHandle->Invalidate();
		}
	};

	class MapCovers : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		CGraphicsRecorder m_Graphics;
		CQmMapCovers m_Covers{CreateResponse};
		CByteBufferWriter m_Png;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			unsigned char aPixel[] = {80, 120, 160, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel;
			ASSERT_TRUE(CImageLoader::SavePng(m_Png, Image));
		}
		void TearDown() override { m_Covers.Shutdown(&m_Graphics); }
		void Update(float Now = 1.0f) { m_Covers.Update(&m_Http, &m_Engine, &m_Graphics, m_pStorage.get(), Now); }
		bool Get(CQmMapCovers::SImage &Image) { return m_Covers.Get(COVER_URL, CQmMapCovers::ERole::ROW, &Image); }
		void DownloadCover()
		{
			CQmMapCovers::SImage Image;
			EXPECT_FALSE(Get(Image));
			Update();
			ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
			ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.front()->Finish(m_Png));
			Update();
			m_Engine.CompleteJobs();
			Update();
			ASSERT_TRUE(Get(Image));
		}
	};
}

TEST_F(MapCovers, PendingCoverCompletesWithoutFurtherVisibleRequests)
{
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Get(Image));
	Update();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	// 页面关闭后不再 Get，只由菜单逐帧完成已经排队的下载与合成。
	Update();
	EXPECT_FALSE(m_Http.m_vRequests.front()->m_Aborted);
	ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.front()->Finish(m_Png));
	Update();
	m_Engine.CompleteJobs();
	Update();
	EXPECT_FALSE(m_Covers.Loading());
	ASSERT_TRUE(Get(Image));
	const int TextureId = Image.m_Texture.Id();
	for(int Reopen = 0; Reopen < 3; ++Reopen)
	{
		Update();
		ASSERT_TRUE(Get(Image));
		EXPECT_EQ(Image.m_Texture.Id(), TextureId);
	}
	EXPECT_EQ(m_Graphics.m_Uploads, 1u);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(MapCovers, ShutdownReleasesTextureAndNewCacheRestoresFromDisk)
{
	ASSERT_NO_FATAL_FAILURE(DownloadCover());
	m_Covers.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	m_Covers.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	CQmMapCovers Restored(CreateResponse);
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Restored.Get(COVER_URL, CQmMapCovers::ERole::ROW, &Image));
	Restored.Update(&m_Http, &m_Engine, &m_Graphics, m_pStorage.get(), 2.0f);
	m_Engine.CompleteJobs();
	Restored.Update(&m_Http, &m_Engine, &m_Graphics, m_pStorage.get(), 2.0f);
	EXPECT_TRUE(Restored.Get(COVER_URL, CQmMapCovers::ERole::ROW, &Image));
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	Restored.Shutdown(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_Unloads, 2);
}

TEST_F(MapCovers, ShutdownCancelsPendingDownload)
{
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Get(Image));
	Update();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Covers.Shutdown(&m_Graphics);
	EXPECT_TRUE(m_Http.m_vRequests.front()->m_Aborted);
	EXPECT_FALSE(m_Covers.Loading());
	EXPECT_EQ(m_Graphics.m_Uploads, 0u);
}

TEST_F(MapCovers, IdleUpdatesAllowFailedCoverToRetryAfterCooldown)
{
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Get(Image));
	Update();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests.front()->Fail();
	Update();
	EXPECT_FALSE(m_Covers.Loading());
	Update(60.0f);
	EXPECT_FALSE(Get(Image));
	EXPECT_FALSE(m_Covers.Loading());
	Update(122.0f);
	EXPECT_FALSE(Get(Image));
	EXPECT_TRUE(m_Covers.Loading());
	Update(122.0f);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}
