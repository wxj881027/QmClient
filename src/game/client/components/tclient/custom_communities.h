#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_CUSTOM_COMMUNITIES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_CUSTOM_COMMUNITIES_H

#include <engine/http.h>
#include <engine/shared/console.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/custom_community_info.h>

class CCustomCommunities : public CComponent
{
private:
	std::shared_ptr<IHttpRequest> m_pCustomCommunitiesDDNetInfoTask = nullptr;
	std::shared_ptr<CQmCustomCommunityCacheJob> m_pCustomCommunitiesCacheJob = nullptr;
	std::shared_ptr<CQmCustomCommunityCacheJob> m_pCustomCommunitiesResponseJob = nullptr;
	json_value *m_pCustomCommunitiesDDNetInfo = nullptr;
	void DownloadCustomCommunitiesDDNetInfo();
	void ApplyCustomCommunitiesInfo(TQmCommunityInfo pInfo);

public:
	void OnInit() override;
	void OnConsoleInit() override;
	void OnRender() override;
	int Sizeof() const override { return sizeof(*this); }
	~CCustomCommunities() override;
};

#endif
