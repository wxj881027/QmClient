/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_RESOURCE_PAGE_LOADER_H
#define GAME_CLIENT_UI_RESOURCE_PAGE_LOADER_H

#include "resource_page_cache.h"
#include <engine/image.h>

#include <memory>
#include <map>
#include <string>
#include <vector>

class IEngine;
class IStorage;

struct SResourcePageArtifact
{
	std::string m_Path;
	CImageInfo m_Image;
};

struct SResourcePageMetrics
{
	uint64_t m_LoadAttempts = 0;
	uint64_t m_CacheHits = 0;
	uint64_t m_DeduplicatedRequests = 0;
	uint64_t m_CancelledLoads = 0;
	uint64_t m_FailedLoads = 0;
	uint64_t m_UploadedResources = 0;
	uint64_t m_LastLoadNanoseconds = 0;
	uint64_t m_LastDecodedBytes = 0;
	uint64_t m_PeakDecodedBytes = 0;
};

class IResourcePageSink
{
public:
	virtual ~IResourcePageSink() = default;
	// 主线程接收一个解码结果；失败时 Finish(false) 必须回收当前页的部分上传。
	virtual bool Upload(const char *pPageId, SResourcePageArtifact &&Artifact) = 0;
	virtual void Finish(const char *pPageId, bool Success) = 0;
};

class CResourcePageLoader final
{
	class CLoadJob;
	struct SPendingLoad;

	CResourcePageCache m_Cache;
	IEngine *m_pEngine = nullptr;
	IStorage *m_pStorage = nullptr;
	std::vector<SResourcePageManifest> m_vManifests;
	std::map<std::string, size_t, std::less<>> m_ManifestIndex;
	std::vector<SResourcePageMetrics> m_vMetrics;
	std::vector<std::shared_ptr<SPendingLoad>> m_vPending;
	bool m_Shutdown = false;

	const SResourcePageManifest *FindManifest(const char *pId) const;
	SResourcePageMetrics *FindMetrics(const char *pId);
	void CancelPending();

public:
	CResourcePageLoader(IEngine *pEngine, IStorage *pStorage) :
		m_pEngine(pEngine), m_pStorage(pStorage) {}
	~CResourcePageLoader();

	CResourcePageLoader(const CResourcePageLoader &) = delete;
	CResourcePageLoader &operator=(const CResourcePageLoader &) = delete;

	bool RegisterManifest(SResourcePageManifest Manifest);
	bool HasRequestCapacity() const;
	size_t ManifestCount() const { return m_vManifests.size(); }
	// READY 命中或排队已满返回 false；generation 变化前必须先 Invalidate。
	bool Request(const char *pId, uint32_t Generation);
	int Poll(IResourcePageSink &Sink, int MaxUploads = 1);
	void Invalidate(uint32_t Generation, IResourcePageSink &Sink);
	void Forget(const char *pId, uint32_t Generation, IResourcePageSink &Sink);
	void UnregisterManifest(const char *pId, uint32_t Generation, IResourcePageSink &Sink);
	void ClearManifests(uint32_t Generation, IResourcePageSink &Sink);
	void Shutdown(IResourcePageSink &Sink);
	// 无 sink 的销毁路径只取消 worker；GPU 资源由 sink owner 的析构释放。
	void Shutdown();

	const CResourcePageCache &Cache() const { return m_Cache; }
	const SResourcePageMetrics *Metrics(const char *pId) const;
};

#endif
