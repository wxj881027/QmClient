#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEESKINLISTSTATE_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEESKINLISTSTATE_H

#include <game/client/components/qmclient/recent_tee_skins.h>
#include <game/client/components/settings_resource_jobs.h>

#include <map>
#include <optional>
#include <string>
#include <tuple>

inline bool SettingsTeeSkinEntryMatches(const char *pName, const std::optional<SSettingsSkinListColorKey> &Color, const SQmRecentTeeSkin &Selected)
{
	if(Selected.m_Name != pName)
		return false;
	if(!Color.has_value())
		return true;
	if(Color->m_UseCustomColor != Selected.m_UseCustomColor)
		return false;
	return !Color->m_UseCustomColor ||
	       (static_cast<unsigned>(Color->m_ColorBody) == Selected.m_ColorBody && static_cast<unsigned>(Color->m_ColorFeet) == Selected.m_ColorFeet);
}

// 列表异步重建和切换编辑对象时，交互与动画标识仍属于同一款皮肤/配色条目。
class CSettingsTeeSkinListState
{
public:
	struct SIds
	{
		char m_ListItem = 0;
		char m_Favorite = 0;
		char m_Queue = 0;
		char m_RightDoubleClick = 0;
		char m_ErrorTooltip = 0;
	};

	SIds &Resolve(const char *pName, const std::optional<SSettingsSkinListColorKey> &Color)
	{
		const bool CustomColors = Color.has_value() && Color->m_UseCustomColor;
		return m_Ids[{pName, Color.has_value(), CustomColors, CustomColors ? Color->m_ColorBody : 0, CustomColors ? Color->m_ColorFeet : 0}];
	}

private:
	std::map<std::tuple<std::string, bool, bool, int, int>, SIds> m_Ids;
};

#endif
