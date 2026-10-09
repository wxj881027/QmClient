#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_JOB_H

#include <base/lock.h>
#include <base/str.h>

#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <game/client/components/qmclient/skin_prepared_textures.h>
#include <game/client/components/qmclient/skin_prepared_visuals.h>
#include <game/client/skin.h>

#include <ctime>
#include <memory>
#include <optional>
#include <string>

class IStorage;

// 后台任务独占像素，任务完成后由主线程移动结果并上传纹理。
struct SQmSkinSourceData
{
	CImageInfo m_Info;
	CImageInfo m_InfoGrayscale;
	CSkin::CSkinMetrics m_Metrics;
	ColorRGBA m_BloodColor;
	SQmPreparedSkinVisuals m_PreparedVisuals;
	std::unique_ptr<CQmPreparedSkinTextures> m_pPreparedTextures;
	size_t m_SourceWidth = 0;
	size_t m_SourceHeight = 0;
	std::optional<time_t> m_LastModified;

	void ReadLastModified(IStorage *pStorage, const char *pPath, int StorageType);

	SQmSkinSourceData() = default;
	SQmSkinSourceData(SQmSkinSourceData &&) = default;
	SQmSkinSourceData &operator=(SQmSkinSourceData &&) = default;
	~SQmSkinSourceData()
	{
		m_Info.Free();
		m_InfoGrayscale.Free();
	}
};

struct SQmSkinSourceIdentity
{
	bool m_Download;
	int m_StorageType;
	time_t m_LastModified;

	bool operator==(const SQmSkinSourceIdentity &Other) const
	{
		return m_Download == Other.m_Download && m_StorageType == Other.m_StorageType && m_LastModified == Other.m_LastModified;
	}
};

class CQmSkinSourceJob : public IJob
{
public:
	explicit CQmSkinSourceJob(const char *pName)
	{
		str_copy(m_aName, pName);
		Abortable(true);
	}

	SQmSkinSourceData m_Data;
	bool m_NotFound = false;
	bool m_UsedCachedSkin = false;
	bool m_NotModified = false;

protected:
	char m_aName[MAX_SKIN_LENGTH];
};

// 缓存任务立即返回；更新任务独立等待网络，不阻塞已发布的皮肤。
class CQmSkinDownloadJob : public CQmSkinSourceJob
{
public:
	using TPrepare = bool (*)(const char *, SQmSkinSourceData &);
	using TCreateRequest = std::unique_ptr<IHttpRequest> (*)(const char *);

	CQmSkinDownloadJob(IStorage *pStorage, IHttp *pHttp, const char *pName, const char *pBaseUrl, TPrepare Prepare, bool UseCache = true, TCreateRequest CreateRequest = CreateHttpRequest);
	bool Abort() override REQUIRES(!m_Lock);
	void StartUpdate() REQUIRES(!m_Lock);
	bool DownloadReady() REQUIRES(!m_Lock);

protected:
	void Run() override REQUIRES(!m_Lock);

private:
	IStorage *m_pStorage;
	IHttp *m_pHttp;
	std::string m_BaseUrl;
	TPrepare m_Prepare;
	bool m_UseCache;
	TCreateRequest m_CreateRequest;
	CLock m_Lock;
	std::shared_ptr<IHttpRequest> m_pGetRequest GUARDED_BY(m_Lock);

	std::shared_ptr<IHttpRequest> Download(const char *pUrl, const char *pPath, bool SkipByFileTime, bool Wait = true) REQUIRES(!m_Lock);
};

#endif
