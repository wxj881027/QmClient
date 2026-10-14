#include "QmCardCatalogInternal.h"

#include <game/client/QmUi/UiTokens.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
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
		const auto NextRow = [Metrics](CUIRect &Content, const char *pId, CUIRect &Row) {
			CSettingsContentLayoutFlow Flow(Content);
			const bool HasRow = Flow.Control(pId, Metrics.m_LineHeight, Row);
			Flow.Space(Metrics.m_LineSpacing);
			return HasRow;
		};
		const auto NumericField = [pMenus, Metrics, CardCtx, NextRow](CUIRect &Content, const char *pTextId, const char *pLabel, int *pValue, int Min, int Max) {
			CUIRect Row;
			if(!NextRow(Content, pTextId, Row))
				return;
			ui_widget::SNumericFieldOptions Options;
			Options.m_pLabel = pLabel;
			Options.m_FontSize = Metrics.m_BodySize;
			Options.m_LabelAlign = TEXTALIGN_ML;
			if(pMenus->PrepareSettingsNumericFieldLabel(Page, Tab, -1, pTextId, Row, pLabel, 0u, Options))
				return;
			ui_widget::NumericField(CardCtx, pMenus->GetSettingsNumericFieldState(pValue), pValue, pValue, Min, Max, Row, Options);
		};
		const auto CheckBox = [pMenus, Metrics, NextRow](CUIRect &Content, const char *pTextId, const char *pLabel, int *pValue) {
			CUIRect Row;
			if(NextRow(Content, pTextId, Row) && pMenus->DoSettingsButton_CheckBox(Page, Tab, Tab, pValue, pTextId, pLabel, *pValue, &Row))
				*pValue ^= 1;
		};
		FSettingsCardRenderMeasured Render;
		if(str_comp(pStableId, "deck:appearance-name-plate-settings") == 0)
		{
			Render = [=](CUIRect &Content) {
				CSettingsContentLayoutFlow Flow(Content);
				const int *pOverrideSource = pMenus->TemporaryOverrideTooltip(&g_Config.m_QmNameplateShowScope) != nullptr ? &g_Config.m_QmNameplateShowScope : nullptr;
				int ShowScope = std::clamp(g_Config.m_QmNameplateShowScope, 0, QM_NAMEPLATE_SHOW_SCOPE_COUNT - 1);
				CUIRect RadioRow;
				if(Flow.Control("nameplate-show", Metrics.m_LineHeight, RadioRow) && pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, RadioRow, "appearance-show-name-plates-label", Localize("Show name plates"),
													     pMenus->m_vButtonContainersNamePlateShow,
													     {"appearance-show-name-plates-none", "appearance-show-name-plates-current", "appearance-show-name-plates-local", "appearance-show-name-plates-others", "appearance-show-name-plates-others-local", "appearance-show-name-plates-all"},
													     {Localize("None", "Show name plates"), Localize("Current", "Show name plates"), Localize("Own characters", "Show name plates"), Localize("Others", "Show name plates"), Localize("Others and own", "Show name plates"), Localize("All", "Show name plates")},
													     {QM_NAMEPLATE_SHOW_SCOPE_OFF, QM_NAMEPLATE_SHOW_SCOPE_CURRENT, QM_NAMEPLATE_SHOW_SCOPE_LOCAL, QM_NAMEPLATE_SHOW_SCOPE_OTHERS, QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL, QM_NAMEPLATE_SHOW_SCOPE_ALL},
													     ShowScope, Metrics, pOverrideSource, &g_Config.m_QmNameplateShowScope))
					g_Config.m_QmNameplateShowScope = ShowScope;
				Flow.Space(Metrics.m_LineSpacing);

				NumericField(Content, "appearance-name-plates-size", Localize("Name plates size"), &g_Config.m_ClNamePlatesSize, -50, 100);
				NumericField(Content, "appearance-name-plates-offset", Localize("Name plates offset"), &g_Config.m_ClNamePlatesOffset, 10, 50);
				CheckBox(Content, "appearance-show-clan-above-name-plates", Localize("Show clan above name plates"), &g_Config.m_ClNamePlatesClan);
				if(g_Config.m_ClNamePlatesClan)
					NumericField(Content, "appearance-clan-plates-size", Localize("Clan plates size"), &g_Config.m_ClNamePlatesClanSize, -50, 100);
				NumericField(Content, "appearance-coords-size", Localize("Coords size"), &g_Config.m_ClNamePlatesCoordsSize, -50, 100);
				pMenus->DoSettingsToggleGroup(Page, Tab, Tab, Content,
					{
						{&g_Config.m_ClNamePlatesTeamcolors, "appearance-name-plates-team-colors", Localize("Use team colors for name plates")},
						{&g_Config.m_ClNamePlatesFriendMark, "appearance-show-friend-icon-name-plates", Localize("Show friend icon in name plates")},
					},
					Metrics, Flow.Input());
				CheckBox(Content, "appearance-show-client-ids-name-plates", Localize("Show client IDs in name plates"), &g_Config.m_ClNamePlatesIds);
				if(g_Config.m_ClNamePlatesIds)
				{
					CheckBox(Content, "appearance-client-ids-separate-line", Localize("Show client IDs on a separate line"), &g_Config.m_ClNamePlatesIdsSeparateLine);
					if(g_Config.m_ClNamePlatesIdsSeparateLine)
						NumericField(Content, "appearance-client-ids-size", Localize("Client IDs size"), &g_Config.m_ClNamePlatesIdsSize, -50, 100);
				}
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-text") == 0)
		{
			Render = [=](CUIRect &Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CheckBox(Content, "appearance-nameplate-advanced", Localize("Show advanced options"), &g_Config.m_QmNameplateAdvanced);
				if(!g_Config.m_QmNameplateAdvanced)
					return;

				const bool GlowWasEnabled = (g_Config.m_QmNameplateTextEffects & QM_TEXT_EFFECT_GLOW) != 0;
				pMenus->DoSettingsToggleGroup(Page, Tab, Tab, Content,
					{
						{&g_Config.m_QmNameplateTextEffects, "appearance-nameplate-text-border", Localize("Border"), "appearance-nameplate-text-border", QM_TEXT_EFFECT_BORDER},
						{&g_Config.m_QmNameplateTextEffects, "appearance-nameplate-text-gradient", Localize("Gradient"), "appearance-nameplate-text-gradient", QM_TEXT_EFFECT_GRADIENT},
						{&g_Config.m_QmNameplateTextEffects, "appearance-nameplate-text-rainbow", Localize("Rainbow"), "appearance-nameplate-text-rainbow", QM_TEXT_EFFECT_RAINBOW},
						{&g_Config.m_QmNameplateTextEffects, "appearance-nameplate-text-glow", Localize("Glow"), "appearance-nameplate-text-glow", QM_TEXT_EFFECT_GLOW},
					},
					Metrics, Flow.Input());
				if(Flow.Input() && !GlowWasEnabled && (g_Config.m_QmNameplateTextEffects & QM_TEXT_EFFECT_GLOW) != 0 && g_Config.m_QmNameplateTextGlowRange == 0)
					g_Config.m_QmNameplateTextGlowRange = 4;

				const auto ControlRow = [&](const char *pTextId, const char *pLabel, const auto &RenderControl) {
					CUIRect Row;
					if(!NextRow(Content, pTextId, Row))
						return;
					CUIRect Label, Control;
					SplitSettingsControlRow(Row, Metrics, &Label, &Control);
					SLabelProperties Props;
					Props.m_DisallowNewline = true;
					Props.m_StopAtEnd = true;
					Props.m_MinimumFontSize = 6.0f;
					Props.m_MaxWidth = Label.w;
					CUIElement &LabelElement = pMenus->SettingsTextElement(Page, Tab, pTextId);
					pMenus->DoSettingsLabelStreamed(LabelElement, &Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML, Props);
					RenderControl(Control, Row);
				};
				const auto DropDown = [&](const char *pTextId, const char *pLabel, int *pValue, int Max, std::vector<const char *> &vNames, CUi::SDropDownState &State, CScrollRegion &ScrollRegion) {
					ControlRow(pTextId, pLabel, [&](CUIRect &Control, const CUIRect &Row) {
						State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
						*pValue = pMenus->DoSettingsDropDown(&Control, std::clamp(*pValue, 0, Max), vNames.data(), (int)vNames.size(), State, {}, pValue, nullptr, &Row);
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
				ControlRow("appearance-nameplate-text-demo-target", Localize("Demo target"), [&](CUIRect &Control, const CUIRect &Row) {
					const int Selection = pMenus->DoSettingsDropDown(&Control, DemoTargetSelection, s_vDemoTargetNames.data(), (int)s_vDemoTargetNames.size(), s_DemoTargetState, {}, &g_Config.m_QmNameplateTextDemoTarget, nullptr, &Row);
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
				CUIRect s_BorderColorRow;
				if(Flow.Control("s_BorderColor", Metrics.m_LineHeight, s_BorderColorRow))
				{
					pMenus->DoLine_ColorPicker(&s_BorderColor, Metrics, &s_BorderColorRow, Localize("Border color"), &g_Config.m_QmNameplateTextBorderColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), false, nullptr, true);
				}
				Flow.Space(Metrics.m_LineSpacing);
				CUIRect s_GradientColorRow;
				if(Flow.Control("s_GradientColor", Metrics.m_LineHeight, s_GradientColorRow))
				{
					pMenus->DoLine_ColorPicker(&s_GradientColor, Metrics, &s_GradientColorRow, Localize("Gradient color"), &g_Config.m_QmNameplateTextGradientColor, ui_token::color::NEUTRAL_WHITE, false, nullptr, true);
				}
				Flow.Space(Metrics.m_LineSpacing);
				CUIRect s_GlowColorRow;
				if(Flow.Control("s_GlowColor", Metrics.m_LineHeight, s_GlowColorRow))
				{
					pMenus->DoLine_ColorPicker(&s_GlowColor, Metrics, &s_GlowColorRow, Localize("Glow color"), &g_Config.m_QmNameplateTextGlowColor, ColorRGBA(0.30f, 0.78f, 1.0f, 0.40f), false, nullptr, true);
				}
				Flow.Space(Metrics.m_LineSpacing);
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-hook-strength") == 0)
		{
			Render = [=](CUIRect &Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect Row;
				const bool HasToggle = NextRow(Content, "hook-strength-enabled", Row);
				if(HasToggle && pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &g_Config.m_ClNamePlatesStrong, "appearance-show-hook-strength-icon", Localize("Show hook strength icon indicator"), g_Config.m_ClNamePlatesStrong, &Row, SingleLineProps))
					g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
				if(!g_Config.m_ClNamePlatesStrong)
					return;
				static int s_StrongNumberId;
				if(NextRow(Content, "hook-strength-number", Row))
				{
					pMenus->Ui()->DoConfigTooltip(&s_StrongNumberId, &Row, &g_Config.m_ClNamePlatesStrong);
					if(pMenus->DoSettingsButton_CheckBox(Page, Tab, -1, &s_StrongNumberId, "appearance-show-hook-strength-number", Localize("Show hook strength number indicator"), g_Config.m_ClNamePlatesStrong == 2, &Row, SingleLineProps))
						g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong != 2 ? 2 : 1;
				}
				CUIRect RadioRow;
				if(Flow.Control("hook-strength-scope", Metrics.m_LineHeight, RadioRow))
				{
					pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, RadioRow, "appearance-hook-strength-scope-label", Localize("Hook strength scope"),
						pMenus->m_vButtonContainersNamePlateHookStrongWeakScope,
						{"appearance-hook-strength-scope-self", "appearance-hook-strength-scope-others", "appearance-hook-strength-scope-strong", "appearance-hook-strength-scope-weak", "appearance-hook-strength-scope-all"},
						{Localize("Self"), Localize("Others"), Localize("Strong hook"), Localize("Weak hook"), Localize("All")},
						{QM_HOOK_STRONG_WEAK_SCOPE_SELF, QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, QM_HOOK_STRONG_WEAK_SCOPE_STRONG, QM_HOOK_STRONG_WEAK_SCOPE_WEAK, QM_HOOK_STRONG_WEAK_SCOPE_ALL},
						g_Config.m_QmNameplateHookStrongWeakScope, Metrics);
				}
				Flow.Space(Metrics.m_LineSpacing);
				static CButtonContainer s_StrongColor, s_WeakColor;
				CUIRect s_StrongColorRow;
				if(Flow.Control("s_StrongColor", Metrics.m_LineHeight, s_StrongColorRow))
				{
					pMenus->DoLine_ColorPicker(&s_StrongColor, Metrics, &s_StrongColorRow, Localize("Strong hook color"), &g_Config.m_QmNameplateStrongHookColor, color_cast<ColorRGBA>(ColorHSLA(6401973)), false);
				}
				Flow.Space(Metrics.m_LineSpacing);
				CUIRect s_WeakColorRow;
				if(Flow.Control("s_WeakColor", Metrics.m_LineHeight, s_WeakColorRow))
				{
					pMenus->DoLine_ColorPicker(&s_WeakColor, Metrics, &s_WeakColorRow, Localize("Weak hook color"), &g_Config.m_QmNameplateWeakHookColor, color_cast<ColorRGBA>(ColorHSLA(41131)), false);
				}
				Flow.Space(Metrics.m_LineSpacing);
				NumericField(Content, "appearance-hook-strength-size", Localize("Size of hook strength icon and number indicator"), &g_Config.m_ClNamePlatesStrongSize, -50, 100);
			};
		}
		else if(str_comp(pStableId, "deck:appearance-name-plate-key-presses") == 0)
		{
			Render = [=](CUIRect &Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect RadioRow;
				if(Flow.Control("keypress-scope", Metrics.m_LineHeight, RadioRow))
				{
					pMenus->DoSettingsLine_RadioMenu(Page, Tab, Tab, RadioRow, "appearance-show-key-presses-label", Localize("Show players' key presses"),
						pMenus->m_vButtonContainersNamePlateKeyPresses,
						{"appearance-show-key-presses-none", "appearance-show-key-presses-own", "appearance-show-key-presses-others", "appearance-show-key-presses-all"},
						{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
						{0, 3, 1, 2}, g_Config.m_ClShowDirection, Metrics, &g_Config.m_ClShowDirection);
				}
				Flow.Space(Metrics.m_LineSpacing);
				if(g_Config.m_ClShowDirection > 0)
					NumericField(Content, "appearance-key-press-icons-size", Localize("Size of key press icons"), &g_Config.m_ClDirectionSize, -50, 100);
			};
		}
		else
			return false;

		Out = {};
		Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = std::move(Render);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
}
