#include "QmCardCatalogInternal.h"

#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_overview.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using qm_bind_editor::ECommandCategory;
	using qm_bind_editor::SCommandOption;

	constexpr std::array<const char *, static_cast<size_t>(ECommandCategory::NUM_CATEGORIES)> s_apCategoryLabels = {
		Localizable("All categories"),
		Localizable("Movement"),
		Localizable("Camera"),
		Localizable("Dummy"),
		Localizable("Weapon"),
		Localizable("Chat"),
		Localizable("Voting"),
		Localizable("Miscellaneous"),
		Localizable("Console"),
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
		std::array<CButtonContainer, s_apCategoryLabels.size()> m_aCategoryButtons;
		ECommandCategory m_Category = ECommandCategory::ALL;
		ECommandCategory m_LastCategory = ECommandCategory::NUM_CATEGORIES;
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
		State.m_vOptions = qm_bind_editor::CommandOptions(Console);
		State.m_LastCategory = ECommandCategory::NUM_CATEGORIES;
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
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = Render;
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog

CUi::EPopupMenuFunctionResult CMenus::PopupQmBindCommands(void *pContext, CUIRect View, const bool Active)
{
	auto &State = *static_cast<SBindCommandEditorState *>(pContext);
	CMenus *pMenus = State.m_pMenus;
	CUi *pUi = pMenus->Ui();
	View.Margin(4.0f, &View);
	const IUiContext Ctx = pMenus->SettingsUiContext("qm_bind_command_picker", 1.0f);
	const float TabFontSize = State.m_FontSize * 0.9f;
	const float Gap = 4.0f;
	float TabWidth = State.m_LineHeight * 3.0f;
	for(const char *pLabel : s_apCategoryLabels)
		TabWidth = std::max(TabWidth, pMenus->TextRender()->TextWidth(TabFontSize, Localize(pLabel)) + 12.0f);
	const int TabsPerRow = std::clamp(static_cast<int>((View.w + Gap) / (TabWidth + Gap)), 1, static_cast<int>(s_apCategoryLabels.size()));
	CUIRect TabRow;
	for(size_t Index = 0; Index < s_apCategoryLabels.size(); ++Index)
	{
		if(Index % TabsPerRow == 0)
		{
			View.HSplitTop(State.m_LineHeight, &TabRow, &View);
			View.HSplitTop(Gap, nullptr, &View);
		}
		CUIRect Tab;
		TabRow.VSplitLeft((View.w - Gap * (TabsPerRow - 1)) / TabsPerRow, &Tab, &TabRow);
		if(Index % TabsPerRow + 1 < static_cast<size_t>(TabsPerRow))
			TabRow.VSplitLeft(Gap, nullptr, &TabRow);
		const auto Category = static_cast<ECommandCategory>(Index);
		const bool Selected = State.m_Category == Category;
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Enabled = Active;
		Options.m_Selected = Selected;
		const ColorRGBA Fill = ui_widget::DrawButtonSurface(Ctx, &State.m_aCategoryButtons[Index], Tab, Options);
		CUiScopedSurfaceText SurfaceText(pMenus->TextRender(), Fill);
		SLabelProperties Props;
		Props.m_MaxWidth = Tab.w;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Tab, Localize(s_apCategoryLabels[Index]), TabFontSize, TEXTALIGN_MC, Props);
		pMenus->GameClient()->m_Tooltips.DoToolTip(&State.m_aCategoryButtons[Index], &Tab, Localize(s_apCategoryLabels[Index]));
		if(Active && pUi->DoButtonLogic(&State.m_aCategoryButtons[Index], Selected, &Tab, BUTTONFLAG_LEFT))
			State.m_Category = Category;
	}
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
	const auto SearchResult = ui_widget::InputField(Ctx, &State.m_Filter, Search, SearchOptions);
	if(State.m_LastCategory != State.m_Category || State.m_LastFilter != State.m_Filter.GetString())
	{
		State.m_LastCategory = State.m_Category;
		State.m_LastFilter = State.m_Filter.GetString();
		State.m_vFiltered = qm_bind_editor::FilterCommandOptions(State.m_vOptions, State.m_Category, State.m_LastFilter, [](const char *pText) { return Localize(pText); });
		State.m_Selection = State.m_vFiltered.empty() ? -1 : 0;
		State.m_ListBox.ResetScroll();
	}

	State.m_ListBox.SetActive(Active);
	State.m_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	const int ItemsPerRow = std::max(1, static_cast<int>(View.w / (State.m_LineHeight * 8.0f)));
	State.m_ListBox.DoStart(State.m_LineHeight * 1.8f, (int)State.m_vFiltered.size(), ItemsPerRow, 3, State.m_Selection, &View, false);
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
	const bool Enabled = (!ReadOnly || Ui()->BackgroundRender()) && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive() && !GameClient()->m_GameConsole.IsActive();
	if(!ReadOnly && State.m_Slot != Slot)
	{
		State.m_Slot = Slot;
		State.m_pError = nullptr;
	}
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Selector;
	bool OpenPicker = false;
	if(Flow.Control("bind-command-selector", Metrics.m_LineHeight, Selector))
	{
		CUIRect SelectorLabel, Arrow;
		Selector.VSplitRight(Metrics.m_LineHeight, &SelectorLabel, &Arrow);
		const char *pSelection = State.m_SelectedLabel.empty() ? Localize("Add function") : Localize(State.m_SelectedLabel.c_str());
		const unsigned SelectorFlags = BUTTONFLAG_LEFT | (Enabled ? BUTTONFLAG_CURRENT_HIT : BUTTONFLAG_NONE);
		const bool Pick = DoButton_Menu(&State.m_PickerButton, "", Enabled ? 0 : -1, &SelectorLabel, SelectorFlags);
		CUIRect SelectionText;
		SelectorLabel.VMargin(4.0f, &SelectionText);
		SLabelProperties SelectionProps;
		SelectionProps.m_MaxWidth = SelectionText.w;
		SelectionProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&SelectionText, pSelection, Metrics.m_BodySize, TEXTALIGN_ML, SelectionProps);
		const bool Drop = Ui()->DoButton_QmIcon(&State.m_DropdownButton, EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN, Enabled ? 0 : -1, &Arrow, SelectorFlags, IGraphics::CORNER_ALL);
		if(!ReadOnly)
			GameClient()->m_Tooltips.DoToolTip(&State.m_DropdownButton, &Arrow, Localize("Add function"));
		OpenPicker = Enabled && (Pick || Drop);
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
				const float Width = std::min(std::max(Selector.w, 560.0f), Screen.w - 16.0f);
				const float Height = std::min(std::max(380.0f, Metrics.m_LineHeight * 19.0f), Screen.h - 16.0f);
				const float X = std::clamp(Selector.x, Screen.x + 8.0f, Screen.x + Screen.w - Width - 8.0f);
				const float Y = std::clamp(Selector.y + Selector.h, Screen.y + 8.0f, Screen.y + Screen.h - Height - 8.0f);
				auto Props = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
				Props.m_AutoReposition = false;
				Props.m_BlockUnderlyingPointerInput = true;
				Props.m_BlockUnderlyingScroll = true;
				Props.m_RequireSourceRefresh = true;
				Props.m_SourceFrame = Ui()->PopupSourceFrame();
				Ui()->DoPopupMenu(&State.m_PopupId, X, Y, Width, Height, &State, PopupQmBindCommands, Props);
			}
			else
				Ui()->ClosePopupMenu(&State.m_PopupId);
		}
		if(!ReadOnly && !Ui()->IsPopupOpen(&State.m_PopupId))
			Ui()->ReleaseActiveTextInput(&State.m_Filter);
	}
	Flow.Space(Metrics.m_LineSpacing);
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
		char aLabelId[64];
		str_format(aLabelId, sizeof(aLabelId), "bind-argument-%d-label", (int)Index);
		CUIRect Label;
		if(Flow.Control(aLabelId, Metrics.m_LineHeight, Label))
		{
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
		}
		Flow.Space(Metrics.m_LineSpacing);
		char aInputId[64];
		str_format(aInputId, sizeof(aInputId), "bind-argument-%d-input", (int)Index);
		CUIRect InputRect;
		if(Flow.Control(aInputId, Metrics.m_LineHeight, InputRect))
		{
			InputOptions.m_ProcessInput = Enabled && !OpenPicker && Argument.m_Included;
			InputOptions.m_pPlaceholder = Argument.m_Parameter.m_Optional ? Localize("Optional") : nullptr;
			if(ui_widget::InputField(InputCtx, &Argument.m_Input, InputRect, InputOptions).m_Changed && Enabled)
				ArgumentsChanged = true;
		}
		Flow.Space(Metrics.m_LineSpacing);
	}
	if(ArgumentsChanged)
		UpdateCommand(State);

	CUIRect CommandRow;
	CUIRect AddRow;
	ui_widget::SInputFieldResult CommandResult{};
	if(Flow.Control("bind-command-input", Metrics.m_LineHeight, CommandRow))
	{
		InputOptions.m_ProcessInput = Enabled && !OpenPicker;
		InputOptions.m_Clearable = true;
		InputOptions.m_pPlaceholder = Localize("Command");
		CommandResult = ui_widget::InputField(InputCtx, &State.m_CommandInput, CommandRow, InputOptions);
		if(CommandResult.m_Changed && Enabled)
		{
			ClearArguments(Ui(), State);
			State.m_SelectedLabel = Localizable("Custom command");
			State.m_pError = nullptr;
		}
	}
	Flow.Space(Metrics.m_LineSpacing);
	const bool CanAdd = Enabled && !OpenPicker && State.m_ArgumentsValid && !State.m_CommandInput.IsEmpty();
	if(Flow.Control("bind-command-add", Metrics.m_ButtonHeight, AddRow))
	{
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
	}
	Flow.Space(Metrics.m_LineSpacing);
	if(State.m_pError != nullptr)
	{
		Flow.Text("bind-command-error", Localize(State.m_pError), 0.0f, Metrics.m_SmallSize, Metrics.m_LineSpacing, [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [this, Metrics](const CUIRect &Error, const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Error.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
		Ui()->DoLabel(&Error, pText, Metrics.m_SmallSize, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor()); }, Metrics.m_RowStep * 2.0f);
	}
}

namespace
{
	struct SBindOverviewEntryUi
	{
		std::string m_KeyName;
		std::string m_Command;
		EBindOptionGroup m_Group;
	};

	struct SBindOverviewState
	{
		CLineInputBuffered<256> m_Filter;
		bool m_SortByCommand = false;
		CButtonContainer m_SortButton;
		CButtonContainer m_CopyButton;
		std::vector<SBindOverviewEntryUi> m_vEntries;
		std::vector<int> m_vFiltered;
		uint64_t m_BindsRevision = 0;
		bool m_EntriesValid = false;
		float m_CopiedTime = -1e9f;
	};

	SBindOverviewState &BindOverviewState()
	{
		static SBindOverviewState s_State;
		return s_State;
	}

	const char *BindOverviewGroupTitle(EBindOptionGroup Group)
	{
		switch(Group)
		{
		case EBindOptionGroup::MOVEMENT: return Localize("Movement");
		case EBindOptionGroup::WEAPON: return Localize("Weapon");
		case EBindOptionGroup::VOTING: return Localize("Voting");
		case EBindOptionGroup::CHAT: return Localize("Chat");
		case EBindOptionGroup::DUMMY: return Localize("Dummy");
		case EBindOptionGroup::MISCELLANEOUS: return Localize("Miscellaneous");
		default: return Localize("Other");
		}
	}

	void RebuildBindOverviewEntries(SBindOverviewState &State, const CBinds &Binds)
	{
		State.m_vEntries.clear();
		for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
		{
			for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
			{
				const char *pCommand = Binds.Get(Key, Modifier);
				if(!pCommand[0])
					continue;
				char aKeyName[128];
				Binds.GetKeyBindName(Key, Modifier, aKeyName, sizeof(aKeyName));
				State.m_vEntries.push_back({aKeyName, pCommand, qm_bind_overview::GroupForCommand(pCommand)});
			}
		}
		State.m_BindsRevision = Binds.Revision();
		State.m_EntriesValid = true;
	}

	void RefreshBindOverviewFilter(SBindOverviewState &State)
	{
		State.m_vFiltered.clear();
		const char *pFilter = State.m_Filter.GetString();
		for(size_t Index = 0; Index < State.m_vEntries.size(); ++Index)
		{
			const SBindOverviewEntryUi &Entry = State.m_vEntries[Index];
			if(pFilter[0] == '\0' ||
				str_utf8_find_nocase(Entry.m_KeyName.c_str(), pFilter) != nullptr ||
				str_utf8_find_nocase(Entry.m_Command.c_str(), pFilter) != nullptr)
				State.m_vFiltered.push_back((int)Index);
		}
		// 排序只改变组内条目顺序：过滤列表本身有序，按组挑取时自然保持该顺序。
		if(State.m_SortByCommand)
		{
			std::sort(State.m_vFiltered.begin(), State.m_vFiltered.end(), [&](int Left, int Right) {
				return str_utf8_comp_nocase(State.m_vEntries[Left].m_Command.c_str(), State.m_vEntries[Right].m_Command.c_str()) < 0;
			});
		}
		else
		{
			std::sort(State.m_vFiltered.begin(), State.m_vFiltered.end(), [&](int Left, int Right) {
				return str_utf8_comp_nocase(State.m_vEntries[Left].m_KeyName.c_str(), State.m_vEntries[Right].m_KeyName.c_str()) < 0;
			});
		}
	}
} // namespace

void CMenus::RenderQmBindOverviewContent(CUIRect &Content, const bool ReadOnly)
{
	SBindOverviewState &State = BindOverviewState();
	const CBinds *pBinds = &GameClient()->m_Binds;
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Content.w);
	const bool Enabled = (!ReadOnly || Ui()->BackgroundRender()) && !Ui()->IsPopupOpen() && !GameClient()->m_KeyBinder.IsActive() && !GameClient()->m_GameConsole.IsActive();

	// 绑定变化时重建条目；过滤与排序每帧重算，绑定数量小，测量与绘制共享结果。
	if(!State.m_EntriesValid || State.m_BindsRevision != pBinds->Revision())
		RebuildBindOverviewEntries(State, *pBinds);
	RefreshBindOverviewFilter(State);

	CSettingsContentLayoutFlow Flow(Content);

	CUIRect Toolbar;
	if(Flow.Control("bind-overview-toolbar", Metrics.m_ButtonHeight, Toolbar))
	{
		const float Scale = Metrics.m_UiScale;
		CUIRect Filter, Sort, Copy;
		Toolbar.VSplitRight(96.0f * Scale, &Toolbar, &Copy);
		Toolbar.VSplitRight(4.0f * Scale, &Toolbar, nullptr);
		Toolbar.VSplitRight(130.0f * Scale, &Toolbar, &Sort);
		Toolbar.VSplitRight(4.0f * Scale, &Toolbar, nullptr);
		Filter = Toolbar;
		if(DoButton_Menu(&State.m_SortButton, State.m_SortByCommand ? Localize("Sort by command") : Localize("Sort by key"), 0, &Sort) && Enabled)
			State.m_SortByCommand = !State.m_SortByCommand;
		if(DoButton_Menu(&State.m_CopyButton, Localize("Copy all"), 0, &Copy) && Enabled)
		{
			std::vector<qm_bind_overview::SBindOverviewEntry> vCopyEntries;
			vCopyEntries.reserve(State.m_vFiltered.size());
			for(const int Index : State.m_vFiltered)
			{
				const SBindOverviewEntryUi &Entry = State.m_vEntries[Index];
				vCopyEntries.push_back({Entry.m_KeyName.c_str(), Entry.m_Command.c_str()});
			}
			const std::string Text = qm_bind_overview::Serialize(vCopyEntries);
			Input()->SetClipboardText(Text.c_str());
			State.m_CopiedTime = Client()->LocalTime();
		}
		IUiContext InputCtx = SettingsUiContext("qm_bind_overview_filter", Metrics.m_UiScale);
		ui_widget::SInputFieldOptions FilterOptions;
		FilterOptions.m_pPlaceholder = Localize("Search");
		FilterOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		FilterOptions.m_Clearable = true;
		FilterOptions.m_ProcessInput = Enabled;
		FilterOptions.m_FontSize = Metrics.m_BodySize;
		(void)ui_widget::InputField(InputCtx, &State.m_Filter, Filter, FilterOptions);
	}
	Flow.Space(Metrics.m_LineSpacing);

	if(Client()->LocalTime() - State.m_CopiedTime < 2.0f)
	{
		CUIRect StatusRow;
		if(Flow.Control("bind-overview-copied", Metrics.m_LineHeight, StatusRow))
		{
			TextRender()->TextColor(ui_token::color::SUCCESS);
			Ui()->DoLabel(&StatusRow, Localize("Copied"), Metrics.m_SmallSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		Flow.Space(Metrics.m_LineSpacing * 0.5f);
	}

	int EntryIndex = 0;
	bool HasAny = false;
	for(int GroupIndex = 0; GroupIndex < (int)EBindOptionGroup::NUM; ++GroupIndex)
	{
		const EBindOptionGroup Group = static_cast<EBindOptionGroup>(GroupIndex);
		bool GroupStarted = false;
		for(const int Index : State.m_vFiltered)
		{
			const SBindOverviewEntryUi &Entry = State.m_vEntries[Index];
			if(Entry.m_Group != Group)
				continue;
			if(!GroupStarted)
			{
				CUIRect Header;
				char aId[64];
				str_format(aId, sizeof(aId), "bind-overview-group-%d", GroupIndex);
				if(Flow.Control(aId, Metrics.m_LineHeight, Header))
				{
					TextRender()->TextColor(ui_token::color::TEXT_TIP);
					Ui()->DoLabel(&Header, BindOverviewGroupTitle(Group), Metrics.m_SmallSize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				Flow.Space(Metrics.m_LineSpacing * 0.4f);
				GroupStarted = true;
			}
			CUIRect Row;
			char aRowId[64];
			str_format(aRowId, sizeof(aRowId), "bind-overview-entry-%d", EntryIndex++);
			if(Flow.Control(aRowId, Metrics.m_LineHeight, Row))
			{
				CUIRect KeyRect, CommandRect;
				Row.VSplitLeft(std::min(Row.w * 0.4f, 160.0f * Metrics.m_UiScale), &KeyRect, &CommandRect);
				Ui()->DoLabel(&KeyRect, Entry.m_KeyName.c_str(), Metrics.m_BodySize, TEXTALIGN_ML);
				SLabelProperties Props;
				Props.m_MaxWidth = std::max(1.0f, CommandRect.w);
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&CommandRect, Entry.m_Command.c_str(), Metrics.m_BodySize, TEXTALIGN_ML, Props);
			}
			Flow.Space(Metrics.m_LineSpacing * 0.35f);
			HasAny = true;
		}
		if(GroupStarted)
			Flow.Space(Metrics.m_LineSpacing * 0.6f);
	}
	if(!HasAny)
	{
		CUIRect EmptyRow;
		if(Flow.Control("bind-overview-empty", Metrics.m_LineHeight, EmptyRow))
			Ui()->DoLabel(&EmptyRow, Localize("No results"), Metrics.m_BodySize, TEXTALIGN_ML);
	}
}

namespace qm_card_catalog
{
	bool BuildBindOverviewCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr)
			return false;

		const bool ReadOnly = Ctx.m_ReadOnly;
		CMenus *pMenus = Ctx.m_pMenus;
		const FSettingsCardRenderMeasured Render = [pMenus, ReadOnly](CUIRect &Content) {
			QmCardRenderHook::RenderQmBindOverviewContent(pMenus, Content, ReadOnly);
		};

		Out = {};
		Out.m_Spec = {"qm:bind_overview", Localize("Bind overview"), qm_card_registry::ResolveLocalizedDescription("qm:bind_overview")};
		Out.m_Render = [Render](CUIRect Content) { Render(Content); };
		Out.m_RenderMeasured = Render;
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
