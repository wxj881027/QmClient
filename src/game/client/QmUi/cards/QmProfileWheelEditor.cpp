#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/wheel_presets.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/skinprofiles.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

using namespace qm_wheel_presets;

namespace
{
	SPreset CurrentPreset(EType Type)
	{
		SPreset Preset;
		Preset.m_Type = Type;
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
		Preset.m_aAssets = {g_Config.m_ClAssetsEntities, g_Config.m_ClAssetGame, g_Config.m_ClAssetEmoticons, g_Config.m_ClAssetParticles, g_Config.m_ClAssetHud, g_Config.m_ClAssetExtras, g_Config.m_ClAssetArrow, g_Config.m_ClAssetGuiCursor, g_Config.m_ClAssetStrongWeak};
		return Preset;
	}
}

struct SQmProfileWheelEditorState
{
	struct SIdentityEditor
	{
		CLineInputBuffered<16> m_Name;
		CLineInputBuffered<12> m_Clan;
		CLineInputBuffered<24> m_Skin;
		CLineInputNumber m_Country;
		std::array<int, NUM_IDENTITY_FIELDS> m_aEnabled{};
		int m_CustomColors = 0;
		unsigned int m_aColors[2]{};
		int m_Eyes = 0;
		CUi::SDropDownState m_EyesDropdown;
		CScrollRegion m_EyesScroll;

		void Load(const SIdentity &Identity)
		{
			for(int Field = 0; Field < NUM_IDENTITY_FIELDS; ++Field)
				m_aEnabled[Field] = Identity.m_aValues[Field].has_value();
			m_Name.Set(Identity.m_aValues[NAME].value_or("").c_str());
			m_Clan.Set(Identity.m_aValues[CLAN].value_or("").c_str());
			m_Skin.Set(Identity.m_aValues[SKIN].value_or("default").c_str());
			m_CustomColors = str_toint(Identity.m_aValues[CUSTOM_COLORS].value_or("0").c_str());
			m_aColors[0] = str_toint(Identity.m_aValues[BODY_COLOR].value_or("0").c_str());
			m_aColors[1] = str_toint(Identity.m_aValues[FEET_COLOR].value_or("0").c_str());
			m_Country.Set(Identity.m_aValues[COUNTRY].value_or("-1").c_str());
			m_Eyes = str_toint(Identity.m_aValues[EYES].value_or("0").c_str());
		}

		SIdentity Values(bool NameOnly) const
		{
			SIdentity Result;
			const std::string aValues[] = {m_Name.GetString(), m_Clan.GetString(), m_Skin.GetString(), std::to_string(m_CustomColors), std::to_string(m_aColors[0]), std::to_string(m_aColors[1]), m_Country.GetString(), std::to_string(m_Eyes)};
			for(int Field = 0; Field < (NameOnly ? 1 : NUM_IDENTITY_FIELDS); ++Field)
				if(NameOnly || m_aEnabled[Field])
					Result.m_aValues[Field] = aValues[Field];
			return Result;
		}
	};

	int m_Type = 0;
	int m_Target = 2;
	int m_TeamChat = 0;
	int m_Selected = -1;
	std::optional<CBindWheel::CBind> m_Original;
	CLineInputBuffered<BINDWHEEL_MAX_NAME> m_Label;
	CLineInputBuffered<256> m_Chat;
	SIdentityEditor m_aTees[2];
	std::array<CLineInputBuffered<50>, NUM_ASSETS> m_aAssets;
	CUi::SDropDownState m_TypeDropdown, m_TargetDropdown;
	CScrollRegion m_TypeScroll, m_TargetScroll;
	CListBox m_List, m_ReadOnlyList;
	std::array<int, BINDWHEEL_MAX_BINDS> m_aItemIds{};
	CButtonContainer m_SaveButton, m_NewButton, m_DeleteButton, m_CaptureButton, m_DefaultsButton, m_WheelKey, m_ClearWheelKey;
	const char *m_pStatus = "";

	SQmProfileWheelEditorState()
	{
		Load(CurrentPreset(EType::NAME));
	}

	void Load(const SPreset &Preset)
	{
		m_Type = static_cast<int>(Preset.m_Type);
		for(int Tee = 0; Tee < 2; ++Tee)
			m_aTees[Tee].Load(Preset.m_aTees[Tee]);
		const auto HasValues = [](const SIdentity &Tee) { return std::any_of(Tee.m_aValues.begin(), Tee.m_aValues.end(), [](const auto &Value) { return Value.has_value(); }); };
		const bool Main = HasValues(Preset.m_aTees[0]);
		const bool Dummy = HasValues(Preset.m_aTees[1]);
		m_Target = Main && Dummy ? 2 : Dummy ? 1 :
						       0;
		m_Chat.Set(Preset.m_Chat.c_str());
		m_TeamChat = Preset.m_TeamChat;
		for(int Index = 0; Index < NUM_ASSETS; ++Index)
			m_aAssets[Index].Set(Preset.m_aAssets[Index].c_str());
		m_pStatus = "";
	}

	SPreset Values() const
	{
		SPreset Preset;
		Preset.m_Type = static_cast<EType>(m_Type);
		for(int Tee = 0; Tee < 2; ++Tee)
			if(m_Target == 2 || m_Target == Tee)
				Preset.m_aTees[Tee] = m_aTees[Tee].Values(Preset.m_Type == EType::NAME);
		Preset.m_Chat = m_Chat.GetString();
		Preset.m_TeamChat = m_TeamChat != 0;
		for(int Index = 0; Index < NUM_ASSETS; ++Index)
			Preset.m_aAssets[Index] = m_aAssets[Index].GetString();
		return Preset;
	}
};

uint64_t CMenus::ProfileWheelEditorRevision() const
{
	const auto *pState = m_pProfileWheelEditor.get();
	return ((static_cast<uint64_t>(m_ProfilePresetsTab) << 32) |
		       (static_cast<uint64_t>(GameClient()->m_BindWheel.m_vBinds.size()) << 33) |
		       (pState ? (static_cast<uint64_t>(pState->m_Type) << 40) | (static_cast<uint64_t>(pState->m_Target) << 43) | (static_cast<uint64_t>(pState->m_Selected + 1) << 46) : 0)) ^
	       (pState ? reinterpret_cast<uintptr_t>(pState->m_pStatus) : 0);
}

void CMenus::EditProfileInWheel(const CProfile &Profile, bool Dummy)
{
	if(!m_pProfileWheelEditor)
		m_pProfileWheelEditor = std::make_shared<SQmProfileWheelEditorState>();
	SPreset Preset;
	Preset.m_Type = EType::IDENTITY;
	auto &Values = Preset.m_aTees[Dummy ? 1 : 0].m_aValues;
	if(g_Config.m_QmProfileName && Profile.m_Name[0])
		Values[NAME] = Profile.m_Name;
	if(g_Config.m_QmProfileClan && (Profile.m_Clan[0] || g_Config.m_QmProfileOverwriteClanWithEmpty))
		Values[CLAN] = Profile.m_Clan;
	if(g_Config.m_QmProfileSkin && Profile.m_SkinName[0])
		Values[SKIN] = Profile.m_SkinName;
	if(g_Config.m_QmProfileColors && Profile.m_BodyColor >= 0 && Profile.m_FeetColor >= 0)
	{
		Values[CUSTOM_COLORS] = "1";
		Values[BODY_COLOR] = std::to_string(Profile.m_BodyColor);
		Values[FEET_COLOR] = std::to_string(Profile.m_FeetColor);
	}
	if(g_Config.m_QmProfileFlag && Profile.m_CountryFlag != -2)
		Values[COUNTRY] = std::to_string(Profile.m_CountryFlag);
	if(g_Config.m_QmProfileEmote && Profile.m_Emote != -1)
		Values[EYES] = std::to_string(Profile.m_Emote);
	auto &State = *m_pProfileWheelEditor;
	State.Load(Preset);
	State.m_Target = Dummy ? 1 : 0;
	State.m_Label.Set(Profile.m_Name[0] ? Profile.m_Name : Localize("Profiles"));
	State.m_Selected = -1;
	State.m_Original.reset();
	m_ProfilePresetsTab = 1;
}

void CMenus::RenderProfileWheelEditor(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool ReadOnly)
{
	if(!m_pProfileWheelEditor)
		m_pProfileWheelEditor = std::make_shared<SQmProfileWheelEditorState>();
	auto &State = *m_pProfileWheelEditor;
	auto &Wheel = GameClient()->m_BindWheel;
	const auto &Metrics = Ctx.m_Metrics;
	CSettingsContentLayoutFlow Flow(Content);
	const bool Editable = Flow.Input() && !ReadOnly && !Ui()->RenderOnly();
	IUiContext InputCtx = SettingsUiContext("profile-wheel-inputs", Metrics.m_UiScale);
	if(ReadOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}
	if(Editable && State.m_Original && (State.m_Selected < 0 || State.m_Selected >= static_cast<int>(Wheel.m_vBinds.size()) || !(Wheel.m_vBinds[State.m_Selected] == *State.m_Original)))
	{
		State.m_Selected = -1;
		State.m_Original.reset();
		State.m_pStatus = Localizable("The wheel changed. Your draft can be added as a new preset.");
	}
	const auto Text = [&](const char *pId, const char *pText) {
		Flow.Text(pId, pText, 0, Metrics.m_BodySize, Metrics.m_LineSpacing, [this](const char *pValue, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pValue, -1, Width).m_H + Size * 0.25f; }, [this, &Metrics](const CUIRect &Row, const char *pValue) { Ui()->DoLabel(&Row, pValue, Metrics.m_BodySize, TEXTALIGN_ML, {.m_MaxWidth = Row.w, .m_EnableWidthCheck = false}); });
	};
	const auto Input = [&](const char *pId, const char *pLabel, CLineInput &Value, int *pEnabled = nullptr) {
		CUIRect Row, Label, Control;
		if(!Flow.Control(pId, Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
			return;
		SplitSettingsControlRow(Row, Metrics, &Label, &Control);
		if(pEnabled)
		{
			if(DoTClientSettingsButton_CheckBox(pEnabled, pId, pLabel, *pEnabled, &Label) && Editable)
				*pEnabled ^= 1;
		}
		else
			Ui()->DoLabel(&Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML);
		ui_widget::InputField(InputCtx, &Value, Control, pLabel, Metrics.m_BodySize);
	};
	const auto Choice = [&](const char *pId, const char *pLabel, int &Value, const char *const *ppNames, int Count, CUi::SDropDownState &DropDown, CScrollRegion &Scroll) {
		CUIRect Row, Label, Control;
		if(!Flow.Control(pId, Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
			return;
		SplitSettingsControlRow(Row, Metrics, &Label, &Control);
		Ui()->DoLabel(&Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML);
		DropDown.m_SelectionPopupContext.m_pScrollRegion = &Scroll;
		const int Selected = DoSettingsDropDown(&Control, Value, ppNames, Count, DropDown);
		if(Editable && Selected >= 0 && Selected < Count)
			Value = Selected;
	};

	Text("profile-wheel-help", Localize("Create wheel presets here. Existing wheel entries are kept."));
	CUIRect Row;
	const char *apTypes[] = {Localize("Name"), Localize("Identity preset"), Localize("Chat phrase"), Localize("Asset preset")};
	if(Flow.Control("profile-wheel-defaults", Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
	{
		if(DoTClientSettingsButton_Menu(&State.m_DefaultsButton, "profile-wheel-defaults", Localize("Add default presets"), 0, &Row) && Editable)
		{
			std::vector<std::pair<std::string, std::string>> vMissing;
			bool Valid = true;
			for(int Type = 0; Type < 4; ++Type)
			{
				const std::string Label = std::string(Localize("Default")) + ": " + apTypes[Type];
				if(std::any_of(Wheel.m_vBinds.begin(), Wheel.m_vBinds.end(), [&](const auto &Bind) { return Label == Bind.m_aName; }))
					continue;
				SPreset Preset = CurrentPreset(static_cast<EType>(Type));
				Preset.m_Chat = Localize("Thanks!");
				Preset.m_aAssets.fill("default");
				std::string Command;
				Valid = Valid && Label.size() < BINDWHEEL_MAX_NAME && BuildCommand(Preset, Command);
				vMissing.emplace_back(Label, Command);
			}
			if(Valid && Wheel.m_vBinds.size() + vMissing.size() <= BINDWHEEL_MAX_BINDS)
			{
				for(const auto &[Label, Command] : vMissing)
					Wheel.SetBind(-1, Label.c_str(), Command.c_str());
				State.m_pStatus = Localizable("Saved to the bind wheel.");
			}
			else
				State.m_pStatus = Localizable("Check the preset fields, label, and wheel capacity (64 entries).");
		}
	}
	if(Flow.Control("profile-wheel-key", Metrics.m_ButtonHeight, Row))
		DoLine_KeyReader(Row, State.m_WheelKey, State.m_ClearWheelKey, Localize("Bind Wheel Key"), "+bindwheel");
	Input("profile-wheel-label", Localize("Wheel label"), State.m_Label);
	const int OldType = State.m_Type;
	Choice("profile-wheel-type", Localize("Preset type"), State.m_Type, apTypes, 4, State.m_TypeDropdown, State.m_TypeScroll);
	if(Editable && OldType != State.m_Type)
	{
		State.Load(CurrentPreset(static_cast<EType>(State.m_Type)));
		return;
	}
	if(State.m_Type <= static_cast<int>(EType::IDENTITY))
	{
		const char *apTargets[] = {Localize("Main Tee"), Localize("Dummy"), Localize("Main Tee and dummy")};
		Choice("profile-wheel-target", Localize("Apply to"), State.m_Target, apTargets, 3, State.m_TargetDropdown, State.m_TargetScroll);
		for(int Tee = 0; Tee < 2; ++Tee)
		{
			if(State.m_Target != 2 && State.m_Target != Tee)
				continue;
			auto &Identity = State.m_aTees[Tee];
			const bool NameOnly = State.m_Type == static_cast<int>(EType::NAME);
			const std::string Prefix = "profile-wheel-tee-" + std::to_string(Tee);
			Text((Prefix + "-title").c_str(), Tee ? Localize("Dummy") : Localize("Main Tee"));
			Input((Prefix + "-name").c_str(), Localize("Name"), Identity.m_Name, NameOnly ? nullptr : &Identity.m_aEnabled[NAME]);
			if(NameOnly)
				continue;
			Input((Prefix + "-clan").c_str(), Localize("Clan"), Identity.m_Clan, &Identity.m_aEnabled[CLAN]);
			Input((Prefix + "-skin").c_str(), Localize("Skin"), Identity.m_Skin, &Identity.m_aEnabled[SKIN]);
			if(Flow.Control((Prefix + "-custom-colors").c_str(), Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
			{
				CUIRect Include, Custom;
				Row.VSplitMid(&Include, &Custom, Metrics.m_LineSpacing);
				if(DoTClientSettingsButton_CheckBox(&Identity.m_aEnabled[CUSTOM_COLORS], (Prefix + "-include-colors").c_str(), Localize("Save/Load Colors"), Identity.m_aEnabled[CUSTOM_COLORS], &Include) && Editable)
				{
					Identity.m_aEnabled[CUSTOM_COLORS] ^= 1;
					Identity.m_aEnabled[BODY_COLOR] = Identity.m_aEnabled[FEET_COLOR] = Identity.m_aEnabled[CUSTOM_COLORS];
				}
				if(DoTClientSettingsButton_CheckBox(&Identity.m_CustomColors, (Prefix + "-use-colors").c_str(), Localize("Custom colors"), Identity.m_CustomColors, &Custom) && Editable)
					Identity.m_CustomColors ^= 1;
			}
			const float ColorsHeight = ResolveSettingsTeeCustomColorsLayout(Content, true, Metrics).m_Height;
			if(Flow.Control((Prefix + "-colors").c_str(), ColorsHeight, Row, Metrics.m_LineSpacing))
			{
				const auto Colors = ResolveSettingsTeeCustomColorsLayout(Row, true, Metrics);
				const CUIRect aTitles[] = {Colors.m_BodyTitle, Colors.m_FeetTitle};
				CUIRect aControls[] = {Colors.m_BodyControls, Colors.m_FeetControls};
				for(int Part = 0; Part < 2; ++Part)
				{
					int &Enabled = Identity.m_aEnabled[BODY_COLOR + Part];
					if(DoTClientSettingsButton_CheckBox(&Enabled, (Prefix + "-color-" + std::to_string(Part)).c_str(), Part ? Localize("Feet") : Localize("Body"), Enabled, &aTitles[Part]) && Editable)
						Enabled ^= 1;
					RenderHslaScrollbars(&aControls[Part], &Identity.m_aColors[Part], false, ColorHSLA::DARKEST_LGT, Metrics);
				}
			}
			Input((Prefix + "-country").c_str(), Localize("Country code"), Identity.m_Country, &Identity.m_aEnabled[COUNTRY]);
			if(Flow.Control((Prefix + "-include-eyes").c_str(), Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
				if(DoTClientSettingsButton_CheckBox(&Identity.m_aEnabled[EYES], (Prefix + "-include-eyes").c_str(), Localize("Save/Load Emote"), Identity.m_aEnabled[EYES], &Row) && Editable)
					Identity.m_aEnabled[EYES] ^= 1;
			const char *apEyes[] = {Localize("Normal"), Localize("Pain"), Localize("Happy"), Localize("Surprise"), Localize("Angry"), Localize("Blink")};
			Choice((Prefix + "-eyes").c_str(), Localize("Eyes", "skins"), Identity.m_Eyes, apEyes, 6, Identity.m_EyesDropdown, Identity.m_EyesScroll);
		}
	}
	else if(State.m_Type == static_cast<int>(EType::CHAT))
	{
		Input("profile-wheel-chat", Localize("Chat phrase"), State.m_Chat);
		if(Flow.Control("profile-wheel-team-chat", Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
			if(DoTClientSettingsButton_CheckBox(&State.m_TeamChat, "profile-wheel-team-chat", Localize("Team chat"), State.m_TeamChat, &Row) && Editable)
				State.m_TeamChat ^= 1;
	}
	else
	{
		Text("profile-wheel-assets-help", Localize("Use installed asset names. Empty fields keep the current asset."));
		const char *apAssets[] = {Localize("Entities"), Localize("Game"), Localize("Emoticons"), Localize("Particles"), Localize("HUD"), Localize("Extras"), Localize("Arrow"), Localize("GUI cursor"), Localize("Strong / Weak")};
		for(int Index = 0; Index < NUM_ASSETS; ++Index)
			Input(AssetCommand(Index), apAssets[Index], State.m_aAssets[Index]);
	}
	if(State.m_Type != static_cast<int>(EType::CHAT) && Flow.Control("profile-wheel-capture", Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
	{
		if(DoTClientSettingsButton_Menu(&State.m_CaptureButton, "profile-wheel-capture", Localize("Use current settings"), 0, &Row) && Editable)
		{
			const int Target = State.m_Target;
			State.Load(CurrentPreset(static_cast<EType>(State.m_Type)));
			State.m_Target = Target;
		}
	}
	if(Flow.Control("profile-wheel-save", Metrics.m_ButtonHeight, Row, Metrics.m_SectionGap))
	{
		if(DoTClientSettingsButton_Menu(&State.m_SaveButton, "profile-wheel-save", State.m_Original ? Localize("Save changes") : Localize("Add to bind wheel"), 0, &Row) && Editable)
		{
			std::string Command;
			if(!BuildCommand(State.Values(), Command) || !Wheel.SetBind(State.m_Selected, State.m_Label.GetString(), Command.c_str()))
				State.m_pStatus = Localizable("Check the preset fields, label, and wheel capacity (64 entries).");
			else
			{
				if(State.m_Selected < 0)
					State.m_Selected = static_cast<int>(Wheel.m_vBinds.size()) - 1;
				State.m_Original = Wheel.m_vBinds[State.m_Selected];
				State.m_pStatus = Localizable("Saved to the bind wheel.");
			}
		}
	}
	if(Flow.Control("profile-wheel-new-remove", Metrics.m_ButtonHeight, Row, Metrics.m_LineSpacing))
	{
		CUIRect New, Remove;
		Row.VSplitMid(&New, &Remove, Metrics.m_LineSpacing);
		if(DoTClientSettingsButton_Menu(&State.m_NewButton, "profile-wheel-new", Localize("New preset"), 0, &New) && Editable)
		{
			State.m_Selected = -1;
			State.m_Original.reset();
			State.m_Label.Set("");
			State.Load(CurrentPreset(static_cast<EType>(State.m_Type)));
		}
		if(DoTClientSettingsButton_Menu(&State.m_DeleteButton, "profile-wheel-remove", Localize("Remove Bind"), 0, &Remove) && Editable && State.m_Original)
		{
			Wheel.RemoveBind(State.m_Selected);
			State.m_Selected = -1;
			State.m_Original.reset();
			State.m_pStatus = "";
		}
	}
	Text("profile-wheel-status", Localize(State.m_pStatus));
	Text("profile-wheel-list-title", Localize("Bind Wheel"));
	if(!Flow.Control("profile-wheel-list", Metrics.m_ListRowHeight * 5, Row, Metrics.m_LineSpacing))
		return;
	auto &List = ReadOnly ? State.m_ReadOnlyList : State.m_List;
	List.SetActive(!ReadOnly);
	List.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
	List.DoStart(Metrics.m_ListRowHeight, Wheel.m_vBinds.size(), 1, 3, State.m_Selected, &Row);
	for(size_t Index = 0; Index < Wheel.m_vBinds.size(); ++Index)
	{
		const CListboxItem Item = List.DoNextItem(&State.m_aItemIds[Index], State.m_Selected == static_cast<int>(Index));
		if(Item.m_Visible)
			Ui()->DoLabel(&Item.m_Rect, Wheel.m_vBinds[Index].m_aName, Metrics.m_BodySize, TEXTALIGN_ML);
	}
	const int Selected = List.DoEnd();
	if(Editable && Selected >= 0 && Selected < static_cast<int>(Wheel.m_vBinds.size()) && (Selected != State.m_Selected || List.WasItemActivated()))
	{
		const auto &Bind = Wheel.m_vBinds[Selected];
		const auto Preset = ParseCommand(Bind.m_aCommand);
		if(Preset)
		{
			State.Load(*Preset);
			State.m_Label.Set(Bind.m_aName);
			State.m_Selected = Selected;
			State.m_Original = Bind;
		}
		else
			State.m_pStatus = Localizable("Edit this custom command on the Bind Wheel page.");
	}
}
