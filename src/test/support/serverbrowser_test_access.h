#ifndef TEST_SUPPORT_SERVERBROWSER_TEST_ACCESS_H
#define TEST_SUPPORT_SERVERBROWSER_TEST_ACCESS_H

#include <engine/client/serverbrowser.h>
#include <engine/shared/json.h>

#include <utility>

class CServerBrowserTestAccess
{
public:
	static void SetStorage(CServerBrowser &Browser, IStorage *pStorage) { Browser.m_pStorage = pStorage; }
	static const json_value *Info(const CServerBrowser &Browser) { return Browser.m_pDDNetInfo; }
	static void SetCustomSource(CServerBrowser &Browser, std::function<void(std::vector<json_value *> &)> Source)
	{
		Browser.m_CustomCommunitiesFunction = std::move(Source);
	}
	static void Initialize(CServerBrowser &Browser, IFriends *pFriends, IFavorites *pFavorites)
	{
		Browser.m_pFriends = pFriends;
		Browser.m_pFavorites = pFavorites;
	}
	static void Add(CServerBrowser &Browser, const NETADDR &Address, const CServerInfo &Info)
	{
		Browser.SetInfo(Browser.Add(&Address, 1), Info);
	}
	static void ReplaceFirstAddress(CServerBrowser &Browser, const NETADDR &Address)
	{
		Browser.ReplaceEntry(Browser.m_vpServerlist[0], &Address, 1);
	}
	static void SetFirstInfo(CServerBrowser &Browser, const CServerInfo &Info)
	{
		Browser.SetInfo(Browser.m_vpServerlist[0], Info);
	}
	static void SetHttp(CServerBrowser &Browser, IServerBrowserHttp *pHttp)
	{
		Browser.m_pHttp = pHttp;
		Browser.m_ServerlistType = IServerBrowser::TYPE_INTERNET;
		Browser.m_RefreshingHttp = false;
	}
	static void SetLan(CServerBrowser &Browser, CNetClient *pNetClient, int64_t BroadcastTime)
	{
		Browser.m_pNetClient = pNetClient;
		Browser.m_ServerlistType = IServerBrowser::TYPE_LAN;
		Browser.m_BroadcastTime = BroadcastTime;
	}
	static int GenerateToken(const CServerBrowser &Browser, const NETADDR &Address) { return Browser.GenerateToken(Address); }
	static int BasicToken(int Token) { return CServerBrowser::GetBasicToken(Token); }
	static void Sort(CServerBrowser &Browser) { Browser.Sort(); }
	static void CleanUp(CServerBrowser &Browser) { Browser.CleanUp(); }
	static bool NeedsResort(const CServerBrowser &Browser) { return Browser.m_NeedResort; }
	static void SetPingCache(CServerBrowser &Browser, IServerBrowserPingCache *pCache)
	{
		Browser.m_pPingCache = pCache;
	}
	static void SetLatency(CServerBrowser &Browser, NETADDR Address, int Latency)
	{
		Browser.SetLatency(Address, Latency);
	}
	static void SetDifficultyStarsFunction(CServerBrowser &Browser, std::function<int(const CServerInfo *)> Function)
	{
		Browser.m_MapDifficultyStarsFunction = std::move(Function);
	}
	static uint64_t HttpRevision(const CServerBrowser &Browser) { return Browser.m_HttpRevision; }
	static bool RefreshPending(const CServerBrowser &Browser) { return Browser.m_RefreshingHttp; }
	static int NumRequests(const CServerBrowser &Browser) { return Browser.m_NumRequests; }
	static CServerBrowser::CServerEntry *FirstRequest(const CServerBrowser &Browser) { return Browser.m_pFirstReqServer; }
	static CServerBrowser::CServerEntry *LastRequest(const CServerBrowser &Browser) { return Browser.m_pLastReqServer; }
	static int MaxRequests(const CServerBrowser &Browser) { return Browser.m_CurrentMaxRequests; }
	static void SetMaxRequests(CServerBrowser &Browser, int Count) { Browser.m_CurrentMaxRequests = Count; }
	static void LoadCommunities(CServerBrowser &Browser, json_value *pInfo)
	{
		json_value_free(Browser.m_pDDNetInfo);
		Browser.m_pDDNetInfo = pInfo;
		Browser.LoadDDNetServers();
	}
};

#endif
