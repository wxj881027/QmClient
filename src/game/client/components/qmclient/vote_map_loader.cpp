#include "vote_map_loader.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/map.h>
#include <engine/shared/jobs.h>

#include <utility>

class CQmVoteMapLoader::CLoadJob : public IJob
{
	std::shared_ptr<IHttpRequest> m_pRequest;
	QmVoteMaps::SMap m_Map;
	bool m_Catalog;
	bool m_Axiom;

	void Run() override
	{
		unsigned char *pData = nullptr;
		size_t Length = 0;
		m_pRequest->Result(&pData, &Length);
		if(m_Catalog)
			m_Success = QmVoteMaps::ParseCatalog((const char *)pData, Length, m_vMaps);
		else if(m_Axiom)
			m_Success = QmVoteMaps::ParseAxiomDetails((const char *)pData, Length, m_Map, m_Details);
		else
			m_Success = QmVoteMaps::ParseDetails((const char *)pData, Length, m_Map, m_Details);
		m_pRequest.reset();
	}

public:
	std::vector<QmVoteMaps::SMap> m_vMaps;
	QmVoteMaps::SDetails m_Details;
	bool m_Success = false;
	CLoadJob(std::shared_ptr<IHttpRequest> pRequest, const QmVoteMaps::SMap &Map, bool Catalog, bool Axiom) :
		m_pRequest(std::move(pRequest)), m_Map(Map), m_Catalog(Catalog), m_Axiom(Axiom) {}
};

void CQmVoteMapLoader::Cancel(SRequest &Request)
{
	if(Request.m_pHttp)
		Request.m_pHttp->Abort();
	// Job 只拥有请求和结果，不持有菜单；丢弃句柄后旧结果无法发布。
	Request = {};
}

CQmVoteMapLoader::CQmVoteMapLoader(TRequestFactory CreateRequest) :
	m_CreateRequest(CreateRequest != nullptr ? CreateRequest : HttpGet)
{
}

void CQmVoteMapLoader::SetAxiomMode(EQmAxiomMode Mode)
{
	if(m_AxiomMode == Mode)
		return;
	Suspend();
	m_AxiomMode = Mode;
	++m_Revision;
}

void CQmVoteMapLoader::Suspend()
{
	Cancel(m_CatalogRequest);
	Cancel(m_DetailRequest);
	m_CatalogRequest.m_Attempted = !m_vMaps.empty();
	m_Selected = {};
	m_DetailsReady = false;
}

CQmVoteMapLoader::~CQmVoteMapLoader()
{
	Cancel(m_CatalogRequest);
	Cancel(m_DetailRequest);
}

void CQmVoteMapLoader::Start(SRequest &Request, IHttp *pHttp, const char *pUrl, int MaxBytes, const char *pApiKey)
{
	Cancel(Request);
	Request.m_Attempted = true;
	Request.m_pHttp = m_CreateRequest(pUrl);
	Request.m_pHttp->Timeout({5000, 20000, 1024, 10});
	Request.m_pHttp->MaxResponseSize(MaxBytes);
	Request.m_pHttp->LogProgress(HTTPLOG::FAILURE);
	if(pApiKey[0])
		Request.m_pHttp->Header((std::string("Authorization: Bearer ") + pApiKey).c_str());
	pHttp->Run(Request.m_pHttp);
}

void CQmVoteMapLoader::Refresh(IHttp *pHttp)
{
	if(m_AxiomMode != EQmAxiomMode::NONE)
	{
		Cancel(m_DetailRequest);
		m_DetailsReady = false;
		return;
	}
	Start(m_CatalogRequest, pHttp, "https://ddnet.org/releases/maps.json", 8 * 1024 * 1024);
}

bool CQmVoteMapLoader::Poll(SRequest &Request, IEngine *pEngine, bool Catalog)
{
	if(Request.m_pHttp && Request.m_pHttp->Done())
	{
		if(Request.m_pHttp->State() == EHttpState::DONE)
		{
			Request.m_pJob = std::make_shared<CLoadJob>(Request.m_pHttp, m_Selected, Catalog, m_AxiomMode != EQmAxiomMode::NONE);
			pEngine->AddJob(Request.m_pJob);
		}
		else
			Request.m_Error = true;
		Request.m_pHttp.reset();
	}
	if(!Request.m_pJob || Request.m_pJob->State() != IJob::STATE_DONE)
		return false;
	Request.m_Error = !Request.m_pJob->m_Success;
	if(!Request.m_Error)
	{
		if(Catalog)
		{
			m_vMaps = std::move(Request.m_pJob->m_vMaps);
			++m_Revision;
		}
		else
		{
			m_Details = Request.m_pJob->m_Details;
			m_DetailsReady = true;
		}
	}
	Request.m_pJob.reset();
	return !Request.m_Error;
}

void CQmVoteMapLoader::Select(const QmVoteMaps::SMap *pMap, float Now)
{
	if(pMap != nullptr ? (pMap->m_Name == m_Selected.m_Name && pMap->m_Category == m_Selected.m_Category) : m_Selected.m_Name.empty())
		return;
	Cancel(m_DetailRequest);
	m_Selected = pMap != nullptr ? *pMap : QmVoteMaps::SMap{};
	m_SelectedAt = Now;
	m_DetailsReady = false;
}

void CQmVoteMapLoader::Update(IHttp *pHttp, IEngine *pEngine, float Now, CQmAxiomRequestBudget *pBudget, const char *pApiKey)
{
	const bool Axiom = m_AxiomMode != EQmAxiomMode::NONE;
	if(Axiom && pBudget && m_DetailRequest.m_pHttp && m_DetailRequest.m_pHttp->Done() && m_DetailRequest.m_pHttp->CompletedStatusCode() == 429)
		pBudget->RateLimited(time_get());
	if(!Axiom && !m_CatalogRequest.m_Attempted)
		Refresh(pHttp);
	Poll(m_CatalogRequest, pEngine, true);
	Poll(m_DetailRequest, pEngine, false);
	// 键盘快速浏览时不为每个经过的条目发请求。
	if(!m_Selected.m_Name.empty() && (Axiom || !m_Selected.m_Category.empty()) && !m_DetailRequest.m_Attempted && Now - m_SelectedAt >= 0.3f)
	{
		char aEscaped[MAX_MAP_LENGTH * 3];
		EscapeUrl(aEscaped, m_Selected.m_Name.c_str());
		char aUrl[1024];
		if(Axiom)
		{
			if(!pBudget || !pBudget->TryStart(time_get(), pApiKey[0] != 0))
				return;
			str_format(aUrl, sizeof(aUrl), "https://api.axiom.teeworlds.cn/v1/open/map/info?mode=%s&map=%s", m_AxiomMode == EQmAxiomMode::GORES ? "gores" : "axrace", aEscaped);
			Start(m_DetailRequest, pHttp, aUrl, 2 * 1024 * 1024, pApiKey);
			return;
		}
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
		Start(m_DetailRequest, pHttp, aUrl, 2 * 1024 * 1024);
	}
}
