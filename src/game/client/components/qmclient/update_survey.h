#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SURVEY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SURVEY_H

#include "update_request.h"

#include <base/mem.h>

namespace qm_update
{
	// 先验证签名附件可达，再对实际包做有界前缀采样，不并行下载完整安装包。
	// 探测成功不构成真实性保证，安装前仍须完整签名、大小和哈希验证。
	class CSourceSurvey
	{
	public:
		static constexpr size_t SAMPLE_BYTES = 256 * 1024;
		using TSampleFactory = std::function<std::shared_ptr<IHttpRequest>(const std::string &, bool)>;

	private:
		struct CProbe
		{
			CSource m_Source;
			std::shared_ptr<IHttpRequest> m_pRequest;
			double m_Start = 0;
			double m_Latency = 0;
			bool m_Finished = false;
			bool m_Reachable = false;
			bool m_DirectRetried = false;
			bool m_Sampling = false;
			double m_Speed = 0;
		};
		std::vector<CProbe> m_vProbes;
		CUpdateRequest::TFactory m_DirectOfficialFactory;
		bool m_Running = false;
		std::string m_PackageUrl;
		TSampleFactory m_SampleFactory;

		bool StartSample(CProbe &Probe, double Now)
		{
			if(!m_SampleFactory)
				return false;
			Probe.m_Sampling = true;
			Probe.m_Reachable = false;
			Probe.m_Finished = false;
			Probe.m_Start = Now;
			Probe.m_pRequest = m_SampleFactory(Probe.m_Source.m_Prefix + m_PackageUrl, Probe.m_DirectRetried);
			return true;
		}

		bool ValidSample(const unsigned char *pBytes, size_t Size) const
		{
			if(!pBytes || Size < 6)
				return false;
			if(m_PackageUrl.ends_with(".7z"))
				return mem_comp(pBytes, "7z\xBC\xAF\x27\x1C", 6) == 0;
			if(m_PackageUrl.ends_with(".zip"))
				return mem_comp(pBytes, "PK\x03\x04", 4) == 0;
			return m_PackageUrl.ends_with(".exe") && pBytes[0] == 'M' && pBytes[1] == 'Z';
		}

	public:
		~CSourceSurvey() { Cancel(); }
		void Begin(std::vector<CSource> vSources, const std::string &OfficialSignatureUrl, double Now, const CUpdateRequest::TFactory &Factory, CUpdateRequest::TFactory DirectOfficialFactory = {}, const std::string &PackageUrl = {}, TSampleFactory SampleFactory = {})
		{
			Cancel();
			m_DirectOfficialFactory = std::move(DirectOfficialFactory);
			m_PackageUrl = PackageUrl;
			m_SampleFactory = std::move(SampleFactory);
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
					if(Now - Probe.m_Start >= (Probe.m_Sampling ? 8 : 10))
						Probe.m_pRequest->Abort();
					Complete = false;
					continue;
				}
				Probe.m_Finished = true;
				Probe.m_Latency = Now - Probe.m_Start;
				if(Probe.m_Sampling && Probe.m_pRequest && Probe.m_pRequest->CompletedStatusCode() == 200)
				{
					unsigned char *pBytes = nullptr;
					size_t Size = 0;
					Probe.m_pRequest->ResultResponseSample(&pBytes, &Size);
					Probe.m_Reachable = ValidSample(pBytes, Size);
					Probe.m_Speed = Size / std::max(0.001, Probe.m_Latency);
				}
				else if(!Probe.m_Sampling && Probe.m_pRequest && Probe.m_pRequest->State() == EHttpState::DONE && Probe.m_pRequest->StatusCode() == 200)
				{
					unsigned char *pBytes = nullptr;
					size_t Size = 0;
					Probe.m_pRequest->Result(&pBytes, &Size);
					Probe.m_Reachable = Size == 64;
					if(Probe.m_Reachable && StartSample(Probe, Now))
					{
						Complete = false;
						continue;
					}
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
						Probe.m_pRequest = Probe.m_Sampling ? m_SampleFactory(Url, true) : m_DirectOfficialFactory(Url);
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
			double BestMirrorSpeed = 0;
			for(const auto &Probe : vProbes)
				if(Probe.m_Reachable && !Probe.m_Source.m_Prefix.empty())
					BestMirrorSpeed = std::max(BestMirrorSpeed, Probe.m_Speed);
			std::stable_sort(vProbes.begin(), vProbes.end(), [BestMirrorSpeed](const CProbe &Left, const CProbe &Right) {
				// 官方低于 100 KiB/s 时优先可达镜像，其余情况仍优先官方。
				if(Left.m_Source.m_Prefix.empty() != Right.m_Source.m_Prefix.empty())
				{
					const auto &Official = Left.m_Source.m_Prefix.empty() ? Left : Right;
					const bool PreferOfficial = Official.m_Speed >= CDownloadSpeedMonitor::MIN_BYTES_PER_SECOND || BestMirrorSpeed <= 0;
					return PreferOfficial ? Left.m_Source.m_Prefix.empty() : !Left.m_Source.m_Prefix.empty();
				}
				if(Left.m_Speed != Right.m_Speed)
					return Left.m_Speed > Right.m_Speed;
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
		double Speed(const CSource *pSource) const
		{
			if(pSource)
				for(const auto &Probe : m_vProbes)
					if(Probe.m_Source.m_Prefix == pSource->m_Prefix)
						return Probe.m_Speed;
			return 0;
		}
		bool Direct(const CSource &Source) const
		{
			for(const auto &Probe : m_vProbes)
				if(Probe.m_Source.m_Prefix == Source.m_Prefix)
					return Probe.m_DirectRetried;
			return false;
		}
		void Cancel()
		{
			for(auto &Probe : m_vProbes)
				if(Probe.m_pRequest && !Probe.m_pRequest->Done())
					Probe.m_pRequest->Abort();
			m_vProbes.clear();
			m_DirectOfficialFactory = {};
			m_SampleFactory = {};
			m_PackageUrl.clear();
			m_Running = false;
		}
	};
}
#endif
