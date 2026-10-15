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
#include <game/client/QmUi/SettingsContentLayout.h>
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

#include <SDL3/SDL_audio.h>

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

namespace qm_tclient_cards
{
	float TClientDeclaredRegion(const char *pId, float Width, const std::function<void(CUIRect &)> &Render, CUIRect *pRect);
}

qm_card_catalog::STClientCardResult CMenus::RunTClientProfilesCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CPerfTimer RenderTimer;
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsContentMetrics ProfileMetrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	IUiContext ProfilesCtx = SettingsUiContext("settings_tclient_profiles", UiScale);
	if(ReadOnly)
	{
		ProfilesCtx.m_pAnim = nullptr;
		ProfilesCtx.m_pTree = nullptr;
	}
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const int CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const int CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;
	static int s_AllowDelete = 0;
	auto &vProfiles = GameClient()->m_SkinProfiles.m_Profiles;
	int PreviewSelectedProfile = s_SelectedProfile;
	int &SelectedProfile = ReadOnly || Ui()->RenderOnly() ? PreviewSelectedProfile : s_SelectedProfile;
	if(SelectedProfile >= (int)vProfiles.size())
		SelectedProfile = vProfiles.empty() ? -1 : (int)vProfiles.size() - 1;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		const float PreviewTeeSize = ProfileMetrics.m_ButtonHeight * 2.0f;
		const float PreviewFlagHeight = ProfileMetrics.m_ButtonHeight;
		const float PreviewColorSize = ProfileMetrics.m_LineSpacing * 2.0f;
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			// 未覆盖字段使用中性短横线，避免与删除操作的 destructive 图标混淆。
			const float Extent = std::min(MaxSize > 0.0f ? MaxSize : Cross.h * 0.4f, Cross.w * 0.5f);
			CUIRect Placeholder{Cross.Center().x - Extent * 0.5f, Cross.Center().y - 1.0f, Extent, 2.0f};
			Placeholder.Draw(ColorRGBA(0.65f, 0.65f, 0.65f, 0.8f), IGraphics::CORNER_ALL, 1.0f);
		};
		{
			CUIRect Skin;
			Rect.VSplitLeft(PreviewTeeSize, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor >= 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = PreviewTeeSize;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(PreviewColorSize, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - PreviewColorSize * 0.5f, Colors.Center().y - PreviewColorSize * 0.5f - ProfileMetrics.m_LineSpacing, PreviewColorSize, PreviewColorSize};
			CUIRect FeetColor{Colors.Center().x - PreviewColorSize * 0.5f, Colors.Center().y + ProfileMetrics.m_LineSpacing, PreviewColorSize, PreviewColorSize};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor >= 0)
			{
				// Body Color
				DrawRoundedSurface(Ui(), BodyColor,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f), ColorRGBA(), 2.0f);
				// Feet Color;
				DrawRoundedSurface(Ui(), FeetColor,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f), ColorRGBA(), 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(PreviewFlagHeight * 2.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - PreviewFlagHeight) / 2.0f, Flag.w, PreviewFlagHeight};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
			{
				const int CountryCode = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : Profile.m_CountryFlag;
				GameClient()->m_CountryFlags.Render(CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
			}
		}
		Rect.VSplitRight(ProfileMetrics.m_LineSpacing, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, ProfileMetrics.m_BodySize, TEXTALIGN_ML);
			}
		}
	};

	auto IsSelectedProfileValid = [&]() {
		return SelectedProfile >= 0 && SelectedProfile < (int)vProfiles.size();
	};

	auto pSelectedProfile = [&]() -> CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[SelectedProfile];
	};

	auto pConstSelectedProfile = [&]() -> const CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[SelectedProfile];
	};

	auto BuildProfileFromCurrentSettings = [&]() {
		return CProfile(
			g_Config.m_QmProfileColors ? CurrentColorBody : -1,
			g_Config.m_QmProfileColors ? CurrentColorFeet : -1,
			g_Config.m_QmProfileFlag ? CurrentFlag : -2,
			g_Config.m_QmProfileEmote ? Emote : -1,
			g_Config.m_QmProfileSkin ? pCurrentSkinName : "",
			g_Config.m_QmProfileName ? pCurrentName : "",
			g_Config.m_QmProfileClan ? pCurrentClan : "");
	};

	auto BuildPreviewProfile = [&]() {
		CProfile PreviewProfile = CurrentProfile;
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return PreviewProfile;

		if(g_Config.m_QmProfileSkin && pProfile->m_SkinName[0] != '\0')
			str_copy(PreviewProfile.m_SkinName, pProfile->m_SkinName);
		if(g_Config.m_QmProfileColors && pProfile->m_BodyColor != -1 && pProfile->m_FeetColor != -1)
		{
			PreviewProfile.m_BodyColor = pProfile->m_BodyColor;
			PreviewProfile.m_FeetColor = pProfile->m_FeetColor;
		}
		if(g_Config.m_QmProfileEmote && pProfile->m_Emote != -1)
			PreviewProfile.m_Emote = pProfile->m_Emote;
		if(g_Config.m_QmProfileName && pProfile->m_Name[0] != '\0')
			str_copy(PreviewProfile.m_Name, pProfile->m_Name);
		if(g_Config.m_QmProfileClan && (pProfile->m_Clan[0] != '\0' || g_Config.m_QmProfileOverwriteClanWithEmpty))
			str_copy(PreviewProfile.m_Clan, pProfile->m_Clan);
		if(g_Config.m_QmProfileFlag && pProfile->m_CountryFlag != -2)
			PreviewProfile.m_CountryFlag = pProfile->m_CountryFlag;

		return PreviewProfile;
	};

	auto ApplySelectedProfile = [&]() {
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return;
		GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, *pProfile);
		if(g_Config.m_QmProfileSkin || g_Config.m_QmProfileColors)
			GameClient()->m_Skins.RecordRecentSkin(m_Dummy);
	};

	auto DeleteSelectedProfile = [&]() {
		if(!IsSelectedProfileValid())
			return;
		vProfiles.erase(vProfiles.begin() + SelectedProfile);
		if(vProfiles.empty())
			SelectedProfile = -1;
		else if(SelectedProfile >= (int)vProfiles.size())
			SelectedProfile = (int)vProfiles.size() - 1;
	};

	auto RenderProfilePreview = [&](CUIRect Profiles) {
		CUIRect Skin;
		Profiles.HSplitTop(LineSize, &Label, &Profiles);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("Your profile"), FontSize, TEXTALIGN_ML);
		Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
		Profiles.HSplitTop(LineSize * 3.0f, &Skin, &Profiles);
		DrawRoundedSurface(Ui(), Skin, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f), ColorRGBA(), 4.0f);
		Skin.Margin(ProfileMetrics.m_LineSpacing, &Skin);
		const float PreviewRowWidth = std::min(Skin.w, ProfileMetrics.m_LabelWidth * 2.5f);
		Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);
		RenderProfile(Skin, CurrentProfile, true);
		if(pConstSelectedProfile())
		{
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize("After Load"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(LineSize * 3.0f, &Skin, &Profiles);
			DrawRoundedSurface(Ui(), Skin, ColorRGBA(0.25f, 0.55f, 0.85f, 0.08f), ColorRGBA(), 4.0f);
			Skin.Margin(ProfileMetrics.m_LineSpacing, &Skin);
			const float PreviewRowWidth = std::min(Skin.w, ProfileMetrics.m_LabelWidth * 2.5f);
			Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);
			RenderProfile(Skin, BuildPreviewProfile(), true);
		}
	};

	auto RenderActionButtons = [&](CUIRect &Actions) {
		CSettingsContentLayoutFlow Flow(Actions);
		CUIRect Row;
		static CButtonContainer s_LoadButton, s_SaveButton, s_DeleteButton, s_OverrideButton;
		if(Flow.Control("tclient-profile-load", ProfileMetrics.m_ButtonHeight, Row))
		{
			if(DoTClientSettingsButton_Menu(&s_LoadButton, "tclient-profile-load", Localize("Load"), 0, &Row) && !ReadOnly && !Ui()->RenderOnly())
				ApplySelectedProfile();
		}
		if(Flow.Control("tclient-profile-save", ProfileMetrics.m_ButtonHeight, Row, MarginSmall))
		{
			if(DoTClientSettingsButton_Menu(&s_SaveButton, "tclient-profile-save", Localize("Save"), 0, &Row) && !ReadOnly && !Ui()->RenderOnly())
			{
				const CProfile ProfileToSave = BuildProfileFromCurrentSettings();
				GameClient()->m_SkinProfiles.AddProfile(ProfileToSave.m_BodyColor, ProfileToSave.m_FeetColor, ProfileToSave.m_CountryFlag, ProfileToSave.m_Emote, ProfileToSave.m_SkinName, ProfileToSave.m_Name, ProfileToSave.m_Clan);
			}
		}
		if(Flow.Control("tclient-profile-enable-deleting", LineSize, Row, MarginSmall))
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, "tclient-profile-enable-deleting", Localizable("Enable Deleting"), &s_AllowDelete, &Row, LineSize);
		if(s_AllowDelete)
		{
			if(Flow.Control("tclient-profile-delete", ProfileMetrics.m_ButtonHeight, Row, MarginSmall))
			{
				if(DoTClientSettingsButton_Menu(&s_DeleteButton, "tclient-profile-delete", Localize("Delete"), 0, &Row) && !ReadOnly && !Ui()->RenderOnly())
					DeleteSelectedProfile();
			}
			if(Flow.Control("tclient-profile-override", ProfileMetrics.m_ButtonHeight, Row, MarginSmall))
			{
				if(DoTClientSettingsButton_Menu(&s_OverrideButton, "tclient-profile-override", Localize("Override"), 0, &Row) && !ReadOnly && !Ui()->RenderOnly())
				{
					if(CProfile *pProfile = pSelectedProfile())
						*pProfile = BuildProfileFromCurrentSettings();
				}
			}
		}
	};

	auto RenderActions = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		const auto *pScope = CSettingsContentLayoutScope::Current();
		const float Width = pScope && pScope->m_pLayout ? pScope->m_pLayout->Width() : Content.w;
		const float InlineMinimum = ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
		const bool Inline = Width >= InlineMinimum;
		const float PreviewWidth = Inline ? std::min(ProfileMetrics.m_LabelWidth * 2.5f, Width * 0.55f) : Width;
		const float ActionsWidth = Inline ? std::max(0.0f, Width - PreviewWidth - MarginSmall * 2.0f) : Width;
		const auto PreviewContent = [&](CUIRect &Rect) {
			CSettingsContentLayoutFlow PreviewFlow(Rect);
			CUIRect Block;
			const float Height = pConstSelectedProfile() ? LineSize * 8.0f + MarginSmall * 3.0f : LineSize * 4.0f + MarginSmall;
			if(PreviewFlow.Control("tclient-profile-preview", Height, Block))
				RenderProfilePreview(Block);
		};
		const float PreviewHeight = TClientDeclaredRegion("profile-preview", PreviewWidth, PreviewContent, nullptr);
		const float ActionsHeight = TClientDeclaredRegion("profile-buttons", ActionsWidth, RenderActionButtons, nullptr);
		CUIRect Block;
		if(!Flow.Control("tclient-profile-actions-area", Inline ? std::max(PreviewHeight, ActionsHeight) : PreviewHeight + MarginSmall + ActionsHeight, Block))
			return;
		CUIRect Preview = Block, Actions = Block;
		Preview.w = PreviewWidth;
		Preview.h = PreviewHeight;
		Actions.w = ActionsWidth;
		Actions.h = ActionsHeight;
		if(Inline)
			Actions.x += PreviewWidth + MarginSmall;
		else
			Actions.y += PreviewHeight + MarginSmall;
		TClientDeclaredRegion("profile-preview", PreviewWidth, PreviewContent, &Preview);
		TClientDeclaredRegion("profile-buttons", ActionsWidth, RenderActionButtons, &Actions);
	};

	auto RenderOptions = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		const auto *pScope = CSettingsContentLayoutScope::Current();
		const float Width = pScope && pScope->m_pLayout ? pScope->m_pLayout->Width() : Content.w;
		const bool Inline = Width >= ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth + 2.0f * ProfileMetrics.m_ButtonHeight, ProfileMetrics.m_SectionGap, 1);
		const float ColumnWidth = Inline ? std::max(0.0f, (Width - MarginSmall) * 0.5f) : Width;
		const auto RenderSaveLoad = [&](CUIRect &View) {
			CSettingsContentLayoutFlow Flow(View);
			CUIRect Row;
			if(Flow.Control("tclient-profile-save-load-skin", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileSkin, "tclient-profile-save-load-skin", Localize("Save/Load Skin"), &g_Config.m_QmProfileSkin, &Row, LineSize);
			if(Flow.Control("tclient-profile-save-load-colors", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileColors, "tclient-profile-save-load-colors", Localize("Save/Load Colors"), &g_Config.m_QmProfileColors, &Row, LineSize);
			if(Flow.Control("tclient-profile-save-load-emote", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileEmote, "tclient-profile-save-load-emote", Localize("Save/Load Emote"), &g_Config.m_QmProfileEmote, &Row, LineSize);
			if(Flow.Control("tclient-profile-save-load-name", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileName, "tclient-profile-save-load-name", Localize("Save/Load Name"), &g_Config.m_QmProfileName, &Row, LineSize);
			if(Flow.Control("tclient-profile-save-load-clan", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileClan, "tclient-profile-save-load-clan", Localize("Save/Load Clan"), &g_Config.m_QmProfileClan, &Row, LineSize);
			if(Flow.Control("tclient-profile-save-load-flag", LineSize, Row, MarginSmall))
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmProfileFlag, "tclient-profile-save-load-flag", Localize("Save/Load Flag"), &g_Config.m_QmProfileFlag, &Row, LineSize);
		};
		const auto RenderIdentityOptions = [&](CUIRect &View) {
			CSettingsContentLayoutFlow Flow(View);
			CUIRect Row;
			if(Flow.Control("tclient-profile-dummy", LineSize, Row))
			{
				if(DoTClientSettingsButton_CheckBox(&m_Dummy, "tclient-profile-dummy", Localize("Dummy"), m_Dummy, &Row) && !ReadOnly && !Ui()->RenderOnly())
					m_Dummy = 1 - m_Dummy;
			}
			static int s_CustomColorId = 0;
			if(Flow.Control("tclient-profile-custom-colors", LineSize, Row, MarginSmall))
			{
				if(DoTClientSettingsButton_CheckBox(&s_CustomColorId, "tclient-profile-custom-colors", Localize("Custom colors"), *pCurrentUseCustomColor, &Row) && !ReadOnly && !Ui()->RenderOnly())
				{
					*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
					SetNeedSendInfo();
				}
			}
			if(Flow.Control("tclient-profile-overwrite-empty-clan", LineSize, Row, MarginSmall))
			{
				if(DoTClientSettingsButton_CheckBox(&g_Config.m_QmProfileOverwriteClanWithEmpty, "tclient-profile-overwrite-empty-clan", Localize("Overwrite clan even if empty"), g_Config.m_QmProfileOverwriteClanWithEmpty, &Row) && !ReadOnly && !Ui()->RenderOnly())
					g_Config.m_QmProfileOverwriteClanWithEmpty = 1 - g_Config.m_QmProfileOverwriteClanWithEmpty;
			}
		};
		const float LeftHeight = TClientDeclaredRegion("profile-save-load", ColumnWidth, RenderSaveLoad, nullptr);
		const float RightHeight = TClientDeclaredRegion("profile-identity", ColumnWidth, RenderIdentityOptions, nullptr);
		CUIRect Block;
		if(!Flow.Control("tclient-profile-options-area", Inline ? std::max(LeftHeight, RightHeight) : LeftHeight + MarginSmall + RightHeight, Block))
			return;
		CUIRect Left = Block, Right = Block;
		Left.w = Right.w = ColumnWidth;
		Left.h = LeftHeight;
		Right.h = RightHeight;
		if(Inline)
			Right.x += ColumnWidth + MarginSmall;
		else
			Right.y += LeftHeight + MarginSmall;
		TClientDeclaredRegion("profile-save-load", ColumnWidth, RenderSaveLoad, &Left);
		TClientDeclaredRegion("profile-identity", ColumnWidth, RenderIdentityOptions, &Right);
	};

	auto RenderSavedProfiles = [&](CUIRect &MainView) {
		CSettingsContentLayoutFlow Flow(MainView);
		CUIRect SelectorRect;
		if(Flow.Control("tclient-profiles-file", LineSize, SelectorRect))
		{
			static CButtonContainer s_ProfilesFile;
			const float ProfilesButtonWidth = std::clamp(ProfileMetrics.m_LabelWidth, ProfileMetrics.m_ButtonHeight * 4.0f, ProfileMetrics.m_LabelWidth * 1.25f);
			SelectorRect.VSplitLeft(ProfilesButtonWidth, &Button, &SelectorRect);
			if(DoTClientSettingsButton_Menu(&s_ProfilesFile, "tclient-profiles-file", Localize("Profiles file"), 0, &Button) && !ReadOnly && !Ui()->RenderOnly())
			{
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}
		}
		Flow.Space(ProfileMetrics.m_LineSpacing);
		CUIRect List;
		const float ListHeight = std::max(0.0f, ResolveSettingsProfilesListHeight(ProfileMetrics, MainView.w, (int)vProfiles.size()) - ProfileMetrics.m_ButtonHeight - ProfileMetrics.m_LineSpacing);
		if(!Flow.Control("tclient-profiles-list", ListHeight, List))
			return;

		static CListBox s_ProfilesListBox;
		static CListBox s_ProfilesReadOnlyListBox;
		CListBox &ProfilesListBox = ReadOnly ? s_ProfilesReadOnlyListBox : s_ProfilesListBox;
		ProfilesListBox.SetActive(!ReadOnly);
		ProfilesListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		ProfilesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		CPerfTimer ListTimer;
		const float ProfileItemWidth = std::max(ProfileMetrics.m_ListRowHeight * 6.0f, ProfileMetrics.m_LabelWidth + ProfileMetrics.m_ButtonHeight * 2.0f);
		const int ProfilesPerRow = maximum(1, (int)(MainView.w / ProfileItemWidth));
		const float ProfileListRowHeight = std::max(ProfileMetrics.m_ListRowHeight * 3.0f, ProfileMetrics.m_ButtonHeight * 3.0f + ProfileMetrics.m_LineSpacing * 2.0f);
		ProfilesListBox.DoStart(ProfileListRowHeight, vProfiles.size(), ProfilesPerRow, 3, SelectedProfile, &List, true, IGraphics::CORNER_ALL);

		static std::vector<int> s_vProfileItemIds;
		if(s_vProfileItemIds.size() != vProfiles.size())
		{
			s_vProfileItemIds.resize(vProfiles.size());
			for(size_t i = 0; i < s_vProfileItemIds.size(); ++i)
				s_vProfileItemIds[i] = (int)i;
		}

		for(size_t i = 0; i < vProfiles.size(); ++i)
		{
			CListboxItem Item = ProfilesListBox.DoNextItem(&s_vProfileItemIds[i], SelectedProfile >= 0 && (size_t)SelectedProfile == i);
			if(!Item.m_Visible)
				continue;

			RenderProfile(Item.m_Rect, vProfiles[i], false);
		}

		const int NewSelectedProfile = ProfilesListBox.DoEnd();
		if(!ReadOnly)
			SelectedProfile = NewSelectedProfile;
		if(!ReadOnly && ProfilesListBox.WasItemActivated())
			ApplySelectedProfile();
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "profiles=%d", (int)vProfiles.size());
		LogTClientPerfStageEx("tclient_profiles", "list", ETClientSettingsPerfStage::STATIC_LAYER, ListTimer.ElapsedMs(), false, aExtra);
	};

	const bool HasSelectedProfile = pConstSelectedProfile() != nullptr;
	const uint64_t CardRevision = (static_cast<uint64_t>(HasSelectedProfile) << 0) | (static_cast<uint64_t>(s_AllowDelete != 0) << 1) | (static_cast<uint64_t>(vProfiles.size()) << 2);
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-profiles-actions") == 0)
	{
		const float SavedY = Content.y;
		RenderActions(Content);
		return {Content.y - SavedY, CardRevision};
	}
	if(str_comp(pStableId, "deck:tclient-profiles-options") == 0)
	{
		const float SavedY = Content.y;
		RenderOptions(Content);
		return {Content.y - SavedY, CardRevision};
	}
	if(str_comp(pStableId, "deck:tclient-profiles-list") == 0)
	{
		const float SavedY = Content.y;
		RenderSavedProfiles(Content);
		return {Content.y - SavedY, CardRevision};
	}
	return {0.0f, CardRevision};
}
