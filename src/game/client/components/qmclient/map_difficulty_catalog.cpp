#include "map_difficulty_catalog.h"

#include "axiom_scores_data.h"
#include "bounded_file_reader.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>
#include <engine/shared/qm_storage_layout.h>
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
	constexpr unsigned MAX_DDNET_CACHE_BYTES = 2 * 1024 * 1024;
	constexpr size_t MAX_DDNET_ENTRIES = 4096;
	constexpr const char *AXIOM_CACHE_PATH = qm_storage::MAP_DIFFICULTY_CACHE_FILE;
	// 统一存档布局前的路径；迁移遇到内容冲突时会同时保留两份，这里负责并入缺失条目。
	constexpr const char *LEGACY_AXIOM_CACHE_PATH = "qmclient/axiom_map_difficulty.json";
	constexpr unsigned MAX_AXIOM_CACHE_BYTES = 2 * 1024 * 1024;
	constexpr size_t MAX_AXIOM_ENTRIES = 4096;

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

	struct SCacheCandidate
	{
		IStorage &m_Storage;
		const char *m_pPath;
		bool m_Owned = false;

		~SCacheCandidate()
		{
			if(!m_Owned)
				return;
			try
			{
				m_Storage.RemoveFile(m_pPath, IStorage::TYPE_SAVE);
			}
			catch(...)
			{
				// 清理失败不能让原保存失败变成进程退出。
			}
		}
	};

	bool SaveCacheFile(IStorage &Storage, const char *pPath, const std::string &Json)
	{
		qm_storage::EnsureFolder(&Storage, qm_storage::CACHE_MAPS_DIR);
		char aTempPath[IO_MAX_PATH_LENGTH];
		str_format(aTempPath, sizeof(aTempPath), "%s.%d.tmp", pPath, pid());
		SCacheCandidate Candidate{Storage, aTempPath};
		CScopedFile File(Storage.OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE));
		if(!File.Get())
			return false;
		Candidate.m_Owned = true;
		const bool Written = io_write(File.Get(), Json.data(), (unsigned)Json.size()) == Json.size();
		const bool Closed = File.Close();
		if(!Written || !Closed || !Storage.RenameFile(aTempPath, pPath, IStorage::TYPE_SAVE, false))
			return false;
		Candidate.m_Owned = false;
		return true;
	}

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

	bool IsDdnetServer(const CServerInfo &Server)
	{
		const bool IsDdrace = str_comp_nocase(Server.m_aGameType, "DDNet") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRaceNetwork") == 0 ||
				      str_comp_nocase(Server.m_aGameType, "DDRace") == 0;
		return IsDdrace && str_comp_nocase(Server.m_aCommunityId, "axiom") != 0 &&
		       str_comp_nocase(Server.m_aCommunityId, "kog") != 0 && str_find_nocase(Server.m_aCommunityType, "gores") == nullptr;
	}

	bool ParseDdnetEntry(const json_value *pRoot, CQmMapDifficultyCatalog::SEntry &Entry)
	{
		if(!pRoot || pRoot->type != json_object)
			return false;
		const auto *pName = json_object_get(pRoot, "name");
		const auto *pType = json_object_get(pRoot, "type");
		const auto *pDifficulty = json_object_get(pRoot, "difficulty");
		if(pName->type != json_string || pType->type != json_string || pDifficulty->type != json_integer ||
			pDifficulty->u.integer < 0 || pDifficulty->u.integer > 5)
			return false;
		Entry.m_MapName.assign(pName->u.string.ptr, pName->u.string.length);
		Entry.m_Category.assign(pType->u.string.ptr, pType->u.string.length);
		Entry.m_Stars = (int)pDifficulty->u.integer;
		return ValidField(Entry.m_MapName, MAX_MAP_LENGTH - 1) && ValidField(Entry.m_Category, 64);
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

const char *CQmMapDifficultyCatalog::SEntry::AxiomCategoryKey() const
{
	static constexpr const char *s_apKeys[] = {
		Localizable("Main", "Axiom map difficulty"),
		Localizable("Easy", "Axiom map difficulty"),
		Localizable("Hard", "Axiom map difficulty"),
		Localizable("Insane", "Axiom map difficulty"),
		Localizable("Extreme", "Axiom map difficulty"),
		Localizable("Train", "Axiom map difficulty"),
		Localizable("Mod", "Axiom map difficulty"),
		Localizable("Solo", "Axiom map difficulty"),
		Localizable("Brutal", "Axiom map difficulty"),
	};
	for(const char *pKey : s_apKeys)
	{
		if(str_comp_nocase(m_Category.c_str(), pKey) == 0)
			return pKey;
	}
	return nullptr;
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
try
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
	return StoreAxiomEntry((int)Mode, std::move(Entry));
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
}

bool CQmMapDifficultyCatalog::StoreAxiomEntry(int ModeId, SEntry Entry)
try
{
	const auto Key = std::make_pair(ModeId, Entry.m_MapName);
	const auto It = m_AxiomEntries.find(Key);
	if(It != m_AxiomEntries.end() && It->second.m_Stars == Entry.m_Stars &&
		It->second.m_MaxStars == Entry.m_MaxStars && It->second.m_Category == Entry.m_Category)
		return false;
	if(It == m_AxiomEntries.end() && m_AxiomEntries.size() >= MAX_AXIOM_ENTRIES)
		return false;
	m_AxiomEntries.insert_or_assign(Key, std::move(Entry));
	m_AxiomCacheDirty = true;
	++m_Revision;
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

CQmMapDifficultyCatalog::CQmMapDifficultyCatalog(TRequestFactory CreateRequest) :
	m_CreateRequest(CreateRequest ? CreateRequest : HttpGet)
{
}

bool CQmMapDifficultyCatalog::SDdnetKeyLess::operator()(std::string_view A, std::string_view B) const
{
	// 不能把 data() 直接喂给 str_comp_nocase_num：string_view 不保证以 '\0' 结尾，
	// 这里按字节逐位比较（与 str_comp_nocase_num 的 ASCII 大小写规则一致）。
	const size_t Length = std::min(A.size(), B.size());
	for(size_t Index = 0; Index < Length; ++Index)
	{
		const int CharA = str_utf8_tolower_codepoint((unsigned char)A[Index]);
		const int CharB = str_utf8_tolower_codepoint((unsigned char)B[Index]);
		if(CharA != CharB)
			return CharA < CharB;
	}
	return A.size() < B.size();
}

void CQmMapDifficultyCatalog::RequestDdnetDifficulty(const CServerInfo &Server, IHttp &Http, int64_t Now)
{
	if(Now < m_NextDdnetRequestTime || m_DdnetRequests.size() >= 2 || m_DdnetEntries.size() >= MAX_DDNET_ENTRIES ||
		!IsDdnetServer(Server) || Server.m_aMap[0] == '\0' || Find(Server.m_aMap) || m_DdnetRequests.contains(Server.m_aMap))
		return;
	auto Retry = m_DdnetRetryAfter.find(Server.m_aMap);
	if(Retry != m_DdnetRetryAfter.end() && Now < Retry->second)
		return;
	// 多个房间共用地图查询；每秒最多开始一次，失败准备也占用本次额度。
	m_NextDdnetRequestTime = Now + time_freq();
	std::shared_ptr<IHttpRequest> pRequest;
	try
	{
		if(Retry == m_DdnetRetryAfter.end())
		{
			if(m_DdnetRetryAfter.size() >= MAX_DDNET_ENTRIES)
				m_DdnetRetryAfter.erase(m_DdnetRetryAfter.begin());
			Retry = m_DdnetRetryAfter.try_emplace(Server.m_aMap, 0).first;
		}
		auto RetryNode = m_DdnetRetryAfter.extract(Retry);
		if(!ValidField(RetryNode.key(), MAX_MAP_LENGTH - 1))
			return;
		char aEscaped[MAX_MAP_LENGTH * 3];
		EscapeUrl(aEscaped, RetryNode.key().c_str());
		char aUrl[1024];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
		pRequest = m_CreateRequest(aUrl);
		if(!pRequest)
			return;
		pRequest->Timeout({3000, 8000, 0, 0});
		pRequest->MaxResponseSize(2 * 1024 * 1024);
		pRequest->LogProgress(HTTPLOG::NONE);
		const auto [Pending, Inserted] = m_DdnetRequests.try_emplace(RetryNode.key());
		if(!Inserted)
			return;
		Pending->second = {pRequest, std::move(RetryNode)};
		Http.Run(pRequest);
		return;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}
	// 只移除本次提交，保留同步回调期间登记的其他请求。
	if(pRequest)
	{
		pRequest->Abort();
		const auto Pending = std::find_if(m_DdnetRequests.begin(), m_DdnetRequests.end(), [&](const auto &Entry) {
			return Entry.second.m_pRequest == pRequest;
		});
		if(Pending != m_DdnetRequests.end())
			m_DdnetRequests.erase(Pending);
	}
}

bool CQmMapDifficultyCatalog::RecordDdnetResponse(const std::string &Map, const IHttpRequest &Request)
try
{
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(Request.ResultJson(), json_value_free);
	SEntry Entry;
	if(!ParseDdnetEntry(pRoot.get(), Entry) || str_comp_nocase(Entry.m_MapName.c_str(), Map.c_str()) != 0)
		return false;
	if(m_DdnetEntries.size() >= MAX_DDNET_ENTRIES)
		return false;
	m_DdnetEntries.insert_or_assign(Map, std::move(Entry));
	m_DdnetCacheDirty = true;
	++m_Revision;
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

bool CQmMapDifficultyCatalog::PollDdnetRequests(int64_t Now)
{
	bool Changed = false;
	for(auto It = m_DdnetRequests.begin(); It != m_DdnetRequests.end();)
	{
		if(!It->second.m_pRequest->Done())
		{
			++It;
			continue;
		}
		auto Completed = m_DdnetRequests.extract(It++);
		const auto &Request = *Completed.mapped().m_pRequest;
		const int Code = Request.CompletedStatusCode();
		if(Code == 429)
			m_NextDdnetRequestTime = std::max(m_NextDdnetRequestTime, Now + 60 * time_freq());
		if(Code == 200 && Request.State() == EHttpState::DONE && RecordDdnetResponse(Completed.key(), Request))
			Changed = true;
		else
		{
			if(m_DdnetRetryAfter.size() >= MAX_DDNET_ENTRIES)
				m_DdnetRetryAfter.erase(m_DdnetRetryAfter.begin());
			auto &RetryNode = Completed.mapped().m_RetryNode;
			RetryNode.mapped() = Now + (Code == 404 ? 600 : 30) * time_freq();
			m_DdnetRetryAfter.insert(std::move(RetryNode));
		}
	}
	return Changed;
}

void CQmMapDifficultyCatalog::CancelDdnetRequests()
{
	for(const auto &[Key, Request] : m_DdnetRequests)
		Request.m_pRequest->Abort();
	m_DdnetRequests.clear();
}

bool CQmMapDifficultyCatalog::LoadDdnetCacheJson(const char *pJson, size_t Length)
try
{
	if(!pJson || Length == 0 || Length > MAX_DDNET_CACHE_BYTES)
		return false;
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(pJson, Length), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return false;
	const auto *pVersion = json_object_get(pRoot.get(), "version");
	const auto *pEntries = json_object_get(pRoot.get(), "entries");
	if(pVersion->type != json_integer || pVersion->u.integer != 1 || pEntries->type != json_array ||
		pEntries->u.array.length > MAX_DDNET_ENTRIES)
		return false;
	TDdnetEntryMap Entries;
	for(unsigned i = 0; i < pEntries->u.array.length; ++i)
	{
		SEntry Entry;
		if(!ParseDdnetEntry(pEntries->u.array.values[i], Entry))
			return false;
		const std::string Key = Entry.m_MapName;
		if(!Entries.emplace(Key, std::move(Entry)).second)
			return false;
	}
	m_DdnetEntries.swap(Entries);
	m_DdnetCacheDirty = false;
	++m_Revision;
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

std::string CQmMapDifficultyCatalog::SerializeDdnetCache() const
{
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("entries");
	Writer.BeginArray();
	for(const auto &[Key, Entry] : m_DdnetEntries)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("name");
		Writer.WriteStrValue(Entry.m_MapName.c_str());
		Writer.WriteAttribute("type");
		Writer.WriteStrValue(Entry.m_Category.c_str());
		Writer.WriteAttribute("difficulty");
		Writer.WriteIntValue(Entry.m_Stars);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CQmMapDifficultyCatalog::LoadDdnetCache(IStorage *pStorage)
try
{
	CQmCheckedFileData Cache;
	if(pStorage && Cache.Read(*pStorage, qm_storage::DDNET_MAP_DIFFICULTY_CACHE_FILE, IStorage::TYPE_SAVE, MAX_DDNET_CACHE_BYTES) &&
		!LoadDdnetCacheJson(static_cast<const char *>(Cache.Data()), Cache.Size()))
		log_warn("map_difficulty", "ignoring invalid DDNet map cache");
}
catch(const std::bad_alloc &)
{
}
catch(const std::length_error &)
{
}

bool CQmMapDifficultyCatalog::SaveDdnetCache(IStorage *pStorage)
try
{
	if(!pStorage)
		return false;
	const std::string Json = SerializeDdnetCache();
	if(Json.size() > MAX_DDNET_CACHE_BYTES || !SaveCacheFile(*pStorage, qm_storage::DDNET_MAP_DIFFICULTY_CACHE_FILE, Json))
		return false;
	m_DdnetCacheDirty = false;
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

void CQmMapDifficultyCatalog::RequestAxiomDifficulty(const CServerInfo &Server, IHttp &Http, CQmAxiomRequestBudget &Budget, const char *pApiKey, int64_t Now)
{
	if(m_AxiomRequests.size() >= 4 || m_AxiomEntries.size() >= MAX_AXIOM_ENTRIES)
		return;
	const EAxiomMode Mode = AxiomMode(Server);
	if(Mode == EAxiomMode::NONE || !Server.m_aMap[0])
		return;
	const auto Key = std::make_pair((int)Mode, std::string_view(Server.m_aMap));
	if(m_AxiomEntries.contains(Key) || m_AxiomRequests.contains(Key))
		return;
	auto Retry = m_AxiomRetryAfter.find(Key);
	if(Retry != m_AxiomRetryAfter.end() && Now < Retry->second)
		return;
	const bool HasApiKey = pApiKey && pApiKey[0];
	if(!Budget.TryStart(Now, HasApiKey))
		return;
	std::shared_ptr<IHttpRequest> pRequest;
	try
	{
		if(Retry == m_AxiomRetryAfter.end())
			Retry = m_AxiomRetryAfter.try_emplace(TAxiomKey{(int)Mode, Server.m_aMap}, 0).first;
		auto RetryNode = m_AxiomRetryAfter.extract(Retry);
		const EQmAxiomMode ApiMode = Mode == EAxiomMode::GORES ? EQmAxiomMode::GORES : EQmAxiomMode::AXRACE;
		const auto Url = QmBuildAxiomMapInfoUrl(RetryNode.key().second.c_str(), ApiMode);
		pRequest = m_CreateRequest(Url.c_str());
		if(!pRequest)
			return;
		pRequest->Timeout({3000, 8000, 0, 0});
		pRequest->MaxResponseSize(16 * 1024);
		pRequest->LogProgress(HTTPLOG::NONE);
		if(HasApiKey)
			pRequest->Header((std::string("Authorization: Bearer ") + pApiKey).c_str());
		const auto [Pending, Inserted] = m_AxiomRequests.try_emplace(RetryNode.key());
		if(!Inserted)
			return;
		Pending->second = {pRequest, std::move(RetryNode)};
		Http.Run(pRequest);
		return;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}

	// 提交过程中可能同步处理其他请求，只回收这一次登记。
	if(pRequest)
	{
		const auto Pending = std::find_if(m_AxiomRequests.begin(), m_AxiomRequests.end(), [&](const auto &Entry) {
			return Entry.second.m_pRequest == pRequest;
		});
		if(Pending != m_AxiomRequests.end())
			m_AxiomRequests.erase(Pending);
	}
}

bool CQmMapDifficultyCatalog::RecordAxiomResponse(const TAxiomKey &Key, const IHttpRequest &Request)
try
{
	constexpr int MaxStars = 5;
	std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(Request.ResultJson(), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return false;
	const auto *pCode = json_object_get(pRoot.get(), "code");
	const auto *pData = json_object_get(pRoot.get(), "data");
	if(pCode->type != json_integer || pCode->u.integer != 200 || pData->type != json_object)
		return false;
	const auto *pMap = json_object_get(pData, "map");
	const auto *pStars = json_object_get(pData, "stars");
	const auto *pCategory = json_object_get(pData, "difficulty");
	if(pMap->type != json_string || std::string_view(pMap->u.string.ptr, pMap->u.string.length) != Key.second ||
		pStars->type != json_integer || pStars->u.integer < 0 || pStars->u.integer > MaxStars || pCategory->type != json_string)
		return false;
	std::string Category(pCategory->u.string.ptr, pCategory->u.string.length);
	if(!ValidField(Key.second, MAX_MAP_LENGTH - 1) || !ValidField(Category, 64))
		return false;
	return StoreAxiomEntry(Key.first, {Key.second, std::move(Category), (int)pStars->u.integer, MaxStars});
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
}

bool CQmMapDifficultyCatalog::PollAxiomRequests(CQmAxiomRequestBudget &Budget, int64_t Now)
{
	bool Changed = false;
	for(auto It = m_AxiomRequests.begin(); It != m_AxiomRequests.end();)
	{
		if(!It->second.m_pRequest->Done())
		{
			++It;
			continue;
		}
		auto Completed = m_AxiomRequests.extract(It++);
		const auto &Request = *Completed.mapped().m_pRequest;
		const int Code = Request.CompletedStatusCode();
		if(Code == 429)
			Budget.RateLimited(Now);
		if(m_AxiomEntries.contains(Completed.key()))
			continue;
		if(Code == 200 && Request.State() == EHttpState::DONE && RecordAxiomResponse(Completed.key(), Request))
			Changed = true;
		else
		{
			if(m_AxiomRetryAfter.size() >= MAX_AXIOM_ENTRIES)
				m_AxiomRetryAfter.erase(m_AxiomRetryAfter.begin());
			int DelaySeconds = 30;
			if(Code == 404)
				DelaySeconds = 600;
			else if(Code == 429)
				DelaySeconds = 1;
			auto &RetryNode = Completed.mapped().m_RetryNode;
			RetryNode.mapped() = Now + DelaySeconds * time_freq();
			m_AxiomRetryAfter.insert(std::move(RetryNode));
		}
	}
	return Changed;
}

void CQmMapDifficultyCatalog::CancelAxiomRequests()
{
	for(const auto &[Key, Request] : m_AxiomRequests)
		Request.m_pRequest->Abort();
	m_AxiomRequests.clear();
}

bool CQmMapDifficultyCatalog::ParseAxiomCacheJson(const char *pJson, size_t Length, TAxiomEntryMap &Out) const
try
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
	TAxiomEntryMap Entries;
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
	Out = std::move(Entries);
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
	TAxiomEntryMap Entries;
	if(!ParseAxiomCacheJson(pJson, Length, Entries))
		return false;
	m_AxiomEntries = std::move(Entries);
	m_AxiomCacheDirty = false;
	++m_Revision;
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

bool CQmMapDifficultyCatalog::MergeAxiomCacheJson(const char *pJson, size_t Length)
try
{
	TAxiomEntryMap Entries;
	if(!ParseAxiomCacheJson(pJson, Length, Entries))
		return false;
	bool Added = false;
	for(const auto &[Key, Entry] : Entries)
	{
		if(m_AxiomEntries.size() >= MAX_AXIOM_ENTRIES)
			break;
		if(m_AxiomEntries.emplace(Key, Entry).second)
			Added = true;
	}
	if(Added)
	{
		m_AxiomCacheDirty = true;
		++m_Revision;
	}
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
try
{
	if(pStorage == nullptr)
		return;
	const auto ReadCacheText = [&](const char *pPath, std::string &Out) {
		void *pRawData = nullptr;
		unsigned Size = 0;
		const auto Result = pStorage->ReadFileCheckedBounded(pPath, IStorage::TYPE_SAVE, &pRawData, &Size, MAX_AXIOM_CACHE_BYTES);
		const std::unique_ptr<void, decltype(&free)> pData(pRawData, free);
		if(Result != IStorage::EReadFileResult::SUCCESS || pRawData == nullptr)
			return false;
		Out.assign(static_cast<const char *>(pData.get()), Size);
		return true;
	};

	bool AnyFile = false;
	bool Loaded = false;
	std::string Text;
	if(ReadCacheText(AXIOM_CACHE_PATH, Text))
	{
		AnyFile = true;
		Loaded = LoadAxiomCacheJson(Text.data(), Text.size());
	}
	// 旧文件只补缺失条目：新文件里已有的评分优先，避免迁移冲突把旧值盖回来。
	if(ReadCacheText(LEGACY_AXIOM_CACHE_PATH, Text))
	{
		AnyFile = true;
		const bool Merged = MergeAxiomCacheJson(Text.data(), Text.size());
		if(!Merged && !Loaded)
			log_warn("map_difficulty", "ignoring invalid Axiom map cache");
	}
	else if(AnyFile && !Loaded)
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
	if(!SaveCacheFile(*pStorage, AXIOM_CACHE_PATH, Json))
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
	++m_Revision;
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
		LoadDdnetCache(pStorage);
		CQmCheckedFileData Sql;
		if(pStorage && Sql.Read(*pStorage, MAP_DIFFICULTY_SQL, IStorage::TYPE_ALL, MAX_MAP_DIFFICULTY_SQL_BYTES) &&
			LoadSqlText(static_cast<const char *>(Sql.Data()), Sql.Size()))
			return true;
	}
	catch(...)
	{
	}
	ReportLoadFailure(pConsole);
	return !m_DdnetEntries.empty();
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
	// 接口缓存和随包星级共用玩法边界，不能把同名 DDNet 地图评分套给 Gores。
	if(!IsDdnetServer(Server))
		return nullptr;
	return Find(Server.m_aMap, pCategoryHint);
}

const CQmMapDifficultyCatalog::SEntry *CQmMapDifficultyCatalog::Find(const char *pMapName, const char *pCategoryHint) const
{
	if(pMapName == nullptr || pMapName[0] == '\0')
		return nullptr;
	const auto Cached = m_DdnetEntries.find(pMapName);
	if(Cached != m_DdnetEntries.end())
		return &Cached->second;

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
