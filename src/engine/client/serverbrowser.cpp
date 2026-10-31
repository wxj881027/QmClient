/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "serverbrowser.h"

#include "server_info_update.h"
#include "serverbrowser_addresses.h"
#include "serverbrowser_http.h"
#include "serverbrowser_ping_cache.h"

#include <base/hash_ctxt.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/http_response_validation.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/masterserver.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/serverinfo.h>
#include <engine/storage.h>

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

	class CSortWrap
	{
		typedef bool (CServerBrowser::*SortFunc)(int, int) const;
		SortFunc m_pfnSort;
		CServerBrowser *m_pThis;

	public:
		CSortWrap(CServerBrowser *pServer, SortFunc Func) :
			m_pfnSort(Func), m_pThis(pServer) {}
		bool operator()(int a, int b) { return (g_Config.m_BrSortOrder ? (m_pThis->*m_pfnSort)(b, a) : (m_pThis->*m_pfnSort)(a, b)); }
	};

	bool MatchesPart(const char *a, const char *b)
	{
		return str_utf8_find_nocase(a, b) != nullptr;
	}

	bool MatchesExactly(const char *a, const char *b)
	{
		return str_comp(a, b) == 0;
	}

	struct SServerFilterToken
	{
		std::string m_Text;
		bool (*m_pfnMatches)(const char *, const char *);
	};

	template<size_t N>
	std::vector<SServerFilterToken> ParseServerFilterTokens(const char (&aInput)[N])
	{
		std::vector<SServerFilterToken> vTokens;
		const char *pStr = aInput;
		char aToken[N];
		char aTrimmed[N];
		while((pStr = str_next_token(pStr, IServerBrowser::SEARCH_EXCLUDE_TOKEN, aToken, sizeof(aToken))))
		{
			str_copy(aTrimmed, str_utf8_skip_whitespaces(aToken));
			str_utf8_trim_right(aTrimmed);
			const int Length = str_length(aTrimmed);
			if(Length == 0)
				continue;
			if(aTrimmed[0] == '"' && aTrimmed[Length - 1] == '"')
				vTokens.push_back({Length > 1 ? std::string(aTrimmed + 1, Length - 2) : std::string(), MatchesExactly});
			else
				vTokens.push_back({aTrimmed, MatchesPart});
		}
		return vTokens;
	}

	NETADDR CommunityAddressKey(const NETADDR &Addr)
	{
		NETADDR AddressKey = Addr;
		AddressKey.type &= ~NETTYPE_TW7;
		return AddressKey;
	}

	const CCommunity *FindCommunity(const std::vector<CCommunity> &vCommunities, const char *pId)
	{
		const auto Found = std::find_if(vCommunities.begin(), vCommunities.end(), [pId](const auto &Community) {
			return str_comp(Community.Id(), pId) == 0;
		});
		return Found == vCommunities.end() ? nullptr : &*Found;
	}

	std::vector<const CCommunity *> CollectFavorites(const std::vector<CCommunity> &vCommunities, const CFavoriteCommunityFilterList &Favorites)
	{
		std::vector<const CCommunity *> vpResult;
		for(const auto &Id : Favorites.Entries())
		{
			if(const auto *pCommunity = FindCommunity(vCommunities, Id.Id()))
				vpResult.push_back(pCommunity);
		}
		return vpResult;
	}

	std::vector<const CCommunity *> CollectCurrentCommunities(const std::vector<CCommunity> &vCommunities, int Type, const CFavoriteCommunityFilterList &Favorites, const CExcludedCommunityFilterList &Excluded)
	{
		std::vector<const CCommunity *> vpResult;
		if(Type == IServerBrowser::TYPE_INTERNET || Type == IServerBrowser::TYPE_FAVORITES)
		{
			for(const auto &Community : vCommunities)
			{
				if(!Excluded.Filtered(Community.Id()))
					vpResult.push_back(&Community);
			}
		}
		else if(Type >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && Type <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5)
		{
			const auto vpFavorites = CollectFavorites(vCommunities, Favorites);
			const size_t Index = Type - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
			if(Index < vpFavorites.size())
				vpResult.push_back(vpFavorites[Index]);
		}
		return vpResult;
	}

	void SetCommunityInfo(CServerInfo *pInfo, const std::unordered_map<NETADDR, CCommunityServer> &ServersByAddr)
	{
		for(int Index = 0; Index < pInfo->m_NumAddresses; ++Index)
		{
			const auto Found = ServersByAddr.find(CommunityAddressKey(pInfo->m_aAddresses[Index]));
			if(Found != ServersByAddr.end())
			{
				str_copy(pInfo->m_aCommunityId, Found->second.CommunityId());
				str_copy(pInfo->m_aCommunityCountry, Found->second.CountryName());
				str_copy(pInfo->m_aCommunityType, Found->second.TypeName());
				return;
			}
		}
		str_copy(pInfo->m_aCommunityId, IServerBrowser::COMMUNITY_NONE);
		str_copy(pInfo->m_aCommunityCountry, IServerBrowser::COMMUNITY_COUNTRY_NONE);
		str_copy(pInfo->m_aCommunityType, IServerBrowser::COMMUNITY_TYPE_NONE);
	}

	void SetRankInfo(CServerInfo *pInfo, const std::vector<CCommunity> &vCommunities)
	{
		const auto *pCommunity = FindCommunity(vCommunities, pInfo->m_aCommunityId);
		pInfo->m_HasRank = pCommunity == nullptr ? CServerInfo::RANK_UNAVAILABLE : pCommunity->HasRank(pInfo->m_aMap);
	}

	bool PreserveUnloadedCustomFilters(bool HasCustomSource)
	{
		return g_Config.m_QmCustomCommunitiesUrl[0] != '\0' && !HasCustomSource;
	}

	unsigned HashCurrentCommunities(const std::vector<CCommunity> &vCommunities, int Type, const CFavoriteCommunityFilterList &Favorites, const CExcludedCommunityFilterList &Excluded)
	{
		unsigned Hash = 5381;
		if(Type == IServerBrowser::TYPE_INTERNET || Type == IServerBrowser::TYPE_FAVORITES)
		{
			// 每帧检查缓存时直接遍历，避免为了计算哈希创建临时指针数组。
			for(const auto &Community : vCommunities)
			{
				if(!Excluded.Filtered(Community.Id()))
					Hash = (Hash << 5) + Hash + str_quickhash(Community.Id());
			}
		}
		else if(Type >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && Type <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5)
		{
			const size_t CommunityIndex = Type - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
			size_t CurrentIndex = 0;
			for(const auto &CommunityId : Favorites.Entries())
			{
				const CCommunity *pCommunity = FindCommunity(vCommunities, CommunityId.Id());
				if(pCommunity == nullptr)
					continue;
				if(CurrentIndex == CommunityIndex)
				{
					Hash = (Hash << 5) + Hash + str_quickhash(pCommunity->Id());
					break;
				}
				++CurrentIndex;
			}
		}
		return Hash;
	}

} // namespace

// 只备份筛选与排序会改写的字段；容量跨次复用，不复制服务器文本和客户端数组。
struct CServerBrowser::CSortBackup
{
	struct CEntry
	{
		CServerEntry *m_pEntry;
		TRISTATE m_Favorite;
		TRISTATE m_FavoriteAllowPing;
		int m_NumFilteredPlayers;
		int m_QuickSearchHit;
		int m_FriendState;
		int m_FriendNum;
		uint64_t m_FriendRevision;
		bool m_FriendStateValid;
		bool m_FriendStateIgnoreClan;
		bool m_RestoreClientFriends;
	};
	std::vector<CEntry> m_vEntries;
	std::vector<int> m_vClientFriends;
	std::vector<int> m_vSorted;
	std::vector<int> m_vCommunityPlayers;

	void Clear() noexcept
	{
		// 事务结束后不保留借用条目，HTTP 重建和 CleanUp 可以正常释放旧列表。
		m_vEntries.clear();
		m_vClientFriends.clear();
		m_vSorted.clear();
		m_vCommunityPlayers.clear();
	}
};

CServerBrowser::CServerBrowser(std::function<int64_t()> UpdateClock) :
	m_UpdateClock(std::move(UpdateClock)),
	m_CommunityCache(this),
	m_CountriesFilter(&m_CommunityCache),
	m_TypesFilter(&m_CommunityCache)
{
	m_NeedResort = false;
	m_Sorthash = 0;

	m_ServerlistType = 0;
	m_BroadcastTime = 0;
	secure_random_fill(m_aTokenSeed, sizeof(m_aTokenSeed));

	CleanUp();
}

CServerBrowser::~CServerBrowser()
{
	json_value_free(m_pDDNetInfo);

	delete m_pHttp;
	m_pHttp = nullptr;
	delete m_pPingCache;
	m_pPingCache = nullptr;
}

void CServerBrowser::Shutdown()
{
	if(m_pHttp != nullptr)
	{
		m_pHttp->Shutdown();
	}
}

void CServerBrowser::SetBaseInfo(class CNetClient *pClient, const char *pNetVersion)
{
	m_pNetClient = pClient;
	str_copy(m_aNetVersion, pNetVersion);
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pFavorites = Kernel()->RequestInterface<IFavorites>();
	m_pFriends = Kernel()->RequestInterface<IFriends>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttpClient = Kernel()->RequestInterface<IHttp>();
	m_pPingCache = CreateServerBrowserPingCache(m_pConsole, m_pStorage);

	RegisterCommands();
}

void CServerBrowser::OnInit()
{
	m_pHttp = CreateServerBrowserHttp(m_pEngine, m_pStorage, m_pHttpClient, g_Config.m_BrCachedBestServerinfoUrl);
}

void CServerBrowser::RegisterCommands()
{
	m_pConfigManager->RegisterCallback(CServerBrowser::ConfigSaveCallback, this);
	m_pConsole->Register("add_favorite_community", "s[community_id]", CFGFLAG_CLIENT, Con_AddFavoriteCommunity, this, "Add a community as a favorite");
	m_pConsole->Register("remove_favorite_community", "s[community_id]", CFGFLAG_CLIENT, Con_RemoveFavoriteCommunity, this, "Remove a community from the favorites");
	m_pConsole->Register("add_excluded_community", "s[community_id]", CFGFLAG_CLIENT, Con_AddExcludedCommunity, this, "Add a community to the exclusion filter");
	m_pConsole->Register("remove_excluded_community", "s[community_id]", CFGFLAG_CLIENT, Con_RemoveExcludedCommunity, this, "Remove a community from the exclusion filter");
	m_pConsole->Register("add_excluded_country", "s[community_id] s[country_code]", CFGFLAG_CLIENT, Con_AddExcludedCountry, this, "Add a country to the exclusion filter for a specific community (ISO 3166-1 numeric)");
	m_pConsole->Register("remove_excluded_country", "s[community_id] s[country_code]", CFGFLAG_CLIENT, Con_RemoveExcludedCountry, this, "Remove a country from the exclusion filter for a specific community (ISO 3166-1 numeric)");
	m_pConsole->Register("add_allowed_country", "s[community_id] s[country_code]", CFGFLAG_CLIENT, Con_AddAllowedCountry, this, "Add a country to the allowed filter list for a specific community (kept visible while its country filter is active)");
	m_pConsole->Register("remove_allowed_country", "s[community_id] s[country_code]", CFGFLAG_CLIENT, Con_RemoveAllowedCountry, this, "Remove a country from the allowed filter list for a specific community");
	m_pConsole->Register("add_excluded_type", "s[community_id] s[type]", CFGFLAG_CLIENT, Con_AddExcludedType, this, "Add a type to the exclusion filter for a specific community");
	m_pConsole->Register("remove_excluded_type", "s[community_id] s[type]", CFGFLAG_CLIENT, Con_RemoveExcludedType, this, "Remove a type from the exclusion filter for a specific community");
	m_pConsole->Register("leak_ip_address_to_all_servers", "", CFGFLAG_CLIENT, Con_LeakIpAddress, this, "Leaks your IP address to all servers by pinging each of them, also acquiring the latency in the process");
#if defined(CONF_QMCLIENT_TEST_STORAGE)
	// 隔离进程测试查询实际消费者；普通发布客户端不注册测试命令。
	m_pConsole->Register("qm_serverlist_status", "i[token]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		auto *pThis = static_cast<CServerBrowser *>(pUserData);
		const auto *pFirst = pThis->Get(0);
		log_info("test/serverlist", "token=%d count=%d revision=%llu stale=%d error=%d name=%s", pResult->GetInteger(0), pThis->NumServers(), (unsigned long long)pThis->m_HttpRevision, pThis->m_pHttp->IsStale(), pThis->m_pHttp->IsError(), pFirst ? pFirst->m_aName : ""); }, this, "Query isolated serverlist consumer state");
	m_pConsole->Register("qm_serverlist_refresh", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUserData) { static_cast<CServerBrowser *>(pUserData)->Refresh(TYPE_INTERNET, true); }, this, "Refresh isolated serverlist consumer");
#endif
}

void CServerBrowser::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	pThis->FavoriteCommunitiesFilter().Save(pConfigManager);
	pThis->CommunitiesFilter().Save(pConfigManager);
	pThis->CountriesFilter().Save(pConfigManager);
	pThis->TypesFilter().Save(pConfigManager);
}

void CServerBrowser::Con_AddFavoriteCommunity(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	if(!pThis->ValidateCommunityId(pCommunityId))
		return;
	pThis->FavoriteCommunitiesFilter().Add(pCommunityId);
}

void CServerBrowser::Con_RemoveFavoriteCommunity(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	if(!pThis->ValidateCommunityId(pCommunityId))
		return;
	pThis->FavoriteCommunitiesFilter().Remove(pCommunityId);
}

void CServerBrowser::Con_AddExcludedCommunity(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	if(!pThis->ValidateCommunityId(pCommunityId))
		return;
	pThis->CommunitiesFilter().Add(pCommunityId);
}

void CServerBrowser::Con_RemoveExcludedCommunity(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	if(!pThis->ValidateCommunityId(pCommunityId))
		return;
	pThis->CommunitiesFilter().Remove(pCommunityId);
}

void CServerBrowser::Con_AddExcludedCountry(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pCountryName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateCountryName(pCountryName))
		return;
	pThis->CountriesFilter().Add(pCommunityId, pCountryName);
}

void CServerBrowser::Con_RemoveExcludedCountry(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pCountryName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateCountryName(pCountryName))
		return;
	pThis->CountriesFilter().Remove(pCommunityId, pCountryName);
}

void CServerBrowser::Con_AddAllowedCountry(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pCountryName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateCountryName(pCountryName))
		return;
	pThis->CountriesFilter().AddAllowed(pCommunityId, pCountryName);
}

void CServerBrowser::Con_RemoveAllowedCountry(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pCountryName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateCountryName(pCountryName))
		return;
	pThis->CountriesFilter().RemoveAllowed(pCommunityId, pCountryName);
}

void CServerBrowser::Con_AddExcludedType(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pTypeName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateTypeName(pTypeName))
		return;
	pThis->TypesFilter().Add(pCommunityId, pTypeName);
}

void CServerBrowser::Con_RemoveExcludedType(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);
	const char *pCommunityId = pResult->GetString(0);
	const char *pTypeName = pResult->GetString(1);
	if(!pThis->ValidateCommunityId(pCommunityId) || !pThis->ValidateTypeName(pTypeName))
		return;
	pThis->TypesFilter().Remove(pCommunityId, pTypeName);
}

void CServerBrowser::Con_LeakIpAddress(IConsole::IResult *pResult, void *pUserData)
{
	CServerBrowser *pThis = static_cast<CServerBrowser *>(pUserData);

	// We only consider the first address of every server.

	std::vector<int> vSortedServers;
	// Sort servers by IP address, ignoring port.
	class CAddrComparer
	{
	public:
		CServerBrowser *m_pThis;
		bool operator()(int i, int j) const
		{
			NETADDR Addr1 = m_pThis->m_vpServerlist[i]->m_Info.m_aAddresses[0];
			NETADDR Addr2 = m_pThis->m_vpServerlist[j]->m_Info.m_aAddresses[0];
			Addr1.port = 0;
			Addr2.port = 0;
			return net_addr_comp(&Addr1, &Addr2) < 0;
		}
	};
	vSortedServers.reserve(pThis->m_vpServerlist.size());
	for(int i = 0; i < (int)pThis->m_vpServerlist.size(); i++)
	{
		vSortedServers.push_back(i);
	}
	std::sort(vSortedServers.begin(), vSortedServers.end(), CAddrComparer{pThis});

	// Group the servers into those with same IP address (but differing
	// port).
	NETADDR Addr;
	int Start = -1;
	for(int i = 0; i <= (int)vSortedServers.size(); i++)
	{
		NETADDR NextAddr;
		if(i < (int)vSortedServers.size())
		{
			NextAddr = pThis->m_vpServerlist[vSortedServers[i]]->m_Info.m_aAddresses[0];
			NextAddr.port = 0;
		}
		bool New = Start == -1 || i == (int)vSortedServers.size() || net_addr_comp(&Addr, &NextAddr) != 0;
		if(Start != -1 && New)
		{
			int Chosen = Start + secure_rand_below(i - Start);
			CServerEntry *pChosen = pThis->m_vpServerlist[vSortedServers[Chosen]];
			pChosen->m_RequestIgnoreInfo = true;
			pThis->QueueRequest(pChosen);
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&pChosen->m_Info.m_aAddresses[0], aAddr, sizeof(aAddr), true);
			dbg_msg("serverbrowser", "queuing ping request for %s", aAddr);
		}
		if(i < (int)vSortedServers.size() && New)
		{
			Start = i;
			Addr = NextAddr;
		}
	}
}

static bool ValidIdentifier(const char *pId, size_t MaxLength)
{
	if(pId[0] == '\0' || (size_t)str_length(pId) >= MaxLength)
	{
		return false;
	}

	for(int i = 0; pId[i] != '\0'; ++i)
	{
		if(pId[i] == '"' || pId[i] == '/' || pId[i] == '\\')
		{
			return false;
		}
	}
	return true;
}

static bool ValidateIdentifier(const char *pId, size_t MaxLength, const char *pContext, IConsole *pConsole)
{
	if(!ValidIdentifier(pId, MaxLength))
	{
		char aError[32 + IConsole::CMDLINE_LENGTH];
		str_format(aError, sizeof(aError), "%s '%s' is not valid", pContext, pId);
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowser", aError);
		return false;
	}
	return true;
}

bool CServerBrowser::ValidateCommunityId(const char *pCommunityId) const
{
	return ValidateIdentifier(pCommunityId, CServerInfo::MAX_COMMUNITY_ID_LENGTH, "Community ID", m_pConsole);
}

bool CServerBrowser::ValidateCountryName(const char *pCountryName) const
{
	return ValidateIdentifier(pCountryName, CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH, "Country name", m_pConsole);
}

bool CServerBrowser::ValidateTypeName(const char *pTypeName) const
{
	return ValidateIdentifier(pTypeName, CServerInfo::MAX_COMMUNITY_TYPE_LENGTH, "Type name", m_pConsole);
}

int CServerBrowser::Players(const CServerInfo &Item) const
{
	return g_Config.m_BrFilterSpectators ? Item.m_NumPlayers : Item.m_NumClients;
}

int CServerBrowser::Max(const CServerInfo &Item) const
{
	return g_Config.m_BrFilterSpectators ? Item.m_MaxPlayers : Item.m_MaxClients;
}

const CServerInfo *CServerBrowser::SortedGet(int Index) const
{
	if(Index < 0 || Index >= (int)m_vSortedServerlist.size())
		return nullptr;
	return &m_vpServerlist[m_vSortedServerlist[Index]]->m_Info;
}

const CServerInfo *CServerBrowser::Get(int Index) const
{
	if(Index < 0 || Index >= (int)m_vpServerlist.size())
		return nullptr;
	return &m_vpServerlist[Index]->m_Info;
}

uint64_t CServerBrowser::HttpListRevision() const
{
	return m_pHttp != nullptr ? m_pHttp->Revision() : 0;
}

int CServerBrowser::NumHttpServers() const
{
	return m_pHttp != nullptr ? m_pHttp->NumServers() : 0;
}

const CServerInfo *CServerBrowser::HttpGet(int Index) const
{
	if(Index < 0 || m_pHttp == nullptr || Index >= m_pHttp->NumServers())
		return nullptr;
	return &m_pHttp->Server(Index);
}

int CServerBrowser::GenerateToken(const NETADDR &Addr) const
{
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	sha256_update(&Sha256, m_aTokenSeed, sizeof(m_aTokenSeed));
	sha256_update(&Sha256, (unsigned char *)&Addr, sizeof(Addr));
	SHA256_DIGEST Digest = sha256_finish(&Sha256);
	return (Digest.data[0] << 16) | (Digest.data[1] << 8) | Digest.data[2];
}

int CServerBrowser::GetBasicToken(int Token)
{
	return Token & 0xff;
}

int CServerBrowser::GetExtraToken(int Token)
{
	return Token >> 8;
}

bool CServerBrowser::SortCompareName(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	//	make sure empty entries are listed last
	return (pIndex1->m_GotInfo && pIndex2->m_GotInfo) || (!pIndex1->m_GotInfo && !pIndex2->m_GotInfo) ? str_comp(pIndex1->m_Info.m_aName, pIndex2->m_Info.m_aName) < 0 :
													    pIndex1->m_GotInfo != 0;
}

bool CServerBrowser::SortCompareMap(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	return str_comp(pIndex1->m_Info.m_aMap, pIndex2->m_Info.m_aMap) < 0;
}

bool CServerBrowser::SortComparePing(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	return pIndex1->m_Info.m_Latency < pIndex2->m_Info.m_Latency;
}

bool CServerBrowser::SortCompareGametype(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	return str_comp(pIndex1->m_Info.m_aGameType, pIndex2->m_Info.m_aGameType) < 0;
}

bool CServerBrowser::SortCompareNumPlayers(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	return pIndex1->m_Info.m_NumFilteredPlayers > pIndex2->m_Info.m_NumFilteredPlayers;
}

bool CServerBrowser::SortCompareNumClients(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];
	return pIndex1->m_Info.m_NumClients > pIndex2->m_Info.m_NumClients;
}

bool CServerBrowser::SortCompareNumFriends(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];

	if(pIndex1->m_Info.m_FriendNum == pIndex2->m_Info.m_FriendNum)
		return pIndex1->m_Info.m_NumFilteredPlayers > pIndex2->m_Info.m_NumFilteredPlayers;
	else
		return pIndex1->m_Info.m_FriendNum > pIndex2->m_Info.m_FriendNum;
}

bool CServerBrowser::SortCompareNumPlayersAndPing(int Index1, int Index2) const
{
	CServerEntry *pIndex1 = m_vpServerlist[Index1];
	CServerEntry *pIndex2 = m_vpServerlist[Index2];

	if(pIndex1->m_Info.m_NumFilteredPlayers == pIndex2->m_Info.m_NumFilteredPlayers)
		return pIndex1->m_Info.m_Latency > pIndex2->m_Info.m_Latency;
	else if(pIndex1->m_Info.m_NumFilteredPlayers == 0 || pIndex2->m_Info.m_NumFilteredPlayers == 0 || pIndex1->m_Info.m_Latency / 100 == pIndex2->m_Info.m_Latency / 100)
		return pIndex1->m_Info.m_NumFilteredPlayers < pIndex2->m_Info.m_NumFilteredPlayers;
	else
		return pIndex1->m_Info.m_Latency > pIndex2->m_Info.m_Latency;
}

bool CServerBrowser::SortCompareFavoritesNumPlayersAndPing(int Index1, int Index2) const
{
	const CServerEntry *pIndex1 = m_vpServerlist[Index1];
	const CServerEntry *pIndex2 = m_vpServerlist[Index2];
	const bool IsFavorite1 = pIndex1->m_Info.m_Favorite != TRISTATE::NONE;
	const bool IsFavorite2 = pIndex2->m_Info.m_Favorite != TRISTATE::NONE;
	if(IsFavorite1 == IsFavorite2)
		return SortCompareNumPlayersAndPing(Index1, Index2);
	return IsFavorite1 && !IsFavorite2;
}

bool CServerBrowser::SortCompareQmClients(int Index1, int Index2) const
{
	return m_vpServerlist[Index1]->m_Info.m_QmClientCount > m_vpServerlist[Index2]->m_Info.m_QmClientCount;
}

bool CServerBrowser::SortCompareDifficulty(int Index1, int Index2) const
{
	// 难度星级来自客户端注入的地图难度目录；未注入或未收录（-1）稳定排后，
	// 同星级的相对顺序交给 stable_sort 保持，不在比较器里引入次级键。
	const int Stars1 = m_MapDifficultyStarsFunction ? m_MapDifficultyStarsFunction(&m_vpServerlist[Index1]->m_Info) : -1;
	const int Stars2 = m_MapDifficultyStarsFunction ? m_MapDifficultyStarsFunction(&m_vpServerlist[Index2]->m_Info) : -1;
	if(Stars1 < 0 || Stars2 < 0)
		return Stars2 < 0 && Stars1 >= 0;
	return Stars1 < Stars2;
}

bool CServerBrowser::SetQmClientServerCounts(const std::unordered_map<std::string, int> &Counts)
{
	if(m_QmClientServerCounts == Counts)
		return true;
	try
	{
		auto Candidate = Counts;
		std::vector<int> vCounts;
		vCounts.reserve(m_vpServerlist.size());
		for(const CServerEntry *pEntry : m_vpServerlist)
		{
			int Count = 0;
			if(!Candidate.empty() && pEntry->m_Info.m_NumAddresses > 0)
			{
				char aAddress[NETADDR_MAXSTRSIZE];
				net_addr_str(&pEntry->m_Info.m_aAddresses[0], aAddress, sizeof(aAddress), true);
				const auto It = Candidate.find(aAddress);
				if(It != Candidate.end())
					Count = It->second;
			}
			vCounts.push_back(Count);
		}
		// 地址查找也可能分配；全部准备成功后才更换索引与现有条目。
		m_QmClientServerCounts.swap(Candidate);
		for(size_t i = 0; i < m_vpServerlist.size(); ++i)
			m_vpServerlist[i]->m_Info.m_QmClientCount = vCounts[i];
		if(g_Config.m_BrSort == IServerBrowser::SORT_QM_CLIENTS)
			RequestResort();
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}

int CServerBrowser::QmClientCountForServer(const CServerInfo &Info) const
{
	if(m_QmClientServerCounts.empty() || Info.m_NumAddresses <= 0)
		return 0;
	char aAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Info.m_aAddresses[0], aAddress, sizeof(aAddress), true);
	const auto It = m_QmClientServerCounts.find(aAddress);
	return It == m_QmClientServerCounts.end() ? 0 : It->second;
}

void CServerBrowser::Filter()
{
	m_NumSortedPlayers = 0;
	const uint64_t FriendsRevision = m_pFriends->Revision();
	const bool IgnoreClan = g_Config.m_ClFriendsIgnoreClan != 0;
	const auto vFilterTokens = ParseServerFilterTokens(g_Config.m_BrFilterString);
	const auto vExcludeTokens = ParseServerFilterTokens(g_Config.m_BrExcludeString);

	m_vSortedServerlist.clear();
	m_vSortedServerlist.reserve(m_vpServerlist.size());

	for(auto &Community : m_vCommunities)
	{
		Community.m_NumPlayers = 0;
	}

	// filter the servers
	for(int ServerIndex = 0; ServerIndex < (int)m_vpServerlist.size(); ServerIndex++)
	{
		CServerEntry *pEntry = m_vpServerlist[ServerIndex];
		CServerInfo &Info = pEntry->m_Info;
		bool Filtered = false;

		if(g_Config.m_QmMapBrowserEmptyOnly && Info.m_NumFilteredPlayers != 0)
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterEmpty && Info.m_NumFilteredPlayers == 0)
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterFull && Players(Info) == Max(Info))
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterPw && Info.m_Flags & SERVER_FLAG_PASSWORD)
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterServerAddress[0] && !str_find_nocase(Info.m_aAddress, g_Config.m_BrFilterServerAddress))
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterGametypeStrict && g_Config.m_BrFilterGametype[0] && str_comp_nocase(Info.m_aGameType, g_Config.m_BrFilterGametype))
		{
			Filtered = true;
		}
		else if(!g_Config.m_BrFilterGametypeStrict && g_Config.m_BrFilterGametype[0] && !str_utf8_find_nocase(Info.m_aGameType, g_Config.m_BrFilterGametype))
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterUnfinishedMap && Info.m_HasRank == CServerInfo::RANK_RANKED)
		{
			Filtered = true;
		}
		else if(g_Config.m_BrFilterLogin && Info.m_RequiresLogin)
		{
			Filtered = true;
		}
		else
		{
			if(!Communities().empty())
			{
				if(m_ServerlistType == IServerBrowser::TYPE_INTERNET || m_ServerlistType == IServerBrowser::TYPE_FAVORITES)
				{
					Filtered = CommunitiesFilter().Filtered(Info.m_aCommunityId);
				}
				if(m_ServerlistType == IServerBrowser::TYPE_INTERNET || m_ServerlistType == IServerBrowser::TYPE_FAVORITES ||
					(m_ServerlistType >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && m_ServerlistType <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5))
				{
					Filtered = Filtered || CountriesFilter().Filtered(Info.m_aCommunityCountry);
					Filtered = Filtered || TypesFilter().Filtered(Info.m_aCommunityType);
				}
			}

			if(!Filtered && g_Config.m_BrFilterCountry)
			{
				Filtered = true;
				// match against player country
				for(const auto &Client : Info.m_vClients)
				{
					if(Client.m_Country == g_Config.m_BrFilterCountryIndex)
					{
						Filtered = false;
						break;
					}
				}
			}

			if(!Filtered && g_Config.m_BrFilterString[0] != '\0')
			{
				Info.m_QuickSearchHit = 0;

				for(const SServerFilterToken &Token : vFilterTokens)
				{
					const auto MatchesFn = Token.m_pfnMatches;
					const char *pFilterStr = Token.m_Text.c_str();

					// match against server name
					if(MatchesFn(Info.m_aName, pFilterStr))
					{
						Info.m_QuickSearchHit |= IServerBrowser::QUICK_SERVERNAME;
					}

					// match against players
					for(const auto &Client : Info.m_vClients)
					{
						if(MatchesFn(Client.m_aName, pFilterStr) ||
							MatchesFn(Client.m_aClan, pFilterStr))
						{
							if(g_Config.m_BrFilterConnectingPlayers &&
								str_comp(Client.m_aName, "(connecting)") == 0 &&
								Client.m_aClan[0] == '\0')
							{
								continue;
							}
							Info.m_QuickSearchHit |= IServerBrowser::QUICK_PLAYER;
							break;
						}
					}

					// match against map
					if(MatchesFn(Info.m_aMap, pFilterStr))
					{
						Info.m_QuickSearchHit |= IServerBrowser::QUICK_MAPNAME;
					}
				}

				if(!Info.m_QuickSearchHit)
					Filtered = true;
			}

			if(!Filtered && g_Config.m_BrExcludeString[0] != '\0')
			{
				for(const SServerFilterToken &Token : vExcludeTokens)
				{
					const auto MatchesFn = Token.m_pfnMatches;
					const char *pExcludeStr = Token.m_Text.c_str();

					// match against server name
					if(MatchesFn(Info.m_aName, pExcludeStr))
					{
						Filtered = true;
						break;
					}

					// match against map
					if(MatchesFn(Info.m_aMap, pExcludeStr))
					{
						Filtered = true;
						break;
					}

					// match against gametype
					if(MatchesFn(Info.m_aGameType, pExcludeStr))
					{
						Filtered = true;
						break;
					}
				}
			}
		}

		if(!pEntry->m_FriendStateValid || pEntry->m_FriendStateRevision != FriendsRevision || pEntry->m_FriendStateIgnoreClan != IgnoreClan)
		{
			UpdateServerFriends(&Info);
			pEntry->m_FriendStateRevision = FriendsRevision;
			pEntry->m_FriendStateIgnoreClan = IgnoreClan;
			pEntry->m_FriendStateValid = true;
		}

		if(!Filtered)
		{
			if(!g_Config.m_BrFilterFriends || Info.m_FriendState != IFriends::FRIEND_NO)
			{
				m_NumSortedPlayers += Info.m_NumFilteredPlayers;
				m_vSortedServerlist.push_back(ServerIndex);
			}
		}

		if(Info.m_NumClients > 0)
		{
			auto Community = std::find_if(m_vCommunities.begin(), m_vCommunities.end(), [&Info](const auto &Elem) {
				return str_comp(Elem.Id(), Info.m_aCommunityId) == 0;
			});
			if(Community != m_vCommunities.end())
			{
				Community->m_NumPlayers += Info.m_NumClients;
			}
		}
	}
}

int CServerBrowser::SortHash() const
{
	int i = g_Config.m_BrSort & 0xff;
	i |= g_Config.m_BrFilterEmpty << 4;
	i |= g_Config.m_BrFilterFull << 5;
	i |= g_Config.m_BrFilterSpectators << 6;
	i |= g_Config.m_BrFilterFriends << 7;
	i |= g_Config.m_BrFilterPw << 8;
	i |= g_Config.m_BrSortOrder << 9;
	i |= g_Config.m_BrFilterGametypeStrict << 12;
	i |= g_Config.m_BrFilterUnfinishedMap << 13;
	i |= g_Config.m_BrFilterCountry << 14;
	i |= g_Config.m_BrFilterConnectingPlayers << 15;
	i |= g_Config.m_BrFilterLogin << 16;
	i |= g_Config.m_QmMapBrowserEmptyOnly << 17;
	return i;
}

class CServerBrowser::CSortUpdate
{
	CServerBrowser &m_Browser;
	uint64_t m_FriendRevision;
	int m_NumSortedPlayers;
	int m_Sorthash;
	bool m_NeedResort;
	bool m_Prepared = false;
	bool m_Committed = false;

public:
	explicit CSortUpdate(CServerBrowser &Browser) :
		m_Browser(Browser),
		m_FriendRevision(Browser.m_FriendListRevision),
		m_NumSortedPlayers(Browser.m_NumSortedPlayers),
		m_Sorthash(Browser.m_Sorthash),
		m_NeedResort(Browser.m_NeedResort)
	{
	}
	CSortUpdate(const CSortUpdate &) = delete;
	CSortUpdate &operator=(const CSortUpdate &) = delete;
	void Prepare()
	{
		if(!m_Browser.m_pSortBackup)
			m_Browser.m_pSortBackup = std::make_unique<CSortBackup>();
		auto &Backup = *m_Browser.m_pSortBackup;
		Backup.Clear();
		Backup.m_vEntries.reserve(m_Browser.m_vpServerlist.size());
		Backup.m_vSorted = m_Browser.m_vSortedServerlist;
		Backup.m_vCommunityPlayers.reserve(m_Browser.m_vCommunities.size());
		const uint64_t FriendsRevision = m_Browser.m_pFriends->Revision();
		const bool IgnoreClan = g_Config.m_ClFriendsIgnoreClan != 0;
		for(CServerEntry *pEntry : m_Browser.m_vpServerlist)
		{
			const CServerInfo &Info = pEntry->m_Info;
			const bool UpdateFriends = !pEntry->m_FriendStateValid || pEntry->m_FriendStateRevision != FriendsRevision || pEntry->m_FriendStateIgnoreClan != IgnoreClan;
			Backup.m_vEntries.push_back({pEntry, Info.m_Favorite, Info.m_FavoriteAllowPing,
				Info.m_NumFilteredPlayers, Info.m_QuickSearchHit, Info.m_FriendState, Info.m_FriendNum,
				pEntry->m_FriendStateRevision, pEntry->m_FriendStateValid, pEntry->m_FriendStateIgnoreClan, UpdateFriends});
			// 好友缓存有效的服务器不会改写逐玩家状态，无需额外扫描或备份这些玩家。
			if(UpdateFriends)
			{
				for(const auto &Client : Info.m_vClients)
					Backup.m_vClientFriends.push_back(Client.m_FriendState);
			}
		}
		for(const auto &Community : m_Browser.m_vCommunities)
			Backup.m_vCommunityPlayers.push_back(Community.NumPlayers());
		// 此前只准备备份；任一分配失败都未改动可见状态。
		m_Prepared = true;
	}
	~CSortUpdate() noexcept
	{
		if(!m_Browser.m_pSortBackup)
			return;
		auto &Backup = *m_Browser.m_pSortBackup;
		if(m_Prepared && !m_Committed)
		{
			size_t ClientIndex = 0;
			for(const auto &Entry : Backup.m_vEntries)
			{
				auto &Info = Entry.m_pEntry->m_Info;
				Info.m_Favorite = Entry.m_Favorite;
				Info.m_FavoriteAllowPing = Entry.m_FavoriteAllowPing;
				Info.m_NumFilteredPlayers = Entry.m_NumFilteredPlayers;
				Info.m_QuickSearchHit = Entry.m_QuickSearchHit;
				Info.m_FriendState = Entry.m_FriendState;
				Info.m_FriendNum = Entry.m_FriendNum;
				Entry.m_pEntry->m_FriendStateRevision = Entry.m_FriendRevision;
				Entry.m_pEntry->m_FriendStateValid = Entry.m_FriendStateValid;
				Entry.m_pEntry->m_FriendStateIgnoreClan = Entry.m_FriendStateIgnoreClan;
				// Sort 不修改客户端数组的数量和顺序，备份可按原顺序无分配写回。
				if(Entry.m_RestoreClientFriends)
				{
					for(auto &Client : Info.m_vClients)
						Client.m_FriendState = Backup.m_vClientFriends[ClientIndex++];
				}
			}
			Backup.m_vSorted.swap(m_Browser.m_vSortedServerlist);
			for(size_t Index = 0; Index < Backup.m_vCommunityPlayers.size(); ++Index)
				m_Browser.m_vCommunities[Index].m_NumPlayers = Backup.m_vCommunityPlayers[Index];
			m_Browser.m_FriendListRevision = m_FriendRevision;
			m_Browser.m_NumSortedPlayers = m_NumSortedPlayers;
			m_Browser.m_Sorthash = m_Sorthash;
			m_Browser.m_NeedResort = m_NeedResort;
		}
		Backup.Clear();
	}
	void Commit() noexcept { m_Committed = true; }
};

bool CServerBrowser::TrySort()
{
	try
	{
		CSortUpdate Update(*this);
		Update.Prepare();
		for(CServerEntry *pEntry : m_vpServerlist)
		{
			CServerInfo *pInfo = &pEntry->m_Info;
			pInfo->m_Favorite = m_pFavorites->IsFavorite(pInfo->m_aAddresses, pInfo->m_NumAddresses);
			pInfo->m_FavoriteAllowPing = m_pFavorites->IsPingAllowed(pInfo->m_aAddresses, pInfo->m_NumAddresses);
		}
		Sort();
		Update.Commit();
		m_SortUpdateFailed = false;
		m_SortUpdateRetryAt = 0;
		return true;
	}
	catch(...)
	{
		if(!m_SortUpdateFailed)
			log_error("serverbrowser", "failed to sort visible server list; retaining previous state and retrying");
		m_SortUpdateFailed = true;
		const int64_t Now = UpdateNow();
		m_SortUpdateRetryAt = Now + std::min<int64_t>(time_freq(), INT64_MAX - Now);
		return false;
	}
}

void CServerBrowser::Sort()
{
	++m_FriendListRevision;
	m_NeedResort = false;
	// update number of filtered players
	for(CServerEntry *pEntry : m_vpServerlist)
	{
		UpdateServerFilteredPlayers(&pEntry->m_Info);
	}

	// create filtered list
	Filter();

	// sort
	if(g_Config.m_BrSortOrder == 2 && (g_Config.m_BrSort == IServerBrowser::SORT_NUMPLAYERS || g_Config.m_BrSort == IServerBrowser::SORT_PING))
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareNumPlayersAndPing));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_NAME)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareName));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_PING)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortComparePing));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_MAP)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareMap));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_NUMFRIENDS)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareNumFriends));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_NUMPLAYERS)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareNumPlayers));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_GAMETYPE)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareGametype));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_FAVORITES)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareFavoritesNumPlayersAndPing));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_QM_CLIENTS)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareQmClients));
	else if(g_Config.m_BrSort == IServerBrowser::SORT_DIFFICULTY)
		std::stable_sort(m_vSortedServerlist.begin(), m_vSortedServerlist.end(), CSortWrap(this, &CServerBrowser::SortCompareDifficulty));

	m_Sorthash = SortHash();
	// 服务器筛选和排序完成后再整理社区，最后的资源交换不分配内存。
	SortCommunities();
}

void CServerBrowser::SortCommunities()
{
	if(m_vCommunities.size() < 2)
		return;
	std::vector<size_t> vOrder(m_vCommunities.size());
	std::vector<size_t> vDestination(m_vCommunities.size());
	for(size_t Index = 0; Index < vOrder.size(); ++Index)
		vOrder[Index] = Index;
	std::stable_sort(vOrder.begin(), vOrder.end(), [this](size_t Left, size_t Right) {
		return m_vCommunities[Left].NumPlayers() > m_vCommunities[Right].NumPlayers();
	});
	for(size_t Index = 0; Index < vOrder.size(); ++Index)
		vDestination[vOrder[Index]] = Index;
	// 这里只交换已有资源；分配失败不会留下移动了一半的社区和悬空的国家/类型指针。
	bool Reordered = false;
	for(size_t Index = 0; Index < vDestination.size(); ++Index)
	{
		while(vDestination[Index] != Index)
		{
			const size_t Other = vDestination[Index];
			Reordered = true;
			m_vCommunities[Index].Swap(m_vCommunities[Other]);
			std::swap(vDestination[Index], vDestination[Other]);
		}
	}
	if(Reordered)
		m_CommunityCache.Invalidate();
}

void CServerBrowser::RemoveRequest(CServerEntry *pEntry)
{
	if(pEntry->m_pPrevReq || pEntry->m_pNextReq || m_pFirstReqServer == pEntry)
	{
		if(pEntry->m_pPrevReq)
			pEntry->m_pPrevReq->m_pNextReq = pEntry->m_pNextReq;
		else
			m_pFirstReqServer = pEntry->m_pNextReq;

		if(pEntry->m_pNextReq)
			pEntry->m_pNextReq->m_pPrevReq = pEntry->m_pPrevReq;
		else
			m_pLastReqServer = pEntry->m_pPrevReq;

		pEntry->m_pPrevReq = nullptr;
		pEntry->m_pNextReq = nullptr;
		m_NumRequests--;
	}
}

CServerBrowser::CServerEntry *CServerBrowser::Find(const NETADDR &Addr)
{
	auto Entry = m_ByAddr.find(Addr);
	if(Entry == m_ByAddr.end())
	{
		return nullptr;
	}
	return m_vpServerlist[Entry->second];
}

void CServerBrowser::QueueRequest(CServerEntry *pEntry)
{
	// add it to the list of servers that we should request info from
	pEntry->m_pPrevReq = m_pLastReqServer;
	if(m_pLastReqServer)
		m_pLastReqServer->m_pNextReq = pEntry;
	else
		m_pFirstReqServer = pEntry;
	m_pLastReqServer = pEntry;
	pEntry->m_pNextReq = nullptr;
	m_NumRequests++;
}

void CServerBrowser::SetInfo(CServerEntry *pEntry, const CServerInfo &Info)
{
	server_info::Update(pEntry->m_Info, Info, [this, pEntry](CServerInfo &Candidate) {
		const CServerInfo &Previous = pEntry->m_Info;
		Candidate.m_Favorite = Previous.m_Favorite;
		Candidate.m_FavoriteAllowPing = Previous.m_FavoriteAllowPing;
		Candidate.m_ServerIndex = Previous.m_ServerIndex;
		Candidate.m_QmClientCount = Previous.m_QmClientCount;
		mem_copy(Candidate.m_aAddresses, Previous.m_aAddresses, sizeof(Candidate.m_aAddresses));
		Candidate.m_NumAddresses = Previous.m_NumAddresses;
		ServerBrowserFormatAddresses(Candidate.m_aAddress, sizeof(Candidate.m_aAddress), Candidate.m_aAddresses, Candidate.m_NumAddresses);
		str_copy(Candidate.m_aCommunityId, Previous.m_aCommunityId);
		str_copy(Candidate.m_aCommunityCountry, Previous.m_aCommunityCountry);
		str_copy(Candidate.m_aCommunityType, Previous.m_aCommunityType);
		UpdateServerRank(&Candidate);
		Candidate.m_GametypeColor = CServerInfo::GametypeColor(Candidate.m_aGameType);

		if(Candidate.m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_UNSPECIFIED)
		{
			if(str_find_nocase(Candidate.m_aGameType, "race") || str_find_nocase(Candidate.m_aGameType, "fastcap"))
			{
				Candidate.m_ClientScoreKind = CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT;
			}
			else
			{
				Candidate.m_ClientScoreKind = CServerInfo::CLIENT_SCORE_KIND_POINTS;
			}
		}

		class CPlayerScoreNameLess
		{
			const int m_ScoreKind;

		public:
			CPlayerScoreNameLess(int ClientScoreKind) :
				m_ScoreKind(ClientScoreKind)
			{
			}

			bool operator()(const CServerInfo::CClient &Client0, const CServerInfo::CClient &Client1) const
			{
				// Sort players before non players
				if(Client0.m_Player && !Client1.m_Player)
					return true;
				if(!Client0.m_Player && Client1.m_Player)
					return false;

				int Score0 = Client0.m_Score;
				int Score1 = Client1.m_Score;

				if(m_ScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME || m_ScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT)
				{
					// Sort unfinished (-9999) and still connecting players (-1) after others
					if(Score0 < 0 && Score1 >= 0)
						return false;
					if(Score0 >= 0 && Score1 < 0)
						return true;
				}

				if(Score0 != Score1)
				{
					// Handle the sign change introduced with CLIENT_SCORE_KIND_TIME
					if(m_ScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME)
						return Score0 < Score1;
					else
						return Score0 > Score1;
				}

				return str_comp_nocase(Client0.m_aName, Client1.m_aName) < 0;
			}
		};

		std::sort(Candidate.m_vClients.begin(), Candidate.m_vClients.end(), CPlayerScoreNameLess(Candidate.m_ClientScoreKind));
	});
	pEntry->m_FriendStateValid = false;
	pEntry->m_GotInfo = 1;
	// 保留成功更新原有的两次修订增量，失败准备不触碰真实条目和缓存标志。
	m_FriendListRevision += 2;
}

// 可用延迟：未测量（-1）或触到 1 秒测量上限的值都不能当作实测延迟使用。
static bool IsUsableLatency(int Latency)
{
	return Latency >= 0 && Latency < CServerInfo::LATENCY_UNKNOWN;
}

void CServerBrowser::SetLatency(NETADDR Addr, int Latency)
{
	if(!IsUsableLatency(Latency))
	{
		// 触到上限等于超时：不要把它写进缓存并广播给同一 IP 的其他服务器。
		return;
	}
	m_pPingCache->CachePing(Addr, Latency);

	Addr.port = 0;
	for(CServerEntry *pEntry : m_vpServerlist)
	{
		if(!pEntry->m_GotInfo)
		{
			continue;
		}
		bool Found = false;
		for(int i = 0; i < pEntry->m_Info.m_NumAddresses; i++)
		{
			NETADDR Other = pEntry->m_Info.m_aAddresses[i];
			Other.port = 0;
			if(Addr == Other)
			{
				Found = true;
				break;
			}
		}
		if(!Found)
		{
			continue;
		}
		int Ping = m_pPingCache->GetPing(pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);
		if(Ping == -1)
		{
			continue;
		}
		if(pEntry->m_Info.m_Latency != Ping || pEntry->m_Info.m_LatencyIsEstimated)
			++m_FriendListRevision;
		pEntry->m_Info.m_Latency = Ping;
		pEntry->m_Info.m_LatencyIsEstimated = false;
	}
}

CServerBrowser::CServerEntry *CServerBrowser::Add(const NETADDR *pAddrs, int NumAddrs)
{
	++m_FriendListRevision;
	// create new pEntry
	CServerEntry *pEntry = &m_ServerlistStorage.emplace_back();

	// set the info
	mem_copy(pEntry->m_Info.m_aAddresses, pAddrs, NumAddrs * sizeof(pAddrs[0]));
	pEntry->m_Info.m_NumAddresses = NumAddrs;
	pEntry->m_Info.m_QmClientCount = QmClientCountForServer(pEntry->m_Info);

	pEntry->m_Info.m_Latency = 999;
	pEntry->m_Info.m_HasRank = CServerInfo::RANK_UNAVAILABLE;
	ServerBrowserFormatAddresses(pEntry->m_Info.m_aAddress, sizeof(pEntry->m_Info.m_aAddress), pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);
	UpdateServerCommunity(&pEntry->m_Info);
	str_copy(pEntry->m_Info.m_aName, pEntry->m_Info.m_aAddress, sizeof(pEntry->m_Info.m_aName));

	// check if it's a favorite
	pEntry->m_Info.m_Favorite = m_pFavorites->IsFavorite(pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);
	pEntry->m_Info.m_FavoriteAllowPing = m_pFavorites->IsPingAllowed(pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);

	int ServerIndex = m_vpServerlist.size();
	for(int i = 0; i < NumAddrs; i++)
	{
		m_ByAddr[pAddrs[i]] = ServerIndex;
	}

	// add to list
	pEntry->m_Info.m_ServerIndex = ServerIndex;
	if(m_vpServerlist.capacity() == 0)
	{
		m_vpServerlist.reserve(128);
	}
	m_vpServerlist.push_back(pEntry);

	return pEntry;
}

CServerBrowser::CServerEntry *CServerBrowser::ReplaceEntry(CServerEntry *pEntry, const NETADDR *pAddrs, int NumAddrs)
{
	++m_FriendListRevision;
	pEntry->m_FriendStateValid = false;
	for(int i = 0; i < pEntry->m_Info.m_NumAddresses; i++)
	{
		m_ByAddr.erase(pEntry->m_Info.m_aAddresses[i]);
	}

	// set the info
	mem_copy(pEntry->m_Info.m_aAddresses, pAddrs, NumAddrs * sizeof(pAddrs[0]));
	pEntry->m_Info.m_NumAddresses = NumAddrs;
	pEntry->m_Info.m_QmClientCount = QmClientCountForServer(pEntry->m_Info);

	pEntry->m_Info.m_Latency = 999;
	pEntry->m_Info.m_HasRank = CServerInfo::RANK_UNAVAILABLE;
	ServerBrowserFormatAddresses(pEntry->m_Info.m_aAddress, sizeof(pEntry->m_Info.m_aAddress), pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);
	UpdateServerCommunity(&pEntry->m_Info);
	str_copy(pEntry->m_Info.m_aName, pEntry->m_Info.m_aAddress, sizeof(pEntry->m_Info.m_aName));

	pEntry->m_Info.m_Favorite = m_pFavorites->IsFavorite(pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);
	pEntry->m_Info.m_FavoriteAllowPing = m_pFavorites->IsPingAllowed(pEntry->m_Info.m_aAddresses, pEntry->m_Info.m_NumAddresses);

	for(int i = 0; i < NumAddrs; i++)
	{
		m_ByAddr[pAddrs[i]] = pEntry->m_Info.m_ServerIndex;
	}

	return pEntry;
}

void CServerBrowser::OnServerInfoUpdate(const NETADDR &Addr, int Token, const CServerInfo *pInfo)
{
	int BasicToken = Token;
	int ExtraToken = 0;
	if(pInfo->m_Type == SERVERINFO_EXTENDED)
	{
		BasicToken = Token & 0xff;
		ExtraToken = Token >> 8;
	}

	CServerEntry *pEntry = Find(Addr);

	if(m_ServerlistType == IServerBrowser::TYPE_LAN)
	{
		// 先校验广播令牌，再允许替换已有的 0.7 地址或新增条目。
		NETADDR Broadcast = NETADDR_ZEROED;
		Broadcast.type = (m_pNetClient->NetType() & ~(NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6)) | NETTYPE_LINK_BROADCAST;
		int TokenBC = GenerateToken(Broadcast);
		bool Drop = false;
		Drop = Drop || BasicToken != GetBasicToken(TokenBC);
		Drop = Drop || (pInfo->m_Type == SERVERINFO_EXTENDED && ExtraToken != GetExtraToken(TokenBC));
		if(Drop)
		{
			return;
		}

		if(!pEntry)
		{
			NETADDR LookupAddr = Addr;
			if(Addr.type & NETTYPE_TW7)
			{
				// don't add 0.7 server if 0.6 server with the same IP and port already exists
				LookupAddr.type &= ~NETTYPE_TW7;
				pEntry = Find(LookupAddr);
				if(pEntry)
					return;
			}
			else
			{
				// replace 0.7 bridge server with 0.6
				LookupAddr.type |= NETTYPE_TW7;
				pEntry = Find(LookupAddr);
				if(pEntry)
					pEntry = ReplaceEntry(pEntry, &Addr, 1);
			}
		}

		if(!pEntry)
			pEntry = Add(&Addr, 1);
	}
	else
	{
		if(!pEntry)
		{
			return;
		}
		int TokenAddr = GenerateToken(Addr);
		bool Drop = false;
		Drop = Drop || BasicToken != GetBasicToken(TokenAddr);
		Drop = Drop || (pInfo->m_Type == SERVERINFO_EXTENDED && ExtraToken != GetExtraToken(TokenAddr));
		if(Drop)
		{
			return;
		}
	}

	if(m_ServerlistType == IServerBrowser::TYPE_LAN)
	{
		SetInfo(pEntry, *pInfo);
		pEntry->m_Info.m_Latency = minimum(static_cast<int>((time_get() - m_BroadcastTime) * 1000 / time_freq()), CServerInfo::LATENCY_UNKNOWN);
	}
	else if(pEntry->m_RequestTime > 0)
	{
		if(!pEntry->m_RequestIgnoreInfo)
		{
			SetInfo(pEntry, *pInfo);
		}

		const int Latency = minimum(static_cast<int>((time_get() - pEntry->m_RequestTime) * 1000 / time_freq()), CServerInfo::LATENCY_UNKNOWN);
		if(!pEntry->m_RequestIgnoreInfo)
		{
			if(IsUsableLatency(Latency))
			{
				pEntry->m_Info.m_Latency = Latency;
			}
			else
			{
				// 这次 GETINFO 往返触到测量上限：回落到缓存/地区估算，避免把上限值当成实测延迟。
				UpdateServerLatency(&pEntry->m_Info, DetermineOwnLocation());
			}
		}
		else
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
			dbg_msg("serverbrowser", "received ping response from %s", aAddr);
			SetLatency(Addr, Latency);
		}
		pEntry->m_RequestTime = -1; // Request has been answered
	}
	RemoveRequest(pEntry);
	RequestResort();
}

void CServerBrowser::Refresh(int Type, bool Force)
{
	bool ServerListTypeChanged = Force || m_ServerlistType != Type;
	int OldServerListType = m_ServerlistType;
	m_ServerlistType = Type;
	secure_random_fill(m_aTokenSeed, sizeof(m_aTokenSeed));

	if(Type == IServerBrowser::TYPE_LAN || (ServerListTypeChanged && OldServerListType == IServerBrowser::TYPE_LAN))
		CleanUp();

	if(Type == IServerBrowser::TYPE_LAN)
	{
		m_HttpUpdateFailed = false;
		m_HttpUpdateRetryAt = 0;
		m_SortUpdateFailed = false;
		m_SortUpdateRetryAt = 0;
		unsigned char aBuffer[sizeof(SERVERBROWSE_GETINFO) + 1];
		CNetChunk Packet;

		/* do the broadcast version */
		mem_zero(&Packet, sizeof(Packet));
		Packet.m_Address.type = (m_pNetClient->NetType() & ~(NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6)) | NETTYPE_LINK_BROADCAST;
		Packet.m_Flags = NETSENDFLAG_CONNLESS | NETSENDFLAG_EXTENDED;
		Packet.m_DataSize = sizeof(aBuffer);
		Packet.m_pData = aBuffer;

		int Token = GenerateToken(Packet.m_Address);
		mem_copy(aBuffer, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO));
		aBuffer[sizeof(SERVERBROWSE_GETINFO)] = GetBasicToken(Token);

		Packet.m_aExtraData[0] = GetExtraToken(Token) >> 8;
		Packet.m_aExtraData[1] = GetExtraToken(Token) & 0xff;

		m_BroadcastTime = time_get();

		CPacker Packer;
		Packer.Reset();
		Packer.AddRaw(SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO));
		Packer.AddInt(Token);

		CNetChunk Packet7;
		mem_zero(&Packet7, sizeof(Packet7));
		Packet7.m_Address.type = (m_pNetClient->NetType() & ~(NETTYPE_WEBSOCKET_IPV4 | NETTYPE_WEBSOCKET_IPV6)) | NETTYPE_TW7 | NETTYPE_LINK_BROADCAST;
		Packet7.m_Flags = NETSENDFLAG_CONNLESS;
		Packet7.m_DataSize = Packer.Size();
		Packet7.m_pData = Packer.Data();

		for(int Port = LAN_PORT_BEGIN; Port <= LAN_PORT_END; Port++)
		{
			Packet.m_Address.port = Port;
			m_pNetClient->Send(&Packet);

			Packet7.m_Address.port = Port;
			m_pNetClient->Send(&Packet7);
		}

		if(g_Config.m_Debug)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "serverbrowser", "broadcasting for servers");
	}
	else
	{
		m_pHttp->Refresh();
		m_pPingCache->Load();
		m_RefreshingHttp = true;

		if(m_HttpUpdateFailed || m_SortUpdateFailed || (ServerListTypeChanged && m_pHttp->NumServers() > 0))
		{
			TryUpdateFromHttp();
		}
	}
}

void CServerBrowser::RefreshHttpServerList()
{
	m_pHttp->Refresh();
	m_pPingCache->Load();
}

void CServerBrowser::RequestImpl(const NETADDR &Addr, CServerEntry *pEntry, int *pBasicToken, int *pToken, bool RandomToken) const
{
	if(g_Config.m_Debug)
	{
		char aAddrStr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), true);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "requesting server info from %s", aAddrStr);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "serverbrowser", aBuf);
	}

	int Token = GenerateToken(Addr);
	if(RandomToken)
	{
		int AvoidBasicToken = GetBasicToken(Token);
		do
		{
			secure_random_fill(&Token, sizeof(Token));
			Token &= 0xffffff;
		} while(GetBasicToken(Token) == AvoidBasicToken);
	}
	if(pToken)
	{
		*pToken = Token;
	}
	if(pBasicToken)
	{
		*pBasicToken = GetBasicToken(Token);
	}

	if(Addr.type & NETTYPE_TW7)
	{
		CPacker Packer;
		Packer.Reset();
		Packer.AddRaw(SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO));
		Packer.AddInt(Token);

		CNetChunk Packet;
		Packet.m_ClientId = -1;
		Packet.m_Address = Addr;
		Packet.m_Flags = NETSENDFLAG_CONNLESS;
		Packet.m_DataSize = Packer.Size();
		Packet.m_pData = Packer.Data();
		mem_zero(&Packet.m_aExtraData, sizeof(Packet.m_aExtraData));

		m_pNetClient->Send(&Packet);
	}
	else
	{
		unsigned char aBuffer[sizeof(SERVERBROWSE_GETINFO) + 1];
		mem_copy(aBuffer, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO));
		aBuffer[sizeof(SERVERBROWSE_GETINFO)] = GetBasicToken(Token);

		CNetChunk Packet;
		Packet.m_ClientId = -1;
		Packet.m_Address = Addr;
		Packet.m_Flags = NETSENDFLAG_CONNLESS | NETSENDFLAG_EXTENDED;
		Packet.m_DataSize = sizeof(aBuffer);
		Packet.m_pData = aBuffer;
		mem_zero(&Packet.m_aExtraData, sizeof(Packet.m_aExtraData));
		Packet.m_aExtraData[0] = GetExtraToken(Token) >> 8;
		Packet.m_aExtraData[1] = GetExtraToken(Token) & 0xff;

		m_pNetClient->Send(&Packet);
	}

	if(pEntry)
		pEntry->m_RequestTime = time_get();
}

void CServerBrowser::RequestCurrentServer(const NETADDR &Addr) const
{
	RequestImpl(Addr, nullptr, nullptr, nullptr, false);
}

void CServerBrowser::RequestCurrentServerWithRandomToken(const NETADDR &Addr, int *pBasicToken, int *pToken) const
{
	RequestImpl(Addr, nullptr, pBasicToken, pToken, true);
}

void CServerBrowser::SetCurrentServerPing(const NETADDR &Addr, int Ping)
{
	SetLatency(Addr, minimum(Ping, CServerInfo::LATENCY_UNKNOWN));
}

class CServerBrowser::CServerListUpdate
{
	CServerBrowser &m_Browser;
	std::deque<CServerEntry> m_Storage;
	std::vector<CServerEntry *> m_vpServers;
	std::vector<int> m_vSorted;
	std::unordered_map<NETADDR, int> m_ByAddr;
	std::vector<int> m_vCommunityPlayers;
	CServerEntry *m_pFirstRequest;
	CServerEntry *m_pLastRequest;
	int m_NumRequests;
	int m_MaxRequests;
	int m_NumSortedPlayers;
	bool m_NeedResort;
	int m_Sorthash;
	uint64_t m_FriendRevision;
	bool m_Committed = false;

	void SwapLists() noexcept
	{
		m_Storage.swap(m_Browser.m_ServerlistStorage);
		m_vpServers.swap(m_Browser.m_vpServerlist);
		m_vSorted.swap(m_Browser.m_vSortedServerlist);
		m_ByAddr.swap(m_Browser.m_ByAddr);
	}

public:
	explicit CServerListUpdate(CServerBrowser &Browser) :
		m_Browser(Browser),
		m_vCommunityPlayers(Browser.m_vCommunities.size()),
		m_pFirstRequest(Browser.m_pFirstReqServer),
		m_pLastRequest(Browser.m_pLastReqServer),
		m_NumRequests(Browser.m_NumRequests),
		m_MaxRequests(Browser.m_CurrentMaxRequests),
		m_NumSortedPlayers(Browser.m_NumSortedPlayers),
		m_NeedResort(Browser.m_NeedResort),
		m_Sorthash(Browser.m_Sorthash),
		m_FriendRevision(Browser.m_FriendListRevision)
	{
		// 备份所需分配先完成；交换保留旧条目地址、索引和请求链的对应关系。
		for(size_t Index = 0; Index < m_vCommunityPlayers.size(); ++Index)
			m_vCommunityPlayers[Index] = Browser.m_vCommunities[Index].NumPlayers();
		SwapLists();
		Browser.CleanUp();
	}
	CServerListUpdate(const CServerListUpdate &) = delete;
	CServerListUpdate &operator=(const CServerListUpdate &) = delete;
	~CServerListUpdate()
	{
		if(m_Committed)
			return;
		SwapLists();
		m_Browser.m_pFirstReqServer = m_pFirstRequest;
		m_Browser.m_pLastReqServer = m_pLastRequest;
		m_Browser.m_NumRequests = m_NumRequests;
		m_Browser.m_CurrentMaxRequests = m_MaxRequests;
		m_Browser.m_NumSortedPlayers = m_NumSortedPlayers;
		m_Browser.m_NeedResort = m_NeedResort;
		m_Browser.m_Sorthash = m_Sorthash;
		m_Browser.m_FriendListRevision = m_FriendRevision;
		// Sort 的最后一步才无异常地交换社区；此前失败时顺序仍与备份一致。
		for(size_t Index = 0; Index < m_vCommunityPlayers.size(); ++Index)
			m_Browser.m_vCommunities[Index].m_NumPlayers = m_vCommunityPlayers[Index];
	}
	void Commit() noexcept { m_Committed = true; }
};

int64_t CServerBrowser::UpdateNow() const
{
	return m_UpdateClock ? m_UpdateClock() : time_get();
}

bool CServerBrowser::TryUpdateFromHttp()
{
	try
	{
		CServerListUpdate Update(*this);
		UpdateFromHttp();
		Sort();
		Update.Commit();
		m_HttpUpdateFailed = false;
		m_HttpUpdateRetryAt = 0;
		m_SortUpdateFailed = false;
		m_SortUpdateRetryAt = 0;
		return true;
	}
	catch(...)
	{
		// 保留旧列表和未消费版本；持续低内存时不逐帧重建，也不重复记录日志。
		if(!m_HttpUpdateFailed)
			log_error("serverbrowser", "failed to rebuild visible server list; retaining previous list and retrying");
		m_HttpUpdateFailed = true;
		const int64_t Now = UpdateNow();
		m_HttpUpdateRetryAt = Now + std::min<int64_t>(time_freq(), INT64_MAX - Now);
		return false;
	}
}

void CServerBrowser::UpdateFromHttp()
{
	const int OwnLocation = DetermineOwnLocation();

	int NumServers = m_pHttp->NumServers();
	m_vpServerlist.reserve(NumServers);
	std::function<bool(const NETADDR *, int)> Want = [](const NETADDR *pAddrs, int NumAddrs) { return true; };
	if(m_ServerlistType == IServerBrowser::TYPE_FAVORITES)
	{
		Want = [this](const NETADDR *pAddrs, int NumAddrs) -> bool {
			return m_pFavorites->IsFavorite(pAddrs, NumAddrs) != TRISTATE::NONE;
		};
	}
	else if(m_ServerlistType >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && m_ServerlistType <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5)
	{
		const size_t CommunityIndex = m_ServerlistType - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
		std::vector<const CCommunity *> vpFavoriteCommunities = FavoriteCommunities();
		dbg_assert(CommunityIndex < vpFavoriteCommunities.size(), "Invalid community index");
		const CCommunity *pWantedCommunity = vpFavoriteCommunities[CommunityIndex];
		const bool IsNoneCommunity = str_comp(pWantedCommunity->Id(), COMMUNITY_NONE) == 0;
		Want = [this, pWantedCommunity, IsNoneCommunity](const NETADDR *pAddrs, int NumAddrs) -> bool {
			for(int AddressIndex = 0; AddressIndex < NumAddrs; AddressIndex++)
			{
				const auto CommunityServer = m_CommunityServersByAddr.find(CommunityAddressKey(pAddrs[AddressIndex]));
				if(CommunityServer != m_CommunityServersByAddr.end())
				{
					if(IsNoneCommunity)
					{
						// Servers with community "none" are not present in m_CommunityServersByAddr, so we ignore
						// any server that is found in this map to determine only the servers without community.
						return false;
					}
					else if(str_comp(CommunityServer->second.CommunityId(), pWantedCommunity->Id()) == 0)
					{
						return true;
					}
				}
			}
			return IsNoneCommunity;
		};
	}

	for(int i = 0; i < NumServers; i++)
	{
		const CServerInfo &Info = m_pHttp->Server(i);
		if(!Want(Info.m_aAddresses, Info.m_NumAddresses))
		{
			continue;
		}
		CServerEntry *pEntry = Add(Info.m_aAddresses, Info.m_NumAddresses);
		SetInfo(pEntry, Info);
		UpdateServerLatency(&pEntry->m_Info, OwnLocation);
		pEntry->m_RequestIgnoreInfo = true;
	}

	if(m_ServerlistType == IServerBrowser::TYPE_FAVORITES)
	{
		const IFavorites::CEntry *pFavorites;
		int NumFavorites;
		m_pFavorites->AllEntries(&pFavorites, &NumFavorites);
		for(int i = 0; i < NumFavorites; i++)
		{
			bool Found = false;
			for(int j = 0; j < pFavorites[i].m_NumAddrs; j++)
			{
				if(Find(pFavorites[i].m_aAddrs[j]))
				{
					Found = true;
					break;
				}
			}
			if(Found)
			{
				continue;
			}
			// (Also add favorites we're not allowed to ping.)
			CServerEntry *pEntry = Add(pFavorites[i].m_aAddrs, pFavorites[i].m_NumAddrs);
			if(pFavorites[i].m_AllowPing)
			{
				QueueRequest(pEntry);
			}
		}
	}

	RequestResort();
}

void CServerBrowser::CleanUp()
{
	// clear out everything
	m_vSortedServerlist.clear();
	m_vpServerlist.clear();
	m_ServerlistStorage.clear();
	m_NumSortedPlayers = 0;
	m_ByAddr.clear();
	m_pFirstReqServer = nullptr;
	m_pLastReqServer = nullptr;
	m_NumRequests = 0;
	m_CurrentMaxRequests = g_Config.m_BrMaxRequests;
	++m_FriendListRevision;
}

void CServerBrowser::Update()
{
	if(m_CommunityUpdateFailed && UpdateNow() >= m_CommunityUpdateRetryAt)
		LoadDDNetServers();
	int64_t Timeout = time_freq();
	int64_t Now = time_get();

	const char *pHttpBestUrl;
	if(!m_pHttp->GetBestUrl(&pHttpBestUrl) && str_comp(pHttpBestUrl, g_Config.m_BrCachedBestServerinfoUrl) != 0)
	{
		str_copy(g_Config.m_BrCachedBestServerinfoUrl, pHttpBestUrl);
	}

	m_pHttp->Update();

	const uint64_t HttpRevision = m_pHttp->Revision();
	if(m_ServerlistType != TYPE_LAN && (m_HttpUpdateFailed || (m_RefreshingHttp && !m_pHttp->IsRefreshing()) || m_HttpRevision != HttpRevision))
	{
		if(m_HttpUpdateFailed && UpdateNow() < m_HttpUpdateRetryAt)
			return;
		if(TryUpdateFromHttp())
		{
			m_RefreshingHttp = false;
			m_HttpRevision = HttpRevision;
		}
		return;
	}

	{
		CServerEntry *pEntry = m_pFirstReqServer;
		int Count = 0;
		while(true)
		{
			if(!pEntry) // no more entries
				break;
			if(pEntry->m_RequestTime && pEntry->m_RequestTime + Timeout < Now)
			{
				pEntry = pEntry->m_pNextReq;
				continue;
			}
			// no more than 10 concurrent requests
			if(Count == m_CurrentMaxRequests)
				break;

			if(pEntry->m_RequestTime == 0)
			{
				RequestImpl(pEntry->m_Info.m_aAddresses[0], pEntry, nullptr, nullptr, false);
			}

			Count++;
			pEntry = pEntry->m_pNextReq;
		}

		if(m_pFirstReqServer && Count == 0 && m_CurrentMaxRequests > 1) //NO More current Server Requests
		{
			//reset old ones
			pEntry = m_pFirstReqServer;
			while(true)
			{
				if(!pEntry) // no more entries
					break;
				pEntry->m_RequestTime = 0;
				pEntry = pEntry->m_pNextReq;
			}

			//update max-requests
			m_CurrentMaxRequests = m_CurrentMaxRequests / 2;
			if(m_CurrentMaxRequests < 1)
				m_CurrentMaxRequests = 1;
		}
		else if(Count == 0 && m_CurrentMaxRequests == 1) //we reached the limit, just release all left requests. IF a server sends us a packet, a new request will be added automatically, so we can delete all
		{
			pEntry = m_pFirstReqServer;
			while(true)
			{
				if(!pEntry) // no more entries
					break;
				CServerEntry *pNext = pEntry->m_pNextReq;
				RemoveRequest(pEntry); //release request
				pEntry = pNext;
			}
		}
	}

	// check if we need to resort
	if(m_Sorthash != SortHash() || m_NeedResort || m_SortUpdateFailed)
	{
		if(m_SortUpdateFailed && UpdateNow() < m_SortUpdateRetryAt)
			return;
		TrySort();
	}
}

// 新 JSON、社区、筛选与缓存均先在本对象中准备；发布阶段不调用外部接口，也不再分配。
class CServerBrowser::CCommunityUpdate
{
	struct CEntry
	{
		CServerEntry *m_pEntry;
		char m_aCommunityId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
		char m_aCountry[CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH];
		char m_aType[CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];
		CServerInfo::ERankState m_Rank;
		int m_Latency;
		bool m_Estimated;
		CEntry(CServerEntry *pEntry, const CServerInfo &Info) :
			m_pEntry(pEntry), m_Rank(Info.m_HasRank), m_Latency(Info.m_Latency), m_Estimated(Info.m_LatencyIsEstimated)
		{
			str_copy(m_aCommunityId, Info.m_aCommunityId);
			str_copy(m_aCountry, Info.m_aCommunityCountry);
			str_copy(m_aType, Info.m_aCommunityType);
		}
		void Publish() const noexcept
		{
			auto &Info = m_pEntry->m_Info;
			str_copy(Info.m_aCommunityId, m_aCommunityId);
			str_copy(Info.m_aCommunityCountry, m_aCountry);
			str_copy(Info.m_aCommunityType, m_aType);
			Info.m_HasRank = m_Rank;
			Info.m_Latency = m_Latency;
			Info.m_LatencyIsEstimated = m_Estimated;
		}
	};
	CServerBrowser &m_Browser;
	const json_value *m_pInfo;
	std::optional<SHA256_DIGEST> m_InfoSha256;
	bool m_ReloadLocation;
	int m_OwnLocation;
	int m_ServerlistType;
	std::vector<CCommunity> m_vCommunities;
	std::unordered_map<NETADDR, CCommunityServer> m_CommunityServersByAddr;
	CFavoriteCommunityFilterList m_Favorites;
	CExcludedCommunityFilterList m_Excluded;
	CExcludedCommunityCountryFilterList m_Countries;
	CExcludedCommunityTypeFilterList m_Types;
	CCommunityCache m_Cache;
	std::vector<CEntry> m_vEntries;

public:
	CCommunityUpdate(CServerBrowser &Browser, const json_value *pInfo, std::optional<SHA256_DIGEST> InfoSha256, bool ReloadLocation) :
		m_Browser(Browser), m_pInfo(pInfo), m_InfoSha256(InfoSha256), m_ReloadLocation(ReloadLocation), m_OwnLocation(Browser.m_OwnLocation), m_ServerlistType(Browser.m_ServerlistType), m_Favorites(Browser.m_FavoriteCommunitiesFilter), m_Excluded(Browser.m_CommunitiesFilter), m_Countries(Browser.m_CountriesFilter), m_Types(Browser.m_TypesFilter), m_Cache(&Browser)
	{
	}
	void Prepare();
	void Commit() noexcept
	{
		m_Browser.m_vCommunities.swap(m_vCommunities);
		m_Browser.m_CommunityServersByAddr.swap(m_CommunityServersByAddr);
		m_Browser.m_FavoriteCommunitiesFilter.Swap(m_Favorites);
		m_Browser.m_CommunitiesFilter.Swap(m_Excluded);
		m_Browser.m_CountriesFilter.Swap(m_Countries);
		m_Browser.m_TypesFilter.Swap(m_Types);
		m_Browser.m_CommunityCache.Swap(m_Cache);
		m_Browser.m_OwnLocation = m_OwnLocation;
		m_Browser.m_ServerlistType = m_ServerlistType;
		for(const auto &Entry : m_vEntries)
			Entry.Publish();
		m_Browser.RequestResort();
		m_Browser.m_CommunityUpdateFailed = false;
		m_Browser.m_CommunityUpdateRetryAt = 0;
	}
};

const json_value *CServerBrowser::LoadDDNetInfo(IHttpRequest *pResponse)
{
	std::optional<CHttpResponseValidation> Validation;
	if(pResponse)
	{
		if(pResponse->State() != EHttpState::DONE)
			return nullptr;
		Validation.emplace(*pResponse);
		if(pResponse->IsAbortRequested())
			return nullptr;
	}
	try
	{
		void *pRawBuffer = nullptr;
		size_t Length = 0;
		std::unique_ptr<void, decltype(&free)> pBuffer(nullptr, free);
		if(pResponse)
		{
			unsigned char *pData;
			pResponse->Result(&pData, &Length);
			pRawBuffer = pData;
		}
		else
		{
			unsigned FileLength = 0;
			if(!m_pStorage->ReadFile(DDNET_INFO_FILE, IStorage::TYPE_SAVE, &pRawBuffer, &FileLength))
				return LoadDDNetServers() ? m_pDDNetInfo : nullptr;
			pBuffer.reset(pRawBuffer);
			Length = FileLength;
		}
		if(!pRawBuffer || Length == 0)
			return nullptr;
		json_settings Settings{};
		char aError[json_error_max];
		std::unique_ptr<json_value, decltype(&json_value_free)> pInfo(
			JsonParseEx(&Settings, static_cast<const json_char *>(pRawBuffer), Length, aError), json_value_free);
		if(!pInfo)
		{
			log_error("serverbrowser", "invalid info json: '%s'", aError);
			return nullptr;
		}
		if(pInfo->type != json_object)
		{
			log_error("serverbrowser", "invalid info root");
			return nullptr;
		}
		const SHA256_DIGEST Digest = sha256(pRawBuffer, Length);
		CCommunityUpdate Update(*this, pInfo.get(), Digest, true);
		Update.Prepare();
		if(Validation && !Validation->Commit())
			return nullptr;
		Update.Commit();
		// JSON 所有权和摘要与准备好的元数据一起发布，失败的摘要不会阻止以后重试。
		json_value *pOldInfo = m_pDDNetInfo;
		m_pDDNetInfo = pInfo.release();
		m_DDNetInfoSha256 = Digest;
		json_value_free(pOldInfo);
		return m_pDDNetInfo;
	}
	catch(...)
	{
		log_error("serverbrowser", "failed to load DDNet info; retaining previous info and communities");
		return nullptr;
	}
}

bool CServerBrowser::LoadDDNetServers()
{
	try
	{
		CCommunityUpdate Update(*this, m_pDDNetInfo, m_DDNetInfoSha256, false);
		Update.Prepare();
		Update.Commit();
		return true;
	}
	catch(...)
	{
		if(!m_CommunityUpdateFailed)
			log_error("serverbrowser", "failed to reload communities; retaining previous state and retrying");
		m_CommunityUpdateFailed = true;
		const int64_t Now = UpdateNow();
		m_CommunityUpdateRetryAt = Now + std::min<int64_t>(time_freq(), INT64_MAX - Now);
		return false;
	}
}

bool CServerBrowser::ParseCommunityServers(CCommunity *pCommunity, const json_value &Servers)
{
	for(unsigned ServerIndex = 0; ServerIndex < Servers.u.array.length; ++ServerIndex)
	{
		// pServer - { name, flagId, servers }
		const json_value &Server = Servers[ServerIndex];
		if(Server.type != json_object)
		{
			log_error("serverbrowser", "invalid server (ServerIndex=%u)", ServerIndex);
			return false;
		}

		const json_value &Name = Server["name"];
		const json_value &FlagId = Server["flagId"];
		const json_value &Types = Server["servers"];
		if(Name.type != json_string || FlagId.type != json_integer || Types.type != json_object)
		{
			log_error("serverbrowser", "invalid server attribute (ServerIndex=%u)", ServerIndex);
			return false;
		}
		// communities JSON 由第三方维护，超范围 flagId 会在浏览器渲染国家过滤器时
		// 触发 CCountryFlags::GetByCountryCode 的 dbg_assert（Invalid CountryCode）。
		// 范围与 countryflags.h 的 CODE_LB/CODE_UB 保持一致。
		if(FlagId.u.integer < -999 || FlagId.u.integer > 999)
		{
			log_error("serverbrowser", "community country flagId out of range (ServerIndex=%u, flagId=%" PRId64 "), skipping country '%s'",
				ServerIndex, FlagId.u.integer, Name.u.string.ptr);
			continue;
		}
		if(Types.u.object.length == 0)
			continue;

		if(str_has_cc(Name.u.string.ptr))
		{
			log_error("serverbrowser", "invalid community country name (ServerIndex=%u)", ServerIndex);
			return false;
		}
		pCommunity->m_vCountries.emplace_back(Name.u.string.ptr, FlagId.u.integer);
		CCommunityCountry *pCountry = &pCommunity->m_vCountries.back();

		for(unsigned TypeIndex = 0; TypeIndex < Types.u.object.length; ++TypeIndex)
		{
			const json_value &Addresses = *Types.u.object.values[TypeIndex].value;
			if(Addresses.type != json_array)
			{
				log_error("serverbrowser", "invalid addresses (ServerIndex=%u, TypeIndex=%u)", ServerIndex, TypeIndex);
				return false;
			}
			if(Addresses.u.array.length == 0)
				continue;

			const char *pTypeName = Types.u.object.values[TypeIndex].name;

			// add type if it doesn't exist already
			const auto CommunityType = std::find_if(pCommunity->m_vTypes.begin(), pCommunity->m_vTypes.end(), [pTypeName](const auto &Elem) {
				return str_comp(Elem.Name(), pTypeName) == 0;
			});
			if(CommunityType == pCommunity->m_vTypes.end())
			{
				pCommunity->m_vTypes.emplace_back(pTypeName);
			}

			// add addresses
			for(unsigned AddressIndex = 0; AddressIndex < Addresses.u.array.length; ++AddressIndex)
			{
				const json_value &Address = Addresses[AddressIndex];
				if(Address.type != json_string)
				{
					log_error("serverbrowser", "invalid address (ServerIndex=%u, TypeIndex=%u, AddressIndex=%u)", ServerIndex, TypeIndex, AddressIndex);
					return false;
				}
				NETADDR NetAddr;
				if(net_addr_from_str(&NetAddr, Address.u.string.ptr))
				{
					log_error("serverbrowser", "invalid address (ServerIndex=%u, TypeIndex=%u, AddressIndex=%u)", ServerIndex, TypeIndex, AddressIndex);
					continue;
				}
				pCountry->m_vServers.emplace_back(NetAddr, pTypeName);
			}
		}
	}
	return true;
}

bool CServerBrowser::ParseCommunityFinishes(CCommunity *pCommunity, const json_value &Finishes)
{
	for(unsigned FinishIndex = 0; FinishIndex < Finishes.u.array.length; ++FinishIndex)
	{
		const json_value &Finish = Finishes[FinishIndex];
		if(Finish.type != json_string)
		{
			log_error("serverbrowser", "invalid rank (FinishIndex=%u)", FinishIndex);
			return false;
		}
		pCommunity->m_FinishedMaps.emplace((const char *)Finish);
	}
	return true;
}

void CServerBrowser::CCommunityUpdate::Prepare()
{
	// TClient
	std::vector<json_value *> vCommunities;
	if(m_pInfo)
	{
		const json_value &Communities = (*m_pInfo)["communities"];
		if(Communities.type == json_array)
		{
			vCommunities.insert(
				vCommunities.end(),
				Communities.u.array.values,
				Communities.u.array.values + Communities.u.array.length);
		}
	}
	if(m_Browser.m_CustomCommunitiesFunction)
		m_Browser.m_CustomCommunitiesFunction(vCommunities);

	for(unsigned CommunityIndex = 0; CommunityIndex < vCommunities.size(); ++CommunityIndex)
	{
		const json_value &Community = *vCommunities[CommunityIndex];
		if(Community.type != json_object)
		{
			log_error("serverbrowser", "invalid community (CommunityIndex=%d)", (int)CommunityIndex);
			continue;
		}
		const json_value &Id = Community["id"];
		if(Id.type != json_string)
		{
			log_error("serverbrowser", "invalid community id (CommunityIndex=%d)", (int)CommunityIndex);
			continue;
		}
		const json_value &Icon = Community["icon"];
		const json_value &IconSha256 = Icon["sha256"];
		const json_value &IconUrl = Icon["url"];
		const json_value &Name = Community["name"];
		const json_value HasFinishes = Community["has_finishes"];
		const json_value *pFinishes = &Community["finishes"];
		const json_value *pServers = &Community["servers"];
		// We accidentally set finishes/servers to be part of icon in
		// the past, so support that, too. Can be removed once we make
		// a breaking change to the whole thing, necessitating a new
		// endpoint.
		if(pFinishes->type == json_none)
		{
			pFinishes = &Icon["finishes"];
		}
		if(pServers->type == json_none)
		{
			pServers = &Icon["servers"];
		}
		// Backward compatibility.
		if(pFinishes->type == json_none)
		{
			if(str_comp(Id, COMMUNITY_DDNET) == 0 && m_pInfo != nullptr)
			{
				pFinishes = &(*m_pInfo)["maps"];
			}
		}
		if(pServers->type == json_none)
		{
			if(str_comp(Id, COMMUNITY_DDNET) == 0 && m_pInfo != nullptr)
			{
				pServers = &(*m_pInfo)["servers"];
			}
			else if(str_comp(Id, "kog") == 0 && m_pInfo != nullptr)
			{
				pServers = &(*m_pInfo)["servers-kog"];
			}
		}
		if(false ||
			Icon.type != json_object ||
			IconSha256.type != json_string ||
			IconUrl.type != json_string ||
			Name.type != json_string ||
			HasFinishes.type != json_boolean ||
			(pFinishes->type != json_array && pFinishes->type != json_none) ||
			pServers->type != json_array)
		{
			log_error("serverbrowser", "invalid community attribute (CommunityId=%s)", (const char *)Id);
			continue;
		}
		SHA256_DIGEST ParsedIconSha256;
		if(sha256_from_str(&ParsedIconSha256, IconSha256) != 0)
		{
			log_error("serverbrowser", "invalid community icon sha256 (CommunityId=%s)", (const char *)Id);
			continue;
		}
		CCommunity NewCommunity(Id, Name, ParsedIconSha256, IconUrl);
		if(!ParseCommunityServers(&NewCommunity, *pServers))
		{
			log_error("serverbrowser", "invalid community servers (CommunityId=%s)", NewCommunity.Id());
			continue;
		}
		NewCommunity.m_HasFinishes = HasFinishes;
		if(NewCommunity.m_HasFinishes && pFinishes->type == json_array && !ParseCommunityFinishes(&NewCommunity, *pFinishes))
		{
			log_error("serverbrowser", "invalid community finishes (CommunityId=%s)", NewCommunity.Id());
			continue;
		}

		for(const auto &Country : NewCommunity.Countries())
		{
			for(const auto &Server : Country.Servers())
			{
				m_CommunityServersByAddr.emplace(CommunityAddressKey(Server.Address()), CCommunityServer(NewCommunity.Id(), Country.Name(), Server.TypeName()));
			}
		}
		m_vCommunities.push_back(std::move(NewCommunity));
	}

	// Add default none community
	{
		CCommunity NoneCommunity(COMMUNITY_NONE, "None", std::nullopt, "");
		NoneCommunity.m_vCountries.emplace_back(COMMUNITY_COUNTRY_NONE, -1);
		NoneCommunity.m_vTypes.emplace_back(COMMUNITY_TYPE_NONE);
		m_vCommunities.push_back(std::move(NoneCommunity));
	}

	if(!PreserveUnloadedCustomFilters(static_cast<bool>(m_Browser.m_CustomCommunitiesFunction)))
	{
		m_Favorites.Clean(m_vCommunities);
		m_Excluded.Clean(m_vCommunities);
		m_Countries.Clean(m_vCommunities);
		m_Types.Clean(m_vCommunities);
	}
	if(m_ServerlistType >= TYPE_FAVORITE_COMMUNITY_1 && m_ServerlistType <= TYPE_FAVORITE_COMMUNITY_5 &&
		static_cast<size_t>(m_ServerlistType - TYPE_FAVORITE_COMMUNITY_1) >= CollectFavorites(m_vCommunities, m_Favorites).size())
		m_ServerlistType = TYPE_INTERNET;
	m_Cache.Prepare(CollectCurrentCommunities(m_vCommunities, m_ServerlistType, m_Favorites, m_Excluded),
		m_InfoSha256, m_ServerlistType, HashCurrentCommunities(m_vCommunities, m_ServerlistType, m_Favorites, m_Excluded));

	if(m_ReloadLocation)
	{
		m_OwnLocation = CServerInfo::LOC_UNKNOWN;
		if(m_pInfo)
		{
			const auto &Location = (*m_pInfo)["location"];
			if(Location.type != json_string || CServerInfo::ParseLocation(&m_OwnLocation, Location))
				log_error("serverbrowser", "invalid location");
		}
	}
	const int PreviousLocation = m_Browser.DetermineOwnLocation();
	const int OwnLocation = str_comp(g_Config.m_BrLocation, "auto") == 0 ? m_OwnLocation : PreviousLocation;
	m_vEntries.reserve(m_Browser.m_vpServerlist.size());
	for(CServerEntry *pEntry : m_Browser.m_vpServerlist)
	{
		// 只复制归属和延迟计算所需的输入，不复制客户端数组和服务器文本。
		CServerInfo Info{};
		Info.m_NumAddresses = pEntry->m_Info.m_NumAddresses;
		mem_copy(Info.m_aAddresses, pEntry->m_Info.m_aAddresses, sizeof(Info.m_aAddresses));
		str_copy(Info.m_aMap, pEntry->m_Info.m_aMap);
		Info.m_Location = pEntry->m_Info.m_Location;
		Info.m_Latency = pEntry->m_Info.m_Latency;
		Info.m_LatencyIsEstimated = pEntry->m_Info.m_LatencyIsEstimated;
		SetCommunityInfo(&Info, m_CommunityServersByAddr);
		SetRankInfo(&Info, m_vCommunities);
		if(PreviousLocation != OwnLocation)
			m_Browser.UpdateServerLatency(&Info, OwnLocation);
		m_vEntries.emplace_back(pEntry, Info);
	}
}

void CServerBrowser::UpdateServerFilteredPlayers(CServerInfo *pInfo) const
{
	pInfo->m_NumFilteredPlayers = g_Config.m_BrFilterSpectators ? pInfo->m_NumPlayers : pInfo->m_NumClients;
	if(g_Config.m_BrFilterConnectingPlayers)
	{
		for(const auto &Client : pInfo->m_vClients)
		{
			if((!g_Config.m_BrFilterSpectators || Client.m_Player) && str_comp(Client.m_aName, "(connecting)") == 0 && Client.m_aClan[0] == '\0')
				pInfo->m_NumFilteredPlayers--;
		}
	}
}

void CServerBrowser::UpdateServerFriends(CServerInfo *pInfo) const
{
	pInfo->m_FriendState = IFriends::FRIEND_NO;
	pInfo->m_FriendNum = 0;
	for(auto &Client : pInfo->m_vClients)
	{
		Client.m_FriendState = m_pFriends->GetFriendState(Client.m_aName, Client.m_aClan);
		pInfo->m_FriendState = std::max(pInfo->m_FriendState, Client.m_FriendState);
		if(Client.m_FriendState != IFriends::FRIEND_NO)
			pInfo->m_FriendNum++;
	}
}

void CServerBrowser::UpdateServerCommunity(CServerInfo *pInfo) const
{
	SetCommunityInfo(pInfo, m_CommunityServersByAddr);
}

void CServerBrowser::UpdateServerRank(CServerInfo *pInfo) const
{
	SetRankInfo(pInfo, m_vCommunities);
}

void CServerBrowser::UpdateServerLatency(CServerInfo *pInfo, int OwnLocation) const
{
	const int Ping = m_pPingCache->GetPing(pInfo->m_aAddresses, pInfo->m_NumAddresses);
	// 没有缓存值（-1，含已过期条目）或缓存值触到测量上限时都按"没有实测"处理，
	// 让延迟列显示地区名而不是一个看似实测的数字。
	pInfo->m_LatencyIsEstimated = !IsUsableLatency(Ping);
	if(pInfo->m_LatencyIsEstimated)
	{
		pInfo->m_Latency = CServerInfo::EstimateLatency(OwnLocation, pInfo->m_Location);
	}
	else
	{
		pInfo->m_Latency = Ping;
	}
}

int CServerBrowser::DetermineOwnLocation() const
{
	if(str_comp(g_Config.m_BrLocation, "auto") == 0)
	{
		return m_OwnLocation;
	}

	int OwnLocation;
	if(CServerInfo::ParseLocation(&OwnLocation, g_Config.m_BrLocation))
	{
		log_error("serverbrowser", "Cannot parse br_location: '%s'", g_Config.m_BrLocation);
	}
	return OwnLocation;
}

void CServerBrowser::ValidateServerlistType()
{
	if(m_ServerlistType >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 &&
		m_ServerlistType <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5)
	{
		const size_t CommunityIndex = m_ServerlistType - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
		if(CommunityIndex >= FavoriteCommunities().size())
		{
			// Reset to internet type if there is no favorite community for the current browser type,
			// in case communities have been removed.
			m_ServerlistType = IServerBrowser::TYPE_INTERNET;
		}
	}
}

const char *CServerBrowser::GetTutorialServer()
{
	const CCommunity *pCommunity = Community(COMMUNITY_DDNET);
	if(pCommunity == nullptr)
		return nullptr;

	const char *pBestAddr = nullptr;
	int BestLatency = std::numeric_limits<int>::max();
	for(const auto &Country : pCommunity->Countries())
	{
		for(const auto &Server : Country.Servers())
		{
			if(str_comp(Server.TypeName(), "Tutorial") != 0)
				continue;
			const CServerEntry *pEntry = Find(Server.Address());
			if(!pEntry)
				continue;
			if(pEntry->m_Info.m_NumPlayers > pEntry->m_Info.m_MaxPlayers - 10)
				continue;
			if(pEntry->m_Info.m_Latency >= BestLatency)
				continue;
			BestLatency = pEntry->m_Info.m_Latency;
			pBestAddr = pEntry->m_Info.m_aAddress;
		}
	}
	return pBestAddr;
}

bool CServerBrowser::IsRefreshing() const
{
	return m_pFirstReqServer != nullptr;
}

bool CServerBrowser::IsGettingServerlist() const
{
	return m_pHttp->IsRefreshing();
}

bool CServerBrowser::IsServerlistError() const
{
	return m_HttpUpdateFailed || m_SortUpdateFailed || m_CommunityUpdateFailed || m_CommunityCache.m_UpdateFailed || m_pHttp->IsError();
}

bool CServerBrowser::IsServerlistStale() const
{
	return m_pHttp->IsStale();
}

int CServerBrowser::LoadingProgression() const
{
	if(m_vpServerlist.empty())
		return 0;

	int Servers = m_vpServerlist.size();
	int Loaded = m_vpServerlist.size() - m_NumRequests;
	return 100.0f * Loaded / Servers;
}

void CCommunity::Swap(CCommunity &Other) noexcept
{
	std::swap(m_aId, Other.m_aId);
	std::swap(m_aName, Other.m_aName);
	std::swap(m_IconSha256, Other.m_IconSha256);
	std::swap(m_aIconUrl, Other.m_aIconUrl);
	m_vCountries.swap(Other.m_vCountries);
	m_vTypes.swap(Other.m_vTypes);
	std::swap(m_NumPlayers, Other.m_NumPlayers);
	std::swap(m_HasFinishes, Other.m_HasFinishes);
	m_FinishedMaps.swap(Other.m_FinishedMaps);
}

bool CCommunity::HasCountry(const char *pCountryName) const
{
	return std::find_if(Countries().begin(), Countries().end(), [pCountryName](const auto &Elem) {
		return str_comp(Elem.Name(), pCountryName) == 0;
	}) != Countries().end();
}

bool CCommunity::HasType(const char *pTypeName) const
{
	return std::find_if(Types().begin(), Types().end(), [pTypeName](const auto &Elem) {
		return str_comp(Elem.Name(), pTypeName) == 0;
	}) != Types().end();
}

CServerInfo::ERankState CCommunity::HasRank(const char *pMap) const
{
	if(!HasRanks())
		return CServerInfo::RANK_UNAVAILABLE;
	const CCommunityMap Needle(pMap);
	return !m_FinishedMaps.contains(Needle) ? CServerInfo::RANK_UNRANKED : CServerInfo::RANK_RANKED;
}

const std::vector<CCommunity> &CServerBrowser::Communities() const
{
	return m_vCommunities;
}

const CCommunity *CServerBrowser::Community(const char *pCommunityId) const
{
	return FindCommunity(m_vCommunities, pCommunityId);
}

std::vector<const CCommunity *> CServerBrowser::SelectedCommunities() const
{
	return CollectCurrentCommunities(m_vCommunities, TYPE_INTERNET, m_FavoriteCommunitiesFilter, m_CommunitiesFilter);
}

std::vector<const CCommunity *> CServerBrowser::FavoriteCommunities() const
{
	return CollectFavorites(m_vCommunities, m_FavoriteCommunitiesFilter);
}

std::vector<const CCommunity *> CServerBrowser::CurrentCommunities() const
{
	return CollectCurrentCommunities(m_vCommunities, m_ServerlistType, m_FavoriteCommunitiesFilter, m_CommunitiesFilter);
}

unsigned CServerBrowser::CurrentCommunitiesHash() const
{
	return HashCurrentCommunities(m_vCommunities, m_ServerlistType, m_FavoriteCommunitiesFilter, m_CommunitiesFilter);
}

void CCommunityCache::Swap(CCommunityCache &Other) noexcept
{
	std::swap(m_InfoSha256, Other.m_InfoSha256);
	std::swap(m_LastType, Other.m_LastType);
	std::swap(m_SelectedCommunitiesHash, Other.m_SelectedCommunitiesHash);
	std::swap(m_UpdateFailed, Other.m_UpdateFailed);
	m_vpSelectedCommunities.swap(Other.m_vpSelectedCommunities);
	m_vpSelectableCountries.swap(Other.m_vpSelectableCountries);
	m_vpSelectableTypes.swap(Other.m_vpSelectableTypes);
	std::swap(m_AnyRanksAvailable, Other.m_AnyRanksAvailable);
	std::swap(m_CountryTypesFilterAvailable, Other.m_CountryTypesFilterAvailable);
	std::swap(m_aCountryTypeFilterKey, Other.m_aCountryTypeFilterKey);
}

void CCommunityCache::Prepare(std::vector<const CCommunity *> vpSelected, std::optional<SHA256_DIGEST> InfoSha256, int Type, unsigned CommunitiesHash)
{
	m_vpSelectedCommunities = std::move(vpSelected);
	m_InfoSha256 = InfoSha256;
	m_LastType = Type;
	m_SelectedCommunitiesHash = CommunitiesHash;
	m_vpSelectableCountries.clear();
	m_vpSelectableTypes.clear();
	for(const CCommunity *pCommunity : m_vpSelectedCommunities)
	{
		for(const auto &Country : pCommunity->Countries())
		{
			const auto ExistingCountry = std::find_if(m_vpSelectableCountries.begin(), m_vpSelectableCountries.end(), [&](const CCommunityCountry *pOther) {
				return str_comp(Country.Name(), pOther->Name()) == 0 && Country.FlagId() == pOther->FlagId();
			});
			if(ExistingCountry == m_vpSelectableCountries.end())
			{
				m_vpSelectableCountries.push_back(&Country);
			}
		}
		for(const auto &Type : pCommunity->Types())
		{
			const auto ExistingType = std::find_if(m_vpSelectableTypes.begin(), m_vpSelectableTypes.end(), [&](const CCommunityType *pOther) {
				return str_comp(Type.Name(), pOther->Name()) == 0;
			});
			if(ExistingType == m_vpSelectableTypes.end())
			{
				m_vpSelectableTypes.push_back(&Type);
			}
		}
	}

	m_AnyRanksAvailable = std::any_of(m_vpSelectedCommunities.begin(), m_vpSelectedCommunities.end(), [](const CCommunity *pCommunity) {
		return pCommunity->HasRanks();
	});

	// Country/type filters not shown if there are no countries and types, or if only the none-community is selected
	m_CountryTypesFilterAvailable = (!m_vpSelectableCountries.empty() || !m_vpSelectableTypes.empty()) &&
					(m_vpSelectedCommunities.size() != 1 || str_comp(m_vpSelectedCommunities[0]->Id(), IServerBrowser::COMMUNITY_NONE) != 0);

	if(Type >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && Type <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 && !m_vpSelectedCommunities.empty())
		str_copy(m_aCountryTypeFilterKey, m_vpSelectedCommunities.front()->Id());
	else
		str_copy(m_aCountryTypeFilterKey, IServerBrowser::COMMUNITY_ALL);
}

void CCommunityCache::Update(bool Force)
{
	try
	{
		unsigned CommunitiesHash = m_pServerBrowser->CurrentCommunitiesHash();
		int Type = m_pServerBrowser->GetCurrentType();
		const bool SelectionChanged = m_LastType == Type && m_SelectedCommunitiesHash != CommunitiesHash;
		if(SelectionChanged && Type >= IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 && Type <= IServerBrowser::TYPE_FAVORITE_COMMUNITY_5)
		{
			m_pServerBrowser->Refresh(Type, true);
			// Refresh 中的排序可以作废本缓存，准备时重新读取最终社区顺序与页面。
			CommunitiesHash = m_pServerBrowser->CurrentCommunitiesHash();
			Type = m_pServerBrowser->GetCurrentType();
		}
		if(!Force && !m_UpdateFailed && m_InfoSha256 == m_pServerBrowser->DDNetInfoSha256() &&
			m_LastType == Type && m_SelectedCommunitiesHash == CommunitiesHash)
			return;
		CCommunityCache Prepared(m_pServerBrowser);
		Prepared.Prepare(m_pServerBrowser->CurrentCommunities(), m_pServerBrowser->DDNetInfoSha256(), Type, CommunitiesHash);
		m_pServerBrowser->CleanFilters();
		Swap(Prepared);
	}
	catch(...)
	{
		if(!m_UpdateFailed)
			log_error("serverbrowser", "failed to rebuild community cache; retaining previous state and retrying");
		m_UpdateFailed = true;
	}
}

void CFavoriteCommunityFilterList::Add(const char *pCommunityId)
{
	// Remove community if it's already a favorite, so it will be added again at
	// the end of the list, to allow setting the entire list easier with binds.
	Remove(pCommunityId);

	// Ensure maximum number of favorite communities, by removing the least-recently
	// added community from the beginning, when the maximum number of favorite
	// communities has been reached.
	constexpr size_t MaxFavoriteCommunities = IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 + 1;
	if(m_vEntries.size() >= MaxFavoriteCommunities)
	{
		dbg_assert(m_vEntries.size() == MaxFavoriteCommunities, "Maximum number of communities can never be exceeded");
		m_vEntries.erase(m_vEntries.begin());
	}
	m_vEntries.emplace_back(pCommunityId);
}

void CFavoriteCommunityFilterList::Remove(const char *pCommunityId)
{
	auto FoundCommunity = std::find(m_vEntries.begin(), m_vEntries.end(), CCommunityId(pCommunityId));
	if(FoundCommunity != m_vEntries.end())
	{
		m_vEntries.erase(FoundCommunity);
	}
}

void CFavoriteCommunityFilterList::Clear()
{
	m_vEntries.clear();
}

bool CFavoriteCommunityFilterList::Filtered(const char *pCommunityId) const
{
	return std::find(m_vEntries.begin(), m_vEntries.end(), CCommunityId(pCommunityId)) != m_vEntries.end();
}

bool CFavoriteCommunityFilterList::Empty() const
{
	return m_vEntries.empty();
}

void CFavoriteCommunityFilterList::Clean(const std::vector<CCommunity> &vAllowedCommunities)
{
	auto It = std::remove_if(m_vEntries.begin(), m_vEntries.end(), [&](const auto &Community) {
		return std::find_if(vAllowedCommunities.begin(), vAllowedCommunities.end(), [&](const CCommunity &AllowedCommunity) {
			return str_comp(Community.Id(), AllowedCommunity.Id()) == 0;
		}) == vAllowedCommunities.end();
	});
	m_vEntries.erase(It, m_vEntries.end());
}

void CFavoriteCommunityFilterList::Save(IConfigManager *pConfigManager) const
{
	char aBuf[32 + CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	for(const auto &FavoriteCommunity : m_vEntries)
	{
		str_copy(aBuf, "add_favorite_community \"");
		str_append(aBuf, FavoriteCommunity.Id());
		str_append(aBuf, "\"");
		pConfigManager->WriteLine(aBuf);
	}
}

const std::vector<CCommunityId> &CFavoriteCommunityFilterList::Entries() const
{
	return m_vEntries;
}

template<typename TNamedElement, typename TElementName>
static bool IsSubsetEquals(const std::vector<const TNamedElement *> &vpLeft, const std::set<TElementName> &Right)
{
	return vpLeft.size() <= Right.size() && std::all_of(vpLeft.begin(), vpLeft.end(), [&](const TNamedElement *pElem) {
		return Right.contains(TElementName(pElem->Name()));
	});
}

void CExcludedCommunityFilterList::Add(const char *pCommunityId)
{
	m_Entries.emplace(pCommunityId);
}

void CExcludedCommunityFilterList::Remove(const char *pCommunityId)
{
	m_Entries.erase(CCommunityId(pCommunityId));
}

void CExcludedCommunityFilterList::Clear()
{
	m_Entries.clear();
}

bool CExcludedCommunityFilterList::Filtered(const char *pCommunityId) const
{
	return std::find(m_Entries.begin(), m_Entries.end(), CCommunityId(pCommunityId)) != m_Entries.end();
}

bool CExcludedCommunityFilterList::Empty() const
{
	return m_Entries.empty();
}

void CExcludedCommunityFilterList::Clean(const std::vector<CCommunity> &vAllowedCommunities)
{
	for(auto It = m_Entries.begin(); It != m_Entries.end();)
	{
		const bool Found = std::find_if(vAllowedCommunities.begin(), vAllowedCommunities.end(), [&](const CCommunity &AllowedCommunity) {
			return str_comp(It->Id(), AllowedCommunity.Id()) == 0;
		}) != vAllowedCommunities.end();
		if(Found)
		{
			++It;
		}
		else
		{
			It = m_Entries.erase(It);
		}
	}
	// Prevent filter that would exclude all allowed communities
	if(m_Entries.size() == vAllowedCommunities.size())
	{
		m_Entries.clear();
	}
}

void CExcludedCommunityFilterList::Save(IConfigManager *pConfigManager) const
{
	char aBuf[32 + CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	for(const auto &ExcludedCommunity : m_Entries)
	{
		str_copy(aBuf, "add_excluded_community \"");
		str_append(aBuf, ExcludedCommunity.Id());
		str_append(aBuf, "\"");
		pConfigManager->WriteLine(aBuf);
	}
}

void CExcludedCommunityCountryFilterList::Add(const char *pCountryName)
{
	// Handle special case that all selectable entries are currently filtered,
	// where adding more entries to the exclusion list would have no effect.
	auto CommunityEntry = m_Entries.find(m_pCommunityCache->CountryTypeFilterKey());
	if(CommunityEntry != m_Entries.end() && IsSubsetEquals(m_pCommunityCache->SelectableCountries(), CommunityEntry->second))
	{
		for(const CCommunityCountry *pSelectableCountry : m_pCommunityCache->SelectableCountries())
		{
			CommunityEntry->second.erase(pSelectableCountry->Name());
		}
	}

	Add(m_pCommunityCache->CountryTypeFilterKey(), pCountryName);
	// 被排除的国家不再是"保留可见"的，避免它之后重新出现时被当成用户特意保留。
	RemoveAllowed(pCountryName);
}

void CExcludedCommunityCountryFilterList::Add(const char *pCommunityId, const char *pCountryName)
{
	CCommunityId CommunityId(pCommunityId);
	if(!m_Entries.contains(CommunityId))
	{
		m_Entries[CommunityId] = {};
	}
	m_Entries[CommunityId].emplace(pCountryName);
}

void CExcludedCommunityCountryFilterList::AddAllowed(const char *pCountryName)
{
	const CCommunityId Key(m_pCommunityCache->CountryTypeFilterKey());
	EnsureAllowedBaseline(Key);
	AddAllowed(Key.Id(), pCountryName);
}

void CExcludedCommunityCountryFilterList::EnsureAllowedBaseline(const CCommunityId &Key)
{
	if(m_AllowedCountries.contains(Key))
		return;

	const auto DeniedEntry = m_Entries.find(Key);
	std::set<CCommunityCountryName> Prepared;
	for(const CCommunityCountry *pCountry : m_pCommunityCache->SelectableCountries())
	{
		if(DeniedEntry == m_Entries.end() || !DeniedEntry->second.contains(CCommunityCountryName(pCountry->Name())))
		{
			Prepared.emplace(pCountry->Name());
		}
	}
	// 构造完成后一次发布，失败时不能留下被当作有效基线的空项。
	m_AllowedCountries.emplace(Key, std::move(Prepared));
}

void CExcludedCommunityCountryFilterList::AddAllowed(const char *pCommunityId, const char *pCountryName)
{
	CCommunityId CommunityId(pCommunityId);
	if(!m_AllowedCountries.contains(CommunityId))
	{
		m_AllowedCountries[CommunityId] = {};
	}
	m_AllowedCountries[CommunityId].emplace(pCountryName);
}

void CExcludedCommunityCountryFilterList::RemoveAllowed(const char *pCountryName)
{
	RemoveAllowed(m_pCommunityCache->CountryTypeFilterKey(), pCountryName);
}

void CExcludedCommunityCountryFilterList::RemoveAllowed(const char *pCommunityId, const char *pCountryName)
{
	auto CommunityEntry = m_AllowedCountries.find(CCommunityId(pCommunityId));
	if(CommunityEntry != m_AllowedCountries.end())
	{
		CommunityEntry->second.erase(pCountryName);
	}
}

bool CExcludedCommunityCountryFilterList::AutoExcludeNewCountries() noexcept
try
{
	const CCommunityId Key(m_pCommunityCache->CountryTypeFilterKey());
	auto DeniedEntry = m_Entries.find(Key);
	if(DeniedEntry == m_Entries.end() || DeniedEntry->second.empty())
		return false; // 未设置国家筛选，没有需要保持的结果

	const auto &vpSelectable = m_pCommunityCache->SelectableCountries();
	const auto &CountryEntries = DeniedEntry->second;
	// 排除名单覆盖了全部可选国家时筛选等价于不过滤（与 Filtered/Empty 的判断一致），
	// 此时用户没有"只看某些国家"的结果需要保持。
	if(std::none_of(vpSelectable.begin(), vpSelectable.end(), [&](const CCommunityCountry *pCountry) {
		   return !CountryEntries.contains(CCommunityCountryName(pCountry->Name()));
	   }))
	{
		return false;
	}

	const bool HadBaseline = m_AllowedCountries.contains(Key);
	// 旧配置或本次会话第一次同步：没有保留基线，无法区分"新出现的国家"和"用户保留的国家"。
	// 按当前可见的国家建立基线，本次不自动排除任何国家，用户已有的结果原样保留。
	EnsureAllowedBaseline(Key);
	if(!HadBaseline)
		return false;

	const auto &AllowedCountries = m_AllowedCountries.find(Key)->second;
	std::set<CCommunityCountryName> Prepared;
	bool Changed = false;
	for(const CCommunityCountry *pCountry : vpSelectable)
	{
		if(str_comp(pCountry->Name(), IServerBrowser::COMMUNITY_COUNTRY_NONE) == 0)
		{
			// 与筛选网格一致：未分类(none) 不参与排除。
			continue;
		}
		if(CountryEntries.contains(CCommunityCountryName(pCountry->Name())) || AllowedCountries.contains(CCommunityCountryName(pCountry->Name())))
			continue;
		// 无新增国家时不复制；有变化时先完整准备，避免异常只排除一部分新国家。
		if(!Changed)
			Prepared = CountryEntries;
		Prepared.emplace(pCountry->Name());
		Changed = true;
	}
	if(Changed)
		DeniedEntry->second.swap(Prepared);
	return Changed;
}
catch(...)
{
	return false;
}

void CExcludedCommunityCountryFilterList::Remove(const char *pCountryName)
{
	Remove(m_pCommunityCache->CountryTypeFilterKey(), pCountryName);
	// 用户显式让这个国家可见，之后新国家出现时不能把它一并排除。
	AddAllowed(pCountryName);
}

void CExcludedCommunityCountryFilterList::Remove(const char *pCommunityId, const char *pCountryName)
{
	auto CommunityEntry = m_Entries.find(CCommunityId(pCommunityId));
	if(CommunityEntry != m_Entries.end())
	{
		CommunityEntry->second.erase(pCountryName);
	}
}

void CExcludedCommunityCountryFilterList::Clear()
{
	auto CommunityEntry = m_Entries.find(m_pCommunityCache->CountryTypeFilterKey());
	if(CommunityEntry != m_Entries.end())
	{
		CommunityEntry->second.clear();
	}
	// 筛选被清空后没有需要保持的结果，丢弃保留基线，下次设置筛选时按当时状态重建。
	m_AllowedCountries.erase(CCommunityId(m_pCommunityCache->CountryTypeFilterKey()));
}

bool CExcludedCommunityCountryFilterList::Filtered(const char *pCountryName) const
{
	auto CommunityEntry = m_Entries.find(CCommunityId(m_pCommunityCache->CountryTypeFilterKey()));
	if(CommunityEntry == m_Entries.end())
		return false;

	const auto &CountryEntries = CommunityEntry->second;
	return !IsSubsetEquals(m_pCommunityCache->SelectableCountries(), CountryEntries) && CountryEntries.contains(CCommunityCountryName(pCountryName));
}

bool CExcludedCommunityCountryFilterList::Empty() const
{
	auto CommunityEntry = m_Entries.find(CCommunityId(m_pCommunityCache->CountryTypeFilterKey()));
	return CommunityEntry == m_Entries.end() ||
	       CommunityEntry->second.empty() ||
	       IsSubsetEquals(m_pCommunityCache->SelectableCountries(), CommunityEntry->second);
}

void CExcludedCommunityCountryFilterList::Clean(const std::vector<CCommunity> &vAllowedCommunities)
{
	std::set<CCommunityId> ExistingCommunityIds;
	for(const CCommunity &AllowedCommunity : vAllowedCommunities)
	{
		ExistingCommunityIds.emplace(AllowedCommunity.Id());
	}
	CleanCountries(ExistingCommunityIds);
}

void CExcludedCommunityCountryFilterList::CleanCountries(const std::set<CCommunityId> &vExistingCommunityIds)
{
	// 只清理"社区本身已不存在"的条目。不再因为某个国家暂时不在当前社区列表里就删除它：社区国家
	// 列表由当时的服务器聚合而成，服务器掉线或换位置会让国家临时消失，删掉条目会让用户"只看某国"
	// 的结果永久漂移（该国回来后就再也不会被排除）。
	for(auto It = m_Entries.begin(); It != m_Entries.end();)
	{
		const bool AllEntry = str_comp(It->first.Id(), IServerBrowser::COMMUNITY_ALL) == 0;
		if(AllEntry || vExistingCommunityIds.contains(It->first))
		{
			++It;
		}
		else
		{
			It = m_Entries.erase(It);
		}
	}
	for(auto It = m_AllowedCountries.begin(); It != m_AllowedCountries.end();)
	{
		const bool AllEntry = str_comp(It->first.Id(), IServerBrowser::COMMUNITY_ALL) == 0;
		if(AllEntry || vExistingCommunityIds.contains(It->first))
		{
			++It;
		}
		else
		{
			It = m_AllowedCountries.erase(It);
		}
	}

	// 这里不再做"排除名单覆盖了社区全部国家就清空名单"的处理：Filtered()/Empty() 已经把这种状态
	// 当作"没有筛选"（全部显示）处理，而清空会把用户设置的名单永久丢掉。社区国家列表只是当时
	// 服务器的快照，某个国家临时缺失也会让名单看起来覆盖了全部国家，清空同样会误伤用户设置。
}

void CExcludedCommunityCountryFilterList::Save(IConfigManager *pConfigManager) const
{
	char aBuf[32 + CServerInfo::MAX_COMMUNITY_ID_LENGTH + CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH];
	for(const auto &[Community, Countries] : m_Entries)
	{
		for(const auto &Country : Countries)
		{
			str_copy(aBuf, "add_excluded_country \"");
			str_append(aBuf, Community.Id());
			str_append(aBuf, "\" \"");
			str_append(aBuf, Country.Name());
			str_append(aBuf, "\"");
			pConfigManager->WriteLine(aBuf);
		}
	}
	// 只持久化仍然生效（排除名单非空）的筛选所对应的保留基线，未设置筛选时不写入整份国家列表。
	for(const auto &[Community, Countries] : m_AllowedCountries)
	{
		const auto DeniedEntry = m_Entries.find(Community);
		if(DeniedEntry == m_Entries.end() || DeniedEntry->second.empty())
			continue;
		for(const auto &Country : Countries)
		{
			str_copy(aBuf, "add_allowed_country \"");
			str_append(aBuf, Community.Id());
			str_append(aBuf, "\" \"");
			str_append(aBuf, Country.Name());
			str_append(aBuf, "\"");
			pConfigManager->WriteLine(aBuf);
		}
	}
}

void CExcludedCommunityTypeFilterList::Add(const char *pTypeName)
{
	// Handle special case that all selectable entries are currently filtered,
	// where adding more entries to the exclusion list would have no effect.
	auto CommunityEntry = m_Entries.find(m_pCommunityCache->CountryTypeFilterKey());
	if(CommunityEntry != m_Entries.end() && IsSubsetEquals(m_pCommunityCache->SelectableTypes(), CommunityEntry->second))
	{
		for(const CCommunityType *pSelectableType : m_pCommunityCache->SelectableTypes())
		{
			CommunityEntry->second.erase(pSelectableType->Name());
		}
	}

	Add(m_pCommunityCache->CountryTypeFilterKey(), pTypeName);
}

void CExcludedCommunityTypeFilterList::Add(const char *pCommunityId, const char *pTypeName)
{
	CCommunityId CommunityId(pCommunityId);
	if(!m_Entries.contains(CommunityId))
	{
		m_Entries[CommunityId] = {};
	}
	m_Entries[CommunityId].emplace(pTypeName);
}

void CExcludedCommunityTypeFilterList::Remove(const char *pTypeName)
{
	Remove(m_pCommunityCache->CountryTypeFilterKey(), pTypeName);
}

void CExcludedCommunityTypeFilterList::Remove(const char *pCommunityId, const char *pTypeName)
{
	auto CommunityEntry = m_Entries.find(CCommunityId(pCommunityId));
	if(CommunityEntry != m_Entries.end())
	{
		CommunityEntry->second.erase(pTypeName);
	}
}

void CExcludedCommunityTypeFilterList::Clear()
{
	auto CommunityEntry = m_Entries.find(m_pCommunityCache->CountryTypeFilterKey());
	if(CommunityEntry != m_Entries.end())
	{
		CommunityEntry->second.clear();
	}
}

bool CExcludedCommunityTypeFilterList::Filtered(const char *pTypeName) const
{
	auto CommunityEntry = m_Entries.find(CCommunityId(m_pCommunityCache->CountryTypeFilterKey()));
	if(CommunityEntry == m_Entries.end())
		return false;

	const auto &TypeEntries = CommunityEntry->second;
	return !IsSubsetEquals(m_pCommunityCache->SelectableTypes(), TypeEntries) && TypeEntries.contains(CCommunityTypeName(pTypeName));
}

bool CExcludedCommunityTypeFilterList::Empty() const
{
	auto CommunityEntry = m_Entries.find(CCommunityId(m_pCommunityCache->CountryTypeFilterKey()));
	return CommunityEntry == m_Entries.end() ||
	       CommunityEntry->second.empty() ||
	       IsSubsetEquals(m_pCommunityCache->SelectableTypes(), CommunityEntry->second);
}

void CExcludedCommunityTypeFilterList::Clean(const std::vector<CCommunity> &vAllowedCommunities)
{
	for(auto It = m_Entries.begin(); It != m_Entries.end();)
	{
		const bool AllEntry = str_comp(It->first.Id(), IServerBrowser::COMMUNITY_ALL) == 0;
		const bool Found = AllEntry || std::find_if(vAllowedCommunities.begin(), vAllowedCommunities.end(), [&](const CCommunity &AllowedCommunity) {
			return str_comp(It->first.Id(), AllowedCommunity.Id()) == 0;
		}) != vAllowedCommunities.end();
		if(Found)
		{
			++It;
		}
		else
		{
			It = m_Entries.erase(It);
		}
	}

	for(const CCommunity &AllowedCommunity : vAllowedCommunities)
	{
		auto CommunityEntry = m_Entries.find(CCommunityId(AllowedCommunity.Id()));
		if(CommunityEntry != m_Entries.end())
		{
			auto &TypeEntries = CommunityEntry->second;
			for(auto It = TypeEntries.begin(); It != TypeEntries.end();)
			{
				if(AllowedCommunity.HasType(It->Name()))
				{
					++It;
				}
				else
				{
					It = TypeEntries.erase(It);
				}
			}
			// Prevent filter that would exclude all allowed types
			if(TypeEntries.size() == AllowedCommunity.Types().size())
			{
				TypeEntries.clear();
			}
		}
	}

	auto AllCommunityEntry = m_Entries.find(CCommunityId(IServerBrowser::COMMUNITY_ALL));
	if(AllCommunityEntry != m_Entries.end())
	{
		auto &TypeEntries = AllCommunityEntry->second;
		for(auto It = TypeEntries.begin(); It != TypeEntries.end();)
		{
			if(std::any_of(vAllowedCommunities.begin(), vAllowedCommunities.end(), [&](const auto &Community) { return Community.HasType(It->Name()); }))
			{
				++It;
			}
			else
			{
				It = TypeEntries.erase(It);
			}
		}
		// Prevent filter that would exclude all allowed types
		std::unordered_set<CCommunityCountryName> UniqueTypes;
		for(const CCommunity &AllowedCommunity : vAllowedCommunities)
		{
			for(const CCommunityType &Type : AllowedCommunity.Types())
			{
				UniqueTypes.emplace(Type.Name());
			}
		}
		if(TypeEntries.size() == UniqueTypes.size())
		{
			TypeEntries.clear();
		}
	}
}

void CExcludedCommunityTypeFilterList::Save(IConfigManager *pConfigManager) const
{
	char aBuf[32 + CServerInfo::MAX_COMMUNITY_ID_LENGTH + CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];
	for(const auto &[Community, Types] : m_Entries)
	{
		for(const auto &Type : Types)
		{
			str_copy(aBuf, "add_excluded_type \"");
			str_append(aBuf, Community.Id());
			str_append(aBuf, "\" \"");
			str_append(aBuf, Type.Name());
			str_append(aBuf, "\"");
			pConfigManager->WriteLine(aBuf);
		}
	}
}

void CServerBrowser::CleanFilters()
{
	if(m_vCommunities.empty() || PreserveUnloadedCustomFilters(static_cast<bool>(m_CustomCommunitiesFunction)))
		return;
	// 清理可能分配集合；所有列表准备成功后才同时替换，避免只删除一部分用户筛选。
	auto Favorites = m_FavoriteCommunitiesFilter;
	auto Excluded = m_CommunitiesFilter;
	auto Countries = m_CountriesFilter;
	auto Types = m_TypesFilter;
	Favorites.Clean(m_vCommunities);
	Excluded.Clean(m_vCommunities);
	Countries.Clean(m_vCommunities);
	Types.Clean(m_vCommunities);
	m_FavoriteCommunitiesFilter.Swap(Favorites);
	m_CommunitiesFilter.Swap(Excluded);
	m_CountriesFilter.Swap(Countries);
	m_TypesFilter.Swap(Types);
}

bool CServerBrowser::IsRegistered(const NETADDR &Addr)
{
	const int NumServers = m_pHttp->NumServers();
	for(int i = 0; i < NumServers; i++)
	{
		const CServerInfo &Info = m_pHttp->Server(i);
		for(int j = 0; j < Info.m_NumAddresses; j++)
		{
			if(net_addr_comp(&Info.m_aAddresses[j], &Addr) == 0)
			{
				return true;
			}
		}
	}
	return false;
}

int CServerInfo::EstimateLatency(int Loc1, int Loc2)
{
	if(Loc1 == LOC_UNKNOWN || Loc2 == LOC_UNKNOWN)
	{
		return 999;
	}
	if(Loc1 != Loc2)
	{
		return 199;
	}
	return 99;
}

ColorRGBA CServerInfo::GametypeColor(const char *pGametype)
{
	ColorHSLA HslaColor;
	if(str_comp(pGametype, "DM") == 0 || str_comp(pGametype, "TDM") == 0 || str_comp(pGametype, "CTF") == 0 || str_comp(pGametype, "LMS") == 0 || str_comp(pGametype, "LTS") == 0)
		HslaColor = ColorHSLA(0.33f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "catch"))
		HslaColor = ColorHSLA(0.17f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "dm") || str_find_nocase(pGametype, "tdm") || str_find_nocase(pGametype, "ctf") || str_find_nocase(pGametype, "lms") || str_find_nocase(pGametype, "lts"))
	{
		if(pGametype[0] == 'i' || pGametype[0] == 'g')
			HslaColor = ColorHSLA(0.0f, 1.0f, 0.75f);
		else
			HslaColor = ColorHSLA(0.40f, 1.0f, 0.75f);
	}
	else if(str_find_nocase(pGametype, "s-ddracex"))
		HslaColor = ColorHSLA(1.0f, 1.0f, 0.7f);
	else if(str_find_nocase(pGametype, "f-ddrace") || str_find_nocase(pGametype, "freeze"))
		HslaColor = ColorHSLA(0.0f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "fng"))
		HslaColor = ColorHSLA(0.83f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "gores"))
		HslaColor = ColorHSLA(0.525f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "BW"))
		HslaColor = ColorHSLA(0.05f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "ddracenet") || str_find_nocase(pGametype, "ddnet") || str_find_nocase(pGametype, "0xf"))
		HslaColor = ColorHSLA(0.58f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "ddrace") || str_find_nocase(pGametype, "mkrace"))
		HslaColor = ColorHSLA(0.75f, 1.0f, 0.75f);
	else if(str_find_nocase(pGametype, "race") || str_find_nocase(pGametype, "fastcap"))
		HslaColor = ColorHSLA(0.46f, 1.0f, 0.75f);
	else
		HslaColor = ColorHSLA(1.0f, 1.0f, 1.0f);
	return color_cast<ColorRGBA>(HslaColor);
}

bool CServerInfo::ParseLocation(int *pResult, const char *pString)
{
	*pResult = LOC_UNKNOWN;
	int Length = str_length(pString);
	if(Length < 2)
	{
		return true;
	}
	// ISO continent code. Allow antarctica, but treat it as unknown.
	static const char s_apLocations[NUM_LOCS][6] = {
		"an", // LOC_UNKNOWN
		"af", // LOC_AFRICA
		"as", // LOC_ASIA
		"oc", // LOC_AUSTRALIA
		"eu", // LOC_EUROPE
		"na", // LOC_NORTH_AMERICA
		"sa", // LOC_SOUTH_AMERICA
		"as:cn", // LOC_CHINA
	};
	for(int i = std::size(s_apLocations) - 1; i >= 0; i--)
	{
		if(str_startswith(pString, s_apLocations[i]))
		{
			*pResult = i;
			return false;
		}
	}
	return true;
}
