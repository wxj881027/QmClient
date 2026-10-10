#include "map_difficulty_catalog.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/serverbrowser.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>

namespace
{
	constexpr const char *MAP_DIFFICULTY_SQL = "qmclient/map_difficulty.sql";
	constexpr size_t MAX_MAP_DIFFICULTY_SQL_BYTES = 2 * 1024 * 1024;

	bool EntryLess(const CQmMapDifficultyCatalog::SEntry &A, const CQmMapDifficultyCatalog::SEntry &B)
	{
		const int MapCompare = str_comp_nocase(A.m_MapName.c_str(), B.m_MapName.c_str());
		return MapCompare != 0 ? MapCompare < 0 : str_comp_nocase(A.m_Category.c_str(), B.m_Category.c_str()) < 0;
	}

	bool SameMap(const CQmMapDifficultyCatalog::SEntry &Entry, const char *pMapName)
	{
		return pMapName != nullptr && str_comp_nocase(Entry.m_MapName.c_str(), pMapName) == 0;
	}
}

bool CQmMapDifficultyCatalog::LoadSqlText(const char *pSql, size_t Length)
{
	m_vEntries.clear();
	if(pSql == nullptr || Length == 0 || Length > MAX_MAP_DIFFICULTY_SQL_BYTES)
		return false;

	sqlite3 *pRawDb = nullptr;
	if(sqlite3_open(":memory:", &pRawDb) != SQLITE_OK || pRawDb == nullptr)
	{
		if(pRawDb != nullptr)
			sqlite3_close(pRawDb);
		return false;
	}
	CSqlite pDb{pRawDb};
	const std::string SqlText(pSql, Length);

	char *pError = nullptr;
	const int ExecResult = sqlite3_exec(pDb.get(), SqlText.c_str(), nullptr, nullptr, &pError);
	if(ExecResult != SQLITE_OK)
	{
		if(pError != nullptr)
		{
			log_warn("map_difficulty", "failed to load SQL: %s", pError);
			sqlite3_free(pError);
		}
		return false;
	}

	CSqliteStmt pStatement{nullptr};
	const char *pQuery = "SELECT map_name, category, stars FROM map_difficulty ORDER BY map_name COLLATE NOCASE, category COLLATE NOCASE";
	sqlite3_stmt *pRawStatement = nullptr;
	if(sqlite3_prepare_v2(pDb.get(), pQuery, -1, &pRawStatement, nullptr) != SQLITE_OK || pRawStatement == nullptr)
		return false;
	pStatement.reset(pRawStatement);

	while(true)
	{
		const int StepResult = sqlite3_step(pStatement.get());
		if(StepResult == SQLITE_DONE)
			break;
		if(StepResult != SQLITE_ROW)
		{
			m_vEntries.clear();
			return false;
		}

		const char *pMapName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement.get(), 0));
		const char *pCategory = reinterpret_cast<const char *>(sqlite3_column_text(pStatement.get(), 1));
		const int Stars = sqlite3_column_int(pStatement.get(), 2);
		if(pMapName == nullptr || pMapName[0] == '\0' || pCategory == nullptr || pCategory[0] == '\0' || Stars < 0 || Stars > 5)
			continue;

		SEntry Entry;
		Entry.m_MapName = pMapName;
		Entry.m_Category = pCategory;
		Entry.m_Stars = Stars;
		m_vEntries.push_back(std::move(Entry));
	}

	std::sort(m_vEntries.begin(), m_vEntries.end(), EntryLess);
	return !m_vEntries.empty();
}

bool CQmMapDifficultyCatalog::Load(IStorage *pStorage, IConsole *pConsole)
{
	m_vEntries.clear();
	if(pStorage == nullptr)
		return false;

	void *pData = nullptr;
	unsigned DataSize = 0;
	if(!pStorage->ReadFile(MAP_DIFFICULTY_SQL, IStorage::TYPE_ALL, &pData, &DataSize))
	{
		log_warn("map_difficulty", "failed to read '%s'", MAP_DIFFICULTY_SQL);
		return false;
	}
	const bool Loaded = LoadSqlText(static_cast<const char *>(pData), DataSize);
	free(pData);
	if(!Loaded && pConsole != nullptr)
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "map_difficulty", "map difficulty SQL is unavailable or invalid");
	return Loaded;
}

const CQmMapDifficultyCatalog::SEntry *CQmMapDifficultyCatalog::FindForServer(const CServerInfo &Server, const char *pCategoryHint) const
{
	// 随包星级属于 DDNet 地图，不能仅凭同名地图或服务器名称中的难度词跨玩法套用。
	const bool IsDdrace = str_comp_nocase(Server.m_aGameType, "DDNet") == 0 ||
			     str_comp_nocase(Server.m_aGameType, "DDRaceNetwork") == 0 ||
			     str_comp_nocase(Server.m_aGameType, "DDRace") == 0;
	// 部分 Gores 服使用兼容的游戏类型，社区信息仍应阻止其读取 DDNet 星级。
	if(!IsDdrace || str_comp_nocase(Server.m_aCommunityId, "kog") == 0 || str_find_nocase(Server.m_aCommunityType, "gores") != nullptr)
		return nullptr;
	return Find(Server.m_aMap, pCategoryHint);
}

const CQmMapDifficultyCatalog::SEntry *CQmMapDifficultyCatalog::Find(const char *pMapName, const char *pCategoryHint) const
{
	if(pMapName == nullptr || pMapName[0] == '\0' || m_vEntries.empty())
		return nullptr;

	const auto It = std::lower_bound(m_vEntries.begin(), m_vEntries.end(), pMapName, [](const SEntry &Entry, const char *pName) {
		return str_comp_nocase(Entry.m_MapName.c_str(), pName) < 0;
	});
	if(It == m_vEntries.end() || !SameMap(*It, pMapName))
		return nullptr;

	const SEntry *pFirst = &*It;
	for(auto Match = It; Match != m_vEntries.end() && SameMap(*Match, pMapName); ++Match)
	{
		if(pCategoryHint != nullptr && pCategoryHint[0] != '\0' && str_comp_nocase(Match->m_Category.c_str(), pCategoryHint) == 0)
			return &*Match;
	}
	return pFirst;
}
