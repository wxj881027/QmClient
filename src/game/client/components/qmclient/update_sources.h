#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SOURCES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_SOURCES_H

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qm_update
{
	enum EResource : unsigned
	{
		RELEASE = 1,
		RAW = 2,
		API = 4,
	};

	struct CSource
	{
		std::string m_Prefix;
		std::string m_Group;
		int m_Priority;
		unsigned m_Resources;
		bool m_Enabled = true;
	};

	// 集中维护批准的服务；保守合并关联或独立运营情况尚未确认的节点。
	inline std::vector<CSource> BuiltinSources()
	{
		return {
			{"https://gh-proxy.com/", "gh-proxy", 10, RELEASE | RAW | API},
			{"https://gh-proxy.org/", "gh-proxy", 20, RELEASE | RAW | API},
			// TLS 验证未通过，未批准任何能力；重新验证后才可启用。
			{"https://cdn.gh-proxy.org/", "gh-proxy", 30, 0, false},
			{"https://ghfast.top/", "ghproxy", 40, RELEASE | RAW},
			{"https://ghproxy.net/", "ghproxy", 50, RELEASE | RAW},
		};
	}

	inline constexpr const char *MANUAL_DOWNLOAD_URL = "https://github.com/wxj881027/QmClient/releases";

	inline bool IsOfficialUrl(const std::string &Url, EResource Resource)
	{
		const std::string Prefix = Resource == API ? "https://api.github.com/repos/wxj881027/QmClient/releases" :
					   Resource == RAW ? "https://raw.githubusercontent.com/wxj881027/QmClient/" :
							     "https://github.com/wxj881027/QmClient/releases/download/";
		if(Url.compare(0, Prefix.size(), Prefix) != 0 || Url.find_first_of("\\\r\n\t #") != std::string::npos)
			return false;
		if(Resource == API && Url.size() > Prefix.size() && Url[Prefix.size()] != '/' && Url[Prefix.size()] != '?')
			return false;
		// URL 必须来自官方地址，不能把镜像 URL 再嵌套到另一代理。
		return Url.find("://", Prefix.size()) == std::string::npos && Url.find('@') == std::string::npos;
	}

	class CSourceRegistry
	{
		std::vector<CSource> m_vSources;
		std::unordered_map<std::string, double> m_DegradedUntil;
		std::string m_Recent;

	public:
		explicit CSourceRegistry(std::vector<CSource> vSources = BuiltinSources()) :
			m_vSources(std::move(vSources)) {}

		void SetRecent(const std::string &Prefix) { m_Recent = Prefix; }
		const std::string &Recent() const { return m_Recent; }

		std::vector<CSource> Candidates(const std::string &OfficialUrl, EResource Resource, double Now) const
		{
			if(!IsOfficialUrl(OfficialUrl, Resource))
				return {};
			auto vSorted = m_vSources;
			std::stable_sort(vSorted.begin(), vSorted.end(), [&](const CSource &Left, const CSource &Right) {
				if((Left.m_Prefix == m_Recent) != (Right.m_Prefix == m_Recent))
					return Left.m_Prefix == m_Recent;
				return Left.m_Priority < Right.m_Priority;
			});
			std::unordered_set<std::string> Groups;
			std::unordered_set<std::string> Urls;
			std::vector<CSource> vResult;
			// 检测先使用系统代理或直连访问官方，失败后才换镜像；限流仍遵守冷却。
			const auto OfficialDegraded = m_DegradedUntil.find("github");
			if(OfficialDegraded == m_DegradedUntil.end() || Now >= OfficialDegraded->second)
				vResult.push_back({"", "github", 0, RELEASE | RAW | API});
			for(const auto &Source : vSorted)
			{
				const auto Degraded = m_DegradedUntil.find(Source.m_Group);
				if(!Source.m_Enabled || !(Source.m_Resources & Resource) || Source.m_Prefix.empty() || Source.m_Group.empty() ||
					(Degraded != m_DegradedUntil.end() && Now < Degraded->second) ||
					Groups.count(Source.m_Group) || Urls.count(Source.m_Prefix + OfficialUrl))
					continue;
				Groups.insert(Source.m_Group);
				Urls.insert(Source.m_Prefix + OfficialUrl);
				vResult.push_back(Source);
			}
			return vResult;
		}

		void Failed(const CSource &Source, double Now, double RetryAfter = 0)
		{
			if(Source.m_Group == "github" && RetryAfter <= 0)
				return;
			m_DegradedUntil[Source.m_Group] = std::max(m_DegradedUntil[Source.m_Group], Now + std::max(300.0, RetryAfter));
		}
		void Succeeded(const CSource &Source)
		{
			m_Recent = Source.m_Prefix;
			m_DegradedUntil.erase(Source.m_Group);
		}
	};

	// 每个操作固定候选快照，每组至多一次；取消与耗尽后不再切换。
	class CSourceAttempt
	{
		std::vector<CSource> m_vSources;
		size_t m_Index = 0;
		bool m_Cancelled = false;

	public:
		void Begin(std::vector<CSource> vSources)
		{
			m_vSources = std::move(vSources);
			m_Index = 0;
			m_Cancelled = false;
		}
		const CSource *Current() const
		{
			return !m_Cancelled && m_Index < m_vSources.size() ? &m_vSources[m_Index] : nullptr;
		}
		bool Next()
		{
			if(Current())
				++m_Index;
			return Current() != nullptr;
		}
		void Cancel() { m_Cancelled = true; }
	};

	// 使用传输字节变化监控首字节和无进展时间，不给正常大包设置总超时。
	class CProgressDeadline
	{
		double m_LastProgress = 0;
		double m_Bytes = 0;

	public:
		void Begin(double Now)
		{
			m_LastProgress = Now;
			m_Bytes = 0;
		}
		bool Expired(double Now, double Bytes, double FirstByteSeconds = 10, double IdleSeconds = 20)
		{
			if(Bytes > m_Bytes)
			{
				m_Bytes = Bytes;
				m_LastProgress = Now;
			}
			return Now - m_LastProgress >= (m_Bytes == 0 ? FirstByteSeconds : IdleSeconds);
		}
	};
}
#endif
