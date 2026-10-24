/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_settings_controls.h"

#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace FontIcons;

inline constexpr float MARGIN = 10.0f;

namespace
{
	void LogControlsPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

}

bool CBindSlotUiElement::operator<(const CBindSlotUiElement &Other) const
{
	if(m_Bind == EMPTY_BIND_SLOT)
	{
		return false;
	}
	if(Other.m_Bind == EMPTY_BIND_SLOT)
	{
		return true;
	}
	return (m_Bind.m_ModifierMask != Other.m_Bind.m_ModifierMask) ? (m_Bind.m_ModifierMask < Other.m_Bind.m_ModifierMask) : (m_Bind.m_Key < Other.m_Bind.m_Key);
}

std::vector<CBindSlotUiElement>::iterator CBindOption::GetBindSlotElement(const CBindSlot &BindSlot)
{
	return std::find_if(m_vCurrentBinds.begin(), m_vCurrentBinds.end(), [&](const CBindSlotUiElement &BindSlotUiElement) {
		return BindSlotUiElement.m_Bind == BindSlot;
	});
}

bool CBindOption::MatchesSearch(const char *pSearch) const
{
	return (m_Group != EBindOptionGroup::CUSTOM && str_utf8_find_nocase(Localize(m_pLabel), pSearch) != nullptr) ||
	       str_utf8_find_nocase(m_Command.c_str(), pSearch) != nullptr;
}

void CMenusSettingsControls::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);

	// 预定义动作来自 bind_overview.h 的共享已知动作表（Bind 一览卡使用同一份表归类）。
	for(const SKnownBindAction &Action : s_aKnownBindActions)
		m_vBindOptions.push_back({Action.m_Group, Action.m_pLabel, Action.m_pCommand});
	m_NumPredefinedBindOptions = m_vBindOptions.size();

	// 此时配置文件尚未加载；持久状态在 Render 中同步，避免提前完成旧配置迁移。
	std::fill(std::begin(m_aBindGroupExpanded), std::end(m_aBindGroupExpanded), true);
	m_aBindGroupExpanded[(int)EBindOptionGroup::CUSTOM] = false;

	m_JoystickDropDownState.m_SelectionPopupContext.m_pScrollRegion = &m_JoystickDropDownScrollRegion;
}

void CMenusSettingsControls::Render(CUIRect MainView)
{
	const bool ReadOnly = Ui()->RenderOnly();
	CPerfTimer ShellTimer;
	PrepareSettingsCards(ResolveSettingsContentMetrics(MainView.w), ReadOnly, Ui()->MeasurementOnly() ? nullptr : &m_SettingsScrollRegion);
	LogControlsPerfStage(GameClient()->Client(), "controls_tab_shell", ShellTimer.ElapsedMs(), false, "page=controls");

	CPerfTimer InteractiveTimer;
	CUIRect QuickSearch, SearchMatches, ResetToDefault;
	MainView.HSplitBottom(m_CardMetrics.m_LineHeight, &MainView, &QuickSearch);
	QuickSearch.VSplitRight(200.0f, &QuickSearch, &ResetToDefault);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	QuickSearch.VSplitRight(150.0f, &QuickSearch, &SearchMatches);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	MainView.HSplitBottom(MARGIN, &MainView, nullptr);

	// Quick search
	IUiContext ControlsSearchCtx = GameClient()->m_Menus.SettingsUiContext("settings_controls_search", m_CardMetrics.m_BodySize / ui_token::font::BODY);
	if(!ReadOnly && ui_widget::InputField(ControlsSearchCtx, &m_FilterInput, QuickSearch, m_CardMetrics.m_BodySize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_KeyBinder.IsActive()))
	{
		m_CurrentSearchMatch = 0;
		UpdateSearchMatches();
		m_SearchMatchReveal = true;
	}
	else if(!ReadOnly && !m_vSearchMatches.empty() && (Ui()->ConsumeHotkey(CUi::EHotkey::HOTKEY_ENTER) || Ui()->ConsumeHotkey(CUi::EHotkey::HOTKEY_TAB)))
	{
		UpdateSearchMatches();
		m_CurrentSearchMatch += Input()->ShiftIsPressed() ? -1 : 1;
		if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
		{
			m_CurrentSearchMatch = 0;
		}
		if(m_CurrentSearchMatch < 0)
		{
			m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
		}
		m_SearchMatchReveal = true;
	}

	if(!m_FilterInput.IsEmpty())
	{
		if(!m_vSearchMatches.empty())
		{
			char aSearchMatchLabel[64];
			str_format(aSearchMatchLabel, sizeof(aSearchMatchLabel), Localize("Match %d of %d"), m_CurrentSearchMatch + 1, (int)m_vSearchMatches.size());
			DoSettingsControlsLabel("controls-search-match-label", &SearchMatches, aSearchMatchLabel, m_CardMetrics.m_BodySize, TEXTALIGN_MC);
		}
		else
		{
			DoSettingsControlsLabel("controls-no-results-label", &SearchMatches, Localize("No results"), m_CardMetrics.m_BodySize, TEXTALIGN_MC);
		}
	}

	// Reset to default button
	static CUi::SConfirmPopupContext s_ResetControlsConfirm;
	static bool s_ResetControlsPending = false;
	if(GameClient()->m_Menus.DoSettingsButton_Menu(CMenus::SETTINGS_CONTROLS, -1, -1, &m_ResetToDefaultButton, "controls-reset-to-defaults", Localize("Reset to defaults"), 0, &ResetToDefault))
	{
		// 重置会丢掉现有绑定：Cancel/OK + 危险色，正文说明后果。
		s_ResetControlsConfirm.Reset();
		s_ResetControlsConfirm.DefaultButtons();
		s_ResetControlsConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
		str_copy(s_ResetControlsConfirm.m_aMessage, Localize("Are you sure that you want to reset the controls to their defaults?"), sizeof(s_ResetControlsConfirm.m_aMessage));
		GameClient()->m_Menus.Ui()->ShowPopupConfirm(GameClient()->m_Menus.Ui()->MouseX(), GameClient()->m_Menus.Ui()->MouseY(), &s_ResetControlsConfirm);
		s_ResetControlsPending = true;
	}
	// 结果下一帧轮询：确认后才重置，取消或被页面切换收掉时不动作。
	if(s_ResetControlsPending)
	{
		if(s_ResetControlsConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			s_ResetControlsPending = false;
			GameClient()->m_Menus.ResetSettingsControls();
		}
		else if(s_ResetControlsConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !GameClient()->m_Menus.Ui()->IsPopupOpen(&s_ResetControlsConfirm))
		{
			s_ResetControlsPending = false;
		}
	}
	LogControlsPerfStage(GameClient()->Client(), "controls_interactive_layer", InteractiveTimer.ElapsedMs(), false, "page=controls section=interactive");
	LogControlsPerfStage(GameClient()->Client(), "controls_text_cache", ShellTimer.ElapsedMs(), false, "page=controls section=text_cache");

	if(!ReadOnly)
	{
		CQmScrollState &ScrollState = m_SettingsScrollRegion.State();
		(void)ScrollState;
	}
	const float UiScale = GameClient()->m_Menus.SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = GameClient()->m_Menus.SettingsPageLayout(MainView, UiScale);
	const IUiContext CardCtx = GameClient()->m_Menus.SettingsUiContext("settings_controls", UiScale);
	const SSettingsCardDeckVisualOptions VisualOptions = GameClient()->m_Menus.SettingsCardDeckVisualOptions();
	CPerfTimer BindListTimer;
	const uint64_t CardLayoutRevision = SettingsCardsRevision(ReadOnly);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(GameClient()->m_Menus.m_SettingsCardDeckDisplayCycle, GameClient()->m_Menus.m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	qm_card_catalog::SQmCardBuildContext CardBuild;
	CardBuild.m_pMenus = &GameClient()->m_Menus;
	CardBuild.m_ReadOnly = ReadOnly;
	CardBuild.m_Page = Page;
	CardBuild.m_Metrics = m_CardMetrics;
	CardBuild.m_UiContext = CardCtx;
	CardBuild.m_pScrollRegion = Ui()->MeasurementOnly() ? nullptr : &m_SettingsScrollRegion;
	const auto BuildDefinitions = [CardBuild](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::ControlsCardStableIds(), vCards);
	};
	if(!ReadOnly && m_SearchMatchReveal && !m_vSearchMatches.empty() && m_CurrentSearchMatch >= 0 && m_CurrentSearchMatch < (int)m_vSearchMatches.size())
	{
		const EBindOptionGroup Group = m_vBindOptions[m_vSearchMatches[m_CurrentSearchMatch]].m_Group;
		static const char *const apCardIds[(int)EBindOptionGroup::NUM] = {
			"deck:controls-movement", "deck:controls-weapon", "deck:controls-voting", "deck:controls-chat",
			"deck:controls-dummy", "deck:controls-miscellaneous", "deck:controls-custom"};
		GameClient()->m_Menus.m_SettingsCardDeck.RequestReveal(apCardIds[(int)Group]);
	}
	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	if(!ReadOnly)
	{
		InputState.m_MouseX = Ui()->MouseX();
		InputState.m_MouseY = Ui()->MouseY();
		InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
		InputState.m_MouseDown = Ui()->MouseButton(0);
		InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
		InputState.m_CtrlPressed = Input()->ModifierIsPressed();
		InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		InputState.m_pScrollParams = &ScrollParams;
	}
	const SSettingsCardDeckResult DeckResult = GameClient()->m_Menus.SettingsCardDeckForRenderPass().RenderCached(CardCtx, Page, "controls", DefinitionsRevision, BuildDefinitions, GameClient()->m_Menus.SettingsCardOrderModelForRenderPass(), Ui()->MeasurementOnly() ? nullptr : &m_SettingsScrollRegion, InputState, GameClient()->m_Menus.SettingsCardMotionSpec(), VisualOptions);
	if(!ReadOnly && DeckResult.m_OrderChanged)
		GameClient()->m_Menus.SaveSettingsCardOrderModel();
	LogControlsPerfStage(GameClient()->Client(), "controls_bind_list", BindListTimer.ElapsedMs(), false, "page=controls section=bind_list");
}

void CMenusSettingsControls::UpdateBindOptions()
{
	for(CBindOption &Option : m_vBindOptions)
	{
		for(CBindSlotUiElement &BindSlot : Option.m_vCurrentBinds)
		{
			if(BindSlot.m_Bind != EMPTY_BIND_SLOT)
			{
				BindSlot.m_ToBeDeleted = true;
			}
		}
	}

	for(int Mod = KeyModifier::NONE; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
	{
		for(int KeyId = KEY_FIRST; KeyId < KEY_LAST; KeyId++)
		{
			const CBindSlot BindSlot = CBindSlot(KeyId, Mod);
			const char *pBind = GameClient()->m_Binds.Get(BindSlot);
			if(!pBind[0])
			{
				continue;
			}

			auto ExistingOption = std::find_if(m_vBindOptions.begin(), m_vBindOptions.end(), [pBind](const CBindOption &Option) {
				return str_comp(pBind, Option.m_Command.c_str()) == 0;
			});
			if(ExistingOption == m_vBindOptions.end())
			{
				// Bind option not found for command, add custom bind option.
				CBindOption NewOption = {EBindOptionGroup::CUSTOM, nullptr, pBind};
				ExistingOption = m_vBindOptions.insert(
					std::upper_bound(m_vBindOptions.begin() + m_NumPredefinedBindOptions, m_vBindOptions.end(), NewOption, [&](const CBindOption &Option1, const CBindOption &Option2) {
						return str_utf8_comp_nocase(Option1.m_Command.c_str(), Option2.m_Command.c_str()) < 0;
					}),
					NewOption);

				// Update search matches due to new option being added.
				if(!m_FilterInput.IsEmpty())
				{
					const int OptionIndex = ExistingOption - m_vBindOptions.begin();
					for(int &SearchMatch : m_vSearchMatches)
					{
						if(OptionIndex <= SearchMatch)
						{
							++SearchMatch;
						}
					}
					if(ExistingOption->MatchesSearch(m_FilterInput.GetString()))
					{
						const int MatchIndex = m_vSearchMatches.insert(std::upper_bound(m_vSearchMatches.begin(), m_vSearchMatches.end(), OptionIndex), OptionIndex) - m_vSearchMatches.begin();
						if(MatchIndex <= m_CurrentSearchMatch)
						{
							++m_CurrentSearchMatch;
						}
					}
				}
			}
			auto ExistingBindSlot = ExistingOption->GetBindSlotElement(BindSlot);
			if(ExistingBindSlot == ExistingOption->m_vCurrentBinds.end())
			{
				// Remove empty bind slot if one is present because it will be replaced with a bind slot for the new bind.
				auto ExistingEmptyBindSlot = ExistingOption->GetBindSlotElement(EMPTY_BIND_SLOT);
				if(ExistingEmptyBindSlot != ExistingOption->m_vCurrentBinds.end())
				{
					ExistingOption->m_vCurrentBinds.erase(ExistingEmptyBindSlot);
				}

				CBindSlotUiElement BindSlotUiElement = {BindSlot};
				ExistingOption->m_vCurrentBinds.insert(
					std::upper_bound(ExistingOption->m_vCurrentBinds.begin(), ExistingOption->m_vCurrentBinds.end(), BindSlotUiElement),
					BindSlotUiElement);
			}
			else
			{
				ExistingBindSlot->m_ToBeDeleted = false;
			}
		}
	}

	// Remove bind slots that are not bound anymore,
	// mark unused custom bind options for removal.
	for(CBindOption &Option : m_vBindOptions)
	{
		Option.m_vCurrentBinds.erase(std::remove_if(Option.m_vCurrentBinds.begin(), Option.m_vCurrentBinds.end(),
						     [&](const CBindSlotUiElement &BindSlotUiElement) { return BindSlotUiElement.m_ToBeDeleted; }),
			Option.m_vCurrentBinds.end());

		Option.m_ToBeDeleted = Option.m_vCurrentBinds.empty() && Option.m_Group == EBindOptionGroup::CUSTOM;
		if(Option.m_ToBeDeleted)
		{
			continue;
		}

		if(Option.m_vCurrentBinds.empty() ||
			(Option.m_AddNewBind && Option.GetBindSlotElement(EMPTY_BIND_SLOT) == Option.m_vCurrentBinds.end()))
		{
			Option.m_vCurrentBinds.emplace_back(EMPTY_BIND_SLOT);
		}
	}

	// Update search matches when removing bind options.
	for(const CBindOption &Option : m_vBindOptions)
	{
		if(!Option.m_ToBeDeleted)
		{
			continue;
		}
		const int OptionIndex = &Option - m_vBindOptions.data();
		auto ExactSearchMatch = std::find(m_vSearchMatches.begin(), m_vSearchMatches.end(), OptionIndex);
		if(ExactSearchMatch != m_vSearchMatches.end())
		{
			m_vSearchMatches.erase(ExactSearchMatch);
			if((int)(ExactSearchMatch - m_vSearchMatches.begin()) < m_CurrentSearchMatch)
			{
				--m_CurrentSearchMatch;
			}
		}
		for(int &SearchMatch : m_vSearchMatches)
		{
			if(OptionIndex < SearchMatch)
			{
				--SearchMatch;
			}
		}
	}
	if(m_vSearchMatches.empty())
	{
		m_CurrentSearchMatch = 0;
	}
	else if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
	{
		m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
	}

	// Remove unused bind options.
	m_vBindOptions.erase(std::remove_if(m_vBindOptions.begin() + m_NumPredefinedBindOptions, m_vBindOptions.end(),
				     [&](const CBindOption &Option) { return Option.m_ToBeDeleted; }),
		m_vBindOptions.end());
	++m_BindLayoutRevision;
}

void CMenusSettingsControls::UpdateSearchMatches()
{
	m_vSearchMatches.clear();

	if(!m_FilterInput.IsEmpty())
	{
		for(CBindOption &Option : m_vBindOptions)
		{
			if(!Option.MatchesSearch(m_FilterInput.GetString()))
			{
				continue;
			}

			qm_card_collapse::SetCollapsed(BindGroupCardStableId(Option.m_Group), false);
			m_aBindGroupExpanded[(int)Option.m_Group] = !qm_card_collapse::IsCollapsed(BindGroupCardStableId(Option.m_Group), Option.m_Group == EBindOptionGroup::CUSTOM);
			m_vSearchMatches.emplace_back(&Option - m_vBindOptions.data());
		}
	}

	if(m_vSearchMatches.empty())
	{
		m_CurrentSearchMatch = 0;
	}
	else if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
	{
		m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
	}
}

void CMenus::ResetSettingsControls()
{
	GameClient()->m_Binds.SetDefaults();

	g_Config.m_InpMousesens = 200;
	g_Config.m_UiMousesens = 200;
	g_Config.m_ClMouseMinDistance = 0;
	g_Config.m_ClMouseMaxDistance = 400;

	g_Config.m_InpControllerEnable = 0;
	g_Config.m_InpControllerGUID[0] = '\0';
	g_Config.m_InpControllerAbsolute = 0;
	g_Config.m_InpControllerSens = 100;
	g_Config.m_InpControllerX = 0;
	g_Config.m_InpControllerY = 1;
	g_Config.m_InpControllerTolerance = 5;
	g_Config.m_UiControllerSens = 100;
}
