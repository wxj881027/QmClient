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

// 地图库条目的封面图缓存。
//
// 数据流：官方缩略图 URL → 磁盘缓存（qmclient/mapcovers）→ 工作线程解码/缩放/拖尾
// → 主线程上传纹理。纹理按「地图 + 用途（列表行 / 详情条）」缓存：尺寸是固定值，
// 不随窗口宽度和滚动条动画变化，所以来回滚动不会反复重合成。页面退出后继续完成已请求的封面，
// 已下载的磁盘缓存和已生成的纹理继续复用。
class CQmMapCovers
{
public:
	enum class ERole
	{
		ROW,
		DETAILS,
	};

	// 可直接绘制的一张封面图（纹理本身已含拖尾 alpha 与压暗）。
	struct SImage
	{
		IGraphics::CTextureHandle m_Texture{};
	};

	using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);

	explicit CQmMapCovers(TRequestFactory CreateRequest = nullptr);
	~CQmMapCovers();
	CQmMapCovers(const CQmMapCovers &) = delete;
	CQmMapCovers &operator=(const CQmMapCovers &) = delete;

	// 每帧调用一次（包含菜单关闭期间）；空闲时只更新时间：处理下载完成、上传纹理。
	void Update(IHttp *pHttp, IEngine *pEngine, IGraphics *pGraphics, IStorage *pStorage, float Now);
	// 取某张地图在指定用途下的封面图；未就绪时返回 false 并排队生成。
	bool Get(const char *pUrl, ERole Role, SImage *pOut);
	// 客户端关闭：释放全部 GPU 纹理（图形接口此刻仍存活）。
	void Shutdown(IGraphics *pGraphics);
	// 还有封面在排队/下载/合成（供性能日志与调试使用）。
	bool Loading() const;

	// 两种用途的固定输出尺寸（像素）。列表行缩略图约等于绘制高度的 2.4 倍，
	// 详情条约 2.5 倍，够清楚又不会让纹理缓存吃内存。
	static constexpr int ROW_WIDTH = 167;
	static constexpr int ROW_HEIGHT = 72;
	static constexpr int DETAILS_WIDTH = 278;
	static constexpr int DETAILS_HEIGHT = 120;

private:
	struct SKey
	{
		std::string m_Url;
		ERole m_Role = ERole::ROW;

		bool operator==(const SKey &Other) const
		{
			return m_Role == Other.m_Role && m_Url == Other.m_Url;
		}
	};

	struct SEntry
	{
		SKey m_Key;
		IGraphics::CTextureHandle m_Texture;
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

	// 每帧/总量预算：打开页面时一屏十几行，分帧补齐避免卡顿；纹理缓存要足够大，
	// 来回滚动时命中已有纹理而不是重新解码。
	inline static constexpr int MAX_DOWNLOADS = 2;
	inline static constexpr int MAX_JOBS = 6;
	inline static constexpr int MAX_QUEUED = 96;
	inline static constexpr int MAX_TEXTURES = 192;
	inline static constexpr int MAX_UPLOADS_PER_FRAME = 4;
	inline static constexpr int MAX_READS_PER_FRAME = 8;
	inline static constexpr float FAILURE_RETRY_SECONDS = 120.0f;
	inline static constexpr int MAX_RESPONSE_BYTES = 512 * 1024;

	static std::string MakeKeyString(const SKey &Key);
	static QmMapCover::SArt StyleForUrl(const char *pUrl, ERole Role);
	static void ComposeSize(ERole Role, int *pWidth, int *pHeight);

	void CancelPending();
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
