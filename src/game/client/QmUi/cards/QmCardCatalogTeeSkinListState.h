#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEESKINLISTSTATE_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEESKINLISTSTATE_H

#include <game/client/QmUi/UiIconActionStrip.h>
#include <game/client/components/qmclient/recent_tee_skins.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/ui.h>

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

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

struct SSettingsTee7SkinDeletion
{
	std::string m_Name;
	int m_ReplacementIndex;
};

inline std::optional<SSettingsTee7SkinDeletion> TakeSettingsTee7SkinDeletion(std::string &PendingName, CUi::SConfirmPopupContext::EConfirmationResult Result, bool PopupOpen, const char *pSelectedName, int SelectedIndex)
{
	if(PendingName.empty())
		return std::nullopt;
	if(Result == CUi::SConfirmPopupContext::CONFIRMED)
	{
		const int ReplacementIndex = PendingName == pSelectedName ? SelectedIndex : -1;
		return SSettingsTee7SkinDeletion{std::exchange(PendingName, std::string{}), ReplacementIndex};
	}
	if(Result == CUi::SConfirmPopupContext::CANCELED || !PopupOpen)
		PendingName.clear();
	return std::nullopt;
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
		// 卡片右上角的功能图标条：只按本帧真的会画的动作从右到左排，
		// 出现/消失走同一条缓动（见 UiIconActionStrip.h 的公共约定）。
		// 0 = 加入/移出队列，1 = 收藏。
		ui_widget::CIconActionStrip<2> m_ActionStrip;
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
