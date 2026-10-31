#include "vote_map_library.h"

#include "map_vote_difficulty.h"

#include <base/str.h>

#include <engine/map.h>
#include <engine/shared/json.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace
{
	using CJson = std::unique_ptr<json_value, decltype(&json_value_free)>;
	constexpr size_t MAX_CATALOG_BYTES = 8 * 1024 * 1024;

	std::string StringField(const json_value *pObject, const char *pKey, size_t MaxLength)
	{
		const json_value *pValue = json_object_get(pObject, pKey);
		if(pValue->type != json_string || pValue->u.string.length > MaxLength)
			return {};
		// 拒绝内嵌零字节，避免展示名与真正提交的投票名不一致。
		if(str_length(pValue->u.string.ptr) != (int)pValue->u.string.length)
			return {};
		return pValue->u.string.ptr;
	}

	int IntField(const json_value *pObject, const char *pKey, int Maximum)
	{
		const json_value *pValue = json_object_get(pObject, pKey);
		if(pValue->type != json_integer || pValue->u.integer < 0 || pValue->u.integer > Maximum)
			return -1;
		return (int)pValue->u.integer;
	}

	bool Contains(const std::string &Text, const std::string &Needle)
	{
		return str_utf8_find_nocase(Text.c_str(), Needle.c_str()) != nullptr;
	}

	bool NameLess(const QmVoteMaps::SMap &A, const QmVoteMaps::SMap &B)
	{
		return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0;
	}

	struct SCategoryAlias
	{
		const char *m_pKey;
		const char *m_pApiKey;
	};

	// ddnet.org 目录把 DDmaX 子类写成 DDmaX.Easy 等点号形式，界面与侧栏统一用空格写法。
	constexpr SCategoryAlias s_aCategoryAliases[] = {
		{"DDmaX Easy", "DDmaX.Easy"},
		{"DDmaX Next", "DDmaX.Next"},
		{"DDmaX Pro", "DDmaX.Pro"},
		{"DDmaX Nut", "DDmaX.Nut"},
	};

	// 官方 ddnet-maps 的分类选项用小型大写字母排版（如 "☐ DDᴍᴀX.Nᴇxᴛ Mᴀᴘs"），
	// 和界面键名不是同一串字符；匹配前把两边折叠成同一种骨架。
	int SmallCapsAscii(int Codepoint)
	{
		switch(Codepoint)
		{
		case 0x1D00: return 'a'; // ᴀ
		case 0x0299: return 'b'; // ʙ
		case 0x1D04: return 'c'; // ᴄ
		case 0x1D05: return 'd'; // ᴅ
		case 0x1D07: return 'e'; // ᴇ
		case 0xA730: return 'f'; // ꜰ
		case 0x0262: return 'g'; // ɢ
		case 0x029C: return 'h'; // ʜ
		case 0x026A: return 'i'; // ɪ
		case 0x1D0A: return 'j'; // ᴊ
		case 0x1D0B: return 'k'; // ᴋ
		case 0x029F: return 'l'; // ʟ
		case 0x1D0D: return 'm'; // ᴍ
		case 0x0274: return 'n'; // ɴ
		case 0x1D0F: return 'o'; // ᴏ
		case 0x1D18: return 'p'; // ᴘ
		case 0x0280: return 'r'; // ʀ
		case 0xA731: return 's'; // ꜱ
		case 0x1D1B: return 't'; // ᴛ
		case 0x1D1C: return 'u'; // ᴜ
		case 0x1D20: return 'v'; // ᴠ
		case 0x1D21: return 'w'; // ᴡ
		case 0x028F: return 'y'; // ʏ
		case 0x1D22: return 'z'; // ᴢ
		default: return 0;
		}
	}

	bool IsCjkCodepoint(int Codepoint)
	{
		return (Codepoint >= 0x3400 && Codepoint <= 0x4DBF) || (Codepoint >= 0x4E00 && Codepoint <= 0x9FFF) || (Codepoint >= 0xF900 && Codepoint <= 0xFAFF);
	}

	// 只保留字母数字与 CJK：装饰符号、点号、下划线、空白都折成一个分隔空格。
	void AppendNormalizedVoteText(const char *pText, std::string &Out)
	{
		bool PendingSpace = false;
		const char *pCursor = pText;
		while(pCursor != nullptr)
		{
			const int Codepoint = str_utf8_decode(&pCursor);
			if(Codepoint == 0)
				break;
			char aToken[8];
			const char *pToken = nullptr;
			int TokenLength = 1;
			if((Codepoint >= '0' && Codepoint <= '9') || (Codepoint >= 'a' && Codepoint <= 'z'))
			{
				aToken[0] = (char)Codepoint;
				pToken = aToken;
			}
			else if(Codepoint >= 'A' && Codepoint <= 'Z')
			{
				aToken[0] = (char)(Codepoint + ('a' - 'A'));
				pToken = aToken;
			}
			else if(const int SmallCaps = SmallCapsAscii(Codepoint))
			{
				aToken[0] = (char)SmallCaps;
				pToken = aToken;
			}
			else if(IsCjkCodepoint(Codepoint))
			{
				TokenLength = str_utf8_encode(aToken, Codepoint);
				pToken = aToken;
			}
			if(pToken == nullptr)
			{
				PendingSpace = !Out.empty();
				continue;
			}
			if(PendingSpace)
				Out.push_back(' ');
			PendingSpace = false;
			Out.append(pToken, (size_t)TokenLength);
		}
	}

	bool MatchesNeedleByConfusables(const char *pDescription, const char *pNeedle)
	{
		if(str_utf8_find_nocase(pDescription, pNeedle))
			return true;

		char aDescription[256];
		char aNeedle[96];
		str_utf8_tolower(pDescription, aDescription, sizeof(aDescription));
		str_utf8_tolower(pNeedle, aNeedle, sizeof(aNeedle));
		int aText[128];
		int aTextNeedle[64];
		const int TextLength = str_utf8_to_skeleton(aDescription, aText, (int)(sizeof(aText) / sizeof(aText[0])));
		const int NeedleLength = str_utf8_to_skeleton(aNeedle, aTextNeedle, (int)(sizeof(aTextNeedle) / sizeof(aTextNeedle[0])));
		if(NeedleLength <= 0 || NeedleLength > TextLength)
			return false;
		for(int i = 0; i + NeedleLength <= TextLength; ++i)
		{
			bool Match = true;
			for(int j = 0; j < NeedleLength; ++j)
			{
				if(aText[i + j] != aTextNeedle[j])
				{
					Match = false;
					break;
				}
			}
			if(Match)
				return true;
		}
		return false;
	}
}

bool QmVoteMaps::ParseCatalog(const char *pJson, size_t Length, std::vector<SMap> &vMaps)
{
	if(pJson == nullptr || Length == 0 || Length > MAX_CATALOG_BYTES)
		return false;
	CJson pRoot(JsonParse(pJson, Length), json_value_free);
	if(!pRoot || pRoot->type != json_array || pRoot->u.array.length > 20000)
		return false;
	std::vector<SMap> vParsed;
	vParsed.reserve(pRoot->u.array.length);
	for(unsigned i = 0; i < pRoot->u.array.length; ++i)
	{
		const json_value *pEntry = pRoot->u.array.values[i];
		if(pEntry->type != json_object)
			continue;
		SMap Map;
		Map.m_Name = StringField(pEntry, "name", MAX_MAP_LENGTH - 1);
		Map.m_Category = StringField(pEntry, "type", 64);
		Map.m_Mapper = StringField(pEntry, "mapper", 512);
		Map.m_Release = StringField(pEntry, "release", 32);
		Map.m_Stars = IntField(pEntry, "difficulty", 5);
		Map.m_Points = IntField(pEntry, "points", 10000);
		if(Map.m_Name.empty() || Map.m_Category.empty() || Map.m_Stars < 0)
			continue;
		vParsed.push_back(std::move(Map));
	}
	if(vParsed.empty())
		return false;
	std::stable_sort(vParsed.begin(), vParsed.end(), NameLess);
	vParsed.erase(std::unique(vParsed.begin(), vParsed.end(), [](const SMap &A, const SMap &B) {
		return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) == 0;
	}),
		vParsed.end());
	vMaps.swap(vParsed);
	return true;
}

bool QmVoteMaps::ParseDetails(const char *pJson, size_t Length, const SMap &Map, SDetails &Details)
{
	if(pJson == nullptr || Length == 0 || Length > 2 * 1024 * 1024)
		return false;
	CJson pRoot(JsonParse(pJson, Length), json_value_free);
	if(!pRoot || pRoot->type != json_object ||
		StringField(pRoot.get(), "name", MAX_MAP_LENGTH - 1) != Map.m_Name ||
		StringField(pRoot.get(), "type", 64) != Map.m_Category)
		return false;
	SDetails Parsed;
	Parsed.m_Finishers = IntField(pRoot.get(), "finishers", std::numeric_limits<int>::max());
	const json_value *pAverage = json_object_get(pRoot.get(), "average_time");
	if(pAverage->type == json_integer || pAverage->type == json_double)
	{
		const double Value = pAverage->type == json_integer ? (double)pAverage->u.integer : pAverage->u.dbl;
		if(std::isfinite(Value) && Value >= 0.0 && Value <= 365.0 * 86400.0)
			Parsed.m_AverageSeconds = Value;
	}
	Details = Parsed;
	return true;
}

bool QmVoteMaps::ParseVoteMap(const char *pDescription, SMap &Map)
{
	if(pDescription == nullptr)
		return false;
	SMap Parsed;
	Parsed.m_Stars = QmMapVotes::Stars(pDescription);
	if(str_startswith_nocase(pDescription, "Map:"))
	{
		if(!QmMapVotes::MapName(pDescription, Parsed.m_Name))
			return false;
	}
	else
	{
		const char *pBy = str_find_nocase(pDescription, " by ");
		if(pBy == nullptr || pBy == pDescription || (Parsed.m_Stars < 0 && str_find(pDescription, "★") == nullptr && str_find(pDescription, "✰") == nullptr))
			return false;
		Parsed.m_Name.assign(pDescription, pBy - pDescription);
		const char *pEnd = str_find(pBy + 4, " | ");
		if(pEnd != nullptr)
			Parsed.m_Mapper.assign(pBy + 4, pEnd - (pBy + 4));
		if(Parsed.m_Stars < 0)
		{
			Parsed.m_Stars = 0;
			for(const char *pStar = str_find(pBy, "★"); pStar; pStar = str_find(pStar + str_length("★"), "★"))
				++Parsed.m_Stars;
			if(Parsed.m_Stars > 5)
				Parsed.m_Stars = -1;
		}
	}
	Map = std::move(Parsed);
	return true;
}

int QmVoteMaps::FindOption(const CVoteOptionClient *pFirst, const char *pMapName)
{
	if(pMapName == nullptr || pMapName[0] == '\0')
		return -1;
	int Index = 0;
	for(const CVoteOptionClient *pOption = pFirst; pOption; pOption = pOption->m_pNext, ++Index)
	{
		SMap Map;
		if((ParseVoteMap(pOption->m_aDescription, Map) && str_comp_nocase(Map.m_Name.c_str(), pMapName) == 0) ||
			str_comp_nocase(pOption->m_aDescription, pMapName) == 0)
			return Index;
	}
	return -1;
}

QmVoteMaps::EVoteResult QmVoteMaps::ServerVoteResult(int ClientId, int Team, const char *pMessage)
{
	// 0.6 通过服务器聊天报告结果；不能接受玩家伪造的同名消息或按客户端语言比较。
	if(ClientId != -1 || Team != 0 || pMessage == nullptr)
		return EVoteResult::NONE;
	for(const char *pText : {"Vote passed", "Vote passed enforced by authorized player", "Admin forced vote yes", "投票通过", "授权玩家强制通过投票"})
		if(str_comp(pMessage, pText) == 0)
			return EVoteResult::PASS;
	for(const char *pText : {"Vote failed", "Vote failed enforced by authorized player", "Vote failed because of veto. Find an empty server instead", "Admin forced vote no", "投票失败", "投票被否决。请换一个空闲服务器", "授权玩家强制否决投票"})
		if(str_comp(pMessage, pText) == 0)
			return EVoteResult::FAIL;
	for(const char *pText : {"Vote aborted", "Vote canceled", "投票已中止"})
		if(str_comp(pMessage, pText) == 0)
			return EVoteResult::ABORT;
	if(pMessage[0] == '\'' && (str_endswith(pMessage, "' canceled vote") || str_endswith(pMessage, "' 取消了投票")))
		return EVoteResult::ABORT;
	return EVoteResult::NONE;
}

std::vector<QmVoteMaps::SMap> QmVoteMaps::MergeCatalog(const std::vector<SMap> &vCatalog, const CVoteOptionClient *pFirst)
{
	std::vector<SMap> vResult = vCatalog;
	std::set<std::string> Names;
	for(const auto &Map : vCatalog)
	{
		std::string Lower = Map.m_Name;
		for(char &Character : Lower)
			Character = str_uppercase(Character);
		Names.insert(Lower);
	}
	for(const CVoteOptionClient *pOption = pFirst; pOption; pOption = pOption->m_pNext)
	{
		SMap Map;
		if(!ParseVoteMap(pOption->m_aDescription, Map))
			continue;
		std::string Lower = Map.m_Name;
		for(char &Character : Lower)
			Character = str_uppercase(Character);
		if(Names.insert(Lower).second)
			vResult.push_back(std::move(Map));
	}
	std::stable_sort(vResult.begin(), vResult.end(), NameLess);
	return vResult;
}

const char *QmVoteMaps::CategoryKey(const char *pType)
{
	if(pType == nullptr || pType[0] == '\0')
		return "";
	if(str_comp_nocase(pType, "DDmaX") == 0)
		return "DDmaX";
	for(const auto &Alias : s_aCategoryAliases)
	{
		if(str_comp_nocase(pType, Alias.m_pKey) == 0 || str_comp_nocase(pType, Alias.m_pApiKey) == 0)
			return Alias.m_pKey;
	}
	return pType;
}

bool QmVoteMaps::CategoryMatches(const char *pFilter, const char *pType)
{
	if(pFilter == nullptr || pFilter[0] == '\0')
		return true;
	const char *pKey = CategoryKey(pType);
	if(str_comp_nocase(pFilter, pKey) == 0)
		return true;
	// 侧栏的 DDmaX 是分组项：目录没有裸 DDmaX 类型，按前缀匹配四个子类。
	return str_comp_nocase(pFilter, "DDmaX") == 0 && str_startswith_nocase(pKey, "DDmaX ");
}

bool QmVoteMaps::TypeVoteOptionMatches(const char *pDescription, const char *pTypeKey, const char *pTypeLabel)
{
	if(pDescription == nullptr || pTypeKey == nullptr || pTypeKey[0] == '\0')
		return false;

	// 官方服务器用 ☒/☑ 标当前分类的 info 行；点它不会切换地图列表，
	// 所以不能当成分类切换入口，否则跨分类投票会卡在一个无效投票上。
	const char *pStart = pDescription;
	while(*pStart == ' ' || *pStart == '\t')
		++pStart;
	if(str_startswith(pStart, "☒") || str_startswith(pStart, "☑"))
		return false;

	std::string NormalizedDescription;
	AppendNormalizedVoteText(pDescription, NormalizedDescription);

	std::string aNeedles[5];
	aNeedles[0] = pTypeKey;
	aNeedles[1] = std::string(pTypeKey) + " Maps";
	aNeedles[2] = std::string(pTypeKey) + " Map";
	if(pTypeLabel != nullptr && pTypeLabel[0] != '\0')
	{
		aNeedles[3] = pTypeLabel;
		aNeedles[4] = std::string(pTypeLabel) + "图";
	}

	for(const std::string &Needle : aNeedles)
	{
		if(Needle.empty())
			continue;
		std::string NormalizedNeedle;
		AppendNormalizedVoteText(Needle.c_str(), NormalizedNeedle);
		if(!NormalizedNeedle.empty() && NormalizedDescription.find(NormalizedNeedle) != std::string::npos)
			return true;
		// 兼容同形字（西里尔字母等）与旧式纯文本选项。
		if(MatchesNeedleByConfusables(pDescription, Needle.c_str()))
			return true;
	}
	return false;
}

bool QmVoteMaps::Matches(const SMap &Map, const SFilter &Filter, bool Favorite, ECompletion Completion)
{
	// 侧栏键是空格写法，目录数据可能是点号写法；比较前先归一。
	if(!Filter.m_Category.empty() && !CategoryMatches(Filter.m_Category.c_str(), Map.m_Category.c_str()))
		return false;
	if(Filter.m_StarMask != 0 && (Map.m_Stars < 0 || (Filter.m_StarMask & (1 << Map.m_Stars)) == 0))
		return false;
	if(Filter.m_FavoritesOnly && !Favorite)
		return false;
	if(Filter.m_Completion != ECompletion::UNKNOWN && Filter.m_Completion != Completion)
		return false;
	if(!Filter.m_Exclude.empty() && (Contains(Map.m_Name, Filter.m_Exclude) || Contains(Map.m_Mapper, Filter.m_Exclude)))
		return false;
	return Filter.m_Search.empty() || Contains(Map.m_Name, Filter.m_Search) || Contains(Map.m_Mapper, Filter.m_Search);
}

void QmVoteMaps::Sort(std::vector<int> &vIndices, const std::vector<SMap> &vMaps, ESort SortMode)
{
	std::stable_sort(vIndices.begin(), vIndices.end(), [&](int Left, int Right) {
		const SMap &A = vMaps[Left];
		const SMap &B = vMaps[Right];
		if(SortMode == ESort::DIFFICULTY && A.m_Stars != B.m_Stars)
			return A.m_Stars < 0 ? false : (B.m_Stars < 0 || A.m_Stars < B.m_Stars);
		if(SortMode == ESort::NEWEST && A.m_Release != B.m_Release)
			return A.m_Release > B.m_Release;
		return NameLess(A, B);
	});
}

int QmVoteMaps::Pick(const std::vector<int> &vIndices, unsigned Ticket)
{
	return vIndices.empty() ? -1 : vIndices[Ticket % vIndices.size()];
}
