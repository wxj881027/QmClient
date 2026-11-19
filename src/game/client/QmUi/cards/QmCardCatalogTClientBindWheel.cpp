#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bind_menu.h>
#include <game/client/components/qmclient/bind_menu_preview.h>
#include <game/client/components/qmclient/wheel_presets.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace qm_tclient_cards;

namespace
{
	qm_wheel_presets::SPreset CaptureCurrentIdentity()
	{
		using namespace qm_wheel_presets;
		SPreset Preset;
		Preset.m_Type = EType::IDENTITY;
		for(int Tee = 0; Tee < 2; ++Tee)
		{
			auto &Values = Preset.m_aTees[Tee].m_aValues;
			Values[NAME] = Tee ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
			Values[CLAN] = Tee ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;
			Values[SKIN] = Tee ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
			Values[CUSTOM_COLORS] = std::to_string(Tee ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor);
			Values[BODY_COLOR] = std::to_string(Tee ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody);
			Values[FEET_COLOR] = std::to_string(Tee ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
			Values[COUNTRY] = std::to_string(Tee ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry);
			Values[EYES] = std::to_string(Tee ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes);
		}
		return Preset;
	}

	qm_wheel_presets::SPreset CaptureCurrentAssets()
	{
		using namespace qm_wheel_presets;
		SPreset Preset;
		Preset.m_Type = EType::ASSETS;
		Preset.m_aAssets = {
			g_Config.m_ClAssetsEntities,
			g_Config.m_ClAssetGame,
			g_Config.m_ClAssetEmoticons,
			g_Config.m_ClAssetParticles,
			g_Config.m_ClAssetHud,
			g_Config.m_ClAssetExtras,
			g_Config.m_ClAssetArrow,
			g_Config.m_ClAssetGuiCursor,
			g_Config.m_ClAssetStrongWeak,
		};
		return Preset;
	}

	const char *AssetPresetLabel(const qm_wheel_presets::SPreset &Preset)
	{
		for(const auto &Asset : Preset.m_aAssets)
		{
			if(!Asset.empty() && str_comp(Asset.c_str(), "default") != 0)
				return Asset.c_str();
		}
		return Localize("Assets");
	}

	std::string TruncateLabel(const char *pText, const char *pFallback)
	{
		std::string Label = pText != nullptr && pText[0] != '\0' ? pText : pFallback;
		if(Label.size() >= BINDWHEEL_MAX_NAME)
			Label.resize(BINDWHEEL_MAX_NAME - 1);
		const auto Trim = [&]() {
			while(!Label.empty() && (Label.back() == ' ' || Label.back() == '\t'))
				Label.pop_back();
		};
		Trim();
		if(Label.empty() && pFallback != nullptr)
		{
			Label = pFallback;
			if(Label.size() >= BINDWHEEL_MAX_NAME)
				Label.resize(BINDWHEEL_MAX_NAME - 1);
			Trim();
		}
		return Label;
	}

	std::vector<int> VisibleBinds(CGameClient *pGameClient, qm_bind_menu::EMenu Menu)
	{
		std::vector<int> vIndices;
		const auto &vBinds = pGameClient->m_BindWheel.m_vBinds;
		for(int Index = 0; Index < (int)vBinds.size(); ++Index)
		{
			if(pGameClient->m_BindWheel.MenuForBind(Index) == Menu)
				vIndices.push_back(Index);
		}
		return vIndices;
	}

	void AddPreset(CGameClient *pGameClient, const qm_wheel_presets::SPreset &Preset, const std::string &Label, const char **ppStatus)
	{
		std::string Command;
		if(Label.empty() || !qm_wheel_presets::BuildCommand(Preset, Command))
		{
			*ppStatus = Localize("Could not add this entry.");
			return;
		}
		if(!pGameClient->m_BindWheel.SetBind(-1, Label.c_str(), Command.c_str()))
			*ppStatus = Localize("The bind menu is full.");
		else
			*ppStatus = Localize("Added to this bind menu.");
	}
} // namespace

qm_card_catalog::STClientCardResult CMenus::RunTClientBindWheelCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());

	const auto &Metrics = Ctx.m_Metrics;
	const float UiScale = SettingsPageUiScale(MainView.w);
	IUiContext InputCtx = SettingsUiContext("settings_tclient_bind_menu_inputs", UiScale);
	if(ReadOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}

	static int s_Menu = (int)qm_bind_menu::EMenu::IDENTITY;
	static int s_SelectedSource = -1;
	static int s_ChatTeam = 0;
	static const char *s_pStatus = "";
	static CLineInputBuffered<BINDWHEEL_MAX_NAME> s_CustomName;
	static CLineInputBuffered<BINDWHEEL_MAX_CMD> s_CustomCommand;
	static CLineInputBuffered<BINDWHEEL_MAX_NAME> s_ChatTitle;
	static CLineInputBuffered<256> s_ChatText;
	static CLineInputBuffered<BINDWHEEL_MAX_NAME> s_AssetTitle;
	static int s_ChatFieldsSource = -2;
	static int s_AssetFieldsSource = -2;
	static CButtonContainer s_aMenuButtons[qm_bind_menu::NUM_MENUS];
	static CButtonContainer s_aLayoutButtons[2];
	static CButtonContainer s_ReaderButton, s_ClearButton;
	static CButtonContainer s_AddButton, s_DeleteButton;
	static CListBox s_List;
	static std::array<int, BINDWHEEL_MAX_BINDS> s_aItemIds{};

	if(s_Menu < 0 || s_Menu >= qm_bind_menu::NUM_MENUS)
		s_Menu = 0;
	const auto Menu = (qm_bind_menu::EMenu)s_Menu;
	const auto vVisible = VisibleBinds(GameClient(), Menu);
	const bool Editable = Pass == qm_card_catalog::ETClientCardPass::RENDER && !ReadOnly && !Ui()->RenderOnly();

	if(Editable && Menu == qm_bind_menu::EMenu::CHAT && s_SelectedSource != s_ChatFieldsSource)
	{
		s_ChatFieldsSource = s_SelectedSource;
		const qm_wheel_presets::SPreset *pPreset = s_SelectedSource >= 0 ?
								   GameClient()->m_BindWheel.PresetForBind(s_SelectedSource) :
								   nullptr;
		if(pPreset != nullptr && pPreset->m_Type == qm_wheel_presets::EType::CHAT)
		{
			const auto &Bind = GameClient()->m_BindWheel.m_vBinds[s_SelectedSource];
			s_ChatTitle.Set(Bind.m_aName);
			s_ChatText.Set(pPreset->m_Chat.c_str());
			s_ChatTeam = pPreset->m_TeamChat ? 1 : 0;
		}
		else
		{
			s_ChatTitle.Clear();
			s_ChatText.Clear();
			s_ChatTeam = 0;
		}
	}
	if(Editable && Menu == qm_bind_menu::EMenu::ASSETS && s_SelectedSource != s_AssetFieldsSource)
	{
		s_AssetFieldsSource = s_SelectedSource;
		const qm_wheel_presets::SPreset *pPreset = s_SelectedSource >= 0 ?
								   GameClient()->m_BindWheel.PresetForBind(s_SelectedSource) :
								   nullptr;
		if(pPreset != nullptr && pPreset->m_Type == qm_wheel_presets::EType::ASSETS)
			s_AssetTitle.Set(GameClient()->m_BindWheel.m_vBinds[s_SelectedSource].m_aName);
		else
			s_AssetTitle.Clear();
	}

	auto RenderEditor = [&](CUIRect &View) {
		CSettingsContentLayoutFlow Flow(View);
		const auto TextRow = [&](const char *pId, const char *pText, const char *pTextId, float Size = -1.0f, float Spacing = -1.0f) {
			CUIRect Row;
			const float ResolvedSize = Size > 0.0f ? Size : Metrics.m_BodySize;
			const float ResolvedSpacing = Spacing >= 0.0f ? Spacing : ui_token::settings::RowGap(Metrics.m_LineSpacing);
			if(!Flow.Control(pId, ResolvedSize * 1.5f, Row, ResolvedSpacing))
				return;
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, pTextId, &Row, pText, ResolvedSize, TEXTALIGN_ML);
		};
		const auto InputRow = [&](const char *pId, const char *pLabel, CLineInput *pInput, float Spacing) {
			CUIRect Row;
			if(!Flow.Control(pId, Metrics.m_ButtonHeight, Row, Spacing))
				return;
			CUIRect Label, Control;
			SplitSettingsControlRow(Row, Metrics, &Label, &Control);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, pId, &Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML);
			ui_widget::InputField(InputCtx, pInput, Control, pLabel, Metrics.m_BodySize);
		};

		CUIRect Row;
		if(Flow.Control("bind-menu-kind", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		{
			CUIRect Label, Control;
			SplitSettingsControlRow(Row, Metrics, &Label, &Control);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "bind-menu-kind-label", &Label, Localize("Menu type"), Metrics.m_BodySize, TEXTALIGN_ML);
			const char *apLabels[] = {Localize("Identity"), Localize("Chat"), Localize("Assets"), Localize("Custom")};
			const int NewMenu = DoSegmentedChoice(s_aMenuButtons, apLabels, qm_bind_menu::NUM_MENUS, s_Menu, Control, ui_token::radius::BASE, nullptr, Metrics.m_BodySize);
			if(Editable && NewMenu != s_Menu)
			{
				s_Menu = NewMenu;
				s_SelectedSource = -1;
				s_ChatFieldsSource = -2;
				s_AssetFieldsSource = -2;
				s_pStatus = "";
			}
		}

		if(Flow.Control("bind-menu-layout", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		{
			CUIRect Label, Control;
			SplitSettingsControlRow(Row, Metrics, &Label, &Control);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, "bind-menu-layout-label", &Label, Localize("Layout"), Metrics.m_BodySize, TEXTALIGN_ML);
			const char *apLabels[] = {Localize("Wheel"), Localize("Panel")};
			const int NewLayout = DoSegmentedChoice(s_aLayoutButtons, apLabels, 2, g_Config.m_QmBindWheelLayout, Control, ui_token::radius::BASE, nullptr, Metrics.m_BodySize);
			if(Editable && NewLayout != g_Config.m_QmBindWheelLayout)
				g_Config.m_QmBindWheelLayout = NewLayout;
		}

		if(Flow.Control("bind-menu-key", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
			DoLine_KeyReader(Row, s_ReaderButton, s_ClearButton, Localize("Trigger key"), qm_bind_menu::TriggerCommand(Menu), Metrics.m_BodySize);

		TextRow("bind-menu-options-title", Localize("Options"), "bind-menu-options-title-label", Metrics.m_BodySize, ui_token::settings::SectionGap(Metrics.m_LineSpacing));

		const int VisibleRows = vVisible.empty() ? 1 : ui_token::list::VisibleRows((int)vVisible.size(), 1, 1, 5);
		const float RowHeight = Metrics.m_ListRowHeight *
					(Menu == qm_bind_menu::EMenu::ASSETS ? ui_token::bind_menu::ASSET_PANEL_ROW_HEIGHT_ROWS : 2.8f);
		const float ListPadding = ui_token::list::ContainerPadding(RowHeight);
		if(Flow.Control("bind-menu-options", ui_token::list::PaddedRowsHeight(VisibleRows, RowHeight, ui_token::list::ROW_SPACING, ListPadding), Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		{
			int SelectedVisible = -1;
			for(int Index = 0; Index < (int)vVisible.size(); ++Index)
				if(vVisible[Index] == s_SelectedSource)
					SelectedVisible = Index;
			s_List.SetActive(Editable);
			s_List.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			s_List.SetCapsuleRows(Metrics.m_BodySize);
			s_List.SetContainerPadding(ListPadding);
			s_List.SetDeferredItemFeedback(true);
			s_List.SetVisibleRowRange(VisibleRows, VisibleRows);
			s_List.DoAutoSpacing(ui_token::list::ROW_SPACING);
			s_List.DoStart(RowHeight, (int)vVisible.size(), 1, 3, SelectedVisible, &Row);
			for(int Index = 0; Index < (int)vVisible.size(); ++Index)
			{
				const CListboxItem Item = s_List.DoNextItem(&s_aItemIds[Index], Index == SelectedVisible);
				if(!Item.m_Visible)
					continue;
				const auto &Bind = GameClient()->m_BindWheel.m_vBinds[vVisible[Index]];
				const auto *pPreset = GameClient()->m_BindWheel.PresetForBind(vVisible[Index]);
				const qm_bind_menu_preview::SOption Option{Bind.m_aName, Bind.m_aCommand, pPreset, pPreset == nullptr};
				CUIRect OptionRect = Item.m_ContentRect;
				const bool Expanded = Ui()->HotItem() == &s_aItemIds[Index];
				s_List.DrawItemFeedback(Index, OptionRect, ui_token::list::ContainerRadius(OptionRect.w, OptionRect.h));
				qm_bind_menu_preview::RenderOption(Ui(), GameClient(), OptionRect, Menu, Option, UiScale, 1.0f, Expanded);
				if(Expanded)
					GameClient()->m_Tooltips.DoToolTipForRect(&s_aItemIds[Index], &OptionRect,
						qm_bind_menu_preview::BuildHoverText(Menu, Option, pPreset).c_str());
			}
			const int NewSelected = s_List.DoEnd();
			if(Editable)
			{
				s_SelectedSource = NewSelected >= 0 && NewSelected < (int)vVisible.size() ? vVisible[NewSelected] : -1;
			}
			if(vVisible.empty())
				Ui()->DoLabel(&Row, Localize("No entries yet"), Metrics.m_BodySize, TEXTALIGN_MC);
		}

		if(Menu == qm_bind_menu::EMenu::IDENTITY)
		{
			if(Flow.Control("bind-menu-add-identity", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
			{
				if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton,
					   "bind-menu-add-identity", Localize("Capture current appearance"), 0, &Row, Metrics) &&
					Editable)
				{
					const auto Preset = CaptureCurrentIdentity();
					const char *pLabel = g_Config.m_PlayerName[0] ? g_Config.m_PlayerName : Localize("Identity");
					AddPreset(GameClient(), Preset, TruncateLabel(pLabel, Localize("Identity")), &s_pStatus);
				}
			}
		}
		else if(Menu == qm_bind_menu::EMenu::CHAT)
		{
			const auto *pSelectedPreset = s_SelectedSource >= 0 && s_SelectedSource < (int)GameClient()->m_BindWheel.m_vBinds.size() ?
							      GameClient()->m_BindWheel.PresetForBind(s_SelectedSource) :
							      nullptr;
			const bool EditingChat = pSelectedPreset != nullptr && pSelectedPreset->m_Type == qm_wheel_presets::EType::CHAT;
			s_ChatTitle.SetEmptyText(Localize("Title"));
			s_ChatText.SetEmptyText(Localize("Chat phrase"));
			InputRow("bind-menu-chat-title", Localize("Title"), &s_ChatTitle, ui_token::settings::RowGap(Metrics.m_LineSpacing));
			InputRow("bind-menu-chat-input", Localize("Phrase"), &s_ChatText, ui_token::settings::RowGap(Metrics.m_LineSpacing));
			if(Flow.Control("bind-menu-chat-add", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
			{
				CUIRect Team, Add;
				Row.VSplitMid(&Team, &Add, Metrics.m_LineSpacing);
				if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_ChatTeam,
					   "bind-menu-chat-team", Localize("Team chat"), s_ChatTeam, &Team) &&
					Editable)
					s_ChatTeam ^= 1;
				if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton,
					   "bind-menu-chat-add", EditingChat ? Localize("Save phrase") : Localize("Add phrase"), 0, &Add, Metrics) &&
					Editable)
				{
					qm_wheel_presets::SPreset Preset;
					Preset.m_Type = qm_wheel_presets::EType::CHAT;
					Preset.m_Chat = s_ChatText.GetString();
					Preset.m_TeamChat = s_ChatTeam != 0;
					if(Preset.m_Chat.empty())
						s_pStatus = Localize("Enter a phrase.");
					else
					{
						std::string Command;
						if(!qm_wheel_presets::BuildCommand(Preset, Command))
							s_pStatus = Localize("Enter a phrase.");
						else
						{
							const std::string Label = TruncateLabel(s_ChatTitle.GetString(), Preset.m_Chat.c_str());
							if(!GameClient()->m_BindWheel.SetBind(EditingChat ? s_SelectedSource : -1, Label.c_str(), Command.c_str()))
								s_pStatus = Localize("The bind menu is full.");
							else if(EditingChat)
							{
								s_ChatFieldsSource = s_SelectedSource;
								s_pStatus = Localize("Updated this phrase.");
							}
							else
							{
								s_SelectedSource = -1;
								s_ChatFieldsSource = -2;
								s_ChatTitle.Clear();
								s_ChatText.Clear();
								s_ChatTeam = 0;
								s_pStatus = Localize("Added to this bind menu.");
							}
						}
					}
				}
			}
		}
		else if(Menu == qm_bind_menu::EMenu::ASSETS)
		{
			const auto *pSelectedPreset = s_SelectedSource >= 0 && s_SelectedSource < (int)GameClient()->m_BindWheel.m_vBinds.size() ?
							      GameClient()->m_BindWheel.PresetForBind(s_SelectedSource) :
							      nullptr;
			const bool EditingAssets = pSelectedPreset != nullptr && pSelectedPreset->m_Type == qm_wheel_presets::EType::ASSETS;
			s_AssetTitle.SetEmptyText(Localize("Title"));
			InputRow("bind-menu-asset-title", Localize("Title"), &s_AssetTitle, ui_token::settings::RowGap(Metrics.m_LineSpacing));
			if(Flow.Control("bind-menu-add-assets", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
			{
				if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton,
					   "bind-menu-add-assets", EditingAssets ? Localize("Save configuration") : Localize("Use current settings"), 0, &Row, Metrics) &&
					Editable)
				{
					const auto Preset = CaptureCurrentAssets();
					std::string Command;
					if(!qm_wheel_presets::BuildCommand(Preset, Command))
						s_pStatus = Localize("Could not add this entry.");
					else
					{
						const std::string Label = TruncateLabel(s_AssetTitle.GetString(), AssetPresetLabel(Preset));
						if(!GameClient()->m_BindWheel.SetBind(EditingAssets ? s_SelectedSource : -1, Label.c_str(), Command.c_str()))
							s_pStatus = Localize("The bind menu is full.");
						else if(EditingAssets)
						{
							s_AssetFieldsSource = s_SelectedSource;
							s_pStatus = Localize("Updated this configuration.");
						}
						else
						{
							s_SelectedSource = -1;
							s_AssetFieldsSource = -2;
							s_AssetTitle.Clear();
							s_pStatus = Localize("Added to this bind menu.");
						}
					}
				}
			}
		}
		else
		{
			s_CustomName.SetEmptyText(Localize("Name"));
			s_CustomCommand.SetEmptyText(Localize("Console command"));
			InputRow("bind-menu-custom-name", Localize("Name"), &s_CustomName, ui_token::settings::RowGap(Metrics.m_LineSpacing));
			InputRow("bind-menu-custom-command", Localize("Command"), &s_CustomCommand, ui_token::settings::RowGap(Metrics.m_LineSpacing));
			if(Flow.Control("bind-menu-custom-add", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
			{
				if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton,
					   "bind-menu-custom-add", Localize("Add command"), 0, &Row, Metrics) &&
					Editable)
				{
					const std::string Label = TruncateLabel(s_CustomName.GetString(), Localize("Custom"));
					if(s_CustomCommand.GetString()[0] == '\0' ||
						!GameClient()->m_BindWheel.SetBind(-1, Label.c_str(), s_CustomCommand.GetString()))
						s_pStatus = Localize("Enter a command and keep the menu under 64 entries.");
					else
						s_pStatus = Localize("Added to this bind menu.");
				}
			}
		}

		if(Flow.Control("bind-menu-delete", Metrics.m_ButtonHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		{
			if(DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_DeleteButton,
				   "bind-menu-delete", Localize("Delete selected"), 0, &Row, Metrics) &&
				Editable && s_SelectedSource >= 0)
			{
				GameClient()->m_BindWheel.RemoveBind(s_SelectedSource);
				s_SelectedSource = -1;
				s_ChatFieldsSource = -2;
				s_AssetFieldsSource = -2;
				s_ChatTitle.Clear();
				s_ChatText.Clear();
				s_ChatTeam = 0;
				s_AssetTitle.Clear();
				s_pStatus = Localize("Removed from this bind menu.");
			}
		}

		if(s_pStatus[0] != '\0')
			TextRow("bind-menu-status", s_pStatus, "bind-menu-status-label", Metrics.m_SmallSize, 0.0f);
	};

	const uint64_t CardRevision = ((uint64_t)GameClient()->m_BindWheel.m_vBinds.size() << 8) ^ (uint64_t)s_Menu ^ ((uint64_t)(s_SelectedSource + 1) << 16) ^ (uint64_t)g_Config.m_QmBindWheelLayout;
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
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Preview;
		const float Height = std::clamp(Content.w * ui_token::bind_menu::PREVIEW_HEIGHT_RATIO,
			ui_token::bind_menu::PREVIEW_MIN_HEIGHT * UiScale, ui_token::bind_menu::PREVIEW_MAX_HEIGHT * UiScale);
		if(!Flow.Control("bind-menu-preview", Height, Preview))
			return {0.0f, CardRevision};
		if(ReadOnly)
			return {Height, CardRevision};
		const auto LayoutKind = g_Config.m_QmBindWheelLayout == (int)qm_bind_menu::ELayout::PANEL ? qm_bind_menu::ELayout::PANEL : qm_bind_menu::ELayout::WHEEL;
		std::vector<qm_bind_menu_preview::SOption> vOptions;
		vOptions.reserve(vVisible.size());
		for(const int SourceIndex : vVisible)
		{
			const auto &Bind = GameClient()->m_BindWheel.m_vBinds[SourceIndex];
			const auto *pPreset = GameClient()->m_BindWheel.PresetForBind(SourceIndex);
			vOptions.push_back({Bind.m_aName, Bind.m_aCommand, pPreset, pPreset == nullptr});
		}
		const auto Layout = qm_bind_menu_preview::BuildLayout(Preview, LayoutKind, (int)vOptions.size(), 0, UiScale, false, Menu);
		const vec2 PreviewMouse = Ui()->MousePos();
		const int PreviewHovered = Preview.Inside(PreviewMouse) ?
						   qm_bind_menu_preview::HitTest(Layout, PreviewMouse, (int)vOptions.size(), 0) :
						   -1;
		qm_bind_menu_preview::Render(Ui(), GameClient(), Layout, Menu, vOptions.data(), (int)vOptions.size(), PreviewHovered, 0, UiScale, 1.0f);
		return {Height, CardRevision};
	}
	return {0.0f, CardRevision};
}
