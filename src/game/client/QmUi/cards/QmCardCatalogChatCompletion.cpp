#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_command_hud_render.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::RenderQmChatCompletionContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	const bool StartedRenderOnly = PrewarmOnly && !Ui()->RenderOnly();
	if(StartedRenderOnly)
		Ui()->BeginRenderOnly();
	const bool ReadOnly = Ui()->RenderOnly();
	CSettingsContentLayoutFlow Flow(Content);
	if(!qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:chat_completion", Metrics, ReadOnly))
	{
		if(StartedRenderOnly)
			Ui()->EndRenderOnly();
		return;
	}
	struct SControls
	{
		CButtonContainer m_Background;
		CButtonContainer m_Text;
		CButtonContainer m_SelectedText;
		CButtonContainer m_Selection;
		ui_widget::SNumericFieldState m_Opacity;
		CButtonContainer m_Reset;
	};
	static SControls s_Controls;
	const auto ColorRow = [&](const char *pId, CButtonContainer *pButton, const char *pLabel, unsigned *pValue, unsigned Default, bool Alpha) {
		CUIRect Row;
		if(Flow.Control(pId, Metrics.m_ButtonHeight, Row))
			DoLine_ColorPicker(pButton, Metrics, &Row, pLabel, pValue, color_cast<ColorRGBA>(ColorHSLA(Default, Alpha)), false, nullptr, Alpha);
		Flow.Space(Metrics.m_LineSpacing);
	};
	ColorRow("completion-background", &s_Controls.m_Background, Localize("Background color"), &g_Config.m_QmChatCompletionBgColor, DefaultConfig::QmChatCompletionBgColor, false);

	CUIRect Row;
	const bool HasOpacity = Flow.Control("completion-opacity", Metrics.m_LineHeight, Row);
	ui_widget::SNumericFieldOptions OpacityOptions;
	OpacityOptions.m_pLabel = Localize("Background opacity");
	OpacityOptions.m_pSuffix = "%";
	OpacityOptions.m_FontSize = Metrics.m_BodySize;
	const IUiContext Ctx = SettingsUiContext("chat_completion_appearance", Metrics.m_UiScale);
	if(HasOpacity)
	{
		ui_widget::NumericField(Ctx, &s_Controls.m_Opacity, &s_Controls.m_Opacity, &g_Config.m_QmChatCompletionOpacity, 0, 100, Row, OpacityOptions);
		Ui()->DoConfigTooltip(&s_Controls.m_Opacity, &Row, &g_Config.m_QmChatCompletionOpacity);
	}
	Flow.Space(Metrics.m_LineSpacing);

	ColorRow("completion-text", &s_Controls.m_Text, Localize("Text color"), &g_Config.m_QmChatCompletionTextColor, DefaultConfig::QmChatCompletionTextColor, true);
	ColorRow("completion-selected-text", &s_Controls.m_SelectedText, Localize("Selected text color"), &g_Config.m_QmChatCompletionSelectedTextColor, DefaultConfig::QmChatCompletionSelectedTextColor, true);
	ColorRow("completion-selection", &s_Controls.m_Selection, Localize("Selection background color"), &g_Config.m_QmChatCompletionSelectedColor, DefaultConfig::QmChatCompletionSelectedColor, true);

	// 预览使用实际补全布局和渲染，选中项与普通项共享聊天中的配色语义。
	CQmChatCommandHud Preview;
	Preview.BeginUpdate("/", 1, true, 0);
	Preview.AddServerCommand("addweapon", "i[weapon-id]", Localize("Weapon"));
	Preview.AddServerCommand("info", "", Localize("Info"));
	Preview.EndUpdate();
	Preview.MoveSelection(1, "/", 1);
	const float FontSize = Metrics.m_BodySize / CQmChatCommandHud::UI_SCALE;
	const float Width = Content.w / CQmChatCommandHud::UI_SCALE;
	Preview.SetLayout(Content.x, Content.y, Width, 1000.0f, FontSize, Content.x, Content.x + Content.w);
	if(Flow.Control("completion-preview", Preview.Layout().m_H, Row))
	{
		Preview.SetLayout(Row.x, Row.y + Row.h, Width, Row.h + Metrics.m_LineHeight, FontSize, Row.x, Row.x + Row.w);
		if(Flow.Drawing() && (!ReadOnly || Ui()->BackgroundRender()))
			QmRenderChatCommandHud(Preview, Ui(), TextRender(), Ui()->MousePos());
	}
	Flow.Space(Metrics.m_LineSpacing);

	if(Flow.Control("completion-reset", Metrics.m_ButtonHeight, Row) && ui_widget::SecondaryButton(Ctx, &s_Controls.m_Reset, Localize("Reset chat completion appearance"), Row, ReadOnly))
	{
		g_Config.m_QmChatCompletionBgColor = DefaultConfig::QmChatCompletionBgColor;
		g_Config.m_QmChatCompletionOpacity = DefaultConfig::QmChatCompletionOpacity;
		g_Config.m_QmChatCompletionTextColor = DefaultConfig::QmChatCompletionTextColor;
		g_Config.m_QmChatCompletionSelectedTextColor = DefaultConfig::QmChatCompletionSelectedTextColor;
		g_Config.m_QmChatCompletionSelectedColor = DefaultConfig::QmChatCompletionSelectedColor;
	}
	Flow.Space(Metrics.m_LineSpacing);
	if(StartedRenderOnly)
		Ui()->EndRenderOnly();
}

namespace qm_card_catalog
{
	bool BuildChatCompletionCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		const auto RenderChatCompletion = [pMenus = Ctx.m_pMenus, Metrics = Ctx.m_Metrics](CUIRect &Content, const bool PrewarmOnly) {
			QmCardRenderHook::RenderQmChatCompletionContent(pMenus, Content, Metrics, PrewarmOnly);
		};
		MakeModuleCard(Ctx, qm_module::EQmModuleId::ChatCompletion, "qm:chat_completion", "Chat completion appearance", "Colors for chat command and emoji completion", [RenderChatCompletion, ReadOnly = Ctx.m_ReadOnly](CUIRect &Content) { RenderChatCompletion(Content, ReadOnly); }, Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
}
