#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CUSTOM_COMMUNITY_INFO_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CUSTOM_COMMUNITY_INFO_H

#include <base/log.h>

#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <cstdlib>
#include <memory>

inline constexpr char QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE[] = "custom-communities-ddnet-info.json";
using TQmCommunityInfo = std::unique_ptr<json_value, decltype(&json_value_free)>;

// 加载时读旧缓存；刷新时只消费完成响应，避免主线程重新打开刚写入的文件。
inline TQmCommunityInfo QmLoadCustomCommunityInfo(IStorage *pStorage, const IHttpRequest *pResponse = nullptr)
{
	unsigned char *pData = nullptr;
	size_t Size = 0;
	std::unique_ptr<void, decltype(&free)> pFileData(nullptr, free);
	if(pResponse)
	{
		if(pResponse->State() != EHttpState::DONE)
			return {nullptr, json_value_free};
		pResponse->Result(&pData, &Size);
	}
	else
	{
		void *pBuffer = nullptr;
		unsigned Length = 0;
		if(!pStorage || !pStorage->ReadFile(QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE, IStorage::TYPE_SAVE, &pBuffer, &Length))
			return {nullptr, json_value_free};
		pFileData.reset(pBuffer);
		pData = static_cast<unsigned char *>(pBuffer);
		Size = Length;
	}
	if(!pData || Size == 0)
		return {nullptr, json_value_free};
	json_settings Settings{};
	char aError[256];
	TQmCommunityInfo pInfo(JsonParseEx(&Settings, reinterpret_cast<const json_char *>(pData), Size, aError), json_value_free);
	if(!pInfo || pInfo->type != json_object)
	{
		log_error("customcommunities", "invalid info: %s", pInfo ? "root must be an object" : aError);
		pInfo.reset();
	}
	return pInfo;
}

class CQmCustomCommunityCacheJob final : public IJob
{
	IStorage *m_pStorage;
	std::shared_ptr<IHttpRequest> m_pResponse;
	TQmCommunityInfo m_pInfo{nullptr, json_value_free};

protected:
	void Run() override
	{
		if(State() == STATE_ABORTED)
			return;
		m_pInfo = QmLoadCustomCommunityInfo(m_pStorage, m_pResponse.get());
	}

public:
	explicit CQmCustomCommunityCacheJob(IStorage *pStorage, std::shared_ptr<IHttpRequest> pResponse = nullptr) : m_pStorage(pStorage), m_pResponse(std::move(pResponse)) { Abortable(true); }
	TQmCommunityInfo TakeInfo() { return std::move(m_pInfo); }
};

#endif
