#include "QmCardCatalogInternal.h"

#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmIconSwap.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/bind_overview.h>
#include <game/client/components/qmclient/bind_workshop.h>
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

	using qm_card_catalog::SBindCommandArgumentInput;
	using qm_card_catalog::TryPrepareCardData;

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
		qm_card_catalog::TBindCommandArgumentInputs m_vArguments;
		std::optional<qm_bind_editor::SRegisteredCommand> m_Command;
		std::string m_SelectedLabel;
		std::string m_LastFilter;
		const char *m_pError = nullptr;
		CBindSlot m_Slot = CBindSlot(KEY_UNKNOWN, KeyModifier::NONE);
		int m_Selection = -1;
		bool m_FocusFilter = false;
		bool m_ArgumentsValid = true;
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
		std::string Command;
		if(!TryPrepareCardData([&] {
			   State.m_ArgumentsValid = qm_card_catalog::ComposeBindCommandInput(*State.m_Command, State.m_vArguments, Command);
		   }))
		{
			State.m_ArgumentsValid = false;
			State.m_pError = Localizable("Failed");
			return;
		}
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
	// 弹层在 RenderPopupMenus 阶段绘制，卡片入口的度量作用域已经退出：字号/行高
	// 按弹层自身宽度重新解析，二级层级（分类页签、命令参数行）直接用小字档，
	// 不再在正文字号上叠 0.9/0.85 这类比例系数。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(View.w);
	const IUiContext Ctx = pMenus->SettingsUiContext("qm_bind_command_picker", 1.0f);
	const float TabFontSize = Metrics.m_SmallSize;
	const float Gap = 4.0f;
	float TabWidth = Metrics.m_LineHeight * 3.0f;
	for(const char *pLabel : s_apCategoryLabels)
		TabWidth = std::max(TabWidth, pMenus->TextRender()->TextWidth(TabFontSize, Localize(pLabel)) + 12.0f);
	const int TabsPerRow = std::clamp(static_cast<int>((View.w + Gap) / (TabWidth + Gap)), 1, static_cast<int>(s_apCategoryLabels.size()));
	CUIRect TabRow;
	for(size_t Index = 0; Index < s_apCategoryLabels.size(); ++Index)
	{
		if(Index % TabsPerRow == 0)
		{
			View.HSplitTop(Metrics.m_LineHeight, &TabRow, &View);
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
	View.HSplitTop(Metrics.m_LineHeight, &Search, &View);
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
	SearchOptions.m_FontSize = Metrics.m_BodySize;
	const auto SearchResult = ui_widget::InputField(Ctx, &State.m_Filter, Search, SearchOptions);
	const auto ClosePicker = [&](bool Prepared) {
		if(!Prepared)
			State.m_pError = Localizable("Failed");
		pUi->ReleaseActiveTextInput(&State.m_Filter);
		return CUi::POPUP_CLOSE_CURRENT;
	};
	if(State.m_LastCategory != State.m_Category || State.m_LastFilter != State.m_Filter.GetString())
	{
		if(!TryPrepareCardData([&] {
			   std::string Filter = State.m_Filter.GetString();
			   auto vFiltered = qm_bind_editor::FilterCommandOptions(State.m_vOptions, State.m_Category, Filter, [](const char *pText) { return Localize(pText); });
			   State.m_LastFilter.swap(Filter);
			   State.m_vFiltered.swap(vFiltered);
		   }))
			return ClosePicker(false);
		State.m_LastCategory = State.m_Category;
		State.m_Selection = State.m_vFiltered.empty() ? -1 : 0;
		State.m_ListBox.ResetScroll();
	}

	State.m_ListBox.SetActive(Active);
	State.m_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	const int ItemsPerRow = std::max(1, static_cast<int>(View.w / (Metrics.m_LineHeight * 8.0f)));
	State.m_ListBox.DoStart(Metrics.m_LineHeight * 1.8f, (int)State.m_vFiltered.size(), ItemsPerRow, 3, State.m_Selection, &View, false);
	for(const size_t Index : State.m_vFiltered)
	{
		const SCommandOption &Option = State.m_vOptions[Index];
		const CListboxItem Item = State.m_ListBox.DoNextItem(&Option);
		if(!Item.m_Visible)
			continue;
		CUIRect Label, Detail;
		// 行内容区自带 token 内退（左右），上下仍各自留 4；命令名不再贴着行左缘。
		CUIRect RowContent = Item.m_ContentRect;
		RowContent.VMargin(4.0f, &Label);
		Label.HSplitTop(Metrics.m_LineHeight, &Label, &Detail);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Label, Localize(Option.m_Label.c_str()), Metrics.m_BodySize, TEXTALIGN_ML, Props);
		const char *pDetail = Option.m_Preset ? Option.m_Command.m_Name.c_str() : Option.m_Command.m_Parameters.c_str();
		pUi->DoLabel(&Detail, pDetail, Metrics.m_SmallSize, TEXTALIGN_ML, Props);
		if(!Option.m_Command.m_Help.empty())
			pMenus->GameClient()->m_Tooltips.DoToolTip(&Option, &Item.m_Rect, Localize(Option.m_Command.m_Help.c_str()));
	}
	State.m_Selection = State.m_ListBox.DoEnd();
	if(State.m_vFiltered.empty())
		pUi->DoLabel(&View, Localize("No results"), Metrics.m_BodySize, TEXTALIGN_MC);
	if(Active && (State.m_ListBox.WasItemSelected() || State.m_ListBox.WasItemActivated() || SearchResult.m_Submitted) &&
		State.m_Selection >= 0 && State.m_Selection < (int)State.m_vFiltered.size())
	{
		const SCommandOption &Option = State.m_vOptions[State.m_vFiltered[State.m_Selection]];
		const bool Selected = qm_card_catalog::SelectBindCommandOption(Option, [&](qm_card_catalog::SBindCommandPickerSelection &&Selection) {
			ClearArguments(pUi, State);
			State.m_SelectedLabel.swap(Selection.m_Label);
			if(!Option.m_Command.m_Name.empty())
				State.m_CommandInput.Set(Selection.m_Text.c_str());
			State.m_Command.swap(Selection.m_Command);
			State.m_vArguments.swap(Selection.m_vArguments);
			State.m_ArgumentsValid = Selection.m_ArgumentsValid;
			State.m_pError = State.m_ArgumentsValid ? nullptr : Localizable("Invalid command or parameters.");
		});
		return ClosePicker(Selected);
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
		// 弹层展开时箭头翻到朝上：与选择器弹层同一拍（0.22s），点下去不会瞬间翻面。
		IUiContext PickerIconCtx = SettingsUiContext("settings_bind_command_picker_icon");
		ui_widget::SIconSwapBinding PickerIcon = ui_widget::ResolveIconSwapBinding(
			PickerIconCtx, EQmIconSwapPair::CHEVRON,
			EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN,
			EQmIcon::CHEVRON_UP, FontIcons::FONT_ICON_CHEVRON_UP,
			Ui()->IsPopupOpen(&State.m_PopupId) ? 1.0f : 0.0f, str_quickhash("bind-command-picker-icon"), &State.m_DropdownButton, 0.22f);
		ui_widget::SIconButtonIconDraw PickerIconDraw{&ui_widget::DrawIconSwapBinding, &PickerIcon};
		const bool Drop = Ui()->DoButton_QmIcon(&State.m_DropdownButton, EQmIcon::COUNT, nullptr, Enabled ? 0 : -1, &Arrow, SelectorFlags, IGraphics::CORNER_ALL, true, std::nullopt, false, false, &PickerIconDraw);
		if(!ReadOnly)
			GameClient()->m_Tooltips.DoToolTip(&State.m_DropdownButton, &Arrow, Localize("Add function"));
		OpenPicker = Enabled && (Pick || Drop);
		if(OpenPicker)
		{
			OpenPicker = TryPrepareCardData([&] { PopulateOptions(*Console(), State); });
			if(OpenPicker)
				State.m_pMenus = this;
			else
				State.m_pError = Localizable("Failed");
		}
		if(!ReadOnly && (OpenPicker || Ui()->IsPopupOpen(&State.m_PopupId)))
		{
			const CUIRect *pClip = Ui()->IsClipped() ? Ui()->ClipArea() : nullptr;
			const bool AnchorVisible = pClip == nullptr || (Selector.x < pClip->x + pClip->w && Selector.x + Selector.w > pClip->x && Selector.y < pClip->y + pClip->h && Selector.y + Selector.h > pClip->y);
			if(AnchorVisible)
			{
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
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
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
		SBindCommandArgumentInput &Argument = *State.m_vArguments[Index];
		char aLabelId[64];
		str_format(aLabelId, sizeof(aLabelId), "bind-argument-%d-label", (int)Index);
		CUIRect Label;
		if(Flow.Control(aLabelId, Metrics.m_LineHeight, Label))
		{
			if(Argument.m_Parameter.m_Optional)
			{
				if(DoButton_CheckBox(&Argument.m_IncludeButton, Argument.m_Parameter.m_Name.c_str(), Argument.m_Included, &Label, Metrics.m_BodySize) && Enabled)
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
		char aInputId[64];
		str_format(aInputId, sizeof(aInputId), "bind-argument-%d-input", (int)Index);
		CUIRect InputRect;
		if(Flow.Control(aInputId, Metrics.m_LineHeight, InputRect, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		{
			InputOptions.m_ProcessInput = Enabled && !OpenPicker && Argument.m_Included;
			InputOptions.m_pPlaceholder = Argument.m_Parameter.m_Optional ? Localize("Optional") : nullptr;
			if(ui_widget::InputField(InputCtx, &Argument.m_Input, InputRect, InputOptions).m_Changed && Enabled)
				ArgumentsChanged = true;
		}
		Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
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
	const bool CanAdd = Enabled && !OpenPicker && State.m_ArgumentsValid && !State.m_CommandInput.IsEmpty();
	if(Flow.Control("bind-command-add", Metrics.m_ButtonHeight, AddRow, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
	{
		const bool Add = DoButton_Menu(&State.m_AddButton, Localize("Add"), CanAdd ? 0 : -1, &AddRow);
		if(CanAdd && (Add || CommandResult.m_Submitted))
		{
			using qm_card_catalog::EBindCommandCommitResult;
			CBinds &Binds = GameClient()->m_Binds;
			char aKeyName[128];
			Binds.GetKeyBindName(Slot.m_Key, Slot.m_ModifierMask, aKeyName, sizeof(aKeyName));
			const auto Result = qm_card_catalog::CommitBindCommandDraft(*Console(), State.m_CommandInput, aKeyName, Binds.Get(Slot), [&](const char *pUpdated) {
				return Binds.Rebind(EMPTY_BIND_SLOT, Slot, pUpdated);
			});
			switch(Result)
			{
			case EBindCommandCommitResult::INVALID_COMMAND:
				State.m_pError = Localizable("Invalid command or parameters.");
				break;
			case EBindCommandCommitResult::INVALID_CONFIG_LINE:
				State.m_pError = Localizable("The command is empty, incomplete, or exceeds the local config line limit.");
				break;
			case EBindCommandCommitResult::FAILED:
				State.m_pError = Localizable("Failed");
				break;
			case EBindCommandCommitResult::COMMITTED:
				ClearArguments(Ui(), State);
				State.m_SelectedLabel.clear();
				State.m_pError = nullptr;
				break;
			}
		}
	}
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
	if(State.m_pError != nullptr)
	{
		Flow.Text("bind-command-error", Localize(State.m_pError), 0.0f, Metrics.m_SmallSize, ui_token::settings::RowGap(Metrics.m_LineSpacing), [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [this, Metrics](const CUIRect &Error, const char *pText) {
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
		// 纯逻辑层的占用标记（OCCUPANCY_*），由 BuildOccupancy 一次给出，本卡不再自行判定。
		int m_Flags = qm_bind_workshop::OCCUPANCY_NONE;
		// 点行跳转到键盘编辑器时要用的槽位（CBindSlot 没有默认构造，这里存两个整数）。
		int m_Key = 0;
		int m_ModifierMask = 0;
	};

	struct SBindOverviewState
	{
		CLineInputBuffered<256> m_Filter;
		bool m_SortByCommand = false;
		// 「只看多动作」胶囊：只叠加过滤条件，不改分组与排序。
		bool m_OnlyMultiAction = false;
		int m_MultiActionCount = 0;
		// 每一行一个隐形按钮：点一行就把该键聚焦到键盘卡片上。
		std::array<CButtonContainer, 128> m_aRowButtons{};
		CButtonContainer m_SortButton;
		CButtonContainer m_CopyButton;
		CButtonContainer m_MultiActionButton;
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

	// 徽标两档，都只是信息、不是错误：一条绑定串了多个动作，或者这条命令还挂在别的键上。
	// 两档同时命中就一起显示，中间用细分隔点隔开。
	std::string BindFlagTags(const int Flags)
	{
		std::string Tags;
		if((Flags & qm_bind_workshop::OCCUPANCY_MULTI_ACTION) != 0)
			Tags += Localize("Multi");
		if((Flags & qm_bind_workshop::OCCUPANCY_SHARED_FUNCTION) != 0)
		{
			if(!Tags.empty())
				Tags += " · ";
			Tags += Localize("Same function");
		}
		return Tags;
	}

	// 数据源直接用纯逻辑层的占用结果：键名、命令与标记出自同一遍扫描。
	void RebuildBindOverviewEntries(SBindOverviewState &State, const CBinds &Binds)
	{
		State.m_vEntries.clear();
		State.m_MultiActionCount = 0;
		const std::vector<qm_bind_workshop::SOccupancyEntry> vOccupancy = qm_bind_workshop::BuildOccupancy(
			[&Binds](int Key, int Modifier) { return Binds.Get(Key, Modifier); },
			[&Binds](int Key, int Modifier, char *pBuffer, size_t BufferSize) { Binds.GetKeyBindName(Key, Modifier, pBuffer, BufferSize); });
		State.m_vEntries.reserve(vOccupancy.size());
		for(const qm_bind_workshop::SOccupancyEntry &Occupancy : vOccupancy)
		{
			SBindOverviewEntryUi Entry;
			Entry.m_KeyName = Occupancy.m_KeyName;
			Entry.m_Command = Occupancy.m_Command;
			Entry.m_Group = qm_bind_overview::GroupForCommand(Occupancy.m_Command.c_str());
			Entry.m_Flags = Occupancy.m_Flags;
			Entry.m_Key = Occupancy.m_Slot.m_Key;
			Entry.m_ModifierMask = Occupancy.m_Slot.m_ModifierMask;
			if((Entry.m_Flags & qm_bind_workshop::OCCUPANCY_MULTI_ACTION) != 0)
				++State.m_MultiActionCount;
			State.m_vEntries.push_back(std::move(Entry));
		}
		State.m_BindsRevision = Binds.Revision();
		State.m_EntriesValid = true;
	}

	void RefreshBindOverviewFilter(SBindOverviewState &State)
	{
		// 没有多动作条目时自动退出过滤：胶囊此时置灰不可点，不能让用户困在空列表里。
		if(State.m_MultiActionCount == 0)
			State.m_OnlyMultiAction = false;
		State.m_vFiltered.clear();
		const char *pFilter = State.m_Filter.GetString();
		for(size_t Index = 0; Index < State.m_vEntries.size(); ++Index)
		{
			const SBindOverviewEntryUi &Entry = State.m_vEntries[Index];
			// 过滤与搜索框叠加：两个条件同时满足才显示，分组与排序逻辑不动。
			if(State.m_OnlyMultiAction && (Entry.m_Flags & qm_bind_workshop::OCCUPANCY_MULTI_ACTION) == 0)
				continue;
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

	// 卡片里只说这两档标记的含义，它们都不是错误：一条绑定可以用分号串多个命令；
	// 同一个命令也可能同时挂在别的键上（合法的双键位）。
	// 说明行取小字档本身，不再在已解析的小字号上叠比例（测量与绘制共用这一档）。
	const float HintSize = Metrics.m_SmallSize;
	Flow.Text("bind-overview-occupancy-hint", Localize("Badges are informational: a bind can chain several commands, and one command can be bound to several keys."), 0.0f, HintSize, ui_token::settings::SubRowGap(Metrics.m_LineSpacing), [this, HintSize](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width, FontSize * 1.45f).m_H; }, [this, HintSize](const CUIRect &Row, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Row.w;
			Props.m_EllipsisAtEnd = true;
			TextRender()->TextColor(ui_token::color::TEXT_TIP);
			Ui()->DoLabel(&Row, pText, HintSize, TEXTALIGN_ML, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor()); }, Metrics.m_RowStep);

	CUIRect Toolbar;
	if(Flow.Control("bind-overview-toolbar", Metrics.m_ButtonHeight, Toolbar))
	{
		const float Scale = Metrics.m_UiScale;
		char aMultiLabel[96];
		str_format(aMultiLabel, sizeof(aMultiLabel), "%s  ·%d", Localize("Only multi-action"), State.m_MultiActionCount);
		CUIRect Filter, Sort, Copy, Conflicts;
		Toolbar.VSplitRight(96.0f * Scale, &Toolbar, &Copy);
		Toolbar.VSplitRight(4.0f * Scale, &Toolbar, nullptr);
		Toolbar.VSplitRight(130.0f * Scale, &Toolbar, &Sort);
		Toolbar.VSplitRight(4.0f * Scale, &Toolbar, nullptr);
		// 「只看多动作」胶囊排在排序按钮左边；卡片太窄时让位给搜索框，保证输入框至少留一行宽度。
		const float ConflictWidth = std::min(
			std::clamp(TextRender()->TextWidth(Metrics.m_SmallSize, aMultiLabel) + 22.0f * Scale, 104.0f * Scale, 200.0f * Scale),
			std::max(0.0f, Toolbar.w - 140.0f * Scale));
		if(ConflictWidth >= 48.0f * Scale)
		{
			Toolbar.VSplitRight(ConflictWidth, &Toolbar, &Conflicts);
			Toolbar.VSplitRight(4.0f * Scale, &Toolbar, nullptr);
			// 计数为 0 时置灰不可点：否则点开只剩一片空列表。
			const bool ConflictsAvailable = State.m_MultiActionCount > 0;
			ui_widget::SButtonSurfaceOptions ConflictOptions;
			ConflictOptions.m_Enabled = ConflictsAvailable;
			ConflictOptions.m_Selected = State.m_OnlyMultiAction;
			ConflictOptions.m_Radius = ui_token::radius::PILL;
			const ColorRGBA ConflictFill = ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), &State.m_MultiActionButton, Conflicts, ConflictOptions);
			{
				CUiScopedSurfaceText SurfaceText(TextRender(), ConflictFill);
				if(!ConflictsAvailable)
					TextRender()->TextColor(ui_token::color::TEXT_DISABLED);
				SLabelProperties ConflictProps;
				ConflictProps.m_MaxWidth = std::max(1.0f, Conflicts.w - 8.0f * Scale);
				ConflictProps.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&Conflicts, aMultiLabel, Metrics.m_SmallSize, TEXTALIGN_MC, ConflictProps);
				if(!ConflictsAvailable)
					TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			if(ConflictsAvailable && Enabled && Ui()->DoButtonLogic(&State.m_MultiActionButton, State.m_OnlyMultiAction ? 1 : 0, &Conflicts, BUTTONFLAG_LEFT) != 0)
			{
				State.m_OnlyMultiAction = !State.m_OnlyMultiAction;
				// 同一帧重算列表，胶囊与列表不滞后一帧。
				RefreshBindOverviewFilter(State);
			}
		}
		Filter = Toolbar;
		if(DoButton_Menu(&State.m_SortButton, State.m_SortByCommand ? Localize("Sort by command") : Localize("Sort by key"), 0, &Sort) && Enabled)
			State.m_SortByCommand = !State.m_SortByCommand;
		// 禁用态交给 DoButton_QmIcon 的 Enabled 形参：外观与命中一起关闭，不再用 0 旗标绕过。
		const bool CopyClicked = Ui()->DoButton_QmIcon(&State.m_CopyButton, EQmIcon::COPY, FontIcons::FONT_ICON_COPY, 0, &Copy, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Enabled) != 0;
		GameClient()->m_Tooltips.DoToolTip(&State.m_CopyButton, &Copy, Localize("Copy all"));
		if(CopyClicked && Enabled)
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
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));

	if(Client()->LocalTime() - State.m_CopiedTime < 2.0f)
	{
		CUIRect StatusRow;
		if(Flow.Control("bind-overview-copied", Metrics.m_LineHeight, StatusRow))
		{
			TextRender()->TextColor(ui_token::color::SUCCESS);
			Ui()->DoLabel(&StatusRow, Localize("Copied"), Metrics.m_SmallSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		Flow.Space(ui_token::settings::SubRowGap(Metrics.m_LineSpacing));
	}

	// 徽标列固定宽度：被标记与未被标记的行共用同一列宽，键名与命令两条竖线不因
	// 徽标出现而抖动；宽度按最长的组合文案量出来，窄卡片时整体收敛。
	std::string BadgeProbe = Localize("Multi");
	BadgeProbe += " · ";
	BadgeProbe += Localize("Same function");
	const float BadgeColumn = std::min(160.0f * Metrics.m_UiScale,
		std::max(56.0f * Metrics.m_UiScale, TextRender()->TextWidth(Metrics.m_SmallSize, BadgeProbe.c_str()) + 20.0f * Metrics.m_UiScale));
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
				Flow.Space(ui_token::settings::SubRowGap(Metrics.m_LineSpacing));
				GroupStarted = true;
			}
			CUIRect Row;
			char aRowId[64];
			str_format(aRowId, sizeof(aRowId), "bind-overview-entry-%d", EntryIndex++);
			if(Flow.Control(aRowId, Metrics.m_LineHeight, Row))
			{
				if(Enabled && EntryIndex < (int)State.m_aRowButtons.size())
				{
					// 整行都可点：点了把焦点交给键盘卡片的这个键，占用和键面从此连起来。
					if(Ui()->DoButtonLogic(&State.m_aRowButtons[EntryIndex], 0, &Row, BUTTONFLAG_LEFT) && Ui()->MouseHovered(&Row))
						FocusBindKey(Entry.m_Key, Entry.m_ModifierMask);
				}
				// 悬停反馈：整行可点却原来没有任何视觉回应，鼠标在上面像失灵。
				if(Enabled && Ui()->MouseHovered(&Row))
					DrawRoundedSurface(Ui(), Row, ui_token::color::TINT_ROW_HOVER, ColorRGBA(), ui_token::radius::TIGHT);
				const bool Flagged = Entry.m_Flags != qm_bind_workshop::OCCUPANCY_NONE;
				// 被标记的行只加一档很淡的警告底色：正文与描边保持中性，颜色不抢命令文本。
				if(Flagged)
					DrawRoundedSurface(Ui(), Row, ui_token::color::WARNING.WithAlpha(0.08f), ColorRGBA(), ui_token::radius::TIGHT);
				CUIRect Columns = Row, Badge;
				Columns.VSplitRight(BadgeColumn, &Columns, &Badge);
				CUIRect KeyRect, CommandRect;
				Columns.VSplitLeft(std::min(Columns.w * 0.4f, 160.0f * Metrics.m_UiScale), &KeyRect, &CommandRect);
				Ui()->DoLabel(&KeyRect, Entry.m_KeyName.c_str(), Metrics.m_BodySize, TEXTALIGN_ML);
				SLabelProperties Props;
				Props.m_MaxWidth = std::max(1.0f, CommandRect.w);
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&CommandRect, Entry.m_Command.c_str(), Metrics.m_BodySize, TEXTALIGN_ML, Props);
				const std::string FlagTags = BindFlagTags(Entry.m_Flags);
				if(!FlagTags.empty())
				{
					const float BadgeTextWidth = TextRender()->TextWidth(Metrics.m_SmallSize, FlagTags.c_str());
					const float BadgeWidth = std::min(Badge.w, BadgeTextWidth + 12.0f * Metrics.m_UiScale);
					const float BadgeHeight = std::min(Badge.h, Metrics.m_BadgeHeight);
					CUIRect BadgeRect{Badge.x + Badge.w - BadgeWidth, Badge.y + (Badge.h - BadgeHeight) * 0.5f, BadgeWidth, BadgeHeight};
					DrawRoundedSurface(Ui(), BadgeRect, ui_token::color::WARNING.WithAlpha(0.12f), ui_token::color::WARNING.WithAlpha(0.10f), ui_token::radius::PILL, 1.0f);
					SLabelProperties BadgeProps;
					BadgeProps.m_MaxWidth = std::max(1.0f, BadgeRect.w - 6.0f * Metrics.m_UiScale);
					BadgeProps.m_EllipsisAtEnd = true;
					TextRender()->TextColor(ui_token::color::WARNING);
					Ui()->DoLabel(&BadgeRect, FlagTags.c_str(), Metrics.m_SmallSize, TEXTALIGN_MC, BadgeProps);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			Flow.Space(ui_token::settings::SubRowGap(Metrics.m_LineSpacing));
			HasAny = true;
		}
		// 组与组之间仍是列表内的一档间距：与上一行的子行档叠加后正好一行行距，
		// 不提升到分区分档（那会是行距的 5 倍，把整张一览表拉散）。
		if(GroupStarted)
			Flow.Space(ui_token::settings::SubRowGap(Metrics.m_LineSpacing));
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
