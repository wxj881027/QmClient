#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

// 更好的计分板卡片内容。计分板相关配置集中在独立卡片中，避免与梦的小功能卡片耦合。
// 组内行距与组尾间距由共享按钮网格按卡片 metrics 统一给出，卡片自己不再补 Spacing，
// 否则组尾间距和额外间距会叠成双倍空隙。
void CMenus::RenderQmFunctionBetterScoreboardContent(CUIRect &Content, float, float, float, float, bool PrewarmOnly)
{
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
		{
			{&g_Config.m_QmBetterScoreboard, "Better scoreboard", Localize("Better scoreboard")},
			{&g_Config.m_QmScoreboardPoints, "Scoreboard point check", Localize("Scoreboard point check")},
			{&g_Config.m_QmScoreboardPlaytime, "Scoreboard playtime", Localize("Scoreboard playtime"), nullptr, 0, Localize("Show total playtime from DDStats instead of points in scoreboard")},
			{&g_Config.m_QmScoreboardOnDeath, "Show scoreboard after death", Localize("Show scoreboard after death")},
			{&g_Config.m_QmScoreboardScroll, "Scroll mode", Localize("Scroll mode"), nullptr, 0, Localize("Use the scoreboard cursor mode to scroll the list")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
}
