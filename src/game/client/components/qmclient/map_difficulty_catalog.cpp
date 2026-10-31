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
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace
{
	constexpr const char *MAP_DIFFICULTY_SQL = "qmclient/map_difficulty.sql";
	constexpr size_t MAX_MAP_DIFFICULTY_SQL_BYTES = 2 * 1024 * 1024;
	class CInputFile
	{
		IOHANDLE m_File;

	public:
		explicit CInputFile(IOHANDLE File) :
			m_File(File) {}
		CInputFile(const CInputFile &) = delete;
		CInputFile &operator=(const CInputFile &) = delete;
		~CInputFile() { Close(); }
		IOHANDLE Get() const { return m_File; }
		bool Close() { return !m_File || io_close(std::exchange(m_File, nullptr)) == 0; }
	};

	void ReportLoadFailure(IConsole *pConsole) noexcept
	{
		try
		{
			log_warn("map_difficulty", "failed to load '%s'", MAP_DIFFICULTY_SQL);
		}
		catch(...)
		{
		}
		if(pConsole != nullptr)
		{
			try
			{
				pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "map_difficulty", "map difficulty SQL is unavailable or invalid");
			}
			catch(...)
			{
			}
		}
	}

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
try
{
	if(pSql == nullptr || Length == 0 || Length > MAX_MAP_DIFFICULTY_SQL_BYTES || std::char_traits<char>::find(pSql, Length, '\0') != nullptr)
		return false;

	sqlite3 *pRawDb = nullptr;
	const int OpenResult = sqlite3_open(":memory:", &pRawDb);
	CSqlite pDb{pRawDb};
	if(OpenResult != SQLITE_OK || !pDb)
		return false;
	const std::string SqlText(pSql, Length);

	char *pError = nullptr;
	const int ExecResult = sqlite3_exec(pDb.get(), SqlText.c_str(), nullptr, nullptr, &pError);
	const std::unique_ptr<char, decltype(&sqlite3_free)> Error(pError, sqlite3_free);
	if(ExecResult != SQLITE_OK)
	{
		if(pError != nullptr)
		{
			log_warn("map_difficulty", "failed to load SQL: %s", pError);
		}
		return false;
	}

	const char *pQuery = "SELECT map_name, category, stars FROM map_difficulty ORDER BY map_name COLLATE NOCASE, category COLLATE NOCASE";
	sqlite3_stmt *pRawStatement = nullptr;
	const int PrepareResult = sqlite3_prepare_v2(pDb.get(), pQuery, -1, &pRawStatement, nullptr);
	CSqliteStmt pStatement{pRawStatement};
	if(PrepareResult != SQLITE_OK || !pStatement)
		return false;

	std::vector<SEntry> vEntries;
	while(true)
	{
		const int StepResult = sqlite3_step(pStatement.get());
		if(StepResult == SQLITE_DONE)
			break;
		if(StepResult != SQLITE_ROW)
			return false;

		const char *pMapName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement.get(), 0));
		if(pMapName == nullptr && sqlite3_errcode(pDb.get()) == SQLITE_NOMEM)
			return false;
		const char *pCategory = reinterpret_cast<const char *>(sqlite3_column_text(pStatement.get(), 1));
		if(pCategory == nullptr && sqlite3_errcode(pDb.get()) == SQLITE_NOMEM)
			return false;
		const int StarType = sqlite3_column_type(pStatement.get(), 2);
		const sqlite3_int64 Stars = StarType == SQLITE_INTEGER ? sqlite3_column_int64(pStatement.get(), 2) : -1;
		if(pMapName == nullptr || pMapName[0] == '\0' || pCategory == nullptr || pCategory[0] == '\0' ||
			Stars < 0 || Stars > 5 ||
			str_length(pMapName) != sqlite3_column_bytes(pStatement.get(), 0) ||
			str_length(pCategory) != sqlite3_column_bytes(pStatement.get(), 1) || !str_utf8_check(pMapName) || !str_utf8_check(pCategory))
			continue;
		if(vEntries.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
			return false;

		SEntry Entry;
		Entry.m_MapName = pMapName;
		Entry.m_Category = pCategory;
		Entry.m_Stars = static_cast<int>(Stars);
		vEntries.push_back(std::move(Entry));
	}

	if(vEntries.empty())
		return false;
	std::sort(vEntries.begin(), vEntries.end(), EntryLess);
	m_vEntries.swap(vEntries);
	return true;
}
catch(...)
{
	return false;
}

bool CQmMapDifficultyCatalog::Load(IStorage *pStorage, IConsole *pConsole)
{
	try
	{
		CInputFile File(pStorage != nullptr ? pStorage->OpenFile(MAP_DIFFICULTY_SQL, IOFLAG_READ, IStorage::TYPE_ALL) : nullptr);
		if(File.Get())
		{
			std::string Sql;
			char aBuffer[4096];
			while(true)
			{
				// 按实际读取计数，达到旧上限后仅探测一个字节，不先整文件分配。
				const unsigned Want = static_cast<unsigned>(std::min(sizeof(aBuffer), MAX_MAP_DIFFICULTY_SQL_BYTES - Sql.size() + 1));
				const unsigned Count = io_read(File.Get(), aBuffer, Want);
				if(Count == 0)
				{
					if(io_error(File.Get()) == 0 && File.Close() && LoadSqlText(Sql.data(), Sql.size()))
						return true;
					break;
				}
				if(Count > MAX_MAP_DIFFICULTY_SQL_BYTES - Sql.size())
					break;
				Sql.append(aBuffer, Count);
			}
		}
	}
	catch(...)
	{
	}
	ReportLoadFailure(pConsole);
	return false;
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
