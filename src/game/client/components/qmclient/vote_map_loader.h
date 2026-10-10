#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MAP_LOADER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MAP_LOADER_H

#include "vote_map_library.h"

#include <cstdint>
#include <memory>

class IEngine;
class IHttp;
class IHttpRequest;

class CQmVoteMapLoader
{
	using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);
	TRequestFactory m_CreateRequest;
	class CLoadJob;
	struct SRequest
	{
		std::shared_ptr<IHttpRequest> m_pHttp;
		std::shared_ptr<CLoadJob> m_pJob;
		bool m_Attempted = false;
		bool m_Error = false;
	};
	SRequest m_CatalogRequest;
	SRequest m_DetailRequest;
	std::vector<QmVoteMaps::SMap> m_vMaps;
	QmVoteMaps::SMap m_Selected;
	QmVoteMaps::SDetails m_Details;
	float m_SelectedAt = 0.0f;
	uint64_t m_Revision = 0;
	bool m_DetailsReady = false;

	static void Cancel(SRequest &Request);
	void Start(SRequest &Request, IHttp *pHttp, const char *pUrl, int MaxBytes);
	bool Poll(SRequest &Request, IEngine *pEngine, bool Catalog);

public:
	explicit CQmVoteMapLoader(TRequestFactory CreateRequest = nullptr);
	~CQmVoteMapLoader();
	CQmVoteMapLoader(const CQmVoteMapLoader &) = delete;
	CQmVoteMapLoader &operator=(const CQmVoteMapLoader &) = delete;
	void Suspend();
	void Update(IHttp *pHttp, IEngine *pEngine, float Now);
	void Refresh(IHttp *pHttp);
	void Select(const QmVoteMaps::SMap *pMap, float Now);
	void RetryDetails() { m_DetailRequest.m_Attempted = false; }
	const std::vector<QmVoteMaps::SMap> &Maps() const { return m_vMaps; }
	uint64_t Revision() const { return m_Revision; }
	bool Loading() const { return m_CatalogRequest.m_pHttp != nullptr || m_CatalogRequest.m_pJob != nullptr; }
	bool Error() const { return m_CatalogRequest.m_Error; }
	bool DetailsError() const { return m_DetailRequest.m_Error; }
	const QmVoteMaps::SDetails *Details() const { return m_DetailsReady ? &m_Details : nullptr; }
};

#endif
