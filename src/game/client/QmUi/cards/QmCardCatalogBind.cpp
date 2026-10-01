#include "QmCardCatalogInternal.h"

#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
	struct SBindPreset
	{
		const char *m_pLabel;
		const char *m_pCommand;
	};

	constexpr SBindPreset s_aPresets[] = {
		{Localizable("Move left"), "+left"},
		{Localizable("Move right"), "+right"},
		{Localizable("Jump"), "+jump"},
		{Localizable("Fire"), "+fire"},
		{Localizable("Hook"), "+hook"},
		{Localizable("Hook collisions"), "+showhookcoll"},
		{Localizable("Pause"), "say /pause"},
		{Localizable("Kill"), "kill"},
		{Localizable("Zoom in"), "zoom+"},
		{Localizable("Zoom out"), "zoom-"},
		{Localizable("Default zoom"), "zoom"},
		{Localizable("Show others"), "say /showothers"},
		{Localizable("Show all"), "say /showall"},
		{Localizable("Toggle dyncam"), "toggle cl_dyncam 0 1"},
		{Localizable("Toggle ghost"), "toggle cl_race_show_ghost 0 1"},
		{Localizable("Hammer"), "+weapon1"},
		{Localizable("Pistol"), "+weapon2"},
		{Localizable("Shotgun"), "+weapon3"},
		{Localizable("Grenade"), "+weapon4"},
		{Localizable("Laser"), "+weapon5"},
		{Localizable("Next weapon"), "+nextweapon"},
		{Localizable("Prev. weapon"), "+prevweapon"},
		{Localizable("Vote yes"), "vote yes"},
		{Localizable("Vote no"), "vote no"},
		{Localizable("Chat"), "+show_chat; chat all"},
		{Localizable("Team chat"), "+show_chat; chat team"},
		{Localizable("Converse"), "+show_chat; chat all /c "},
		{Localizable("Show chat"), "+show_chat"},
		{Localizable("Repeat message"), "+qm_repeat"},
		{Localizable("Voice chat"), "+qm_voice_ptt"},
		{Localizable("Toggle dummy"), "toggle cl_dummy 0 1"},
		{Localizable("Dummy jump"), "+toggle_restore cl_dummy_jump 1"},
		{Localizable("Dummy fire"), "+toggle_restore cl_dummy_fire 1"},
		{Localizable("Dummy hook"), "+toggle_restore cl_dummy_hook 1"},
		{Localizable("Dummy copy"), "toggle cl_dummy_copy_moves 0 1"},
		{Localizable("Dummy control"), "toggle cl_dummy_control 1 0"},
		{Localizable("Emoticon"), "+emote"},
		{Localizable("Spectate mode"), "+spectate"},
		{Localizable("Spectate next"), "spectate_next"},
		{Localizable("Spectate previous"), "spectate_previous"},
		{Localizable("Active disconnect"), "qm_timeout_disconnect"},
		{Localizable("Console"), "toggle_local_console"},
		{Localizable("Screenshot"), "screenshot"},
		{Localizable("Scoreboard"), "+scoreboard"},
		{Localizable("Statboard"), "+statboard"},
		{Localizable("Pie menu"), "+pie_menu"},
		{Localizable("Show entities"), "toggle cl_overlay_entities 0 100"},
		{Localizable("Show HUD"), "toggle cl_showhud 0 1"},
	};

	struct SCommandOption
	{
		std::string m_Label;
		qm_bind_editor::SRegisteredCommand m_Command;
		bool m_Preset = false;
	};

	struct SArgumentInput
	{
		qm_bind_editor::SParameter m_Parameter;
		CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> m_Input;
		CButtonContainer m_IncludeButton;
		bool m_Included = true;
	};

	struct SBindCommandEditorState
	{
		CMenus *m_pMenus = nullptr;
		SPopupMenuId m_PopupId;
		CButtonContainer m_PickerButton;
		CButtonContainer m_DropdownButton;
		CButtonContainer m_AddButton;
		CLineInputBuffered<256> m_Filter;
		CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> m_CommandInput;
		CListBox m_ListBox;
		std::vector<SCommandOption> m_vOptions;
		std::vector<size_t> m_vFiltered;
		std::vector<std::unique_ptr<SArgumentInput>> m_vArguments;
		std::optional<qm_bind_editor::SRegisteredCommand> m_Command;
		std::string m_SelectedLabel;
		std::string m_LastFilter;
		const char *m_pError = nullptr;
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		int m_Selection = -1;
		bool m_FocusFilter = false;
		bool m_ArgumentsValid = true;
		float m_FontSize = 12.0f;
		float m_LineHeight = 20.0f;
	};

	SBindCommandEditorState &CommandEditorState()
	{
		static SBindCommandEditorState s_State;
		return s_State;
	}

	void ClearArguments(CUi *pUi, SBindCommandEditorState &State)
	{
		for(const auto &pArgument : State.m_vArguments)
			pUi->ReleaseActiveTextInput(&pArgument->m_Input);
		State.m_vArguments.clear();
		State.m_Command.reset();
		State.m_ArgumentsValid = true;
	}

	void UpdateCommand(SBindCommandEditorState &State)
	{
		if(!State.m_Command.has_value())
			return;
		std::vector<qm_bind_editor::SParameter> vParameters;
		std::vector<std::optional<std::string>> vArguments;
		for(const auto &pArgument : State.m_vArguments)
		{
			vParameters.push_back(pArgument->m_Parameter);
			vArguments.emplace_back(pArgument->m_Included ? std::make_optional<std::string>(pArgument->m_Input.GetString()) : std::nullopt);
		}
		std::string Command;
		State.m_ArgumentsValid = qm_bind_editor::ComposeCommand(State.m_Command->m_Name, vParameters, vArguments, Command);
		if(State.m_ArgumentsValid)
			State.m_CommandInput.Set(Command.c_str());
		State.m_pError = State.m_ArgumentsValid ? nullptr : Localizable("Invalid command or parameters.");
	}

	void PopulateOptions(IConsole &Console, SBindCommandEditorState &State)
	{
		State.m_vOptions.clear();
		State.m_vOptions.push_back({Localizable("Custom command"), {}, true});
		for(const SBindPreset &Preset : s_aPresets)
			State.m_vOptions.push_back({Preset.m_pLabel, {Preset.m_pCommand, {}, {}}, true});
		for(auto &Command : qm_bind_editor::RegisteredCommands(Console))
			State.m_vOptions.push_back({Command.m_Name, std::move(Command), false});
		State.m_vFiltered.clear();
		State.m_LastFilter.clear();
		State.m_Filter.Clear();
		State.m_Selection = -1;
		State.m_FocusFilter = true;
		State.m_ListBox.Reset();
	}
}

namespace qm_card_catalog
{
	bool BuildBindCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr)
			return false;

		const bool ReadOnly = Ctx.m_ReadOnly;
		CMenus *pMenus = Ctx.m_pMenus;
		const FSettingsCardRenderMeasured Render = [pMenus, ReadOnly](CUIRect &Content) {
			QmCardRenderHook::RenderQmBindEditorContent(pMenus, Content, ReadOnly);
		};

		Out = {};
		Out.m_Spec = {"qm:bind_editor", Localize("Bind"), qm_card_registry::ResolveLocalizedDescription("qm:bind_editor")};
		Out.m_Measure = [pMenus, Render](const float ContentWidth) {
			return QmCardRenderHook::MeasureContent(pMenus, Render, ContentWidth);
		};
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = Render;
		// 绑定、选中键位和命令筛选都能独立变化，不能依赖注册表版本触发重测。
		Out.m_MeasureEachFrame = true;
		return true;
	}
} // namespace qm_card_catalog

CUi::EPopupMenuFunctionResult CMenus::PopupQmBindCommands(void *pContext, CUIRect View, const bool Active)
{
	auto &State = *static_cast<SBindCommandEditorState *>(pContext);
	CMenus *pMenus = State.m_pMenus;
	CUi *pUi = pMenus->Ui();
	View.Margin(4.0f, &View);
	CUIRect Search;
	View.HSplitTop(State.m_LineHeight, &Search, &View);
	View.HSplitTop(4.0f, nullptr, &View);
	if(Active && State.m_FocusFilter)
	{
		pUi->SetActiveItem(&State.m_Filter);
		State.m_Filter.Activate(EInputPriority::UI);
		State.m_FocusFilter = false;
	}
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_pPlaceholder = Localize("Search");
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_Clearable = true;
	SearchOptions.m_ProcessInput = Active;
	SearchOptions.m_FontSize = State.m_FontSize;
	const IUiContext Ctx = pMenus->SettingsUiContext("qm_bind_command_picker", 1.0f);
	const auto SearchResult = ui_widget::InputField(Ctx, &State.m_Filter, Search, SearchOptions);
	const bool FirstFrame = State.m_vFiltered.empty() && State.m_LastFilter.empty();
	if(FirstFrame || State.m_LastFilter != State.m_Filter.GetString())
	{
		State.m_LastFilter = State.m_Filter.GetString();
		State.m_vFiltered.clear();
		for(size_t Index = 0; Index < State.m_vOptions.size(); ++Index)
		{
			const SCommandOption &Option = State.m_vOptions[Index];
			const char *pFilter = State.m_LastFilter.c_str();
			if(State.m_LastFilter.empty() || str_utf8_find_nocase(Localize(Option.m_Label.c_str()), pFilter) ||
				str_utf8_find_nocase(Option.m_Command.m_Name.c_str(), pFilter) ||
				str_utf8_find_nocase(Localize(Option.m_Command.m_Help.c_str()), pFilter))
				State.m_vFiltered.push_back(Index);
		}
		State.m_Selection = State.m_vFiltered.empty() ? -1 : 0;
		State.m_ListBox.ResetScroll();
	}

	State.m_ListBox.SetActive(Active);
	State.m_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	State.m_ListBox.DoStart(State.m_LineHeight * 1.8f, (int)State.m_vFiltered.size(), 1, 3, State.m_Selection, &View, false);
	for(const size_t Index : State.m_vFiltered)
	{
		const SCommandOption &Option = State.m_vOptions[Index];
		const CListboxItem Item = State.m_ListBox.DoNextItem(&Option);
		if(!Item.m_Visible)
			continue;
		CUIRect Label, Detail;
		Item.m_Rect.VMargin(4.0f, &Label);
		Label.HSplitTop(State.m_LineHeight, &Label, &Detail);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Label, Localize(Option.m_Label.c_str()), State.m_FontSize, TEXTALIGN_ML, Props);
		const char *pDetail = Option.m_Preset ? Option.m_Command.m_Name.c_str() : Option.m_Command.m_Parameters.c_str();
		pUi->DoLabel(&Detail, pDetail, State.m_FontSize * 0.85f, TEXTALIGN_ML, Props);
		if(!Option.m_Command.m_Help.empty())
			pMenus->GameClient()->m_Tooltips.DoToolTip(&Option, &Item.m_Rect, Localize(Option.m_Command.m_Help.c_str()));
	}
	State.m_Selection = State.m_ListBox.DoEnd();
	if(State.m_vFiltered.empty())
		pUi->DoLabel(&View, Localize("No results"), State.m_FontSize, TEXTALIGN_MC);
	if(Active && (State.m_ListBox.WasItemSelected() || State.m_ListBox.WasItemActivated() || SearchResult.m_Submitted) &&
		State.m_Selection >= 0 && State.m_Selection < (int)State.m_vFiltered.size())
	{
		const SCommandOption &Option = State.m_vOptions[State.m_vFiltered[State.m_Selection]];
		ClearArguments(pUi, State);
		State.m_SelectedLabel = Option.m_Label;
		if(!Option.m_Command.m_Name.empty())
			State.m_CommandInput.Set(Option.m_Command.m_Name.c_str());
		State.m_pError = nullptr;
		if(!Option.m_Preset)
		{
			State.m_Command = Option.m_Command;
			for(auto &Parameter : qm_bind_editor::ParseParameters(Option.m_Command.m_Parameters))
			{
				auto pArgument = std::make_unique<SArgumentInput>();
				pArgument->m_Included = !Parameter.m_Optional;
				pArgument->m_Parameter = std::move(Parameter);
				State.m_vArguments.push_back(std::move(pArgument));
			}
			UpdateCommand(State);
		}
		pUi->ReleaseActiveTextInput(&State.m_Filter);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderQmBindCommandEditor(CUIRect &Content, const CBindSlot Slot, const bool ReadOnly)
{
	SBindCommandEditorState &State = CommandEditorState();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Content.w);
	const bool Enabled = !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive() && !GameClient()->m_GameConsole.IsActive();
	if(!ReadOnly && State.m_Slot != Slot)
	{
		State.m_Slot = Slot;
		State.m_pError = nullptr;
	}
	CSettingsContentRowFlow Rows(Content, Metrics);
	CUIRect Selector = Rows.NextLine();
	CUIRect SelectorLabel, Arrow;
	Selector.VSplitRight(Metrics.m_LineHeight, &SelectorLabel, &Arrow);
	const char *pSelection = State.m_SelectedLabel.empty() ? Localize("Add function") : Localize(State.m_SelectedLabel.c_str());
	const bool Pick = DoButton_Menu(&State.m_PickerButton, "", Enabled ? 0 : -1, &SelectorLabel);
	CUIRect SelectionText;
	SelectorLabel.VMargin(4.0f, &SelectionText);
	SLabelProperties SelectionProps;
	SelectionProps.m_MaxWidth = SelectionText.w;
	SelectionProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&SelectionText, pSelection, Metrics.m_BodySize, TEXTALIGN_ML, SelectionProps);
	const bool Drop = Ui()->DoButton_QmIcon(&State.m_DropdownButton, EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN, Enabled ? 0 : -1, &Arrow, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
	if(!ReadOnly)
		GameClient()->m_Tooltips.DoToolTip(&State.m_DropdownButton, &Arrow, Localize("Add function"));
	const bool OpenPicker = Enabled && (Pick || Drop);
	if(OpenPicker)
	{
		State.m_pMenus = this;
		PopulateOptions(*Console(), State);
	}
	if(!ReadOnly && (OpenPicker || Ui()->IsPopupOpen(&State.m_PopupId)))
	{
		const CUIRect *pClip = Ui()->IsClipped() ? Ui()->ClipArea() : nullptr;
		const bool AnchorVisible = pClip == nullptr || (Selector.x < pClip->x + pClip->w && Selector.x + Selector.w > pClip->x && Selector.y < pClip->y + pClip->h && Selector.y + Selector.h > pClip->y);
		if(AnchorVisible)
		{
			State.m_FontSize = Metrics.m_BodySize;
			State.m_LineHeight = Metrics.m_LineHeight;
			const CUIRect Screen = *Ui()->Screen();
			const float Width = std::min(std::max(Selector.w, 300.0f), Screen.w - 16.0f);
			const float Height = std::min(280.0f, Screen.h - 16.0f);
			const float X = std::clamp(Selector.x, Screen.x + 8.0f, Screen.x + Screen.w - Width - 8.0f);
			const float Y = std::clamp(Selector.y + Selector.h, Screen.y + 8.0f, Screen.y + Screen.h - Height - 8.0f);
			SPopupMenuProperties Props;
			Props.m_AutoReposition = false;
			Props.m_BlockUnderlyingPointerInput = true;
			Props.m_BlockUnderlyingScroll = true;
			Props.m_RequireSourceRefresh = true;
			Props.m_SourceFrame = Client()->PerfFrame();
			Ui()->DoPopupMenu(&State.m_PopupId, X, Y, Width, Height, &State, PopupQmBindCommands, Props);
		}
		else
			Ui()->ClosePopupMenu(&State.m_PopupId);
	}
	if(!ReadOnly && !Ui()->IsPopupOpen(&State.m_PopupId))
		Ui()->ReleaseActiveTextInput(&State.m_Filter);

	IUiContext InputCtx = SettingsUiContext("qm_bind_command_editor", Metrics.m_UiScale);
	if(ReadOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_FontSize = Metrics.m_BodySize;
	InputOptions.m_ProcessInput = Enabled && !OpenPicker;
	bool ArgumentsChanged = false;
	for(size_t Index = 0; Index < State.m_vArguments.size(); ++Index)
	{
		SArgumentInput &Argument = *State.m_vArguments[Index];
		CUIRect Label = Rows.NextLine();
		if(Argument.m_Parameter.m_Optional)
		{
			if(DoButton_CheckBox(&Argument.m_IncludeButton, Argument.m_Parameter.m_Name.c_str(), Argument.m_Included, &Label) && Enabled)
			{
				Argument.m_Included = !Argument.m_Included;
				// 可选参数只能从尾部省略，启用后项时同时保留前项的位置。
				for(size_t Other = 0; Other < State.m_vArguments.size(); ++Other)
				{
					if((Argument.m_Included && Other < Index) || (!Argument.m_Included && Other > Index))
						State.m_vArguments[Other]->m_Included = Argument.m_Included;
				}
				ArgumentsChanged = true;
			}
		}
		else
		{
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Label, Argument.m_Parameter.m_Name.c_str(), Metrics.m_BodySize, TEXTALIGN_ML, Props);
		}
		CUIRect InputRect = Rows.NextLine();
		InputOptions.m_ProcessInput = Enabled && !OpenPicker && Argument.m_Included;
		InputOptions.m_pPlaceholder = Argument.m_Parameter.m_Optional ? Localize("Optional") : nullptr;
		if(ui_widget::InputField(InputCtx, &Argument.m_Input, InputRect, InputOptions).m_Changed && Enabled)
			ArgumentsChanged = true;
	}
	if(ArgumentsChanged)
		UpdateCommand(State);

	const CUIRect CommandRow = Rows.NextLine();
	InputOptions.m_ProcessInput = Enabled && !OpenPicker;
	InputOptions.m_Clearable = true;
	InputOptions.m_pPlaceholder = Localize("Command");
	const auto CommandResult = ui_widget::InputField(InputCtx, &State.m_CommandInput, CommandRow, InputOptions);
	if(CommandResult.m_Changed && Enabled)
	{
		ClearArguments(Ui(), State);
		State.m_SelectedLabel = Localizable("Custom command");
		State.m_pError = nullptr;
	}
	const bool CanAdd = Enabled && !OpenPicker && State.m_ArgumentsValid && !State.m_CommandInput.IsEmpty();
	CUIRect AddRow = Rows.NextButton();
	const bool Add = DoButton_Menu(&State.m_AddButton, Localize("Add"), CanAdd ? 0 : -1, &AddRow);
	if(CanAdd && (Add || CommandResult.m_Submitted))
	{
		char aKeyName[128];
		GameClient()->m_Binds.GetKeyBindName(Slot.m_Key, Slot.m_ModifierMask, aKeyName, sizeof(aKeyName));
		std::string Updated;
		if(!Console()->LineIsValid(State.m_CommandInput.GetString()))
			State.m_pError = Localizable("Invalid command or parameters.");
		else if(!qm_bind_editor::AppendCommand(aKeyName, GameClient()->m_Binds.Get(Slot), State.m_CommandInput.GetString(), Updated))
			State.m_pError = Localizable("The command is empty, incomplete, or exceeds the local config line limit.");
		else
		{
			GameClient()->m_Binds.Bind(Slot.m_Key, Updated.c_str(), false, Slot.m_ModifierMask);
			ClearArguments(Ui(), State);
			State.m_CommandInput.Clear();
			State.m_SelectedLabel.clear();
			State.m_pError = nullptr;
		}
	}
	if(State.m_pError != nullptr)
	{
		CUIRect Error = Rows.Next(Metrics.m_RowStep * 2.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = Error.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
		Ui()->DoLabel(&Error, Localize(State.m_pError), Metrics.m_SmallSize, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
