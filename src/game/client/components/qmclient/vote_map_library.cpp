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
	}), vParsed.end());
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

bool QmVoteMaps::Matches(const SMap &Map, const SFilter &Filter, bool Favorite, ECompletion Completion)
{
	if(!Filter.m_Category.empty() && Map.m_Category != Filter.m_Category)
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
