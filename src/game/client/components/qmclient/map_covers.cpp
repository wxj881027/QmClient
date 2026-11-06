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
		m_Success = QmMapCover::DecodeAndCompose(m_Data.data(), m_Data.size(), "map_cover", m_Width, m_Height, m_Art, m_Image);
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
	m_pHttp = pHttp;
	m_pEngine = pEngine;
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_Now = Now;
	++m_Frame;
	// 空闲时只更新时间，失败冷却仍可到期；不做读盘、下载或纹理操作。
	if(!Loading())
		return;

	PollJobs();
	PollDownloads();
	StartQueued();
}

bool CQmMapCovers::Get(const char *pUrl, ERole Role, SImage *pOut)
{
	if(pOut == nullptr)
		return false;
	*pOut = {};
	if(!QmMapCover::ValidThumbnailUrl(pUrl))
		return false;

	const SKey Key{pUrl, Role};
	if(SEntry *pEntry = FindEntry(Key))
	{
		pEntry->m_LastUsedFrame = m_Frame;
		pOut->m_Texture = pEntry->m_Texture;
		return pOut->m_Texture.IsValid();
	}

	const auto Failed = m_vFailedUntil.find(Key.m_Url);
	if(Failed != m_vFailedUntil.end() && m_Now < Failed->second)
		return false;
	if(IsQueued(Key) || FindJob(Key) != nullptr || IsDownloading(Key.m_Url))
		return false;

	char aCachePath[IO_MAX_PATH_LENGTH];
	if(!QmMapCover::ThumbnailCacheFileName(pUrl, aCachePath, sizeof(aCachePath)))
		return false;
	if((int)m_vQueue.size() >= MAX_QUEUED)
		return false;

	STask Task;
	Task.m_Key = Key;
	Task.m_CachePath = aCachePath;
	m_vQueue.push_back(std::move(Task));
	m_PendingKeys.insert(MakeKeyString(Key));
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
	CancelPending();
	if(pGraphics != nullptr)
	{
		for(auto &Entry : m_vEntries)
			pGraphics->UnloadTexture(&Entry.m_Texture);
	}
	m_vEntries.clear();
	m_vFailedUntil.clear();
	m_pGraphics = nullptr;
}

bool CQmMapCovers::Loading() const
{
	return !m_vQueue.empty() || !m_vJobs.empty() || !m_vDownloads.empty();
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

void CQmMapCovers::MarkFailed(const SKey &Key, const char *pReason)
{
	log_debug("map_covers", "failed to prepare cover (%s): %s", pReason != nullptr ? pReason : "unknown", Key.m_Url.c_str());
	m_vFailedUntil[Key.m_Url] = m_Now + FAILURE_RETRY_SECONDS;
	m_PendingKeys.erase(MakeKeyString(Key));
	// 同一地图的其它用途也一并冷却，避免一个坏 URL 被每个可见行反复排队。
	for(auto It = m_vQueue.begin(); It != m_vQueue.end();)
	{
		if(It->m_Key.m_Url != Key.m_Url)
		{
			++It;
			continue;
		}
		m_PendingKeys.erase(MakeKeyString(It->m_Key));
		It = m_vQueue.erase(It);
	}
}

void CQmMapCovers::PollJobs()
{
	int Uploads = 0;
	for(size_t Index = 0; Index < m_vJobs.size();)
	{
		SJob &Job = m_vJobs[Index];
		const IJob::EJobState State = Job.m_pJob != nullptr ? Job.m_pJob->State() : IJob::STATE_ABORTED;
		if(State == IJob::STATE_DONE && Job.m_pJob->m_Success)
		{
			if(Uploads >= MAX_UPLOADS_PER_FRAME)
			{
				++Index;
				continue;
			}
			AddTexture(Job);
			++Uploads;
			m_PendingKeys.erase(MakeKeyString(Job.m_Key));
			m_vJobs.erase(m_vJobs.begin() + Index);
			continue;
		}
		if(State == IJob::STATE_DONE || State == IJob::STATE_ABORTED || State == IJob::STATE_FAILED)
		{
			// 解码/合成失败：缓存文件可能损坏，删除后冷却重试。
			if(State == IJob::STATE_DONE && m_pStorage != nullptr && !Job.m_CachePath.empty())
				m_pStorage->RemoveFile(Job.m_CachePath.c_str(), IStorage::TYPE_SAVE);
			MarkFailed(Job.m_Key, "compose");
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

		bool Success = false;
		std::vector<unsigned char> Data;
		if(Download.m_pRequest->State() == EHttpState::DONE)
		{
			unsigned char *pData = nullptr;
			size_t Length = 0;
			Download.m_pRequest->Result(&pData, &Length);
			if(PngHeaderMatches(pData, Length) && Length <= static_cast<size_t>(MAX_RESPONSE_BYTES))
			{
				Data.assign(pData, pData + Length);
				Success = true;
			}
		}
		// ValidateBeforeOverwrite 模式下必须在这里决定临时文件去留：成功才落到缓存路径。
		Download.m_pRequest->OnValidation(Success);
		m_DownloadingUrls.erase(Download.m_Key.m_Url);
		if(Success)
			StartComposeJob(STask{Download.m_Key, Download.m_CachePath}, Data.data(), Data.size());
		else
			MarkFailed(Download.m_Key, "download");
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
		const STask Task = *It;
		if(IsDownloading(Task.m_Key.m_Url) || FindJob(Task.m_Key) != nullptr)
		{
			++It;
			continue;
		}

		if(Reads >= MAX_READS_PER_FRAME)
			return;
		void *pFileData = nullptr;
		unsigned FileSize = 0;
		if(m_pStorage != nullptr && m_pStorage->ReadFile(Task.m_CachePath.c_str(), IStorage::TYPE_SAVE, &pFileData, &FileSize))
		{
			++Reads;
			const bool Valid = pFileData != nullptr && FileSize >= 8 && FileSize <= static_cast<size_t>(MAX_RESPONSE_BYTES) &&
					   PngHeaderMatches(static_cast<const unsigned char *>(pFileData), FileSize);
			if(Valid)
			{
				StartComposeJob(Task, static_cast<const unsigned char *>(pFileData), FileSize);
				free(pFileData);
				It = m_vQueue.erase(It);
			}
			else
			{
				// 损坏的缓存删掉重新下载；下载槽位不足时保留任务等下一帧。
				free(pFileData);
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
	std::shared_ptr<IHttpRequest> pRequest(m_CreateRequest(Task.m_Key.m_Url.c_str()));
	if(!pRequest)
		return false;
	pRequest->Timeout(CTimeout{5000, 15000, 512, 10});
	pRequest->MaxResponseSize(MAX_RESPONSE_BYTES);
	pRequest->WriteToFileAndMemory(m_pStorage, Task.m_CachePath.c_str(), IStorage::TYPE_SAVE);
	// 先落在临时文件：只有确认是合法 PNG 才由 OnValidation(true) 移到缓存路径。
	pRequest->ValidateBeforeOverwrite(true);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	m_pHttp->Run(pRequest);
	m_DownloadingUrls.insert(Task.m_Key.m_Url);
	m_vDownloads.push_back(SDownload{Task.m_Key, Task.m_CachePath, pRequest});
	return true;
}

void CQmMapCovers::StartComposeJob(const STask &Task, const unsigned char *pPngData, size_t PngSize)
{
	if(m_pEngine == nullptr || pPngData == nullptr || PngSize == 0)
	{
		MarkFailed(Task.m_Key, "empty response");
		return;
	}
	int Width = 0;
	int Height = 0;
	ComposeSize(Task.m_Key.m_Role, &Width, &Height);
	std::vector<unsigned char> Data(pPngData, pPngData + PngSize);
	auto pJob = std::make_shared<CComposeJob>(std::move(Data), Width, Height, StyleForUrl(Task.m_Key.m_Url.c_str(), Task.m_Key.m_Role));
	m_vJobs.push_back(SJob{Task.m_Key, Task.m_CachePath, pJob});
	m_PendingKeys.insert(MakeKeyString(Task.m_Key));
	m_pEngine->AddJob(pJob);
}

void CQmMapCovers::AddTexture(SJob &Job)
{
	if(m_pGraphics == nullptr || Job.m_pJob == nullptr || Job.m_pJob->m_Image.m_pData == nullptr)
	{
		MarkFailed(Job.m_Key, "empty image");
		return;
	}
	const IGraphics::CTextureHandle Texture = m_pGraphics->LoadTextureRawMove(Job.m_pJob->m_Image, 0, "qm_map_cover");
	if(!Texture.IsValid())
	{
		MarkFailed(Job.m_Key, "upload");
		return;
	}
	SEntry Entry;
	Entry.m_Key = Job.m_Key;
	Entry.m_Texture = Texture;
	Entry.m_LastUsedFrame = m_Frame;
	m_vEntries.push_back(std::move(Entry));
	EvictTextures();
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
	auto It = std::find_if(m_vJobs.begin(), m_vJobs.end(), [&](const SJob &Job) { return Job.m_Key == Key; });
	return It == m_vJobs.end() ? nullptr : &(*It);
}

bool CQmMapCovers::IsQueued(const SKey &Key) const
{
	return m_PendingKeys.find(MakeKeyString(Key)) != m_PendingKeys.end();
}

bool CQmMapCovers::IsDownloading(const std::string &Url) const
{
	return m_DownloadingUrls.find(Url) != m_DownloadingUrls.end();
}
