#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SURVEY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SURVEY_H

#include "update_request.h"

namespace qm_update
{
	// 只请求每组的小型签名附件测可达性与延迟，不并行下载完整安装包。
	// 探测成功不构成真实性保证，安装前仍须完整签名、大小和哈希验证。
	class CSourceSurvey
	{
		struct CProbe
		{
			CSource m_Source;
			std::shared_ptr<IHttpRequest> m_pRequest;
			double m_Start = 0;
			double m_Latency = 0;
			bool m_Finished = false;
			bool m_Reachable = false;
		};
		std::vector<CProbe> m_vProbes;
		std::vector<CSource> m_vFallback;
		bool m_Running = false;

	public:
		~CSourceSurvey() { Cancel(); }
		void Begin(std::vector<CSource> vSources, const std::string &OfficialSignatureUrl, double Now, const CUpdateRequest::TFactory &Factory)
		{
			Cancel();
			m_vFallback.clear();
			for(const auto &Source : vSources)
			{
				if(Source.m_Prefix.empty())
					m_vFallback.push_back(Source);
				else
					m_vProbes.push_back({Source, Factory(Source.m_Prefix + OfficialSignatureUrl), Now});
			}
			m_Running = !m_vProbes.empty();
		}
		bool Poll(CSourceRegistry &Sources, double Now)
		{
			if(!m_Running)
				return true;
			bool Complete = true;
			for(auto &Probe : m_vProbes)
			{
				if(Probe.m_Finished)
					continue;
				if(Probe.m_pRequest && !Probe.m_pRequest->Done())
				{
					if(Now - Probe.m_Start >= 10)
						Probe.m_pRequest->Abort();
					Complete = false;
					continue;
				}
				Probe.m_Finished = true;
				Probe.m_Latency = Now - Probe.m_Start;
				if(Probe.m_pRequest && Probe.m_pRequest->State() == EHttpState::DONE && Probe.m_pRequest->StatusCode() == 200)
				{
					unsigned char *pBytes = nullptr;
					size_t Size = 0;
					Probe.m_pRequest->Result(&pBytes, &Size);
					Probe.m_Reachable = Size == 64;
				}
				if(!Probe.m_Reachable)
					Sources.Failed(Probe.m_Source, Now, SourceRetryDelay(Probe.m_pRequest.get()));
			}
			if(Complete)
				m_Running = false;
			return !m_Running;
		}
		std::vector<CSource> Candidates() const
		{
			if(m_Running)
				return {};
			auto vProbes = m_vProbes;
			std::stable_sort(vProbes.begin(), vProbes.end(), [](const CProbe &Left, const CProbe &Right) { return Left.m_Latency < Right.m_Latency; });
			std::vector<CSource> Result;
			for(const auto &Probe : vProbes)
				if(Probe.m_Reachable)
					Result.push_back(Probe.m_Source);
			Result.insert(Result.end(), m_vFallback.begin(), m_vFallback.end());
			return Result;
		}
		bool Running() const { return m_Running; }
		void Cancel()
		{
			for(auto &Probe : m_vProbes)
				if(Probe.m_pRequest && !Probe.m_pRequest->Done())
					Probe.m_pRequest->Abort();
			m_vProbes.clear();
			m_vFallback.clear();
			m_Running = false;
		}
	};
}
#endif
