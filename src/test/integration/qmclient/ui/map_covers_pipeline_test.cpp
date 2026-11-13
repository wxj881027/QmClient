#include <base/mem.h>
#include <base/str.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/map_covers.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	class CResponse final : public IHttpRequest
	{
	public:
		enum class EFileFailure
		{
			NONE,
			BAD_ALLOC,
			LENGTH_ERROR,
			UNKNOWN,
		};
		EFileFailure m_FileFailure = EFileFailure::NONE;
		int m_Renames = 0;
		int m_Removes = 0;
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_Aborted = true; }
		void Finish(const CByteBufferWriter &Png, bool ValidHeader = true)
		{
			free(m_pBuffer);
			m_ResponseLength = m_BufferSize = Png.Size();
			m_pBuffer = static_cast<unsigned char *>(malloc(Png.Size()));
			ASSERT_NE(m_pBuffer, nullptr);
			mem_copy(m_pBuffer, Png.Data(), Png.Size());
			if(!ValidHeader)
				m_pBuffer[0] = 0;
			const IOHANDLE File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, m_pBuffer, Png.Size()), Png.Size());
			EXPECT_EQ(io_close(File), 0);
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}

	protected:
		int RenameResponseFile(bool AllowDestinationRemoval) override
		{
			++m_Renames;
			switch(m_FileFailure)
			{
			case EFileFailure::BAD_ALLOC: throw std::bad_alloc();
			case EFileFailure::LENGTH_ERROR: throw std::length_error("map cover validation file");
			case EFileFailure::UNKNOWN: throw std::logic_error("map cover validation file");
			case EFileFailure::NONE: break;
			}
			// 准备矩阵只注入 Covers 主线程候选；一次性验证的真实文件操作单独验证。
			CPredictionAllocationFailure FileBoundary(std::numeric_limits<size_t>::max());
			return IHttpRequest::RenameResponseFile(AllowDestinationRemoval);
		}
		int RemoveResponseFile() override
		{
			++m_Removes;
			CPredictionAllocationFailure FileBoundary(std::numeric_limits<size_t>::max());
			return IHttpRequest::RemoveResponseFile();
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder final : public IHttp
	{
	public:
		// 记录边界自身不在 native-new 故障域内申请容器节点。
		std::array<std::shared_ptr<CResponse>, 256> m_apRequests{};
		size_t m_Count = 0;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(m_Count >= m_apRequests.size())
				throw std::logic_error("map cover test request capacity");
			m_apRequests[m_Count++] = std::static_pointer_cast<CResponse>(std::move(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
		void Reset()
		{
			for(auto &pRequest : m_apRequests)
				pRequest.reset();
			m_Count = 0;
		}
	};

	class CDeferredEngine final : public IEngine
	{
		CJobPool m_ClosedPool;

	public:
		std::array<std::shared_ptr<IJob>, 6> m_apJobs{};
		std::shared_ptr<IJob> m_pLastRejected;
		size_t m_Count = 0;
		int m_Accepted = 0;
		int m_Attempts = 0;
		bool m_Reject = false;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			if(m_Reject)
			{
				m_pLastRejected = pJob;
				return m_ClosedPool.TryAdd(std::move(pJob));
			}
			if(m_Count >= m_apJobs.size())
				throw std::logic_error("map cover test job capacity");
			m_apJobs[m_Count++] = std::move(pJob);
			++m_Accepted;
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void CompleteJobs()
		{
			if(m_Count == 0)
				return;
			CJobPool Pool;
			Pool.Init(1);
			for(size_t i = 0; i < m_Count; ++i)
				Pool.Add(m_apJobs[i]);
			for(size_t i = 0; i < m_Count; ++i)
				EXPECT_TRUE(WaitForJobDone(*m_apJobs[i]));
			Pool.Shutdown();
			for(auto &pJob : m_apJobs)
				pJob.reset();
			m_Count = 0;
		}
		void Reset()
		{
			for(auto &pJob : m_apJobs)
				pJob.reset();
			m_pLastRejected.reset();
			m_Count = m_Accepted = m_Attempts = 0;
			m_Reject = false;
		}
	};

	class CUploadRecorder final : public CIconBenchmarkGraphics
	{
	public:
		enum class EMode
		{
			ACCEPT,
			REFUSE,
			BAD_ALLOC,
			LENGTH_ERROR,
			UNKNOWN,
			CONSUME_INVALID,
		};
		EMode m_Mode = EMode::ACCEPT;
		EMode m_UnloadMode = EMode::ACCEPT;
		int m_Attempts = 0;
		int m_LegacyUploads = 0;
		int m_Unloads = 0;
		bool m_RefuseUnload = false;
		const uint8_t *m_pRejectedPixels = nullptr;
		const uint8_t *m_pAcceptedPixels = nullptr;
		CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pTexName) override
		{
			++m_LegacyUploads;
			return CIconBenchmarkGraphics::LoadTextureRawMove(Image, Flags, pTexName);
		}
		bool TryLoadTextureRawMoveOwned(CImageInfo &Image, int Flags, const char *pTexName, SGraphicsUploadRelease &Release, CTextureHandle &Texture) override
		{
			++m_Attempts;
			if(m_Mode != EMode::ACCEPT)
			{
				m_pRejectedPixels = Image.m_pData;
				switch(m_Mode)
				{
				case EMode::REFUSE: return false;
				case EMode::BAD_ALLOC: throw std::bad_alloc();
				case EMode::LENGTH_ERROR: throw std::length_error("map cover upload preparation");
				case EMode::UNKNOWN: throw std::logic_error("map cover upload preparation");
				case EMode::CONSUME_INVALID:
					Image.Free();
					Texture.Invalidate();
					return true;
				case EMode::ACCEPT: break;
				}
			}
			m_pAcceptedPixels = Image.m_pData;
			Texture = CIconBenchmarkGraphics::LoadTextureRawMove(Image, Flags, pTexName);
			const auto Owned = std::exchange(Release, {});
			if(Owned.m_pfnRelease != nullptr)
				Owned.m_pfnRelease(Owned.m_pContext);
			return true;
		}
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			if(m_RefuseUnload || m_UnloadMode == EMode::BAD_ALLOC)
				throw std::bad_alloc();
			if(m_UnloadMode == EMode::LENGTH_ERROR)
				throw std::length_error("map cover unload preparation");
			if(m_UnloadMode == EMode::UNKNOWN)
				throw std::logic_error("map cover unload preparation");
			++m_Unloads;
			pHandle->Invalidate();
		}
		void Reset()
		{
			ResetCounters();
			m_NextId = 1;
			m_Mode = EMode::ACCEPT;
			m_UnloadMode = EMode::ACCEPT;
			m_Attempts = m_LegacyUploads = m_Unloads = 0;
			m_RefuseUnload = false;
			m_pRejectedPixels = m_pAcceptedPixels = nullptr;
		}
	};

	struct SUpdateObservation
	{
		size_t m_Allocations;
		bool m_Triggered;
		bool m_Escaped;
	};

	class MapCoversPipeline : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CByteBufferWriter m_Png;
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		CUploadRecorder m_Graphics;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			// 计数与各故障预算从相同目录状态开始，建目录不进入 native-new 故障域。
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE));
			unsigned char aPixel[] = {80, 120, 160, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel;
			ASSERT_TRUE(CImageLoader::SavePng(m_Png, Image));
		}
		std::string Url(const char *pCase, size_t Index)
		{
			char aUrl[160];
			str_format(aUrl, sizeof(aUrl), "https://ddnet.org/ranks/maps/pipeline_%s_%u.png", pCase, static_cast<unsigned>(Index));
			return aUrl;
		}
		void Update(CQmMapCovers &Covers, float Now = 1.0f)
		{
			Covers.Update(&m_Http, &m_Engine, &m_Graphics, m_pStorage.get(), Now);
		}
		SUpdateObservation ObserveUpdate(CQmMapCovers &Covers, size_t Budget = std::numeric_limits<size_t>::max())
		{
			CPredictionAllocationFailure Fault(Budget);
			bool Escaped = false;
			try
			{
				Update(Covers);
			}
			catch(...)
			{
				Escaped = true;
			}
			return {Fault.Attempts(), Fault.Triggered(), Escaped};
		}
		bool Get(CQmMapCovers &Covers, const std::string &Url, CQmMapCovers::SImage &Image)
		{
			return Covers.Get(Url.c_str(), CQmMapCovers::ERole::ROW, &Image);
		}
		std::string CachePath(const std::string &Url)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			if(!QmMapCover::ThumbnailCacheFileName(Url.c_str(), aPath, sizeof(aPath)))
				return {};
			return aPath;
		}
		bool CacheExists(const std::string &Url)
		{
			return m_pStorage->FileExists(CachePath(Url).c_str(), IStorage::TYPE_SAVE);
		}
		void WriteCache(const std::string &Url)
		{
			m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			m_pStorage->CreateFolder(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE);
			const IOHANDLE File = m_pStorage->OpenFile(CachePath(Url).c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, m_Png.Data(), m_Png.Size()), m_Png.Size());
			EXPECT_EQ(io_close(File), 0);
		}
		void Download(CQmMapCovers &Covers, const std::string &Url)
		{
			CQmMapCovers::SImage Image;
			EXPECT_FALSE(Get(Covers, Url, Image));
			const size_t Before = m_Http.m_Count;
			Update(Covers);
			ASSERT_EQ(m_Http.m_Count, Before + 1);
			EXPECT_STREQ(m_Http.m_apRequests[Before]->Url(), Url.c_str());
			ASSERT_NO_FATAL_FAILURE(m_Http.m_apRequests[Before]->Finish(m_Png));
		}
		void ReadyJob(CQmMapCovers &Covers, const std::string &Url)
		{
			ASSERT_NO_FATAL_FAILURE(Download(Covers, Url));
			Update(Covers);
			ASSERT_EQ(m_Engine.m_Count, 1u);
			m_Engine.CompleteJobs();
		}
		void MakeReady(CQmMapCovers &Covers, const std::string &Url)
		{
			ASSERT_NO_FATAL_FAILURE(ReadyJob(Covers, Url));
			Update(Covers);
			CQmMapCovers::SImage Image;
			ASSERT_TRUE(Get(Covers, Url, Image));
		}
		void Reset(CQmMapCovers &Covers)
		{
			m_Graphics.m_RefuseUnload = false;
			m_Graphics.m_UnloadMode = CUploadRecorder::EMode::ACCEPT;
			Covers.Shutdown(&m_Graphics);
			m_Http.Reset();
			m_Engine.Reset();
			m_Graphics.Reset();
		}
	};
}

TEST_F(MapCoversPipeline, DownloadPreparationFailuresRetainTheTaskAndSubmitOnceOnRetry)
{
	size_t Allocations;
	{
		CQmMapCovers Covers(CreateResponse);
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Get(Covers, Url("download_count", 0), Image));
		const auto Result = ObserveUpdate(Covers);
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		Reset(Covers);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 32u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("download", Budget);
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		const auto Result = ObserveUpdate(Covers, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_TRUE(Covers.Loading());
		EXPECT_EQ(m_Http.m_Count, 0u);
		Update(Covers);
		ASSERT_EQ(m_Http.m_Count, 1u);
		EXPECT_STREQ(m_Http.m_apRequests[0]->Url(), Candidate.c_str());
		ASSERT_NO_FATAL_FAILURE(m_Http.m_apRequests[0]->Finish(m_Png));
		Update(Covers);
		m_Engine.CompleteJobs();
		Update(Covers);
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Http.m_Count, 1u);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, CachedReadPreparationFailuresRetainTheCacheAndRetryWithoutHttp)
{
	size_t Allocations;
	{
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("disk_count", 0);
		WriteCache(Candidate);
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		const auto Result = ObserveUpdate(Covers);
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		Reset(Covers);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 16u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("disk", Budget);
		WriteCache(Candidate);
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		const auto Result = ObserveUpdate(Covers, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_TRUE(Covers.Loading());
		EXPECT_TRUE(CacheExists(Candidate));
		EXPECT_EQ(m_Engine.m_Count, 0u);
		Update(Covers);
		ASSERT_EQ(m_Engine.m_Count, 1u);
		m_Engine.CompleteJobs();
		Update(Covers);
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Http.m_Count, 0u);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, CompletedResponsePreparationFailuresKeepTheUnvalidatedResultForRetry)
{
	size_t Allocations;
	{
		CQmMapCovers Covers(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(Download(Covers, Url("response_count", 0)));
		const auto Result = ObserveUpdate(Covers);
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		Reset(Covers);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 16u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("response", Budget);
		ASSERT_NO_FATAL_FAILURE(Download(Covers, Candidate));
		const auto Result = ObserveUpdate(Covers, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_EQ(m_Http.m_apRequests[0]->State(), EHttpState::DONE);
		EXPECT_FALSE(CacheExists(Candidate));
		EXPECT_EQ(m_Engine.m_Count, 0u);
		EXPECT_TRUE(Covers.Loading());
		Update(Covers);
		ASSERT_EQ(m_Engine.m_Count, 1u);
		EXPECT_TRUE(CacheExists(Candidate));
		m_Engine.CompleteJobs();
		Update(Covers);
		CQmMapCovers::SImage Image;
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Http.m_Count, 1u);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, ClosedPoolRetainsBothCachedAndDownloadedSourcesUntilFreshJobIsAccepted)
{
	for(const bool Cached : {false, true})
	{
		SCOPED_TRACE(Cached);
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("closed_pool", Cached);
		CQmMapCovers::SImage Image;
		if(Cached)
		{
			WriteCache(Candidate);
			EXPECT_FALSE(Get(Covers, Candidate, Image));
		}
		else
			ASSERT_NO_FATAL_FAILURE(Download(Covers, Candidate));
		m_Engine.m_Reject = true;
		Update(Covers);
		ASSERT_NE(m_Engine.m_pLastRejected, nullptr);
		const auto Rejected = m_Engine.m_pLastRejected;
		EXPECT_EQ(Rejected->State(), IJob::STATE_ABORTED);
		EXPECT_TRUE(Rejected->Finished());
		EXPECT_TRUE(Covers.Loading());
		EXPECT_TRUE(CacheExists(Candidate));
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		m_Engine.m_Reject = false;
		Update(Covers);
		ASSERT_EQ(m_Engine.m_Count, 1u);
		EXPECT_NE(m_Engine.m_apJobs[0], Rejected);
		m_Engine.CompleteJobs();
		Update(Covers);
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Http.m_Count, Cached ? 0u : 1u);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, ValidationRenameFailureCoolsTheUrlWithoutSubmittingThePreparedJob)
{
	CQmMapCovers Covers(CreateResponse);
	const auto Candidate = Url("rename_failure", 0);
	ASSERT_NO_FATAL_FAILURE(Download(Covers, Candidate));
	// 真实存储不能用下载文件替换目录，触发一次性验证的 ERROR 分支。
	m_pStorage->CreateFolder(CachePath(Candidate).c_str(), IStorage::TYPE_SAVE);
	ASSERT_TRUE(m_pStorage->FolderExists(CachePath(Candidate).c_str(), IStorage::TYPE_SAVE));
	Update(Covers);
	EXPECT_EQ(m_Http.m_apRequests[0]->State(), EHttpState::ERROR);
	EXPECT_EQ(m_Engine.m_Accepted, 0);
	EXPECT_FALSE(Covers.Loading());
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Get(Covers, Candidate, Image));
	EXPECT_FALSE(Covers.Loading());
	Update(Covers);
	EXPECT_EQ(m_Http.m_Count, 1u);
	Reset(Covers);
}

TEST_F(MapCoversPipeline, ValidationFileExceptionsConsumeTheDecisionAndUseTheExistingCooldown)
{
	using EFileFailure = CResponse::EFileFailure;
	for(const auto Failure : {EFileFailure::BAD_ALLOC, EFileFailure::LENGTH_ERROR, EFileFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("validation_exception", static_cast<size_t>(Failure));
		ASSERT_NO_FATAL_FAILURE(Download(Covers, Candidate));
		const auto pRequest = m_Http.m_apRequests[0];
		pRequest->m_FileFailure = Failure;
		EXPECT_NO_THROW(Update(Covers));
		EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
		EXPECT_EQ(pRequest->m_Renames, 1);
		EXPECT_EQ(pRequest->m_Removes, 1);
		EXPECT_EQ(m_Engine.m_Accepted, 0);
		EXPECT_FALSE(CacheExists(Candidate));
		EXPECT_FALSE(Covers.Loading());
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		Update(Covers);
		EXPECT_EQ(m_Http.m_Count, 1u);
		EXPECT_EQ(pRequest->m_Renames, 1);
		EXPECT_EQ(pRequest->m_Removes, 1);
		Update(Covers, 122.0f);
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		Update(Covers, 122.0f);
		EXPECT_EQ(m_Http.m_Count, 2u);
		EXPECT_EQ(pRequest->m_Renames, 1);
		EXPECT_EQ(pRequest->m_Removes, 1);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, UploadMetadataFailuresRetainTheReadyPixelsAndExistingTexture)
{
	size_t Allocations;
	{
		CQmMapCovers Covers(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, Url("old_count", 0)));
		ASSERT_NO_FATAL_FAILURE(ReadyJob(Covers, Url("upload_count", 0)));
		const auto Result = ObserveUpdate(Covers);
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		Reset(Covers);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 8u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapCovers Covers(CreateResponse);
		const auto Old = Url("old", Budget);
		const auto Candidate = Url("upload", Budget);
		ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, Old));
		CQmMapCovers::SImage OldImage;
		ASSERT_TRUE(Get(Covers, Old, OldImage));
		const int OldId = OldImage.m_Texture.Id();
		ASSERT_NO_FATAL_FAILURE(ReadyJob(Covers, Candidate));
		const auto Result = ObserveUpdate(Covers, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_TRUE(Covers.Loading());
		ASSERT_TRUE(Get(Covers, Old, OldImage));
		EXPECT_EQ(OldImage.m_Texture.Id(), OldId);
		EXPECT_EQ(m_Graphics.m_Uploads, 1u);
		EXPECT_EQ(m_Graphics.m_Unloads, 0);
		Update(Covers);
		CQmMapCovers::SImage Image;
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Graphics.m_Uploads, 2u);
		EXPECT_EQ(m_Engine.m_Accepted, 2);
		EXPECT_EQ(m_Graphics.m_LegacyUploads, 0);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, OwnedUploadRefusalAndPreparationExceptionsKeepTheSameReadyPixels)
{
	using EMode = CUploadRecorder::EMode;
	for(const auto Mode : {EMode::REFUSE, EMode::BAD_ALLOC, EMode::LENGTH_ERROR, EMode::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("owned", static_cast<size_t>(Mode));
		ASSERT_NO_FATAL_FAILURE(ReadyJob(Covers, Candidate));
		m_Graphics.m_Mode = Mode;
		if(Mode == EMode::UNKNOWN)
			EXPECT_THROW(Update(Covers), std::logic_error);
		else
			EXPECT_NO_THROW(Update(Covers));
		ASSERT_NE(m_Graphics.m_pRejectedPixels, nullptr);
		EXPECT_TRUE(Covers.Loading());
		EXPECT_TRUE(CacheExists(Candidate));
		EXPECT_EQ(m_Graphics.m_Uploads, 0u);
		m_Graphics.m_Mode = EMode::ACCEPT;
		Update(Covers);
		CQmMapCovers::SImage Image;
		EXPECT_TRUE(Get(Covers, Candidate, Image));
		EXPECT_EQ(m_Graphics.m_pAcceptedPixels, m_Graphics.m_pRejectedPixels);
		EXPECT_EQ(m_Engine.m_Accepted, 1);
		EXPECT_EQ(m_Http.m_Count, 1u);
		EXPECT_EQ(m_Graphics.m_Uploads, 1u);
		EXPECT_EQ(m_Graphics.m_LegacyUploads, 0);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, ConsumedUploadWithoutAHandleRecomposesFromTheRetainedPng)
{
	CQmMapCovers Covers(CreateResponse);
	const auto Candidate = Url("consumed", 0);
	ASSERT_NO_FATAL_FAILURE(ReadyJob(Covers, Candidate));
	m_Graphics.m_Mode = CUploadRecorder::EMode::CONSUME_INVALID;
	Update(Covers);
	EXPECT_TRUE(Covers.Loading());
	EXPECT_TRUE(CacheExists(Candidate));
	m_Graphics.m_Mode = CUploadRecorder::EMode::ACCEPT;
	Update(Covers);
	ASSERT_EQ(m_Engine.m_Count, 1u);
	m_Engine.CompleteJobs();
	Update(Covers);
	CQmMapCovers::SImage Image;
	EXPECT_TRUE(Get(Covers, Candidate, Image));
	EXPECT_EQ(m_Engine.m_Accepted, 2);
	EXPECT_EQ(m_Http.m_Count, 1u);
	EXPECT_EQ(m_Graphics.m_Uploads, 1u);
	Reset(Covers);
}

TEST_F(MapCoversPipeline, FailureCooldownPreparationRetainsTheResponseAndBothQueuedRoles)
{
	size_t Allocations;
	{
		CQmMapCovers Covers(CreateResponse);
		ASSERT_NO_FATAL_FAILURE(Download(Covers, Url("failure_count", 0)));
		ASSERT_NO_FATAL_FAILURE(m_Http.m_apRequests[0]->Finish(m_Png, false));
		const auto Result = ObserveUpdate(Covers);
		ASSERT_FALSE(Result.m_Escaped);
		Allocations = Result.m_Allocations;
		Reset(Covers);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 8u);
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapCovers Covers(CreateResponse);
		const auto Candidate = Url("failure", Budget);
		CQmMapCovers::SImage Image;
		EXPECT_FALSE(Covers.Get(Candidate.c_str(), CQmMapCovers::ERole::DETAILS, &Image));
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		Update(Covers);
		ASSERT_EQ(m_Http.m_Count, 1u);
		ASSERT_NO_FATAL_FAILURE(m_Http.m_apRequests[0]->Finish(m_Png, false));
		const auto Result = ObserveUpdate(Covers, Budget);
		EXPECT_TRUE(Result.m_Triggered);
		EXPECT_FALSE(Result.m_Escaped);
		EXPECT_EQ(m_Http.m_apRequests[0]->State(), EHttpState::DONE);
		EXPECT_TRUE(Covers.Loading());
		EXPECT_EQ(m_Engine.m_Accepted, 0);
		Update(Covers);
		EXPECT_FALSE(Covers.Loading());
		EXPECT_EQ(m_Http.m_apRequests[0]->State(), EHttpState::ERROR);
		EXPECT_FALSE(CacheExists(Candidate));
		Update(Covers, 120.0f);
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		EXPECT_FALSE(Covers.Loading());
		Update(Covers, 122.0f);
		EXPECT_FALSE(Get(Covers, Candidate, Image));
		EXPECT_TRUE(Covers.Loading());
		Update(Covers, 122.0f);
		EXPECT_EQ(m_Http.m_Count, 2u);
		Reset(Covers);
	}
}

TEST_F(MapCoversPipeline, FailedComposeKeepsTheCacheUntilCooldownPreparationSucceeds)
{
	CQmMapCovers Covers(CreateResponse);
	const auto Candidate = Url("decode_failure", 0);
	ASSERT_NO_FATAL_FAILURE(WriteCache(Candidate));
	const IOHANDLE File = m_pStorage->OpenFile(CachePath(Candidate).c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	// 只保留真实 PNG 的签名：主线程准入后由真实解码器拒绝截断数据。
	EXPECT_EQ(io_write(File, m_Png.Data(), 8), 8u);
	EXPECT_EQ(io_close(File), 0);
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Get(Covers, Candidate, Image));
	Update(Covers);
	ASSERT_EQ(m_Engine.m_Count, 1u);
	m_Engine.CompleteJobs();
	const auto Result = ObserveUpdate(Covers, 0);
	EXPECT_TRUE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Escaped);
	EXPECT_TRUE(CacheExists(Candidate));
	EXPECT_TRUE(Covers.Loading());
	Update(Covers);
	EXPECT_FALSE(CacheExists(Candidate));
	EXPECT_FALSE(Covers.Loading());
	EXPECT_FALSE(Get(Covers, Candidate, Image));
	EXPECT_FALSE(Covers.Loading());
	EXPECT_EQ(m_Http.m_Count, 0u);
	Reset(Covers);
}

TEST_F(MapCoversPipeline, ExistingDownloadJobAndUploadBudgetsRemainInForce)
{
	CQmMapCovers Covers(CreateResponse);
	CQmMapCovers::SImage Image;
	for(size_t i = 0; i < 3; ++i)
		EXPECT_FALSE(Get(Covers, Url("download_budget", i), Image));
	Update(Covers);
	EXPECT_EQ(m_Http.m_Count, 2u);
	Reset(Covers);
	for(size_t i = 0; i < 8; ++i)
	{
		const auto Candidate = Url("job_budget", i);
		ASSERT_NO_FATAL_FAILURE(WriteCache(Candidate));
		EXPECT_FALSE(Get(Covers, Candidate, Image));
	}
	Update(Covers);
	EXPECT_EQ(m_Engine.m_Count, 6u);
	EXPECT_EQ(m_Http.m_Count, 0u);
	m_Engine.CompleteJobs();
	Update(Covers);
	EXPECT_EQ(m_Graphics.m_Uploads, 4u);
	EXPECT_EQ(m_Engine.m_Count, 2u);
	m_Engine.CompleteJobs();
	Update(Covers);
	EXPECT_EQ(m_Graphics.m_Uploads, 8u);
	EXPECT_EQ(m_Engine.m_Accepted, 8);
	EXPECT_FALSE(Covers.Loading());
	Reset(Covers);
}

TEST_F(MapCoversPipeline, DeferredLruUnloadKeepsThePublishedHandleAndDoesNotRepeatTheUpload)
{
	CQmMapCovers Covers(CreateResponse);
	for(size_t i = 0; i < 192; ++i)
		ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, Url("lru", i)));
	EXPECT_EQ(m_Graphics.m_Uploads, 192u);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	m_Graphics.m_RefuseUnload = true;
	const auto Newest = Url("lru", 192);
	ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, Newest));
	EXPECT_TRUE(Covers.Loading());
	CQmMapCovers::SImage Image;
	EXPECT_TRUE(Get(Covers, Url("lru", 0), Image));
	for(int Frame = 0; Frame < 2; ++Frame)
		EXPECT_NO_THROW(Update(Covers));
	EXPECT_EQ(m_Graphics.m_Uploads, 193u);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	m_Graphics.m_RefuseUnload = false;
	Update(Covers);
	EXPECT_FALSE(Covers.Loading());
	EXPECT_EQ(m_Graphics.m_Uploads, 193u);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	EXPECT_TRUE(Get(Covers, Newest, Image));
	Reset(Covers);
}

TEST_F(MapCoversPipeline, ShutdownRetainsUnreleasedHandlesForFrameOrExplicitRetry)
{
	using EMode = CUploadRecorder::EMode;
	for(const auto Mode : {EMode::BAD_ALLOC, EMode::LENGTH_ERROR, EMode::UNKNOWN})
	{
		for(const bool RetryByFrame : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << "mode=" << static_cast<int>(Mode) << " frame=" << RetryByFrame);
			CQmMapCovers Covers(CreateResponse);
			const auto Candidate = Url("shutdown", static_cast<size_t>(Mode) * 2 + RetryByFrame);
			ASSERT_NO_FATAL_FAILURE(MakeReady(Covers, Candidate));
			m_Graphics.m_UnloadMode = Mode;
			if(Mode == EMode::UNKNOWN)
				EXPECT_THROW(Covers.Shutdown(&m_Graphics), std::logic_error);
			else
				EXPECT_NO_THROW(Covers.Shutdown(&m_Graphics));
			EXPECT_TRUE(Covers.Loading());
			EXPECT_EQ(m_Graphics.m_Unloads, 0);
			CQmMapCovers::SImage Image;
			EXPECT_FALSE(Get(Covers, Candidate, Image));
			CUploadRecorder NewGraphics;
			Covers.Update(&m_Http, &m_Engine, &NewGraphics, m_pStorage.get(), 2.0f);
			Covers.Shutdown(&NewGraphics);
			EXPECT_EQ(NewGraphics.m_Unloads, 0);
			EXPECT_EQ(m_Graphics.m_Unloads, 0);
			EXPECT_TRUE(Covers.Loading());
			m_Graphics.m_UnloadMode = EMode::ACCEPT;
			if(RetryByFrame)
				Update(Covers);
			else
				Covers.Shutdown(&m_Graphics);
			EXPECT_FALSE(Covers.Loading());
			EXPECT_EQ(m_Graphics.m_Unloads, 1);
			EXPECT_EQ(m_Graphics.m_Uploads, 1u);
			Covers.Shutdown(&m_Graphics);
			EXPECT_EQ(m_Graphics.m_Unloads, 1);
			Reset(Covers);
		}
	}
}
