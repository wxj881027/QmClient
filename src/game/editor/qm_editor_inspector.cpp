#include "editor.h"
#include "editor_actions.h"

#include <engine/textrender.h>

#include <game/client/qm_icon_manager.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

using namespace FontIcons;

void CEditor::ResetInspectorSelection()
{
	if(m_pInspectorMap)
	{
		bool Changed = m_pInspectorMap->m_LayerGroupPropTracker.Finish();
		Changed |= m_pInspectorMap->m_LayerPropTracker.Finish();
		Changed |= m_pInspectorMap->m_LayerTilesPropTracker.Finish();
		Changed |= m_pInspectorMap->m_LayerTilesCommonPropTracker.Finish();
		Changed |= m_pInspectorMap->m_LayerQuadPropTracker.Finish();
		Changed |= m_pInspectorMap->m_LayerSoundsPropTracker.Finish();
		if(Changed)
			m_pInspectorMap->OnModify();
		m_pInspectorMap = nullptr;
	}
	if(m_GroupNameInput.IsActive())
		m_GroupNameInput.Deactivate();
	if(m_LayerNameInput.IsActive())
		m_LayerNameInput.Deactivate();
	m_GroupNameInput.SetBuffer(nullptr, 0);
	m_LayerNameInput.SetBuffer(nullptr, 0);
	m_ValueSelectorState.Reset();
	m_InspectorGroup.reset();
	m_InspectorLayer.reset();
	m_vInspectorSelection.clear();
	m_LayerPropertiesContext.m_vpLayers.clear();
	m_LayerPropertiesContext.m_vLayerIndices.clear();
	m_LayerPropertiesContext.m_CommonPropState = {};
}

void CEditor::RefreshInspectorSelection()
{
	const auto pGroup = Map()->SelectedGroup();
	const auto pLayer = Map()->SelectedLayer(0);
	if(m_InspectorGroup.lock() == pGroup && m_InspectorLayer.lock() == pLayer && m_vInspectorSelection == Map()->m_vSelectedLayers)
		return;

	if(m_ValueSelectorState.m_pEditing || m_ValueSelectorState.m_pLastTextId)
	{
		Ui()->SetActiveItem(nullptr);
		Ui()->DisableMouseLock();
	}
	ResetInspectorSelection();
	m_pInspectorMap = Map();
	m_InspectorGroup = pGroup;
	m_InspectorLayer = pLayer;
	m_vInspectorSelection = Map()->m_vSelectedLayers;
	m_LayerPropertiesContext.m_pEditor = this;
	Map()->m_EditorUiElements.m_InspectorScrollRegion.Reset();

	if(!pGroup || m_vInspectorSelection.size() < 2)
		return;
	for(int Index : m_vInspectorSelection)
	{
		if(Index < 0 || (size_t)Index >= pGroup->m_vpLayers.size() || pGroup->m_vpLayers[Index]->m_Type != LAYERTYPE_TILES)
		{
			m_LayerPropertiesContext.m_vpLayers.clear();
			m_LayerPropertiesContext.m_vLayerIndices.clear();
			return;
		}
		m_LayerPropertiesContext.m_vpLayers.push_back(std::static_pointer_cast<CLayerTiles>(pGroup->m_vpLayers[Index]));
		m_LayerPropertiesContext.m_vLayerIndices.push_back(Index);
	}
}

void CEditor::ShowGroupProperties()
{
	Map()->m_EditorUiElements.m_InspectGroup = true;
	RefreshInspectorSelection();
	if(m_ShowInspector && m_Workspace.m_Inspector.w > 0.0f)
		return;
	Ui()->DoPopupMenu(&m_GroupPropertiesPopupId, Ui()->MouseX(), Ui()->MouseY(), 180.0f, 280.0f, this, PopupGroup);
}

void CEditor::ShowLayerProperties()
{
	Map()->m_EditorUiElements.m_InspectGroup = false;
	RefreshInspectorSelection();
	if(m_ShowInspector && m_Workspace.m_Inspector.w > 0.0f)
		return;
	Ui()->DoPopupMenu(&m_LayerPropertiesContext, Ui()->MouseX(), Ui()->MouseY(), 190.0f, 320.0f, &m_LayerPropertiesContext, PopupLayer);
}

void CEditor::RenderInspector(CUIRect View)
{
	RefreshInspectorSelection();
	View.Margin(7.0f, &View);
	CUIRect Heading;
	View.HSplitTop(22.0f, &Heading, &View);
	Ui()->DoLabel(&Heading, Localize("Properties", "Editor"), 12.0f, TEXTALIGN_ML);
	View.HSplitTop(5.0f, nullptr, &View);

	if(m_Mode == MODE_IMAGES)
	{
		const auto pImage = Map()->SelectedImage();
		if(pImage)
		{
			View.HSplitTop(22.0f, &Heading, &View);
			Ui()->DoLabel(&Heading, pImage->m_aName, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = Heading.w, .m_EllipsisAtEnd = true});
			View.HSplitTop(22.0f, &Heading, &View);
			char aSize[64];
			str_format(aSize, sizeof(aSize), Localize("Size: %zu × %zu", "Editor"), pImage->m_Width, pImage->m_Height);
			Ui()->DoLabel(&Heading, aSize, 10.0f, TEXTALIGN_ML);
		}
		return;
	}
	if(m_Mode == MODE_SOUNDS)
	{
		const auto pSound = Map()->SelectedSound();
		if(pSound)
		{
			View.HSplitTop(22.0f, &Heading, &View);
			Ui()->DoLabel(&Heading, pSound->m_aName, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = Heading.w, .m_EllipsisAtEnd = true});
			View.HSplitTop(60.0f, &View, nullptr);
			DoToolbarSounds(View);
		}
		return;
	}

	bool &InspectGroup = Map()->m_EditorUiElements.m_InspectGroup;
	CUIRect Tabs, GroupTab, LayerTab;
	View.HSplitTop(22.0f, &Tabs, &View);
	Tabs.VSplitMid(&GroupTab, &LayerTab, 3.0f);
	static int s_GroupTab, s_LayerTab;
	if(DoButton_Editor(&s_GroupTab, Localize("Group", "Editor"), InspectGroup, &GroupTab, BUTTONFLAG_LEFT, nullptr))
	{
		InspectGroup = true;
		Map()->m_EditorUiElements.m_InspectorScrollRegion.Reset();
	}
	if(DoButton_Editor(&s_LayerTab, Localize("Layer", "Editor"), !InspectGroup, &LayerTab, BUTTONFLAG_LEFT, nullptr))
	{
		InspectGroup = false;
		Map()->m_EditorUiElements.m_InspectorScrollRegion.Reset();
	}
	View.HSplitTop(8.0f, nullptr, &View);
	if(Ui()->IsPopupOpen(&m_GroupPropertiesPopupId) || Ui()->IsPopupOpen(&m_LayerPropertiesContext))
		return;

	const auto pGroup = Map()->SelectedGroup();
	const auto pLayer = Map()->SelectedLayer(0);
	if(!pGroup || (!InspectGroup && !pLayer))
	{
		View.HSplitTop(20.0f, &Heading, nullptr);
		Ui()->DoLabel(&Heading, Localize("No selection", "Editor"), 10.0f, TEXTALIGN_ML);
		return;
	}

	CScrollRegion &ScrollRegion = Map()->m_EditorUiElements.m_InspectorScrollRegion;
	CScrollRegionParams Params;
	Params.m_ScrollbarThickness = 8.0f;
	Params.m_ScrollbarMargin = 1.0f;
	Params.m_ScrollUnit = QmEditorTheme::ROW_HEIGHT * 3.0f;
	vec2 Offset(0.0f, 0.0f);
	ScrollRegion.Begin(&View, &Offset, &Params);
	CUIRect Content = View;
	Content.y += Offset.y;
	Content.h = InspectGroup ? 400.0f : pLayer->m_Type == LAYERTYPE_TILES ? 420.0f :
										260.0f;
	ScrollRegion.AddRect(Content);
	m_PropertiesInInspector = true;
	if(InspectGroup)
		RenderGroupProperties(Content);
	else
		RenderLayerProperties(m_LayerPropertiesContext, Content);
	m_PropertiesInInspector = false;
	ScrollRegion.End();
}

CUi::EPopupMenuFunctionResult CEditor::RenderGroupProperties(CUIRect View)
{
	CEditor *pEditor = this;
	if(!Map()->SelectedGroup())
		return CUi::POPUP_CLOSE_CURRENT;
	const float ButtonHeight = m_PropertiesInInspector ? QmEditorTheme::ROW_HEIGHT : 12.0f;

	// remove group button
	CUIRect Button;
	View.HSplitBottom(ButtonHeight, &View, &Button);
	static int s_DeleteButton = 0;

	// don't allow deletion of game group
	if(pEditor->Map()->m_pGameGroup != pEditor->Map()->SelectedGroup())
	{
		if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Delete group", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Delete the group.", "Editor")))
		{
			pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, true));
			pEditor->Map()->DeleteGroup(pEditor->Map()->m_SelectedGroup);
			pEditor->Map()->m_SelectedGroup = maximum(0, pEditor->Map()->m_SelectedGroup - 1);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else
	{
		if(pEditor->DoButton_Editor(&s_DeleteButton, Localize("Clean up game tiles", "Editor"), 0, &Button, BUTTONFLAG_LEFT, Localize("Remove game tiles that aren't based on a layer.", "Editor")))
		{
			// gather all tile layers
			std::vector<std::shared_ptr<CLayerTiles>> vpLayers;
			int GameLayerIndex = -1;
			for(int LayerIndex = 0; LayerIndex < (int)pEditor->Map()->m_pGameGroup->m_vpLayers.size(); LayerIndex++)
			{
				auto &pLayer = pEditor->Map()->m_pGameGroup->m_vpLayers.at(LayerIndex);
				if(pLayer != pEditor->Map()->m_pGameLayer && pLayer->m_Type == LAYERTYPE_TILES)
					vpLayers.push_back(std::static_pointer_cast<CLayerTiles>(pLayer));
				else if(pLayer == pEditor->Map()->m_pGameLayer)
					GameLayerIndex = LayerIndex;
			}

			// search for unneeded game tiles
			std::shared_ptr<CLayerTiles> pGameLayer = pEditor->Map()->m_pGameLayer;
			for(int y = 0; y < pGameLayer->m_Height; ++y)
			{
				for(int x = 0; x < pGameLayer->m_Width; ++x)
				{
					if(pGameLayer->m_pTiles[y * pGameLayer->m_Width + x].m_Index > static_cast<unsigned char>(TILE_NOHOOK))
						continue;

					bool Found = false;
					for(const auto &pLayer : vpLayers)
					{
						if(x < pLayer->m_Width && y < pLayer->m_Height && pLayer->m_pTiles[y * pLayer->m_Width + x].m_Index)
						{
							Found = true;
							break;
						}
					}

					CTile Tile = pGameLayer->GetTile(x, y);
					if(!Found && Tile.m_Index != TILE_AIR)
					{
						Tile.m_Index = TILE_AIR;
						pGameLayer->SetTile(x, y, Tile);
						pEditor->Map()->OnModify();
					}
				}
			}

			if(!pGameLayer->m_TilesHistory.empty())
			{
				if(GameLayerIndex == -1)
				{
					dbg_msg("editor", "failed to record action (GameLayerIndex not found)");
				}
				else
				{
					// record undo
					pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionTileChanges>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, GameLayerIndex, Localize("Clean up game tiles", "Editor"), pGameLayer->m_TilesHistory));
				}
				pGameLayer->ClearHistory();
			}

			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pTeleLayer)
	{
		// new tele layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTeleLayer, pEditor->m_QuickActionAddTeleLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTeleLayer.Description()))
		{
			pEditor->m_QuickActionAddTeleLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pSpeedupLayer)
	{
		// new speedup layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSpeedupLayer, pEditor->m_QuickActionAddSpeedupLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSpeedupLayer.Description()))
		{
			pEditor->m_QuickActionAddSpeedupLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pTuneLayer)
	{
		// new tune layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTuneLayer, pEditor->m_QuickActionAddTuneLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTuneLayer.Description()))
		{
			pEditor->m_QuickActionAddTuneLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pFrontLayer)
	{
		// new front layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddFrontLayer, pEditor->m_QuickActionAddFrontLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddFrontLayer.Description()))
		{
			pEditor->m_QuickActionAddFrontLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	if(pEditor->Map()->SelectedGroup()->m_GameGroup && !pEditor->Map()->m_pSwitchLayer)
	{
		// new Switch layer
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSwitchLayer, pEditor->m_QuickActionAddSwitchLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSwitchLayer.Description()))
		{
			pEditor->m_QuickActionAddSwitchLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// new quad layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(ButtonHeight, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddQuadsLayer, pEditor->m_QuickActionAddQuadsLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddQuadsLayer.Description()))
	{
		pEditor->m_QuickActionAddQuadsLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// new tile layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(ButtonHeight, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddTileLayer, pEditor->m_QuickActionAddTileLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddTileLayer.Description()))
	{
		pEditor->m_QuickActionAddTileLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// new sound layer
	View.HSplitBottom(5.0f, &View, nullptr);
	View.HSplitBottom(ButtonHeight, &View, &Button);
	if(pEditor->DoButton_Editor(&pEditor->m_QuickActionAddSoundLayer, pEditor->m_QuickActionAddSoundLayer.Label(), 0, &Button, BUTTONFLAG_LEFT, pEditor->m_QuickActionAddSoundLayer.Description()))
	{
		pEditor->m_QuickActionAddSoundLayer.Call();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// group name
	if(!pEditor->Map()->SelectedGroup()->m_GameGroup)
	{
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Button);
		pEditor->Ui()->DoLabel(&Button, Localize("Name:", "Editor property label"), 10.0f, TEXTALIGN_ML);
		Button.VSplitLeft(40.0f, nullptr, &Button);
		CLineInput &s_NameInput = m_GroupNameInput;
		s_NameInput.SetBuffer(pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_aName, sizeof(pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_aName));
		if(pEditor->DoEditBox(&s_NameInput, &Button, 10.0f))
			pEditor->Map()->OnModify();
	}

	CProperty aProps[] = {
		{Localize("Order", "Editor"), pEditor->Map()->m_SelectedGroup, PROPTYPE_INT, 0, (int)pEditor->Map()->m_vpGroups.size() - 1},
		{Localize("Pos X", "Editor"), -pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Pos Y", "Editor"), -pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Para X", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Para Y", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Use Clipping", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_UseClipping, PROPTYPE_BOOL, 0, 1},
		{Localize("Clip X", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipX, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Clip Y", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipY, PROPTYPE_INT, -1000000, 1000000},
		{Localize("Clip W", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipW, PROPTYPE_INT, 0, 1000000},
		{Localize("Clip H", "Editor"), pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipH, PROPTYPE_INT, 0, 1000000},
		{nullptr},
	};

	// cut the properties that aren't needed
	if(pEditor->Map()->SelectedGroup()->m_GameGroup)
		aProps[(int)EGroupProp::PROP_POS_X].m_pName = nullptr;

	static int s_aIds[(int)EGroupProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<EGroupProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != EGroupProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->OnModify();
	}

	pEditor->Map()->m_LayerGroupPropTracker.Begin(pEditor->Map()->SelectedGroup().get(), Prop, State);

	if(Prop == EGroupProp::PROP_ORDER)
	{
		pEditor->Map()->m_SelectedGroup = pEditor->Map()->MoveGroup(pEditor->Map()->m_SelectedGroup, NewVal);
	}

	// these can not be changed on the game group
	if(!pEditor->Map()->SelectedGroup()->m_GameGroup)
	{
		if(Prop == EGroupProp::PROP_PARA_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxX = NewVal;
		}
		else if(Prop == EGroupProp::PROP_PARA_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ParallaxY = NewVal;
		}
		else if(Prop == EGroupProp::PROP_POS_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetX = -NewVal;
		}
		else if(Prop == EGroupProp::PROP_POS_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_OffsetY = -NewVal;
		}
		else if(Prop == EGroupProp::PROP_USE_CLIPPING)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_UseClipping = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_X)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipX = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_Y)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipY = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_W)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipW = NewVal;
		}
		else if(Prop == EGroupProp::PROP_CLIP_H)
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->m_ClipH = NewVal;
		}
	}

	pEditor->Map()->m_LayerGroupPropTracker.End(Prop, State);

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CEditor::RenderLayerProperties(SLayerPopupContext &Context, CUIRect View)
{
	SLayerPopupContext *pPopup = &Context;
	CEditor *pEditor = this;
	const float ButtonHeight = m_PropertiesInInspector ? QmEditorTheme::ROW_HEIGHT : 12.0f;

	std::shared_ptr<CLayerGroup> pCurrentGroup = pEditor->Map()->SelectedGroup();
	std::shared_ptr<CLayer> pCurrentLayer = pEditor->Map()->SelectedLayer(0);

	if(!pCurrentLayer || !pCurrentGroup)
		return CUi::POPUP_CLOSE_CURRENT;

	if(pPopup->m_vpLayers.size() > 1)
	{
		return CLayerTiles::RenderCommonProperties(pPopup->m_CommonPropState, pEditor->Map(), &View, pPopup->m_vpLayers, pPopup->m_vLayerIndices);
	}

	const bool EntitiesLayer = pCurrentLayer->IsEntitiesLayer();

	// delete button
	if(pEditor->Map()->m_pGameLayer != pCurrentLayer) // entities layers except the game layer can be deleted
	{
		CUIRect DeleteButton;
		View.HSplitBottom(ButtonHeight, &View, &DeleteButton);
		if(pEditor->DoButton_Editor(&pEditor->m_QuickActionDeleteLayer, pEditor->m_QuickActionDeleteLayer.Label(), 0, &DeleteButton, BUTTONFLAG_LEFT, pEditor->m_QuickActionDeleteLayer.Description()))
		{
			pEditor->m_QuickActionDeleteLayer.Call();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// duplicate button
	if(!EntitiesLayer) // entities layers cannot be duplicated
	{
		CUIRect DuplicateButton;
		View.HSplitBottom(4.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &DuplicateButton);
		static int s_DuplicationButton = 0;
		if(pEditor->DoButton_Editor(&s_DuplicationButton, Localize("Duplicate layer", "Editor"), 0, &DuplicateButton, BUTTONFLAG_LEFT, Localize("Create an identical copy of the selected layer.", "Editor")))
		{
			pEditor->Map()->m_vpGroups[pEditor->Map()->m_SelectedGroup]->DuplicateLayer(pEditor->Map()->m_vSelectedLayers[0]);
			pEditor->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(pEditor->Map(), pEditor->Map()->m_SelectedGroup, pEditor->Map()->m_vSelectedLayers[0] + 1, true));
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	// layer name
	if(!EntitiesLayer) // name cannot be changed for entities layers
	{
		CUIRect Label, EditBox;
		View.HSplitBottom(5.0f, &View, nullptr);
		View.HSplitBottom(ButtonHeight, &View, &Label);
		Label.VSplitLeft(40.0f, &Label, &EditBox);
		pEditor->Ui()->DoLabel(&Label, Localize("Name:", "Editor property label"), 10.0f, TEXTALIGN_ML);
		CLineInput &s_NameInput = m_LayerNameInput;
		s_NameInput.SetBuffer(pCurrentLayer->m_aName, sizeof(pCurrentLayer->m_aName));
		if(pEditor->DoEditBox(&s_NameInput, &EditBox, 10.0f))
			pEditor->Map()->OnModify();
	}

	// spacing if any button was rendered
	if(!EntitiesLayer || pEditor->Map()->m_pGameLayer != pCurrentLayer)
		View.HSplitBottom(10.0f, &View, nullptr);

	CProperty aProps[] = {
		{Localize("Group", "Editor"), pEditor->Map()->m_SelectedGroup, PROPTYPE_INT, 0, (int)pEditor->Map()->m_vpGroups.size() - 1},
		{Localize("Order", "Editor"), pEditor->Map()->m_vSelectedLayers[0], PROPTYPE_INT, 0, (int)pCurrentGroup->m_vpLayers.size() - 1},
		{Localize("Detail", "Editor"), pCurrentLayer->m_Flags & LAYERFLAG_DETAIL, PROPTYPE_BOOL, 0, 1},
		{nullptr},
	};

	// don't use Group and Detail from the selection if this is an entities layer
	if(EntitiesLayer)
	{
		aProps[0].m_Type = PROPTYPE_NULL;
		aProps[2].m_Type = PROPTYPE_NULL;
	}

	static int s_aIds[(int)ELayerProp::NUM_PROPS] = {0};
	int NewVal = 0;
	auto [State, Prop] = pEditor->DoPropertiesWithState<ELayerProp>(&View, aProps, s_aIds, &NewVal);
	if(Prop != ELayerProp::PROP_NONE && (State == EEditState::END || State == EEditState::ONE_GO))
	{
		pEditor->Map()->OnModify();
	}

	pEditor->Map()->m_LayerPropTracker.Begin(pCurrentLayer.get(), Prop, State);

	if(Prop == ELayerProp::PROP_ORDER)
	{
		pEditor->Map()->SelectLayer(pCurrentGroup->MoveLayer(pEditor->Map()->m_vSelectedLayers[0], NewVal));
	}
	else if(Prop == ELayerProp::PROP_GROUP)
	{
		if(NewVal >= 0 && (size_t)NewVal < pEditor->Map()->m_vpGroups.size() && NewVal != pEditor->Map()->m_SelectedGroup)
		{
			auto Position = std::find(pCurrentGroup->m_vpLayers.begin(), pCurrentGroup->m_vpLayers.end(), pCurrentLayer);
			if(Position != pCurrentGroup->m_vpLayers.end())
				pCurrentGroup->m_vpLayers.erase(Position);
			pEditor->Map()->m_vpGroups[NewVal]->m_vpLayers.push_back(pCurrentLayer);
			pEditor->Map()->m_SelectedGroup = NewVal;
			pEditor->Map()->SelectLayer(pEditor->Map()->m_vpGroups[NewVal]->m_vpLayers.size() - 1);
		}
	}
	else if(Prop == ELayerProp::PROP_HQ)
	{
		pCurrentLayer->m_Flags &= ~LAYERFLAG_DETAIL;
		if(NewVal)
			pCurrentLayer->m_Flags |= LAYERFLAG_DETAIL;
	}

	pEditor->Map()->m_LayerPropTracker.End(Prop, State);

	return pCurrentLayer->RenderProperties(&View);
}
