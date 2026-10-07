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
			bool m_DirectRetried = false;
		};
		std::vector<CProbe> m_vProbes;
		CUpdateRequest::TFactory m_DirectOfficialFactory;
		bool m_Running = false;

	public:
		~CSourceSurvey() { Cancel(); }
		void Begin(std::vector<CSource> vSources, const std::string &OfficialSignatureUrl, double Now, const CUpdateRequest::TFactory &Factory, CUpdateRequest::TFactory DirectOfficialFactory = {})
		{
			Cancel();
			m_DirectOfficialFactory = std::move(DirectOfficialFactory);
			for(const auto &Source : vSources)
				m_vProbes.push_back({Source, Factory(Source.m_Prefix + OfficialSignatureUrl), Now});
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
				{
					const std::shared_ptr<IHttpRequest> apRequests[] = {Probe.m_pRequest};
					if(m_DirectOfficialFactory && CanRetryOfficialDirect(&Probe.m_Source, Probe.m_DirectRetried, apRequests))
					{
						const std::string Url = Probe.m_pRequest->Url();
						Probe.m_DirectRetried = true;
						Probe.m_Finished = false;
						Probe.m_Start = Now;
						Probe.m_pRequest = m_DirectOfficialFactory(Url);
						Complete = false;
						continue;
					}
					Sources.Failed(Probe.m_Source, Now, SourceRetryDelay(Probe.m_pRequest.get()));
				}
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
			std::stable_sort(vProbes.begin(), vProbes.end(), [](const CProbe &Left, const CProbe &Right) {
				// 官方可达时不被更快的镜像抢占；镜像内部仍按实际延迟排序。
				if(Left.m_Source.m_Prefix.empty() != Right.m_Source.m_Prefix.empty())
					return Left.m_Source.m_Prefix.empty();
				return Left.m_Latency < Right.m_Latency;
			});
			std::vector<CSource> Result;
			for(const auto &Probe : vProbes)
				if(Probe.m_Reachable)
					Result.push_back(Probe.m_Source);
			return Result;
		}
		// PAC/系统代理的解析预算独立计算，移交 HTTP 后才计传输超时。
		void BeginTransfer(const std::shared_ptr<IHttpRequest> &Request, double Now)
		{
			for(auto &Probe : m_vProbes)
				if(Probe.m_pRequest == Request && !Probe.m_Finished)
					Probe.m_Start = Now;
		}
		bool Running() const { return m_Running; }
		void Cancel()
		{
			for(auto &Probe : m_vProbes)
				if(Probe.m_pRequest && !Probe.m_pRequest->Done())
					Probe.m_pRequest->Abort();
			m_vProbes.clear();
			m_DirectOfficialFactory = {};
			m_Running = false;
		}
	};
}
#endif
