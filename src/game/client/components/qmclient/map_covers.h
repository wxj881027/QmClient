// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVERS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVERS_H

#include "map_cover_art.h"

#include <engine/graphics.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class IEngine;
class IHttp;
class IHttpRequest;
class IStorage;

// 地图库条目的封面背景缓存。
//
// 数据流：官方缩略图 URL → 磁盘缓存（qmclient/mapcovers）→ 工作线程解码/裁切/遮罩
// → 主线程上传纹理。同一 (URL, 目标像素尺寸) 只做一次；行条目与详情面板各自取自己
// 尺寸的纹理。页面退出只取消未完成的请求，已下载的磁盘缓存和已生成的纹理继续复用。
class CQmMapCovers
{
public:
	// 可直接绘制的一张封面：纹理 + 淡入系数（0..1）。
	struct SImage
	{
		IGraphics::CTextureHandle m_Texture{};
		float m_Opacity = 0.0f;
	};

	using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);

	explicit CQmMapCovers(TRequestFactory CreateRequest = nullptr);
	~CQmMapCovers();
	CQmMapCovers(const CQmMapCovers &) = delete;
	CQmMapCovers &operator=(const CQmMapCovers &) = delete;

	// 每帧调用一次（地图库页面渲染入口）：处理下载完成、上传纹理、推进淡入。
	void Update(IHttp *pHttp, IEngine *pEngine, IGraphics *pGraphics, IStorage *pStorage, float Now);
	// 取指定 UI 目标像素尺寸的封面；未就绪时返回 false 并排队生成。
	bool Get(const char *pUrl, int PixelWidth, int PixelHeight, SImage *pOut);
	// 离开地图库页面：取消排队与进行中的下载/任务；保留磁盘缓存与已生成的纹理。
	void Release();
	// 客户端关闭：释放全部 GPU 纹理（图形接口此刻仍存活）。
	void Shutdown(IGraphics *pGraphics);
	// 还有封面在排队/下载/合成（供性能日志与调试使用）。
	bool Loading() const;

private:
	struct SKey
	{
		std::string m_Url;
		int m_Width = 0;
		int m_Height = 0;

		bool operator==(const SKey &Other) const
		{
			return m_Width == Other.m_Width && m_Height == Other.m_Height && m_Url == Other.m_Url;
		}
	};

	struct SEntry
	{
		SKey m_Key;
		IGraphics::CTextureHandle m_Texture;
		float m_LoadedAt = 0.0f;
		int64_t m_LastUsedFrame = 0;
	};

	struct STask
	{
		SKey m_Key;
		std::string m_CachePath;
	};

	struct SDownload
	{
		SKey m_Key;
		std::string m_CachePath;
		std::shared_ptr<IHttpRequest> m_pRequest;
	};

	class CComposeJob;
	struct SJob
	{
		SKey m_Key;
		std::string m_CachePath;
		std::shared_ptr<CComposeJob> m_pJob;
	};

	// 每帧/总量预算：地图库一屏约二十行，超出部分分帧补齐，避免打开页面时卡顿。
	inline static constexpr int MAX_DOWNLOADS = 2;
	inline static constexpr int MAX_JOBS = 4;
	inline static constexpr int MAX_QUEUED = 64;
	inline static constexpr int MAX_TEXTURES = 40;
	inline static constexpr int MAX_UPLOADS_PER_FRAME = 2;
	inline static constexpr float FADE_SECONDS = 0.18f;
	inline static constexpr float FAILURE_RETRY_SECONDS = 120.0f;
	inline static constexpr int MAX_RESPONSE_BYTES = 512 * 1024;

	static std::string MakeKeyString(const SKey &Key);
	static QmMapCover::SArt StyleForUrl(const char *pUrl);

	void PollJobs();
	void PollDownloads();
	void StartQueued();
	void EnsureCacheFolder();
	void MarkFailed(const SKey &Key, const char *pReason);
	bool StartDownload(const STask &Task);
	void StartComposeJob(const STask &Task, const unsigned char *pPngData, size_t PngSize);
	void AddTexture(SJob &Job);
	void EvictTextures();
	SEntry *FindEntry(const SKey &Key);
	SJob *FindJob(const SKey &Key);
	bool IsQueued(const SKey &Key) const;
	bool IsDownloading(const std::string &Url) const;

	IHttp *m_pHttp = nullptr;
	IEngine *m_pEngine = nullptr;
	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	TRequestFactory m_CreateRequest;
	float m_Now = 0.0f;
	int64_t m_Frame = 0;
	bool m_CacheFolderReady = false;

	std::vector<SEntry> m_vEntries;
	std::deque<STask> m_vQueue;
	std::vector<SJob> m_vJobs;
	std::vector<SDownload> m_vDownloads;
	std::unordered_set<std::string> m_PendingKeys;
	std::unordered_set<std::string> m_DownloadingUrls;
	std::unordered_map<std::string, float> m_vFailedUntil;
};

#endif
