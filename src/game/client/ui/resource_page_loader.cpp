/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "resource_page_loader.h"

#include <base/io.h>
#include <base/time.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

namespace
{
constexpr size_t MAX_RESOURCES = 16;
constexpr size_t MAX_PENDING = 2;
constexpr size_t MAX_MANIFESTS = 8192;
constexpr size_t MAX_FALLBACK_PATHS = 8;
constexpr size_t MAX_FILE_BYTES = 16 * 1024 * 1024;
constexpr size_t MAX_PAGE_BYTES = 64 * 1024 * 1024;
constexpr size_t MAX_IMAGE_PIXELS = 2048 * 2048;

bool SafeResourcePath(const std::string &Path)
{
	if(Path.empty() || Path.front() == '/' || Path.find_first_of("\\:") != std::string::npos ||
		Path.find('\0') != std::string::npos)
		return false;
	size_t Start = 0;
	do
	{
		const size_t End = Path.find('/', Start);
		const auto Part = Path.substr(Start, End == std::string::npos ? End : End - Start);
		if(Part.empty() || Part == "." || Part == "..")
			return false;
		if(End == std::string::npos)
			return true;
		Start = End + 1;
	} while(Start <= Path.size());
	return false;
}

// 在官方 PNG 解码器分配内存前限制 IHDR；格式校验仍由官方解码器负责。
bool BoundedPngHeader(const std::vector<uint8_t> &vData)
{
	constexpr uint8_t aPrefix[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
	if(vData.size() < 33 || std::memcmp(vData.data(), aPrefix, sizeof(aPrefix)) != 0)
		return false;
	const auto Read32 = [&vData](size_t Offset) {
		return (uint32_t(vData[Offset]) << 24) | (uint32_t(vData[Offset + 1]) << 16) |
			(uint32_t(vData[Offset + 2]) << 8) | uint32_t(vData[Offset + 3]);
	};
	const uint64_t Width = Read32(16);
	const uint64_t Height = Read32(20);
	return Width > 0 && Height > 0 && Width <= 4096 && Height <= 4096 &&
		Width * Height <= MAX_IMAGE_PIXELS;
}
}

struct CResourcePageLoader::SPendingLoad
{
	std::string m_Id;
	uint32_t m_Generation = 0;
	uint64_t m_RequestId = 0;
	std::shared_ptr<CLoadJob> m_pJob;
	size_t m_NextArtifact = 0;
	bool m_Cancelled = false;
};

class CResourcePageLoader::CLoadJob final : public IJob
{
	IStorage *m_pStorage;
	std::vector<SResourcePageResource> m_vResources;
	std::atomic<bool> m_Cancelled{false};

protected:
	void Run() override
	{
		m_StartNanoseconds = static_cast<uint64_t>(time_get_nanoseconds().count());
		size_t PageBytes = 0;
		for(const SResourcePageResource &Resource : m_vResources)
		{
			if(m_Cancelled.load())
				break;
			std::vector<std::string> vCandidates;
			vCandidates.reserve(Resource.m_vFallbackPaths.size() + 1);
			vCandidates.push_back(Resource.m_Path);
			vCandidates.insert(vCandidates.end(), Resource.m_vFallbackPaths.begin(), Resource.m_vFallbackPaths.end());
			SResourcePageArtifact Artifact;
			Artifact.m_Path = Resource.m_Path;
			bool Loaded = false;
			std::string LastError = "resource read failed";
			for(const auto &Candidate : vCandidates)
			{
				if(m_Cancelled.load())
					break;
				IOHANDLE File = m_pStorage->OpenFile(Candidate.c_str(), IOFLAG_READ, IStorage::TYPE_ALL);
				if(!File)
					continue;
				const int64_t Length = io_length(File);
				if(Length <= 0 || Length > static_cast<int64_t>(MAX_FILE_BYTES))
				{
					io_close(File);
					LastError = "resource file size exceeds limit";
					continue;
				}
				std::vector<uint8_t> vData(static_cast<size_t>(Length));
				size_t Offset = 0;
				while(Offset < vData.size() && !m_Cancelled.load())
				{
					const unsigned Size = static_cast<unsigned>(std::min<size_t>(64 * 1024, vData.size() - Offset));
					const unsigned Read = io_read(File, vData.data() + Offset, Size);
					Offset += Read;
					if(Read != Size)
						break;
				}
				io_close(File);
				if(m_Cancelled.load())
					break;
				if(Offset != vData.size() || !BoundedPngHeader(vData))
				{
					LastError = "resource PNG header invalid or exceeds limit";
					continue;
				}
				CImageInfo Image;
				CByteBufferReader Reader(vData.data(), vData.size());
				int PngliteIncompatible = 0;
				if(!CImageLoader::LoadPng(Reader, Candidate.c_str(), Image, PngliteIncompatible))
				{
					LastError = "resource decode failed";
					continue;
				}
				Artifact.m_Image = std::move(Image);
				Loaded = true;
				break;
			}
			if(m_Cancelled.load())
				break;
			if(!Loaded)
			{
				m_Error = LastError;
				break;
			}
			if(Resource.m_PreviewSize > 0 &&
				(Artifact.m_Image.m_Width > static_cast<size_t>(Resource.m_PreviewSize) ||
					Artifact.m_Image.m_Height > static_cast<size_t>(Resource.m_PreviewSize)))
			{
				const float Scale = std::min(
					static_cast<float>(Resource.m_PreviewSize) / Artifact.m_Image.m_Width,
					static_cast<float>(Resource.m_PreviewSize) / Artifact.m_Image.m_Height);
				ResizeImage(Artifact.m_Image,
					std::max(1, static_cast<int>(Artifact.m_Image.m_Width * Scale)),
					std::max(1, static_cast<int>(Artifact.m_Image.m_Height * Scale)));
			}
			if(Artifact.m_Image.DataSize() > MAX_PAGE_BYTES - PageBytes)
			{
				m_Error = "resource page exceeds memory limit";
				break;
			}
			PageBytes += Artifact.m_Image.DataSize();
			m_DecodedBytes = PageBytes;
			m_vArtifacts.push_back(std::move(Artifact));
		}
		if(m_Cancelled.load() || !m_Error.empty())
			m_vArtifacts.clear();
		m_EndNanoseconds = static_cast<uint64_t>(time_get_nanoseconds().count());
	}

public:
	std::vector<SResourcePageArtifact> m_vArtifacts;
	std::string m_Error;
	uint64_t m_StartNanoseconds = 0;
	uint64_t m_EndNanoseconds = 0;
	uint64_t m_DecodedBytes = 0;

	CLoadJob(IStorage *pStorage, std::vector<SResourcePageResource> vResources) :
		m_pStorage(pStorage), m_vResources(std::move(vResources))
	{
		// ABORTED 不代表 worker 已停止；独立取消标志使结果始终以 STATE_DONE 发布。
		// 官方 CClient 在销毁 storage / GameClient 前执行 Engine::ShutdownJobs。
	}

	void Cancel() { m_Cancelled.store(true); }
};

const SResourcePageManifest *CResourcePageLoader::FindManifest(const char *pId) const
{
	if(!pId)
		return nullptr;
	const auto It = m_ManifestIndex.find(pId);
	return It == m_ManifestIndex.end() ? nullptr : &m_vManifests[It->second];
}

SResourcePageMetrics *CResourcePageLoader::FindMetrics(const char *pId)
{
	if(!pId)
		return nullptr;
	const auto It = m_ManifestIndex.find(pId);
	return It == m_ManifestIndex.end() ? nullptr : &m_vMetrics[It->second];
}

const SResourcePageMetrics *CResourcePageLoader::Metrics(const char *pId) const
{
	return const_cast<CResourcePageLoader *>(this)->FindMetrics(pId);
}

CResourcePageLoader::~CResourcePageLoader()
{
	Shutdown();
}

bool CResourcePageLoader::RegisterManifest(SResourcePageManifest Manifest)
{
	if(m_Shutdown || Manifest.m_Id.empty() || Manifest.m_Id.size() > 128 ||
		Manifest.m_Id.find('\0') != std::string::npos || FindManifest(Manifest.m_Id.c_str()) ||
		m_vManifests.size() >= MAX_MANIFESTS ||
		Manifest.m_vResources.empty() || Manifest.m_vResources.size() > MAX_RESOURCES)
		return false;
	for(size_t Index = 0; Index < Manifest.m_vResources.size(); ++Index)
	{
		const auto &Resource = Manifest.m_vResources[Index];
		if(!SafeResourcePath(Resource.m_Path) || Resource.m_PreviewSize < 0 || Resource.m_PreviewSize > 2048 ||
			Resource.m_vFallbackPaths.size() > MAX_FALLBACK_PATHS)
			return false;
		for(const auto &Fallback : Resource.m_vFallbackPaths)
			if(!SafeResourcePath(Fallback))
				return false;
		for(size_t Other = 0; Other < Index; ++Other)
			if(Manifest.m_vResources[Other].m_Path == Resource.m_Path)
				return false;
	}
	if(!m_Cache.RegisterPage(Manifest.m_Id.c_str()))
		return false;
	const size_t Index = m_vManifests.size();
	m_ManifestIndex.emplace(Manifest.m_Id, Index);
	m_vManifests.push_back(std::move(Manifest));
	m_vMetrics.emplace_back();
	return true;
}

bool CResourcePageLoader::HasRequestCapacity() const
{
	return !m_Shutdown && m_vPending.size() < MAX_PENDING;
}

bool CResourcePageLoader::Request(const char *pId, uint32_t Generation)
{
	const auto *pManifest = FindManifest(pId);
	const auto *pState = m_Cache.Find(pId);
	// 更换 generation 必须经过 Invalidate，同时回收未提交的 GPU 资源。
	auto *pMetrics = FindMetrics(pId);
	if(m_Shutdown)
		return false;
	if(pState && pState->m_State == EResourcePageState::READY && pState->m_Generation == Generation)
	{
		++pMetrics->m_CacheHits;
		return false;
	}
	if(pState && pState->m_State == EResourcePageState::LOADING)
	{
		++pMetrics->m_DeduplicatedRequests;
		return false;
	}
	if(pState && pState->m_State == EResourcePageState::READY)
		return false;
	if(!pManifest || !m_pEngine || !m_pStorage ||
		m_vPending.size() >= MAX_PENDING || !m_Cache.BeginLoad(pId, Generation))
		return false;
	++pMetrics->m_LoadAttempts;
	auto pPending = std::make_shared<SPendingLoad>();
	pPending->m_Id = pId;
	pPending->m_Generation = Generation;
	pPending->m_RequestId = m_Cache.Find(pId)->m_RequestId;
	pPending->m_pJob = std::make_shared<CLoadJob>(m_pStorage, pManifest->m_vResources);
	m_pEngine->AddJob(pPending->m_pJob);
	m_vPending.push_back(std::move(pPending));
	return true;
}

int CResourcePageLoader::Poll(IResourcePageSink &Sink, int MaxUploads)
{
	int Completed = 0;
	for(auto It = m_vPending.begin(); It != m_vPending.end();)
	{
		auto &Pending = **It;
		if(Pending.m_pJob->State() != IJob::STATE_DONE)
		{
			++It;
			continue;
		}
		if(!m_Cache.IsCurrentLoad(Pending.m_Id.c_str(), Pending.m_Generation, Pending.m_RequestId))
		{
			It = m_vPending.erase(It);
			continue;
		}
		auto &Job = *Pending.m_pJob;
		while(Job.m_Error.empty() && Pending.m_NextArtifact < Job.m_vArtifacts.size() && MaxUploads > 0)
		{
			--MaxUploads;
			if(!Sink.Upload(Pending.m_Id.c_str(), std::move(Job.m_vArtifacts[Pending.m_NextArtifact++])))
				Job.m_Error = "resource texture upload failed";
			else
				++FindMetrics(Pending.m_Id.c_str())->m_UploadedResources;
		}
		if(Job.m_Error.empty() && Pending.m_NextArtifact < Job.m_vArtifacts.size())
		{
			++It;
			continue;
		}
		const bool Success = Job.m_Error.empty();
		auto *pMetrics = FindMetrics(Pending.m_Id.c_str());
		pMetrics->m_LastLoadNanoseconds = Job.m_EndNanoseconds >= Job.m_StartNanoseconds ? Job.m_EndNanoseconds - Job.m_StartNanoseconds : 0;
		pMetrics->m_LastDecodedBytes = Job.m_DecodedBytes;
		pMetrics->m_PeakDecodedBytes = std::max(pMetrics->m_PeakDecodedBytes, Job.m_DecodedBytes);
		if(!Success)
			++pMetrics->m_FailedLoads;
		Sink.Finish(Pending.m_Id.c_str(), Success);
		m_Cache.CompleteLoad(Pending.m_Id.c_str(), Pending.m_Generation, Pending.m_RequestId, Success, Job.m_Error.c_str());
		It = m_vPending.erase(It);
		++Completed;
	}
	return Completed;
}

void CResourcePageLoader::CancelPending()
{
	for(auto It = m_vPending.begin(); It != m_vPending.end();)
	{
		if(!(*It)->m_Cancelled)
		{
			auto *pMetrics = FindMetrics((*It)->m_Id.c_str());
			if(pMetrics)
				++pMetrics->m_CancelledLoads;
			(*It)->m_Cancelled = true;
		}
		(*It)->m_pJob->Cancel();
		if((*It)->m_pJob->State() == IJob::STATE_DONE)
			It = m_vPending.erase(It);
		else
			++It;
	}
}

void CResourcePageLoader::Invalidate(uint32_t Generation, IResourcePageSink &Sink)
{
	CancelPending();
	for(const auto &Manifest : m_vManifests)
		Sink.Finish(Manifest.m_Id.c_str(), false);
	m_Cache.Invalidate(Generation);
}

void CResourcePageLoader::Forget(const char *pId, uint32_t Generation, IResourcePageSink &Sink)
{
	if(!FindManifest(pId))
		return;
	for(auto &Pending : m_vPending)
	{
		if(Pending->m_Id == pId && !Pending->m_Cancelled)
		{
			Pending->m_Cancelled = true;
			Pending->m_pJob->Cancel();
			++FindMetrics(pId)->m_CancelledLoads;
		}
	}
	Sink.Finish(pId, false);
	m_Cache.InvalidatePage(pId, Generation);
}

void CResourcePageLoader::UnregisterManifest(const char *pId, uint32_t Generation, IResourcePageSink &Sink)
{
	const auto IndexIt = pId ? m_ManifestIndex.find(pId) : m_ManifestIndex.end();
	if(IndexIt == m_ManifestIndex.end())
		return;
	const std::string Id(pId);
	Forget(Id.c_str(), Generation, Sink);
	const size_t Index = IndexIt->second;
	m_vManifests.erase(m_vManifests.begin() + Index);
	m_vMetrics.erase(m_vMetrics.begin() + Index);
	m_ManifestIndex.erase(IndexIt);
	for(size_t Current = Index; Current < m_vManifests.size(); ++Current)
		m_ManifestIndex[m_vManifests[Current].m_Id] = Current;
	m_Cache.UnregisterPage(Id.c_str());
}

void CResourcePageLoader::ClearManifests(uint32_t Generation, IResourcePageSink &Sink)
{
	Invalidate(Generation, Sink);
	m_vManifests.clear();
	m_vMetrics.clear();
	m_ManifestIndex.clear();
	m_Cache.Clear();
}

void CResourcePageLoader::Shutdown()
{
	m_Shutdown = true;
	CancelPending();
	m_Cache.Invalidate(0);
	// JobPool 保持 worker 所有权；worker 不保存 loader、UI 或 graphics 引用。
	m_vPending.clear();
}

void CResourcePageLoader::Shutdown(IResourcePageSink &Sink)
{
	Invalidate(0, Sink);
	Shutdown();
}
