/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_SERVERBROWSER_H
#define ENGINE_CLIENT_SERVERBROWSER_H

#include <base/hash.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/serverbrowser.h>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

typedef struct _json_value json_value;
class CNetClient;
class IConfigManager;
class IConsole;
class IEngine;
class IFavorites;
class IFriends;
class IServerBrowserHttp;
class IServerBrowserPingCache;
class IStorage;
class IHttp;

class CCommunityId
{
	char m_aId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];

public:
	CCommunityId(const char *pCommunityId)
	{
		str_copy(m_aId, pCommunityId);
	}

	const char *Id() const { return m_aId; }

	bool operator==(const CCommunityId &Other) const
	{
		return str_comp(Id(), Other.Id()) == 0;
	}

	bool operator<(const CCommunityId &Other) const
	{
		return str_comp(Id(), Other.Id()) < 0;
	}
};

template<>
struct std::hash<CCommunityId>
{
	size_t operator()(const CCommunityId &Elem) const noexcept
	{
		return str_quickhash(Elem.Id());
	}
};

class CCommunityCountryName
{
	char m_aName[CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH];

public:
	CCommunityCountryName(const char *pCountryName)
	{
		str_copy(m_aName, pCountryName);
	}

	const char *Name() const { return m_aName; }

	bool operator==(const CCommunityCountryName &Other) const
	{
		return str_comp(Name(), Other.Name()) == 0;
	}

	bool operator<(const CCommunityCountryName &Other) const
	{
		return str_comp(Name(), Other.Name()) < 0;
	}
};

template<>
struct std::hash<CCommunityCountryName>
{
	size_t operator()(const CCommunityCountryName &Elem) const noexcept
	{
		return str_quickhash(Elem.Name());
	}
};

class CCommunityTypeName
{
	char m_aName[CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];

public:
	CCommunityTypeName(const char *pTypeName)
	{
		str_copy(m_aName, pTypeName);
	}

	const char *Name() const { return m_aName; }

	bool operator==(const CCommunityTypeName &Other) const
	{
		return str_comp(Name(), Other.Name()) == 0;
	}

	bool operator<(const CCommunityTypeName &Other) const
	{
		return str_comp(Name(), Other.Name()) < 0;
	}
};

template<>
struct std::hash<CCommunityTypeName>
{
	size_t operator()(const CCommunityTypeName &Elem) const noexcept
	{
		return str_quickhash(Elem.Name());
	}
};

class CCommunityServer
{
	char m_aCommunityId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	char m_aCountryName[CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH];
	char m_aTypeName[CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];

public:
	CCommunityServer(const char *pCommunityId, const char *pCountryName, const char *pTypeName)
	{
		str_copy(m_aCommunityId, pCommunityId);
		str_copy(m_aCountryName, pCountryName);
		str_copy(m_aTypeName, pTypeName);
	}

	const char *CommunityId() const { return m_aCommunityId; }
	const char *CountryName() const { return m_aCountryName; }
	const char *TypeName() const { return m_aTypeName; }
};

class CFavoriteCommunityFilterList : public IFilterList
{
public:
	void Add(const char *pCommunityId) override;
	void Remove(const char *pCommunityId) override;
	void Clear() override;
	bool Filtered(const char *pCommunityId) const override;
	bool Empty() const override;
	void Clean(const std::vector<CCommunity> &vAllowedCommunities);
	void Save(IConfigManager *pConfigManager) const;
	const std::vector<CCommunityId> &Entries() const;

private:
	std::vector<CCommunityId> m_vEntries;
};

class CExcludedCommunityFilterList : public IFilterList
{
public:
	void Add(const char *pCommunityId) override;
	void Remove(const char *pCommunityId) override;
	void Clear() override;
	bool Filtered(const char *pCommunityId) const override;
	bool Empty() const override;
	void Clean(const std::vector<CCommunity> &vAllowedCommunities);
	void Save(IConfigManager *pConfigManager) const;

private:
	std::set<CCommunityId> m_Entries;
};

class CExcludedCommunityCountryFilterList : public IFilterList
{
public:
	CExcludedCommunityCountryFilterList(const ICommunityCache *pCommunityCache) :
		m_pCommunityCache(pCommunityCache)
	{
	}

	void Add(const char *pCountryName) override;
	void Add(const char *pCommunityId, const char *pCountryName);
	void Remove(const char *pCountryName) override;
	void Remove(const char *pCommunityId, const char *pCountryName);
	// 国家筛选是"排除名单"语义：只有名单里的国家被隐藏，其余都算选中。名单由"只看某几个国家"
	// 之类的操作按当时格子里可选的国家写入，之后新出现（或曾被裁剪后重新出现）的国家不在名单里，
	// 就会被当成用户保留的国家，导致筛选结果随时间漂移。这里额外记录"用户保留可见"的基线
	// m_AllowedCountries（随配置持久化），用于把新出现的国家补进排除名单。
	void AddAllowed(const char *pCountryName);
	void AddAllowed(const char *pCommunityId, const char *pCountryName);
	void RemoveAllowed(const char *pCountryName);
	void RemoveAllowed(const char *pCommunityId, const char *pCountryName);
	// 把可选国家里既不在排除名单、也不在保留基线里的国家补进排除名单。
	// @return 排除名单是否发生变化（调用方需要据此重新过滤服务器列表）
	bool AutoExcludeNewCountries();
	void Clear() override;
	bool Filtered(const char *pCountryName) const override;
	bool Empty() const override;
	void Clean(const std::vector<CCommunity> &vAllowedCommunities);
	// Clean 的实现主体：以"当前存在的社区"为输入，便于单测覆盖。
	void CleanCountries(const std::set<CCommunityId> &vExistingCommunityIds);
	void Save(IConfigManager *pConfigManager) const;

private:
	// 确保当前 key 存在保留基线：缺失时按"当前可见的可选国家"（可选国家减去排除名单）建立。
	// 供 AutoExcludeNewCountries 与"用户让某国可见"使用，避免基线只包含刚点的那一个国家，
	// 从而在其他国家出现时把它们误当成新国家排除掉。
	void EnsureAllowedBaseline();

	const ICommunityCache *m_pCommunityCache;
	std::map<CCommunityId, std::set<CCommunityCountryName>> m_Entries;
	std::map<CCommunityId, std::set<CCommunityCountryName>> m_AllowedCountries;
};

class CExcludedCommunityTypeFilterList : public IFilterList
{
public:
	CExcludedCommunityTypeFilterList(const ICommunityCache *pCommunityCache) :
		m_pCommunityCache(pCommunityCache)
	{
	}

	void Add(const char *pTypeName) override;
	void Add(const char *pCommunityId, const char *pTypeName);
	void Remove(const char *pTypeName) override;
	void Remove(const char *pCommunityId, const char *pTypeName);
	void Clear() override;
	bool Filtered(const char *pTypeName) const override;
	bool Empty() const override;
	void Clean(const std::vector<CCommunity> &vAllowedCommunities);
	void Save(IConfigManager *pConfigManager) const;

private:
	const ICommunityCache *m_pCommunityCache;
	std::map<CCommunityId, std::set<CCommunityTypeName>> m_Entries;
};

class CCommunityCache : public ICommunityCache
{
	IServerBrowser *m_pServerBrowser;
	std::optional<SHA256_DIGEST> m_InfoSha256;
	int m_LastType = IServerBrowser::NUM_TYPES; // initial value does not appear normally, marking uninitialized cache
	unsigned m_SelectedCommunitiesHash = 0;
	std::vector<const CCommunity *> m_vpSelectedCommunities;
	std::vector<const CCommunityCountry *> m_vpSelectableCountries;
	std::vector<const CCommunityType *> m_vpSelectableTypes;
	bool m_AnyRanksAvailable = false;
	bool m_CountryTypesFilterAvailable = false;
	const char *m_pCountryTypeFilterKey = IServerBrowser::COMMUNITY_ALL;

public:
	CCommunityCache(IServerBrowser *pServerBrowser) :
		m_pServerBrowser(pServerBrowser)
	{
	}

	void Update(bool Force) override;
	// 社区数据（m_vCommunities 及其内部容器）被整体重建后必须调用：缓存持有的是指向这些容器的
	// 裸指针，而 Update 只在 DDNet info 摘要/社区 ID 哈希/页面类型变化时才重建，重建后容器地址
	// 变化时旧指针会指向已释放内存。复位标记以强制下一次 Update 重新构建。
	void Invalidate()
	{
		m_InfoSha256.reset();
		m_LastType = IServerBrowser::NUM_TYPES;
	}
	const std::vector<const CCommunity *> &SelectedCommunities() const override { return m_vpSelectedCommunities; }
	const std::vector<const CCommunityCountry *> &SelectableCountries() const override { return m_vpSelectableCountries; }
	const std::vector<const CCommunityType *> &SelectableTypes() const override { return m_vpSelectableTypes; }
	bool AnyRanksAvailable() const override { return m_AnyRanksAvailable; }
	bool CountriesTypesFilterAvailable() const override { return m_CountryTypesFilterAvailable; }
	const char *CountryTypeFilterKey() const override { return m_pCountryTypeFilterKey; }
};

class CServerBrowser : public IServerBrowser
{
public:
	CServerBrowser();
	~CServerBrowser() override;

	// interface functions
	void Shutdown() override;
	void Refresh(int Type, bool Force = false) override;
	void RefreshHttpServerList() override;
	bool IsRefreshing() const override;
	bool IsGettingServerlist() const override;
	bool IsServerlistError() const override;
	int LoadingProgression() const override;
	void RequestResort() { m_NeedResort = true; }

	uint64_t FriendListRevision() const override { return m_FriendListRevision; }
	int NumServers() const override { return m_vpServerlist.size(); }
	const CServerInfo *Get(int Index) const override;
	int NumHttpServers() const override;
	const CServerInfo *HttpGet(int Index) const override;
	int Players(const CServerInfo &Item) const override;
	int Max(const CServerInfo &Item) const override;
	int NumSortedServers() const override { return m_vSortedServerlist.size(); }
	int NumSortedPlayers() const override { return m_NumSortedPlayers; }
	const CServerInfo *SortedGet(int Index) const override;
	void SetQmClientServerCounts(const std::unordered_map<std::string, int> &Counts) override;

	const json_value *LoadDDNetInfo();
	void LoadDDNetInfoJson();
	void LoadDDNetLocation();
	void LoadDDNetServers();
	void UpdateServerFilteredPlayers(CServerInfo *pInfo) const;
	void UpdateServerFriends(CServerInfo *pInfo) const;
	void UpdateServerCommunity(CServerInfo *pInfo) const;
	void UpdateServerRank(CServerInfo *pInfo) const;
	void UpdateServerLatency(CServerInfo *pInfo, int OwnLocation) const;
	int DetermineOwnLocation() const;
	void ValidateServerlistType();
	const char *GetTutorialServer() override;

	const std::vector<CCommunity> &Communities() const override;
	const CCommunity *Community(const char *pCommunityId) const override;
	std::vector<const CCommunity *> SelectedCommunities() const override;
	std::vector<const CCommunity *> FavoriteCommunities() const override;
	std::vector<const CCommunity *> CurrentCommunities() const override;
	unsigned CurrentCommunitiesHash() const override;

	bool DDNetInfoAvailable() const override { return m_pDDNetInfo != nullptr; }
	std::optional<SHA256_DIGEST> DDNetInfoSha256() const override { return m_DDNetInfoSha256; }

	ICommunityCache &CommunityCache() override { return m_CommunityCache; }
	const ICommunityCache &CommunityCache() const override { return m_CommunityCache; }
	CFavoriteCommunityFilterList &FavoriteCommunitiesFilter() override { return m_FavoriteCommunitiesFilter; }
	CExcludedCommunityFilterList &CommunitiesFilter() override { return m_CommunitiesFilter; }
	CExcludedCommunityCountryFilterList &CountriesFilter() override { return m_CountriesFilter; }
	CExcludedCommunityTypeFilterList &TypesFilter() override { return m_TypesFilter; }
	const CFavoriteCommunityFilterList &FavoriteCommunitiesFilter() const override { return m_FavoriteCommunitiesFilter; }
	const CExcludedCommunityFilterList &CommunitiesFilter() const override { return m_CommunitiesFilter; }
	const CExcludedCommunityCountryFilterList &CountriesFilter() const override { return m_CountriesFilter; }
	const CExcludedCommunityTypeFilterList &TypesFilter() const override { return m_TypesFilter; }
	void CleanFilters() override;

	//
	void Update();
	void OnServerInfoUpdate(const NETADDR &Addr, int Token, const CServerInfo *pInfo);
	void SetHttpInfo(const CServerInfo *pInfo);
	void RequestCurrentServer(const NETADDR &Addr) const;
	void RequestCurrentServerWithRandomToken(const NETADDR &Addr, int *pBasicToken, int *pToken) const;
	void SetCurrentServerPing(const NETADDR &Addr, int Ping);

	void SetBaseInfo(class CNetClient *pClient, const char *pNetVersion);
	void OnInit();

	void QueueRequest(CServerEntry *pEntry);
	CServerEntry *Find(const NETADDR &Addr) override;
	int GetCurrentType() override { return m_ServerlistType; }
	bool IsRegistered(const NETADDR &Addr);

private:
	friend class CServerBrowserTestAccess;

	CNetClient *m_pNetClient = nullptr;
	IConfigManager *m_pConfigManager = nullptr;
	IConsole *m_pConsole = nullptr;
	IEngine *m_pEngine = nullptr;
	IFriends *m_pFriends = nullptr;
	IFavorites *m_pFavorites = nullptr;
	IStorage *m_pStorage = nullptr;
	IHttp *m_pHttpClient = nullptr;
	char m_aNetVersion[128];

	bool m_RefreshingHttp = false;
	IServerBrowserHttp *m_pHttp = nullptr;
	IServerBrowserPingCache *m_pPingCache = nullptr;
	const char *m_pHttpPrevBestUrl = nullptr;

	// Entries are owned by m_ServerlistStorage; m_vpServerlist holds non-owning
	// pointers into it. std::deque keeps element pointers stable across growth,
	// which the request list (m_pPrevReq/m_pNextReq) and m_vpServerlist rely on.
	std::deque<CServerEntry> m_ServerlistStorage;
	std::vector<CServerEntry *> m_vpServerlist;
	std::vector<int> m_vSortedServerlist;
	std::unordered_map<NETADDR, int> m_ByAddr;
	std::unordered_map<std::string, int> m_QmClientServerCounts;

	std::vector<CCommunity> m_vCommunities;
	std::unordered_map<NETADDR, CCommunityServer> m_CommunityServersByAddr;

	int m_OwnLocation = CServerInfo::LOC_UNKNOWN;

	CCommunityCache m_CommunityCache;
	CFavoriteCommunityFilterList m_FavoriteCommunitiesFilter;
	CExcludedCommunityFilterList m_CommunitiesFilter;
	CExcludedCommunityCountryFilterList m_CountriesFilter;
	CExcludedCommunityTypeFilterList m_TypesFilter;

	json_value *m_pDDNetInfo = nullptr;
	std::optional<SHA256_DIGEST> m_DDNetInfoSha256;

	CServerEntry *m_pFirstReqServer; // request list
	CServerEntry *m_pLastReqServer;
	int m_NumRequests;

	bool m_NeedResort;
	uint64_t m_FriendListRevision = 0;
	int m_Sorthash;

	// used instead of g_Config.br_max_requests to get more servers
	int m_CurrentMaxRequests;

	int m_NumSortedPlayers;

	int m_ServerlistType;
	int64_t m_BroadcastTime;
	unsigned char m_aTokenSeed[16];

	int GenerateToken(const NETADDR &Addr) const;
	static int GetBasicToken(int Token);
	static int GetExtraToken(int Token);

	// 最近一次由游戏层推送的在线梦客户端分布，排序前物化到 CServerInfo。
	std::unordered_map<std::string, int> m_QmClientServerCounts;
	int QmClientCountForServer(const CServerInfo &Info) const;
	void UpdateQmClientServerCounts();

	// sorting criteria
	bool SortCompareName(int Index1, int Index2) const;
	bool SortCompareMap(int Index1, int Index2) const;
	bool SortComparePing(int Index1, int Index2) const;
	bool SortCompareGametype(int Index1, int Index2) const;
	bool SortCompareNumPlayers(int Index1, int Index2) const;
	bool SortCompareNumClients(int Index1, int Index2) const;
	bool SortCompareNumFriends(int Index1, int Index2) const;
	bool SortCompareNumPlayersAndPing(int Index1, int Index2) const;
	bool SortCompareFavoritesNumPlayersAndPing(int Index1, int Index2) const;
	bool SortCompareQmClients(int Index1, int Index2) const;

	//
	void Filter();
	void Sort();
	int SortHash() const;

	void CleanUp();

	void UpdateFromHttp();
	CServerEntry *Add(const NETADDR *pAddrs, int NumAddrs);
	CServerEntry *ReplaceEntry(CServerEntry *pEntry, const NETADDR *pAddrs, int NumAddrs);

	void RemoveRequest(CServerEntry *pEntry);

	void RequestImpl(const NETADDR &Addr, CServerEntry *pEntry, int *pBasicToken, int *pToken, bool RandomToken) const;

	void RegisterCommands();
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);
	static void Con_AddFavoriteCommunity(IConsole::IResult *pResult, void *pUserData);
	static void Con_RemoveFavoriteCommunity(IConsole::IResult *pResult, void *pUserData);
	static void Con_AddExcludedCommunity(IConsole::IResult *pResult, void *pUserData);
	static void Con_RemoveExcludedCommunity(IConsole::IResult *pResult, void *pUserData);
	static void Con_AddExcludedCountry(IConsole::IResult *pResult, void *pUserData);
	static void Con_RemoveExcludedCountry(IConsole::IResult *pResult, void *pUserData);
	static void Con_AddAllowedCountry(IConsole::IResult *pResult, void *pUserData);
	static void Con_RemoveAllowedCountry(IConsole::IResult *pResult, void *pUserData);
	static void Con_AddExcludedType(IConsole::IResult *pResult, void *pUserData);
	static void Con_RemoveExcludedType(IConsole::IResult *pResult, void *pUserData);
	static void Con_LeakIpAddress(IConsole::IResult *pResult, void *pUserData);

	bool ValidateCommunityId(const char *pCommunityId) const;
	bool ValidateCountryName(const char *pCountryName) const;
	bool ValidateTypeName(const char *pTypeName) const;

	void SetInfo(CServerEntry *pEntry, const CServerInfo &Info);
	void SetLatency(NETADDR Addr, int Latency);

	static bool ParseCommunityFinishes(CCommunity *pCommunity, const json_value &Finishes);
	static bool ParseCommunityServers(CCommunity *pCommunity, const json_value &Servers);

	// TClient
	std::function<void(std::vector<json_value *> &)> m_CustomCommunitiesFunction = nullptr;
	friend class CCustomCommunities;
};

#endif
