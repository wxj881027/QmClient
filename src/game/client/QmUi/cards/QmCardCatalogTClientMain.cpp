#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

FSettingsCardPreLayoutInput CMenus::BuildTClientCardPreLayoutInput(const char *pStableId)
{
	const auto BuildTClientConditionalRowsPreLayoutInput = [this](const char *pStableCardId) -> FSettingsCardPreLayoutInput {
		const auto ProcessToggle = [this](const CUIRect &Row, int *pValue) {
			if(!Ui()->DoButtonLogic(pValue, 0, &Row, BUTTONFLAG_LEFT))
				return false;
			*pValue ^= 1;
			return true;
		};
		if(str_comp(pStableCardId, "tclient:visual-nameplates") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				Rows.Next();
				Rows.Next();
				Rows.Next();
				Rows.Next();
				Rows.Next();
				Rows.Next();
				return ProcessToggle(Rows.Next(), &g_Config.m_QmWhiteFeet);
			};
		}
		if(str_comp(pStableCardId, "tclient:anti-latency-tools") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				Rows.Next();
				bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmRemoveAnti);
				if(g_Config.m_QmRemoveAnti)
				{
					Rows.Next();
					Rows.Next();
				}
				Rows.Next();
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmPredMarginInFreeze) || Changed;
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:auto-reply") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmAutoReplyMuted);
				if(g_Config.m_QmAutoReplyMuted)
					Rows.Next();
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmAutoReplyMinimized) || Changed;
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:player-indicator") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				for(int RowIndex = 0; RowIndex < 5; ++RowIndex)
					Rows.Next();
				bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmWarListIndicator);

				Rows.Next();
				Rows.Next();
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmIndicatorVariableDistance) || Changed;
				Rows.Next();
				if(g_Config.m_QmIndicatorVariableDistance)
				{
					Rows.Next();
					Rows.Next();
				}

				if(g_Config.m_QmWarListIndicator)
				{
					Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmWarListIndicatorColors) || Changed;
					Rows.Next();
					Rows.Next();
					Rows.Next();
				}
				if(!g_Config.m_QmWarListIndicator || !g_Config.m_QmWarListIndicatorColors)
				{
					Rows.Next();
					Rows.Next();
					Rows.Next();
				}
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:tee-status-bar") == 0)
		{
			return [this](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				for(int RowIndex = 0; RowIndex < 5; ++RowIndex)
					Rows.Next();

				CUIRect ShowFrozenTextRow = Rows.Next();
				bool Changed = false;
				if(Ui()->DoButtonLogic(&g_Config.m_QmShowFrozenText, g_Config.m_QmShowFrozenText >= 1, &ShowFrozenTextRow, BUTTONFLAG_LEFT))
				{
					g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText >= 1 ? 0 : 1;
					Changed = true;
				}
				if(g_Config.m_QmShowFrozenText)
				{
					CUIRect CountFrozenTextRow = Rows.Next();
					if(Ui()->DoButtonLogic(&s_CountFrozenText, g_Config.m_QmShowFrozenText == 2, &CountFrozenTextRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText != 2 ? 2 : 1;
						Changed = true;
					}
				}
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:finish-name") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				const CUIRect ToggleRow = Rows.Next();
				const bool Changed = ProcessToggle(ToggleRow, &g_Config.m_QmChangeNameNearFinish);
				if(g_Config.m_QmChangeNameNearFinish)
					Rows.Next();
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:hud") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				Rows.Next();
				Rows.Next();
				bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmRenderCursorSpec);
				if(g_Config.m_QmRenderCursorSpec)
					Rows.Next();
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmNotifyWhenLast) || Changed;
				if(g_Config.m_QmNotifyWhenLast)
				{
					Rows.Next();
					Rows.Next();
					Rows.Next();
					Rows.Next();
				}
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmShowCenter) || Changed;
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:visual-effects") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(Content.w);
				const CUIRect TinyTeeModeRow = Rows.Next(ResolveSettingsRadioRowLayout(Content, 3, ContentMetrics).m_Height);
				const SSettingsRadioRowLayout TinyTeeModeLayout = ResolveSettingsRadioRowLayout(TinyTeeModeRow, 3, ContentMetrics);
				CUIRect TinyTeeModeButtons = TinyTeeModeLayout.m_ButtonsRect;
				int TinyTeeMode = g_Config.m_QmTinyTees ? (g_Config.m_QmTinyTeesOthers ? 2 : 1) : 0;
				bool Changed = false;
				const float ButtonWidth = TinyTeeModeButtons.w / s_vTinyTeeModeButtons.size();
				for(int Index = 0; Index < (int)s_vTinyTeeModeButtons.size(); ++Index)
				{
					CUIRect RadioButton;
					TinyTeeModeButtons.VSplitLeft(ButtonWidth, &RadioButton, &TinyTeeModeButtons);
					if(Ui()->DoButtonLogic(&s_vTinyTeeModeButtons[Index], Index == TinyTeeMode, &RadioButton, BUTTONFLAG_LEFT))
					{
						g_Config.m_QmTinyTees = Index > 0 ? 1 : 0;
						g_Config.m_QmTinyTeesOthers = Index > 1 ? 1 : 0;
						Changed = true;
					}
				}
				if(g_Config.m_QmTinyTees > 0)
					Rows.Next();
				return ProcessToggle(Rows.Next(), &g_Config.m_QmJellyTee) || Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:input") == 0)
		{
			return [this, ProcessToggle](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CTClientSettingsRowAllocator Rows(Content);
				bool Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmFastInput);
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmAutoMargin) || Changed;

				Rows.Next();
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_QmFastInputOthers) || Changed;
				Changed = ProcessToggle(Rows.Next(), &g_Config.m_ClSubTickAiming) || Changed;
				return Changed;
			};
		}
		if(str_comp(pStableCardId, "tclient:tee-trails") == 0)
		{
			return [](CUIRect) {
				// 下拉弹层先于卡片内容绘制写入选择项；这里仅提前提交选择，
				// 正式 DoSettingsDropDown 仍负责清理状态和绘制弹层。
				const int Selected = s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;
				if(Selected < 0 || Selected >= 4)
					return false;
				const int NewColorMode = Selected + 1;
				if(g_Config.m_QmTeeTrailColorMode == NewColorMode)
					return false;
				g_Config.m_QmTeeTrailColorMode = NewColorMode;
				return true;
			};
		}
		return {};
	};
	return BuildTClientConditionalRowsPreLayoutInput(pStableId);
}

qm_card_catalog::STClientCardResult CMenus::RunTClientMainCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	uint64_t Revision = HashTClientSettingsCardLayout(pStableId);
	if(str_comp(pStableId, "tclient:font") == 0)
		Revision = HashValueFnv1a64(Revision, TextRender()->GetCustomFaces()->size());
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, Revision};
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const bool Render = Pass == qm_card_catalog::ETClientCardPass::RENDER;
	if(str_comp(pStableId, "tclient:visual-nameplates") == 0)
		return {LayoutTClientVisualNameplateCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:visual-effects") == 0)
		return {LayoutTClientVisualEffectsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:input") == 0)
		return {LayoutTClientInputCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:anti-latency-tools") == 0)
		return {LayoutTClientAntiLatencyToolsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:improved-anti-ping") == 0)
		return {LayoutTClientAntiPingSmoothingCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:execute-on-join") == 0)
		return {LayoutTClientAutoExecuteCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:voting") == 0)
		return {LayoutTClientVotingCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:player-indicator") == 0)
		return {LayoutTClientPlayerIndicatorCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tee-status-bar") == 0)
		return {LayoutTClientTeeStatusBarCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tile-outlines") == 0)
		return {LayoutTClientTileOutlinesCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:ghost-tools") == 0)
		return {LayoutTClientGhostToolsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:rainbow") == 0)
		return {LayoutTClientRainbowCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:tee-trails") == 0)
		return {LayoutTClientTeeTrailsCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:background-draw") == 0)
		return {LayoutTClientBackgroundDrawCard(Ctx, Content, Render), Revision};
	if(str_comp(pStableId, "tclient:finish-name") == 0)
		return {LayoutTClientFinishNameCard(Ctx, Content, Render), Revision};
	SSettingsSection Section;
	if(str_comp(pStableId, "tclient:font") == 0)
		Section = BuildTClientThemeCacheSection();
	else if(str_comp(pStableId, "tclient:cursor") == 0)
		Section = BuildTClientCursorCacheSection();
	else if(str_comp(pStableId, "tclient:auto-reply") == 0)
		Section = BuildTClientAutoReplyCacheSection();
	else if(str_comp(pStableId, "tclient:pet") == 0)
		Section = BuildTClientPetCacheSection();
	else if(str_comp(pStableId, "tclient:hud") == 0)
		Section = BuildTClientHudCacheSection();
	else
		return {0.0f, Revision};
	CUIRect Measured = Content;
	const float Height = Section.m_MeasureFn ? Section.m_MeasureFn(Measured) : 0.0f;
	if(Render && Section.m_RenderFullFn)
		Section.m_RenderFullFn(Content);
	return {Height, Revision};
}
