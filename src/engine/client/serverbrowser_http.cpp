#include "serverbrowser_http.h"

#include "serverbrowser_http_parse.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/jobs.h>
#include <engine/shared/linereader.h>
#include <engine/shared/serverinfo.h>
#include <engine/storage.h>

#include <chrono>
#include <memory>
#include <vector>

using namespace std::chrono_literals;

namespace
{

	int SanitizeAge(std::optional<int64_t> Age)
	{
		// A year is of course pi*10**7 seconds.
		if(!(Age && 0 <= *Age && *Age < 31415927))
		{
			return 31415927;
		}
		return *Age;
	}

	// Classify HTTP responses into buckets, treat 15 seconds as fresh, 1 minute as
	// less fresh, etc. This ensures that differences in the order of seconds do
	// not affect master choice.
	int ClassifyAge(int AgeSeconds)
	{
		return 0 //
		       + (AgeSeconds >= 15) // 15 seconds
		       + (AgeSeconds >= 60) // 1 minute
		       + (AgeSeconds >= 300) // 5 minutes
		       + (AgeSeconds / 3600); // 1 hour
	}

	class CChooseMaster
	{
	public:
		typedef bool (*VALIDATOR)(json_value *pJson);

		enum
		{
			MAX_URLS = 16,
		};
		CChooseMaster(IEngine *pEngine, IHttp *pHttp, VALIDATOR pfnValidator, const char **ppUrls, int NumUrls, int PreviousBestIndex);
		virtual ~CChooseMaster();

		bool GetBestUrl(const char **pBestUrl) const;
		void Shutdown();
		void Reset();
		bool IsRefreshing() const { return m_pJob && !m_pJob->Done(); }
		void Refresh();

	private:
		int GetBestIndex() const;

		class CData
		{
		public:
			std::atomic_int m_BestIndex{-1};
			// Constant after construction.
			VALIDATOR m_pfnValidator;
			int m_NumUrls;
			char m_aaUrls[MAX_URLS][256];
		};
		class CJob : public IJob
		{
			CChooseMaster *m_pParent;
			CLock m_Lock;
			std::shared_ptr<CData> m_pData;
			std::shared_ptr<IHttpRequest> m_pHead;
			std::shared_ptr<IHttpRequest> m_pGet;

		protected:
			void Run() override REQUIRES(!m_Lock);

		public:
			CJob(CChooseMaster *pParent, std::shared_ptr<CData> pData) :
				m_pParent(pParent),
				m_pData(std::move(pData))
			{
				Abortable(true);
			}
			bool Abort() override REQUIRES(!m_Lock);
		};

		IEngine *m_pEngine;
		IHttp *m_pHttp;
		int m_PreviousBestIndex;
		std::shared_ptr<CData> m_pData;
		std::shared_ptr<CJob> m_pJob;
	};

	CChooseMaster::CChooseMaster(IEngine *pEngine, IHttp *pHttp, VALIDATOR pfnValidator, const char **ppUrls, int NumUrls, int PreviousBestIndex) :
		m_pEngine(pEngine),
		m_pHttp(pHttp),
		m_PreviousBestIndex(PreviousBestIndex)
	{
		dbg_assert(NumUrls >= 0, "no master URLs");
		dbg_assert(NumUrls <= MAX_URLS, "too many master URLs");
		dbg_assert(PreviousBestIndex >= -1, "previous best index negative and not -1");
		dbg_assert(PreviousBestIndex < NumUrls, "previous best index too high");
		m_pData = std::make_shared<CData>();
		m_pData->m_pfnValidator = pfnValidator;
		m_pData->m_NumUrls = NumUrls;
		for(int i = 0; i < m_pData->m_NumUrls; i++)
		{
			str_copy(m_pData->m_aaUrls[i], ppUrls[i]);
		}
	}

	CChooseMaster::~CChooseMaster()
	{
		dbg_assert(m_pJob == nullptr, "Choose master job was not cleared");
	}

	int CChooseMaster::GetBestIndex() const
	{
		int BestIndex = m_pData->m_BestIndex.load();
		if(BestIndex >= 0)
		{
			return BestIndex;
		}
		else
		{
			return m_PreviousBestIndex;
		}
	}

	bool CChooseMaster::GetBestUrl(const char **ppBestUrl) const
	{
		int Index = GetBestIndex();
		if(Index < 0)
		{
			*ppBestUrl = nullptr;
			return true;
		}
		*ppBestUrl = m_pData->m_aaUrls[Index];
		return false;
	}

	void CChooseMaster::Shutdown()
	{
		if(m_pJob)
		{
			m_pJob->Abort();
			m_pJob = nullptr;
		}
	}

	void CChooseMaster::Reset()
	{
		m_PreviousBestIndex = -1;
		m_pData->m_BestIndex.store(-1);
	}

	void CChooseMaster::Refresh()
	{
		if(m_pJob == nullptr || m_pJob->State() == IJob::STATE_DONE)
		{
			m_pJob = std::make_shared<CJob>(this, m_pData);
			m_pEngine->AddJob(m_pJob);
		}
	}

	bool CChooseMaster::CJob::Abort()
	{
		if(!IJob::Abort())
		{
			return false;
		}

		const CLockScope LockScope(m_Lock);
		if(m_pHead != nullptr)
		{
			m_pHead->Abort();
		}

		if(m_pGet != nullptr)
		{
			m_pGet->Abort();
		}

		return true;
	}

	void CChooseMaster::CJob::Run()
	{
		// Check masters in a random order.
		int aRandomized[MAX_URLS] = {0};
		for(int i = 0; i < m_pData->m_NumUrls; i++)
		{
			aRandomized[i] = i;
		}
		// https://en.wikipedia.org/w/index.php?title=Fisher%E2%80%93Yates_shuffle&oldid=1002922479#The_modern_algorithm
		// The equivalent version.
		for(int i = 0; i <= m_pData->m_NumUrls - 2; i++)
		{
			int j = i + secure_rand_below(m_pData->m_NumUrls - i);
			std::swap(aRandomized[i], aRandomized[j]);
		}
		// Do a HEAD request to ensure that a connection is established and
		// then do a GET request to check how fast we can get the server list.
		//
		// 10 seconds connection timeout, overall timeout to prevent getting stuck
		// forever on HEAD responses in broken network setups, lower than 8KB/s for
		// 10 seconds to fail.
		CTimeout Timeout{10000, 15000, 8000, 10};
		int aTimeMs[MAX_URLS];
		int aAgeS[MAX_URLS];
		for(int i = 0; i < m_pData->m_NumUrls; i++)
		{
			aTimeMs[i] = -1;
			aAgeS[i] = SanitizeAge({});
			const char *pUrl = m_pData->m_aaUrls[aRandomized[i]];
			std::shared_ptr<IHttpRequest> pHead = HttpHead(pUrl);
			pHead->Timeout(Timeout);
			pHead->LogProgress(HTTPLOG::FAILURE);
			{
				const CLockScope LockScope(m_Lock);
				if(State() == IJob::STATE_ABORTED)
				{
					return;
				}
				m_pHead = pHead;
				m_pParent->m_pHttp->Run(pHead);
			}

			pHead->Wait();
			if(pHead->State() == EHttpState::ABORTED || State() == IJob::STATE_ABORTED)
			{
				log_debug("serverbrowser_http", "master chooser aborted");
				return;
			}
			if(pHead->State() != EHttpState::DONE)
			{
				continue;
			}

			auto StartTime = time_get_nanoseconds();
			std::shared_ptr<IHttpRequest> pGet = HttpGet(pUrl);
			pGet->Timeout(Timeout);
			pGet->LogProgress(HTTPLOG::FAILURE);
			{
				const CLockScope LockScope(m_Lock);
				if(State() == IJob::STATE_ABORTED)
				{
					return;
				}
				m_pGet = pGet;
				m_pParent->m_pHttp->Run(pGet);
			}

			pGet->Wait();

			auto Time = std::chrono::duration_cast<std::chrono::milliseconds>(time_get_nanoseconds() - StartTime);
			if(pGet->State() == EHttpState::ABORTED || State() == IJob::STATE_ABORTED)
			{
				log_debug("serverbrowser_http", "master chooser aborted");
				return;
			}
			if(pGet->State() != EHttpState::DONE)
			{
				continue;
			}
			json_value *pJson = pGet->ResultJson();
			if(!pJson)
			{
				continue;
			}

			bool ParseFailure = m_pData->m_pfnValidator(pJson);
			json_value_free(pJson);
			if(ParseFailure)
			{
				continue;
			}
			int AgeS = SanitizeAge(pGet->ResultAgeSeconds());
			log_info("serverbrowser_http", "found master, url='%s' time=%dms age=%ds", pUrl, (int)Time.count(), AgeS);

			aTimeMs[i] = Time.count();
			aAgeS[i] = AgeS;
		}

		// Determine index of the minimum time.
		int BestIndex = -1;
		int BestTime = 0;
		int BestAge = 0;
		for(int i = 0; i < m_pData->m_NumUrls; i++)
		{
			if(aTimeMs[i] < 0)
			{
				continue;
			}
			if(BestIndex == -1 || std::tuple(ClassifyAge(aAgeS[i]), aTimeMs[i]) < std::tuple(ClassifyAge(BestAge), BestTime))
			{
				BestTime = aTimeMs[i];
				BestAge = aAgeS[i];
				BestIndex = aRandomized[i];
			}
		}
		if(BestIndex == -1)
		{
			log_error("serverbrowser_http", "WARNING: no usable masters found");
			return;
		}

		log_info("serverbrowser_http", "determined best master, url='%s' time=%dms age=%ds", m_pData->m_aaUrls[BestIndex], BestTime, BestAge);
		m_pData->m_BestIndex.store(BestIndex);
	}

	// 解析任务只持有 HTTP 响应，不引用浏览器的生命周期。
	class CServerListParseJob : public IJob
	{
		std::shared_ptr<IHttpRequest> m_pResponse;

		void Run() override
		{
			if(m_pResponse->State() == EHttpState::DONE)
			{
				json_value *pJson = m_pResponse->ResultJson();
				m_Success = !ServerBrowserParseHttpList(pJson, &m_vServers);
				json_value_free(pJson);
				m_Age = SanitizeAge(m_pResponse->ResultAgeSeconds());
			}
			m_pResponse.reset();
		}

	public:
		explicit CServerListParseJob(std::shared_ptr<IHttpRequest> pResponse) :
			m_pResponse(std::move(pResponse))
		{
		}

		bool m_Success = false;
		int m_Age = 0;
		std::vector<CServerInfo> m_vServers;
	};

	class CServerBrowserHttp : public IServerBrowserHttp
	{
	public:
		CServerBrowserHttp(IEngine *pEngine, IHttp *pHttp, const char **ppUrls, int NumUrls, int PreviousBestIndex);
		~CServerBrowserHttp() override;
		void Shutdown() override;
		void Update() override;
		bool IsRefreshing() const override { return m_State != STATE_DONE && m_State != STATE_NO_MASTER; }
		bool IsError() const override { return m_State == STATE_NO_MASTER; }
		void Refresh() override;
		bool GetBestUrl(const char **pBestUrl) const override { return m_pChooseMaster->GetBestUrl(pBestUrl); }

		int NumServers() const override
		{
			return m_vServers.size();
		}
		const CServerInfo &Server(int Index) const override
		{
			return m_vServers[Index];
		}

	private:
		enum
		{
			STATE_DONE,
			STATE_WANTREFRESH,
			STATE_REFRESHING,
			STATE_PARSING,
			STATE_NO_MASTER,
		};

		static bool Validate(json_value *pJson);

		IEngine *m_pEngine;
		IHttp *m_pHttp;

		int m_State = STATE_WANTREFRESH;
		std::shared_ptr<IHttpRequest> m_pGetServers;
		std::shared_ptr<CServerListParseJob> m_pParseJob;
		std::unique_ptr<CChooseMaster> m_pChooseMaster;

		std::vector<CServerInfo> m_vServers;
	};

	CServerBrowserHttp::CServerBrowserHttp(IEngine *pEngine, IHttp *pHttp, const char **ppUrls, int NumUrls, int PreviousBestIndex) :
		m_pEngine(pEngine),
		m_pHttp(pHttp),
		m_pChooseMaster(new CChooseMaster(pEngine, pHttp, Validate, ppUrls, NumUrls, PreviousBestIndex))
	{
		Refresh();
	}

	CServerBrowserHttp::~CServerBrowserHttp()
	{
		dbg_assert(m_pGetServers == nullptr, "Server browser load job was not cleared");
		dbg_assert(m_pParseJob == nullptr, "Server browser parse job was not cleared");
	}

	void CServerBrowserHttp::Shutdown()
	{
		if(m_pGetServers != nullptr)
		{
			m_pGetServers->Abort();
			m_pGetServers = nullptr;
		}
		m_pParseJob.reset();
		m_pChooseMaster->Shutdown();
	}

	void CServerBrowserHttp::Update()
	{
		if(m_State == STATE_WANTREFRESH)
		{
			const char *pBestUrl;
			if(m_pChooseMaster->GetBestUrl(&pBestUrl))
			{
				if(!m_pChooseMaster->IsRefreshing())
				{
					log_error("serverbrowser_http", "no working serverlist URL found");
					m_State = STATE_NO_MASTER;
				}
				return;
			}
			m_pGetServers = HttpGet(pBestUrl);
			// 10 seconds connection timeout, include an overall timeout so refreshing
			// cannot stall forever in pathological network environments.
			m_pGetServers->Timeout(CTimeout{10000, 30000, 8000, 10});
			m_pHttp->Run(m_pGetServers);
			m_State = STATE_REFRESHING;
		}
		else if(m_State == STATE_REFRESHING)
		{
			if(!m_pGetServers->Done())
			{
				return;
			}
			m_pParseJob = std::make_shared<CServerListParseJob>(std::move(m_pGetServers));
			m_State = STATE_PARSING;
			m_pEngine->AddJob(m_pParseJob);
		}
		else if(m_State == STATE_PARSING)
		{
			if(m_pParseJob->State() != IJob::STATE_DONE)
				return;
			const bool Success = m_pParseJob->m_Success;
			const int Age = m_pParseJob->m_Age;
			if(Success)
				m_vServers = std::move(m_pParseJob->m_vServers);
			m_pParseJob.reset();
			m_State = STATE_DONE;
			if(!Success)
			{
				log_error("serverbrowser_http", "failed getting serverlist, trying to find best URL");
				m_pChooseMaster->Reset();
				m_State = STATE_WANTREFRESH;
				m_pChooseMaster->Refresh();
			}
			else
			{
				// Try to find new master if the current one returns
				// results that are 5 minutes old.
				if(Age > 300)
				{
					log_info("serverbrowser_http", "got stale serverlist, age=%ds, trying to find best URL", Age);
					m_pChooseMaster->Refresh();
				}
			}
		}
	}
	void CServerBrowserHttp::Refresh()
	{
		if(m_State == STATE_WANTREFRESH || m_State == STATE_REFRESHING || m_State == STATE_PARSING || m_State == STATE_NO_MASTER)
		{
			if(m_State == STATE_NO_MASTER)
			{
				m_State = STATE_WANTREFRESH;
			}

			const char *pBestUrl;
			// 有可用缓存地址时直接使用，只有没有地址时才启动 master 探测。
			if(m_pChooseMaster->GetBestUrl(&pBestUrl))
			{
				m_pChooseMaster->Refresh();
			}
		}
		if(m_State == STATE_DONE)
		{
			m_State = STATE_WANTREFRESH;
		}
		Update();
	}
	bool CServerBrowserHttp::Validate(json_value *pJson)
	{
		std::vector<CServerInfo> vServers;
		return ServerBrowserParseHttpList(pJson, &vServers);
	}
	const char *DEFAULT_SERVERLIST_URLS[] = {
		"https://master1.ddnet.org/ddnet/15/servers.json",
		"https://master2.ddnet.org/ddnet/15/servers.json",
		"https://master3.ddnet.org/ddnet/15/servers.json",
		"https://master4.ddnet.org/ddnet/15/servers.json",
	};

} // namespace

IServerBrowserHttp *CreateServerBrowserHttp(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp, const char *pPreviousBestUrl)
{
	char aaUrls[CChooseMaster::MAX_URLS][256];
	const char *apUrls[CChooseMaster::MAX_URLS] = {nullptr};
	const char **ppUrls = apUrls;
	int NumUrls = 0;
	CLineReader LineReader;
	if(LineReader.OpenFile(pStorage->OpenFile("ddnet-serverlist-urls.cfg", IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		while(const char *pLine = LineReader.Get())
		{
			if(NumUrls == CChooseMaster::MAX_URLS)
			{
				break;
			}
			str_copy(aaUrls[NumUrls], pLine);
			apUrls[NumUrls] = aaUrls[NumUrls];
			NumUrls += 1;
		}
	}
	if(NumUrls == 0)
	{
		ppUrls = DEFAULT_SERVERLIST_URLS;
		NumUrls = std::size(DEFAULT_SERVERLIST_URLS);
	}
	int PreviousBestIndex = -1;
	for(int i = 0; i < NumUrls; i++)
	{
		if(str_comp(ppUrls[i], pPreviousBestUrl) == 0)
		{
			PreviousBestIndex = i;
			break;
		}
	}
	return new CServerBrowserHttp(pEngine, pHttp, ppUrls, NumUrls, PreviousBestIndex);
}
