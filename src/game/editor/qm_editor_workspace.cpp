#include "editor.h"

#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/qm_icon_manager.h>

using namespace FontIcons;

void CEditor::DoMapTabs(CUIRect MapTabs)
{
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 6.0f;
	ScrollParams.m_ScrollbarMargin = 2.0f;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_ScrollUnit = 140.0f;
	ScrollParams.m_ScrollHorizontal = true;
	vec2 ScrollOffset(0.0f, 0.0f);
	m_MapTabsScrollRegion.Begin(&MapTabs, &ScrollOffset, &ScrollParams);
	MapTabs.x += ScrollOffset.x;

	std::optional<size_t> CloseIndex;
	for(size_t Index = 0; Index < m_vpMaps.size(); ++Index)
	{
		CEditorMap &Map = *m_vpMaps[Index];
		const float ButtonWidth = std::clamp(TextRender()->TextWidth(10.0f, Map.m_aDisplayName) + 18.0f, 90.0f, 180.0f);
		CUIRect Tab;
		MapTabs.VSplitLeft(ButtonWidth + 18.0f, &Tab, &MapTabs);
		MapTabs.VSplitLeft(2.0f, nullptr, &MapTabs);
		if(!m_MapTabsScrollRegion.AddRect(Tab, m_MapTabsRevealSelected && m_SelectedMap == Index))
			continue;

		CUIRect CloseButton;
		Tab.VSplitRight(18.0f, &Tab, &CloseButton);
		const bool Saving = IsSaving(&Map);
		char aTooltip[256];
		str_format(aTooltip, sizeof(aTooltip), "Select map '%s'.", Map.m_aFilename[0] == '\0' ? "unnamed" : Map.m_aFilename);
		const int TabResult = DoButton_Ex(&Map.m_TabSelectButtonId, Map.m_aDisplayName, m_SelectedMap == Index ? 1 : 0, &Tab, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT | (Saving ? 0 : (int)BUTTONFLAG_MIDDLE), aTooltip, IGraphics::CORNER_L);
		int CloseResult;
		if(Saving)
		{
			CloseResult = DoButton_Ex(&Map.m_TabCloseButtonId, "", 0, &CloseButton, BUTTONFLAG_RIGHT, "This map is being saved.", IGraphics::CORNER_R);
			Ui()->RenderProgressSpinner(CloseButton.Center(), 4.0f);
		}
		else
		{
			const bool ShowCloseIcon = !Map.m_Modified || Ui()->HotItem() == &Map.m_TabCloseButtonId;
			CloseResult = DoButton_QmIcon(&Map.m_TabCloseButtonId, ShowCloseIcon ? EQmIcon::CLOSE : EQmIcon::CIRCLE, ShowCloseIcon ? FONT_ICON_XMARK : FONT_ICON_CIRCLE, 0, &CloseButton, BUTTONFLAG_ALL, Map.m_Modified ? "Close the selected map. This map has unsaved changes." : "Close the selected map.", IGraphics::CORNER_R, ShowCloseIcon ? 9.0f : 6.0f);
		}
		if(m_SelectedMap == Index)
		{
			CUIRect Accent{Tab.x, Tab.y + Tab.h - 1.5f, Tab.w + CloseButton.w, 1.5f};
			Accent.Draw(QmEditorTheme::ACCENT, IGraphics::CORNER_NONE, 0.0f);
		}
		if(TabResult == 1)
		{
			m_SelectedMap = Index;
			m_MapTabsScrollRegion.ScrollHere();
			Reset(false);
		}
		else if(TabResult == 2 || CloseResult == 2)
		{
			m_PopupMapTab.m_pEditor = this;
			m_PopupMapTab.m_pSelectedMap = &Map;
			Ui()->DoPopupMenu(&m_PopupMapTab, Ui()->MouseX(), Ui()->MouseY(), 150.0f, 80.0f, &m_PopupMapTab, CPopupMapTab::Render);
		}
		else if(TabResult == 3 || CloseResult == 1 || CloseResult == 3)
		{
			CloseIndex = Index;
		}
	}

	m_MapTabsRevealSelected = false;
	m_MapTabsScrollRegion.End();
	if(CloseIndex.has_value())
		CloseMap(CloseIndex.value(), true);
}

void CEditor::HandleMapTabShortcuts()
{
	if(m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && !Ui()->IsPopupOpen() && Ui()->CheckActiveItem(nullptr))
	{
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_F4))
		{
			CloseMap(m_SelectedMap, true);
			return;
		}
		if(m_vpMaps.size() > 1 && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_TAB))
		{
			m_SelectedMap = Input()->ShiftIsPressed() ? (m_SelectedMap == 0 ? m_vpMaps.size() - 1 : m_SelectedMap - 1) : (m_SelectedMap + 1) % m_vpMaps.size();
			m_MapTabsRevealSelected = true;
			Reset(false);
		}
	}
}

void CEditor::RenderWorkspaceToolbar()
{
	CUIRect ToolBar = m_Workspace.m_Toolbar;
	if(!m_GuiActive)
	{
		ToolBar = {-1400.0f, -100.0f, 1200.0f, 44.0f};
	}
	else
	{
		ToolBar.Margin(4.0f, &ToolBar);
		CScrollRegionParams Params;
		Params.m_ScrollHorizontal = true;
		Params.m_ScrollbarThickness = 6.0f;
		Params.m_ScrollbarMargin = 0.0f;
		Params.m_ScrollbarNoOuterMargin = true;
		Params.m_ScrollUnit = 100.0f;
		vec2 Offset{};
		m_ToolbarScrollRegion.Begin(&ToolBar, &Offset, &Params);
		ToolBar.x += Offset.x;
		ToolBar.w = std::max(ToolBar.w, 1020.0f);
		m_ToolbarScrollRegion.AddRect(ToolBar);
	}

	if(m_Mode == MODE_LAYERS)
		DoToolbarLayers(ToolBar);
	else if(m_Mode == MODE_IMAGES)
		DoToolbarImages(ToolBar);
	else if(m_Mode == MODE_SOUNDS && m_Workspace.m_Inspector.w <= 0.0f)
		DoToolbarSounds(ToolBar);
	if(m_GuiActive)
		m_ToolbarScrollRegion.End();
}

void CEditor::RenderModebar(CUIRect View)
{
	const float ButtonWidth = View.w / NUM_MODES;
	const EQmIcon aIcons[] = {EQmIcon::LAYER_GROUP, EQmIcon::IMAGE, EQmIcon::MUSIC};
	const char *apFallbacks[] = {FONT_ICON_LAYER_GROUP, FONT_ICON_IMAGE, FONT_ICON_MUSIC};
	const char *apTooltips[] = {
		Localize("Go to layers management.", "Editor"),
		Localize("Go to images management.", "Editor"),
		Localize("Go to sounds management.", "Editor")};
	static int s_aModeButtons[NUM_MODES];
	for(int Mode = 0; Mode < NUM_MODES; ++Mode)
	{
		CUIRect Button;
		View.VSplitLeft(ButtonWidth, &Button, &View);
		Button.VMargin(1.0f, &Button);
		if(DoButton_QmIcon(&s_aModeButtons[Mode], aIcons[Mode], apFallbacks[Mode], m_Mode == Mode,
			   &Button, BUTTONFLAG_LEFT, apTooltips[Mode], IGraphics::CORNER_ALL, 12.0f))
		{
			m_Mode = Mode;
		}
	}
}

void CEditor::RenderStatusbar(CUIRect View, CUIRect *pTooltipRect)
{
	CQuickAction *apActions[] = {&m_QuickActionEnvelopes, &m_QuickActionHistory, &m_QuickActionServerSettings};
	const float ButtonWidth = std::min(100.0f, View.w / 5.0f);
	for(CQuickAction *pAction : apActions)
	{
		CUIRect Button;
		View.VSplitLeft(ButtonWidth, &Button, &View);
		View.VSplitLeft(4.0f, nullptr, &View);
		if(DoButton_Editor(pAction, pAction->Label(), pAction->Color(), &Button, BUTTONFLAG_LEFT, pAction->Description()))
			pAction->Call();
	}
	View.VMargin(4.0f, pTooltipRect);
}

void CEditor::RenderTooltip(CUIRect TooltipRect)
{
	char aBuf[512];
	SLabelProperties Props;
	Props.m_MaxWidth = TooltipRect.w;
	Props.m_EllipsisAtEnd = true;
	Props.SetColor(QmEditorTheme::TEXT_MUTED);
	if(m_aTooltip[0] != '\0')
	{
		if(m_pUiGotContext && m_pUiGotContext == Ui()->HotItem())
			str_format(aBuf, sizeof(aBuf), Localize("%s Right click for context menu.", "Editor"), m_aTooltip);
		else
			str_copy(aBuf, m_aTooltip);
		Props.SetColor(QmEditorTheme::TEXT);
	}
	else if(m_Mentions > 0)
	{
		str_format(aBuf, sizeof(aBuf), Localize("%d new mentions", "Editor"), m_Mentions);
		Props.SetColor(QmEditorTheme::WARNING);
	}
	else if(m_IngameMoved)
	{
		str_copy(aBuf, Localize("Moved ingame", "Editor"));
		Props.SetColor(QmEditorTheme::WARNING);
	}
	else
	{
		char aTime[6];
		str_timestamp_format(aTime, sizeof(aTime), "%H:%M");
		str_format(aBuf, sizeof(aBuf), Localize("X: %.1f, Y: %.1f, Z: %.1f, T: %.1f, A: %.1f, G: %i  %s", "Editor"),
			MapView()->MouseWorldPos().x / 32.0f, MapView()->MouseWorldPos().y / 32.0f,
			MapView()->Zoom()->GetValue(), Map()->m_EnvelopeEvaluator.m_AnimateTime * Map()->m_EnvelopeEvaluator.m_AnimateSpeed,
			Map()->m_EnvelopeEvaluator.m_AnimateSpeed, MapView()->MapGrid()->Factor(), aTime);
	}
	Ui()->DoLabel(&TooltipRect, aBuf, 10.0f, TEXTALIGN_ML, Props);
}

void CEditor::RenderMenubar(CUIRect MenuBar)
{
	CUIRect Brand, Help, Close, Inspector;
	MenuBar.VSplitLeft(34.0f, &Brand, &MenuBar);
	SLabelProperties BrandProps;
	BrandProps.SetColor(QmEditorTheme::ACCENT);
	Ui()->DoLabel(&Brand, "Qm", 13.0f, TEXTALIGN_MC, BrandProps);
	MenuBar.VSplitRight(20.0f, &MenuBar, &Close);
	MenuBar.VSplitRight(3.0f, &MenuBar, nullptr);
	MenuBar.VSplitRight(20.0f, &MenuBar, &Help);
	MenuBar.VSplitRight(3.0f, &MenuBar, nullptr);
	MenuBar.VSplitRight(20.0f, &MenuBar, &Inspector);
	MenuBar.VSplitRight(8.0f, &MenuBar, nullptr);

	struct SMenu
	{
		const char *m_pLabel;
		float m_Width;
		float m_PopupWidth;
		float m_PopupHeight;
		CUi::FPopupMenuFunction m_pfnRender;
	};
	const SMenu aMenus[] = {
		{Localize("File", "Editor"), 48.0f, 120.0f, 188.0f, PopupMenuFile},
		{Localize("Tools", "Editor"), 52.0f, 200.0f, 78.0f, PopupMenuTools},
		{Localize("Settings", "Editor"), 60.0f, 280.0f, 148.0f, PopupMenuSettings},
		{Localize("Collaboration", "Editor"), 86.0f, 360.0f, 170.0f, PopupCollab}};
	static int s_aMenuButtons[std::size(aMenus)];
	static SPopupMenuId s_aPopupIds[std::size(aMenus)];
	for(size_t Index = 0; Index < std::size(aMenus); ++Index)
	{
		const SMenu &Menu = aMenus[Index];
		CUIRect Button;
		MenuBar.VSplitLeft(Menu.m_Width, &Button, &MenuBar);
		MenuBar.VSplitLeft(3.0f, nullptr, &MenuBar);
		const bool Selected = Ui()->IsPopupOpen(&s_aPopupIds[Index]) || (Index == 3 && m_CollabState == ECollabState::CONNECTED);
		if(DoButton_Ex(&s_aMenuButtons[Index], Menu.m_pLabel, Selected, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
		{
			Ui()->DoPopupMenu(&s_aPopupIds[Index], Button.x, Button.y + Button.h + 3.0f,
				Menu.m_PopupWidth, Menu.m_PopupHeight, this, Menu.m_pfnRender);
		}
	}

	if(Map()->m_Modified)
	{
		CUIRect Changed;
		MenuBar.VSplitLeft(12.0f, &Changed, &MenuBar);
		Ui()->DoLabel_QmIcon(&Changed, EQmIcon::CIRCLE, FONT_ICON_CIRCLE, 5.0f, TEXTALIGN_MC);
		static int s_ChangedIndicator;
		DoButtonLogic(&s_ChangedIndicator, 0, &Changed, BUTTONFLAG_NONE, Localize("This map has unsaved changes.", "Editor"));
	}
	MenuBar.VMargin(4.0f, &MenuBar);
	char aFilename[IO_MAX_PATH_LENGTH + 32];
	str_format(aFilename, sizeof(aFilename), Localize("File: %s", "Editor"), Map()->m_aFilename);
	SLabelProperties Props;
	Props.m_MaxWidth = std::max(0.0f, MenuBar.w);
	Props.m_EllipsisAtEnd = true;
	Props.SetColor(QmEditorTheme::TEXT_MUTED);
	if(MenuBar.w > 0.0f)
		Ui()->DoLabel(&MenuBar, aFilename, 10.0f, TEXTALIGN_ML, Props);

	static int s_InspectorButton;
	if(DoButton_QmIcon(&s_InspectorButton, EQmIcon::LIST_UL, FONT_ICON_LIST_UL, m_ShowInspector, &Inspector,
		   BUTTONFLAG_LEFT, Localize("Toggle properties panel.", "Editor"), IGraphics::CORNER_ALL))
		m_ShowInspector = !m_ShowInspector;
	static int s_HelpButton;
	if(DoButton_QmIcon(&s_HelpButton, EQmIcon::QUESTION, FONT_ICON_QUESTION, 0, &Help,
		   BUTTONFLAG_LEFT, Localize("[F1] Open the DDNet Wiki page for the map editor in a web browser.", "Editor"), IGraphics::CORNER_ALL))
		m_QuickActionShowHelp.Call();
	static int s_CloseButton;
	if(DoButton_QmIcon(&s_CloseButton, EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &Close,
		   BUTTONFLAG_LEFT, Localize("[Escape] Exit from the editor.", "Editor"), IGraphics::CORNER_ALL))
	{
		OnClose();
		g_Config.m_ClEditor = 0;
	}
}

void CEditor::Render()
{
	Graphics()->Clear(QmEditorTheme::WORKSPACE.r, QmEditorTheme::WORKSPACE.g, QmEditorTheme::WORKSPACE.b);
	const CUIRect Screen = *Ui()->Screen();
	const float Width = Screen.w;
	const float Height = Screen.h;
	Ui()->MapScreen();
	m_CursorType = CURSOR_NORMAL;
	str_copy(m_aTooltip, "");

	m_ShowPicker = m_Mode == MODE_LAYERS &&
		       m_Dialog == DIALOG_NONE &&
		       CLineInput::GetActiveInput() == nullptr &&
		       !Ui()->IsPopupOpen() &&
		       Map()->m_vSelectedLayers.size() == 1 &&
		       Map()->SelectedLayer(0) != nullptr &&
		       (Map()->SelectedLayer(0)->m_Type == LAYERTYPE_TILES || Map()->SelectedLayer(0)->m_Type == LAYERTYPE_QUADS) &&
		       Input()->KeyIsPressed(KEY_SPACE);

	const bool ShowExtraEditor = !m_ShowPicker && m_ActiveExtraEditor != EXTRAEDITOR_NONE;
	m_Workspace = QmEditorLayout::Calculate(Screen, m_ToolBoxWidth, m_InspectorWidth,
		ShowExtraEditor ? m_aExtraEditorSplits[(int)m_ActiveExtraEditor] : 0.0f,
		m_GuiActive, m_ShowInspector, ShowExtraEditor);
	CUIRect View = m_Workspace.m_Canvas;
	CUIRect TooltipRect{};

	if(m_GuiActive)
	{
		m_Workspace.m_Menu.Draw(QmEditorTheme::HEADER, IGraphics::CORNER_NONE, 0.0f);
		m_Workspace.m_MapTabs.Draw(QmEditorTheme::WORKSPACE, IGraphics::CORNER_NONE, 0.0f);
		m_Workspace.m_Toolbar.Draw(QmEditorTheme::HEADER, IGraphics::CORNER_NONE, 0.0f);
		m_Workspace.m_Layers.Draw(QmEditorTheme::PANEL, IGraphics::CORNER_NONE, 0.0f);
		m_Workspace.m_Inspector.Draw(QmEditorTheme::PANEL, IGraphics::CORNER_NONE, 0.0f);
		m_Workspace.m_Status.Draw(QmEditorTheme::HEADER, IGraphics::CORNER_NONE, 0.0f);

		CUIRect MenuBar = m_Workspace.m_Menu;
		MenuBar.Margin(3.0f, &MenuBar);
		RenderMenubar(MenuBar);
		CUIRect MapTabs = m_Workspace.m_MapTabs;
		MapTabs.Margin(3.0f, &MapTabs);
		DoMapTabs(MapTabs);
	}
	HandleMapTabShortcuts();
	RenderWorkspaceToolbar();

	RenderBackground(View, m_CheckerTexture, 32.0f, 0.60f);
	if(m_Mode == MODE_LAYERS)
		DoMapEditor(View);

	if(m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && !Ui()->IsPopupOpen())
	{
		// handle undo/redo hotkeys
		if(Ui()->CheckActiveItem(nullptr))
		{
			if(Input()->KeyPress(KEY_Z) && Input()->ModifierIsPressed() && !Input()->ShiftIsPressed())
				ActiveHistory().Undo();
			if((Input()->KeyPress(KEY_Y) && Input()->ModifierIsPressed()) || (Input()->KeyPress(KEY_Z) && Input()->ModifierIsPressed() && Input()->ShiftIsPressed()))
				ActiveHistory().Redo();
		}

		// handle brush save/load hotkeys
		for(int i = KEY_1; i <= KEY_0; i++)
		{
			if(Input()->KeyPress(i))
			{
				int Slot = i - KEY_1;
				if(Input()->ModifierIsPressed() && !m_pBrush->IsEmpty())
				{
					dbg_msg("editor", Localize("saving current brush to %d", "Editor"), Slot);
					m_apSavedBrushes[Slot] = std::make_shared<CLayerGroup>(*m_pBrush);
				}
				else if(m_apSavedBrushes[Slot])
				{
					dbg_msg("editor", Localize("loading brush from slot %d", "Editor"), Slot);
					m_pBrush = std::make_shared<CLayerGroup>(*m_apSavedBrushes[Slot]);
				}
			}
		}
	}

	if(m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && !Ui()->IsPopupOpen())
	{
		const bool ModPressed = Input()->ModifierIsPressed();
		const bool ShiftPressed = Input()->ShiftIsPressed();
		const bool AltPressed = Input()->AltIsPressed();

		if(CLineInput::GetActiveInput() == nullptr)
		{
			// ctrl+a to append map
			if(Input()->KeyPress(KEY_A) && ModPressed)
			{
				m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::MAP, Localize("Append map", "Editor"), Localize("Append", "Editor"), "maps", "", CallbackAppendMap, this);
			}
		}

		// ctrl+n to create new map
		if(Input()->KeyPress(KEY_N) && ModPressed)
		{
			AddDefaultMap();
			Reset(false);
		}
		// ctrl+o or ctrl+l to open
		if((Input()->KeyPress(KEY_O) || Input()->KeyPress(KEY_L)) && ModPressed)
		{
			if(ShiftPressed)
			{
				if(!m_QuickActionLoadCurrentMap.Disabled())
				{
					m_QuickActionLoadCurrentMap.Call();
				}
			}
			else
			{
				m_FileBrowser.ShowFileDialog(IStorage::TYPE_ALL, CFileBrowser::EFileType::MAP, Localize("Load map", "Editor"), Localize("Load", "Editor"), "maps", "", CallbackOpenMap, this);
			}
		}

		// ctrl+shift+alt+s to save copy
		if(Input()->KeyPress(KEY_S) && ModPressed && ShiftPressed && AltPressed)
		{
			char aDefaultName[IO_MAX_PATH_LENGTH];
			fs_split_file_extension(fs_filename(Map()->m_aFilename), aDefaultName, sizeof(aDefaultName));
			m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save copy", "Editor"), "maps", aDefaultName, CallbackSaveCopyMap, this);
		}
		// ctrl+shift+s to save as
		else if(Input()->KeyPress(KEY_S) && ModPressed && ShiftPressed)
		{
			m_QuickActionSaveAs.Call();
		}
		// ctrl+s to save
		else if(Input()->KeyPress(KEY_S) && ModPressed)
		{
			if(Map()->m_aFilename[0] != '\0' && Map()->m_ValidSaveFilename)
			{
				CallbackSaveMap(Map()->m_aFilename, IStorage::TYPE_SAVE, this);
			}
			else
			{
				m_FileBrowser.ShowFileDialog(IStorage::TYPE_SAVE, CFileBrowser::EFileType::MAP, Localize("Save map", "Editor"), Localize("Save", "Editor"), "maps", "", CallbackSaveMap, this);
			}
		}
	}

	if(m_GuiActive)
	{
		Ui()->MapScreen();
		CUIRect ToolBox = m_Workspace.m_Layers;
		if(ToolBox.w > 0.0f)
		{
			DoEditorDragBar(ToolBox, &m_Workspace.m_LayersSplitter, EDragSide::SIDE_RIGHT, &m_ToolBoxWidth,
				QmEditorLayout::MIN_LAYERS_WIDTH, std::max(QmEditorLayout::MIN_LAYERS_WIDTH, Screen.w - m_Workspace.m_Inspector.w - QmEditorLayout::MIN_CANVAS_WIDTH - 2.0f * QmEditorLayout::SPLITTER_WIDTH));
			ToolBox.Margin(5.0f, &ToolBox);
			CUIRect ModeBar;
			ToolBox.HSplitTop(24.0f, &ModeBar, &ToolBox);
			ToolBox.HSplitTop(6.0f, nullptr, &ToolBox);
			RenderModebar(ModeBar);
			if(m_Mode == MODE_LAYERS)
				RenderLayers(ToolBox);
			else if(m_Mode == MODE_IMAGES)
			{
				RenderImagesList(ToolBox);
				RenderSelectedImage(View);
			}
			else if(m_Mode == MODE_SOUNDS)
				RenderSounds(ToolBox);
		}
		if(m_Workspace.m_Inspector.w > 0.0f)
		{
			DoEditorDragBar(m_Workspace.m_Inspector, &m_Workspace.m_InspectorSplitter, EDragSide::SIDE_LEFT, &m_InspectorWidth,
				QmEditorLayout::MIN_INSPECTOR_WIDTH, std::max(QmEditorLayout::MIN_INSPECTOR_WIDTH, Screen.w - m_Workspace.m_Layers.w - QmEditorLayout::MIN_CANVAS_WIDTH - 2.0f * QmEditorLayout::SPLITTER_WIDTH));
			RenderInspector(m_Workspace.m_Inspector);
		}

		if(m_Workspace.m_ExtraEditor.h > 0.0f)
		{
			CUIRect ExtraEditor = m_Workspace.m_ExtraEditor;
			ExtraEditor.Draw(QmEditorTheme::PANEL, IGraphics::CORNER_NONE, 0.0f);
			ExtraEditor.Margin(2.0f, &ExtraEditor);
			if(m_ActiveExtraEditor == EXTRAEDITOR_ENVELOPES)
				m_EnvelopeEditor.Render(ExtraEditor);
			else if(m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS)
				RenderServerSettingsEditor(ExtraEditor, m_ShowServerSettingsEditorLast);
			else if(m_ActiveExtraEditor == EXTRAEDITOR_HISTORY)
				RenderEditorHistory(ExtraEditor);
		}
		m_ShowServerSettingsEditorLast = m_ActiveExtraEditor == EXTRAEDITOR_SERVER_SETTINGS && ShowExtraEditor;
		CUIRect StatusBar = m_Workspace.m_Status;
		StatusBar.Margin(3.0f, &StatusBar);
		RenderStatusbar(StatusBar, &TooltipRect);
	}

	RenderPressedKeys(View);
	RenderSavingIndicator(View);

	if(m_Dialog == DIALOG_MAPSETTINGS_ERROR)
	{
		static int s_NullUiTarget = 0;
		Ui()->SetHotItem(&s_NullUiTarget);
		RenderMapSettingsErrorDialog();
	}

	if(m_PopupEventActivated)
	{
		static SPopupMenuId s_PopupEventId;
		constexpr float PopupWidth = 400.0f;
		constexpr float PopupHeight = 150.0f;
		Ui()->DoPopupMenu(&s_PopupEventId, Width / 2.0f - PopupWidth / 2.0f, Height / 2.0f - PopupHeight / 2.0f, PopupWidth, PopupHeight, this, PopupEvent);
		m_PopupEventActivated = false;
		m_PopupEventWasActivated = true;
	}

	if(m_Dialog == DIALOG_NONE && !Ui()->IsPopupOpen() && Ui()->MouseInside(&View))
	{
		// handle zoom hotkeys
		if(CLineInput::GetActiveInput() == nullptr)
		{
			if(Input()->KeyPress(KEY_KP_MINUS))
				MapView()->Zoom()->ChangeValue(50.0f);
			if(Input()->KeyPress(KEY_KP_PLUS))
				MapView()->Zoom()->ChangeValue(-50.0f);
			if(Input()->KeyPress(KEY_KP_MULTIPLY))
				MapView()->ResetZoom();
		}

		const bool DrawingToolsWheelHandled = m_DrawingTools.HandleWheelInput(this, View);
		if(!DrawingToolsWheelHandled && (m_pBrush->IsEmpty() || !Input()->ShiftIsPressed()))
		{
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				MapView()->Zoom()->ChangeValue(20.0f);
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
				MapView()->Zoom()->ChangeValue(-20.0f);
		}
		if(!DrawingToolsWheelHandled && !m_pBrush->IsEmpty())
		{
			const bool HasTeleTiles = std::any_of(m_pBrush->m_vpLayers.begin(), m_pBrush->m_vpLayers.end(), [](const auto &pLayer) {
				return pLayer->m_Type == LAYERTYPE_TILES && std::static_pointer_cast<CLayerTiles>(pLayer)->m_HasTele;
			});
			if(HasTeleTiles)
				str_copy(m_aTooltip, Localize("Use shift+mouse wheel up/down to adjust the tele numbers. Use ctrl+f to change all tele numbers to the first unused number.", "Editor"));

			if(Input()->ShiftIsPressed())
			{
				const int AdjustModifiers = Input()->ModifierIsPressed() ? (Input()->AltIsPressed() ? 2 : 1) : 0;
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					AdjustBrushSpecialTiles(false, AdjustModifiers, -1);
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					AdjustBrushSpecialTiles(false, AdjustModifiers, 1);
			}

			// Use ctrl+f to replace number in brush with next free
			if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_F))
				AdjustBrushSpecialTiles(true, 0, 0);
		}
	}

	for(CEditorComponent &Component : m_vComponents)
		Component.OnRender(View);

	MapView()->UpdateZoom();

	// Cancel color pipette with escape before closing popup menus with escape
	if(m_ColorPipetteActive && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ColorPipetteActive = false;
	}

	Ui()->RenderPopupMenus();
	FreeDynamicPopupMenus();

	UpdateColorPipette();

	if(m_Dialog == DIALOG_NONE && !m_PopupEventActivated && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		OnClose();
		g_Config.m_ClEditor = 0;
	}

	// The tooltip can be set in popup menus so we have to render the tooltip after the popup menus.
	if(m_GuiActive)
		RenderTooltip(TooltipRect);

	Ui()->RenderBackButton();
	RenderMousePointer();
}
