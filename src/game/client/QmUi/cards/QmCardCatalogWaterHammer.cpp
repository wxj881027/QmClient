#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/water_hammer_indicator_logic.h>
#include <game/localization.h>

#include <vector>

namespace qm_card_catalog
{
	bool QmCardRenderHook::BuildWaterHammerCard(const SQmCardBuildContext &Ctx, const qm_module::EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		if(Id != qm_module::EQmModuleId::WaterHammerHighlight)
			return false;

		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float LineSpacing = Ctx.m_Metrics.m_LineSpacing;

		MakeModuleCard(
			Ctx,
			Id,
			"qm:water_hammer",
			"?!Stuck keys!?",
			"Highlight held hammer and hook inputs while frozen",
			[pMenus, Metrics, LineHeight, LineSpacing](CUIRect &Content) {
				constexpr int Page = CMenus::SETTINGS_QMCLIENT;
				constexpr int Tab = CMenus::QMCLIENT_SETTINGS_TAB_VISUAL;
				const float RowGap = ui_token::settings::RowGap(LineSpacing);
				QmCardRenderHook::RenderQmVisualCheckbox(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWaterHammerHighlight, "water-input-highlight", Localize("Highlight held hammer and hook inputs while frozen"), &g_Config.m_QmWaterHammerHighlight);
				CSettingsContentLayoutFlow Flow(Content);
				static std::vector<CButtonContainer> s_vScopeButtons(3);
				CUIRect Row;
				if(Flow.Control("water-input-scope", LineHeight, Row, RowGap))
				{
					int Scope = g_Config.m_QmWaterInputScope;
					if(pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, Row, "water-input-scope", Localize("Stuck input display scope"),
						   s_vScopeButtons,
						   {"water-input-own", "water-input-others", "water-input-all"},
						   {Localize("Own", "Stuck input display scope"), Localize("Others", "Stuck input display scope"), Localize("All", "Stuck input display scope")},
						   {static_cast<int>(EQmWaterInputScope::OWN), static_cast<int>(EQmWaterInputScope::OTHERS), static_cast<int>(EQmWaterInputScope::ALL)},
						   Scope, Metrics, nullptr, &g_Config.m_QmWaterInputScope) &&
						Flow.Input())
						g_Config.m_QmWaterInputScope = Scope;
				}
				pMenus->DoSettingsToggleGroup(Page, Tab, Tab, Content,
					{
						{&g_Config.m_QmWaterHammerIcon, "water-hammer-icon", Localize("Show hammer icon")},
						{&g_Config.m_QmWaterHookIcon, "water-hook-icon", Localize("Show hook icon")},
					},
					Metrics, Flow.Input());

				static CButtonContainer s_HammerColor, s_HookColor;
				// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space。
				if(Flow.Control("water-hammer-color", LineHeight, Row, RowGap))
					pMenus->DoLine_ColorPicker(&s_HammerColor, Metrics, &Row, Localize("Pressed hammer icon color"), &g_Config.m_QmWaterHammerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmWaterHammerColor)), false);
				if(Flow.Control("water-hook-color", LineHeight, Row, RowGap))
					pMenus->DoLine_ColorPicker(&s_HookColor, Metrics, &Row, Localize("Pressed hook icon color"), &g_Config.m_QmWaterHookColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmWaterHookColor)), false);
			},
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
