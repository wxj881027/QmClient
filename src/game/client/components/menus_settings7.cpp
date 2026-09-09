/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"
#include "skins7.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <vector>

using namespace FontIcons;

void CMenus::RenderSettingsTee7(CUIRect MainView)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const qm_card_registry::SCardDefault *pEditorDefault = qm_card_registry::FindByStableId("deck:tee7-editor");
	dbg_assert(pEditorDefault != nullptr, "sixup tee settings card must be registered");
	if(pEditorDefault == nullptr)
		return;

	const float ContentHeight = maximum(520.0f * UiScale, Page.m_ScrollViewport.h - 2.0f * ui_token::settings::CARD_PADDING * UiScale);
	const bool RenderOnly = Ui()->RenderOnly();
	const uint64_t CardLayoutRevision = ((uint64_t)str_quickhash("tee7") << 32) ^ (uint64_t)maximum(0, (int)(ContentHeight * 100.0f + 0.5f)) ^ (RenderOnly ? 1u : 0u);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [this, Metrics, ContentHeight, pEditorDefault](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(1);
		SSettingsCardDefinition EditorCard;
		EditorCard.m_Spec = {pEditorDefault->m_pStableId, Localize(pEditorDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pEditorDefault)};
		EditorCard.m_Measure = [ContentHeight](float) { return ContentHeight; };
		EditorCard.m_Render = [this, Metrics](CUIRect Content) { RenderSettingsTee7Content(Content, Metrics); };
		vCards.push_back(std::move(EditorCard));
	};

	static CScrollRegion s_Tee7SettingsScrollRegion;
	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f));
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = RenderOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = RenderOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !RenderOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !RenderOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !RenderOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !RenderOnly && Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const IUiContext CardCtx = SettingsUiContext("settings_tee7", UiScale);
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(CardCtx, Page, "tee7", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_Tee7SettingsScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)
{
	CUIRect SkinPreview, NormalSkinPreview, RedTeamSkinPreview, BlueTeamSkinPreview, Buttons, QuickSearch, DirectoryButton, RefreshButton, SaveDeleteButton, EditTextureButton, TabColumn, TabBar, LeftTab, RightTab, InfoRow;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float BodySize = Metrics.m_BodySize;
	static bool s_Tee7TransitionInitialized = false;
	static bool s_PrevTee7Dummy = false;
	static bool s_PrevTee7Custom = false;
	static float s_Tee7TransitionDirection = 0.0f;
	const uint64_t Tee7SwitchNode = UiAnimNodeKey("settings_tee7_tab_switch");
	const bool CompactToolbar = MainView.w < 700.0f;
	MainView.HSplitBottom(LineHeight * (CompactToolbar ? 2.0f : 1.0f) + LineSpacing * (CompactToolbar ? 2.0f : 1.0f), &MainView, &Buttons);
	if(CompactToolbar)
	{
		CUIRect SearchRow, ActionRow;
		Buttons.HSplitTop(LineHeight, &SearchRow, &ActionRow);
		ActionRow.HSplitTop(LineSpacing, nullptr, &ActionRow);
		SearchRow.VSplitRight(25.0f, &SearchRow, &RefreshButton);
		SearchRow.VSplitRight(LineSpacing * 2.0f, &SearchRow, nullptr);
		SearchRow.VSplitRight(minimum(140.0f, SearchRow.w * 0.36f), &QuickSearch, &DirectoryButton);
		ActionRow.VSplitMid(&SaveDeleteButton, &EditTextureButton, LineSpacing * 2.0f);
	}
	else
	{
		Buttons.VSplitRight(25.0f, &Buttons, &RefreshButton);
		Buttons.VSplitRight(10.0f, &Buttons, nullptr);
		Buttons.VSplitRight(140.0f, &Buttons, &DirectoryButton);
		Buttons.VSplitRight(10.0f, &Buttons, nullptr);
		Buttons.VSplitRight(140.0f, &Buttons, &EditTextureButton);
		Buttons.VSplitRight(10.0f, &Buttons, nullptr);
		Buttons.VSplitRight(120.0f, &QuickSearch, &SaveDeleteButton);
	}
	const CUIRect HeaderSource = MainView;
	HeaderSource.VSplitMid(&TabColumn, &SkinPreview, 20.0f * Metrics.m_UiScale);
	const SSettingsSubTabLayoutFrame PlayerDummyTabs = ResolveSettingsSubTabLayout(TabColumn, Metrics.m_UiScale);
	TabBar = PlayerDummyTabs.m_TabBarRect;
	TabBar.VSplitMid(&LeftTab, &RightTab);
	const SSettingsSubTabLayoutFrame ModeTabs = ResolveSettingsSubTabLayout(PlayerDummyTabs.m_ContentRect, Metrics.m_UiScale);
	CUIRect HeaderRemainder = ModeTabs.m_ContentRect;
	HeaderRemainder.HSplitTop(Metrics.m_InputHeight, &InfoRow, &HeaderRemainder);
	HeaderRemainder.HSplitTop(LineSpacing, nullptr, &HeaderRemainder);
	const float HeaderBottom = HeaderRemainder.y;
	SkinPreview.h = maximum(0.0f, HeaderBottom - SkinPreview.y);
	MainView.y = HeaderBottom;
	MainView.h = maximum(0.0f, HeaderSource.y + HeaderSource.h - HeaderBottom);

	SkinPreview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
	SkinPreview.VMargin(10.0f, &SkinPreview);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &BlueTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &RedTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &NormalSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &LeftTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &RightTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
	}
	const CSkins7::CSkin *pSelectedSkin = GameClient()->m_Skins7.FindSkin(CSkins7::ms_apSkinNameVariables[m_Dummy], false);
	m_SelectedSkin7Name = pSelectedSkin != nullptr ? pSelectedSkin->m_aName : "";

	TabBar = ModeTabs.m_TabBarRect;
	TabBar.VSplitMid(&LeftTab, &RightTab);

	static CButtonContainer s_BasicTabButton;
	if(DoButton_MenuTab(&s_BasicTabButton, Localize("Basic"), !m_CustomSkinMenu, &LeftTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_CustomSkinMenu = false;
	}

	static CButtonContainer s_CustomTabButton;
	if(DoButton_MenuTab(&s_CustomTabButton, Localize("Custom"), m_CustomSkinMenu, &RightTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_CustomSkinMenu = true;
		if(m_CustomSkinMenu && pSelectedSkin)
		{
			if(pSelectedSkin->m_Flags & CSkins7::SKINFLAG_STANDARD)
			{
				m_SkinNameInput.Set("copy_");
				m_SkinNameInput.Append(pSelectedSkin->m_aName);
			}
			else
			{
				m_SkinNameInput.Set(pSelectedSkin->m_aName);
			}
		}
	}

	RenderSettingsTeeIdentity(InfoRow, nullptr);

	if(!Ui()->RenderOnly())
	{
		if(!s_Tee7TransitionInitialized)
		{
			s_PrevTee7Dummy = m_Dummy;
			s_PrevTee7Custom = m_CustomSkinMenu;
			s_Tee7TransitionInitialized = true;
		}
		else if(m_Dummy != s_PrevTee7Dummy || m_CustomSkinMenu != s_PrevTee7Custom)
		{
			if(m_CustomSkinMenu != s_PrevTee7Custom)
				s_Tee7TransitionDirection = m_CustomSkinMenu ? 1.0f : -1.0f;
			else
				s_Tee7TransitionDirection = m_Dummy ? 1.0f : -1.0f;
			TriggerUiSwitchAnimation(Tee7SwitchNode, 0.18f);
			s_PrevTee7Dummy = m_Dummy;
			s_PrevTee7Custom = m_CustomSkinMenu;
		}
	}

	const float TransitionStrength = ReadUiSwitchAnimation(Tee7SwitchNode);
	const bool TransitionActive = TransitionStrength > 0.0f && s_Tee7TransitionDirection != 0.0f;
	const CUIRect ContentClip = MainView;
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(MainView, TransitionStrength, s_Tee7TransitionDirection, false, 0.08f, 24.0f, 120.0f);
	}

	// validate skin parts for solo mode
	char aSkinParts[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_ARRAY_SIZE];
	char *apSkinPartsPtr[protocol7::NUM_SKINPARTS];
	int aUCCVars[protocol7::NUM_SKINPARTS];
	int aColorVars[protocol7::NUM_SKINPARTS];
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		str_copy(aSkinParts[Part], CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], protocol7::MAX_SKIN_ARRAY_SIZE);
		apSkinPartsPtr[Part] = aSkinParts[Part];
		aUCCVars[Part] = *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part];
		aColorVars[Part] = *CSkins7::ms_apColorVariables[(int)m_Dummy][Part];
	}
	GameClient()->m_Skins7.ValidateSkinParts(apSkinPartsPtr, aUCCVars, aColorVars, 0);

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.m_Size = 50.0f;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		GameClient()->m_Skins7.FindSkinPart(Part, apSkinPartsPtr[Part], false)->ApplyTo(OwnSkinInfo.m_aSixup[g_Config.m_ClDummy]);
		GameClient()->m_Skins7.ApplyColorTo(OwnSkinInfo.m_aSixup[g_Config.m_ClDummy], aUCCVars[Part], aColorVars[Part], Part);
	}
	if(!m_Dummy || g_Config.m_QmCycleTeeHueDummy != 0)
	{
		const std::chrono::nanoseconds PreviewNow = time_get_nanoseconds();
		SQmTeeHueCycleConfig HueCycleConfig;
		HueCycleConfig.m_Enabled = g_Config.m_QmCycleTeeHue != 0;
		HueCycleConfig.m_PlayerUsesCustomColors = aUCCVars[protocol7::SKINPART_BODY] || aUCCVars[protocol7::SKINPART_FEET];
		HueCycleConfig.m_TClientRainbowTees = g_Config.m_TcRainbowTees != 0;
		HueCycleConfig.m_SpeedDegreesPerSecond = g_Config.m_QmCycleTeeHueSpeed;
		HueCycleConfig.m_TimeSeconds = PreviewNow.count() / 1000000000.0;
		HueCycleConfig.m_SixupIndex = g_Config.m_ClDummy;
		QmApplyTeeHueCycle(OwnSkinInfo, HueCycleConfig);
	}

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&SkinPreview, aBuf, BodySize, TEXTALIGN_ML);

	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
	{
		// interactive tee: tee looking towards cursor, and it is happy when you touch it
		const vec2 TeePosition = NormalSkinPreview.Center() + OffsetToMid;
		const vec2 DeltaPosition = Ui()->MousePos() - TeePosition;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, maximum(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : EMOTE_NORMAL;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeePosition);
		static char s_InteractiveTeeButtonId;
		if(Distance < InteractionDistance && Ui()->DoButtonLogic(&s_InteractiveTeeButtonId, 0, &NormalSkinPreview, BUTTONFLAG_LEFT))
		{
			GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_PLAYER_SPAWN, 1.0f);
		}
	}

	// validate skin parts for team game mode
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		str_copy(aSkinParts[Part], CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], protocol7::MAX_SKIN_ARRAY_SIZE);
		apSkinPartsPtr[Part] = aSkinParts[Part];
		aUCCVars[Part] = *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part];
		aColorVars[Part] = *CSkins7::ms_apColorVariables[(int)m_Dummy][Part];
	}
	GameClient()->m_Skins7.ValidateSkinParts(apSkinPartsPtr, aUCCVars, aColorVars, GAMEFLAG_TEAMS);

	CTeeRenderInfo TeamSkinInfo;
	TeamSkinInfo.m_Size = OwnSkinInfo.m_Size;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		GameClient()->m_Skins7.FindSkinPart(Part, apSkinPartsPtr[Part], false)->ApplyTo(TeamSkinInfo.m_aSixup[g_Config.m_ClDummy]);
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aUseCustomColors[Part] = aUCCVars[Part];
	}

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aColors[Part] = GameClient()->m_Skins7.GetTeamColor(aUCCVars[Part], aColorVars[Part], TEAM_RED, Part);
	}
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeamSkinInfo, 0, vec2(1, 0), RedTeamSkinPreview.Center() + OffsetToMid);

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aColors[Part] = GameClient()->m_Skins7.GetTeamColor(aUCCVars[Part], aColorVars[Part], TEAM_BLUE, Part);
	}
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeamSkinInfo, 0, vec2(-1, 0), BlueTeamSkinPreview.Center() + OffsetToMid);

	if(m_CustomSkinMenu)
		RenderSettingsTeeCustom7(MainView, Metrics);
	else
		RenderSkinSelection7(MainView, BodySize);

	if(m_CustomSkinMenu)
	{
		static CButtonContainer s_CustomSkinSaveButton;
		if(DoButton_Menu(&s_CustomSkinSaveButton, Localize("Save"), 0, &SaveDeleteButton))
		{
			m_Popup = POPUP_SAVE_SKIN;
			m_SkinNameInput.SelectAll();
			Ui()->SetActiveItem(&m_SkinNameInput);
		}
	}
	else if(pSelectedSkin && (pSelectedSkin->m_Flags & CSkins7::SKINFLAG_STANDARD) == 0)
	{
		static CButtonContainer s_CustomSkinDeleteButton;
		if(DoButton_Menu(&s_CustomSkinDeleteButton, Localize("Delete"), 0, &SaveDeleteButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_DELETE))
		{
			str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete '%s'?"), pSelectedSkin->m_aName);
			PopupConfirm(Localize("Delete skin"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSkin7);
		}
	}

	static CButtonContainer s_EditSkinTextureButton;
	if(DoButton_Menu(&s_EditSkinTextureButton, Localize("Edit skin texture"), 0, &EditTextureButton))
		AssetsEditorOpen(ASSETS_EDITOR_TYPE_SKIN);

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	const IUiContext Tee7SkinSearchCtx = SettingsUiContext("settings_tee7_skin_search");
	ui_widget::SInputFieldOptions SkinSearchOptions;
	SkinSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SkinSearchOptions.m_Clearable = true;
	SkinSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	SkinSearchOptions.m_FontSize = BodySize;
	if(ui_widget::InputField(Tee7SkinSearchCtx, &s_SkinFilterInput, QuickSearch, SkinSearchOptions).m_Changed)
	{
		m_SkinList7LastRefreshTime = std::nullopt;
		m_SkinPartsList7LastRefreshTime = std::nullopt;
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins7", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins7", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(!Ui()->RenderOnly() && (DoButton_Menu(&s_SkinRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) ||
					  (!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && (Input()->KeyPress(KEY_F5) || (Input()->ModifierIsPressed() && Input()->KeyPress(KEY_R))))))
	{
		// reset render flags for possible loading screen
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SEVEN);
		m_SelectedSkin7Name.clear();
		m_SkinList7LastRefreshTime = std::nullopt;
		m_SkinPartsList7LastRefreshTime = std::nullopt;
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(TransitionActive)
	{
		Ui()->ClipDisable();
	}
}

void CMenus::PopupConfirmDeleteSkin7()
{
	dbg_assert(!m_SelectedSkin7Name.empty(), "no skin selected for deletion");

	if(!GameClient()->m_Skins7.RemoveSkin(m_SelectedSkin7Name.c_str()))
	{
		PopupMessage(Localize("Error"), Localize("Unable to delete skin"), Localize("Ok"));
		return;
	}
	m_SelectedSkin7Name.clear();
}

void CMenus::RenderSettingsTeeCustom7(CUIRect MainView, const SSettingsContentMetrics &Metrics)
{
	CUIRect ButtonBar, SkinPartSelection, CustomColors;
	const float BodySize = Metrics.m_BodySize;
	static bool s_SkinPartTransitionInitialized = false;
	static int s_PrevSkinPart = 0;
	static float s_SkinPartTransitionDirection = 0.0f;
	const uint64_t SkinPartSwitchNode = UiAnimNodeKey("settings_tee7_skinpart_switch");

	const SSettingsSubTabLayoutFrame SkinPartTabs = ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale);
	ButtonBar = SkinPartTabs.m_TabBarRect;
	MainView = SkinPartTabs.m_ContentRect;

	const float ButtonWidth = ButtonBar.w / (float)protocol7::NUM_SKINPARTS;

	static CButtonContainer s_aSkinPartButtons[protocol7::NUM_SKINPARTS];
	for(int i = 0; i < protocol7::NUM_SKINPARTS; i++)
	{
		CUIRect Button;
		ButtonBar.VSplitLeft(ButtonWidth, &Button, &ButtonBar);
		const int Corners = i == 0 ? IGraphics::CORNER_TL : (i == (protocol7::NUM_SKINPARTS - 1) ? IGraphics::CORNER_TR : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aSkinPartButtons[i], Localize(CSkins7::ms_apSkinPartNamesLocalized[i], "skins"), m_TeePartSelected == i, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			m_TeePartSelected = i;
		}
	}

	if(!Ui()->RenderOnly())
	{
		if(!s_SkinPartTransitionInitialized)
		{
			s_PrevSkinPart = m_TeePartSelected;
			s_SkinPartTransitionInitialized = true;
		}
		else if(m_TeePartSelected != s_PrevSkinPart)
		{
			s_SkinPartTransitionDirection = m_TeePartSelected > s_PrevSkinPart ? 1.0f : -1.0f;
			TriggerUiSwitchAnimation(SkinPartSwitchNode, 0.18f);
			s_PrevSkinPart = m_TeePartSelected;
		}
	}

	const float TransitionStrength = ReadUiSwitchAnimation(SkinPartSwitchNode);
	const bool TransitionActive = TransitionStrength > 0.0f && s_SkinPartTransitionDirection != 0.0f;
	const CUIRect ContentClip = MainView;
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(MainView, TransitionStrength, s_SkinPartTransitionDirection, false, 0.08f, 24.0f, 120.0f);
	}

	MainView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_B, 5.0f);
	MainView.VSplitMid(&SkinPartSelection, &CustomColors, 10.0f);
	CustomColors.Margin(5.0f, &CustomColors);
	CUIRect CustomColorsButton, RandomSkinButton;
	CustomColors.HSplitTop(20.0f, &CustomColorsButton, &CustomColors);
	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(20.0f, &CustomColorsButton, nullptr);

	RenderSkinPartSelection7(SkinPartSelection, BodySize);

	int *pUseCustomColor = CSkins7::ms_apUCCVariables[(int)m_Dummy][m_TeePartSelected];
	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = !*pUseCustomColor;
		SetNeedSendInfo();
	}

	if(*pUseCustomColor)
	{
		CUIRect CustomColorScrollbars;
		CustomColors.HSplitTop(Metrics.m_LineSpacing, nullptr, &CustomColors);
		CustomColors.HSplitTop(ResolveSettingsHslaRowsHeight(Metrics, m_TeePartSelected == protocol7::SKINPART_MARKING), &CustomColorScrollbars, &CustomColors);

		if(RenderHslaScrollbars(&CustomColorScrollbars, CSkins7::ms_apColorVariables[(int)m_Dummy][m_TeePartSelected], m_TeePartSelected == protocol7::SKINPART_MARKING, ColorHSLA::DARKEST_LGT7, Metrics))
		{
			SetNeedSendInfo();
		}
	}

	// Random skin button
	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FONT_ICON_DICE_ONE, FONT_ICON_DICE_TWO, FONT_ICON_DICE_THREE, FONT_ICON_DICE_FOUR, FONT_ICON_DICE_FIVE, FONT_ICON_DICE_SIX};
	static int s_CurrentDie = rand() % std::size(s_apDice);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(DoButton_Menu(&s_RandomSkinButton, s_apDice[s_CurrentDie], 0, &RandomSkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
	{
		GameClient()->m_Skins7.RandomizeSkin(m_Dummy);
		SetNeedSendInfo();
		s_CurrentDie = rand() % std::size(s_apDice);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkinButton, &RandomSkinButton, Localize("Create a random skin"));

	if(TransitionActive)
	{
		Ui()->ClipDisable();
	}
}

void CMenus::RenderSkinSelection7(CUIRect MainView, float BodySize)
{
	static float s_LastSelectionTime = -10.0f;
	static std::vector<std::string> s_vSkinNames;
	static CListBox s_ListBox;
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);

	const auto RefreshTime = GameClient()->m_Skins7.LastRefreshTime();
	if(GameClient()->m_Skins7.IsLoading() || !m_SkinList7LastRefreshTime.has_value() || m_SkinList7LastRefreshTime.value() != RefreshTime)
	{
		s_vSkinNames.clear();
		for(const CSkins7::CSkin &Skin : GameClient()->m_Skins7.GetSkins())
		{
			if((Skin.m_Flags & CSkins7::SKINFLAG_SPECIAL) != 0)
				continue;
			if(g_Config.m_ClSkinFilterString[0] != '\0' && !str_utf8_find_nocase(Skin.m_aName, g_Config.m_ClSkinFilterString))
				continue;

			s_vSkinNames.emplace_back(Skin.m_aName);
		}
		m_SkinList7LastRefreshTime = RefreshTime;
	}

	m_SelectedSkin7Name.clear();
	int OldSelected = -1;
	for(int i = 0; i < (int)s_vSkinNames.size(); ++i)
	{
		if(!str_comp(s_vSkinNames[i].c_str(), CSkins7::ms_apSkinNameVariables[m_Dummy]))
		{
			m_SelectedSkin7Name = s_vSkinNames[i];
			OldSelected = i;
			break;
		}
	}
	s_ListBox.DoStart(50.0f, s_vSkinNames.size(), 4, 1, OldSelected, &MainView);

	for(const std::string &SkinName : s_vSkinNames)
	{
		const CSkins7::CSkin *pSkin = GameClient()->m_Skins7.FindSkin(SkinName.c_str(), false);
		const CListboxItem Item = s_ListBox.DoNextItem(SkinName.c_str());
		if(!Item.m_Visible)
			continue;
		if(pSkin == nullptr)
			continue;

		CUIRect TeePreview, Label;
		Item.m_Rect.VSplitLeft(60.0f, &TeePreview, &Label);

		CTeeRenderInfo Info;
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			pSkin->m_apParts[Part]->ApplyTo(Info.m_aSixup[g_Config.m_ClDummy]);
			GameClient()->m_Skins7.ApplyColorTo(Info.m_aSixup[g_Config.m_ClDummy], pSkin->m_aUseCustomColors[Part], pSkin->m_aPartColors[Part], Part);
		}
		Info.m_Size = 50.0f;

		{
			// interactive tee: tee is happy to be selected
			int TeeEmote = (Item.m_Selected && s_LastSelectionTime + 0.75f > Client()->GlobalTime()) ? EMOTE_HAPPY : EMOTE_NORMAL;
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, TeeEmote, vec2(1.0f, 0.0f), TeePreview.Center() + OffsetToMid);
		}

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w - 5.0f;
		Ui()->DoLabel(&Label, pSkin->m_aName, BodySize, TEXTALIGN_ML, Props);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected != -1 && NewSelected != OldSelected)
	{
		s_LastSelectionTime = Client()->GlobalTime();
		const CSkins7::CSkin *pNewSkin = GameClient()->m_Skins7.FindSkin(s_vSkinNames[NewSelected].c_str(), false);
		if(pNewSkin == nullptr)
			return;
		m_SelectedSkin7Name = pNewSkin->m_aName;
		str_copy(CSkins7::ms_apSkinNameVariables[m_Dummy], pNewSkin->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			str_copy(CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], pNewSkin->m_apParts[Part]->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
			*CSkins7::ms_apUCCVariables[(int)m_Dummy][Part] = pNewSkin->m_aUseCustomColors[Part];
			*CSkins7::ms_apColorVariables[(int)m_Dummy][Part] = pNewSkin->m_aPartColors[Part];
		}
		SetNeedSendInfo();
	}
}

void CMenus::RenderSkinPartSelection7(CUIRect MainView, float BodySize)
{
	static std::vector<std::string> s_avPartNames[protocol7::NUM_SKINPARTS];
	static CListBox s_ListBox;
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
	const auto RefreshTime = GameClient()->m_Skins7.LastRefreshTime();
	if(GameClient()->m_Skins7.IsLoading() || !m_SkinPartsList7LastRefreshTime.has_value() || m_SkinPartsList7LastRefreshTime.value() != RefreshTime)
	{
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			s_avPartNames[Part].clear();
			for(const CSkins7::CSkinPart &SkinPart : GameClient()->m_Skins7.GetSkinParts(Part))
			{
				if((SkinPart.m_Flags & CSkins7::SKINFLAG_SPECIAL) != 0)
					continue;

				if(g_Config.m_ClSkinFilterString[0] != '\0' && !str_utf8_find_nocase(SkinPart.m_aName, g_Config.m_ClSkinFilterString))
					continue;

				s_avPartNames[Part].emplace_back(SkinPart.m_aName);
			}
		}
		m_SkinPartsList7LastRefreshTime = RefreshTime;
	}

	int OldSelected = -1;
	for(int i = 0; i < (int)s_avPartNames[m_TeePartSelected].size(); ++i)
	{
		if(!str_comp(s_avPartNames[m_TeePartSelected][i].c_str(), CSkins7::ms_apSkinVariables[(int)m_Dummy][m_TeePartSelected]))
		{
			OldSelected = i;
			break;
		}
	}
	s_ListBox.DoStart(72.0f, s_avPartNames[m_TeePartSelected].size(), 4, 1, OldSelected, &MainView, false, IGraphics::CORNER_NONE);

	for(const std::string &PartName : s_avPartNames[m_TeePartSelected])
	{
		const CSkins7::CSkinPart *pPart = GameClient()->m_Skins7.FindSkinPartOrNullptr(m_TeePartSelected, PartName.c_str(), false);
		CListboxItem Item = s_ListBox.DoNextItem(PartName.c_str());
		if(!Item.m_Visible)
			continue;
		if(pPart == nullptr)
			continue;

		CUIRect Label;
		Item.m_Rect.Margin(5.0f, &Item.m_Rect);
		Item.m_Rect.HSplitBottom(BodySize, &Item.m_Rect, &Label);

		CTeeRenderInfo Info;
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			const CSkins7::CSkinPart *pPreviewPart = (m_TeePartSelected == Part ? pPart : GameClient()->m_Skins7.FindSkinPart(Part, CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], false));
			pPreviewPart->ApplyTo(Info.m_aSixup[g_Config.m_ClDummy]);
			GameClient()->m_Skins7.ApplyColorTo(Info.m_aSixup[g_Config.m_ClDummy], *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part], *CSkins7::ms_apColorVariables[(int)m_Dummy][Part], Part);
		}
		Info.m_Size = 50.0f;

		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
		const vec2 TeePos = Item.m_Rect.Center() + OffsetToMid;
		if(m_TeePartSelected == protocol7::SKINPART_HANDS)
		{
			// RenderTools()->RenderTeeHand(&Info, TeePos, vec2(1.0f, 0.0f), -pi*0.5f, vec2(18, 0));
		}
		int TeePartEmote = EMOTE_NORMAL;
		if(m_TeePartSelected == protocol7::SKINPART_EYES)
		{
			TeePartEmote = (int)(Client()->GlobalTime() * 0.5f) % NUM_EMOTES;
		}
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, TeePartEmote, vec2(1.0f, 0.0f), TeePos);

		Ui()->DoLabel(&Label, pPart->m_aName, BodySize, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected != -1 && NewSelected != OldSelected)
	{
		str_copy(CSkins7::ms_apSkinVariables[(int)m_Dummy][m_TeePartSelected], s_avPartNames[m_TeePartSelected][NewSelected].c_str(), protocol7::MAX_SKIN_ARRAY_SIZE);
		CSkins7::ms_apSkinNameVariables[m_Dummy][0] = '\0';
		SetNeedSendInfo();
	}
}
