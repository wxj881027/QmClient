#include "map_difficulty_catalog.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/serverbrowser.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	constexpr const char *MAP_DIFFICULTY_SQL = "qmclient/map_difficulty.sql";
	constexpr size_t MAX_MAP_DIFFICULTY_SQL_BYTES = 2 * 1024 * 1024;
	class CScopedFile
	{
		IOHANDLE m_File;

	public:
		explicit CScopedFile(IOHANDLE File) :
			m_File(File) {}
		CScopedFile(const CScopedFile &) = delete;
		CScopedFile &operator=(const CScopedFile &) = delete;
		~CScopedFile() { Close(); }
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

	constexpr const char *AXIOM_CACHE_PATH = "qmclient/axiom_map_difficulty.json";
	constexpr size_t MAX_AXIOM_CACHE_BYTES = 2 * 1024 * 1024;
	constexpr size_t MAX_AXIOM_ENTRIES = 4096;

	int AxiomMaxStars(const CServerInfo &Server)
	{
		if(str_comp_nocase(Server.m_aCommunityId, "axiom") != 0)
			return 0;
		if(str_comp_nocase(Server.m_aGameType, "Gores") == 0)
			return 3;
		if(str_comp_nocase(Server.m_aGameType, "AXRace") == 0)
			return 5;
		const bool IsDdrace = str_comp_nocase(Server.m_aGameType, "DDNet") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRaceNetwork") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRace") == 0;
		if(IsDdrace && str_comp_nocase(Server.m_aCommunityType, "Gores") == 0)
			return 3;
		if(IsDdrace && str_comp_nocase(Server.m_aCommunityType, "Other") == 0)
			return 5;
		return 0;
	}

	bool ValidField(const std::string &Value, size_t MaxLength)
	{
		return !Value.empty() && Value.size() <= MaxLength && str_utf8_check(Value.c_str()) &&
		       std::none_of(Value.begin(), Value.end(), [](unsigned char Ch) { return Ch < 0x20 || Ch == 0x7f; });
	}

	bool MotdField(std::string_view Motd, std::string_view Prefix, std::string &Out)
	{
		bool Found = false;
		while(!Motd.empty())
		{
			const size_t End = Motd.find('\n');
			std::string_view Line = Motd.substr(0, End);
			const size_t Start = Line.find_first_not_of(" \t\r");
			if(Start != std::string_view::npos)
				Line.remove_prefix(Start);
			if(Line.substr(0, Prefix.size()) == Prefix)
			{
				if(Found)
					return false;
				Line.remove_prefix(Prefix.size());
				const size_t Last = Line.find_last_not_of(" \t\r");
				Out = Line.substr(0, Last == std::string_view::npos ? 0 : Last + 1);
				Found = true;
			}
			if(End == std::string_view::npos)
				break;
			Motd.remove_prefix(End + 1);
		}
		return Found;
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

const char *CQmMapDifficultyCatalog::SEntry::StarLabel() const
{
	static constexpr const char *s_apFive[] = {"☆☆☆☆☆", "★☆☆☆☆", "★★☆☆☆", "★★★☆☆", "★★★★☆", "★★★★★"};
	static constexpr const char *s_apThree[] = {"☆☆☆", "★☆☆", "★★☆", "★★★"};
	if(m_Stars < 0 || m_Stars > m_MaxStars)
		return "";
	if(m_MaxStars == 3)
		return s_apThree[m_Stars];
	return m_MaxStars == 5 ? s_apFive[m_Stars] : "";
}

bool CQmMapDifficultyCatalog::RecordAxiomMotd(const CServerInfo &Server, const char *pMotd)
try
{
	const int MaxStars = AxiomMaxStars(Server);
	if(MaxStars == 0 || pMotd == nullptr || str_length(pMotd) > 4096 || !str_utf8_check(pMotd))
		return false;
	const std::string_view Motd(pMotd);
	if(Motd.find("Axiom") == std::string_view::npos || Motd.find("当前地图信息") == std::string_view::npos)
		return false;
	SEntry Entry;
	std::string Stars;
	if(!MotdField(Motd, "│ 地图：", Entry.m_MapName) || !MotdField(Motd, "│ 类型：", Entry.m_Category) ||
		!MotdField(Motd, "│ 星级：", Stars) || !ValidField(Entry.m_MapName, MAX_MAP_LENGTH - 1) ||
		!ValidField(Entry.m_Category, 64) || Entry.m_MapName != Server.m_aMap)
		return false;
	Entry.m_MaxStars = MaxStars;
	Entry.m_Stars = 0;
	int Total = 0;
	const char *pStar = Stars.c_str();
	bool EmptyStarSeen = false;
	while(*pStar != '\0')
	{
		const int Codepoint = str_utf8_decode(&pStar);
		if(Codepoint == 0x2605 && !EmptyStarSeen)
			++Entry.m_Stars;
		else if(Codepoint == 0x2606 || Codepoint == 0x2730)
			EmptyStarSeen = true;
		else
			return false;
		if(++Total > MaxStars)
			return false;
	}
	if(Total != MaxStars)
		return false;
	const auto Key = std::make_pair(MaxStars, Entry.m_MapName);
	const auto It = m_AxiomEntries.find(Key);
	if(It != m_AxiomEntries.end() && It->second.m_Stars == Entry.m_Stars && It->second.m_Category == Entry.m_Category)
		return false;
	if(It == m_AxiomEntries.end() && m_AxiomEntries.size() >= MAX_AXIOM_ENTRIES)
		return false;
	m_AxiomEntries.insert_or_assign(Key, std::move(Entry));
	m_AxiomCacheDirty = true;
	return true;
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
}

bool CQmMapDifficultyCatalog::LoadAxiomCacheJson(const char *pJson, size_t Length)
try
{
	if(pJson == nullptr || Length == 0 || Length > MAX_AXIOM_CACHE_BYTES)
		return false;
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(pJson, Length), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return false;
	const json_value *pVersion = json_object_get(pRoot.get(), "version");
	const json_value *pEntries = json_object_get(pRoot.get(), "entries");
	if(pVersion->type != json_integer || pVersion->u.integer != 1 || pEntries->type != json_array || pEntries->u.array.length > MAX_AXIOM_ENTRIES)
		return false;
	decltype(m_AxiomEntries) Entries;
	for(unsigned i = 0; i < pEntries->u.array.length; ++i)
	{
		const json_value *pItem = pEntries->u.array.values[i];
		if(pItem->type != json_object)
			return false;
		const json_value *pMap = json_object_get(pItem, "map");
		const json_value *pCategory = json_object_get(pItem, "category");
		const json_value *pMode = json_object_get(pItem, "mode");
		const json_value *pStars = json_object_get(pItem, "stars");
		if(pMap->type != json_string || pCategory->type != json_string || pMode->type != json_string || pStars->type != json_integer)
			return false;
		const std::string Mode(pMode->u.string.ptr, pMode->u.string.length);
		SEntry Entry;
		Entry.m_MapName.assign(pMap->u.string.ptr, pMap->u.string.length);
		Entry.m_Category.assign(pCategory->u.string.ptr, pCategory->u.string.length);
		Entry.m_MaxStars = Mode == "Gores" ? 3 : Mode == "AXRace" ? 5 :
									    0;
		if(Entry.m_MaxStars == 0 || !ValidField(Entry.m_MapName, MAX_MAP_LENGTH - 1) || !ValidField(Entry.m_Category, 64) ||
			pStars->u.integer < 0 || pStars->u.integer > Entry.m_MaxStars)
			return false;
		Entry.m_Stars = (int)pStars->u.integer;
		const auto Key = std::make_pair(Entry.m_MaxStars, Entry.m_MapName);
		if(!Entries.emplace(Key, std::move(Entry)).second)
			return false;
	}
	m_AxiomEntries = std::move(Entries);
	m_AxiomCacheDirty = false;
	return true;
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
}

std::string CQmMapDifficultyCatalog::SerializeAxiomCache() const
{
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("entries");
	Writer.BeginArray();
	for(const auto &[Key, Entry] : m_AxiomEntries)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("mode");
		Writer.WriteStrValue(Entry.m_MaxStars == 3 ? "Gores" : "AXRace");
		Writer.WriteAttribute("map");
		Writer.WriteStrValue(Entry.m_MapName.c_str());
		Writer.WriteAttribute("category");
		Writer.WriteStrValue(Entry.m_Category.c_str());
		Writer.WriteAttribute("stars");
		Writer.WriteIntValue(Entry.m_Stars);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CQmMapDifficultyCatalog::LoadAxiomCache(IStorage *pStorage)
try
{
	if(pStorage == nullptr)
		return;
	CScopedFile File(pStorage->OpenFile(AXIOM_CACHE_PATH, IOFLAG_READ, IStorage::TYPE_SAVE));
	if(!File.Get())
		return;
	const int64_t Length = io_length(File.Get());
	bool Loaded = false;
	if(Length > 0 && Length <= (int64_t)MAX_AXIOM_CACHE_BYTES)
	{
		std::string Json((size_t)Length, '\0');
		if(io_read(File.Get(), Json.data(), (unsigned)Json.size()) == Json.size() && io_error(File.Get()) == 0 && File.Close())
			Loaded = LoadAxiomCacheJson(Json.data(), Json.size());
	}
	if(!Loaded)
		log_warn("map_difficulty", "ignoring invalid Axiom map cache");
}
catch(const std::bad_alloc &)
{
}
catch(const std::length_error &)
{
}

bool CQmMapDifficultyCatalog::SaveAxiomCache(IStorage *pStorage)
try
{
	if(pStorage == nullptr)
		return false;
	// 候选内容先准备，资源失败不能截断候选文件或留下未关闭的句柄。
	const std::string Json = SerializeAxiomCache();
	pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.%d.tmp", AXIOM_CACHE_PATH, pid());
	CScopedFile File(pStorage->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE));
	if(!File.Get())
		return false;
	const bool Written = io_write(File.Get(), Json.data(), (unsigned)Json.size()) == Json.size();
	const bool Closed = File.Close();
	if(!Written || !Closed || !pStorage->RenameFile(aTempPath, AXIOM_CACHE_PATH, IStorage::TYPE_SAVE, false))
		return false;
	m_AxiomCacheDirty = false;
	return true;
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
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
		LoadAxiomCache(pStorage);
		CScopedFile File(pStorage != nullptr ? pStorage->OpenFile(MAP_DIFFICULTY_SQL, IOFLAG_READ, IStorage::TYPE_ALL) : nullptr);
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
	if(str_comp_nocase(Server.m_aCommunityId, "axiom") == 0)
	{
		const auto It = m_AxiomEntries.find(std::make_pair(AxiomMaxStars(Server), std::string_view(Server.m_aMap)));
		return It == m_AxiomEntries.end() ? nullptr : &It->second;
	}
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
