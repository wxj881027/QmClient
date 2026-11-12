#include "QmCardCatalog.h"
#include "QmCardCatalogInternal.h"
#include "QmCardCatalogTClientInternal.h"
#include "QmTClientDeclaredRegions.h"

#include <base/str.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <optional>

namespace qm_tclient_cards
{
	// 多列专用区块保留各自原点；普通控件仍用子 Flow 的不可变行快照。
	// 状态由 builder 持有，搜索页与原设置页不会互相覆盖输入快照。
	float TClientDeclaredRegion(const char *pId, float Width, const std::function<void(CUIRect &)> &Render, CUIRect *pRect)
	{
		auto *pRegions = CDeclaredRegionsScope::Current();
		dbg_assert(pRegions != nullptr, "missing TClient declaration owner");
		Width = std::isfinite(Width) ? std::max(0.0f, Width) : 0.0f;
		auto &Region = pRegions->m_Regions[pId];
		const auto *pScope = CSettingsContentLayoutScope::Current();
		if(pScope && pScope->m_Pass == ESettingsContentPass::DECLARE)
		{
			if(pRect == nullptr)
			{
				std::vector<SSettingsContentRowDeclaration> vRows;
				CUIRect Content{0, 0, Width, 0};
				{
					qm_settings_content::CContentDeclarationUiGuard Guard(pRegions->m_pUi);
					CSettingsContentLayoutScope Scope(vRows);
					Render(Content);
				}
				auto *pText = pRegions->m_pText;
				Region.m_pLayout = Region.m_Cache.Resolve(vRows, Width, pText ? pText->GlyphAtlasRevision() : 0, [pText](const char *pValue, float TextWidth, float FontSize) { return pText ? pText->TextBoundingBox(FontSize, pValue, -1, std::max(1.0f, TextWidth)).m_H + FontSize * 0.25f : FontSize; }, [pText](const SSettingsContentRowDeclaration &Row, float TextWidth) {
						const char *pValue = Row.m_Text.empty() ? " " : Row.m_Text.c_str();
						return pText ? pText->TextBoundingBox(Row.m_FontSize, pValue, -1, std::max(1.0f, TextWidth - 2.0f * Row.m_TextPadding), Row.m_TextLineSpacing).m_H + 2.0f * Row.m_TextPadding : Row.m_MinimumHeight; });
			}
		}
		else if(pRect && Region.m_pLayout)
		{
			if(Region.m_pLayout->Width() <= 0.0f || !(pRect->w > 0.0f))
				return Region.m_pLayout->Height();
			auto Pass = pScope ? pScope->m_Pass : ESettingsContentPass::DRAW;
			if(Pass == ESettingsContentPass::INPUT && (pRegions->m_ReadOnly || (pRegions->m_pUi && pRegions->m_pUi->RenderOnly())))
				return Region.m_pLayout->Height();
			std::optional<CUiScopedBackgroundRender> Guard;
			if(Pass == ESettingsContentPass::DRAW && pRegions->m_pUi)
				Guard.emplace(pRegions->m_pUi, true);
			CSettingsContentLayoutScope Scope(*Region.m_pLayout, Pass);
			Render(*pRect);
		}
		return Region.m_pLayout ? Region.m_pLayout->Height() : 0.0f;
	}
} // namespace qm_tclient_cards

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
		auto pRegions = std::make_shared<qm_tclient_cards::SDeclaredRegions>();
		pRegions->m_pUi = Ctx.m_pMenus->Ui();
		pRegions->m_pText = Ctx.m_pMenus->TextRender();
		pRegions->m_ReadOnly = Ctx.m_ReadOnly;
		Out.m_RenderMeasured = [Ctx, pStableId, pRegions](CUIRect &Rect) {
			const qm_tclient_cards::CDeclaredRegionsScope Scope(*pRegions);
			RunTClientCard(Ctx, pStableId, Rect, ETClientCardPass::RENDER);
		};
		if(!Ctx.m_ReadOnly && str_comp(pStableId, "tclient:tee-trails") == 0)
		{
			Out.m_HasPendingPreLayoutInput = [] {
				const int Selected = qm_tclient_cards::s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;
				return Selected >= 0 && Selected < 4;
			};
		}
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}

} // namespace qm_card_catalog
