#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

// 更好的计分板卡片内容。计分板相关配置集中在独立卡片中，避免与梦的小功能卡片耦合。
void CMenus::RenderQmFunctionBetterScoreboardContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	// 文案在调用点用 Localize 包裹：提取器按 Localize/Desc 字面量收集可翻译 key，
	// 只把裸字面量转发给局部 lambda 会被漏掉（计分板积分检查条目曾因此丢掉全部译文）。
	auto RenderCheckbox = [this, &Flow, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		if(Flow.Control(pTextId, LineHeight, Row))
			RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &Row, PrewarmOnly);
		Flow.Space(LineSpacing);
	};
	auto RenderCheckboxTipped = [this, &Flow, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, const char *pTooltip, int *pValue) {
		if(Flow.Control(pTextId, LineHeight, Row))
			RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &Row, PrewarmOnly, pTooltip);
		Flow.Space(LineSpacing);
	};

	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
		{
			{&g_Config.m_QmBetterScoreboard, "Better scoreboard", Localize("Better scoreboard")},
			{&g_Config.m_QmScoreboardPoints, "Scoreboard point check", Localize("Scoreboard point check")},
			{&g_Config.m_QmScoreboardPlaytime, "Scoreboard playtime", Localize("Scoreboard playtime"), nullptr, 0, Localize("Show total playtime from DDStats instead of points in scoreboard")},
			{&g_Config.m_QmScoreboardOnDeath, "Show scoreboard after death", Localize("Show scoreboard after death")},
			{&g_Config.m_QmScoreboardScroll, "Scroll mode", Localize("Scroll mode"), nullptr, 0, Localize("Use the scoreboard cursor mode to scroll the list")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
	if(Flow.Control("qmclient-scoreboard-filter", LineHeight, Row))
	{
		// 计分板过滤器直接绑定配置缓冲，保持原有输入即时生效和控制台同步语义。
		IUiContext TextInputCtx = SettingsUiContext("qmclient-mini-scoreboard-filter-input", BodySize / ui_token::font::BODY);
		CUIRect LabelColumn;
		CUIRect ControlColumn;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-scoreboard-filter", &LabelColumn, Localize("Scoreboard filter: only show players whose name or clan contains this text"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static CLineInput s_ScoreboardFilterInput(g_Config.m_QmScoreboardFilter, sizeof(g_Config.m_QmScoreboardFilter));
		s_ScoreboardFilterInput.SetEmptyText(Localize("Leave empty to show everyone"));
		ui_widget::InputField(TextInputCtx, &s_ScoreboardFilterInput, ControlColumn, Localize("Leave empty to show everyone"), BodySize);
	}
	Flow.Space(LineSpacing);
}
