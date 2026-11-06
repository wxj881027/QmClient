#include "vote_map_loader.h"

#include <base/str.h>

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

	void Run() override
	{
		try
		{
			unsigned char *pData = nullptr;
			size_t Length = 0;
			m_pRequest->Result(&pData, &Length);
			m_Success = m_Catalog ? QmVoteMaps::ParseCatalog((const char *)pData, Length, m_vMaps) :
						QmVoteMaps::ParseDetails((const char *)pData, Length, m_Map, m_Details);
		}
		catch(...)
		{
			m_Success = false;
		}
		m_pRequest.reset();
	}

public:
	std::vector<QmVoteMaps::SMap> m_vMaps;
	QmVoteMaps::SDetails m_Details;
	bool m_Success = false;
	CLoadJob(std::shared_ptr<IHttpRequest> pRequest, const QmVoteMaps::SMap &Map, bool Catalog) :
		m_pRequest(std::move(pRequest)), m_Map(Map), m_Catalog(Catalog) {}
};

void CQmVoteMapLoader::Cancel(SRequest &Request) noexcept
{
	if(Request.m_pHttp)
	{
		try
		{
			Request.m_pHttp->Abort();
		}
		catch(...)
		{
		}
	}
	// Job 只拥有请求和结果，不持有菜单；丢弃句柄后旧结果无法发布。
	Request = {};
}

CQmVoteMapLoader::CQmVoteMapLoader(TRequestFactory CreateRequest) :
	m_CreateRequest(CreateRequest != nullptr ? CreateRequest : HttpGet)
{
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

void CQmVoteMapLoader::Start(SRequest &Request, IHttp *pHttp, const char *pUrl, int MaxBytes)
{
	Cancel(Request);
	Request.m_Attempted = true;
	try
	{
		if(pHttp == nullptr)
		{
			Request.m_Error = true;
			return;
		}
		Request.m_pHttp = m_CreateRequest(pUrl);
		if(!Request.m_pHttp)
		{
			Request.m_Error = true;
			return;
		}
		Request.m_pHttp->Timeout({5000, 20000, 1024, 10});
		Request.m_pHttp->MaxResponseSize(MaxBytes);
		Request.m_pHttp->LogProgress(HTTPLOG::FAILURE);
		pHttp->Run(Request.m_pHttp);
	}
	catch(...)
	{
		Cancel(Request);
		Request.m_Attempted = true;
		Request.m_Error = true;
	}
}

void CQmVoteMapLoader::Refresh(IHttp *pHttp)
{
	Start(m_CatalogRequest, pHttp, "https://ddnet.org/releases/maps.json", 8 * 1024 * 1024);
}

bool CQmVoteMapLoader::Poll(SRequest &Request, IEngine *pEngine, bool Catalog)
{
	if(Request.m_pHttp && Request.m_pHttp->Done())
	{
		if(Request.m_pHttp->State() == EHttpState::DONE)
		{
			try
			{
				auto pJob = std::make_shared<CLoadJob>(Request.m_pHttp, m_Selected, Catalog);
				if(pEngine == nullptr || !pEngine->TryAddJob(pJob))
				{
					Request.m_Error = true;
					return false;
				}
				Request.m_pJob = std::move(pJob);
				Request.m_Error = false;
			}
			catch(...)
			{
				// 保留已完成的响应，后续帧重试解析入队，不重下目录或挂住空作业。
				Request.m_Error = true;
				return false;
			}
		}
		else
			Request.m_Error = true;
		Request.m_pHttp.reset();
	}
	if(!Request.m_pJob || !Request.m_pJob->Finished())
		return false;
	Request.m_Error = Request.m_pJob->State() != IJob::STATE_DONE || !Request.m_pJob->m_Success;
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
	try
	{
		auto Selected = pMap != nullptr ? *pMap : QmVoteMaps::SMap{};
		Cancel(m_DetailRequest);
		m_Selected = std::move(Selected);
	}
	catch(...)
	{
		// 菜单已选中另一张图，失败时不暴露旧图统计；下次Select可重新准备。
		Cancel(m_DetailRequest);
		m_Selected = {};
		m_DetailRequest.m_Error = true;
	}
	m_SelectedAt = Now;
	m_DetailsReady = false;
}

void CQmVoteMapLoader::Update(IHttp *pHttp, IEngine *pEngine, float Now)
{
	if(!m_CatalogRequest.m_Attempted)
		Refresh(pHttp);
	Poll(m_CatalogRequest, pEngine, true);
	Poll(m_DetailRequest, pEngine, false);
	// 键盘快速浏览时不为每个经过的条目发请求。
	if(!m_Selected.m_Name.empty() && !m_Selected.m_Category.empty() && !m_DetailRequest.m_Attempted && Now - m_SelectedAt >= 0.3f)
	{
		char aEscaped[MAX_MAP_LENGTH * 3];
		EscapeUrl(aEscaped, m_Selected.m_Name.c_str());
		char aUrl[1024];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
		Start(m_DetailRequest, pHttp, aUrl, 2 * 1024 * 1024);
	}
}
