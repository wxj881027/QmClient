#include "QmCardCatalog.h"
#include "QmCardCatalogTClientInternal.h"

#include <base/str.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <optional>

namespace qm_card_catalog
{

	STClientCardResult QmCardRenderHook::RunTClientCard(const SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, ETClientCardPass Pass)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const auto *pDefault = qm_card_registry::FindByStableId(pStableId);
		if(pMenus == nullptr || pDefault == nullptr || !IsTClientCard(pStableId))
			return {};
		std::optional<qm_tclient_cards::CUiRenderOnlyGuard> ReadOnlyGuard;
		if((Ctx.m_ReadOnly || Pass != ETClientCardPass::RENDER) && !pMenus->Ui()->RenderOnly())
			ReadOnlyGuard.emplace(pMenus->Ui());
		const char *pTab = pDefault->m_pDefaultTab;
		if(str_comp(pTab, "tclient") == 0)
			return pMenus->RunTClientMainCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-bind-wheel") == 0)
			return pMenus->RunTClientBindWheelCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-chat-binds") == 0)
			return pMenus->RunTClientChatBindsCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-warlist") == 0)
			return pMenus->RunTClientWarListCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-status-bar") == 0)
			return pMenus->RunTClientStatusBarCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-profiles") == 0)
			return pMenus->RunTClientProfilesCard(Ctx, pStableId, Content, Pass);
		if(str_comp(pTab, "tclient-configs") == 0)
			return pMenus->RunTClientConfigsCard(Ctx, pStableId, Content, Pass);
		return {};
	}

	uint64_t QmCardRenderHook::PrepareTClientCards(const SQmCardBuildContext &Ctx, const std::vector<const char *> &vIds)
	{
		uint64_t Revision = 0;
		for(const char *pStableId : vIds)
		{
			if(!IsTClientCard(pStableId))
				continue;
			CUIRect Content = Ctx.m_Page.m_ContentViewport;
			Revision = Revision * 1099511628211ULL ^ RunTClientCard(Ctx, pStableId, Content, ETClientCardPass::REVISION).m_Revision;
		}
		return Revision;
	}

	bool QmCardRenderHook::BuildTClientCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr || !IsTClientCard(pStableId))
			return false;
		const auto *pDefault = qm_card_registry::FindByStableId(pStableId);
		// 缓存回调仅保留注册表中的稳定 ID 和上下文值，不借用某次页面调用的局部变量。
		pStableId = pDefault->m_pStableId;
		Out = {};
		Out.m_Spec = {pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		CUIRect Content = Ctx.m_Page.m_ContentViewport;
		Out.m_MeasureRevision = RunTClientCard(Ctx, pStableId, Content, ETClientCardPass::REVISION).m_Revision;
		Out.m_Measure = [Ctx, pStableId](float Width) {
			CUIRect MeasureRect{0.0f, 0.0f, Width, Ctx.m_Page.m_ContentViewport.h};
			return RunTClientCard(Ctx, pStableId, MeasureRect, ETClientCardPass::MEASURE).m_Height;
		};
		Out.m_Render = [Ctx, pStableId](CUIRect Rect) {
			RunTClientCard(Ctx, pStableId, Rect, ETClientCardPass::RENDER);
		};
		if(!Ctx.m_ReadOnly)
			Out.m_PreLayoutInput = Ctx.m_pMenus->BuildTClientCardPreLayoutInput(pStableId);
		if(str_comp(pStableId, "tclient:tee-trails") == 0)
		{
			Out.m_HasPendingPreLayoutInput = [] {
				const int Selected = qm_tclient_cards::s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;
				return Selected >= 0 && Selected < 4;
			};
		}
		return true;
	}

} // namespace qm_card_catalog
