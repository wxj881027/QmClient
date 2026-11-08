#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <utility>

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
			"Highlight frozen teammates' hammer and hook inputs",
			[pMenus, Metrics, LineHeight, LineSpacing](CUIRect &Content) {
				QmCardRenderHook::RenderQmVisualCheckbox(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWaterHammerHighlight, "Highlight water hammering teammates", Localize("Highlight frozen teammates' hammer and hook inputs"), &g_Config.m_QmWaterHammerHighlight);
				CSettingsContentLayoutFlow Flow(Content);
				static CButtonContainer s_HammerColor, s_HookColor;
				CUIRect Row;
				if(Flow.Control("water-hammer-color", LineHeight, Row))
					pMenus->DoLine_ColorPicker(&s_HammerColor, Metrics, &Row, Localize("Pressed hammer icon color"), &g_Config.m_QmWaterHammerColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				Flow.Space(LineSpacing);
				if(Flow.Control("water-hook-color", LineHeight, Row))
					pMenus->DoLine_ColorPicker(&s_HookColor, Metrics, &Row, Localize("Pressed hook icon color"), &g_Config.m_QmWaterHookColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				Flow.Space(LineSpacing);
			},
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
