#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_JOB_H

#include <base/str.h>

#include <engine/http.h>

#include <game/client/components/qmclient/skin_download_session.h>
#include <game/client/components/qmclient/skin_prepared_textures.h>
#include <game/client/components/qmclient/skin_prepared_visuals.h>
#include <game/client/skin.h>

#include <ctime>
#include <memory>
#include <optional>

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

class CQmSkinSourceJob : public IQmSkinDataJob
{
public:
	explicit CQmSkinSourceJob(const char *pName)
	{
		str_copy(m_aName, pName);
		Abortable(true);
	}

	SQmSkinSourceData m_Data;
	bool HasData() const override { return m_Data.m_pPreparedTextures != nullptr; }

protected:
	char m_aName[MAX_SKIN_LENGTH];
};

// 只读取磁盘缓存或解码已完成响应；HTTP 生命周期与缓存替换由下载会话管理。
class CQmSkinDownloadJob : public CQmSkinSourceJob
{
public:
	using TPrepare = bool (*)(const char *, SQmSkinSourceData &);

	CQmSkinDownloadJob(IStorage *pStorage, const char *pName, TPrepare Prepare, std::shared_ptr<IHttpRequest> pResponse = nullptr);

protected:
	void Run() override;

private:
	IStorage *m_pStorage;
	TPrepare m_Prepare;
	// 主线程在构造时取得响应数据；保活请求，避免校验失败改变状态后再次调用 Result。
	std::shared_ptr<IHttpRequest> m_pResponse;
	const unsigned char *m_pResponseData = nullptr;
	size_t m_ResponseSize = 0;
};

#endif
