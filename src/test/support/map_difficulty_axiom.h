#ifndef TEST_SUPPORT_MAP_DIFFICULTY_AXIOM_H
#define TEST_SUPPORT_MAP_DIFFICULTY_AXIOM_H

#include <base/str.h>

#include <engine/serverbrowser.h>

#include <string>

namespace map_difficulty_test
{
	inline CServerInfo Server(const char *pMap = "Old")
	{
		CServerInfo Result{};
		str_copy(Result.m_aCommunityId, "axiom");
		str_copy(Result.m_aGameType, "Gores");
		str_copy(Result.m_aMap, pMap);
		return Result;
	}
	inline std::string Motd(const char *pMap, const char *pStars, const char *pCategory = "Normal")
	{
		return std::string("Axiom\n┌──── 当前地图信息\n│ 地图：") + pMap + "\n│ 类型：" + pCategory + "\n│ 星级：" + pStars;
	}
	inline constexpr char OLD_CACHE[] = R"({"version":1,"entries":[{"mode":"Gores","map":"Old","category":"Old","stars":1}]})";
}

#endif
