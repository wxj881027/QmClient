/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVERBROWSER_H
#define ENGINE_SERVERBROWSER_H

#include "kernel.h"

#include <base/color.h>
#include <base/hash.h>
#include <base/system.h>

#include <engine/map.h>
#include <engine/shared/protocol.h>

#include <generated/protocol7.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static constexpr const char *DDNET_INFO_FILE = "ddnet-info.json";
static constexpr const char *DDNET_INFO_URL = "https://info.ddnet.org/info";

class CUIElement;

class CServerInfo
{
public:
	enum
	{
		LOC_UNKNOWN = 0,
		LOC_AFRICA,
		LOC_ASIA,
		LOC_AUSTRALIA,
		LOC_EUROPE,
		LOC_NORTH_AMERICA,
		LOC_SOUTH_AMERICA,
		// Special case China because it has an exceptionally bad
		// connection to the outside due to the Great Firewall of
		// China:
		// https://en.wikipedia.org/w/index.php?title=Great_Firewall&oldid=1019589632
		LOC_CHINA,
		NUM_LOCS,
	};

	enum EClientScoreKind
	{
		CLIENT_SCORE_KIND_UNSPECIFIED,
		CLIENT_SCORE_KIND_POINTS,
		CLIENT_SCORE_KIND_TIME,
		CLIENT_SCORE_KIND_TIME_BACKCOMPAT,
	};

	enum ERankState
	{
		RANK_UNAVAILABLE,
		RANK_RANKED,
		RANK_UNRANKED,
	};

	enum
	{
		MAX_COMMUNITY_ID_LENGTH = 32,
		MAX_COMMUNITY_COUNTRY_LENGTH = 32,
		MAX_COMMUNITY_TYPE_LENGTH = 32,
	};

	class CClient
	{
	public:
		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Score;
		bool m_Player;
		bool m_Afk;
		int m_FriendState;
		// skin info 0.6
		char m_aSkin[MAX_SKIN_LENGTH];
		bool m_CustomSkinColors;
		int m_CustomSkinColorBody;
		int m_CustomSkinColorFeet;
		// skin info 0.7
		char m_aaSkin7[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH];
		bool m_aUseCustomSkinColor7[protocol7::NUM_SKINPARTS];
		int m_aCustomSkinColor7[protocol7::NUM_SKINPARTS];
	};

	int m_ServerIndex;

	int m_Type;
	uint64_t m_ReceivedPackets;

	int m_NumAddresses;
	NETADDR m_aAddresses[MAX_SERVER_ADDRESSES];

	int m_QuickSearchHit;
	int m_FriendState;
	int m_FriendNum;

	int m_MaxClients;
	int m_NumClients;
	int m_MaxPlayers;
	int m_NumPlayers;
	int m_Flags;
	EClientScoreKind m_ClientScoreKind;
	TRISTATE m_Favorite;
	TRISTATE m_FavoriteAllowPing;
	char m_aCommunityId[MAX_COMMUNITY_ID_LENGTH];
	char m_aCommunityCountry[MAX_COMMUNITY_COUNTRY_LENGTH];
	char m_aCommunityType[MAX_COMMUNITY_TYPE_LENGTH];
	int m_Location;
	bool m_LatencyIsEstimated;
	int m_Latency; // in ms
	// 延迟测量上限：达到该值表示这次测量触到 1 秒超时，不视为可用延迟。
	// 延迟列对无可用实测值的服务器显示地区估算值，估算不可得时显示占位符
	// （见 CServerInfo::FormatLatency）。
	static constexpr int LATENCY_UNKNOWN = 999;
	ERankState m_HasRank;
	char m_aGameType[16];
	ColorRGBA m_GametypeColor;
	char m_aName[64];
	char m_aMap[MAX_MAP_LENGTH];
	int m_MapCrc;
	int m_MapSize;
	char m_aVersion[32];
	char m_aAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
	std::vector<CClient> m_vClients;
	int m_NumFilteredPlayers;
	bool m_RequiresLogin;
	// 游戏层推送的该服在线梦客户端人数（含 Arg）。
	int m_QmClientCount = 0;

	static int EstimateLatency(int Loc1, int Loc2);
	static bool ParseLocation(int *pResult, const char *pString);
	static ColorRGBA GametypeColor(const char *pGametype);

	// 可用延迟：未测量（-1）或触到 1 秒测量上限（LATENCY_UNKNOWN）的值都不能
	// 当作实测延迟使用；延迟列也因此不按这类值着色。
	static bool IsUsableLatency(int Latency);
	// 延迟列文本：可用值输出毫秒数，否则输出占位符 "--"。
	template<size_t N>
	static void FormatLatency(char (&aBuffer)[N], int Latency)
	{
		if(!IsUsableLatency(Latency))
		{
			str_copy(aBuffer, "--");
			return;
		}
		str_format(aBuffer, N, "%d", Latency);
	}
};

class CCommunityCountryServer
{
	NETADDR m_Address;
	char m_aTypeName[CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];

public:
	CCommunityCountryServer(NETADDR Address, const char *pTypeName) :
		m_Address(Address)
	{
		str_copy(m_aTypeName, pTypeName);
	}

	NETADDR Address() const { return m_Address; }
	const char *TypeName() const { return m_aTypeName; }
};

class CCommunityCountry
{
	friend class CServerBrowser;

	char m_aName[CServerInfo::MAX_COMMUNITY_COUNTRY_LENGTH];
	int m_FlagId;
	std::vector<CCommunityCountryServer> m_vServers;

public:
	CCommunityCountry(const char *pName, int FlagId) :
		m_FlagId(FlagId)
	{
		str_copy(m_aName, pName);
	}

	const char *Name() const { return m_aName; }
	int FlagId() const { return m_FlagId; }
	const std::vector<CCommunityCountryServer> &Servers() const { return m_vServers; }
};

class CCommunityType
{
	char m_aName[CServerInfo::MAX_COMMUNITY_TYPE_LENGTH];

public:
	CCommunityType(const char *pName)
	{
		str_copy(m_aName, pName);
	}

	const char *Name() const { return m_aName; }
};

class CCommunityMap
{
	char m_aName[MAX_MAP_LENGTH];

public:
	CCommunityMap(const char *pName)
	{
		str_copy(m_aName, pName);
	}

	const char *Name() const { return m_aName; }

	bool operator==(const CCommunityMap &Other) const
	{
		return str_comp(Name(), Other.Name()) == 0;
	}

	bool operator!=(const CCommunityMap &Other) const
	{
		return !(*this == Other);
	}

	struct SHash
	{
		size_t operator()(const CCommunityMap &Map) const
		{
			return str_quickhash(Map.Name());
		}
	};
};

class CCommunity // NOLINT(bugprone-exception-escape)
{
	friend class CServerBrowser;

	char m_aId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	char m_aName[64];
	std::optional<SHA256_DIGEST> m_IconSha256;
	char m_aIconUrl[128];
	std::vector<CCommunityCountry> m_vCountries;
	std::vector<CCommunityType> m_vTypes;
	int m_NumPlayers = 0;
	bool m_HasFinishes = false;
	std::unordered_set<CCommunityMap, CCommunityMap::SHash> m_FinishedMaps;
	void Swap(CCommunity &Other) noexcept;

public:
	CCommunity(const char *pId, const char *pName, std::optional<SHA256_DIGEST> IconSha256, const char *pIconUrl) :
		m_IconSha256(IconSha256)
	{
		str_copy(m_aId, pId);
		str_copy(m_aName, pName);
		str_copy(m_aIconUrl, pIconUrl);
	}

	const char *Id() const { return m_aId; }
	const char *Name() const { return m_aName; }
	const char *IconUrl() const { return m_aIconUrl; }
	const std::optional<SHA256_DIGEST> &IconSha256() const { return m_IconSha256; }
	const std::vector<CCommunityCountry> &Countries() const { return m_vCountries; }
	const std::vector<CCommunityType> &Types() const { return m_vTypes; }
	bool HasCountry(const char *pCountryName) const;
	bool HasType(const char *pTypeName) const;
	bool HasRanks() const { return m_HasFinishes; }
	int NumFinishedMaps() const { return static_cast<int>(m_FinishedMaps.size()); }
	CServerInfo::ERankState HasRank(const char *pMap) const;
	int NumPlayers() const { return m_NumPlayers; }
};

class IFilterList
{
public:
	virtual ~IFilterList() = default;
	virtual void Add(const char *pElement) = 0;
	virtual void Remove(const char *pElement) = 0;
	virtual void Clear() = 0;
	virtual bool Empty() const = 0;
	virtual bool Filtered(const char *pElement) const = 0;
};

class ICommunityCache
{
public:
	virtual ~ICommunityCache() = default;
	virtual void Update(bool Force) = 0;
	virtual const std::vector<const CCommunity *> &SelectedCommunities() const = 0;
	virtual const std::vector<const CCommunityCountry *> &SelectableCountries() const = 0;
	virtual const std::vector<const CCommunityType *> &SelectableTypes() const = 0;
	virtual bool AnyRanksAvailable() const = 0;
	virtual bool CountriesTypesFilterAvailable() const = 0;
	virtual const char *CountryTypeFilterKey() const = 0;
};

class IServerBrowser : public IInterface
{
	MACRO_INTERFACE("serverbrowser")
public:
	/* Constants: Server Browser Sorting
		SORT_NAME - Sort by name.
		SORT_PING - Sort by ping.
		SORT_MAP - Sort by map.
		SORT_GAMETYPE - Sort by game type. DM, TDM etc.
		SORT_NUMPLAYERS - Sort after how many players there are on the server.
		SORT_NUMFRIENDS - Sort after how many friends there are on the server.
		SORT_FAVORITES - Sort by favorite status, number of players and then ping.
		SORT_QM_CLIENTS - Sort by the number of Qm clients online on the server.
		SORT_DIFFICULTY - Sort by map difficulty stars (QmClient map difficulty catalog, unknown maps last).
	*/
	enum
	{
		SORT_NAME = 0,
		SORT_PING,
		SORT_MAP,
		SORT_GAMETYPE,
		SORT_NUMPLAYERS,
		SORT_NUMFRIENDS,
		SORT_FAVORITES,
		SORT_QM_CLIENTS,
		SORT_DIFFICULTY,
	};

	enum
	{
		QUICK_SERVERNAME = 1,
		QUICK_PLAYER = 2,
		QUICK_MAPNAME = 4,
	};

	enum
	{
		TYPE_INTERNET = 0,
		TYPE_LAN,
		TYPE_FAVORITES,
		TYPE_FAVORITE_COMMUNITY_1,
		TYPE_FAVORITE_COMMUNITY_2,
		TYPE_FAVORITE_COMMUNITY_3,
		TYPE_FAVORITE_COMMUNITY_4,
		TYPE_FAVORITE_COMMUNITY_5,
		NUM_TYPES,
	};

	enum
	{
		LAN_PORT_BEGIN = 8303,
		LAN_PORT_END = 8310,
	};

	class CServerEntry
	{
	public:
		int64_t m_RequestTime;
		bool m_RequestIgnoreInfo;
		int m_GotInfo;
		// QmClient: 该条目的补测请求已入队且尚未收到回复。
		bool m_PingProbePending = false;
		// QmClient: 该条目的补测请求被释放且没等到回复。本次列表生命周期内不再
		// 自动重试，把探测机会让给同 IP 的其他端口实例；列表刷新后条目重建，
		// 允许重新尝试。
		bool m_PingProbeFailed = false;
		bool m_FriendStateValid = false;
		uint64_t m_FriendStateRevision = 0;
		bool m_FriendStateIgnoreClan = false;
		CServerInfo m_Info;

		CServerEntry *m_pPrevReq; // request list
		CServerEntry *m_pNextReq;
	};

	static constexpr const char *COMMUNITY_DDNET = "ddnet";
	static constexpr const char *COMMUNITY_NONE = "none";

	static constexpr const char *COMMUNITY_COUNTRY_NONE = "none";
	static constexpr const char *COMMUNITY_TYPE_NONE = "None";
	/**
	 * Special community value for country/type filters that
	 * affect all communities.
	 */
	static constexpr const char *COMMUNITY_ALL = "all";

	static constexpr const char *SEARCH_EXCLUDE_TOKEN = ";";

	virtual void Refresh(int Type, bool Force = false) = 0;
	virtual void RefreshHttpServerList() = 0;
	virtual bool IsRefreshing() const = 0;
	virtual bool IsGettingServerlist() const = 0;
	virtual bool IsServerlistError() const = 0;
	virtual bool IsServerlistStale() const { return false; }
	virtual int LoadingProgression() const = 0;
	virtual uint64_t FriendListRevision() const = 0;
	// QmClient: 当前客户端自身的位置分类（br_location 或 geoip 推断）。
	// 与服务器 m_Location 同一套枚举；用于「同地区服务器延迟列显示绿色」。
	virtual int GetCurrentClientLocation() const = 0;
	// QmClient: 给单个服务器补测一次延迟（GETINFO 只测 ping，不回写服务器信息）。
	// 已有可用实测值、已在请求队列中、本次列表生命周期内补测失败过，或同一 IP
	// 刚探测过时忽略。
	virtual void RequestServerPing(const NETADDR &Addr) = 0;
	// QmClient: 国服判定。地区为中国、社区 id 含 "chn" 或服务器名含 "CHN" 时
	// 成立；可见行补测和名称列短名共用这一口径。
	virtual bool IsChineseServer(const CServerInfo &Info) const = 0;

	virtual int NumServers() const = 0;
	virtual const CServerInfo *Get(int Index) const = 0;
	virtual int NumHttpServers() const = 0;
	virtual const CServerInfo *HttpGet(int Index) const = 0;
	// 原始 HTTP 列表的代次，不受可见列表排序或延迟变化影响。
	virtual uint64_t HttpListRevision() const = 0;

	virtual int Players(const CServerInfo &Item) const = 0;
	virtual int Max(const CServerInfo &Item) const = 0;

	virtual int NumSortedServers() const = 0;
	virtual int NumSortedPlayers() const = 0;
	virtual const CServerInfo *SortedGet(int Index) const = 0;
	// 中心服下发的在线分布（"ip:port" → 人数），游戏层在分布更新时推送。
	// 完整准备失败时保留原计数和排序状态，并返回false。
	virtual bool SetQmClientServerCounts(const std::unordered_map<std::string, int> &Counts) = 0;

	virtual const std::vector<CCommunity> &Communities() const = 0;
	virtual const CCommunity *Community(const char *pCommunityId) const = 0;
	virtual std::vector<const CCommunity *> SelectedCommunities() const = 0;
	virtual std::vector<const CCommunity *> FavoriteCommunities() const = 0;
	virtual std::vector<const CCommunity *> CurrentCommunities() const = 0;
	virtual unsigned CurrentCommunitiesHash() const = 0;

	virtual bool DDNetInfoAvailable() const = 0;
	virtual std::optional<SHA256_DIGEST> DDNetInfoSha256() const = 0;

	virtual ICommunityCache &CommunityCache() = 0;
	virtual const ICommunityCache &CommunityCache() const = 0;
	virtual IFilterList &FavoriteCommunitiesFilter() = 0;
	virtual IFilterList &CommunitiesFilter() = 0;
	virtual IFilterList &CountriesFilter() = 0;
	virtual IFilterList &TypesFilter() = 0;
	virtual const IFilterList &FavoriteCommunitiesFilter() const = 0;
	virtual const IFilterList &CommunitiesFilter() const = 0;
	virtual const IFilterList &CountriesFilter() const = 0;
	virtual const IFilterList &TypesFilter() const = 0;
	virtual void CleanFilters() = 0;

	virtual CServerEntry *Find(const NETADDR &Addr) = 0;
	virtual int GetCurrentType() = 0;
	virtual const char *GetTutorialServer() = 0;
};

#endif
