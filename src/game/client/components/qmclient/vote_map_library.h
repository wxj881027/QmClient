#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MAP_LIBRARY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MAP_LIBRARY_H

#include <game/voting.h>

#include <cstddef>
#include <string>
#include <vector>

namespace QmVoteMaps
{
	struct SMap
	{
		std::string m_Name;
		std::string m_Mapper;
		std::string m_Category;
		std::string m_Release;
		int m_Stars = -1;
		int m_Points = -1;
	};

	struct SDetails
	{
		int m_Finishers = -1;
		double m_AverageSeconds = -1.0;
	};

	enum class ECompletion
	{
		UNKNOWN,
		UNFINISHED,
		FINISHED,
	};

	enum class ESort
	{
		NAME,
		DIFFICULTY,
		NEWEST,
	};

	enum class EVoteResult
	{
		NONE,
		PASS,
		FAIL,
		ABORT,
	};

	struct SFilter
	{
		std::string m_Category;
		std::string m_Search;
		std::string m_Exclude;
		int m_StarMask = 0;
		bool m_FavoritesOnly = false;
		ECompletion m_Completion = ECompletion::UNKNOWN;
	};

	// 解析失败保留上一次有效目录；未知分类仍可在「全部」中浏览。
	bool ParseCatalog(const char *pJson, size_t Length, std::vector<SMap> &vMaps);
	bool ParseDetails(const char *pJson, size_t Length, const SMap &Map, SDetails &Details);
	bool ParseVoteMap(const char *pDescription, SMap &Map);
	int FindOption(const CVoteOptionClient *pFirst, const char *pMapName);
	EVoteResult ServerVoteResult(int ClientId, int Team, const char *pMessage);
	std::vector<SMap> MergeCatalog(const std::vector<SMap> &vCatalog, const CVoteOptionClient *pFirst);
	bool Matches(const SMap &Map, const SFilter &Filter, bool Favorite, ECompletion Completion);
	// ddnet.org 目录把 DDmaX 子类写成 DDmaX.Next 等点号形式，界面与侧栏用空格写法；
	// 归一化只用于比较和展示，SMap::m_Category 仍保存接口原文，避免详情接口校验失配。
	const char *CategoryKey(const char *pType);
	bool CategoryMatches(const char *pFilter, const char *pType);
	// 官方 DDNet 服务器的分类切换选项带装饰符号与小型大写字母（☐ DDᴍᴀX.Nᴇxᴛ Mᴀᴘs），
	// 与界面键名写法不同；按归一化骨架匹配，并保留同形字兼容。
	bool TypeVoteOptionMatches(const char *pDescription, const char *pTypeKey, const char *pTypeLabel);
	void Sort(std::vector<int> &vIndices, const std::vector<SMap> &vMaps, ESort SortMode);
	int Pick(const std::vector<int> &vIndices, unsigned Ticket);
}

#endif
