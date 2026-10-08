#include "QmCardCatalogInternal.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;

void CMenus::RenderQmFunctionFavoriteMapsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	const auto &FavMaps = GameClient()->TClientComponent().GetFavoriteMaps();
	qm_card_catalog::FavoriteMapsLayoutRevision(FavMaps.size());

	auto MapCategoryKeyFromText = [](const char *pText) -> const char * {
		if(!pText || pText[0] == '\0')
			return nullptr;
		if(str_find_nocase(pText, "DDmaX"))
		{
			if(str_find_nocase(pText, "Easy"))
				return "DDmaX Easy";
			if(str_find_nocase(pText, "Next"))
				return "DDmaX Next";
			if(str_find_nocase(pText, "Pro"))
				return "DDmaX Pro";
			if(str_find_nocase(pText, "Nut"))
				return "DDmaX Nut";
			return "DDmaX";
		}
		if(str_find_nocase(pText, "Oldschool"))
			return "Oldschool";
		if(str_find_nocase(pText, "Novice"))
			return "Novice";
		if(str_find_nocase(pText, "Moderate"))
			return "Moderate";
		if(str_find_nocase(pText, "Brutal"))
			return "Brutal";
		if(str_find_nocase(pText, "Insane"))
			return "Insane";
		if(str_find_nocase(pText, "Dummy"))
			return "Dummy";
		if(str_find_nocase(pText, "Solo"))
			return "Solo";
		if(str_find_nocase(pText, "Race"))
			return "Race";
		if(str_find_nocase(pText, "Fun"))
			return "Fun";
		if(str_find_nocase(pText, "Event"))
			return "Event";
		return nullptr;
	};
	// 只做纯文本映射，不读成员，无需捕获 this。
	auto MapTypeDisplayName = [](const char *pType) -> const char * {
		if(!pType || pType[0] == '\0')
			return Localize("Unknown");
		if(str_comp_nocase(pType, "DDmaX Easy") == 0)
			return Localize("Classic easy");
		if(str_comp_nocase(pType, "DDmaX Next") == 0)
			return Localize("Classic next");
		if(str_comp_nocase(pType, "DDmaX Pro") == 0)
			return Localize("Classic pro");
		if(str_comp_nocase(pType, "DDmaX Nut") == 0)
			return Localize("Classic nut");
		if(str_comp_nocase(pType, "DDmaX") == 0)
			return Localize("Classic");
		if(str_comp_nocase(pType, "Novice") == 0)
			return Localize("Novice");
		if(str_comp_nocase(pType, "Moderate") == 0)
			return Localize("Moderate");
		if(str_comp_nocase(pType, "Brutal") == 0)
			return Localize("Brutal");
		if(str_comp_nocase(pType, "Insane") == 0)
			return Localize("Insane");
		if(str_comp_nocase(pType, "Dummy") == 0)
			return Localize("Dummy");
		if(str_comp_nocase(pType, "Solo") == 0)
			return Localize("Solo");
		if(str_comp_nocase(pType, "Oldschool") == 0)
			return Localize("Oldschool");
		if(str_comp_nocase(pType, "Race") == 0)
			return Localize("Race");
		if(str_comp_nocase(pType, "Fun") == 0)
			return Localize("Fun");
		if(str_comp_nocase(pType, "Event") == 0)
			return Localize("Event");
		return Localize("Unknown");
	};

	static std::unordered_map<std::string, std::string> s_MapCategories;
	static int s_MapCategoryScanIndex = 0;
	static int s_LastNumServers = -1;
	static float s_NextFullScan = 0.0f;
	IServerBrowser *pServerBrowser = ServerBrowser();
	const float Now = Client()->LocalTime();
	if(Ui()->RenderOnly())
	{
		// 文本预热只读取现有分类快照，不能扫描服务器或写入磁盘缓存。
	}
	else if(!pServerBrowser || FavMaps.empty())
	{
		s_MapCategories.clear();
		s_MapCategoryScanIndex = 0;
		s_LastNumServers = -1;
		s_NextFullScan = 0.0f;
	}
	else
	{
		const int NumServers = pServerBrowser->NumSortedServers();
		if(NumServers != s_LastNumServers)
		{
			s_LastNumServers = NumServers;
			s_MapCategoryScanIndex = 0;
			s_NextFullScan = 0.0f;
		}
		if(NumServers > 0 && (Now >= s_NextFullScan || s_MapCategoryScanIndex > 0))
		{
			if(s_MapCategoryScanIndex == 0)
			{
				s_MapCategories.clear();
				s_MapCategories.reserve((size_t)NumServers);
			}
			constexpr int ServersPerFrame = 64;
			int ProcessedServers = 0;
			while(s_MapCategoryScanIndex < NumServers && ProcessedServers < ServersPerFrame)
			{
				const CServerInfo *pInfo = pServerBrowser->SortedGet(s_MapCategoryScanIndex);
				++s_MapCategoryScanIndex;
				++ProcessedServers;
				if(!pInfo || pInfo->m_aMap[0] == '\0')
					continue;
				const char *pCategoryKey = MapCategoryKeyFromText(pInfo->m_aCommunityType);
				if(!pCategoryKey)
					pCategoryKey = MapCategoryKeyFromText(pInfo->m_aName);
				if(!pCategoryKey)
					continue;
				auto It = s_MapCategories.find(pInfo->m_aMap);
				if(It == s_MapCategories.end() || It->second != pCategoryKey)
				{
					s_MapCategories[pInfo->m_aMap] = pCategoryKey;
					GameClient()->TClientComponent().UpdateMapCategoryCache(pInfo->m_aMap, pCategoryKey);
				}
			}
			if(s_MapCategoryScanIndex >= NumServers)
			{
				s_MapCategoryScanIndex = 0;
				s_NextFullScan = Now + 2.0f;
			}
		}
		const NETADDR *pServerAddr = Client()->ServerAddress();
		const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
		if(pEntry && pEntry->m_Info.m_aMap[0] != '\0')
		{
			const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
			if(!pCategoryKey)
				pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
			if(pCategoryKey)
			{
				s_MapCategories[pEntry->m_Info.m_aMap] = pCategoryKey;
				GameClient()->TClientComponent().UpdateMapCategoryCache(pEntry->m_Info.m_aMap, pCategoryKey);
			}
		}
	}

	auto GetMapCategory = [&](const char *pMapName) -> const char * {
		if(!pMapName || pMapName[0] == '\0')
			return Localize("Unknown");
		const auto It = s_MapCategories.find(pMapName);
		if(It != s_MapCategories.end() && !It->second.empty())
			return MapTypeDisplayName(It->second.c_str());
		const char *pCurrentMap = Client()->GetCurrentMap();
		if(pServerBrowser && pCurrentMap && str_comp(pCurrentMap, pMapName) == 0)
		{
			const NETADDR *pServerAddr = Client()->ServerAddress();
			const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
			if(pEntry)
			{
				const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
				if(!pCategoryKey)
					pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
				if(pCategoryKey)
					return MapTypeDisplayName(pCategoryKey);
			}
		}
		const char *pCachedCategory = GameClient()->TClientComponent().GetCachedMapCategoryKey(pMapName);
		if(pCachedCategory)
			return MapTypeDisplayName(pCachedCategory);
		return Localize("Unknown");
	};

	static int s_CopiedMapIndex = -1;
	static float s_CopiedTime = 0.0f;
	if(s_CopiedMapIndex >= 0 && Client()->LocalTime() - s_CopiedTime > 1.5f)
		s_CopiedMapIndex = -1;
	CUIRect Row;
	if(FavMaps.empty())
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-favorite-maps-empty", &Row, Localize("No favorite maps yet"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		return;
	}

	static int s_aMapButtonIds[64];
	static CButtonContainer s_aMapRemoveButtons[64];
	IUiContext IconButtonCtx = SettingsUiContext("settings_qmclient_favorite_maps_icons", UiScale);
	std::string RemoveMapName;
	size_t MapIndex = 0;
	for(const std::string &MapName : FavMaps)
	{
		if(MapIndex >= std::size(s_aMapButtonIds))
			break;
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect RowLabel, RowRemove;
		Row.VSplitRight(LineHeight, &RowLabel, &RowRemove);
		RowRemove.HMargin(std::clamp(2.0f * UiScale, 1.0f, 2.0f), &RowRemove);
		if(ui_widget::IconButton(IconButtonCtx, &s_aMapRemoveButtons[MapIndex], EQmIcon::CLOSE, FONT_ICON_XMARK, RowRemove))
		{
			if(RemoveMapName.empty())
				RemoveMapName = MapName;
			s_CopiedMapIndex = -1;
		}
		if(!PrewarmOnly && Ui()->MouseInside(&RowLabel))
		{
			Ui()->SetHotItem(&s_aMapButtonIds[MapIndex]);
			if(Ui()->MouseButtonClicked(0))
			{
				Input()->SetClipboardText(MapName.c_str());
				s_CopiedMapIndex = (int)MapIndex;
				s_CopiedTime = Client()->LocalTime();
			}
		}
		if(s_CopiedMapIndex == (int)MapIndex)
		{
			TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-favorite-map-copied", &RowLabel, Localize("Copied"), BodySize, TEXTALIGN_ML, {}, (int)RowLabel.w);
		}
		else
		{
			char aLabel[256];
			str_format(aLabel, sizeof(aLabel), "%s (%s)", MapName.c_str(), GetMapCategory(MapName.c_str()));
			TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f);
			Ui()->DoLabel(&RowLabel, aLabel, BodySize, TEXTALIGN_ML);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(Ui()->HotItem() == &s_aMapButtonIds[MapIndex])
			GameClient()->m_Tooltips.DoToolTip(&s_aMapButtonIds[MapIndex], &RowLabel, Localize("Click to copy the map name"));
		if(Ui()->HotItem() == &s_aMapRemoveButtons[MapIndex])
			GameClient()->m_Tooltips.DoToolTip(&s_aMapRemoveButtons[MapIndex], &RowRemove, Localize("Remove from favorites"));
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		++MapIndex;
	}
	if(!RemoveMapName.empty())
	{
		GameClient()->TClientComponent().RemoveFavoriteMap(RemoveMapName.c_str());
		const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
		qm_card_catalog::FavoriteMapsLayoutRevision(FavoriteMapCount);
	}
}

namespace qm_card_catalog
{
	uint64_t FavoriteMapsLayoutRevision(size_t Count)
	{
		static size_t s_Count = std::numeric_limits<size_t>::max();
		static uint64_t s_Revision = 1;
		if(s_Count != Count)
		{
			s_Count = Count;
			++s_Revision;
		}
		return s_Revision;
	}
}
