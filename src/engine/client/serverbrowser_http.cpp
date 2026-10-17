#include "serverbrowser_http.h"

#include "qm_serverlist_cache.h"
#include "serverbrowser_http_parse.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdio>
#if defined(CONF_QMCLIENT_TEST_STORAGE)
#include <cstdlib>
#endif
#include <deque>
#include <exception>
#include <mutex>
#include <string>

namespace
{
	constexpr int MAX_URLS = 16;
	constexpr size_t MAX_BODY = 8 * 1024 * 1024;
	constexpr int MAX_CACHE_AGE = 24 * 60 * 60;
	constexpr const char *MIRROR_URL = "https://ddrace.cn/ddnet/15/servers.json";
	constexpr const char *SNAPSHOT_URL = "https://raw.githubusercontent.com/wxj881027/QmClient/serverlist-snapshot/servers.json";

	// 失败报告也可能分配内存，不能让日志异常再次逃出任务入口。
	void ReportServerListFailure(const char *pOperation, const char *pReason) noexcept
	{
		try
		{
			log_error("serverbrowser_http", "%s: %s", pOperation, pReason);
		}
		catch(...)
		{
		}
	}

	// 解析任务只持有响应与解析函数，取消后的旧任务没有浏览器或存储的引用。
	class CServerListParseJob : public IJob
	{
		std::shared_ptr<IHttpRequest> m_pResponse;
		bool m_Snapshot;
		FServerListParser m_ParseList;

		bool Parse()
		{
			std::unique_ptr<json_value, decltype(&json_value_free)> pJson(m_pResponse->ResultJson(), json_value_free);
			std::vector<CServerInfo> vServers;
			if(!pJson || m_ParseList(pJson.get(), &vServers) || vServers.empty())
				return false;
			int64_t Age = m_pResponse->ResultAgeSeconds().value_or(301);
			if(m_Snapshot)
			{
				const json_value &Updated = (*pJson)["qm_snapshot_updated_at"];
				const int64_t Now = time_timestamp();
				if(Updated.type != json_integer || Updated.u.integer < Now - MAX_CACHE_AGE || Updated.u.integer > Now + 300)
					return false;
				Age = std::max<int64_t>(m_pResponse->ResultAgeSeconds().value_or(0), std::max<int64_t>(0, Now - Updated.u.integer));
			}
			if(Age < 0 || Age > MAX_CACHE_AGE)
				return false;
			pJson.reset();
			unsigned char *pBody;
			size_t Size;
			m_pResponse->Result(&pBody, &Size);
			std::string Body(reinterpret_cast<const char *>(pBody), Size);
			// 正文和解析结果均准备完成后才发布，异常时局部资源自动释放。
			m_vServers = std::move(vServers);
			m_Body = std::move(Body);
			m_Age = Age;
			return true;
		}

	protected:
		void Run() override
		{
			try
			{
				m_Success = Parse();
			}
			catch(const std::exception &Error)
			{
				ReportServerListFailure("serverlist parsing failed", Error.what());
			}
			catch(...)
			{
				ReportServerListFailure("serverlist parsing failed", "unknown exception");
			}
			m_pResponse.reset();
		}

	public:
		CServerListParseJob(std::shared_ptr<IHttpRequest> pResponse, bool Snapshot, FServerListParser ParseList) :
			m_pResponse(std::move(pResponse)), m_Snapshot(Snapshot), m_ParseList(std::move(ParseList)) {}
		bool m_Success = false;
		int m_Age = 0;
		std::string m_Body;
		std::vector<CServerInfo> m_vServers;
	};

	// worker 持有共享的最新版本待写状态；浏览器关闭后仍会串行排空，不依赖主线程轮询。
	struct CServerListSaveState
	{
		std::mutex m_Lock;
		std::string m_Path;
		std::string m_Pending;
		bool m_WorkerActive = false;
		std::function<bool(const std::string &, const std::string &)> m_Writer;
	};
	bool WriteServerListCache(const std::string &Path, const std::string &Body)
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		IStorage::FormatTmpPath(aTmp, sizeof(aTmp), Path.c_str());
		IOHANDLE File = io_open(aTmp, IOFLAG_WRITE);
		if(!File)
			return false;
		const bool Written = io_write(File, Body.data(), Body.size()) == Body.size();
		const int Closed = io_close(File);
		return Written && Closed == 0 && fs_rename(aTmp, Path.c_str(), false) == 0;
	}
	class CServerListSaveJob : public IJob
	{
		std::shared_ptr<CServerListSaveState> m_pState;

	protected:
		void Run() override
		{
			while(true)
			{
				std::string Path;
				std::string Body;
				{
					std::lock_guard Lock(m_pState->m_Lock);
					if(m_pState->m_Pending.empty())
					{
						m_pState->m_WorkerActive = false;
						return;
					}
					// 锁内只交换所有权，不因路径或正文复制分配内存。
					Path.swap(m_pState->m_Path);
					Body.swap(m_pState->m_Pending);
				}
				try
				{
					if(!m_pState->m_Writer(Path, Body))
						ReportServerListFailure("serverlist save failed", "writer returned failure");
				}
				catch(const std::exception &Error)
				{
					ReportServerListFailure("serverlist save failed", Error.what());
				}
				catch(...)
				{
					ReportServerListFailure("serverlist save failed", "unknown exception");
				}
			}
		}

	public:
		explicit CServerListSaveJob(std::shared_ptr<CServerListSaveState> pState) : m_pState(std::move(pState)) {}
	};

	class CServerBrowserHttp : public IServerBrowserHttp
	{
		struct CAttempt
		{
			int m_Index;
			bool m_DnsFallback;
			std::shared_ptr<IHttpRequest> m_pRequest;
			std::shared_ptr<CServerListParseJob> m_pParse;
		};
		IEngine *m_pEngine;
		IStorage *m_pStorage;
		IHttp *m_pHttp;
		std::vector<std::string> m_vUrls;
		std::function<std::shared_ptr<IHttpRequest>(const char *)> m_Factory;
		std::function<int64_t()> m_Clock;
		FServerListParser m_ParseList;
		std::deque<std::pair<int, bool>> m_Queue;
		std::vector<CAttempt> m_vActive;
		CQmServerListCache<CServerInfo> m_Cache;
		std::string m_BestUrl;
		std::string m_SourceHash;
		uint64_t m_Revision = 0;
		int64_t m_RetryAt = 0;
		int m_Failures = 0;
		bool m_Running = false;
		bool m_HadSuccess = false;
		bool m_Shutdown = false;
		std::shared_ptr<CServerListSaveState> m_pSaveState;
		std::vector<int64_t> m_vBlockedUntil;
		bool m_UseFallbackTiers;
		int m_Tier = 0;
		int64_t m_TierStartedAt = 0;
		std::function<int(int)> m_RetryDelay;

		int Tier(const std::string &Url) const
		{
			if(!m_UseFallbackTiers)
				return 0;
			return Url == SNAPSHOT_URL ? 2 : (Url == MIRROR_URL ? 1 : 0);
		}
		void QueueTier()
		{
			for(size_t Index = 0; Index < m_vUrls.size(); ++Index)
				if(Tier(m_vUrls[Index]) == m_Tier)
					m_Queue.emplace_back(Index, false);
		}
		void Cancel()
		{
			for(auto &Attempt : m_vActive)
				if(Attempt.m_pRequest)
					Attempt.m_pRequest->Abort();
			m_vActive.clear();
			m_Queue.clear();
			m_Running = false;
		}
		void StartRequest(int Index, bool DnsFallback)
		{
			auto pRequest = m_Factory(m_vUrls[Index].c_str());
			if(DnsFallback && !pRequest->ServerListDnsFallback())
				return;
			const bool EncryptedDns = pRequest->ServerListDnsFallbackEnabled();
			// Windows 吊销信息离线时尽力检查；保留目标和 DoH 的证书及主机名验证。
			if(EncryptedDns)
				pRequest->RevocationBestEffort();
			pRequest->Timeout({EncryptedDns ? 15000 : 6000, m_UseFallbackTiers && m_Tier == 0 && !EncryptedDns ? 15000 : 30000, 1024, 10});
			pRequest->MaxResponseSize(MAX_BODY);
			pRequest->LogProgress(HTTPLOG::FAILURE);
			log_info("serverbrowser_http", "fetching serverlist, url='%s' dns=%s", pRequest->Url(), EncryptedDns ? "encrypted" : "system");
			// 先登记取消入口，再提交给 HTTP；提交失败只撤销本次尝试。
			m_vActive.push_back({Index, DnsFallback, std::move(pRequest), nullptr});
			try
			{
				m_pHttp->Run(m_vActive.back().m_pRequest);
			}
			catch(...)
			{
				m_vActive.back().m_pRequest->Abort();
				m_vActive.pop_back();
				throw;
			}
		}
		void Fill()
		{
			while(m_vActive.size() < 2 && !m_Queue.empty())
			{
				const auto [Index, DnsFallback] = m_Queue.front();
				m_Queue.pop_front();
				if(m_vBlockedUntil[Index] > m_Clock())
					continue;
				try
				{
					StartRequest(Index, DnsFallback);
				}
				catch(const std::exception &Error)
				{
					ReportServerListFailure("serverlist request scheduling failed", Error.what());
				}
				catch(...)
				{
					ReportServerListFailure("serverlist request scheduling failed", "unknown exception");
				}
			}
		}
		void Failed(const CAttempt &Attempt)
		{
			// HTTP 拒绝或 Retry-After 不应通过换解析绕过；恢复只处理无响应网络失败。
			if(Attempt.m_pRequest)
			{
				const auto RetryAfter = Attempt.m_pRequest->ResultRetryAfterSeconds();
				if(RetryAfter && *RetryAfter > 0)
				{
					const int64_t Now = m_Clock();
					m_vBlockedUntil[Attempt.m_Index] = Now + std::min<int64_t>(*RetryAfter, INT64_MAX - Now);
				}
				if(Tier(m_vUrls[Attempt.m_Index]) == 1 && Attempt.m_pRequest->CompletedStatusCode() == 503)
					m_vBlockedUntil[Attempt.m_Index] = std::max(m_vBlockedUntil[Attempt.m_Index], m_Clock() + 60);
				if(Attempt.m_pRequest->CompletedStatusCode() != 0 || Attempt.m_pRequest->State() == EHttpState::ABORTED)
					return;
			}
			if(!Attempt.m_DnsFallback && Attempt.m_pRequest && Attempt.m_pRequest->ServerListDnsFallback())
				m_Queue.emplace_back(Attempt.m_Index, true);
		}
		void Save(const std::string &Body, int Age)
		{
			if(!m_pStorage || Body.size() > MAX_BODY)
				return;
			try
			{
				m_pStorage->CreateFolder("cache", IStorage::TYPE_SAVE);
				char aPath[IO_MAX_PATH_LENGTH];
				m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "cache/qm-serverlist.json", aPath, sizeof(aPath));
				char aHeader[160];
				str_format(aHeader, sizeof(aHeader), "QMSL1 %lld %d %s\n", (long long)time_timestamp(), Age, m_SourceHash.c_str());
				std::string Path(aPath);
				std::string Pending = std::string(aHeader) + Body;
				std::shared_ptr<CServerListSaveJob> pJob;
				{
					std::lock_guard Lock(m_pSaveState->m_Lock);
					// 先分配任务，再接管待写内容并标记活跃，分配失败不锁死后续保存。
					if(!m_pSaveState->m_WorkerActive)
						pJob = std::make_shared<CServerListSaveJob>(m_pSaveState);
					m_pSaveState->m_Path.swap(Path);
					m_pSaveState->m_Pending.swap(Pending);
					if(pJob)
						m_pSaveState->m_WorkerActive = true;
				}
				if(pJob)
				{
					try
					{
						m_pEngine->AddJob(std::move(pJob));
					}
					catch(...)
					{
						std::lock_guard Lock(m_pSaveState->m_Lock);
						m_pSaveState->m_WorkerActive = false;
						throw;
					}
				}
			}
			catch(const std::exception &Error)
			{
				ReportServerListFailure("serverlist save scheduling failed", Error.what());
			}
			catch(...)
			{
				ReportServerListFailure("serverlist save scheduling failed", "unknown exception");
			}
		}

		void LoadCache()
		{
			if(!m_pStorage)
				return;
			std::unique_ptr<void, decltype(&io_close)> pFile(m_pStorage->OpenFile("cache/qm-serverlist.json", IOFLAG_READ, IStorage::TYPE_SAVE), io_close);
			if(!pFile)
				return;
			const int64_t Length = io_length(pFile.get());
			if(Length <= 0 || Length > (int64_t)MAX_BODY + 160)
				return;
			std::string Body(Length, '\0');
			const bool Read = io_read(pFile.get(), Body.data(), Body.size()) == Body.size();
			pFile.reset();
			const auto End = Body.find('\n');
			if(!Read || End == std::string::npos)
				return;
			const std::string Header = Body.substr(0, End);
			long long Received;
			int Age;
			char aHash[65];
			if(sscanf(Header.c_str(), "QMSL1 %lld %d %64s", &Received, &Age, aHash) != 3 || m_SourceHash != aHash || Age < 0 || Age > MAX_CACHE_AGE)
				return;
			const int64_t Now = time_timestamp();
			if(Received < 0 || Received > Now)
				return;
			const int64_t Elapsed = Now - Received;
			if(Elapsed + Age > MAX_CACHE_AGE)
				return;
			std::unique_ptr<json_value, decltype(&json_value_free)> pJson(JsonParse(Body.data() + End + 1, Body.size() - End - 1), json_value_free);
			std::vector<CServerInfo> vServers;
			const bool Success = pJson && !m_ParseList(pJson.get(), &vServers);
			if(Success && m_Cache.Publish(std::move(vServers), Age + Elapsed, m_Clock(), true))
			{
				++m_Revision;
				log_info("serverbrowser_http", "loaded previous serverlist; awaiting live refresh");
			}
		}

		void Load()
		{
			try
			{
				LoadCache();
			}
			catch(const std::exception &Error)
			{
				ReportServerListFailure("serverlist cache loading failed", Error.what());
			}
			catch(...)
			{
				ReportServerListFailure("serverlist cache loading failed", "unknown exception");
			}
		}

	public:
		CServerBrowserHttp(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp, std::vector<std::string> vUrls, const char *pPreviousBestUrl,
			std::function<std::shared_ptr<IHttpRequest>(const char *)> Factory, std::function<int64_t()> Clock, bool UseFallbackTiers, std::function<int(int)> RetryDelay, std::function<bool(const std::string &, const std::string &)> SaveWriter, FServerListParser ParseList) :
			m_pEngine(pEngine), m_pStorage(pStorage), m_pHttp(pHttp), m_vUrls(std::move(vUrls)), m_Factory(std::move(Factory)), m_Clock(std::move(Clock)), m_ParseList(ParseList ? std::move(ParseList) : ServerBrowserParseHttpList), m_UseFallbackTiers(UseFallbackTiers), m_RetryDelay(std::move(RetryDelay))
		{
			m_pSaveState = std::make_shared<CServerListSaveState>();
			m_pSaveState->m_Writer = SaveWriter ? std::move(SaveWriter) : WriteServerListCache;
			if(m_vUrls.size() > MAX_URLS)
				m_vUrls.resize(MAX_URLS);
			std::string SourceKey;
			for(const auto &Url : m_vUrls)
				SourceKey += Url + "\n";
			char aHash[SHA256_MAXSTRSIZE];
			sha256_str(sha256(SourceKey.data(), SourceKey.size()), aHash, sizeof(aHash));
			m_SourceHash = aHash;
			const auto Previous = std::find(m_vUrls.begin(), m_vUrls.end(), pPreviousBestUrl);
			if(Previous != m_vUrls.end())
				std::rotate(m_vUrls.begin(), Previous, Previous + 1);
			m_vBlockedUntil.resize(m_vUrls.size());
			Load();
			Refresh();
		}
		~CServerBrowserHttp() override { Shutdown(); }
		void Shutdown() override
		{
			m_Shutdown = true;
			Cancel();
		}
		uint64_t Revision() const override { return m_Revision; }
		bool IsRefreshing() const override { return m_Running && !m_HadSuccess; }
		bool IsError() const override { return m_Cache.HasRefreshFailed(); }
		bool IsStale() const override { return m_Cache.IsStale(m_Clock()); }
		int NumServers() const override { return m_Cache.Servers().size(); }
		const CServerInfo &Server(int Index) const override { return m_Cache.Servers()[Index]; }
		bool GetBestUrl(const char **ppBestUrl) const override
		{
			*ppBestUrl = m_BestUrl.empty() ? nullptr : m_BestUrl.c_str();
			return m_BestUrl.empty();
		}
		void Refresh() override
		{
			// 同一轮重复刷新不产生第二批请求；退避期间手动刷新立即开始。
			if(m_Shutdown || m_Running)
				return;
			m_Running = true;
			m_HadSuccess = false;
			m_RetryAt = 0;
			m_Tier = 0;
			m_TierStartedAt = m_Clock();
			QueueTier();
			Fill();
		}
		void Update() override
		{
			if(m_Shutdown)
				return;
			if(!m_Running)
			{
				if(m_RetryAt && m_Clock() >= m_RetryAt)
					Refresh();
				return;
			}
			for(size_t Index = 0; Index < m_vActive.size();)
			{
				auto &Attempt = m_vActive[Index];
				if(Attempt.m_pRequest)
				{
					if(!Attempt.m_pRequest->Done())
					{
						++Index;
						continue;
					}
					if(Attempt.m_pRequest->State() != EHttpState::DONE)
					{
						Failed(Attempt);
						m_vActive.erase(m_vActive.begin() + Index);
						continue;
					}
					try
					{
						auto pParse = std::make_shared<CServerListParseJob>(Attempt.m_pRequest, m_vUrls[Attempt.m_Index] == SNAPSHOT_URL, m_ParseList);
						m_pEngine->AddJob(pParse);
						Attempt.m_pParse = std::move(pParse);
						Attempt.m_pRequest.reset();
					}
					catch(const std::exception &Error)
					{
						ReportServerListFailure("serverlist parse scheduling failed", Error.what());
						m_vActive.erase(m_vActive.begin() + Index);
						continue;
					}
					catch(...)
					{
						ReportServerListFailure("serverlist parse scheduling failed", "unknown exception");
						m_vActive.erase(m_vActive.begin() + Index);
						continue;
					}
				}
				if(Attempt.m_pParse->State() != IJob::STATE_DONE)
				{
					++Index;
					continue;
				}
				const auto &Result = Attempt.m_pParse;
				if(Result->m_Success)
				{
					m_HadSuccess = true;
					m_Failures = 0;
					// 比较当前缓存的真实驻留年龄；重开刷新不能把较旧响应覆盖较新数据。
					if(m_Cache.Servers().empty() || Result->m_Age <= m_Cache.Age(m_Clock()))
					{
						m_BestUrl = m_vUrls[Attempt.m_Index];
						m_Cache.Publish(std::move(Result->m_vServers), Result->m_Age, m_Clock());
						++m_Revision;
						Save(Result->m_Body, Result->m_Age);
						log_info("serverbrowser_http", "published serverlist, url='%s' age=%ds", m_BestUrl.c_str(), Result->m_Age);
					}
					else
						log_info("serverbrowser_http", "retained fresher cached serverlist, response age=%ds", Result->m_Age);
					if(Result->m_Age <= 300)
					{
						Cancel();
						return;
					}
				}

				m_vActive.erase(m_vActive.begin() + Index);
			}
			// 已完成结果优先发布；阶段总预算避免坏线路逐源累积等待。
			if(m_Clock() - m_TierStartedAt >= (m_UseFallbackTiers ? 30 : 120))
			{
				for(auto &Attempt : m_vActive)
					if(Attempt.m_pRequest)
						Attempt.m_pRequest->Abort();
				m_vActive.clear();
				m_Queue.clear();
				log_info("serverbrowser_http", "serverlist source tier %d exceeded time budget", m_Tier);
			}
			Fill();
			// 默认源按官方（含加密解析恢复）、试镜像、快照依次升级；自定义文件仍为同级源。
			while(m_UseFallbackTiers && m_vActive.empty() && m_Queue.empty() && m_Tier < 2)
			{
				++m_Tier;
				m_TierStartedAt = m_Clock();
				QueueTier();
				Fill();
			}
			if(m_vActive.empty() && m_Queue.empty())
			{
				m_Running = false;
				if(!m_HadSuccess)
				{
					m_Cache.RefreshFailed();
					m_Failures = std::min(m_Failures + 1, 5);
				}
				const int BaseDelay = m_HadSuccess ? 60 : std::min(60, 5 << (m_Failures - 1));
				const int Delay = m_RetryDelay ? std::clamp(m_RetryDelay(BaseDelay), BaseDelay, 72) : BaseDelay;
				m_RetryAt = m_Clock() + Delay;
				const bool AllBlocked = !m_vBlockedUntil.empty() && std::all_of(m_vBlockedUntil.begin(), m_vBlockedUntil.end(), [this](int64_t Until) { return Until > m_Clock(); });
				if(AllBlocked)
					m_RetryAt = *std::min_element(m_vBlockedUntil.begin(), m_vBlockedUntil.end());
				log_info("serverbrowser_http", "serverlist refresh %s; retry in %llds", m_HadSuccess ? "stale" : "failed", (long long)(m_RetryAt - m_Clock()));
			}
		}
	};
	const char *DEFAULT_SERVERLIST_URLS[] = {
		"https://master1.ddnet.org/ddnet/15/servers.json",
		"https://master2.ddnet.org/ddnet/15/servers.json",
		"https://master3.ddnet.org/ddnet/15/servers.json",
		"https://master4.ddnet.org/ddnet/15/servers.json",
		MIRROR_URL,
		SNAPSHOT_URL,
	};
}

IServerBrowserHttp *CreateServerBrowserHttpWithSources(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp,
	std::vector<std::string> vUrls, const char *pPreviousBestUrl,
	std::function<std::shared_ptr<IHttpRequest>(const char *)> Factory, std::function<int64_t()> Clock, bool UseFallbackTiers, std::function<int(int)> RetryDelay, std::function<bool(const std::string &, const std::string &)> SaveWriter, FServerListParser ParseList)
{
	return new CServerBrowserHttp(pEngine, pStorage, pHttp, std::move(vUrls), pPreviousBestUrl, std::move(Factory), std::move(Clock), UseFallbackTiers, std::move(RetryDelay), std::move(SaveWriter), std::move(ParseList));
}

IServerBrowserHttp *CreateServerBrowserHttp(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp, const char *pPreviousBestUrl)
{
	std::vector<std::string> vUrls;
	CLineReader LineReader;
	if(LineReader.OpenFile(pStorage->OpenFile("ddnet-serverlist-urls.cfg", IOFLAG_READ, IStorage::TYPE_ALL)))
		while(const char *pLine = LineReader.Get())
		{
			if(vUrls.size() == MAX_URLS)
				break;
			if(*pLine && std::find(vUrls.begin(), vUrls.end(), pLine) == vUrls.end())
				vUrls.emplace_back(pLine);
		}
	const bool UseDefaults = vUrls.empty();
	if(UseDefaults)
		for(const char *pUrl : DEFAULT_SERVERLIST_URLS)
			vUrls.emplace_back(pUrl);
	std::function<std::shared_ptr<IHttpRequest>(const char *)> Factory = [](const char *pUrl) { return std::shared_ptr<IHttpRequest>(HttpGet(pUrl)); };
#if defined(CONF_QMCLIENT_TEST_STORAGE)
	// 仅替换隔离测试的传输目的地，来源身份、优先级、预算与快照校验保持生产行为。
	const char *pOrigin = std::getenv("QMCLIENT_TEST_SERVERLIST_ORIGIN");
	NETADDR Address;
	if(UseDefaults && pOrigin && str_startswith(pOrigin, "http://127.0.0.1:") &&
		net_addr_from_str(&Address, pOrigin + str_length("http://")) == 0 && Address.port != 0)
	{
		Factory = [Origin = std::string(pOrigin)](const char *pUrl) {
			for(size_t Index = 0; Index < std::size(DEFAULT_SERVERLIST_URLS); ++Index)
				if(str_comp(pUrl, DEFAULT_SERVERLIST_URLS[Index]) == 0)
					return std::shared_ptr<IHttpRequest>(HttpGet((Origin + "/default/" + std::to_string(Index)).c_str()));
			return std::shared_ptr<IHttpRequest>(HttpGet(pUrl));
		};
	}
	const char *pDoh = std::getenv("QMCLIENT_TEST_SERVERLIST_DOH");
	if(pDoh && str_comp(pDoh, "1") == 0)
	{
		Factory = [CreateRequest = std::move(Factory)](const char *pUrl) {
			auto pRequest = CreateRequest(pUrl);
			// 仍由生产接口限制为精确官方 URL，并使用正常目标与 DoH 证书校验。
			pRequest->ServerListDnsFallback();
			return pRequest;
		};
	}
#endif
	return CreateServerBrowserHttpWithSources(pEngine, pStorage, pHttp, std::move(vUrls), pPreviousBestUrl, std::move(Factory), [] { return time_get() / time_freq(); }, UseDefaults, [](int Delay) { return Delay + secure_rand_below(Delay / 5 + 1); });
}
