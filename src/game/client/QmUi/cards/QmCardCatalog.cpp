#include "QmCardCatalogInternal.h"

#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsPersonalization.h>
#include <game/client/components/menus.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace qm_card_catalog
{
	namespace
	{
		CSettingsPersonalizationState s_Personalization;

		bool ContainsStableId(const std::vector<const char *> &vStableIds, const char *pStableId)
		{
			if(pStableId == nullptr)
				return false;
			return std::any_of(vStableIds.begin(), vStableIds.end(), [pStableId](const char *pCandidate) { return str_comp(pCandidate, pStableId) == 0; });
		}
	} // namespace

	bool QmCardRenderHook::Personalization(CMenus *pMenus, CUIRect &Content, const char *pStableId, const SSettingsContentMetrics &Metrics, bool ReadOnly)
	{
		return SettingsPersonalizationSection(s_Personalization, Content, pStableId, std::max(Metrics.m_LineHeight, Metrics.m_ButtonHeight), Metrics.m_LineSpacing,
			ReadOnly || pMenus->Ui()->RenderOnly() || pMenus->m_MenuTextPlanCollecting,
			[&](const void *pId, const CUIRect &Row, bool Expanded) {
				return pMenus->DoButton_CheckBox(pId, Localize("Personal adjustments"), Expanded, &Row, Metrics.m_BodySize) != 0;
			});
	}

	void UseDeclaredModuleContent(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr || !Out.m_RenderMeasured)
			return;
		qm_settings_content::UseDeclaredContent(Out, Ctx.m_UiContext.m_pUi, QmCardRenderHook::TextRenderer(Ctx.m_pMenus));
	}
	void MakeModuleCard(
		const SQmCardBuildContext &Ctx,
		const qm_module::EQmModuleId Id,
		const char *pStableId,
		const char *pTitle,
		const char *pSubtitle,
		const FSettingsCardRenderMeasured &Render,
		SSettingsCardDefinition &Out)
	{
		// 标题与描述以 QmCardRegistry 为唯一事实源；构建期未注册时退回卡片模块自带的默认串。
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
		const char *pRegisteredTitle = pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : nullptr;
		const char *pRegisteredSubtitle = qm_card_registry::ResolveLocalizedDescription(pStableId);

		Out = {};
		Out.m_Spec = {pStableId, pRegisteredTitle != nullptr ? pRegisteredTitle : Localize(pTitle), pRegisteredSubtitle != nullptr ? pRegisteredSubtitle : Localize(pSubtitle)};
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = Render;
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
			Out.m_PreLayoutHeaderInput = [Ctx, pCollapseButtons, Index, pToggleCollapsed, pToggleUser, pOnCardExpanded, pExpandedUser, ReadOnly, Id](const SSettingsCardFrame &Frame, const bool IsCollapsed) {
				if(ReadOnly || !QmCardRenderHook::DoButtonLogic(Ctx.m_pMenus, &pCollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				pToggleCollapsed(pToggleUser, Id);
				// 展开时让页面按需让测量缓存失效（折叠态与展开态的行数口径不同）。
				if(IsCollapsed && pOnCardExpanded != nullptr)
					pOnCardExpanded(pExpandedUser, Id);
				return true;
			};
			// 自定义折叠状态由此处的回调切换，卡头只绘制与该输入处理对应的按钮。
			const IUiContext CardCtx = Ctx.m_UiContext;
			Out.m_HeaderAction = [CardCtx, pCollapseButtons, Index](const SSettingsCardFrame &Frame, const bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed, 1.0f, &pCollapseButtons[Index]); };
		}
	}

	bool BuildCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(ContainsStableId(ControlsCardStableIds(), pStableId))
			return QmCardRenderHook::BuildControlsCard(Ctx, pStableId, Out);
		if(ContainsStableId(GeneralCardStableIds(), pStableId))
			return QmCardRenderHook::BuildGeneralCard(Ctx, pStableId, Out);
		if(str_comp(pStableId, "qm:bind_editor") == 0)
			return BuildBindCard(Ctx, Out);
		if(str_comp(pStableId, "qm:bind_overview") == 0)
			return BuildBindOverviewCard(Ctx, Out);
		if(str_comp(pStableId, "qm:self_kill_protection") == 0)
			return BuildSelfKillProtectionCard(Ctx, Out);
		if(ContainsStableId(TitleCardStableIds(), pStableId))
			return QmCardRenderHook::BuildTitleCard(Ctx, pStableId, Out);
		if(ContainsStableId(NameplateCardStableIds(), pStableId))
			return QmCardRenderHook::BuildNameplateCard(Ctx, pStableId, Out);
		if(ContainsStableId(TeeCardStableIds(), pStableId))
			return QmCardRenderHook::BuildTeeCard(Ctx, pStableId, Out);
		if(IsTClientCard(pStableId))
			return QmCardRenderHook::BuildTClientCard(Ctx, pStableId, Out);
		if(IsStandardCard(pStableId))
			return QmCardRenderHook::BuildStandardCard(Ctx, pStableId, Out);
		qm_module::EQmModuleId Id = qm_module::EQmModuleId::Info;
		if(!qm_module::QmModuleIdFromStableId(pStableId, &Id))
			return false;
		if(Id == qm_module::EQmModuleId::Voice)
			return BuildFunctionCard(Ctx, Id, Out);
		if(Id == qm_module::EQmModuleId::ChatCompletion)
			return BuildChatCompletionCard(Ctx, Out);
		if(str_comp(pStableId, "qm:steam") == 0)
			return BuildSteamCard(Ctx, Id, Out);
		if(Id == qm_module::EQmModuleId::ChatBubble || Id == qm_module::EQmModuleId::EntityOverlay || ContainsStableId(VisualCardStableIds(), pStableId))
			return BuildVisualCard(Ctx, Id, Out);
		if(ContainsStableId(FunctionCardStableIds(), pStableId))
			return BuildFunctionCard(Ctx, Id, Out);
		if(Id == qm_module::EQmModuleId::SystemMediaControls || Id == qm_module::EQmModuleId::BindStatusHud || ContainsStableId(HudCardStableIds(), pStableId))
			return BuildHudCard(Ctx, Id, Out);
		return false;
	}

	namespace
	{
		// 单页最多记 4 张最慢卡片：够定位瓶颈，又不会让日志变成流水账。
		struct SCardBuildTimingEntry
		{
			const char *m_pStableId = nullptr;
			double m_Ms = 0.0;
		};
		struct SCardBuildTimingState
		{
			double m_TotalMs = 0.0;
			int m_BuildCount = 0;
			SCardBuildTimingEntry m_aSlowest[4];
		};
		SCardBuildTimingState s_CardBuildTiming;
	} // namespace

	void ResetCardBuildTimings()
	{
		s_CardBuildTiming = SCardBuildTimingState{};
	}

	void LogCardBuildTimings(const char *pPage)
	{
		if(s_CardBuildTiming.m_BuildCount == 0 || s_CardBuildTiming.m_TotalMs < 20.0)
			return;
		char aSlowest[256];
		aSlowest[0] = '\0';
		for(const auto &Entry : s_CardBuildTiming.m_aSlowest)
		{
			if(Entry.m_pStableId == nullptr)
				continue;
			char aEntry[80];
			str_format(aEntry, sizeof(aEntry), "%s=%.1fms ", Entry.m_pStableId, Entry.m_Ms);
			str_append(aSlowest, aEntry, sizeof(aSlowest));
		}
		dbg_msg("perf/cards", "page=%s build_total=%.1fms cards=%d slowest=[%s]", pPage != nullptr ? pPage : "?", s_CardBuildTiming.m_TotalMs, s_CardBuildTiming.m_BuildCount, aSlowest);
	}

	void BuildCards(const SQmCardBuildContext &Ctx, const std::vector<const char *> &vStableIds, std::vector<SSettingsCardDefinition> &vOut)
	{
		vOut.reserve(vOut.size() + vStableIds.size());
		for(const char *pStableId : vStableIds)
		{
			const int64_t CardStartNs = time_get_nanoseconds().count();
			SSettingsCardDefinition Definition;
			const bool Built = BuildCard(Ctx, pStableId, Definition);
			const double CardMs = (time_get_nanoseconds().count() - CardStartNs) / 1000000.0;
			s_CardBuildTiming.m_TotalMs += CardMs;
			s_CardBuildTiming.m_BuildCount++;
			// 插入到最慢榜（4 项降序），只在真的比榜尾快时才跳过。
			for(auto &Entry : s_CardBuildTiming.m_aSlowest)
			{
				if(CardMs > Entry.m_Ms)
				{
					for(size_t Index = std::size(s_CardBuildTiming.m_aSlowest) - 1; Index > 0; --Index)
						s_CardBuildTiming.m_aSlowest[Index] = s_CardBuildTiming.m_aSlowest[Index - 1];
					Entry = SCardBuildTimingEntry{};
					s_CardBuildTiming.m_aSlowest[0] = SCardBuildTimingEntry{pStableId, CardMs};
					break;
				}
			}
			if(Built)
				vOut.push_back(std::move(Definition));
		}
	}

} // namespace qm_card_catalog
