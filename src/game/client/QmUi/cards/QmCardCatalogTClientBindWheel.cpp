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
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
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

qm_card_catalog::STClientCardResult CMenus::RunTClientBindWheelCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	const float UiScale = SettingsPageUiScale(MainView.w);
	const float SmallSize = ResolveSettingsContentMetrics(MainView.w).m_SmallSize;
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	IUiContext TClientBindWheelTextInputCtx = SettingsUiContext("settings_tclient_bindwheel_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientBindWheelTextInputCtx.m_pAnim = nullptr;
		TClientBindWheelTextInputCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_BindWheelSettingsScrollRegion;
	static char s_aBindName[BINDWHEEL_MAX_NAME];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD];
	static int s_SelectedBindIndex = -1;
	const float PreviewContentHeight = 280.0f;
	const auto RenderEditor = [this, &TClientBindWheelTextInputCtx, ReadOnly, SmallSize](CUIRect &Content) {
		CPerfTimer EditorTimer;
		CUIRect LeftView = Content, Label, Button;
		CSettingsContentLayoutFlow Flow(LeftView);
		static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

		if(Flow.Control("tclient-bindwheel-name", LineSize, Button, MarginSmall))
		{
			SplitSettingsControlRow(Button, CurrentSettingsContentMetrics(), &Label, &Button);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-name-label", &Label, Localize("Name:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_NameInput;
			s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
			s_NameInput.SetEmptyText(Localize("Name"));
			ui_widget::InputField(TClientBindWheelTextInputCtx, &s_NameInput, Button, Localize("Name"), EditBoxFontSize);
		}

		if(Flow.Control("tclient-bindwheel-command", LineSize, Button, MarginSmall))
		{
			SplitSettingsControlRow(Button, CurrentSettingsContentMetrics(), &Label, &Button);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-command-label", &Label, Localize("Command:"), FontSize, TEXTALIGN_ML);
			static CLineInput s_BindInput;
			s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
			s_BindInput.SetEmptyText(Localize("Command"));
			ui_widget::InputField(TClientBindWheelTextInputCtx, &s_BindInput, Button, Localize("Command"), EditBoxFontSize);
		}

		if(Flow.Control("tclient-bindwheel-override", LineSize, Button, MarginSmall))
		{
			if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_OverrideButton, "tclient-bindwheel-override-selected", Localize("Override Selected"), 0, &Button) && !ReadOnly && !Ui()->RenderOnly() && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
			{
				CBindWheel::CBind TempBind;
				str_copy(TempBind.m_aName, str_length(s_aBindName) == 0 ? "*" : s_aBindName);
				str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
				str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
			}
		}

		if(Flow.Control("tclient-bindwheel-add-remove", LineSize, Button, MarginSmall))
		{
			CUIRect ButtonAdd, ButtonRemove;
			static ui_widget::SSettingsActionGroupState s_BindActionWidths;
			const char *apActionLabels[] = {Localize("Remove Bind"), Localize("Add Bind")};
			const auto Actions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_BindActionWidths, Button, apActionLabels, 2, CurrentSettingsContentMetrics());
			ButtonRemove = Actions.m_aButtons[0];
			ButtonAdd = Actions.m_aButtons[1];
			if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton, "tclient-bindwheel-add-bind", Localize("Add Bind"), 0, &ButtonAdd) && !ReadOnly && !Ui()->RenderOnly())
			{
				CBindWheel::CBind TempBind;
				str_copy(TempBind.m_aName, str_length(s_aBindName) == 0 ? "*" : s_aBindName);
				GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
				s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
			}
			if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_RemoveButton, "tclient-bindwheel-remove-bind", Localize("Remove Bind"), 0, &ButtonRemove) && !ReadOnly && !Ui()->RenderOnly() && s_SelectedBindIndex >= 0)
			{
				GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
				s_SelectedBindIndex = -1;
			}
		}

		if(Flow.Control("tclient-bindwheel-help-console", LineSize, Label, MarginSmall))
		{
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-footer-console", &Label, Localize("Commands run in the console"), FontSize, TEXTALIGN_ML);
		}

		if(Flow.Control("tclient-bindwheel-help-mouse", SmallSize, Label, MarginSmall))
		{
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "tclient-bindwheel-footer-mouse", &Label, Localize("L select  R swap  M select only"), SmallSize, TEXTALIGN_ML);
		}

		CUIRect CheckBoxRect;
		if(Flow.Control("tclient-bindwheel-reset-mouse", LineSize, CheckBoxRect))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &g_Config.m_QmResetBindWheelMouse, "tclient-bindwheel-reset-mouse", Localize("Reset position of mouse when opening bindwheel"), g_Config.m_QmResetBindWheelMouse, &CheckBoxRect) && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmResetBindWheelMouse ^= 1;
		}
		if(Flow.Control("tclient-bindwheel-key", LineSize, Label))
		{
			static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
			DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, Localize("Bind Wheel Key"), "+bindwheel");
		}
		Content = LeftView;
		LogTClientPerfStage("tclient_bindwheel_editor", EditorTimer.ElapsedMs(), false);
	};
	const auto MeasurePreview = [PreviewContentHeight](float) { return PreviewContentHeight; };
	const auto RenderPreview = [this, ReadOnly](CUIRect RightView) {
		if(ReadOnly)
			return;
		CPerfTimer WheelTimer;
		const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
		const vec2 Center = RightView.Center();
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
		Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
		Graphics()->QuadsEnd();

		int HoveringIndex = -1;
		const int SegmentCount = GameClient()->m_BindWheel.m_vBinds.size();
		const float MouseDist = distance(Center, Ui()->MousePos());
		if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
		{
			float HoveringAngle = angle(Ui()->MousePos() - Center) + pi / SegmentCount;
			if(HoveringAngle < 0.0f)
				HoveringAngle += 2.0f * pi;
			HoveringIndex = std::clamp((int)(HoveringAngle / (2.0f * pi) * SegmentCount), 0, SegmentCount - 1);
			if(!ReadOnly && Ui()->MouseButtonClicked(0))
			{
				s_SelectedBindIndex = HoveringIndex;
				str_copy(s_aBindName, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName);
				str_copy(s_aBindCommand, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand);
			}
			else if(!ReadOnly && Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < SegmentCount && HoveringIndex != s_SelectedBindIndex)
			{
				std::swap(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex], GameClient()->m_BindWheel.m_vBinds[HoveringIndex]);
			}
			else if(!ReadOnly && Ui()->MouseButtonClicked(2))
				s_SelectedBindIndex = HoveringIndex;
		}
		else if(!ReadOnly && MouseDist < Radius && Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = -1;
			str_copy(s_aBindName, "");
			str_copy(s_aBindCommand, "");
		}

		const float Theta = pi * 2.0f / std::max<float>(1.0f, SegmentCount);
		for(int Index = 0; Index < SegmentCount; ++Index)
		{
			TextRender()->TextColor(ui_token::color::NEUTRAL_WHITE);
			float SegmentFontSize = FontSize * 1.1f;
			if(Index == s_SelectedBindIndex)
			{
				SegmentFontSize = FontSize * 1.7f;
				TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
			}
			else if(Index == HoveringIndex)
				SegmentFontSize = FontSize * 1.35f;
			const vec2 Pos = direction(Theta * Index) * (Radius * 0.75f) + Center;
			const CUIRect Rect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
			Ui()->DoLabel(&Rect, GameClient()->m_BindWheel.m_vBinds[Index].m_aName, SegmentFontSize, TEXTALIGN_MC);
		}
		TextRender()->TextColor(ui_token::color::NEUTRAL_WHITE);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "count=%d", SegmentCount);
		LogTClientPerfStage("tclient_bindwheel_wheel", WheelTimer.ElapsedMs(), false, aExtra);
	};
	const uint64_t CardRevision = (uint64_t)GameClient()->m_BindWheel.m_vBinds.size();
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-bind-wheel-editor") == 0)
	{
		const float SavedY = Content.y;
		RenderEditor(Content);
		return {Content.y - SavedY, CardRevision};
	}
	if(str_comp(pStableId, "deck:tclient-bind-wheel-preview") == 0)
	{
		const float Height = MeasurePreview(Content.w);
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Block;
		if(Flow.Control("tclient-bindwheel-preview", Height, Block))
			RenderPreview(Block);
		return {Height, CardRevision};
	}
	return {0.0f, CardRevision};
}
