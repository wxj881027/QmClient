#ifndef GAME_CLIENT_QMUI_QMVOTEMENU_H
#define GAME_CLIENT_QMUI_QMVOTEMENU_H

#include <game/client/components/qmclient/vote_map_loader.h>

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

struct SQmVoteMenuState
{
	CQmVoteMapLoader m_Loader;
	std::vector<QmVoteMaps::SMap> m_vMaps;
	std::vector<int> m_vVisible;
	QmVoteMaps::SFilter m_Filter;
	QmVoteMaps::ESort m_Sort = QmVoteMaps::ESort::NAME;
	std::string m_SelectedName;
	std::string m_Status;
	uint64_t m_OptionsRevision = std::numeric_limits<uint64_t>::max();
	uint64_t m_CatalogRevision = std::numeric_limits<uint64_t>::max();
	bool m_ShowLibrary = true;
	bool m_Dirty = true;
	bool m_UseCatalog = false;
	int m_FinishedCount = -1;
	std::string m_RankPlayer;
	std::string m_Community;
	std::set<std::string> m_Favorites;
};

#endif
