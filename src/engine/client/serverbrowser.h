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
#include <memory>
#include <optional>
#include <set>

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
class IHttpRequest;

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
	void Swap(CFavoriteCommunityFilterList &Other) noexcept { m_vEntries.swap(Other.m_vEntries); }
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
	void Swap(CExcludedCommunityFilterList &Other) noexcept { m_Entries.swap(Other.m_Entries); }
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
	// 失败时保留原名单并返回 false，后续刷新可重试；仅发布变化时才需要重新过滤。
	bool AutoExcludeNewCountries() noexcept;
	void Clear() override;
	bool Filtered(const char *pCountryName) const override;
	bool Empty() const override;
	void Clean(const std::vector<CCommunity> &vAllowedCommunities);
	// Clean 的实现主体：以"当前存在的社区"为输入，便于单测覆盖。
	void CleanCountries(const std::set<CCommunityId> &vExistingCommunityIds);
	void Swap(CExcludedCommunityCountryFilterList &Other) noexcept
	{
		m_Entries.swap(Other.m_Entries);
		m_AllowedCountries.swap(Other.m_AllowedCountries);
	}
	void Save(IConfigManager *pConfigManager) const;

private:
	// 确保当前 key 存在保留基线：缺失时按"当前可见的可选国家"（可选国家减去排除名单）建立。
	// 供 AutoExcludeNewCountries 与"用户让某国可见"使用，避免基线只包含刚点的那一个国家，
	// 从而在其他国家出现时把它们误当成新国家排除掉。
	void EnsureAllowedBaseline(const CCommunityId &Key);

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
	void Swap(CExcludedCommunityTypeFilterList &Other) noexcept { m_Entries.swap(Other.m_Entries); }
	void Save(IConfigManager *pConfigManager) const;

private:
	const ICommunityCache *m_pCommunityCache;
	std::map<CCommunityId, std::set<CCommunityTypeName>> m_Entries;
};

class CCommunityCache : public ICommunityCache
{
	friend class CServerBrowser;
	IServerBrowser *m_pServerBrowser;
	std::optional<SHA256_DIGEST> m_InfoSha256;
	int m_LastType = IServerBrowser::NUM_TYPES; // initial value does not appear normally, marking uninitialized cache
	unsigned m_SelectedCommunitiesHash = 0;
	bool m_UpdateFailed = false;
	std::vector<const CCommunity *> m_vpSelectedCommunities;
	std::vector<const CCommunityCountry *> m_vpSelectableCountries;
	std::vector<const CCommunityType *> m_vpSelectableTypes;
	bool m_AnyRanksAvailable = false;
	bool m_CountryTypesFilterAvailable = false;
	// 社区按人数重排会交换 ID 所在的槽位，筛选键由缓存自己保存。
	char m_aCountryTypeFilterKey[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	void Prepare(std::vector<const CCommunity *> vpSelected, std::optional<SHA256_DIGEST> InfoSha256, int Type, unsigned CommunitiesHash);
	void Swap(CCommunityCache &Other) noexcept;

public:
	CCommunityCache(IServerBrowser *pServerBrowser) :
		m_pServerBrowser(pServerBrowser)
	{
		str_copy(m_aCountryTypeFilterKey, IServerBrowser::COMMUNITY_ALL);
	}

	void Update(bool Force) override;
	// 作废时立即清除借用指针；下次重建即使失败，也不能再向 UI 返回旧容器中的元素。
	void Invalidate() noexcept
	{
		m_InfoSha256.reset();
		m_LastType = IServerBrowser::NUM_TYPES;
		m_vpSelectedCommunities.clear();
		m_vpSelectableCountries.clear();
		m_vpSelectableTypes.clear();
		m_AnyRanksAvailable = false;
		m_CountryTypesFilterAvailable = false;
	}
	const std::vector<const CCommunity *> &SelectedCommunities() const override { return m_vpSelectedCommunities; }
	const std::vector<const CCommunityCountry *> &SelectableCountries() const override { return m_vpSelectableCountries; }
	const std::vector<const CCommunityType *> &SelectableTypes() const override { return m_vpSelectableTypes; }
	bool AnyRanksAvailable() const override { return m_AnyRanksAvailable; }
	bool CountriesTypesFilterAvailable() const override { return m_CountryTypesFilterAvailable; }
	const char *CountryTypeFilterKey() const override { return m_aCountryTypeFilterKey; }
};

class CServerBrowser : public IServerBrowser
{
public:
	// 注入时钟使用 time_get 的单调刻度；空回调使用生产时钟。
	explicit CServerBrowser(std::function<int64_t()> UpdateClock = {});
	~CServerBrowser() override;

	// interface functions
	void Shutdown() override;
	void Refresh(int Type, bool Force = false) override;
	void RefreshHttpServerList() override;
	bool IsRefreshing() const override;
	bool IsGettingServerlist() const override;
	bool IsServerlistError() const override;
	bool IsServerlistStale() const override;
	int LoadingProgression() const override;
	uint64_t FriendListRevision() const override { return m_FriendListRevision; }
	int GetCurrentClientLocation() const override { return DetermineOwnLocation(); }
	void RequestResort() { m_NeedResort = true; }

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

	const json_value *LoadDDNetInfo(IHttpRequest *pResponse = nullptr);
	bool LoadDDNetServers();
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
	class CServerListUpdate;
	struct CSortBackup;
	class CSortUpdate;
	class CCommunityUpdate;

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
	bool m_HttpUpdateFailed = false;
	int64_t m_HttpUpdateRetryAt = 0;
	std::function<int64_t()> m_UpdateClock;
	uint64_t m_HttpRevision = 0;
	IServerBrowserHttp *m_pHttp = nullptr;
	IServerBrowserPingCache *m_pPingCache = nullptr;

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
	bool m_CommunityUpdateFailed = false;
	int64_t m_CommunityUpdateRetryAt = 0;

	CServerEntry *m_pFirstReqServer; // request list
	CServerEntry *m_pLastReqServer;
	int m_NumRequests;

	bool m_NeedResort;
	bool m_SortUpdateFailed = false;
	int64_t m_SortUpdateRetryAt = 0;
	std::unique_ptr<CSortBackup> m_pSortBackup;
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
	bool SortCompareDifficulty(int Index1, int Index2) const;

	//
	void Filter();
	void Sort();
	bool TrySort();
	void SortCommunities();
	int SortHash() const;

	void CleanUp();

	void UpdateFromHttp();
	bool TryUpdateFromHttp();
	int64_t UpdateNow() const;
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

	// QmClient: 难度星级由客户端注入（Qm 地图难度目录），未注入或未收录返回 -1。
	std::function<int(const CServerInfo *)> m_MapDifficultyStarsFunction = nullptr;
	friend class CMenus;
};

#endif
