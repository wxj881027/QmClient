#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DIFFICULTY_CATALOG_H

#include <cstddef>
#include <string>
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
	};

	// 读取随客户端发布的 ddnet-maps 分类 SQL，不访问网络或用户目录。
	bool Load(IStorage *pStorage, IConsole *pConsole);
	// 供行为测试和生成数据校验使用的 SQL 加载入口。
	bool LoadSqlText(const char *pSql, size_t Length);

	const SEntry *Find(const char *pMapName, const char *pCategoryHint = nullptr) const;
	// 服务器列表与星级筛选共用此入口，先限定玩法再查询 DDNet 地图星级。
	const SEntry *FindForServer(const CServerInfo &Server, const char *pCategoryHint = nullptr) const;
	int Size() const { return (int)m_vEntries.size(); }
	bool Empty() const { return m_vEntries.empty(); }

private:
	std::vector<SEntry> m_vEntries;
};

#endif
