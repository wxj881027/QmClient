#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CQmAxiomRequestBudget;
class CServerInfo;
class IHttp;
class IHttpRequest;
class IConsole;
class IStorage;

class CQmMapDifficultyCatalog
{
public:
	struct SEntry
	{
		std::string m_MapName;
		std::string m_Category;
		int m_Stars = -1;
		int m_MaxStars = 5;

		const char *StarLabel() const;
		// 已知英文类别返回本地化 key；中文公告和未知类别保留原文。
		const char *AxiomCategoryKey() const;
		// 难度列单元格文本：类别 + 空格 + 星级；类别为空时只输出星级。
		// pCategoryText 由调用方按社区决定使用原文或本地化名称。
		void CellLabel(const char *pCategoryText, char *pOut, size_t OutSize) const;
	};

	using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *);
	explicit CQmMapDifficultyCatalog(TRequestFactory CreateRequest = nullptr);
	void RequestDdnetDifficulty(const CServerInfo &Server, IHttp &Http, int64_t Now);
	bool PollDdnetRequests(int64_t Now);
	void CancelDdnetRequests();
	void RequestAxiomDifficulty(const CServerInfo &Server, IHttp &Http, CQmAxiomRequestBudget &Budget, const char *pApiKey, int64_t Now);
	bool PollAxiomRequests(CQmAxiomRequestBudget &Budget, int64_t Now);
	void CancelAxiomRequests();

	// 读取随包 DDNet 分类 SQL 和本地接口缓存；失败保留可用目录。
	bool Load(IStorage *pStorage, IConsole *pConsole);
	// 供行为测试和生成数据校验使用的 SQL 加载入口。
	bool LoadSqlText(const char *pSql, size_t Length);
	bool LoadDdnetCacheJson(const char *pJson, size_t Length);
	std::string SerializeDdnetCache() const;
	bool SaveDdnetCache(IStorage *pStorage);
	bool DdnetCacheDirty() const { return m_DdnetCacheDirty; }
	// 只记录已确认属于 Axiom 的当前地图公告；没有变化或资源失败时返回 false。
	// 同时支持旧公告的「类型 + 星级」和当前公告的「难度 + 星级」两种写法。
	bool RecordAxiomMotd(const CServerInfo &Server, const char *pMotd);
	bool LoadAxiomCacheJson(const char *pJson, size_t Length);
	std::string SerializeAxiomCache() const;
	bool SaveAxiomCache(IStorage *pStorage);
	bool AxiomCacheDirty() const { return m_AxiomCacheDirty; }

	const SEntry *Find(const char *pMapName, const char *pCategoryHint = nullptr) const;
	// 服务器列表与星级筛选共用此入口，按社区和玩法选择对应来源。
	const SEntry *FindForServer(const CServerInfo &Server, const char *pCategoryHint = nullptr) const;
	int Size() const { return (int)(m_vEntries.size() + m_DdnetEntries.size()); }
	bool Empty() const { return m_vEntries.empty() && m_DdnetEntries.empty(); }
	// 目录内容单调递增的修订号；后台补查、缓存载入和 SQL 重载成功时自增。
	// 调用方用它缓存派生结果，替代按固定周期全量重扫。
	uint64_t Revision() const { return m_Revision; }

private:
	struct SDdnetKeyLess
	{
		using is_transparent = void;
		bool operator()(std::string_view A, std::string_view B) const;
	};
	using TDdnetRetryMap = std::map<std::string, int64_t, SDdnetKeyLess>;
	using TDdnetEntryMap = std::map<std::string, SEntry, SDdnetKeyLess>;
	struct SDdnetRequest
	{
		std::shared_ptr<IHttpRequest> m_pRequest;
		TDdnetRetryMap::node_type m_RetryNode;
	};
	void LoadDdnetCache(IStorage *pStorage);
	bool RecordDdnetResponse(const std::string &Map, const IHttpRequest &Request);
	std::map<std::string, SDdnetRequest, SDdnetKeyLess> m_DdnetRequests;
	TDdnetRetryMap m_DdnetRetryAfter;
	TDdnetEntryMap m_DdnetEntries;
	int64_t m_NextDdnetRequestTime = 0;
	bool m_DdnetCacheDirty = false;
	void LoadAxiomCache(IStorage *pStorage);
	struct SAxiomKeyLess
	{
		using is_transparent = void;
		bool operator()(std::pair<int, std::string_view> A, std::pair<int, std::string_view> B) const { return A < B; }
	};
	// 键为（玩法，地图名），同名地图不跨玩法共用；星级上限由公告自身决定。
	using TAxiomKey = std::pair<int, std::string>;
	using TAxiomRetryMap = std::map<TAxiomKey, int64_t, SAxiomKeyLess>;
	using TAxiomEntryMap = std::map<TAxiomKey, SEntry, SAxiomKeyLess>;
	struct SAxiomRequest
	{
		std::shared_ptr<IHttpRequest> m_pRequest;
		TAxiomRetryMap::node_type m_RetryNode;
	};
	bool ParseAxiomCacheJson(const char *pJson, size_t Length, TAxiomEntryMap &Out) const;
	// 旧版本把同一份缓存写在 qmclient 根目录；统一布局后只补充新文件缺失的条目。
	bool MergeAxiomCacheJson(const char *pJson, size_t Length);
	bool StoreAxiomEntry(int ModeId, SEntry Entry);
	bool RecordAxiomResponse(const TAxiomKey &Key, const IHttpRequest &Request);
	TRequestFactory m_CreateRequest;
	std::map<TAxiomKey, SAxiomRequest, SAxiomKeyLess> m_AxiomRequests;
	TAxiomRetryMap m_AxiomRetryAfter;
	TAxiomEntryMap m_AxiomEntries;
	bool m_AxiomCacheDirty = false;
	std::vector<SEntry> m_vEntries;
	uint64_t m_Revision = 0;
};

#endif
