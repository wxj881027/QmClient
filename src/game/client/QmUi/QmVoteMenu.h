#ifndef GAME_CLIENT_QMUI_QMVOTEMENU_H
#define GAME_CLIENT_QMUI_QMVOTEMENU_H

class CGameClient;

#include "QmPlayersMenu.h"
#include "QmVoteMapPreview.h"

#include <generated/protocol.h>

#include <game/client/components/qmclient/map_preview_panel.h>
#include <game/client/components/qmclient/server_vote_list.h>
#include <game/client/components/qmclient/vote_map_loader.h>
#include <game/client/components/qmclient/vote_menu_mode.h>

#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

enum class EQmVoteMenuPage
{
	OPTIONS,
	KICK,
	SPECTATE,
};

inline bool QmVotePlayerEligible(EQmVoteMenuPage Page, int Id, int LocalId, bool Present, bool Spectator)
{
	if(!Present || Id < 0 || Id >= MAX_CLIENTS || Id == LocalId)
		return false;
	return Page == EQmVoteMenuPage::KICK || (Page == EQmVoteMenuPage::SPECTATE && !Spectator);
}

struct SQmVoteMenuState
{
	CQmVoteMapLoader m_Loader;
	QmVoteMaps::SAxiomMenu m_AxiomMenu;
	uint64_t m_AxiomRevision = std::numeric_limits<uint64_t>::max();
	std::vector<const char *> m_vpAxiomCategoryLabels;
	std::vector<QmVoteMaps::SMap> m_vMaps;
	std::vector<int> m_vVisible;
	QmVoteMaps::SFilter m_Filter;
	QmVoteMaps::ESort m_Sort = QmVoteMaps::ESort::NAME;
	std::string m_SelectedName;
	uint64_t m_OptionsRevision = std::numeric_limits<uint64_t>::max();
	uint64_t m_CatalogRevision = std::numeric_limits<uint64_t>::max();
	EQmVoteMenuPage m_Page = EQmVoteMenuPage::OPTIONS;
	CQmVoteMenuMode m_Mode;
	CQmServerVoteList m_ServerList;
	CLineInputBuffered<64> m_ServerSearch;
	std::array<QmPlayersUi::CSelection, 2> m_PlayerSelections;
	std::array<CLineInputBuffered<64>, 2> m_PlayerSearch;
	std::array<CLineInputBuffered<64>, 2> m_PlayerExclude;
	// 预览地图：按钮容器与面板上下文跨帧保留，三张子页共用同一个入口。
	CButtonContainer m_MapPreviewButton;
	CQmMapPreviewPanel::SContext m_MapPreview;
	bool m_Dirty = true;
	int m_FinishedCount = -1;
	std::string m_RankPlayer;
	std::set<std::string> m_Favorites;
};

inline bool QmTryPrepareDdnetVoteMaps(const std::vector<QmVoteMaps::SMap> &vCatalog, const CVoteOptionClient *pFirst, std::vector<QmVoteMaps::SMap> &vCandidate)
{
	try
	{
		vCandidate = QmVoteMaps::MergeCatalog(vCatalog, pFirst);
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
}

template<typename FMatches>
bool QmTryRebuildVoteMapRows(const std::vector<QmVoteMaps::SMap> &vMaps, const QmVoteMaps::SFilter &CurrentFilter, QmVoteMaps::ESort Sort,
	bool Axiom, int AvailableStarMask, std::vector<int> &vVisible, const FMatches &Matches)
{
	vVisible.clear();
	try
	{
		auto Filter = CurrentFilter;
		if(Axiom)
		{
			// Axiom 由服务器下发当前分类，本地保存的分类筛选留给 DDNet 使用。
			Filter.m_Category.clear();
			Filter.m_StarMask &= AvailableStarMask;
		}
		for(size_t Index = 0; Index < vMaps.size(); ++Index)
			if(Matches(vMaps[Index], Filter))
				vVisible.push_back((int)Index);
		QmVoteMaps::Sort(vVisible, vMaps, Sort);
		return true;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}
	// 不向绘制和投票入口暴露只筛完一部分的索引。
	vVisible.clear();
	return false;
}

#endif
