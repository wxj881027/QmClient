#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>

#include <game/localization.h>

namespace qm_card_catalog
{
	namespace
	{
		void RenderSteamContent(const SQmCardBuildContext &Ctx, CUIRect &Content)
		{
			CUIRect Row;
			Content.HSplitTop(Ctx.m_Metrics.m_LineHeight, &Row, &Content);
			QmCardRenderHook::RenderQmFunctionCheckbox(
				Ctx.m_pMenus,
				&g_Config.m_QmSteamAutoLaunch,
				"qm-steam-auto-launch",
				Localize("Launch Steam automatically when starting externally"),
				&g_Config.m_QmSteamAutoLaunch,
				&Row,
				Ctx.m_ReadOnly);
			Content.HSplitTop(Ctx.m_Metrics.m_LineSpacing, nullptr, &Content);
		}

		float MeasureSteamContent(const SQmCardBuildContext &Ctx, const float)
		{
			return CardRow(Ctx.m_Metrics);
		}
	}

	bool BuildSteamCard(const SQmCardBuildContext &Ctx, const qm_module::EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(
			Ctx,
			Id,
			"qm:steam",
			"Steam integration",
			"Automatically launch Steam and hand over external launches when enabled",
			[Ctx](CUIRect &Content) { RenderSteamContent(Ctx, Content); },
			[Metrics = Ctx.m_Metrics](const float ContentWidth) {
				SQmCardBuildContext MeasureContext;
				MeasureContext.m_Metrics = Metrics;
				return MeasureSteamContent(MeasureContext, ContentWidth);
			},
			g_Config.m_QmSteamAutoLaunch != 0 ? 1u : 0u,
			{},
			Out);
		return true;
	}
} // namespace qm_card_catalog
