#include "map_view.h"

#include "editor.h"

#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/ui.h>
#include <game/editor/editor_actions.h>
#include <game/editor/explanations.h>

// 图层种类：复制区域时用来把画笔层对应到装着同一种数据的图层
static ELayerKind LayerKindOf(const std::shared_ptr<CLayer> &pLayer)
{
	if(pLayer->m_Type == LAYERTYPE_TILES)
	{
		const std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
		return LayerKindFromFlags(pTiles->m_HasGame, pTiles->m_HasFront, pTiles->m_HasTele, pTiles->m_HasSpeedup, pTiles->m_HasSwitch, pTiles->m_HasTune);
	}
	if(pLayer->m_Type == LAYERTYPE_QUADS)
		return ELayerKind::QUADS;
	if(pLayer->m_Type == LAYERTYPE_SOUNDS)
		return ELayerKind::SOUNDS;
	return ELayerKind::INVALID;
}

/**
 * 在画笔里找目标图层对应的层：
 * 1. 先按旧的“层数一致时按顺序对应”取 PreferredIndex 的层，种类一致就用它，保证多层同类选择时行为不变；
 * 2. 再按图层种类匹配，避免自动补进画笔的实体层（排在末尾）被索引规则取错；
 * 3. 最后回退到第一个 m_Type 相同的层，保持旧的“同类贴同类”行为。
 */
static std::shared_ptr<CLayer> FindBrushLayer(const std::shared_ptr<CLayerGroup> &pBrush, const std::shared_ptr<CLayer> &pTarget, size_t PreferredIndex)
{
	const std::vector<std::shared_ptr<CLayer>> &vpLayers = pBrush->m_vpLayers;
	const ELayerKind TargetKind = LayerKindOf(pTarget);
	if(PreferredIndex < vpLayers.size() && vpLayers[PreferredIndex] && LayerKindOf(vpLayers[PreferredIndex]) == TargetKind)
		return vpLayers[PreferredIndex];
	for(const std::shared_ptr<CLayer> &pBrushLayer : vpLayers)
	{
		if(pBrushLayer && LayerKindOf(pBrushLayer) == TargetKind)
			return pBrushLayer;
	}
	for(const std::shared_ptr<CLayer> &pBrushLayer : vpLayers)
	{
		if(pBrushLayer && pBrushLayer->m_Type == pTarget->m_Type)
			return pBrushLayer;
	}
	return nullptr;
}

// 组内索引为 LayerIndex 的图层是否已在本轮编辑图层列表里（即已被选中）
static bool IsEditLayerSelected(const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, int LayerIndex)
{
	for(size_t k = 0; k < NumEditLayers; k++)
	{
		if(pEditLayers[k].first == LayerIndex)
			return true;
	}
	return false;
}

void CMapView::CState::Reset(CEditor *pEditor)
{
	m_Zoom = CSmoothValue(200.0f, 10.0f, 2000.0f);
	m_Zoom.OnInit(pEditor);
	m_WorldZoom = 1.0f;
	m_WorldOffset = vec2(0.0f, 0.0f);
	m_EditorOffset = vec2(0.0f, 0.0f);
	m_MouseWorldScale = 1.0f;
	m_MouseWorldPos = vec2(0.0f, 0.0f);
	m_MouseWorldNoParaPos = vec2(0.0f, 0.0f);
	m_MouseDeltaWorld = vec2(0.0f, 0.0f);
	m_ActiveOp = EActiveOp::NONE;
}

void CMapView::OnInit(CEditor *pEditor)
{
	CEditorComponent::OnInit(pEditor);
	RegisterSubComponent(m_MapGrid);
	RegisterSubComponent(m_ProofMode);
	InitSubComponents();
}

void CMapView::OnMapLoad()
{
	m_ProofMode.OnMapLoad();
}

bool CMapView::IsFocused()
{
	return GetWorldOffset() == (m_ProofMode.IsModeMenu() ? m_ProofMode.CurrentMenuBackgroundPosition() : vec2(0.0f, 0.0f));
}

void CMapView::Focus()
{
	SetWorldOffset(m_ProofMode.IsModeMenu() ? m_ProofMode.CurrentMenuBackgroundPosition() : vec2(0.0f, 0.0f));
}

void CMapView::RenderGroupBorder()
{
	std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();
	if(pGroup)
	{
		pGroup->MapScreen();

		for(size_t i = 0; i < Map()->m_vSelectedLayers.size(); i++)
		{
			std::shared_ptr<CLayer> pLayer = Map()->SelectedLayerType(i, LAYERTYPE_TILES);
			if(pLayer)
			{
				CUIRect BorderRect;
				BorderRect.x = 0.0f;
				BorderRect.y = 0.0f;
				pLayer->GetSize(&BorderRect.w, &BorderRect.h);
				BorderRect.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			}
		}
	}
}

void CMapView::RenderEditorMap()
{
	if(Editor()->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && Input()->KeyPress(KEY_G))
	{
		const bool AnyHidden =
			!Map()->m_pGameLayer->m_Visible ||
			(Map()->m_pFrontLayer && !Map()->m_pFrontLayer->m_Visible) ||
			(Map()->m_pTeleLayer && !Map()->m_pTeleLayer->m_Visible) ||
			(Map()->m_pSpeedupLayer && !Map()->m_pSpeedupLayer->m_Visible) ||
			(Map()->m_pTuneLayer && !Map()->m_pTuneLayer->m_Visible) ||
			(Map()->m_pSwitchLayer && !Map()->m_pSwitchLayer->m_Visible);
		Map()->m_pGameLayer->m_Visible = AnyHidden;
		if(Map()->m_pFrontLayer)
			Map()->m_pFrontLayer->m_Visible = AnyHidden;
		if(Map()->m_pTeleLayer)
			Map()->m_pTeleLayer->m_Visible = AnyHidden;
		if(Map()->m_pSpeedupLayer)
			Map()->m_pSpeedupLayer->m_Visible = AnyHidden;
		if(Map()->m_pTuneLayer)
			Map()->m_pTuneLayer->m_Visible = AnyHidden;
		if(Map()->m_pSwitchLayer)
			Map()->m_pSwitchLayer->m_Visible = AnyHidden;
	}

	for(auto &pGroup : Map()->m_vpGroups)
	{
		if(pGroup->m_Visible)
			pGroup->Render();
	}

	// render the game, tele, speedup, front, tune and switch above everything else
	if(Map()->m_pGameGroup->m_Visible)
	{
		Map()->m_pGameGroup->MapScreen();
		for(auto &pLayer : Map()->m_pGameGroup->m_vpLayers)
		{
			if(pLayer->m_Visible && pLayer->IsEntitiesLayer())
				pLayer->Render();
		}
	}

	std::shared_ptr<CLayerTiles> pSelectedTilesLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
	if(Editor()->m_ShowTileInfo != CEditor::SHOW_TILE_OFF && pSelectedTilesLayer && pSelectedTilesLayer->m_Visible && Zoom()->GetValue() <= 300.0f)
	{
		Map()->SelectedGroup()->MapScreen();
		pSelectedTilesLayer->ShowInfo();
	}
}

void CEditor::DoMapEditor(CUIRect View)
{
	// render all good stuff
	if(!m_ShowPicker)
	{
		MapView()->RenderEditorMap();
	}
	else
	{
		// fix aspect ratio of the image in the picker
		float Max = minimum(View.w, View.h);
		View.w = View.h = Max;
	}

	const bool Inside = Ui()->MouseInside(&View);

	// fetch mouse position
	float wx = MapView()->MouseWorldPos().x;
	float wy = MapView()->MouseWorldPos().y;
	float mx = Ui()->MouseX();
	float my = Ui()->MouseY();

	static float s_StartWx = 0;
	static float s_StartWy = 0;

	// remap the screen so it can display the whole tileset
	if(m_ShowPicker)
	{
		CUIRect Screen = *Ui()->Screen();
		float Size = 32.0f * 16.0f;
		float w = Size * (Screen.w / View.w);
		float h = Size * (Screen.h / View.h);
		float x = -(View.x / Screen.w) * w;
		float y = -(View.y / Screen.h) * h;
		wx = x + w * mx / Screen.w;
		wy = y + h * my / Screen.h;
		std::shared_ptr<CLayerTiles> pTileLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
		if(pTileLayer)
		{
			Graphics()->MapScreen(x, y, x + w, y + h);
			m_pTilesetPicker->m_Image = pTileLayer->m_Image;
			if(m_BrushColorEnabled)
			{
				m_pTilesetPicker->m_Color = pTileLayer->m_Color;
				m_pTilesetPicker->m_Color.a = 255;
			}
			else
			{
				m_pTilesetPicker->m_Color = {255, 255, 255, 255};
			}

			m_pTilesetPicker->m_HasGame = pTileLayer->m_HasGame;
			m_pTilesetPicker->m_HasTele = pTileLayer->m_HasTele;
			m_pTilesetPicker->m_HasSpeedup = pTileLayer->m_HasSpeedup;
			m_pTilesetPicker->m_HasFront = pTileLayer->m_HasFront;
			m_pTilesetPicker->m_HasSwitch = pTileLayer->m_HasSwitch;
			m_pTilesetPicker->m_HasTune = pTileLayer->m_HasTune;

			m_pTilesetPicker->Render(true);

			if(m_ShowTileInfo != SHOW_TILE_OFF)
				m_pTilesetPicker->ShowInfo();
		}
		else
		{
			std::shared_ptr<CLayerQuads> pQuadLayer = std::static_pointer_cast<CLayerQuads>(Map()->SelectedLayerType(0, LAYERTYPE_QUADS));
			if(pQuadLayer)
			{
				m_pQuadsetPicker->m_Image = pQuadLayer->m_Image;
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[0].x = f2fx(View.x);
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[0].y = f2fx(View.y);
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[1].x = f2fx((View.x + View.w));
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[1].y = f2fx(View.y);
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[2].x = f2fx(View.x);
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[2].y = f2fx((View.y + View.h));
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[3].x = f2fx((View.x + View.w));
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[3].y = f2fx((View.y + View.h));
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[4].x = f2fx((View.x + View.w / 2));
				m_pQuadsetPicker->m_vQuads[0].m_aPoints[4].y = f2fx((View.y + View.h / 2));
				m_pQuadsetPicker->Render();
			}
		}
	}

	// draw layer borders
	std::pair<int, std::shared_ptr<CLayer>> apEditLayers[128];
	size_t NumEditLayers = 0;

	if(m_ShowPicker && Map()->SelectedLayer(0) && Map()->SelectedLayer(0)->m_Type == LAYERTYPE_TILES)
	{
		apEditLayers[0] = {0, m_pTilesetPicker};
		NumEditLayers++;
	}
	else if(m_ShowPicker)
	{
		apEditLayers[0] = {0, m_pQuadsetPicker};
		NumEditLayers++;
	}
	else
	{
		// pick a type of layers to edit, preferring Tiles layers.
		int EditingType = -1;
		for(size_t i = 0; i < Map()->m_vSelectedLayers.size(); i++)
		{
			std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(i);
			if(pLayer && (EditingType == -1 || pLayer->m_Type == LAYERTYPE_TILES))
			{
				EditingType = pLayer->m_Type;
				if(EditingType == LAYERTYPE_TILES)
					break;
			}
		}
		for(size_t i = 0; i < Map()->m_vSelectedLayers.size() && NumEditLayers < 128; i++)
		{
			apEditLayers[NumEditLayers] = {Map()->m_vSelectedLayers[i], Map()->SelectedLayerType(i, EditingType)};
			if(apEditLayers[NumEditLayers].second)
			{
				NumEditLayers++;
			}
		}

		MapView()->RenderGroupBorder();
		MapView()->MapGrid()->OnRender(View);
	}

	const bool ShouldPan = Ui()->HotItem() == MapView() && ((Input()->ModifierIsPressed() && Ui()->MouseButton(0)) || Ui()->MouseButton(2));
	if(m_pContainerPanned == MapView())
	{
		// do panning
		if(ShouldPan)
		{
			if(Input()->ShiftIsPressed())
				Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::PAN_EDITOR;
			else
				Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::PAN_WORLD;
			Ui()->SetActiveItem(MapView());
		}
		else
		{
			Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::NONE;
		}

		if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::PAN_WORLD)
			MapView()->OffsetWorld(-Ui()->MouseDelta() * MapView()->MouseWorldScale());
		else if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::PAN_EDITOR)
			MapView()->OffsetEditor(-Ui()->MouseDelta() * MapView()->MouseWorldScale());

		if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::NONE)
			m_pContainerPanned = nullptr;
	}

	if(Inside || m_DrawingTools.IsDrawing())
	{
		if(Inside)
			Ui()->SetHotItem(MapView());

		// do global operations like pan and zoom
		if(Ui()->CheckActiveItem(nullptr) && (Ui()->MouseButton(0) || Ui()->MouseButton(2)))
		{
			s_StartWx = wx;
			s_StartWy = wy;

			if(ShouldPan && m_pContainerPanned == nullptr)
				m_pContainerPanned = MapView();
		}

		// brush editing
		if(Ui()->HotItem() == MapView() || m_DrawingTools.IsDrawing())
		{
			if(m_ShowPicker)
			{
				std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
				int Layer;
				if(pLayer == Map()->m_pGameLayer)
					Layer = LAYER_GAME;
				else if(pLayer == Map()->m_pFrontLayer)
					Layer = LAYER_FRONT;
				else if(pLayer == Map()->m_pSwitchLayer)
					Layer = LAYER_SWITCH;
				else if(pLayer == Map()->m_pTeleLayer)
					Layer = LAYER_TELE;
				else if(pLayer == Map()->m_pSpeedupLayer)
					Layer = LAYER_SPEEDUP;
				else if(pLayer == Map()->m_pTuneLayer)
					Layer = LAYER_TUNE;
				else
					Layer = NUM_LAYERS;

				CExplanations::EGametype ExplanationGametype;
				if(m_SelectEntitiesImage == "DDNet")
					ExplanationGametype = CExplanations::EGametype::DDNET;
				else if(m_SelectEntitiesImage == "FNG")
					ExplanationGametype = CExplanations::EGametype::FNG;
				else if(m_SelectEntitiesImage == "Race")
					ExplanationGametype = CExplanations::EGametype::RACE;
				else if(m_SelectEntitiesImage == "Vanilla")
					ExplanationGametype = CExplanations::EGametype::VANILLA;
				else if(m_SelectEntitiesImage == "blockworlds")
					ExplanationGametype = CExplanations::EGametype::BLOCKWORLDS;
				else
					ExplanationGametype = CExplanations::EGametype::NONE;

				if(Layer != NUM_LAYERS)
				{
					const char *pExplanation = CExplanations::Explain(ExplanationGametype, (int)wx / 32 + (int)wy / 32 * 16, Layer);
					if(pExplanation)
						str_copy(m_aTooltip, pExplanation);
				}
			}
			else if(m_pBrush->IsEmpty() && Map()->SelectedLayerType(0, LAYERTYPE_QUADS) != nullptr)
			{
				str_copy(m_aTooltip, Localize("Use left mouse button to drag and create a brush. Hold shift to select multiple quads. Press R to rotate selected quads. Use ctrl+right click to select layer.", "Editor"));
			}
			else if(m_pBrush->IsEmpty())
			{
				if(g_Config.m_EdLayerSelector)
					str_copy(m_aTooltip, Localize("Use left mouse button to drag and create a brush. Use ctrl+right click to select layer of hovered tile.", "Editor"));
				else
					str_copy(m_aTooltip, Localize("Use left mouse button to drag and create a brush.", "Editor"));
			}
			else
			{
				// Alt behavior handled in CEditor::MouseAxisLock
				str_copy(m_aTooltip, Localize("Use left mouse button to paint with the brush. Right click to clear the brush. Hold Alt to lock the mouse movement to a single axis.", "Editor"));
			}

			const bool DrawingToolsHandled = (Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::NONE || m_DrawingTools.IsDrawing()) && m_pContainerPanned == nullptr && m_DrawingTools.HandleMapEditorInput(this, apEditLayers, NumEditLayers, Inside);
			if(DrawingToolsHandled)
			{
				m_DrawingTools.RenderPreview(this);
			}

			if(!DrawingToolsHandled && Ui()->CheckActiveItem(MapView()))
			{
				CUIRect r;
				r.x = s_StartWx;
				r.y = s_StartWy;
				r.w = wx - s_StartWx;
				r.h = wy - s_StartWy;
				if(r.w < 0)
				{
					r.x += r.w;
					r.w = -r.w;
				}

				if(r.h < 0)
				{
					r.y += r.h;
					r.h = -r.h;
				}

				if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::BRUSH_DRAW)
				{
					if(!m_pBrush->IsEmpty())
					{
						// draw with brush
						for(size_t k = 0; k < NumEditLayers; k++)
						{
							std::shared_ptr<CLayer> pBrushLayer = FindBrushLayer(m_pBrush, apEditLayers[k].second, k % m_pBrush->m_vpLayers.size());
							if(!pBrushLayer || apEditLayers[k].second->m_Type != pBrushLayer->m_Type)
								continue;

							if(apEditLayers[k].second->m_Type == LAYERTYPE_TILES)
							{
								std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(apEditLayers[k].second);
								std::shared_ptr<CLayerTiles> pBrushTiles = std::static_pointer_cast<CLayerTiles>(pBrushLayer);

								if((!pLayer->m_HasTele || pBrushTiles->m_HasTele) && (!pLayer->m_HasSpeedup || pBrushTiles->m_HasSpeedup) && (!pLayer->m_HasFront || pBrushTiles->m_HasFront) && (!pLayer->m_HasGame || pBrushTiles->m_HasGame) && (!pLayer->m_HasSwitch || pBrushTiles->m_HasSwitch) && (!pLayer->m_HasTune || pBrushTiles->m_HasTune))
									pLayer->BrushDraw(pBrushTiles.get(), vec2(wx, wy));
							}
							else
							{
								apEditLayers[k].second->BrushDraw(pBrushLayer.get(), vec2(wx, wy));
							}
						}
					}
				}
				else if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::BRUSH_GRAB)
				{
					if(!Ui()->MouseButton(0))
					{
						std::shared_ptr<CLayerQuads> pQuadLayer = std::static_pointer_cast<CLayerQuads>(Map()->SelectedLayerType(0, LAYERTYPE_QUADS));
						if(Input()->ShiftIsPressed() && pQuadLayer)
						{
							Map()->DeselectQuads();
							for(size_t i = 0; i < pQuadLayer->m_vQuads.size(); i++)
							{
								const CQuad &Quad = pQuadLayer->m_vQuads[i];
								vec2 Position = vec2(fx2f(Quad.m_aPoints[4].x), fx2f(Quad.m_aPoints[4].y));
								if(r.Inside(Position) && !Map()->IsQuadSelected(i))
									Map()->ToggleSelectQuad(i);
							}
						}
						else
						{
							// TODO: do all layers
							int Grabs = 0;
							for(size_t k = 0; k < NumEditLayers; k++)
								Grabs += apEditLayers[k].second->BrushGrab(m_pBrush.get(), r);

							// 游戏组里的传送/开关/速度/调参层和游戏层属于同一块地图数据，未选中时不会被复制，
							// 粘贴出来的区块就会缺这些内容，所以把选区内非空、且未选中的实体层一并抓进画笔。
							const std::shared_ptr<CLayerGroup> pSelectedGroup = Map()->SelectedGroup();
							if(!m_ShowPicker && NumEditLayers > 0 && apEditLayers[0].second->m_Type == LAYERTYPE_TILES && pSelectedGroup && pSelectedGroup->m_GameGroup)
							{
								for(size_t LayerIndex = 0; LayerIndex < pSelectedGroup->m_vpLayers.size(); LayerIndex++)
								{
									const std::shared_ptr<CLayer> pLayer = pSelectedGroup->m_vpLayers[LayerIndex];
									if(pLayer->m_Type != LAYERTYPE_TILES)
										continue;
									if(!ShouldAutoGrabEntityLayer(LayerKindOf(pLayer), IsEditLayerSelected(apEditLayers, NumEditLayers, LayerIndex)))
										continue;

									const std::shared_ptr<CLayerTiles> pLayerTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
									CIntRect LayerRect;
									pLayerTiles->Convert(r, &LayerRect);
									pLayerTiles->Clamp(&LayerRect);
									if(!pLayerTiles->HasContentInRect(LayerRect))
										continue;

									Grabs += pLayerTiles->BrushGrab(m_pBrush.get(), r);
								}
							}

							if(Grabs == 0)
								m_pBrush->Clear();

							Map()->DeselectQuads();
							Map()->DeselectQuadPoints();
						}
					}
					else
					{
						if(NumEditLayers > 0)
						{
							apEditLayers[0].second->BrushSelecting(r);
						}
						Ui()->MapScreen();
					}
				}
				else if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::BRUSH_PAINT)
				{
					if(!Ui()->MouseButton(0))
					{
						for(size_t k = 0; k < NumEditLayers; k++)
						{
							std::shared_ptr<CLayer> pBrush = m_pBrush->IsEmpty() ? nullptr : FindBrushLayer(m_pBrush, apEditLayers[k].second, m_pBrush->m_vpLayers.size() != NumEditLayers ? 0 : k);
							apEditLayers[k].second->FillSelection(m_pBrush->IsEmpty(), pBrush.get(), r);
						}
						std::shared_ptr<IEditorAction> Action = std::make_shared<CEditorBrushDrawAction>(Map(), Map()->m_SelectedGroup);
						Map()->m_EditorHistory.RecordAction(Action);
					}
					else
					{
						if(NumEditLayers > 0)
						{
							apEditLayers[0].second->BrushSelecting(r);
						}
						Ui()->MapScreen();
					}
				}
			}
			else if(!DrawingToolsHandled)
			{
				if(Ui()->MouseButton(1))
				{
					m_pBrush->Clear();
				}

				if(!Input()->ModifierIsPressed() && Ui()->MouseButton(0) && Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::NONE && !m_QuadKnife.IsActive())
				{
					Ui()->SetActiveItem(MapView());

					if(m_pBrush->IsEmpty())
					{
						Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::BRUSH_GRAB;
					}
					else
					{
						Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::BRUSH_DRAW;
						for(size_t k = 0; k < NumEditLayers; k++)
						{
							std::shared_ptr<CLayer> pBrushLayer = FindBrushLayer(m_pBrush, apEditLayers[k].second, m_pBrush->m_vpLayers.size() != NumEditLayers ? 0 : k);
							if(!pBrushLayer || apEditLayers[k].second->m_Type != pBrushLayer->m_Type)
								continue;

							apEditLayers[k].second->BrushPlace(pBrushLayer.get(), vec2(wx, wy));
						}
					}

					std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
					if(Input()->ShiftIsPressed() && pLayer)
						Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::BRUSH_PAINT;
				}

				if(!m_pBrush->IsEmpty())
				{
					m_pBrush->m_OffsetX = -(int)wx;
					m_pBrush->m_OffsetY = -(int)wy;
					for(const auto &pLayer : m_pBrush->m_vpLayers)
					{
						if(pLayer->m_Type == LAYERTYPE_TILES)
						{
							m_pBrush->m_OffsetX = -(int)(wx / 32.0f) * 32;
							m_pBrush->m_OffsetY = -(int)(wy / 32.0f) * 32;
							break;
						}
					}

					std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();
					if(!m_ShowPicker && pGroup)
					{
						m_pBrush->m_OffsetX += pGroup->m_OffsetX;
						m_pBrush->m_OffsetY += pGroup->m_OffsetY;
						m_pBrush->m_ParallaxX = pGroup->m_ParallaxX;
						m_pBrush->m_ParallaxY = pGroup->m_ParallaxY;
						m_pBrush->Render();

						CUIRect BorderRect;
						BorderRect.x = 0.0f;
						BorderRect.y = 0.0f;
						m_pBrush->GetSize(&BorderRect.w, &BorderRect.h);
						BorderRect.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
					}
				}
			}
			else
			{
				if(!Ui()->MouseButton(0))
					Ui()->SetActiveItem(nullptr);
			}
		}

		// quad & sound editing
		{
			if(!m_ShowPicker && m_pBrush->IsEmpty())
			{
				// fetch layers
				std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();
				if(pGroup)
					pGroup->MapScreen();

				for(size_t k = 0; k < NumEditLayers; k++)
				{
					auto &[LayerIndex, pEditLayer] = apEditLayers[k];

					if(pEditLayer->m_Type == LAYERTYPE_QUADS)
					{
						std::shared_ptr<CLayerQuads> pLayer = std::static_pointer_cast<CLayerQuads>(pEditLayer);

						if(m_ActiveEnvelopePreview == EEnvelopePreview::NONE)
							m_ActiveEnvelopePreview = EEnvelopePreview::ALL;

						if(QuadKnife()->IsActive())
						{
							QuadKnife()->DoSlice();
						}
						else
						{
							UpdateHotQuadPoint(pLayer.get());

							Graphics()->TextureClear();
							Graphics()->QuadsBegin();
							for(size_t i = 0; i < pLayer->m_vQuads.size(); i++)
							{
								for(int v = 0; v < 4; v++)
									DoQuadPoint(LayerIndex, pLayer, &pLayer->m_vQuads[i], i, v);

								DoQuad(LayerIndex, pLayer, &pLayer->m_vQuads[i], i);
							}
							Graphics()->QuadsEnd();
						}
					}
					else if(pEditLayer->m_Type == LAYERTYPE_SOUNDS)
					{
						std::shared_ptr<CLayerSounds> pLayer = std::static_pointer_cast<CLayerSounds>(pEditLayer);

						UpdateHotSoundSource(pLayer.get());

						Graphics()->TextureClear();
						Graphics()->QuadsBegin();
						for(size_t i = 0; i < pLayer->m_vSources.size(); i++)
						{
							DoSoundSource(LayerIndex, &pLayer->m_vSources[i], i);
						}
						Graphics()->QuadsEnd();
					}
				}

				Ui()->MapScreen();
			}
		}

		// menu proof selection
		if(MapView()->ProofMode()->IsModeMenu() && !m_ShowPicker)
		{
			MapView()->ProofMode()->ResetMenuBackgroundPositions();
			const std::vector<vec2> &Positions = MapView()->ProofMode()->MenuBackgroundPositions();
			const int CurrentMenuProofIndex = MapView()->ProofMode()->CurrentMenuProofIndex();
			for(int i = 0; i < (int)Positions.size(); i++)
			{
				const void *pPositionId = &Positions[i];
				vec2 Pos = Positions[i];
				Pos += MapView()->GetWorldOffset() - Positions[CurrentMenuProofIndex];
				Pos.y -= 3.0f;

				if(distance(Pos, MapView()->MouseWorldNoParaPos()) <= 20.0f)
				{
					Ui()->SetHotItem(pPositionId);

					if(i != CurrentMenuProofIndex && Ui()->CheckActiveItem(pPositionId))
					{
						if(!Ui()->MouseButton(0))
						{
							MapView()->ProofMode()->SetCurrentMenuProofIndex(i);
							MapView()->SetWorldOffset(Positions[i]);
							Ui()->SetActiveItem(nullptr);
						}
					}
					else if(Ui()->HotItem() == pPositionId)
					{
						char aTooltipPrefix[32];
						str_copy(aTooltipPrefix, Localize("Switch proof position to", "Editor"));
						if(i == CurrentMenuProofIndex)
							str_copy(aTooltipPrefix, Localize("Current proof position at", "Editor"));

						char aNumBuf[8];
						if(i < (TILE_TIME_CHECKPOINT_LAST - TILE_TIME_CHECKPOINT_FIRST))
							str_format(aNumBuf, sizeof(aNumBuf), "#%d", i + 1);
						else
							aNumBuf[0] = '\0';

						char aTooltipPositions[128];
						str_format(aTooltipPositions, sizeof(aTooltipPositions), "%s %s", MapView()->ProofMode()->MenuBackgroundPositionName(i), aNumBuf);

						for(int k : MapView()->ProofMode()->MenuBackgroundCollisions(i))
						{
							if(k == CurrentMenuProofIndex)
								str_copy(aTooltipPrefix, Localize("Current proof position at", "Editor"));

							Pos = Positions[k];
							Pos += MapView()->GetWorldOffset() - Positions[CurrentMenuProofIndex];
							Pos.y -= 3.0f;

							if(distance(Pos, MapView()->MouseWorldNoParaPos()) > 20.0f)
								continue;

							if(i < (TILE_TIME_CHECKPOINT_LAST - TILE_TIME_CHECKPOINT_FIRST))
								str_format(aNumBuf, sizeof(aNumBuf), "#%d", k + 1);
							else
								aNumBuf[0] = '\0';

							char aTooltipPositionsCopy[128];
							str_copy(aTooltipPositionsCopy, aTooltipPositions);
							str_format(aTooltipPositions, sizeof(aTooltipPositions), "%s, %s %s", aTooltipPositionsCopy, MapView()->ProofMode()->MenuBackgroundPositionName(k), aNumBuf);
						}
						str_format(m_aTooltip, sizeof(m_aTooltip), "%s %s.", aTooltipPrefix, aTooltipPositions);

						if(Ui()->MouseButton(0))
							Ui()->SetActiveItem(pPositionId);
					}
					break;
				}
			}
		}

		if(!Input()->ModifierIsPressed() && m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr)
		{
			float PanSpeed = Input()->ShiftIsPressed() ? 200.0f : 64.0f;
			if(Input()->KeyPress(KEY_A))
				MapView()->OffsetWorld({-PanSpeed * MapView()->MouseWorldScale(), 0});
			else if(Input()->KeyPress(KEY_D))
				MapView()->OffsetWorld({PanSpeed * MapView()->MouseWorldScale(), 0});
			if(Input()->KeyPress(KEY_W))
				MapView()->OffsetWorld({0, -PanSpeed * MapView()->MouseWorldScale()});
			else if(Input()->KeyPress(KEY_S))
				MapView()->OffsetWorld({0, PanSpeed * MapView()->MouseWorldScale()});
		}
	}

	if(Ui()->CheckActiveItem(MapView()) && m_pContainerPanned == nullptr)
	{
		// release mouse
		if(!Ui()->MouseButton(0))
		{
			if(Map()->m_MapViewState.m_ActiveOp == CMapView::EActiveOp::BRUSH_DRAW)
			{
				std::shared_ptr<IEditorAction> pAction = std::make_shared<CEditorBrushDrawAction>(Map(), Map()->m_SelectedGroup);

				if(!pAction->IsEmpty()) // Avoid recording tile draw action when placing quads only
					Map()->m_EditorHistory.RecordAction(pAction);
			}

			Map()->m_MapViewState.m_ActiveOp = CMapView::EActiveOp::NONE;
			Ui()->SetActiveItem(nullptr);
		}
	}

	if(!m_ShowPicker && Map()->SelectedGroup() && Map()->SelectedGroup()->m_UseClipping)
	{
		std::shared_ptr<CLayerGroup> pGameGroup = Map()->m_pGameGroup;
		pGameGroup->MapScreen();

		CUIRect ClipRect;
		ClipRect.x = Map()->SelectedGroup()->m_ClipX;
		ClipRect.y = Map()->SelectedGroup()->m_ClipY;
		ClipRect.w = Map()->SelectedGroup()->m_ClipW;
		ClipRect.h = Map()->SelectedGroup()->m_ClipH;
		ClipRect.DrawOutline(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	}

	if(!m_ShowPicker)
		MapView()->ProofMode()->RenderScreenSizes();

	if(!m_ShowPicker && m_ShowEnvelopePreview && m_ActiveEnvelopePreview != EEnvelopePreview::NONE)
	{
		const std::shared_ptr<CLayer> pSelectedLayer = Map()->SelectedLayer(0);
		if(pSelectedLayer != nullptr && pSelectedLayer->m_Type == LAYERTYPE_QUADS)
		{
			DoQuadEnvelopes(static_cast<const CLayerQuads *>(pSelectedLayer.get()));
		}
		m_ActiveEnvelopePreview = EEnvelopePreview::NONE;
	}

	Ui()->MapScreen();
}

void CMapView::UpdateMouseWorld()
{
	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	const vec2 UpdatedMouseDelta = Ui()->UpdatedMouseDelta();

	// fix correct world x and y
	const std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();
	if(pGroup)
	{
		float aPoints[4];
		pGroup->Mapping(aPoints);

		float WorldWidth = aPoints[2] - aPoints[0];
		float WorldHeight = aPoints[3] - aPoints[1];

		Map()->m_MapViewState.m_MouseWorldScale = WorldWidth / Graphics()->WindowWidth();

		Map()->m_MapViewState.m_MouseWorldPos.x = aPoints[0] + WorldWidth * (UpdatedMousePos.x / Graphics()->WindowWidth());
		Map()->m_MapViewState.m_MouseWorldPos.y = aPoints[1] + WorldHeight * (UpdatedMousePos.y / Graphics()->WindowHeight());
		Map()->m_MapViewState.m_MouseDeltaWorld.x = UpdatedMouseDelta.x * (WorldWidth / Graphics()->WindowWidth());
		Map()->m_MapViewState.m_MouseDeltaWorld.y = UpdatedMouseDelta.y * (WorldHeight / Graphics()->WindowHeight());
	}
	else
	{
		Map()->m_MapViewState.m_MouseWorldPos = vec2(-1.0f, -1.0f);
		Map()->m_MapViewState.m_MouseDeltaWorld = vec2(0.0f, 0.0f);
	}

	Map()->m_MapViewState.m_MouseWorldNoParaPos = vec2(-1.0f, -1.0f);
	for(const std::shared_ptr<CLayerGroup> &pGameGroup : Map()->m_vpGroups)
	{
		if(!pGameGroup->m_GameGroup)
			continue;

		float aPoints[4];
		pGameGroup->Mapping(aPoints);

		float WorldWidth = aPoints[2] - aPoints[0];
		float WorldHeight = aPoints[3] - aPoints[1];

		Map()->m_MapViewState.m_MouseWorldNoParaPos.x = aPoints[0] + WorldWidth * (UpdatedMousePos.x / Graphics()->WindowWidth());
		Map()->m_MapViewState.m_MouseWorldNoParaPos.y = aPoints[1] + WorldHeight * (UpdatedMousePos.y / Graphics()->WindowHeight());
	}
}

void CMapView::ResetMouseDeltaWorld()
{
	Map()->m_MapViewState.m_MouseDeltaWorld = vec2(0.0f, 0.0f);
}

float CMapView::MouseWorldScale() const
{
	return Map()->m_MapViewState.m_MouseWorldScale;
}

vec2 CMapView::MouseDeltaWorld() const
{
	return Map()->m_MapViewState.m_MouseDeltaWorld;
}

vec2 CMapView::MouseWorldPos() const
{
	return Map()->m_MapViewState.m_MouseWorldPos;
}

vec2 CMapView::MouseWorldNoParaPos() const
{
	return Map()->m_MapViewState.m_MouseWorldNoParaPos;
}

void CMapView::ResetZoom()
{
	SetEditorOffset({0, 0});
	Zoom()->SetValue(100.0f);
}

float CMapView::ScaleLength(float Value) const
{
	return GetWorldZoom() * Value;
}

void CMapView::ZoomMouseTarget(float ZoomFactor)
{
	// zoom to the current mouse position
	// get absolute mouse position
	float aPoints[4];
	Graphics()->MapScreenToWorld(
		GetWorldOffset().x, GetWorldOffset().y,
		100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GetWorldZoom(), aPoints);

	float WorldWidth = aPoints[2] - aPoints[0];
	float WorldHeight = aPoints[3] - aPoints[1];

	float MouseWorldX = aPoints[0] + WorldWidth * (Ui()->MouseX() / Ui()->Screen()->w);
	float MouseWorldY = aPoints[1] + WorldHeight * (Ui()->MouseY() / Ui()->Screen()->h);

	// adjust camera
	OffsetWorld((vec2(MouseWorldX, MouseWorldY) - GetWorldOffset()) * (1.0f - ZoomFactor));
}

void CMapView::UpdateZoom()
{
	float OldLevel = Zoom()->GetValue();
	bool UpdatedZoom = Zoom()->UpdateValue();
	Zoom()->SetValueRange(10.0f, g_Config.m_EdLimitMaxZoomLevel ? 2000.0f : std::numeric_limits<float>::max());
	float NewLevel = Zoom()->GetValue();
	if(UpdatedZoom && g_Config.m_EdZoomTarget)
		ZoomMouseTarget(NewLevel / OldLevel);
	Map()->m_MapViewState.m_WorldZoom = NewLevel / 100.0f;
}

CSmoothValue *CMapView::Zoom()
{
	return &Map()->m_MapViewState.m_Zoom;
}

const CSmoothValue *CMapView::Zoom() const
{
	return &Map()->m_MapViewState.m_Zoom;
}

CProofMode *CMapView::ProofMode()
{
	return &m_ProofMode;
}

const CProofMode *CMapView::ProofMode() const
{
	return &m_ProofMode;
}

CMapGrid *CMapView::MapGrid()
{
	return &m_MapGrid;
}

const CMapGrid *CMapView::MapGrid() const
{
	return &m_MapGrid;
}

void CMapView::OffsetWorld(vec2 Offset)
{
	Map()->m_MapViewState.m_WorldOffset += Offset;
}

void CMapView::OffsetEditor(vec2 Offset)
{
	Map()->m_MapViewState.m_EditorOffset += Offset;
}

void CMapView::SetWorldOffset(vec2 WorldOffset)
{
	Map()->m_MapViewState.m_WorldOffset = WorldOffset;
}

void CMapView::SetEditorOffset(vec2 EditorOffset)
{
	Map()->m_MapViewState.m_EditorOffset = EditorOffset;
}

vec2 CMapView::GetWorldOffset() const
{
	return Map()->m_MapViewState.m_WorldOffset;
}

vec2 CMapView::GetEditorOffset() const
{
	return Map()->m_MapViewState.m_EditorOffset;
}

float CMapView::GetWorldZoom() const
{
	return Map()->m_MapViewState.m_WorldZoom;
}
