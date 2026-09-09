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
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <functional>
#include <string>
#include <vector>

using namespace FontIcons;

static float HEADER_FONT_SIZE = ui_token::font::HEADLINE;
static float FONT_SIZE = ui_token::font::BODY;
inline constexpr float MARGIN = 10.0f;
static float BUTTON_HEIGHT = ui_token::settings::ROW_HEIGHT;
static float BUTTON_SPACING = ui_token::settings::ROW_GAP;
static float BIND_OPTION_SPACING = ui_token::settings::ROW_GAP;

namespace
{
	void LogControlsPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	void ApplyControlsContentMetrics(const float ContentWidth)
	{
		const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(ContentWidth);
		HEADER_FONT_SIZE = std::clamp(ui_token::font::HEADLINE * Metrics.m_UiScale, 13.0f, ui_token::font::HEADLINE);
		FONT_SIZE = Metrics.m_BodySize;
		BUTTON_HEIGHT = Metrics.m_LineHeight;
		BUTTON_SPACING = Metrics.m_LineSpacing;
		BIND_OPTION_SPACING = Metrics.m_LineSpacing;
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
	return m_Bind.m_ModifierMask < Other.m_Bind.m_ModifierMask ||
	       m_Bind.m_Key < Other.m_Bind.m_Key;
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

	m_vBindOptions = {
		{EBindOptionGroup::MOVEMENT, Localizable("Move left"), "+left"},
		{EBindOptionGroup::MOVEMENT, Localizable("Move right"), "+right"},
		{EBindOptionGroup::MOVEMENT, Localizable("Jump"), "+jump"},
		{EBindOptionGroup::MOVEMENT, Localizable("Fire"), "+fire"},
		{EBindOptionGroup::MOVEMENT, Localizable("Hook"), "+hook"},
		{EBindOptionGroup::MOVEMENT, Localizable("Hook collisions"), "+showhookcoll"},
		{EBindOptionGroup::MOVEMENT, Localizable("Pause"), "say /pause"},
		{EBindOptionGroup::MOVEMENT, Localizable("Kill"), "kill"},
		{EBindOptionGroup::MOVEMENT, Localizable("Zoom in"), "zoom+"},
		{EBindOptionGroup::MOVEMENT, Localizable("Zoom out"), "zoom-"},
		{EBindOptionGroup::MOVEMENT, Localizable("Default zoom"), "zoom"},
		{EBindOptionGroup::MOVEMENT, Localizable("Show others"), "say /showothers"},
		{EBindOptionGroup::MOVEMENT, Localizable("Show all"), "say /showall"},
		{EBindOptionGroup::MOVEMENT, Localizable("Toggle dyncam"), "toggle cl_dyncam 0 1"},
		{EBindOptionGroup::MOVEMENT, Localizable("Toggle ghost"), "toggle cl_race_show_ghost 0 1"},
		{EBindOptionGroup::WEAPON, Localizable("Hammer"), "+weapon1"},
		{EBindOptionGroup::WEAPON, Localizable("Pistol"), "+weapon2"},
		{EBindOptionGroup::WEAPON, Localizable("Shotgun"), "+weapon3"},
		{EBindOptionGroup::WEAPON, Localizable("Grenade"), "+weapon4"},
		{EBindOptionGroup::WEAPON, Localizable("Laser"), "+weapon5"},
		{EBindOptionGroup::WEAPON, Localizable("Weapon trajectory"), "+showweapontrajectory"},
		{EBindOptionGroup::WEAPON, Localizable("Next weapon"), "+nextweapon"},
		{EBindOptionGroup::WEAPON, Localizable("Prev. weapon"), "+prevweapon"},
		{EBindOptionGroup::VOTING, Localizable("Vote yes"), "vote yes"},
		{EBindOptionGroup::VOTING, Localizable("Vote no"), "vote no"},
		{EBindOptionGroup::CHAT, Localizable("Chat"), "+show_chat; chat all"},
		{EBindOptionGroup::CHAT, Localizable("Team chat"), "+show_chat; chat team"},
		{EBindOptionGroup::CHAT, Localizable("Converse"), "+show_chat; chat all /c "},
		{EBindOptionGroup::CHAT, Localizable("Chat command"), "+show_chat; chat all /"},
		{EBindOptionGroup::CHAT, Localizable("Show chat"), "+show_chat"},
		{EBindOptionGroup::CHAT, Localizable("Repeat message"), "+qm_repeat"},
		{EBindOptionGroup::CHAT, Localizable("Voice chat"), "+qm_voice_ptt"},
		{EBindOptionGroup::DUMMY, Localizable("Toggle dummy"), "toggle cl_dummy 0 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy jump"), "+toggle_restore cl_dummy_jump 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy fire"), "+toggle_restore cl_dummy_fire 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy hook"), "+toggle_restore cl_dummy_hook 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy copy"), "toggle cl_dummy_copy_moves 0 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy hammer fly"), "toggle cl_dummy_hammer 0 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy Control"), "toggle cl_dummy_control 1 0"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Emoticon"), "+emote"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate mode"), "+spectate"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate teleport"), "qm_spec_teleport"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate next"), "spectate_next"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate previous"), "spectate_previous"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Console"), "toggle_local_console"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Remote console"), "toggle_remote_console"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Screenshot"), "screenshot"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard"), "+scoreboard"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard cursor"), "toggle_scoreboard_cursor"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Statboard"), "+statboard"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Pie menu"), "+pie_menu"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Fast practice"), "fast_practice_toggle"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Lock team"), "say /lock"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Show entities"), "toggle cl_overlay_entities 0 100"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Show HUD"), "toggle cl_showhud 0 1"},
	};
	m_NumPredefinedBindOptions = m_vBindOptions.size();

	std::fill(std::begin(m_aBindGroupExpanded), std::end(m_aBindGroupExpanded), true);
	m_aBindGroupExpanded[(int)EBindOptionGroup::CUSTOM] = false;

	m_JoystickDropDownState.m_SelectionPopupContext.m_pScrollRegion = &m_JoystickDropDownScrollRegion;
}

void CMenusSettingsControls::DoSettingsControlsLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps) const
{
	GameClient()->m_Menus.DoSettingsLabel(CMenus::SETTINGS_CONTROLS, -1, pTextId, pRect, pText, Size, Align, LabelProps);
}

void CMenusSettingsControls::DoSettingsControlsMenuLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &Props, int MaxWidth) const
{
	GameClient()->m_Menus.DoSettingsMenuLabel(CMenus::SETTINGS_CONTROLS, -1, -1, pTextId, pRect, pText, Size, Align, Props, MaxWidth);
}

int CMenusSettingsControls::DoSettingsControlsCheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect) const
{
	return GameClient()->m_Menus.DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTROLS, -1, -1, pId, pTextId, pText, Checked, pRect);
}

bool CMenusSettingsControls::DoSettingsControlsNumericField(const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags)
{
	const float BodySize = GameClient()->m_Menus.CurrentSettingsContentMetrics().m_BodySize;
	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = pLabel;
	Options.m_pScale = pScale;
	Options.m_Flags = Flags;
	Options.m_FontSize = BodySize;
	Options.m_LabelAlign = TEXTALIGN_ML;
	Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
	if(GameClient()->m_Menus.PrepareSettingsNumericFieldLabel(CMenus::SETTINGS_CONTROLS, -1, -1, pTextId, Rect, pLabel, Flags, Options))
		return false;
	IUiContext Context = GameClient()->m_Menus.SettingsUiContext("settings_controls", BodySize / ui_token::font::BODY);
	return ui_widget::NumericField(Context, GameClient()->m_Menus.GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
}

void CMenusSettingsControls::Render(CUIRect MainView)
{
	ApplyControlsContentMetrics(MainView.w);
	const bool ReadOnly = Ui()->RenderOnly();
	CPerfTimer ShellTimer;
	if(!ReadOnly && (m_BindOptionsDirty || GameClient()->m_KeyBinder.IsActive()))
	{
		UpdateBindOptions();
		m_BindOptionsDirty = false;
	}
	LogControlsPerfStage(GameClient()->Client(), "controls_tab_shell", ShellTimer.ElapsedMs(), false, "page=controls");

	CPerfTimer InteractiveTimer;
	CUIRect QuickSearch, SearchMatches, ResetToDefault;
	MainView.HSplitBottom(BUTTON_HEIGHT, &MainView, &QuickSearch);
	QuickSearch.VSplitRight(200.0f, &QuickSearch, &ResetToDefault);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	QuickSearch.VSplitRight(150.0f, &QuickSearch, &SearchMatches);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	MainView.HSplitBottom(MARGIN, &MainView, nullptr);

	// Quick search
	IUiContext ControlsSearchCtx = GameClient()->m_Menus.SettingsUiContext("settings_controls_search", FONT_SIZE / ui_token::font::BODY);
	if(!ReadOnly && ui_widget::InputField(ControlsSearchCtx, &m_FilterInput, QuickSearch, FONT_SIZE, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_KeyBinder.IsActive()))
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
			DoSettingsControlsLabel("controls-search-match-label", &SearchMatches, aSearchMatchLabel, FONT_SIZE, TEXTALIGN_MC);
		}
		else
		{
			DoSettingsControlsLabel("controls-no-results-label", &SearchMatches, Localize("No results"), FONT_SIZE, TEXTALIGN_MC);
		}
	}

	// Reset to default button
	if(GameClient()->m_Menus.DoSettingsButton_Menu(CMenus::SETTINGS_CONTROLS, -1, -1, &m_ResetToDefaultButton, "controls-reset-to-defaults", Localize("Reset to defaults"), 0, &ResetToDefault))
	{
		GameClient()->m_Menus.PopupConfirm(Localize("Reset controls"), Localize("Are you sure that you want to reset the controls to their defaults?"),
			Localize("Reset"), Localize("Cancel"), &CMenus::ResetSettingsControls);
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
	uint64_t CardLayoutRevision = str_quickhash("controls");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ m_BindLayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(m_FilterInput.GetString());
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)m_vSearchMatches.size();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)(g_Config.m_InpControllerEnable != 0);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)(g_Config.m_InpControllerAbsolute != 0);
	const int NumJoysticks = Input()->NumJoysticks();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, NumJoysticks);
	if(NumJoysticks > 0)
	{
		const IInput::IJoystick *pActiveJoystick = Input()->GetActiveJoystick();
		if(pActiveJoystick != nullptr)
		{
			CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, pActiveJoystick->GetIndex());
			CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, pActiveJoystick->GetNumAxes());
		}
	}
	const bool HasControllerJoystick = NumJoysticks > 0 && Input()->GetActiveJoystick() != nullptr;
	const int ControllerAxisCount = HasControllerJoystick ? Input()->GetActiveJoystick()->GetNumAxes() : 0;
	const uint64_t ControllerMeasureRevision =
		(uint64_t)(g_Config.m_InpControllerEnable != 0) |
		((uint64_t)(g_Config.m_InpControllerAbsolute != 0) << 1) |
		((uint64_t)HasControllerJoystick << 2) |
		((uint64_t)maximum(0, ControllerAxisCount) << 3);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)HasControllerJoystick;
	for(bool Expanded : m_aBindGroupExpanded)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Expanded ? 1u : 0u);
	const bool HasCustomBinds = std::any_of(m_vBindOptions.begin(), m_vBindOptions.end(), [](const CBindOption &Option) { return Option.m_Group == EBindOptionGroup::CUSTOM; });
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (HasCustomBinds ? 1u : 0u);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(GameClient()->m_Menus.m_SettingsCardDeckDisplayCycle, GameClient()->m_Menus.m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [this, HasCustomBinds, ReadOnly, ControllerMeasureRevision, CardCtx](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(9);
		const auto AddCard = [this](std::vector<SSettingsCardDefinition> &Cards, const char *pId, float MinHeight, FSettingsCardMeasure Measure, FSettingsCardRender Render, std::function<bool()> IsVisible = {}, bool RenderWhenClipped = false, std::function<bool()> IsCollapsed = {}, FSettingsCardPreLayoutHeaderInput PreLayoutHeaderInput = {}, FSettingsCardHeaderAction HeaderAction = {}, bool MeasureEachFrame = false, uint64_t MeasureRevision = 0) {
			const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pId);
			if(pDefault == nullptr)
				return;
			SSettingsCardDefinition Definition;
			Definition.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
			Definition.m_Measure = [Measure, MinHeight](float Width) { return std::max(MinHeight, Measure ? Measure(Width) : 0.0f); };
			Definition.m_Render = std::move(Render);
			Definition.m_IsVisible = std::move(IsVisible);
			Definition.m_IsCollapsed = std::move(IsCollapsed);
			Definition.m_PreLayoutHeaderInput = std::move(PreLayoutHeaderInput);
			Definition.m_HeaderAction = std::move(HeaderAction);
			Definition.m_MeasureEachFrame = MeasureEachFrame;
			Definition.m_MeasureRevision = MeasureRevision;
			Definition.m_RenderWhenClipped = RenderWhenClipped;
			Cards.push_back(std::move(Definition));
		};
		const auto BindHeight = [this](EBindOptionGroup Group, float) {
			const bool Expanded = m_aBindGroupExpanded[(int)Group];
			return Expanded ? MeasureSettingsBindsHeight(Group) : 0.0f;
		};
		AddCard(vCards, "deck:controls-mouse", 0.0f, [this](float) { return MeasureSettingsMouseHeight(); }, [this](CUIRect Rect) { RenderSettingsMouse(Rect); });
		AddCard(vCards, "deck:controls-controller", 0.0f, [this](float Width) { return MeasureSettingsJoystickHeight(Width); }, [this, ReadOnly](CUIRect Rect) { RenderSettingsJoystick(Rect, ReadOnly); }, {}, false, {}, {}, {}, false, ControllerMeasureRevision);
		vCards.back().m_PreLayoutInput = [this](CUIRect Content) {
			bool Changed = false;
			CUIRect Button;
			Content.HSplitTop(BUTTON_SPACING, nullptr, &Content);
			Content.HSplitTop(BUTTON_HEIGHT, &Button, &Content);
			const bool WasJoystickEnabled = g_Config.m_InpControllerEnable != 0;
			if(Ui()->DoButtonLogic(&g_Config.m_InpControllerEnable, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_InpControllerEnable ^= 1;
				Changed = true;
			}
			if(!WasJoystickEnabled)
				return Changed;

			const IInput::IJoystick *pActiveJoystick = Input()->GetActiveJoystick();
			if(Input()->NumJoysticks() <= 0 || pActiveJoystick == nullptr)
				return Changed;
			Content.HSplitTop(BUTTON_SPACING, nullptr, &Content);
			Content.HSplitTop(BUTTON_HEIGHT, nullptr, &Content);

			const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Content.w);
			const bool WasAbsolute = g_Config.m_InpControllerAbsolute != 0;
			const SSettingsRadioRowLayout ModeLayout = ResolveSettingsRadioRowLayout(Content, 2, Metrics);
			CUIRect ModeButtons = ModeLayout.m_ButtonsRect;
			Content.HSplitTop(ModeLayout.m_Height, nullptr, &Content);
			const float ButtonWidth = ModeButtons.w / (float)m_vJoystickIngameModeButtonContainers.size();
			for(size_t Index = 0; Index < m_vJoystickIngameModeButtonContainers.size(); ++Index)
			{
				CUIRect ModeButton;
				ModeButtons.VSplitLeft(ButtonWidth, &ModeButton, &ModeButtons);
				if(Ui()->DoButtonLogic(&m_vJoystickIngameModeButtonContainers[Index], Index == (size_t)g_Config.m_InpControllerAbsolute, &ModeButton, BUTTONFLAG_LEFT))
				{
					g_Config.m_InpControllerAbsolute = (int)Index;
					Changed = true;
				}
			}
			if(!WasAbsolute)
			{
				Content.HSplitTop(BUTTON_SPACING, nullptr, &Content);
				Content.HSplitTop(BUTTON_HEIGHT, nullptr, &Content);
			}
			Content.HSplitTop(BUTTON_SPACING, nullptr, &Content);
			Content.HSplitTop(BUTTON_HEIGHT, nullptr, &Content);
			Content.HSplitTop(BUTTON_SPACING, nullptr, &Content);
			Content.HSplitTop(BUTTON_HEIGHT, nullptr, &Content);
			return Changed;
		};
		const std::pair<EBindOptionGroup, const char *> aBindCards[] = {
			{EBindOptionGroup::MOVEMENT, "deck:controls-movement"}, {EBindOptionGroup::WEAPON, "deck:controls-weapon"},
			{EBindOptionGroup::VOTING, "deck:controls-voting"}, {EBindOptionGroup::CHAT, "deck:controls-chat"},
			{EBindOptionGroup::DUMMY, "deck:controls-dummy"}, {EBindOptionGroup::MISCELLANEOUS, "deck:controls-miscellaneous"}, {EBindOptionGroup::CUSTOM, "deck:controls-custom"}};
		for(const auto &[Group, pId] : aBindCards)
		{
			const bool IsCustom = Group == EBindOptionGroup::CUSTOM;
			const auto IsCollapsed = [this, Group] { return !m_aBindGroupExpanded[(int)Group]; };
			const auto PreLayoutHeaderInput = [this, Group](const SSettingsCardFrame &Frame, bool Collapsed) {
				const int GroupIndex = (int)Group;
				if(!Ui()->DoButtonLogic(&m_aBindGroupExpandButtons[GroupIndex], Collapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				m_aBindGroupExpanded[GroupIndex] = !m_aBindGroupExpanded[GroupIndex];
				return true;
			};
			const auto HeaderAction = [CardCtx](const SSettingsCardFrame &Frame, bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
			AddCard(vCards, pId, 0.0f, [BindHeight, Group](float Width) { return BindHeight(Group, Width); }, [this, Group, ReadOnly](CUIRect Rect) { RenderSettingsBindCard(Group, Rect, ReadOnly); }, IsCustom ? std::function<bool()>([HasCustomBinds] { return HasCustomBinds; }) : std::function<bool()>(), true, IsCollapsed, PreLayoutHeaderInput, HeaderAction, false, m_BindLayoutRevision);
		}
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
	const SSettingsCardDeckResult DeckResult = GameClient()->m_Menus.SettingsCardDeckForRenderPass().RenderCached(CardCtx, Page, "controls", DefinitionsRevision, BuildDefinitions, GameClient()->m_Menus.SettingsCardOrderModelForRenderPass(), ReadOnly ? nullptr : &m_SettingsScrollRegion, InputState, GameClient()->m_Menus.SettingsCardMotionSpec(), VisualOptions);
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

			m_aBindGroupExpanded[(int)Option.m_Group] = true;
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

void CMenusSettingsControls::RenderSettingsBindCard(EBindOptionGroup Group, CUIRect View, bool ReadOnly)
{
	RenderSettingsBinds(Group, View, ReadOnly);
}

float CMenusSettingsControls::MeasureSettingsBindsHeight(EBindOptionGroup Group) const
{
	float Height = 0.0f;
	for(const CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}
		Height += BUTTON_HEIGHT * BindOption.m_vCurrentBinds.size() + BUTTON_SPACING * (BindOption.m_vCurrentBinds.size() - 1) + 4.0f + BIND_OPTION_SPACING;
	}
	return Height;
}

void CMenusSettingsControls::RenderSettingsBinds(EBindOptionGroup Group, CUIRect View, bool ReadOnly)
{
	for(CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}

		CUIRect KeyReaders;
		View.HSplitTop(BUTTON_HEIGHT * BindOption.m_vCurrentBinds.size() + BUTTON_SPACING * (BindOption.m_vCurrentBinds.size() - 1) + 4.0f, &KeyReaders, &View);
		View.HSplitTop(BIND_OPTION_SPACING, nullptr, &View);
		if(!ReadOnly && !m_SettingsScrollRegion.AddRect(KeyReaders) && !m_SearchMatchReveal)
		{
			continue;
		}
		DrawRoundedSurface(Ui(), KeyReaders, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
		KeyReaders.Margin(2.0f, &KeyReaders);

		CUIRect Label, AddButton;
		KeyReaders.VSplitLeft(KeyReaders.w / 3.0f, &Label, &KeyReaders);
		KeyReaders.VSplitLeft(5.0f, nullptr, &KeyReaders);
		KeyReaders.VSplitLeft(BUTTON_HEIGHT, &AddButton, &KeyReaders);
		AddButton.HSplitTop(BUTTON_HEIGHT, &AddButton, nullptr);
		KeyReaders.VSplitLeft(2.0f, nullptr, &KeyReaders);
		Label.HSplitTop(BUTTON_HEIGHT, &Label, nullptr);

		const auto SearchMatch = std::find(m_vSearchMatches.begin(), m_vSearchMatches.end(), &BindOption - m_vBindOptions.data());
		const bool SearchMatchSelected = SearchMatch != m_vSearchMatches.end() && m_CurrentSearchMatch == (int)(SearchMatch - m_vSearchMatches.begin());
		if(!ReadOnly && SearchMatchSelected && m_SearchMatchReveal)
		{
			m_SearchMatchReveal = false;
			// Scroll to reveal search match
			CUIRect ScrollTarget;
			Label.HMargin(-MARGIN, &ScrollTarget);
			m_SettingsScrollRegion.AddRect(ScrollTarget, true);
		}
		SLabelProperties LabelProps = {.m_MaxWidth = Label.w, .m_EllipsisAtEnd = BindOption.m_Group == EBindOptionGroup::CUSTOM, .m_MinimumFontSize = 9.0f};
		if(SearchMatchSelected)
		{
			LabelProps.SetColor(ColorRGBA(0.1f, 0.1f, 1.0f, 1.0f));
		}
		else if(SearchMatch != m_vSearchMatches.end())
		{
			LabelProps.SetColor(ColorRGBA(0.4f, 0.4f, 0.9f, 1.0f));
		}
		const char *pBindLabel = BindOption.m_Group == EBindOptionGroup::CUSTOM ? BindOption.m_Command.c_str() : Localize(BindOption.m_pLabel);
		const char *pBindTextId = BindOption.m_Group == EBindOptionGroup::CUSTOM ? BindOption.m_Command.c_str() : BindOption.m_pLabel;
		DoSettingsControlsLabel(pBindTextId, &Label, pBindLabel, FONT_SIZE, TEXTALIGN_ML, LabelProps);
		Ui()->DoButtonLogic(&BindOption.m_TooltipButtonId, 0, &Label, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(&BindOption.m_TooltipButtonId, &Label, BindOption.m_Command.c_str());

		for(CBindSlotUiElement &CurrentBind : BindOption.m_vCurrentBinds)
		{
			CUIRect KeyReader;
			KeyReaders.HSplitTop(BUTTON_HEIGHT, &KeyReader, &KeyReaders);
			KeyReaders.HSplitTop(BUTTON_SPACING, nullptr, &KeyReaders);
			const bool ActivateKeyReader = BindOption.m_AddNewBindActivate && CurrentBind.m_Bind == EMPTY_BIND_SLOT;
			if(ReadOnly)
				continue;
			const CKeyBinder::CKeyReaderResult KeyReaderResult = GameClient()->m_KeyBinder.DoKeyReader(
				&CurrentBind.m_KeyReaderButton, &CurrentBind.m_KeyResetButton,
				&KeyReader, CurrentBind.m_Bind, ActivateKeyReader);
			if(ActivateKeyReader)
			{
				BindOption.m_AddNewBindActivate = false;
				// Scroll to reveal activated key reader
				CUIRect ScrollTarget;
				KeyReader.HMargin(-MARGIN, &ScrollTarget);
				m_SettingsScrollRegion.AddRect(ScrollTarget, true);
			}
			if(KeyReaderResult.m_Aborted)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind == EMPTY_BIND_SLOT && (&CurrentBind - BindOption.m_vCurrentBinds.data()) > 0)
				{
					CurrentBind.m_ToBeDeleted = true;
					m_BindOptionsDirty = true;
				}
			}
			else if(KeyReaderResult.m_Bind != CurrentBind.m_Bind)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind.m_Key != KEY_UNKNOWN || KeyReaderResult.m_Bind.m_Key == KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(CurrentBind.m_Bind.m_Key, "", false, CurrentBind.m_Bind.m_ModifierMask);
				}
				if(KeyReaderResult.m_Bind.m_Key != KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(KeyReaderResult.m_Bind.m_Key, BindOption.m_Command.c_str(), false, KeyReaderResult.m_Bind.m_ModifierMask);
				}
				m_BindOptionsDirty = true;
			}
		}
	}
}

float CMenusSettingsControls::MeasureSettingsMouseHeight() const
{
	return 2.0f * BUTTON_HEIGHT + BUTTON_SPACING;
}

void CMenusSettingsControls::RenderSettingsMouse(CUIRect View)
{
	CUIRect Button;
	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	DoSettingsControlsNumericField("controls-ingame-mouse-sens-label", &g_Config.m_InpMousesens, &g_Config.m_InpMousesens, Button, Localize("Ingame mouse sens."), 1, 500, &CUi::ms_LogarithmicScrollbarScale);

	View.HSplitTop(BIND_OPTION_SPACING, nullptr, &View);
	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	DoSettingsControlsNumericField("controls-ui-mouse-sens-label", &g_Config.m_UiMousesens, &g_Config.m_UiMousesens, Button, Localize("UI mouse sens."), 1, 500, &CUi::ms_LogarithmicScrollbarScale);
}

float CMenusSettingsControls::MeasureSettingsJoystickHeight(const float ContentWidth) const
{
	const bool HasJoystick = Input()->NumJoysticks() > 0 && Input()->GetActiveJoystick() != nullptr;
	const int AxisCount = HasJoystick ? Input()->GetActiveJoystick()->GetNumAxes() : 0;
	return ResolveSettingsControllerContentHeight(ContentWidth, g_Config.m_InpControllerEnable != 0, HasJoystick, g_Config.m_InpControllerAbsolute != 0, AxisCount, NUM_JOYSTICK_AXES, BUTTON_HEIGHT, BUTTON_SPACING);
}

void CMenusSettingsControls::RenderSettingsJoystick(CUIRect View, bool ReadOnly)
{
	CUIRect Button;
	View.HSplitTop(BUTTON_SPACING, nullptr, &View);
	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	const bool WasJoystickEnabled = g_Config.m_InpControllerEnable;
	if(DoSettingsControlsCheckBox(&g_Config.m_InpControllerEnable, "controls-enable-controller", Localize("Enable controller"), g_Config.m_InpControllerEnable, &Button))
	{
		g_Config.m_InpControllerEnable ^= 1;
	}
	if(!WasJoystickEnabled) // Use old value because this was used to allocate the available height
	{
		return;
	}

	const int NumJoysticks = Input()->NumJoysticks();
	if(NumJoysticks > 0)
	{
		// show joystick device selection if more than one available or just the joystick name if there is only one
		{
			CUIRect JoystickDropDown;
			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
			View.HSplitTop(BUTTON_HEIGHT, &JoystickDropDown, &View);
			if(NumJoysticks > 1)
			{
				std::vector<std::string> vJoystickNames;
				std::vector<const char *> vpJoystickNames;
				vJoystickNames.resize(NumJoysticks);
				vpJoystickNames.resize(NumJoysticks);

				for(int i = 0; i < NumJoysticks; ++i)
				{
					char aJoystickName[256];
					str_format(aJoystickName, sizeof(aJoystickName), "%s %d: %s", Localize("Controller"), i, Input()->GetJoystick(i)->GetName());
					vJoystickNames[i] = aJoystickName;
					vpJoystickNames[i] = vJoystickNames[i].c_str();
				}

				const int CurrentJoystick = Input()->GetActiveJoystick()->GetIndex();
				CUi::SDropDownProperties JoystickDropDownProps;
				JoystickDropDownProps.m_pPopupViewport = Ui()->OutermostClipArea();
				const int NewJoystick = GameClient()->m_Menus.DoSettingsDropDown(&JoystickDropDown, CurrentJoystick, vpJoystickNames.data(), vpJoystickNames.size(), m_JoystickDropDownState, JoystickDropDownProps);
				if(NewJoystick != CurrentJoystick)
				{
					Input()->SetActiveJoystick(NewJoystick);
				}
			}
			else
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "%s 0: %s", Localize("Controller"), Input()->GetJoystick(0)->GetName());
				DoSettingsControlsLabel("controls-controller-device-label", &JoystickDropDown, aBuf, FONT_SIZE, TEXTALIGN_ML);
			}
		}

		const bool WasAbsolute = g_Config.m_InpControllerAbsolute;
		GameClient()->m_Menus.DoSettingsLine_RadioMenu(CMenus::SETTINGS_CONTROLS, -1, -1, View, "controls-ingame-controller-mode-label", Localize("Ingame controller mode"),
			m_vJoystickIngameModeButtonContainers,
			{"controls-ingame-controller-mode-relative", "controls-ingame-controller-mode-absolute"},
			{Localize("Relative", "Ingame controller mode"), Localize("Absolute", "Ingame controller mode")},
			{0, 1},
			g_Config.m_InpControllerAbsolute,
			ResolveSettingsContentMetrics(View.w));

		if(!WasAbsolute) // Use old value because this was used to allocate the available height
		{
			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
			View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
			DoSettingsControlsNumericField("controls-ingame-controller-sens-label", &g_Config.m_InpControllerSens, &g_Config.m_InpControllerSens, Button, Localize("Ingame controller sens."), 1, 500,
				&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		}

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		DoSettingsControlsNumericField("controls-ui-controller-sens-label", &g_Config.m_UiControllerSens, &g_Config.m_UiControllerSens, Button, Localize("UI controller sens."), 1, 500,
			&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		DoSettingsControlsNumericField("controls-controller-jitter-tolerance-label", &g_Config.m_InpControllerTolerance, &g_Config.m_InpControllerTolerance, Button, Localize("Controller jitter tolerance"), 0, 50);

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.h = minimum(View.h, ResolveSettingsControllerAxisPickerHeight(Input()->GetActiveJoystick()->GetNumAxes(), NUM_JOYSTICK_AXES, BUTTON_HEIGHT, BUTTON_SPACING));
		if(ReadOnly || m_SettingsScrollRegion.AddRect(View))
		{
			DrawRoundedSurface(Ui(), View, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
			RenderJoystickAxisPicker(View, ReadOnly);
		}
	}
	else
	{
		View.HSplitTop(View.h - BUTTON_HEIGHT, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		DoSettingsControlsLabel("controls-no-controller-label", &Button, Localize("No controller found. Plug in a controller."), FONT_SIZE, TEXTALIGN_ML);
	}
}

void CMenusSettingsControls::RenderJoystickAxisPicker(CUIRect View, bool ReadOnly)
{
	const float AxisWidth = 0.2f * View.w;
	const float StatusWidth = 0.4f * View.w;
	const float AimBindWidth = 90.0f;
	const float SpacingV = (View.w - AxisWidth - StatusWidth - AimBindWidth) / 2.0f;

	CUIRect Row, Axis, Status, AimBind;
	View.HSplitTop(BUTTON_SPACING, nullptr, &View);
	View.HSplitTop(BUTTON_HEIGHT, &Row, &View);
	Row.VSplitLeft(AxisWidth, &Axis, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(StatusWidth, &Status, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

	DoSettingsControlsLabel("controls-axis-header", &Axis, Localize("Axis"), FONT_SIZE, TEXTALIGN_MC);
	DoSettingsControlsLabel("controls-axis-status-header", &Status, Localize("Status"), FONT_SIZE, TEXTALIGN_MC);
	DoSettingsControlsLabel("controls-axis-aim-bind-header", &AimBind, Localize("Aim bind"), FONT_SIZE, TEXTALIGN_MC);

	IInput::IJoystick *pJoystick = Input()->GetActiveJoystick();
	for(int i = 0; i < std::min<int>(pJoystick->GetNumAxes(), NUM_JOYSTICK_AXES); i++)
	{
		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Row, &View);
		if(!ReadOnly && !m_SettingsScrollRegion.AddRect(Row))
		{
			continue;
		}
		DrawRoundedSurface(Ui(), Row, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
		Row.VSplitLeft(AxisWidth, &Axis, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(StatusWidth, &Status, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

		const bool Active = g_Config.m_InpControllerX == i || g_Config.m_InpControllerY == i;

		// Axis label
		char aLabel[16];
		str_format(aLabel, sizeof(aLabel), "%d", i + 1);
		char aLabelId[32];
		str_format(aLabelId, sizeof(aLabelId), "controls-axis-%d-label", i + 1);
		SLabelProperties LabelProps;
		if(!Active)
		{
			LabelProps.SetColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
		}
		DoSettingsControlsLabel(aLabelId, &Axis, aLabel, FONT_SIZE, TEXTALIGN_MC, LabelProps);

		// Axis status
		Status.HMargin(7.0f, &Status);
		RenderJoystickBar(&Status, (pJoystick->GetAxisValue(i) + 1.0f) / 2.0f, g_Config.m_InpControllerTolerance / 50.0f, Active);

		// Bind to X/Y
		CUIRect AimBindX, AimBindY;
		AimBind.VSplitMid(&AimBindX, &AimBindY);
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][0], "X", g_Config.m_InpControllerX == i, &AimBindX))
		{
			if(g_Config.m_InpControllerY == i)
				g_Config.m_InpControllerY = g_Config.m_InpControllerX;
			g_Config.m_InpControllerX = i;
		}
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][1], "Y", g_Config.m_InpControllerY == i, &AimBindY))
		{
			if(g_Config.m_InpControllerX == i)
				g_Config.m_InpControllerX = g_Config.m_InpControllerY;
			g_Config.m_InpControllerY = i;
		}
	}
}

void CMenusSettingsControls::RenderJoystickBar(const CUIRect *pRect, float Current, float Tolerance, bool Active)
{
	CUIRect Handle;
	pRect->VSplitLeft(pRect->h, &Handle, nullptr); // Slider size
	Handle.x += (pRect->w - Handle.w) * Current;

	pRect->Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Active ? 0.25f : 0.125f), IGraphics::CORNER_ALL, pRect->h / 2.0f);

	CUIRect ToleranceArea = *pRect;
	ToleranceArea.w *= Tolerance;
	ToleranceArea.x += (pRect->w - ToleranceArea.w) / 2.0f;
	const ColorRGBA ToleranceColor = Active ? ColorRGBA(0.8f, 0.35f, 0.35f, 1.0f) : ColorRGBA(0.7f, 0.5f, 0.5f, 1.0f);
	ToleranceArea.Draw(ToleranceColor, IGraphics::CORNER_ALL, ToleranceArea.h / 2.0f);

	const ColorRGBA SliderColor = Active ? ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f) : ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	Handle.Draw(SliderColor, IGraphics::CORNER_ALL, Handle.h / 2.0f);
}

void CMenus::ResetSettingsControls()
{
	GameClient()->m_Binds.SetDefaults();

	g_Config.m_InpMousesens = 200;
	g_Config.m_UiMousesens = 200;

	g_Config.m_InpControllerEnable = 0;
	g_Config.m_InpControllerGUID[0] = '\0';
	g_Config.m_InpControllerAbsolute = 0;
	g_Config.m_InpControllerSens = 100;
	g_Config.m_InpControllerX = 0;
	g_Config.m_InpControllerY = 1;
	g_Config.m_InpControllerTolerance = 5;
	g_Config.m_UiControllerSens = 100;
}
