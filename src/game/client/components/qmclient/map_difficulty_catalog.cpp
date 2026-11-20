#include "map_difficulty_catalog.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/serverbrowser.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/qm_storage_layout.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	constexpr const char *MAP_DIFFICULTY_SQL = "qmclient/map_difficulty.sql";
	constexpr size_t MAX_MAP_DIFFICULTY_SQL_BYTES = 2 * 1024 * 1024;
	constexpr const char *AXIOM_CACHE_PATH = qm_storage::MAP_DIFFICULTY_CACHE_FILE;
	constexpr size_t MAX_AXIOM_CACHE_BYTES = 2 * 1024 * 1024;
	constexpr size_t MAX_AXIOM_ENTRIES = 4096;

	// 玩法只用于隔离同名地图，星级上限由 Axiom 公告自身决定。
	// Axiom 已把 Gores 的星级从 3 格改为 5 格，不能再用玩法硬编码上限。
	enum class EAxiomMode
	{
		NONE = -1,
		GORES = 0,
		AXRACE = 1,
	};

	EAxiomMode AxiomMode(const CServerInfo &Server)
	{
		if(str_comp_nocase(Server.m_aCommunityId, "axiom") != 0)
			return EAxiomMode::NONE;
		if(str_comp_nocase(Server.m_aGameType, "Gores") == 0)
			return EAxiomMode::GORES;
		if(str_comp_nocase(Server.m_aGameType, "AXRace") == 0)
			return EAxiomMode::AXRACE;
		const bool IsDdrace = str_comp_nocase(Server.m_aGameType, "DDNet") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRaceNetwork") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRace") == 0;
		if(IsDdrace && str_comp_nocase(Server.m_aCommunityType, "Gores") == 0)
			return EAxiomMode::GORES;
		if(IsDdrace && str_comp_nocase(Server.m_aCommunityType, "Other") == 0)
			return EAxiomMode::AXRACE;
		return EAxiomMode::NONE;
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

	bool IsStarGlyph(int Codepoint)
	{
		return Codepoint == 0x2605 || Codepoint == 0x2606 || Codepoint == 0x2730;
	}

	// 只接受 3 格或 5 格星制；实心星必须全部排在空心星之前。
	bool ParseStars(std::string_view Text, int &Stars, int &MaxStars)
	{
		const char *pCursor = Text.data();
		const char *pEnd = Text.data() + Text.size();
		int Total = 0;
		int Filled = 0;
		bool EmptySeen = false;
		while(pCursor < pEnd)
		{
			const char *pBefore = pCursor;
			const int Codepoint = str_utf8_decode(&pCursor);
			if(pCursor <= pBefore)
				return false;
			if(Codepoint == 0x2605)
			{
				if(EmptySeen)
					return false;
				++Filled;
			}
			else if(Codepoint == 0x2606 || Codepoint == 0x2730)
			{
				EmptySeen = true;
			}
			else
			{
				return false;
			}
			if(++Total > 5)
				return false;
		}
		if(Total != 3 && Total != 5)
			return false;
		Stars = Filled;
		MaxStars = Total;
		return true;
	}

	bool IsBlankCodepoint(int Codepoint)
	{
		return Codepoint == ' ' || Codepoint == '\t' || Codepoint == '\r' || Codepoint == 0x00a0 || Codepoint == 0x3000;
	}

	// 新公告写成「难度：疯狂  ★★★✰✰」，类别与星级之间可能有多个空格。
	bool ParseCombinedDifficulty(std::string_view Text, std::string &Category, int &Stars, int &MaxStars)
	{
		size_t CategoryStart = std::string_view::npos;
		size_t CategoryEnd = 0;
		size_t StarOffset = std::string_view::npos;
		for(size_t Offset = 0; Offset < Text.size();)
		{
			const char *pCursor = Text.data() + Offset;
			const char *pBefore = pCursor;
			const int Codepoint = str_utf8_decode(&pCursor);
			if(pCursor <= pBefore)
				return false;
			if(IsStarGlyph(Codepoint))
			{
				StarOffset = Offset;
				break;
			}
			if(!IsBlankCodepoint(Codepoint))
			{
				if(CategoryStart == std::string_view::npos)
					CategoryStart = Offset;
				CategoryEnd = (size_t)(pCursor - Text.data());
			}
			Offset = (size_t)(pCursor - Text.data());
		}
		if(StarOffset == std::string_view::npos || CategoryStart == std::string_view::npos)
			return false;
		std::string ParsedCategory(Text.substr(CategoryStart, CategoryEnd - CategoryStart));
		if(!ValidField(ParsedCategory, 64))
			return false;
		if(!ParseStars(Text.substr(StarOffset), Stars, MaxStars))
			return false;
		Category = std::move(ParsedCategory);
		return true;
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

void CQmMapDifficultyCatalog::SEntry::CellLabel(const char *pCategoryText, char *pOut, size_t OutSize) const
{
	if(pOut == nullptr || OutSize == 0)
		return;
	pOut[0] = '\0';
	const char *pStars = StarLabel();
	if(pStars[0] == '\0')
		return;
	if(pCategoryText == nullptr || pCategoryText[0] == '\0')
	{
		str_copy(pOut, pStars, (int)OutSize);
		return;
	}
	str_format(pOut, (int)OutSize, "%s %s", pCategoryText, pStars);
}

bool CQmMapDifficultyCatalog::RecordAxiomMotd(const CServerInfo &Server, const char *pMotd)
{
	const EAxiomMode Mode = AxiomMode(Server);
	if(Mode == EAxiomMode::NONE || pMotd == nullptr || str_length(pMotd) > 4096 || !str_utf8_check(pMotd))
		return false;
	const std::string_view Motd(pMotd);
	// 旧公告标题为「当前地图信息」，新公告标题为「当前地图」，共同前缀足以覆盖两者。
	if(Motd.find("Axiom") == std::string_view::npos || Motd.find("当前地图") == std::string_view::npos)
		return false;

	SEntry Entry;
	std::string Difficulty;
	std::string Category;
	std::string Stars;
	if(!MotdField(Motd, "│ 地图：", Entry.m_MapName) || !ValidField(Entry.m_MapName, MAX_MAP_LENGTH - 1))
		return false;

	// 新格式把类别和星级合并到「难度」一行，旧格式分为「类型」和「星级」两行。
	// 一份公告里只能出现其中一种，混用或缺失都视为不可信。
	const bool HasCombined = MotdField(Motd, "│ 难度：", Difficulty);
	const bool HasCategory = MotdField(Motd, "│ 类型：", Category);
	const bool HasStars = MotdField(Motd, "│ 星级：", Stars);
	if(HasCombined == (HasCategory || HasStars))
		return false;
	if(HasCombined)
	{
		if(!ParseCombinedDifficulty(Difficulty, Entry.m_Category, Entry.m_Stars, Entry.m_MaxStars))
			return false;
	}
	else
	{
		if(!HasCategory || !HasStars || !ValidField(Category, 64) ||
			!ParseStars(Stars, Entry.m_Stars, Entry.m_MaxStars))
			return false;
		Entry.m_Category = std::move(Category);
	}

	// 首次连接或地图仍在下载时，当前地图名可能为空或仍是上一张图；
	// 此时信任已验证社区的公告，已知地图名时仍要求与公告一致。
	if(Server.m_aMap[0] != '\0' && Entry.m_MapName != Server.m_aMap)
		return false;

	const auto Key = std::make_pair((int)Mode, Entry.m_MapName);
	const auto It = m_AxiomEntries.find(Key);
	if(It != m_AxiomEntries.end() && It->second.m_Stars == Entry.m_Stars &&
		It->second.m_MaxStars == Entry.m_MaxStars && It->second.m_Category == Entry.m_Category)
		return false;
	if(It == m_AxiomEntries.end() && m_AxiomEntries.size() >= MAX_AXIOM_ENTRIES)
		return false;
	m_AxiomEntries.insert_or_assign(Key, std::move(Entry));
	m_AxiomCacheDirty = true;
	return true;
}

bool CQmMapDifficultyCatalog::LoadAxiomCacheJson(const char *pJson, size_t Length)
{
	if(pJson == nullptr || Length == 0 || Length > MAX_AXIOM_CACHE_BYTES)
		return false;
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(pJson, Length), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return false;
	const json_value *pVersion = json_object_get(pRoot.get(), "version");
	const json_value *pEntries = json_object_get(pRoot.get(), "entries");
	if(pVersion == nullptr || pEntries == nullptr || pVersion->type != json_integer ||
		(pVersion->u.integer != 1 && pVersion->u.integer != 2) ||
		pEntries->type != json_array || pEntries->u.array.length > MAX_AXIOM_ENTRIES)
		return false;
	// 版本 1 没有 max_stars，按旧规则还原上限；版本 2 保存公告自己的星制。
	const bool Legacy = pVersion->u.integer == 1;
	decltype(m_AxiomEntries) Entries;
	for(unsigned i = 0; i < pEntries->u.array.length; ++i)
	{
		const json_value *pItem = pEntries->u.array.values[i];
		if(pItem == nullptr || pItem->type != json_object)
			return false;
		const json_value *pMap = json_object_get(pItem, "map");
		const json_value *pCategory = json_object_get(pItem, "category");
		const json_value *pMode = json_object_get(pItem, "mode");
		const json_value *pStars = json_object_get(pItem, "stars");
		const json_value *pMaxStars = json_object_get(pItem, "max_stars");
		if(pMap == nullptr || pCategory == nullptr || pMode == nullptr || pStars == nullptr ||
			pMap->type != json_string || pCategory->type != json_string ||
			pMode->type != json_string || pStars->type != json_integer)
			return false;
		if(!Legacy && (pMaxStars == nullptr || pMaxStars->type != json_integer))
			return false;

		const std::string Mode(pMode->u.string.ptr, pMode->u.string.length);
		int ModeId = -1;
		int MaxStars = 0;
		if(Mode == "Gores")
		{
			ModeId = (int)EAxiomMode::GORES;
			MaxStars = 3;
		}
		else if(Mode == "AXRace")
		{
			ModeId = (int)EAxiomMode::AXRACE;
			MaxStars = 5;
		}
		else
		{
			return false;
		}
		if(!Legacy)
		{
			const json_int_t SavedMaxStars = pMaxStars->u.integer;
			if(SavedMaxStars != 3 && SavedMaxStars != 5)
				return false;
			MaxStars = (int)SavedMaxStars;
		}

		SEntry Entry;
		Entry.m_MapName.assign(pMap->u.string.ptr, pMap->u.string.length);
		Entry.m_Category.assign(pCategory->u.string.ptr, pCategory->u.string.length);
		Entry.m_MaxStars = MaxStars;
		if(!ValidField(Entry.m_MapName, MAX_MAP_LENGTH - 1) || !ValidField(Entry.m_Category, 64) ||
			pStars->u.integer < 0 || pStars->u.integer > Entry.m_MaxStars)
			return false;
		Entry.m_Stars = (int)pStars->u.integer;
		const auto Key = std::make_pair(ModeId, Entry.m_MapName);
		if(!Entries.emplace(Key, std::move(Entry)).second)
			return false;
	}
	m_AxiomEntries = std::move(Entries);
	m_AxiomCacheDirty = false;
	return true;
}

std::string CQmMapDifficultyCatalog::SerializeAxiomCache() const
{
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(2);
	Writer.WriteAttribute("entries");
	Writer.BeginArray();
	for(const auto &[Key, Entry] : m_AxiomEntries)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("mode");
		Writer.WriteStrValue(Key.first == (int)EAxiomMode::GORES ? "Gores" : "AXRace");
		Writer.WriteAttribute("map");
		Writer.WriteStrValue(Entry.m_MapName.c_str());
		Writer.WriteAttribute("category");
		Writer.WriteStrValue(Entry.m_Category.c_str());
		Writer.WriteAttribute("stars");
		Writer.WriteIntValue(Entry.m_Stars);
		Writer.WriteAttribute("max_stars");
		Writer.WriteIntValue(Entry.m_MaxStars);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CQmMapDifficultyCatalog::LoadAxiomCache(IStorage *pStorage)
{
	m_AxiomEntries.clear();
	m_AxiomCacheDirty = false;
	if(pStorage == nullptr)
		return;
	IOHANDLE File = pStorage->OpenFile(AXIOM_CACHE_PATH, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return;
	const int64_t Length = io_length(File);
	bool Loaded = false;
	if(Length > 0 && Length <= (int64_t)MAX_AXIOM_CACHE_BYTES)
	{
		std::string Json((size_t)Length, '\0');
		if(io_read(File, Json.data(), (unsigned)Json.size()) == Json.size())
			Loaded = LoadAxiomCacheJson(Json.data(), Json.size());
	}
	io_close(File);
	if(!Loaded)
		log_warn("map_difficulty", "ignoring invalid Axiom map cache");
}

bool CQmMapDifficultyCatalog::SaveAxiomCache(IStorage *pStorage)
{
	if(pStorage == nullptr)
		return false;
	qm_storage::EnsureFolder(pStorage, qm_storage::CACHE_MAPS_DIR);
	char aTempPath[IO_MAX_PATH_LENGTH];
	str_format(aTempPath, sizeof(aTempPath), "%s.%d.tmp", AXIOM_CACHE_PATH, pid());
	IOHANDLE File = pStorage->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const std::string Json = SerializeAxiomCache();
	const bool Written = io_write(File, Json.data(), (unsigned)Json.size()) == Json.size();
	const bool Closed = io_close(File) == 0;
	if(!Written || !Closed || !pStorage->RenameFile(aTempPath, AXIOM_CACHE_PATH, IStorage::TYPE_SAVE, false))
		return false;
	m_AxiomCacheDirty = false;
	return true;
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
	LoadAxiomCache(pStorage);
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
	if(str_comp_nocase(Server.m_aCommunityId, "axiom") == 0)
	{
		const EAxiomMode Mode = AxiomMode(Server);
		if(Mode == EAxiomMode::NONE)
			return nullptr;
		const auto It = m_AxiomEntries.find(std::make_pair((int)Mode, std::string_view(Server.m_aMap)));
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
