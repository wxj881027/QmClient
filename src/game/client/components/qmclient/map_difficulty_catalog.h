#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CServerInfo;
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
	};

	// 沿现有存储路径读取 DDNet 分类 SQL 和本地 Axiom 公告缓存；失败保留可用目录。
	bool Load(IStorage *pStorage, IConsole *pConsole);
	// 供行为测试和生成数据校验使用的 SQL 加载入口。
	bool LoadSqlText(const char *pSql, size_t Length);
	// 只记录已确认属于 Axiom 的当前地图公告；资源失败返回 false 并保留旧记录。
	bool RecordAxiomMotd(const CServerInfo &Server, const char *pMotd);
	bool LoadAxiomCacheJson(const char *pJson, size_t Length);
	std::string SerializeAxiomCache() const;
	bool SaveAxiomCache(IStorage *pStorage);
	bool AxiomCacheDirty() const { return m_AxiomCacheDirty; }

	const SEntry *Find(const char *pMapName, const char *pCategoryHint = nullptr) const;
	// 服务器列表与星级筛选共用此入口，按社区和玩法选择对应来源。
	const SEntry *FindForServer(const CServerInfo &Server, const char *pCategoryHint = nullptr) const;
	int Size() const { return (int)m_vEntries.size(); }
	bool Empty() const { return m_vEntries.empty(); }

private:
	void LoadAxiomCache(IStorage *pStorage);
	struct SAxiomKeyLess
	{
		using is_transparent = void;
		bool operator()(std::pair<int, std::string_view> A, std::pair<int, std::string_view> B) const { return A < B; }
	};
	// 星级上限区分 Gores（3）与 AXRace（5），同名地图不跨玩法共用。
	std::map<std::pair<int, std::string>, SEntry, SAxiomKeyLess> m_AxiomEntries;
	bool m_AxiomCacheDirty = false;
	std::vector<SEntry> m_vEntries;
};

#endif
