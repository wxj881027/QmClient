#include "QmCardCatalog.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace qm_card_catalog
{
	bool QmCardRenderHook::BuildNameplateCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr || pStableId == nullptr)
			return false;
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
		if(pDefault == nullptr)
			return false;

		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const IUiContext CardCtx = Ctx.m_UiContext;
		constexpr int Page = CMenus::SETTINGS_APPEARANCE;
		constexpr int Tab = CMenus::APPEARANCE_TAB_NAME_PLATE;
		SLabelProperties SingleLineProps;
		SingleLineProps.m_DisallowNewline = true;
		SingleLineProps.m_StopAtEnd = true;
		SingleLineProps.m_MinimumFontSize = 6.0f;
		const auto NextRow = [Metrics](CUIRect &Content) {
			CUIRect Row;
			Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
			Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
			return Row;
		};
		const auto NumericField = [pMenus, Metrics, CardCtx, NextRow](CUIRect &Content, const char *pTextId, const char *pLabel, int *pValue, int Min, int Max) {
			const CUIRect Row = NextRow(Content);
			ui_widget::SNumericFieldOptions Options;
			Options.m_pLabel = pLabel;
			Options.m_FontSize = Metrics.m_BodySize;
			Options.m_LabelAlign = TEXTALIGN_ML;
			if(pMenus->PrepareSettingsNumericFieldLabel(Page, Tab, -1, pTextId, Row, pLabel, 0u, Options))
				return;
			ui_widget::NumericField(CardCtx, pMenus->GetSettingsNumericFieldState(pValue), pValue, pValue, Min, Max, Row, Options);
		};
		const auto CheckBox = [pMenus, Metrics](CUIRect &Content, const char *pTextId, const char *pLabel, int *pValue) {
			pMenus->DoSettingsButton_CheckBoxAutoVMarginAndSet(Page, Tab, pValue, pTextId, pLabel, pValue, &Content, Metrics.m_LineHeight, 0.0f, Metrics.m_BodySize);
			Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
		};
		const auto ProcessToggle = [pMenus, NextRow](CUIRect &Content, int *pValue) {
			const CUIRect Row = NextRow(Content);
			if(pMenus->TemporaryOverrideTooltip(pValue) != nullptr)
				return false;
			if(!pMenus->Ui()->DoButtonLogic(pValue, 0, &Row, BUTTONFLAG_LEFT))
				return false;
			*pValue ^= 1;
			return true;
		};

		FSettingsCardRenderMeasured Render;
		FSettingsCardPreLayoutInput PreLayoutInput;
		if(str_comp(pStableId, "deck:appearance-name-plate-settings") == 0)
		{
			Render = [=](CUIRect &Content) {
				const int *pOverrideSource = pMenus->TemporaryOverrideTooltip(&g_Config.m_QmNameplateShowScope) != nullptr ? &g_Config.m_QmNameplateShowScope : nullptr;
				int ShowScope = std::clamp(g_Config.m_QmNameplateShowScope, 0, QM_NAMEPLATE_SHOW_SCOPE_COUNT - 1);
				if(pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, Content, "appearance-show-name-plates-label", Localize("Show name plates"),
					   pMenus->m_vButtonContainersNamePlateShow,
					   {"appearance-show-name-plates-none", "appearance-show-name-plates-current", "appearance-show-name-plates-local", "appearance-show-name-plates-others", "appearance-show-name-plates-others-local", "appearance-show-name-plates-all"},
					   {Localize("None", "Show name plates"), Localize("Current", "Show name plates"), Localize("Own characters", "Show name plates"), Localize("Others", "Show name plates"), Localize("Others and own", "Show name plates"), Localize("All", "Show name plates")},
					   {QM_NAMEPLATE_SHOW_SCOPE_OFF, QM_NAMEPLATE_SHOW_SCOPE_CURRENT, QM_NAMEPLATE_SHOW_SCOPE_LOCAL, QM_NAMEPLATE_SHOW_SCOPE_OTHERS, QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL, QM_NAMEPLATE_SHOW_SCOPE_ALL},
					   ShowScope, Metrics, pOverrideSource, &g_Config.m_QmNameplateShowScope))
					g_Config.m_QmNameplateShowScope = ShowScope;
				Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);

				NumericField(Content, "appearance-name-plates-size", Localize("Name plates size"), &g_Config.m_ClNamePlatesSize, -50, 100);
				NumericField(Content, "appearance-name-plates-offset", Localize("Name plates offset"), &g_Config.m_ClNamePlatesOffset, 10, 50);
				CheckBox(Content, "appearance-show-clan-above-name-plates", Localize("Show clan above name plates"), &g_Config.m_ClNamePlatesClan);
				if(g_Config.m_ClNamePlatesClan)
					NumericField(Content, "appearance-clan-plates-size", Localize("Clan plates size"), &g_Config.m_ClNamePlatesClanSize, -50, 100);
				NumericField(Content, "appearance-coords-size", Localize("Coords size"), &g_Config.m_ClNamePlatesCoordsSize, -50, 100);
				CheckBox(Content, "appearance-name-plates-team-colors", Localize("Use team colors for name plates"), &g_Config.m_ClNamePlatesTeamcolors);
				CheckBox(Content, "appearance-show-friend-icon-name-plates", Localize("Show friend icon in name plates"), &g_Config.m_ClNamePlatesFriendMark);
				CheckBox(Content, "appearance-show-client-ids-name-plates", Localize("Show client IDs in name plates"), &g_Config.m_ClNamePlatesIds);
				if(g_Config.m_ClNamePlatesIds)
				{
					CheckBox(Content, "appearance-client-ids-separate-line", Localize("Show client IDs on a separate line"), &g_Config.m_ClNamePlatesIdsSeparateLine);
					if(g_Config.m_ClNamePlatesIdsSeparateLine)
						NumericField(Content, "appearance-client-ids-size", Localize("Client IDs size"), &g_Config.m_ClNamePlatesIdsSize, -50, 100);
				}
			};
			PreLayoutInput = [=](CUIRect Content) {
				const float RadioHeight = ResolveSettingsRadioRowLayout(Content, QM_NAMEPLATE_SHOW_SCOPE_COUNT, Metrics).m_Height;
				Content.HSplitTop(RadioHeight + Metrics.m_LineSpacing, nullptr, &Content);
				NextRow(Content);
				NextRow(Content);
				bool Changed = ProcessToggle(Content, &g_Config.m_ClNamePlatesClan);
				if(g_Config.m_ClNamePlatesClan)
					NextRow(Content);
				NextRow(Content);
				NextRow(Content);
				NextRow(Content);
				Changed = ProcessToggle(Content, &g_Config.m_ClNamePlatesIds) || Changed;
				if(g_Config.m_ClNamePlatesIds)
					Changed = ProcessToggle(Content, &g_Config.m_ClNamePlatesIdsSeparateLine) || Changed;
				return Changed;
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-text") == 0)
		{
			Render = [=](CUIRect &Content) {
				CheckBox(Content, "appearance-nameplate-advanced", Localize("Show advanced options"), &g_Config.m_QmNameplateAdvanced);
				if(!g_Config.m_QmNameplateAdvanced)
					return;

				const auto EffectToggle = [&](int Effect, const char *pTextId, const char *pLabel) {
					CUIRect Row = NextRow(Content);
					const bool Enabled = (g_Config.m_QmNameplateTextEffects & Effect) != 0;
					if(pMenus->DoSettingsButton_CheckBox(Page, Tab, Tab, pTextId, pTextId, pLabel, Enabled, &Row, SingleLineProps))
					{
						g_Config.m_QmNameplateTextEffects ^= Effect;
						if(!Enabled && Effect == QM_TEXT_EFFECT_GLOW && g_Config.m_QmNameplateTextGlowRange == 0)
							g_Config.m_QmNameplateTextGlowRange = 4;
					}
				};
				EffectToggle(QM_TEXT_EFFECT_BORDER, "appearance-nameplate-text-border", Localize("Border"));
				EffectToggle(QM_TEXT_EFFECT_GRADIENT, "appearance-nameplate-text-gradient", Localize("Gradient"));
				EffectToggle(QM_TEXT_EFFECT_RAINBOW, "appearance-nameplate-text-rainbow", Localize("Rainbow"));
				EffectToggle(QM_TEXT_EFFECT_GLOW, "appearance-nameplate-text-glow", Localize("Glow"));

				const auto ControlRow = [&](const char *pTextId, const char *pLabel, const auto &RenderControl) {
					CUIRect Row = NextRow(Content);
					CUIRect Label, Control;
					Row.VSplitLeft(std::min(150.0f, Row.w * 0.42f), &Label, &Control);
					SLabelProperties Props;
					Props.m_DisallowNewline = true;
					Props.m_StopAtEnd = true;
					Props.m_MinimumFontSize = 6.0f;
					Props.m_MaxWidth = Label.w;
					CUIElement &LabelElement = pMenus->SettingsTextElement(Page, Tab, pTextId);
					pMenus->DoSettingsLabelStreamed(LabelElement, &Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML, Props);
					RenderControl(Control);
				};
				const auto DropDown = [&](const char *pTextId, const char *pLabel, int *pValue, int Max, std::vector<const char *> &vNames, CUi::SDropDownState &State, CScrollRegion &ScrollRegion) {
					ControlRow(pTextId, pLabel, [&](CUIRect &Control) {
						State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
						*pValue = pMenus->DoSettingsDropDown(&Control, std::clamp(*pValue, 0, Max), vNames.data(), (int)vNames.size(), State, {}, pValue);
					});
				};
				static std::vector<const char *> s_vPlayingNames;
				s_vPlayingNames = {Localize("Off"), Localize("Self only"), Localize("Others only"), Localize("Friends only"), Localize("Self and friends"), Localize("All players")};
				static CUi::SDropDownState s_PlayingState;
				static CScrollRegion s_PlayingScroll;
				DropDown("appearance-nameplate-text-playing-effects", Localize("Playing effects"), &g_Config.m_QmNameplateTextPlayingScope, 5, s_vPlayingNames, s_PlayingState, s_PlayingScroll);
				static std::vector<const char *> s_vSpectateNames;
				s_vSpectateNames = {Localize("Off"), Localize("Spectated player"), Localize("Others only"), Localize("Friends only"), Localize("Spectated player and friends"), Localize("All players")};
				static CUi::SDropDownState s_SpectateState;
				static CScrollRegion s_SpectateScroll;
				DropDown("appearance-nameplate-text-spectate-effects", Localize("Spectate effects"), &g_Config.m_QmNameplateTextSpectateScope, 5, s_vSpectateNames, s_SpectateState, s_SpectateScroll);
				static std::vector<const char *> s_vDemoNames;
				s_vDemoNames = {Localize("Off"), Localize("Smart"), Localize("Manual target"), Localize("Manual scope")};
				static CUi::SDropDownState s_DemoState;
				static CScrollRegion s_DemoScroll;
				DropDown("appearance-nameplate-text-demo-effects", Localize("Demo effects"), &g_Config.m_QmNameplateTextDemoMode, 3, s_vDemoNames, s_DemoState, s_DemoScroll);

				static std::vector<std::string> s_vDemoTargetStorage;
				static std::vector<const char *> s_vDemoTargetNames;
				s_vDemoTargetStorage.clear();
				s_vDemoTargetNames.clear();
				s_vDemoTargetStorage.emplace_back(Localize("None"));
				bool DemoTargetListed = g_Config.m_QmNameplateTextDemoTarget < 0;
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
				{
					if(!pMenus->GameClient()->m_Snap.m_apPlayerInfos[ClientId])
						continue;
					char aClientName[128];
					str_format(aClientName, sizeof(aClientName), "%d: %s", ClientId, pMenus->GameClient()->m_aClients[ClientId].m_aName);
					s_vDemoTargetStorage.emplace_back(aClientName);
					if(ClientId == g_Config.m_QmNameplateTextDemoTarget)
						DemoTargetListed = true;
				}
				if(!DemoTargetListed)
				{
					char aClientName[128];
					str_format(aClientName, sizeof(aClientName), "%d: -", g_Config.m_QmNameplateTextDemoTarget);
					s_vDemoTargetStorage.emplace_back(aClientName);
				}
				for(const std::string &Name : s_vDemoTargetStorage)
					s_vDemoTargetNames.push_back(Name.c_str());
				static CUi::SDropDownState s_DemoTargetState;
				static CScrollRegion s_DemoTargetScroll;
				s_DemoTargetState.m_SelectionPopupContext.m_pScrollRegion = &s_DemoTargetScroll;
				int DemoTargetSelection = 0;
				for(size_t Index = 1; Index < s_vDemoTargetStorage.size(); ++Index)
				{
					int ClientId = -1;
					if(sscanf(s_vDemoTargetStorage[Index].c_str(), "%d:", &ClientId) == 1 && ClientId == g_Config.m_QmNameplateTextDemoTarget)
					{
						DemoTargetSelection = (int)Index;
						break;
					}
				}
				ControlRow("appearance-nameplate-text-demo-target", Localize("Demo target"), [&](CUIRect &Control) {
					const int Selection = pMenus->DoSettingsDropDown(&Control, DemoTargetSelection, s_vDemoTargetNames.data(), (int)s_vDemoTargetNames.size(), s_DemoTargetState, {}, &g_Config.m_QmNameplateTextDemoTarget);
					if(Selection == 0)
						g_Config.m_QmNameplateTextDemoTarget = -1;
					else if(Selection > 0 && Selection < (int)s_vDemoTargetStorage.size())
						sscanf(s_vDemoTargetStorage[Selection].c_str(), "%d:", &g_Config.m_QmNameplateTextDemoTarget);
				});

				NumericField(Content, "appearance-nameplate-text-border-range", Localize("Border range"), &g_Config.m_QmNameplateTextBorderRange, 1, 4);
				NumericField(Content, "appearance-nameplate-text-glow-range", Localize("Glow range"), &g_Config.m_QmNameplateTextGlowRange, 1, 12);
				CheckBox(Content, "appearance-nameplate-text-auto-lod", Localize("Automatic effect LOD when crowded"), &g_Config.m_QmNameplateEffectAutoLod);
				if(g_Config.m_QmNameplateEffectAutoLod)
					NumericField(Content, "appearance-nameplate-text-lod-threshold", Localize("Full quality nameplate count"), &g_Config.m_QmNameplateEffectLodThreshold, 4, 64);
				static CButtonContainer s_BorderColor, s_GradientColor, s_GlowColor;
				pMenus->DoLine_ColorPicker(&s_BorderColor, Metrics, &Content, Localize("Border color"), &g_Config.m_QmNameplateTextBorderColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), false, nullptr, true);
				pMenus->DoLine_ColorPicker(&s_GradientColor, Metrics, &Content, Localize("Gradient color"), &g_Config.m_QmNameplateTextGradientColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, true);
				pMenus->DoLine_ColorPicker(&s_GlowColor, Metrics, &Content, Localize("Glow color"), &g_Config.m_QmNameplateTextGlowColor, ColorRGBA(0.30f, 0.78f, 1.0f, 0.40f), false, nullptr, true);
			};
			PreLayoutInput = [=](CUIRect Content) {
				const bool Changed = ProcessToggle(Content, &g_Config.m_QmNameplateAdvanced);
				if(!g_Config.m_QmNameplateAdvanced || Changed)
					return Changed;
				for(int Row = 0; Row < 10; ++Row)
					NextRow(Content);
				return ProcessToggle(Content, &g_Config.m_QmNameplateEffectAutoLod);
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-hook-strength") == 0)
		{
			Render = [=](CUIRect &Content) {
				CUIRect Row = NextRow(Content);
				if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &g_Config.m_ClNamePlatesStrong, "appearance-show-hook-strength-icon", Localize("Show hook strength icon indicator"), g_Config.m_ClNamePlatesStrong, &Row, SingleLineProps))
					g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
				if(!g_Config.m_ClNamePlatesStrong)
					return;
				static int s_StrongNumberId;
				Row = NextRow(Content);
				pMenus->Ui()->DoConfigTooltip(&s_StrongNumberId, &Row, &g_Config.m_ClNamePlatesStrong);
				if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &s_StrongNumberId, "appearance-show-hook-strength-number", Localize("Show hook strength number indicator"), g_Config.m_ClNamePlatesStrong == 2, &Row, SingleLineProps))
					g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong != 2 ? 2 : 1;
				pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, Content, "appearance-hook-strength-scope-label", Localize("Hook strength scope"),
					pMenus->m_vButtonContainersNamePlateHookStrongWeakScope,
					{"appearance-hook-strength-scope-self", "appearance-hook-strength-scope-others", "appearance-hook-strength-scope-strong", "appearance-hook-strength-scope-weak", "appearance-hook-strength-scope-all"},
					{Localize("Self"), Localize("Others"), Localize("Strong hook"), Localize("Weak hook"), Localize("All")},
					{QM_HOOK_STRONG_WEAK_SCOPE_SELF, QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, QM_HOOK_STRONG_WEAK_SCOPE_STRONG, QM_HOOK_STRONG_WEAK_SCOPE_WEAK, QM_HOOK_STRONG_WEAK_SCOPE_ALL},
					g_Config.m_QmNameplateHookStrongWeakScope, Metrics);
				Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
				static CButtonContainer s_StrongColor, s_WeakColor;
				pMenus->DoLine_ColorPicker(&s_StrongColor, Metrics, &Content, Localize("Strong hook color"), &g_Config.m_QmNameplateStrongHookColor, color_cast<ColorRGBA>(ColorHSLA(6401973)), false);
				pMenus->DoLine_ColorPicker(&s_WeakColor, Metrics, &Content, Localize("Weak hook color"), &g_Config.m_QmNameplateWeakHookColor, color_cast<ColorRGBA>(ColorHSLA(41131)), false);
				NumericField(Content, "appearance-hook-strength-size", Localize("Size of hook strength icon and number indicator"), &g_Config.m_ClNamePlatesStrongSize, -50, 100);
			};
			PreLayoutInput = [=](CUIRect Content) {
				const CUIRect Row = NextRow(Content);
				if(!pMenus->Ui()->DoButtonLogic(&g_Config.m_ClNamePlatesStrong, 0, &Row, BUTTONFLAG_LEFT))
					return false;
				g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
				return true;
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-key-presses") == 0)
		{
			Render = [=](CUIRect &Content) {
				pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, Content, "appearance-show-key-presses-label", Localize("Show players' key presses"),
					pMenus->m_vButtonContainersNamePlateKeyPresses,
					{"appearance-show-key-presses-none", "appearance-show-key-presses-own", "appearance-show-key-presses-others", "appearance-show-key-presses-all"},
					{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
					{0, 3, 1, 2}, g_Config.m_ClShowDirection, Metrics, &g_Config.m_ClShowDirection);
				Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
				if(g_Config.m_ClShowDirection > 0)
					NumericField(Content, "appearance-key-press-icons-size", Localize("Size of key press icons"), &g_Config.m_ClDirectionSize, -50, 100);
			};
			PreLayoutInput = [=](CUIRect Content) {
				CUIRect Buttons = ResolveSettingsRadioRowLayout(Content, 4, Metrics).m_ButtonsRect;
				const int aValues[] = {0, 3, 1, 2};
				const float ButtonWidth = Buttons.w / 4.0f;
				const char *pOverrideTooltip = pMenus->TemporaryOverrideTooltip(&g_Config.m_ClShowDirection);
				bool Changed = false;
				for(int Index = 0; Index < 4; ++Index)
				{
					CUIRect Button;
					Buttons.VSplitLeft(ButtonWidth, &Button, &Buttons);
					CButtonContainer *pId = &pMenus->m_vButtonContainersNamePlateKeyPresses[Index];
					if(pOverrideTooltip != nullptr)
						pMenus->GameClient()->m_Tooltips.DoToolTip(pId, &Button, pOverrideTooltip);
					if(pMenus->Ui()->DoButtonLogic(pId, aValues[Index] == g_Config.m_ClShowDirection, &Button, BUTTONFLAG_LEFT) && pOverrideTooltip == nullptr)
					{
						g_Config.m_ClShowDirection = aValues[Index];
						Changed = true;
					}
				}
				return Changed;
			};
		}
		else
			return false;

		Out = {};
		Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		// 每张卡按自己的内容回调测量，条件行不再依赖整块昵称设置的累计行数。
		Out.m_Measure = [pMenus, Render](float ContentWidth) { return MeasureContent(pMenus, Render, ContentWidth); };
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = std::move(Render);
		Out.m_MeasureRevision = NameplateMeasureContentRevision();
		if(!Ctx.m_ReadOnly)
		{
			Out.m_PreLayoutInput = [pMenus, PreLayoutInput](CUIRect Content) {
				return !pMenus->m_MenuTextPlanCollecting && !pMenus->Ui()->RenderOnly() && PreLayoutInput(Content);
			};
		}
		return true;
	}
}
