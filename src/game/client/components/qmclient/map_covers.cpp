// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "map_covers.h"

#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
	bool PngHeaderMatches(const unsigned char *pData, size_t DataSize)
	{
		static constexpr unsigned char s_aPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		return pData != nullptr && DataSize >= sizeof(s_aPngSignature) && mem_comp(pData, s_aPngSignature, sizeof(s_aPngSignature)) == 0;
	}
} // namespace

// 工作线程只做解码、缩放与拖尾；主线程负责纹理上传。
class CQmMapCovers::CComposeJob : public IJob
{
public:
	std::vector<unsigned char> m_Data;
	int m_Width = 0;
	int m_Height = 0;
	QmMapCover::SArt m_Art;
	CImageInfo m_Image;
	bool m_Success = false;
	bool m_Retry = false;

	CComposeJob(std::vector<unsigned char> Data, int Width, int Height, const QmMapCover::SArt &Art) :
		m_Data(std::move(Data)), m_Width(Width), m_Height(Height), m_Art(Art)
	{
		Abortable(true);
	}

	~CComposeJob() override
	{
		m_Image.Free();
	}

	void Run() override
	{
		if(IsAbortable() && State() == STATE_ABORTED)
			return;
		try
		{
			const auto Result = QmMapCover::DecodeAndComposeChecked(m_Data.data(), m_Data.size(), "map_cover", m_Width, m_Height, m_Art, m_Image);
			m_Success = Result == QmMapCover::EPrepareResult::SUCCESS;
			m_Retry = Result == QmMapCover::EPrepareResult::RESOURCE_ERROR;
		}
		catch(const std::bad_alloc &)
		{
			m_Retry = true;
		}
		catch(const std::length_error &)
		{
			m_Retry = true;
		}
	}
};

CQmMapCovers::CQmMapCovers(TRequestFactory CreateRequest) :
	m_CreateRequest(CreateRequest != nullptr ? CreateRequest : HttpGet)
{
}

CQmMapCovers::~CQmMapCovers()
{
	CancelPending();
}

std::string CQmMapCovers::MakeKeyString(const SKey &Key)
{
	return Key.m_Url + (Key.m_Role == ERole::DETAILS ? "|details" : "|row");
}

QmMapCover::SArt CQmMapCovers::StyleForUrl(const char *pUrl, ERole Role)
{
	QmMapCover::SArt Art;
	Art.m_Seed = QmMapCover::SeedForUrl(pUrl);
	if(Role == ERole::DETAILS)
	{
		Art.m_Shade = 0.88f;
		Art.m_CornerRadius = 9.0f;
	}
	else
	{
		Art.m_Shade = 0.84f;
		Art.m_CornerRadius = 7.0f;
	}
	return Art;
}

void CQmMapCovers::ComposeSize(ERole Role, int *pWidth, int *pHeight)
{
	if(Role == ERole::DETAILS)
	{
		*pWidth = DETAILS_WIDTH;
		*pHeight = DETAILS_HEIGHT;
	}
	else
	{
		*pWidth = ROW_WIDTH;
		*pHeight = ROW_HEIGHT;
	}
}

void CQmMapCovers::Update(IHttp *pHttp, IEngine *pEngine, IGraphics *pGraphics, IStorage *pStorage, float Now)
{
	if(pHttp == nullptr || pEngine == nullptr || pGraphics == nullptr || pStorage == nullptr)
		return;
	if(m_ShutdownPending && pGraphics != m_pGraphics)
		return;
	m_pHttp = pHttp;
	m_pEngine = pEngine;
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_Now = Now;
	++m_Frame;
	// 空闲时只更新时间，失败冷却仍可到期；不做读盘、下载或纹理操作。
	if(!Loading())
		return;

	try
	{
		if(m_ShutdownPending)
		{
			DrainTextures();
			return;
		}
		// 上帧已登记的纹理可能还在等待卸载旧项，先收尾再接纳新上传。
		EvictTextures();
		PollJobs();
		PollDownloads();
		StartQueued();
	}
	catch(const std::bad_alloc &)
	{
		// 各阶段在移交前保留来源，下一帧从现有 owner 继续。
	}
	catch(const std::length_error &)
	{
	}
}

bool CQmMapCovers::Get(const char *pUrl, ERole Role, SImage *pOut)
{
	if(pOut == nullptr)
		return false;
	*pOut = {};
	if(m_ShutdownPending)
		return false;
	if(!QmMapCover::ValidThumbnailUrl(pUrl))
		return false;

	try
	{
		SKey Key{pUrl, Role};
		if(SEntry *pEntry = FindEntry(Key))
		{
			pEntry->m_LastUsedFrame = m_Frame;
			pOut->m_Texture = pEntry->m_Texture;
			return pOut->m_Texture.IsValid();
		}

		const auto Failed = m_vFailedUntil.find(Key.m_Url);
		if(Failed != m_vFailedUntil.end() && m_Now < Failed->second)
			return false;
		std::string PendingKey = MakeKeyString(Key);
		if(m_PendingKeys.find(PendingKey) != m_PendingKeys.end() || FindJob(Key) != nullptr || IsDownloading(Key.m_Url))
			return false;

		char aCachePath[IO_MAX_PATH_LENGTH];
		if(!QmMapCover::ThumbnailCacheFileName(pUrl, aCachePath, sizeof(aCachePath)))
			return false;
		if((int)m_vQueue.size() >= MAX_QUEUED)
			return false;

		STask Task{std::move(Key), aCachePath, PendingKey};
		const auto [Pending, Inserted] = m_PendingKeys.insert(std::move(PendingKey));
		if(!Inserted)
			return false;
		try
		{
			m_vQueue.push_back(std::move(Task));
		}
		catch(...)
		{
			// 只撤回本次准入；旧队列与已就绪纹理仍由原所有者持有。
			m_PendingKeys.erase(Pending);
			throw;
		}
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}

	return false;
}

void CQmMapCovers::CancelPending()
{
	for(auto &Download : m_vDownloads)
	{
		if(Download.m_pRequest)
			Download.m_pRequest->Abort();
	}
	m_vDownloads.clear();
	m_DownloadingUrls.clear();
	for(auto &Job : m_vJobs)
	{
		if(Job.m_pJob)
			Job.m_pJob->Abort();
	}
	m_vJobs.clear();
	m_vQueue.clear();
	m_PendingKeys.clear();
}

void CQmMapCovers::Shutdown(IGraphics *pGraphics)
{
	// 未卸载完的句柄仍属于旧设备，不能借重初始化后的接口收尾。
	if(m_ShutdownPending && pGraphics != m_pGraphics)
		return;
	CancelPending();
	m_vFailedUntil.clear();
	m_pGraphics = pGraphics;
	m_ShutdownPending = pGraphics != nullptr && !m_vEntries.empty();
	if(pGraphics == nullptr)
		m_vEntries.clear();
	else
	{
		try
		{
			DrainTextures();
		}
		catch(const std::bad_alloc &)
		{
			return;
		}
		catch(const std::length_error &)
		{
			return;
		}
	}
	m_pGraphics = nullptr;
}

void CQmMapCovers::DrainTextures()
{
	while(!m_vEntries.empty())
	{
		m_pGraphics->UnloadTexture(&m_vEntries.front().m_Texture);
		m_vEntries.erase(m_vEntries.begin());
	}
	m_ShutdownPending = false;
}

bool CQmMapCovers::Loading() const
{
	return m_ShutdownPending || !m_vQueue.empty() || !m_vJobs.empty() || !m_vDownloads.empty() || (int)m_vEntries.size() > MAX_TEXTURES;
}

void CQmMapCovers::EnsureCacheFolder()
{
	if(m_pStorage == nullptr || m_CacheFolderReady)
		return;
	if(!m_pStorage->FolderExists(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE))
	{
		// 存储接口只创建单层目录，先补父目录再建缓存目录。
		if(!m_pStorage->FolderExists("qmclient", IStorage::TYPE_SAVE))
			m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE);
	}
	m_CacheFolderReady = m_pStorage->FolderExists(QmMapCover::CACHE_FOLDER, IStorage::TYPE_SAVE);
}

void CQmMapCovers::MarkFailed(const STask &Task, const char *pReason)
{
	// 唯一可能分配的冷却登记先完成；失败时队列、结果和 pending 均保持原样。
	m_vFailedUntil.insert_or_assign(Task.m_Key.m_Url, m_Now + FAILURE_RETRY_SECONDS);
	log_debug("map_covers", "failed to prepare cover (%s): %s", pReason != nullptr ? pReason : "unknown", Task.m_Key.m_Url.c_str());
	m_PendingKeys.erase(Task.m_PendingKey);
	// 同一地图的其它用途也一并冷却，避免一个坏 URL 被每个可见行反复排队。
	for(auto It = m_vQueue.begin(); It != m_vQueue.end();)
	{
		if(It->m_Key.m_Url != Task.m_Key.m_Url)
		{
			++It;
			continue;
		}
		m_PendingKeys.erase(It->m_PendingKey);
		It = m_vQueue.erase(It);
	}
}

void CQmMapCovers::EraseQueued(const SKey &Key)
{
	for(auto It = m_vQueue.begin(); It != m_vQueue.end();)
	{
		if(It->m_Key == Key)
			It = m_vQueue.erase(It);
		else
			++It;
	}
}

void CQmMapCovers::PollJobs()
{
	int Uploads = 0;
	for(size_t Index = 0; Index < m_vJobs.size();)
	{
		SJob &Job = m_vJobs[Index];
		const IJob::EJobState State = Job.m_pJob != nullptr ? Job.m_pJob->State() : IJob::STATE_ABORTED;
		if(Job.m_pJob != nullptr && !Job.m_pJob->Finished())
		{
			++Index;
			continue;
		}
		if(State == IJob::STATE_DONE && Job.m_pJob->m_Retry)
		{
			// 拒收或资源不足后的旧 job 不能再次入池；编码数据仍由旧 owner 保留。
			auto pRetry = std::make_shared<CComposeJob>(Job.m_pJob->m_Data, Job.m_pJob->m_Width, Job.m_pJob->m_Height, Job.m_pJob->m_Art);
			try
			{
				if(m_pEngine->TryAddJob(pRetry))
					Job.m_pJob = std::move(pRetry);
			}
			catch(...)
			{
				pRetry->Abort();
				throw;
			}
			++Index;
			continue;
		}
		if(State == IJob::STATE_DONE && Job.m_pJob->m_Success)
		{
			if(Uploads >= MAX_UPLOADS_PER_FRAME)
			{
				++Index;
				continue;
			}
			const bool Uploaded = AddTexture(Job);
			++Uploads;
			if(!Uploaded)
			{
				++Index;
				continue;
			}
			m_PendingKeys.erase(Job.m_Task.m_PendingKey);
			m_vJobs.erase(m_vJobs.begin() + Index);
			EvictTextures();
			continue;
		}
		if(State == IJob::STATE_DONE || State == IJob::STATE_ABORTED || State == IJob::STATE_FAILED)
		{
			// 解码/合成失败：缓存文件可能损坏，删除后冷却重试。
			if(!Job.m_FailureRecorded)
			{
				MarkFailed(Job.m_Task, "compose");
				Job.m_FailureRecorded = true;
			}
			if(State == IJob::STATE_DONE && m_pStorage != nullptr && !Job.m_Task.m_CachePath.empty())
				m_pStorage->RemoveFile(Job.m_Task.m_CachePath.c_str(), IStorage::TYPE_SAVE);
			m_vJobs.erase(m_vJobs.begin() + Index);
			continue;
		}
		++Index;
	}
}

void CQmMapCovers::PollDownloads()
{
	for(size_t Index = 0; Index < m_vDownloads.size();)
	{
		SDownload &Download = m_vDownloads[Index];
		if(Download.m_pRequest == nullptr || !Download.m_pRequest->Done())
		{
			++Index;
			continue;
		}

		unsigned char *pData = nullptr;
		size_t Length = 0;
		if(Download.m_pRequest->State() == EHttpState::DONE)
		{
			Download.m_pRequest->Result(&pData, &Length);
		}
		const bool Success = PngHeaderMatches(pData, Length) && Length <= static_cast<size_t>(MAX_RESPONSE_BYTES);
		if(Success)
		{
			if((int)m_vJobs.size() >= MAX_JOBS)
			{
				++Index;
				continue;
			}
			// Result 是借用。先备齐所有合成输入及登记容量，准备失败时不确认也不丢结果。
			SJob Prepared = PrepareComposeJob(Download.m_Task, pData, Length);
			if(!Download.m_Validated)
			{
				Download.m_pRequest->OnValidation(true);
				Download.m_Validated = true;
			}
			if(Download.m_pRequest->State() == EHttpState::DONE)
			{
				if(!SubmitComposeJob(Prepared))
				{
					++Index;
					continue;
				}
				EraseQueued(Download.m_Task.m_Key);
				m_DownloadingUrls.erase(Download.m_Task.m_Key.m_Url);
				m_vDownloads.erase(m_vDownloads.begin() + Index);
				continue;
			}
		}
		MarkFailed(Download.m_Task, "download");
		if(!Download.m_Validated)
		{
			Download.m_pRequest->OnValidation(false);
			Download.m_Validated = true;
		}
		m_DownloadingUrls.erase(Download.m_Task.m_Key.m_Url);
		m_vDownloads.erase(m_vDownloads.begin() + Index);
	}
}

void CQmMapCovers::StartQueued()
{
	EnsureCacheFolder();
	// 每帧最多读几张磁盘缓存，避免一次打开页面时连续读盘造成卡顿。
	int Reads = 0;
	for(auto It = m_vQueue.begin(); It != m_vQueue.end();)
	{
		if((int)m_vJobs.size() >= MAX_JOBS)
			return;
		const STask &Task = *It;
		if(IsDownloading(Task.m_Key.m_Url) || FindJob(Task.m_Key) != nullptr)
		{
			++It;
			continue;
		}

		if(Reads >= MAX_READS_PER_FRAME)
			return;
		void *pFileData = nullptr;
		unsigned FileSize = 0;
		const bool Read = m_pStorage != nullptr && m_pStorage->ReadFile(Task.m_CachePath.c_str(), IStorage::TYPE_SAVE, &pFileData, &FileSize);
		const std::unique_ptr<void, decltype(&std::free)> FileData(pFileData, &std::free);
		if(Read)
		{
			++Reads;
			const bool Valid = pFileData != nullptr && FileSize >= 8 && FileSize <= static_cast<size_t>(MAX_RESPONSE_BYTES) &&
					   PngHeaderMatches(static_cast<const unsigned char *>(pFileData), FileSize);
			if(Valid)
			{
				if(!StartComposeJob(Task, static_cast<const unsigned char *>(pFileData), FileSize))
					return;
				It = m_vQueue.erase(It);
			}
			else
			{
				// 损坏的缓存删掉重新下载；下载槽位不足时保留任务等下一帧。
				if(m_pStorage != nullptr)
					m_pStorage->RemoveFile(Task.m_CachePath.c_str(), IStorage::TYPE_SAVE);
				if(!StartDownload(Task))
					return;
				It = m_vQueue.erase(It);
			}
			continue;
		}

		if(!StartDownload(Task))
			return;
		It = m_vQueue.erase(It);
	}
}

bool CQmMapCovers::StartDownload(const STask &Task)
{
	if(m_pHttp == nullptr || (int)m_vDownloads.size() >= MAX_DOWNLOADS)
		return false;
	SDownload Download{Task, {}};
	std::shared_ptr<IHttpRequest> pRequest(m_CreateRequest(Task.m_Key.m_Url.c_str()));
	if(!pRequest)
		return false;
	pRequest->Timeout(CTimeout{5000, 15000, 512, 10});
	pRequest->MaxResponseSize(MAX_RESPONSE_BYTES);
	pRequest->WriteToFileAndMemory(m_pStorage, Task.m_CachePath.c_str(), IStorage::TYPE_SAVE);
	// 先落在临时文件：只有确认是合法 PNG 才由 OnValidation(true) 移到缓存路径。
	pRequest->ValidateBeforeOverwrite(true);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	Download.m_pRequest = pRequest;
	m_vDownloads.reserve(m_vDownloads.size() + 1);
	if(!m_DownloadingUrls.insert(Task.m_Key.m_Url).second)
		return false;
	static_assert(std::is_nothrow_move_constructible_v<SDownload>);
	m_vDownloads.push_back(std::move(Download));
	// 从这里开始已有持久 owner。Run 即使异常退出也不能遗失可能已提交的请求。
	try
	{
		m_pHttp->Run(std::move(pRequest));
	}
	catch(...)
	{
		m_vDownloads.back().m_pRequest->Abort();
		throw;
	}
	return true;
}

CQmMapCovers::SJob CQmMapCovers::PrepareComposeJob(const STask &Task, const unsigned char *pPngData, size_t PngSize)
{
	int Width = 0;
	int Height = 0;
	ComposeSize(Task.m_Key.m_Role, &Width, &Height);
	std::vector<unsigned char> Data(pPngData, pPngData + PngSize);
	auto pJob = std::make_shared<CComposeJob>(std::move(Data), Width, Height, StyleForUrl(Task.m_Key.m_Url.c_str(), Task.m_Key.m_Role));
	SJob Prepared{Task, std::move(pJob)};
	m_vJobs.reserve(m_vJobs.size() + 1);
	return Prepared;
}

bool CQmMapCovers::SubmitComposeJob(SJob &Job)
{
	try
	{
		if(!m_pEngine->TryAddJob(Job.m_pJob))
			return false;
	}
	catch(...)
	{
		Job.m_pJob->Abort();
		throw;
	}
	static_assert(std::is_nothrow_move_constructible_v<SJob>);
	m_vJobs.push_back(std::move(Job));
	return true;
}

bool CQmMapCovers::StartComposeJob(const STask &Task, const unsigned char *pPngData, size_t PngSize)
{
	SJob Prepared = PrepareComposeJob(Task, pPngData, PngSize);
	return SubmitComposeJob(Prepared);
}

bool CQmMapCovers::AddTexture(SJob &Job)
{
	if(Job.m_pJob->m_Image.m_pData == nullptr)
	{
		Job.m_pJob->m_Retry = true;
		return false;
	}
	// 元数据与容量必须在上传接受前备齐；接受后只移动登记句柄。
	SEntry Entry{Job.m_Task.m_Key, {}, m_Frame};
	m_vEntries.reserve(m_vEntries.size() + 1);
	SGraphicsUploadRelease Release{};
	IGraphics::CTextureHandle Texture;
	if(!m_pGraphics->TryLoadTextureRawMoveOwned(Job.m_pJob->m_Image, 0, "qm_map_cover", Release, Texture))
		return false;
	if(!Texture.IsValid())
	{
		// 已消费但没有可用句柄：编码输入仍保留，可重新合成而不把缓存标坏。
		Job.m_pJob->m_Retry = true;
		return false;
	}
	Entry.m_Texture = Texture;
	static_assert(std::is_nothrow_move_constructible_v<SEntry>);
	m_vEntries.push_back(std::move(Entry));
	return true;
}

void CQmMapCovers::EvictTextures()
{
	while((int)m_vEntries.size() > MAX_TEXTURES)
	{
		auto Oldest = std::min_element(m_vEntries.begin(), m_vEntries.end(), [](const SEntry &A, const SEntry &B) {
			return A.m_LastUsedFrame < B.m_LastUsedFrame;
		});
		if(Oldest == m_vEntries.end())
			return;
		if(m_pGraphics != nullptr)
			m_pGraphics->UnloadTexture(&Oldest->m_Texture);
		m_vEntries.erase(Oldest);
	}
}

CQmMapCovers::SEntry *CQmMapCovers::FindEntry(const SKey &Key)
{
	auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const SEntry &Entry) { return Entry.m_Key == Key; });
	return It == m_vEntries.end() ? nullptr : &(*It);
}

CQmMapCovers::SJob *CQmMapCovers::FindJob(const SKey &Key)
{
	auto It = std::find_if(m_vJobs.begin(), m_vJobs.end(), [&](const SJob &Job) { return Job.m_Task.m_Key == Key; });
	return It == m_vJobs.end() ? nullptr : &(*It);
}

bool CQmMapCovers::IsDownloading(const std::string &Url) const
{
	return m_DownloadingUrls.find(Url) != m_DownloadingUrls.end();
}
