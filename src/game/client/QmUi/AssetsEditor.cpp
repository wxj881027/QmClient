#include <generated/client_data.h>

#include <game/client/QmUi/AssetsEditorLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTag.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

using namespace FontIcons;

const char *CMenus::AssetsEditorTypeName(int Type)
{
	switch(Type)
	{
	case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return "emoticons";
	case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return "entities";
	case CMenus::ASSETS_EDITOR_TYPE_SKIN: return "skin";
	case CMenus::ASSETS_EDITOR_TYPE_HUD: return "hud";
	case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return "particles";
	case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR: return "gui_cursor";
	case CMenus::ASSETS_EDITOR_TYPE_ARROW: return "arrow";
	case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK: return "strong_weak";
	case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return "extras";
	default: return "game";
	}
}

namespace
{
	static const char *AssetsEditorTypeDisplayName(int Type)
	{
		switch(Type)
		{
		case CMenus::ASSETS_EDITOR_TYPE_EMOTICONS: return Localize("Emoticons");
		case CMenus::ASSETS_EDITOR_TYPE_ENTITIES: return Localize("Entities");
		case CMenus::ASSETS_EDITOR_TYPE_SKIN: return Localize("Skin");
		case CMenus::ASSETS_EDITOR_TYPE_HUD: return Localize("HUD");
		case CMenus::ASSETS_EDITOR_TYPE_PARTICLES: return Localize("Particles");
		case CMenus::ASSETS_EDITOR_TYPE_GUI_CURSOR: return Localize("Mouse");
		case CMenus::ASSETS_EDITOR_TYPE_ARROW: return Localize("Direction Keys");
		case CMenus::ASSETS_EDITOR_TYPE_STRONG_WEAK: return Localize("Strong Weak Hook");
		case CMenus::ASSETS_EDITOR_TYPE_EXTRAS: return Localize("Extras");
		default: return Localize("Game");
		}
	}

	bool IconAction(const IUiContext &Ctx, CButtonContainer &Id, CUIRect Rect, EQmIcon Icon, const char *pGlyph, const char *pLabel)
	{
		Rect = QmUiSquareIconButtonRect(Rect);
		const bool Click = Ctx.m_pUi->DoButton_QmIcon(&Id, Icon, pGlyph, 0, &Rect, BUTTONFLAG_LEFT);
		Ctx.m_pTooltips->DoToolTipForRect(&Id, &Rect, pLabel);
		return Click;
	}

	void Label(CUi *pUi, CUIRect Rect, const char *pText, float Size, ColorRGBA Color, int Align = TEXTALIGN_ML)
	{
		SLabelProperties Props;
		Props.SetColor(Color);
		Props.m_MaxWidth = Rect.w;
		Props.m_DisallowNewline = true;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Rect, pText, Size, Align, Props);
	}
}

void CMenus::AssetsEditorRenderToolbar(CUIRect Tools, CUIRect Export, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics)
{
	auto &State = m_AssetsEditorState;
	const float Gap = Metrics.m_LineSpacing;
	CUIRect Type, Grid, Batch;
	Tools.VSplitLeft(Tools.w * 0.30f, &Type, &Tools);
	Tools.VSplitLeft(std::min(Gap, Tools.w), nullptr, &Tools);
	Tools.VSplitLeft(Tools.w * 0.38f, &Grid, &Batch);
	Batch.VMargin(std::min(Gap, Batch.w / 2), &Batch);
	const char *apTypes[ASSETS_EDITOR_TYPE_COUNT];
	for(int i = 0; i < ASSETS_EDITOR_TYPE_COUNT; ++i)
		apTypes[i] = AssetsEditorTypeDisplayName(i);
	static CUi::SDropDownState s_Types;
	static CScrollRegion s_TypeScroll;
	s_Types.m_SelectionPopupContext.m_pScrollRegion = &s_TypeScroll;
	const int TypeIndex = DoSettingsDropDown(&Type, State.m_Type, apTypes, ASSETS_EDITOR_TYPE_COUNT, s_Types);
	if(!Ui()->RenderOnly() && TypeIndex >= 0 && TypeIndex != State.m_Type)
		AssetsEditorRequestAction({EAssetsEditorAction::SWITCH_TYPE, TypeIndex});
	if(ui_widget::FilterChip(Ctx, &State.m_ShowGrid, Grid, Localize("Show Grid"), State.m_ShowGrid, true, Metrics.m_BodySize))
		State.m_ShowGrid = !State.m_ShowGrid;
	const char *pBatch = Localize("Same-size family");
	if(ui_widget::FilterChip(Ctx, &State.m_ApplySameSize, Batch, pBatch, State.m_ApplySameSize, true, Metrics.m_BodySize))
		State.m_ApplySameSize = !State.m_ApplySameSize;
	Ctx.m_pTooltips->DoToolTipForRect(&State.m_ApplySameSize, &Batch, Localize("Replace same-size parts in the target family."));
	const auto Actions = ResolveSettingsInputActionsLayout(Export, 1, Metrics, TextRender()->TextWidth(Metrics.m_BodySize, Localize("Export")) + 2 * ui_token::spacing::SM * Metrics.m_UiScale);
	static CLineInput s_Name;
	s_Name.SetBuffer(State.m_aExportName, sizeof(State.m_aExportName));
	char aPlaceholder[64];
	str_format(aPlaceholder, sizeof(aPlaceholder), Localize("my_%s"), AssetsEditorTypeName(State.m_Type));
	ui_widget::SInputFieldOptions Options;
	Options.m_Clearable = true;
	Options.m_FontSize = Metrics.m_BodySize;
	Options.m_pPlaceholder = aPlaceholder;
	if(ui_widget::InputField(Ctx, &s_Name, Actions.m_Input, Options).m_Changed)
		AssetsEditorCommitExportNameForType();
	static CButtonContainer s_Export;
	if(DoSettingsButton_Menu(SETTINGS_ASSETS, -1, -1, &s_Export, "assets-editor-export", Localize("Export"), 0, &Actions.m_Actions.m_aButtons[0], Metrics))
		AssetsEditorExport();
}

void CMenus::AssetsEditorRenderSelector(CUIRect Search, CUIRect Selector, bool Target, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics)
{
	auto &State = m_AssetsEditorState;
	const int Type = State.m_Type;
	const auto &vAssets = State.m_avAssets[Type];
	int &Current = Target ? State.m_aMainAssetIndex[Type] : State.m_aDonorAssetIndex[Type];
	static CLineInputBuffered<64> s_aaSearch[2][ASSETS_EDITOR_TYPE_COUNT];
	auto &Input = s_aaSearch[Target][Type];
	ui_widget::SInputFieldOptions Options;
	Options.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	Options.m_Clearable = true;
	Options.m_SearchHotkeyEnabled = false;
	Options.m_FontSize = Metrics.m_BodySize;
	Options.m_pPlaceholder = Target ? Localize("Search main asset") : Localize("Search donor asset");
	ui_widget::InputField(Ctx, &Input, Search, Options);
	std::vector<int> vIndices;
	std::vector<const char *> vNames;
	for(size_t i = 0; i < vAssets.size(); ++i)
	{
		if(!Input.IsEmpty() && !str_utf8_find_nocase(vAssets[i].m_aName, Input.GetString()))
			continue;
		vIndices.push_back(i);
		vNames.push_back(vAssets[i].m_aName);
	}
	if(vNames.empty())
	{
		Label(Ui(), Selector, Localize("No matching assets."), Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		return;
	}
	const int Selection = qmclient::assets_editor::FilteredMainAssetSelection(vIndices, Current, Target && State.m_HasUnsavedChanges);
	static CUi::SDropDownState s_aaDropdown[2][ASSETS_EDITOR_TYPE_COUNT];
	static CScrollRegion s_aaScroll[2][ASSETS_EDITOR_TYPE_COUNT];
	auto &Dropdown = s_aaDropdown[Target][Type];
	Dropdown.m_SelectionPopupContext.m_pScrollRegion = &s_aaScroll[Target][Type];
	const int Result = DoSettingsDropDown(&Selector, Selection, vNames.data(), vNames.size(), Dropdown);
	if(!Ui()->RenderOnly() && Result >= 0 && Result < (int)vIndices.size() && Current != vIndices[Result])
	{
		if(Target)
			AssetsEditorRequestAction({EAssetsEditorAction::SWITCH_MAIN_ASSET, Type, vAssets[vIndices[Result]].m_aName});
		else
		{
			Current = vIndices[Result];
			AssetsEditorCancelDrag();
		}
	}
}

void CMenus::AssetsEditorRenderInspector(CUIRect Rect, bool Wrap, const IUiContext &Ctx, const SSettingsContentMetrics &Metrics)
{
	auto &State = m_AssetsEditorState;
	const int Index = State.m_SelectedTargetSlotIndex;
	if(Index < 0 || Index >= (int)State.m_vPartSlots.size())
	{
		const char *pHelp = Localize("Click to select, double-click to tint. Drag to replace a part. Right-click to reset.");
		Label(Ui(), Rect, pHelp, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		Ctx.m_pTooltips->DoToolTipForRect(&State.m_SelectedTargetSlotIndex, &Rect, pHelp);
		return;
	}
	auto &Slot = State.m_vPartSlots[Index];
	CUIRect Identity, Controls;
	if(Wrap)
	{
		Rect.HSplitTop(Metrics.m_LineHeight, &Identity, &Controls);
		Controls.HSplitBottom(std::min(Controls.h, Metrics.m_LineHeight), nullptr, &Controls);
	}
	else
		Rect.VSplitLeft(Rect.w * 0.32f, &Identity, &Controls);
	const auto IdentityRow = ResolveSettingsInputActionsLayout(Identity, 1, Metrics, Metrics.m_LineHeight);
	CUIRect Name = IdentityRow.m_Input, Source;
	const float BadgeWidth = std::min(Name.w * 0.48f, ui_tag::MeasureWidth(TextRender(), Slot.m_aSourceAsset, Metrics.m_UiScale));
	Name.VSplitRight(BadgeWidth, &Name, &Source);
	ui_tag::Draw(Ui(), Source, Slot.m_aSourceAsset, ui_tag::EColorMode::DERIVED, Ui()->QmControlTheme().m_Selected, nullptr, 1.0f, Metrics.m_UiScale);
	const char *pPart = Slot.m_SpriteId >= 0 ? g_pData->m_aSprites[Slot.m_SpriteId].m_pName : Slot.m_aFamilyKey;
	char aName[128];
	str_format(aName, sizeof(aName), "#%d · %s", Index + 1, pPart);
	Label(Ui(), Name, aName, Metrics.m_SmallSize, ResolveConfiguredTextColor());
	Ctx.m_pTooltips->DoToolTipForRect(&Slot.m_SpriteId, &Name, aName);
	Ctx.m_pTooltips->DoToolTipForRect(Slot.m_aSourceAsset, &Source, Slot.m_aSourceAsset);
	const CUIRect Swatch = QmUiSquareIconButtonRect(IdentityRow.m_Actions.m_aButtons[0]);
	static CButtonContainer s_Color;
	DrawRoundedSurface(Ctx, Swatch, AssetsEditorSlotColorToRgba(Slot.m_Color), ui_token::feedback::ACTIVE_BORDER, ui_token::radius::PILL, ui_token::assets_editor::BORDER_WIDTH);
	if(Ui()->DoButtonLogic(&s_Color, 0, &Swatch, BUTTONFLAG_LEFT))
		AssetsEditorOpenColorPicker(Index);
	Ctx.m_pTooltips->DoToolTipForRect(&s_Color, &Swatch, Localize("Color"));
	const auto Row = ResolveSettingsInputActionsLayout(Controls, 1, Metrics, Metrics.m_LineHeight);
	CUIRect Blend, Strength;
	Row.m_Input.VSplitLeft(Row.m_Input.w * 0.35f, &Blend, &Strength);
	static constexpr int s_aBlendModes[] = {
		ASSETS_EDITOR_COLOR_BLEND_NORMAL,
		ASSETS_EDITOR_COLOR_BLEND_DARKEN,
		ASSETS_EDITOR_COLOR_BLEND_MULTIPLY,
		ASSETS_EDITOR_COLOR_BLEND_COLOR_BURN,
		ASSETS_EDITOR_COLOR_BLEND_LIGHTEN,
		ASSETS_EDITOR_COLOR_BLEND_SCREEN,
		ASSETS_EDITOR_COLOR_BLEND_COLOR_DODGE,
		ASSETS_EDITOR_COLOR_BLEND_OVERLAY,
		ASSETS_EDITOR_COLOR_BLEND_SOFT_LIGHT,
		ASSETS_EDITOR_COLOR_BLEND_HARD_LIGHT,
		ASSETS_EDITOR_COLOR_BLEND_DIFFERENCE,
		ASSETS_EDITOR_COLOR_BLEND_EXCLUSION,
		ASSETS_EDITOR_COLOR_BLEND_HUE,
		ASSETS_EDITOR_COLOR_BLEND_SATURATION,
		ASSETS_EDITOR_COLOR_BLEND_COLOR,
		ASSETS_EDITOR_COLOR_BLEND_LUMINOSITY,
	};
	static_assert(std::size(s_aBlendModes) == ASSETS_EDITOR_COLOR_BLEND_COUNT);

	const char *apNames[ASSETS_EDITOR_COLOR_BLEND_COUNT];
	int Current = 0;
	for(int i = 0; i < ASSETS_EDITOR_COLOR_BLEND_COUNT; ++i)
	{
		apNames[i] = Localize(AssetsEditorColorBlendModeName(s_aBlendModes[i]), "Assets editor blend mode");
		if(s_aBlendModes[i] == ClampAssetsEditorColorBlendMode(Slot.m_ColorBlendMode))
			Current = i;
	}
	static CUi::SDropDownState s_Mode;
	static CScrollRegion s_Scroll;
	s_Mode.m_SelectionPopupContext.m_pScrollRegion = &s_Scroll;
	const int NewMode = DoSettingsDropDown(&Blend, Current, apNames, ASSETS_EDITOR_COLOR_BLEND_COUNT, s_Mode);
	Ctx.m_pTooltips->DoToolTipForRect(&s_Mode.m_ButtonContainer, &Blend, Localize("Blend mode"));
	static ui_widget::SNumericFieldState s_Strength;
	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = Localize("Blend strength");
	Options.m_pSuffix = "%";
	Options.m_FontSize = Metrics.m_SmallSize;
	Options.m_LineSpacing = Metrics.m_LineSpacing;
	const bool Changed = ui_widget::NumericField(Ctx, &s_Strength, &Slot.m_BlendStrength, &Slot.m_BlendStrength, 0, 100, Strength, Options);
	if(!Ui()->RenderOnly() && (Changed || (NewMode >= 0 && NewMode != Current)))
	{
		if(NewMode >= 0 && NewMode < ASSETS_EDITOR_COLOR_BLEND_COUNT)
			Slot.m_ColorBlendMode = s_aBlendModes[NewMode];
		State.m_DirtyPreview = true;
		State.m_HasUnsavedChanges = true;
	}
	static CButtonContainer s_Reset;
	if(IconAction(Ctx, s_Reset, Row.m_Actions.m_aButtons[0], EQmIcon::ARROW_ROTATE_LEFT, FONT_ICON_ARROW_ROTATE_LEFT, Localize("Reset")))
		AssetsEditorResetSlot(Index);
}

namespace
{
	struct SExitDialog : SPopupMenuId
	{
		SSettingsContentMetrics m_Metrics;
		bool m_Accepted = false;
	};
	SExitDialog gs_ExitDialog;
}

void CMenus::AssetsEditorRenderExitConfirm(const CUIRect &Rect)
{
	if(Ui()->RenderOnly())
		return;
	if(gs_ExitDialog.m_Accepted)
	{
		// 弹窗关闭后在设置页执行，离页或重启也在这一趟完成。
		gs_ExitDialog.m_Accepted = false;
		AssetsEditorExecutePendingAction();
		return;
	}
	if(Ui()->IsPopupOpen(&gs_ExitDialog))
		return;

	gs_ExitDialog.m_Metrics = ResolveSettingsContentMetrics(Rect.w);
	const auto &Metrics = gs_ExitDialog.m_Metrics;
	const float Scale = Metrics.m_UiScale;
	const float Padding = ui_token::panel::PADDING * Scale;
	const float Width = std::min(Rect.w, ui_token::confirm_dialog::MAX_WIDTH * Scale);
	const float Height = std::min(Rect.h, 3 * Padding + (ui_token::confirm_dialog::TITLE_HEIGHT + ui_token::confirm_dialog::BUTTON_HEIGHT) * Scale + Metrics.m_LineHeight);
	SPopupMenuProperties Props;
	Props.m_CenterInViewport = true;
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_BlockUnderlyingScroll = true;
	Props.m_IgnoreOpeningClick = true;
	Props.m_ContentInset = std::min({Padding, Width / 2, Height / 2});
	Props.m_OnClosed = [this]() {
		if(!gs_ExitDialog.m_Accepted)
			m_AssetsEditorState.m_PendingAction.reset();
	};
	Ui()->DoPopupMenu(&gs_ExitDialog, Rect.x, Rect.y, Width, Height, this, [](void *pContext, CUIRect Box, bool Active) {
		auto *pMenus = static_cast<CMenus *>(pContext);
		auto &State = pMenus->m_AssetsEditorState;
		const auto &M = gs_ExitDialog.m_Metrics;
		CUIRect Title, Message, Buttons;
		Box.HSplitTop(std::min(Box.h, ui_token::confirm_dialog::TITLE_HEIGHT * M.m_UiScale), &Title, &Box);
		Box.HSplitBottom(std::min(Box.h, ui_token::confirm_dialog::BUTTON_HEIGHT * M.m_UiScale), &Message, &Buttons);
		Label(pMenus->Ui(), Title, Localize("Save asset before closing?"), M.m_HeadlineSize, ResolveConfiguredTextColor());
		Label(pMenus->Ui(), Message, State.m_StatusIsError ? State.m_aStatusMessage : Localize("You have unsaved changes."), M.m_BodySize, State.m_StatusIsError ? ui_token::color::DANGER : ui_token::color::TEXT_SECONDARY);
		static ui_widget::SSettingsActionGroupState s_Group;
		static CButtonContainer s_aActions[3];
		const char *apLabels[] = {Localize("Save"), Localize("Discard"), Localize("Cancel")};
		const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_Group, Buttons, apLabels, 3, M);
		const char *apIds[] = {"assets-editor-save-close", "assets-editor-discard", "assets-editor-cancel"};
		for(int i = 0; i < 3; ++i)
		{
			if(pMenus->DoSettingsButton_Menu(SETTINGS_ASSETS, -1, -1, &s_aActions[i], apIds[i], apLabels[i], 0, &Actions.m_aButtons[i], M) && Active)
			{
				if(i == 0 && !pMenus->AssetsEditorExport())
					return CUi::POPUP_KEEP_OPEN;
				gs_ExitDialog.m_Accepted = i != 2;
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
		return CUi::POPUP_KEEP_OPEN; }, Props);
}

void CMenus::RenderAssetsEditorScreen(CUIRect MainView)
{
	auto &State = m_AssetsEditorState;
	if(State.m_FullscreenOpen)
		MainView = *Ui()->Screen();
	if(MainView.w <= 0 || MainView.h <= 0 || m_MenuTextPlanCollecting)
		return;
	const bool Interactive = !Ui()->RenderOnly();
	const bool Initialize = Interactive && !State.m_Initialized;
	if(Initialize)
	{
		gs_ExitDialog.m_Accepted = false;
		AssetsEditorEnsureAssetsLoadedForType(State.m_Type);
		AssetsEditorResetPartSlots();
		AssetsEditorSyncExportNameFromType();
		State.m_Initialized = true;
	}
	const bool HadPopup = Ui()->IsPopupOpen();
	const bool WasConfirming = State.m_PendingAction.has_value();
	if(Interactive && !WasConfirming && !HadPopup && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		AssetsEditorRequestAction({EAssetsEditorAction::CLOSE});
	if(!State.m_Open)
		return;
	const CUiScopedInteractionOwner Owner(Ui(), &State, "assets_editor");
	if(Interactive)
		Ui()->RefreshInteractionSource(&State, "assets_editor");
	const auto Metrics = ResolveSettingsContentMetrics(MainView.w);
	const auto Scale = Metrics.m_UiScale;
	if(Interactive && State.m_ColorPickerSlotIndex >= 0)
	{
		const int Index = State.m_ColorPickerSlotIndex;
		if(Index < (int)State.m_vPartSlots.size() && m_ColorPickerPopupContext.m_pHslaColor == &State.m_ColorPickerValue)
		{
			const unsigned Value = ColorHSLA(State.m_ColorPickerValue).Pack(true);
			if(State.m_vPartSlots[Index].m_Color != Value)
			{
				State.m_vPartSlots[Index].m_Color = Value;
				State.m_DirtyPreview = State.m_HasUnsavedChanges = true;
				str_copy(State.m_aStatusMessage, Localize("Part color updated."));
				State.m_StatusIsError = false;
			}
		}
		if(!Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
			State.m_ColorPickerSlotIndex = -1;
	}
	DrawRoundedSurface(Ui(), MainView, ResolveConfiguredSecondaryPanelTheme().m_Surface, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD * Scale, ui_token::assets_editor::BORDER_WIDTH);
	auto RenderWorkbench = [&] {
		auto Ctx = SettingsUiContext("assets_editor", Scale);
		if(Ui()->RenderOnly())
		{
			Ctx.m_pAnim = nullptr;
			Ctx.m_pTree = nullptr;
		}
		auto LayoutFor = [&](CUIRect View) {
			return qmclient::assets_editor::ResolveWorkbench(View, Scale, Metrics.m_LineHeight);
		};
		CUIRect Content = MainView;
		auto Layout = LayoutFor(Content);
		const bool Scrollable = Layout.m_ContentHeight > Content.h;
		static CScrollRegion s_Scroll;
		if(Initialize)
			s_Scroll.Reset();
		if(Scrollable)
		{
			auto Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, Scale);
			vec2 Offset;
			s_Scroll.Begin(&Content, &Offset, &Params);
			Content.y += Offset.y;
			Layout = LayoutFor(Content);
			Content.h = Layout.m_ContentHeight;
			s_Scroll.AddRect(Content);
		}
		const auto Header = ResolveSettingsInputActionsLayout(Layout.m_Header, 2, Metrics, Metrics.m_LineHeight);
		ui_widget::PageTitle(Ctx, Header.m_Input, Localize("Assets editor"), EQmIcon::IMAGE, FONT_ICON_IMAGE);
		static CButtonContainer s_Reload, s_Close, s_ResetAll;
		if(IconAction(Ctx, s_Reload, Header.m_Actions.m_aButtons[0], EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, Localize("Reload")))
			AssetsEditorReloadAssetsImagesOnly();
		if(IconAction(Ctx, s_Close, Header.m_Actions.m_aButtons[1], EQmIcon::CLOSE, FONT_ICON_XMARK, Localize("Close")))
		{
			AssetsEditorRequestAction({EAssetsEditorAction::CLOSE});
			if(Scrollable)
				s_Scroll.End();
			return;
		}
		AssetsEditorRenderToolbar(Layout.m_Tools, Layout.m_Export, Ctx, Metrics);
		for(const auto &Panel : {Layout.m_Donor.m_Panel, Layout.m_Target.m_Panel, Layout.m_Inspector})
			DrawRoundedSurface(Ctx, Panel, ui_token::color::PREVIEW_BACKDROP, ui_token::color::BORDER_SUBTLE, ui_token::radius::MENU * Scale, ui_token::assets_editor::BORDER_WIDTH);
		Label(Ui(), Layout.m_Donor.m_Title, Localize("Donor Asset"), Metrics.m_BodySize, ResolveConfiguredTextColor());
		Ctx.m_pTooltips->DoToolTipForRect(&State.m_aDonorAssetIndex, &Layout.m_Donor.m_Title, Localize("Donor (drag parts from left)"));
		const auto Title = ResolveSettingsInputActionsLayout(Layout.m_Target.m_Title, 1, Metrics, Metrics.m_LineHeight);
		Label(Ui(), Title.m_Input, Localize("Main Asset"), Metrics.m_BodySize, ResolveConfiguredTextColor());
		if(IconAction(Ctx, s_ResetAll, Title.m_Actions.m_aButtons[0], EQmIcon::ARROW_ROTATE_LEFT, FONT_ICON_ARROW_ROTATE_LEFT, Localize("Reset All")))
		{
			if(AssetsEditorHasSlotChanges())
				AssetsEditorResetPartSlots();
			State.m_HasUnsavedChanges = false;
		}
		AssetsEditorRenderSelector(Layout.m_Donor.m_Search, Layout.m_Donor.m_Selector, false, Ctx, Metrics);
		AssetsEditorRenderSelector(Layout.m_Target.m_Search, Layout.m_Target.m_Selector, true, Ctx, Metrics);
		CUIRect Inspector = Layout.m_Inspector;
		Inspector.Margin(std::min(Inspector.w / 2, ui_token::settings::CARD_PADDING * Scale), &Inspector);
		AssetsEditorRenderInspector(Inspector, Layout.m_WrapInspector, Ctx, Metrics);
		AssetsEditorRenderCanvases(Layout.m_Donor.m_Canvas, Layout.m_Target.m_Canvas, MainView, HadPopup || Ui()->IsPopupOpen() || State.m_PendingAction, Metrics);
		CUIRect Status, Size;
		Layout.m_Status.VSplitRight(std::min(Layout.m_Status.w / 3, Metrics.m_LineHeight * 6), &Status, &Size);
		Label(Ui(), Status, State.m_aStatusMessage, Metrics.m_SmallSize, State.m_StatusIsError ? ui_token::color::DANGER : ui_token::color::SUCCESS);
		Ctx.m_pTooltips->DoToolTipForRect(State.m_aStatusMessage, &Status, State.m_aStatusMessage);
		if(State.m_ComposedPreviewWidth > 0)
		{
			char aSize[48];
			str_format(aSize, sizeof(aSize), "%d × %d px", State.m_ComposedPreviewWidth, State.m_ComposedPreviewHeight);
			Label(Ui(), Size, aSize, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY, TEXTALIGN_MR);
		}
		if(Scrollable)
			s_Scroll.End();
	};
	if(State.m_PendingAction)
	{
		const CUiScopedBackgroundRender Background(Ui());
		RenderWorkbench();
	}
	else
		RenderWorkbench();
	if(State.m_PendingAction)
		AssetsEditorRenderExitConfirm(MainView);
}
