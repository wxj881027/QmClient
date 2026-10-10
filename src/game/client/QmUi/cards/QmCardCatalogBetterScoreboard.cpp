#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/localization.h>

// 更好的计分板卡片内容。计分板相关配置集中在独立卡片中，避免与梦的小功能卡片耦合。
void CMenus::RenderQmFunctionBetterScoreboardContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row;
	// 文案在调用点用 Localize 包裹：提取器按 Localize/Desc 字面量收集可翻译 key，
	// 只把裸字面量转发给局部 lambda 会被漏掉（计分板积分检查条目曾因此丢掉全部译文）。
	auto RenderCheckbox = [this, &Content, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, pId, pTextId, pText, pValue, PrewarmOnly);
	};
	auto RenderCheckboxTipped = [this, &Content, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, const char *pTooltip, int *pValue) {
		RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, pId, pTextId, pText, pValue, PrewarmOnly, pTooltip);
	};

	RenderCheckbox(&g_Config.m_QmBetterScoreboard, "Better scoreboard", Localize("Better scoreboard"), &g_Config.m_QmBetterScoreboard);
	DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
		{
			{&g_Config.m_QmScoreboardPoints, "Scoreboard point check", Localize("Scoreboard point check")},
			{&g_Config.m_QmScoreboardOnDeath, "Show scoreboard after death", Localize("Show scoreboard after death")},
		},
		CurrentSettingsContentMetrics(), !PrewarmOnly);
	RenderCheckboxTipped(&g_Config.m_QmScoreboardScroll, "Fixed-size scoreboard rows with mouse wheel scrolling for crowded servers", Localize("Fixed-size scoreboard rows with mouse wheel scrolling for crowded servers"), Localize("Use the scoreboard cursor mode to scroll the list"), &g_Config.m_QmScoreboardScroll);
	{
		// 计分板过滤器直接绑定配置缓冲，保持原有输入即时生效和控制台同步语义。
		IUiContext TextInputCtx = SettingsUiContext("qmclient-mini-scoreboard-filter-input", BodySize / ui_token::font::BODY);
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect LabelColumn;
		CUIRect ControlColumn;
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-scoreboard-filter", &LabelColumn, Localize("Scoreboard filter: only show players whose name or clan contains this text"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static CLineInput s_ScoreboardFilterInput(g_Config.m_QmScoreboardFilter, sizeof(g_Config.m_QmScoreboardFilter));
		s_ScoreboardFilterInput.SetEmptyText(Localize("Leave empty to show everyone"));
		ui_widget::InputField(TextInputCtx, &s_ScoreboardFilterInput, ControlColumn, Localize("Leave empty to show everyone"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}
