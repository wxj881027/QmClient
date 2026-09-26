#include "QmCardCatalogInternal.h"

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

namespace qm_card_catalog
{
	namespace
	{
		bool ContainsStableId(const std::vector<const char *> &vStableIds, const char *pStableId)
		{
			if(pStableId == nullptr)
				return false;
			return std::any_of(vStableIds.begin(), vStableIds.end(), [pStableId](const char *pCandidate) { return str_comp(pCandidate, pStableId) == 0; });
		}

		uint64_t FoldRevision(uint64_t Hash, const uint64_t Revision)
		{
			return Hash * 1099511628211ULL ^ Revision;
		}
	} // namespace

	void MakeModuleCard(
		const SQmCardBuildContext &Ctx,
		const qm_module::EQmModuleId Id,
		const char *pStableId,
		const char *pTitle,
		const char *pSubtitle,
		const FSettingsCardRenderMeasured &Render,
		FSettingsCardMeasure Measure,
		const uint64_t MeasureRevision,
		FSettingsCardPreLayoutInput PreLayoutInput,
		SSettingsCardDefinition &Out)
	{
		// 标题与描述以 QmCardRegistry 为唯一事实源；构建期未注册时退回卡片模块自带的默认串。
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
		const char *pRegisteredTitle = pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : nullptr;
		const char *pRegisteredSubtitle = qm_card_registry::ResolveLocalizedDescription(pStableId);

		Out = {};
		Out.m_Spec = {pStableId, pRegisteredTitle != nullptr ? pRegisteredTitle : Localize(pTitle), pRegisteredSubtitle != nullptr ? pRegisteredSubtitle : Localize(pSubtitle)};
		Out.m_Measure = std::move(Measure);
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		if(Ctx.m_pCollapsed != nullptr)
		{
			const int Index = std::clamp((int)Id, 0, (int)qm_module::QmModuleCount - 1);
			Out.m_IsCollapsed = [pCollapsed = Ctx.m_pCollapsed, Index] { return pCollapsed[Index]; };
		}
		if(Ctx.m_pCollapseButtons != nullptr && Ctx.m_pToggleCollapsed != nullptr)
		{
			const int Index = std::clamp((int)Id, 0, (int)qm_module::QmModuleCount - 1);
			CButtonContainer *pCollapseButtons = Ctx.m_pCollapseButtons;
			void (*pToggleCollapsed)(void *, qm_module::EQmModuleId) = Ctx.m_pToggleCollapsed;
			void *pToggleUser = Ctx.m_pToggleCollapsedUser;
			void (*pOnCardExpanded)(void *, qm_module::EQmModuleId) = Ctx.m_pOnCardExpanded;
			void *pExpandedUser = Ctx.m_pOnCardExpandedUser;
			const bool ReadOnly = Ctx.m_ReadOnly;
			CMenus *pMenus = Ctx.m_pMenus;
			Out.m_PreLayoutHeaderInput = [pMenus, pCollapseButtons, Index, pToggleCollapsed, pToggleUser, pOnCardExpanded, pExpandedUser, ReadOnly, Id](const SSettingsCardFrame &Frame, const bool IsCollapsed) {
				if(ReadOnly || !QmCardRenderHook::DoButtonLogic(pMenus, &pCollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				pToggleCollapsed(pToggleUser, Id);
				// 展开时让页面按需让测量缓存失效（折叠态与展开态的行数口径不同）。
				if(IsCollapsed && pOnCardExpanded != nullptr)
					pOnCardExpanded(pExpandedUser, Id);
				return true;
			};
			// 自定义折叠状态由此处的回调切换，卡头只绘制与该输入处理对应的按钮。
			const IUiContext CardCtx = Ctx.m_UiContext;
			Out.m_HeaderAction = [CardCtx](const SSettingsCardFrame &Frame, const bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
		}
		Out.m_MeasureRevision = MeasureRevision;
		Out.m_PreLayoutInput = std::move(PreLayoutInput);
	}

	bool BuildCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		qm_module::EQmModuleId Id = qm_module::EQmModuleId::Info;
		if(!qm_module::QmModuleIdFromStableId(pStableId, &Id))
			return false;
		if(str_comp(pStableId, "qm:steam") == 0)
			return BuildSteamCard(Ctx, Id, Out);
		if(ContainsStableId(VisualCardStableIds(), pStableId))
			return BuildVisualCard(Ctx, Id, Out);
		if(ContainsStableId(FunctionCardStableIds(), pStableId))
			return BuildFunctionCard(Ctx, Id, Out);
		if(ContainsStableId(HudCardStableIds(), pStableId))
			return BuildHudCard(Ctx, Id, Out);
		return false;
	}

	void BuildCards(const SQmCardBuildContext &Ctx, const std::vector<const char *> &vStableIds, std::vector<SSettingsCardDefinition> &vOut)
	{
		vOut.reserve(vOut.size() + vStableIds.size());
		for(const char *pStableId : vStableIds)
		{
			SSettingsCardDefinition Definition;
			if(BuildCard(Ctx, pStableId, Definition))
				vOut.push_back(std::move(Definition));
		}
	}

	std::vector<SQmSearchResultEntry> SearchResultEntries(const char *pQuery, const qm_card_order::CModel &Model)
	{
		std::vector<SQmSearchResultEntry> vEntries;
		std::vector<qm_card_registry::SCardSearchResult> vMatches;
		if(pQuery != nullptr && pQuery[0] != '\0')
		{
			vMatches = qm_card_registry::SearchCards(pQuery, Model);
		}
		else
		{
			// 空查询列出全部可构造卡片，供用户浏览（搜索页即卡片目录本身）。
			vMatches.reserve(qm_card_registry::Defaults().size());
			for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
			{
				qm_card_registry::SCardSearchResult Match;
				Match.m_pStableId = Default.m_pStableId;
				Match.m_Title = Default.m_pTitle != nullptr ? Localize(Default.m_pTitle) : "";
				Match.m_Description = Default.m_pDescription != nullptr ? Localize(Default.m_pDescription) : "";
				Match.m_Target = qm_card_registry::ResolveCardNavigationTarget(Default, Model);
				vMatches.push_back(std::move(Match));
			}
		}

		vEntries.reserve(vMatches.size());
		for(qm_card_registry::SCardSearchResult &Match : vMatches)
		{
			// 只有真正有卡片模块的 stableId 才能在搜索页就地渲染，其余（已下线/纯占位）不展示。
			if(!HasCardModule(Match.m_pStableId))
				continue;
			const char *pTab = Match.m_Target.m_pTab;
			if(pTab != nullptr && str_comp(pTab, "global-search") == 0)
				continue;
			SQmSearchResultEntry Entry;
			Entry.m_pStableId = Match.m_pStableId;
			Entry.m_pTab = pTab;
			Entry.m_Title = std::move(Match.m_Title);
			Entry.m_Description = std::move(Match.m_Description);
			vEntries.push_back(std::move(Entry));
		}
		return vEntries;
	}

	std::vector<qm_card_order::SEntry> BuildSearchModelEntries(const std::vector<SQmSearchResultEntry> &vResults)
	{
		// 搜索结果每张卡独占整行：Full 列 + 顺序即匹配顺序，搜索页不参与分类内的拖拽排序。
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vResults.size());
		for(size_t Index = 0; Index < vResults.size(); ++Index)
			vEntries.push_back({vResults[Index].m_pStableId, "global-search", 0, (int)Index});
		return vEntries;
	}
} // namespace qm_card_catalog
