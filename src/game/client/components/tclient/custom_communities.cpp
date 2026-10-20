#include "custom_communities.h"

#include <base/log.h>
#include <base/perf_timer.h>

#include <engine/client/serverbrowser.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/custom_community_info.h>
#include <game/client/components/qmclient/perf_logging.h>

void CCustomCommunities::DownloadCustomCommunitiesDDNetInfo()
{
	if(m_pCustomCommunitiesDDNetInfoTask)
		m_pCustomCommunitiesDDNetInfoTask->Abort();
	m_pCustomCommunitiesDDNetInfoTask.reset();
	if(m_pCustomCommunitiesCacheJob)
		m_pCustomCommunitiesCacheJob->Abort();
	m_pCustomCommunitiesCacheJob.reset();
	if(m_pCustomCommunitiesResponseJob)
		m_pCustomCommunitiesResponseJob->Abort();
	m_pCustomCommunitiesResponseJob.reset();

	if(g_Config.m_QmCustomCommunitiesUrl[0] == '\0')
	{
		auto *pServerBrowser = dynamic_cast<CServerBrowser *>(ServerBrowser());
		dbg_assert(pServerBrowser, "pServerBrowser is nullptr");
		pServerBrowser->m_CustomCommunitiesFunction = nullptr;
		pServerBrowser->LoadDDNetServers();
		return;
	}
	else
	{
		// 加载页同样需要持续响应输入；旧缓存的磁盘读取和解析只能在后台进行。
		m_pCustomCommunitiesCacheJob = std::make_shared<CQmCustomCommunityCacheJob>(Storage());
		Engine()->AddJob(m_pCustomCommunitiesCacheJob);
		m_pCustomCommunitiesDDNetInfoTask = HttpGetBoth(g_Config.m_QmCustomCommunitiesUrl, Storage(), QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE, IStorage::TYPE_SAVE);
		m_pCustomCommunitiesDDNetInfoTask->ValidateBeforeOverwrite(true);
		m_pCustomCommunitiesDDNetInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
		m_pCustomCommunitiesDDNetInfoTask->SkipByFileTime(false); // Always re-download.
		// Use ipv4 so we can know the ingame ip addresses of players before they join game servers
		m_pCustomCommunitiesDDNetInfoTask->IpResolve(IPRESOLVE::V4);
		Http()->Run(m_pCustomCommunitiesDDNetInfoTask);
	}
}

void CCustomCommunities::ApplyCustomCommunitiesInfo(TQmCommunityInfo pInfo)
{
	auto *pServerBrowser = dynamic_cast<CServerBrowser *>(ServerBrowser());
	dbg_assert(pServerBrowser, "pServerBrowser is nullptr");

	// Apply new data
	CPerfTimer ApplyTimer(QmPerfEnabled());
	if(m_pCustomCommunitiesDDNetInfo)
		json_value_free(m_pCustomCommunitiesDDNetInfo);
	m_pCustomCommunitiesDDNetInfo = pInfo.release();
	pServerBrowser->m_CustomCommunitiesFunction = [&](std::vector<json_value *> &vCommunities) {
		const json_value &Communities = (*m_pCustomCommunitiesDDNetInfo)["communities"];
		if(Communities.type == json_array)
		{
			vCommunities.insert(
				vCommunities.end(),
				Communities.u.array.values,
				Communities.u.array.values + Communities.u.array.length);
		}
	};
	pServerBrowser->LoadDDNetServers();
	QmPerfLogStage("perf/custom_communities", "apply", ApplyTimer.ElapsedMs(), false, Client());
}

void CCustomCommunities::OnInit()
{
	DownloadCustomCommunitiesDDNetInfo();
}

void CCustomCommunities::OnConsoleInit()
{
	Console()->Chain(
		"qm_custom_communities_url", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			if(pResult->NumArguments() > 0)
				((CCustomCommunities *)pUserData)->DownloadCustomCommunitiesDDNetInfo();
		},
		this);
}

void CCustomCommunities::OnRender()
{
	if(m_pCustomCommunitiesCacheJob && m_pCustomCommunitiesCacheJob->State() == IJob::STATE_DONE)
	{
		if(auto pInfo = m_pCustomCommunitiesCacheJob->TakeInfo())
		{
			ApplyCustomCommunitiesInfo(std::move(pInfo));
			if(QmPerfEnabled())
				QmPerfLogPayloadForce("perf/custom_communities", "event=cache_applied", Client());
		}
		m_pCustomCommunitiesCacheJob = nullptr;
	}
	if(m_pCustomCommunitiesResponseJob && m_pCustomCommunitiesResponseJob->State() == IJob::STATE_DONE)
	{
		if(auto pInfo = m_pCustomCommunitiesResponseJob->TakeInfo())
		{
			ApplyCustomCommunitiesInfo(std::move(pInfo));
			if(QmPerfEnabled())
				QmPerfLogPayloadForce("perf/custom_communities", "event=response_applied", Client());
			// 仅有效新响应才能替代旧缓存；损坏响应不能丢弃仍在读取的可用缓存。
			if(m_pCustomCommunitiesCacheJob)
				m_pCustomCommunitiesCacheJob->Abort();
			m_pCustomCommunitiesCacheJob.reset();
		}
		m_pCustomCommunitiesResponseJob.reset();
	}
	if(m_pCustomCommunitiesDDNetInfoTask)
	{
		const auto State = m_pCustomCommunitiesDDNetInfoTask->State();
		if(State != EHttpState::RUNNING && State != EHttpState::QUEUED)
		{
			// 失败保留已经加载的社区；成功只消费内存响应，不在交互帧重读缓存。
			if(State == EHttpState::DONE)
			{
				m_pCustomCommunitiesResponseJob = std::make_shared<CQmCustomCommunityCacheJob>(Storage(), m_pCustomCommunitiesDDNetInfoTask, true);
				Engine()->AddJob(m_pCustomCommunitiesResponseJob);
			}
			m_pCustomCommunitiesDDNetInfoTask = nullptr;
		}
	}
}

CCustomCommunities::~CCustomCommunities()
{
	if(m_pCustomCommunitiesResponseJob)
		m_pCustomCommunitiesResponseJob->Abort();
	if(m_pCustomCommunitiesDDNetInfoTask)
	{
		m_pCustomCommunitiesDDNetInfoTask->Abort();
		m_pCustomCommunitiesDDNetInfoTask = nullptr;
	}
	if(m_pCustomCommunitiesCacheJob)
	{
		m_pCustomCommunitiesCacheJob->Abort();
		m_pCustomCommunitiesCacheJob = nullptr;
	}
	if(m_pCustomCommunitiesDDNetInfo)
		json_value_free(m_pCustomCommunitiesDDNetInfo);
}
