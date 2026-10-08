/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
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
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/skins7.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <vector>

using namespace FontIcons;

uint64_t CMenus::BuildTee7SettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	const auto *pDefault = qm_card_registry::FindByStableId("deck:tee7-editor");
	const float ContentHeight = maximum(520.0f * Ctx.m_Metrics.m_UiScale, Ctx.m_Page.m_ScrollViewport.h - 2.0f * ui_token::settings::CARD_PADDING * Ctx.m_Metrics.m_UiScale);
	const uint64_t Revision = static_cast<uint64_t>(maximum(0, (int)(ContentHeight * 100.0f + 0.5f)));
	if(pCards != nullptr && pDefault != nullptr)
	{
		SSettingsCardDefinition Card;
		Card.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		Card.m_Measure = [ContentHeight](float) { return ContentHeight; };
		Card.m_Render = [this, Metrics = Ctx.m_Metrics](CUIRect Content) { RenderSettingsTee7Content(Content, Metrics); };
		pCards->push_back(std::move(Card));
	}
	return Revision;
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

	SkinPreview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
	SkinPreview.VMargin(10.0f, &SkinPreview);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &BlueTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &RedTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &NormalSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);

	static CButtonContainer s_PlayerTabButton;
	static CButtonContainer s_DummyTabButton;
	// 胶囊 Tabbar：容器与滑块先画，页签文字随后，滑块压在文字之下。
	const CUIRect aPlayerDummySlots[] = {LeftTab, RightTab};
	ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash("settings_tee7_player_dummy_tabs_capsule"), aPlayerDummySlots, std::size(aPlayerDummySlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &LeftTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE, nullptr, nullptr, -1.0f, true))
	{
		m_Dummy = false;
		m_TeeEntranceStartTime = time_get();
	}
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &RightTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE, nullptr, nullptr, -1.0f, true))
	{
		m_Dummy = true;
		m_TeeEntranceStartTime = time_get();
	}
	const CSkins7::CSkin *pSelectedSkin = GameClient()->m_Skins7.FindSkin(CSkins7::ms_apSkinNameVariables[m_Dummy], false);
	m_SelectedSkin7Name = pSelectedSkin != nullptr ? pSelectedSkin->m_aName : "";

	TabBar = ModeTabs.m_TabBarRect;
	TabBar.VSplitMid(&LeftTab, &RightTab);

	static CButtonContainer s_BasicTabButton;
	static CButtonContainer s_CustomTabButton;
	// 胶囊 Tabbar：容器与滑块先画，页签文字随后，滑块压在文字之下。
	const CUIRect aModeTabSlots[] = {LeftTab, RightTab};
	ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash("settings_tee7_mode_tabs_capsule"), aModeTabSlots, std::size(aModeTabSlots), m_CustomSkinMenu ? 1 : 0, SettingsCapsuleTabBarStyle());
	const bool ClickedBasicTab = DoButton_MenuTab(&s_BasicTabButton, Localize("Basic"), !m_CustomSkinMenu, &LeftTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE, nullptr, nullptr, -1.0f, true) != 0;
	const bool ClickedCustomTab = DoButton_MenuTab(&s_CustomTabButton, Localize("Custom"), m_CustomSkinMenu, &RightTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE, nullptr, nullptr, -1.0f, true) != 0;
	if(ClickedBasicTab)
	{
		m_CustomSkinMenu = false;
	}
	if(ClickedCustomTab)
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
		HueCycleConfig.m_TClientRainbowTees = g_Config.m_QmRainbowTees != 0;
		HueCycleConfig.m_SpeedDegreesPerSecond = g_Config.m_QmCycleTeeHueSpeed;
		HueCycleConfig.m_TimeSeconds = PreviewNow.count() / 1000000000.0;
		HueCycleConfig.m_SixupIndex = g_Config.m_ClDummy;
		QmApplyTeeHueCycle(OwnSkinInfo, HueCycleConfig);
	}

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&SkinPreview, aBuf, BodySize, TEXTALIGN_ML);

	const bool MotionEnabled = g_Config.m_QmUiMotionLevel > 0;
	float TeeScale = 1.0f;
	if(MotionEnabled && m_TeeEntranceStartTime > 0)
	{
		const float Duration = g_Config.m_QmUiMotionLevel == 1 ? 0.16f : COUNTRY_FLAG_ANIM_DURATION;
		const float Overshoot = g_Config.m_QmUiMotionLevel == 1 ? 1.4f : COUNTRY_FLAG_ANIM_OVERSHOOT;
		const float Elapsed = (time_get() - m_TeeEntranceStartTime) / (float)time_freq();
		if(Elapsed >= 0.0f && Elapsed < Duration)
		{
			const float Progress = Elapsed / Duration;
			TeeScale = ComputeCountryFlagEntryScale(Progress, Overshoot);
		}
	}
	OwnSkinInfo.m_Size *= TeeScale;

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
	if(!Ui()->RenderOnly() && (DoButton_Menu_QmIcon(&s_SkinRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) ||
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
