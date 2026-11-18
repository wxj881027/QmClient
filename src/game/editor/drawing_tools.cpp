#include "drawing_tools.h"

#include "editor.h"
#include "editor_actions.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/keys.h>

#include <game/client/lineinput.h>
#include <game/editor/mapitems.h>
#include <game/editor/mapitems/layer_front.h>
#include <game/editor/mapitems/layer_game.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>

namespace
{
	constexpr size_t MAX_PREVIEW_CELLS = 4096;
	constexpr int64_t MAX_PREVIEW_WORK = 32 * 1024;
	constexpr int64_t MAX_DRAW_WORK = 1024 * 1024;
	// 给镜像、线段误差和平滑取整留出整数和浮点精度余量。
	constexpr float MAX_DRAW_WORLD_COORDINATE = (1 << 20) * 32.0f;

	struct SButtonId
	{
		const void *m_pOwner;
		int m_Value;
	};

	float StableUnitRandom(int x, int y, int Salt)
	{
		unsigned Value = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)Salt * 83492791u;
		Value ^= Value >> 13;
		Value *= 1274126177u;
		Value ^= Value >> 16;
		return (Value & 0x00ffffff) / (float)0x01000000;
	}

	std::optional<vec2> DrawingMouseWorld(CEditor *pEditor, const CLayerGroup &Group, float (&aPoints)[4])
	{
		const int WindowWidth = pEditor->Graphics()->WindowWidth();
		const int WindowHeight = pEditor->Graphics()->WindowHeight();
		if(WindowWidth <= 0 || WindowHeight <= 0)
			return std::nullopt;

		Group.Mapping(aPoints);
		const vec2 MousePos = pEditor->Ui()->UpdatedMousePos();
		const float WorldWidth = aPoints[2] - aPoints[0];
		const float WorldHeight = aPoints[3] - aPoints[1];
		const float LayerX = aPoints[0] + WorldWidth * (MousePos.x / WindowWidth);
		const float LayerY = aPoints[1] + WorldHeight * (MousePos.y / WindowHeight);
		if(!(std::abs(LayerX) <= MAX_DRAW_WORLD_COORDINATE && std::abs(LayerY) <= MAX_DRAW_WORLD_COORDINATE))
			return std::nullopt;

		return vec2(LayerX, LayerY);
	}

	ivec2 LinePoint(const std::vector<ivec2> &vPath, size_t Index, bool Smooth)
	{
		const ivec2 Point = vPath[Index];
		if(!Smooth || Index == 0 || Index + 1 == vPath.size())
			return Point;

		const ivec2 Previous = vPath[Index - 1];
		const ivec2 Next = vPath[Index + 1];
		return ivec2(round_to_int((Previous.x + Point.x * 2.0f + Next.x) / 4.0f),
			round_to_int((Previous.y + Point.y * 2.0f + Next.y) / 4.0f));
	}

	int64_t LineRasterWork(const std::vector<ivec2> &vPath, bool Smooth, int Thickness, int64_t Budget)
	{
		const int64_t WorkPerPoint = Thickness * Thickness;
		if(vPath.size() > static_cast<size_t>(Budget / WorkPerPoint))
			return Budget + 1;
		if(vPath.size() <= 1)
			return vPath.empty() ? 0 : WorkPerPoint;

		int64_t Work = 0;
		ivec2 From = vPath.front();
		for(size_t i = 1; i < vPath.size(); ++i)
		{
			const ivec2 To = LinePoint(vPath, i, Smooth);
			const int Steps = maximum(absolute(To.x - From.x), absolute(To.y - From.y)) + 1;
			Work += Steps * WorkPerPoint;
			if(Work > Budget)
				break;
			From = To;
		}
		return Work;
	}

	ELayerKind TileLayerKind(const CLayerTiles &Layer)
	{
		return LayerKindFromFlags(Layer.m_HasGame, Layer.m_HasFront, Layer.m_HasTele, Layer.m_HasSpeedup, Layer.m_HasSwitch, Layer.m_HasTune);
	}

	const CLayerTiles *UsableTileBrush(const std::shared_ptr<CLayer> &pLayer)
	{
		if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
			return nullptr;
		const auto *pTiles = static_cast<const CLayerTiles *>(pLayer.get());
		return pTiles->m_Width > 0 && pTiles->m_Height > 0 ? pTiles : nullptr;
	}

	bool IsCurrentDrawingTarget(const CEditorMap &Map, const CLayerTiles &Layer)
	{
		return (!Layer.m_HasGame || &Layer == Map.m_pGameLayer.get()) &&
		       (!Layer.m_HasFront || &Layer == Map.m_pFrontLayer.get()) &&
		       (!Layer.m_HasTele || &Layer == Map.m_pTeleLayer.get()) &&
		       (!Layer.m_HasSpeedup || &Layer == Map.m_pSpeedupLayer.get()) &&
		       (!Layer.m_HasSwitch || &Layer == Map.m_pSwitchLayer.get()) &&
		       (!Layer.m_HasTune || &Layer == Map.m_pTuneLayer.get());
	}
}

CEditorDrawingTools::CEditorDrawingTools()
{
	Reset();
}

void CEditorDrawingTools::Reset()
{
	m_Tool = ETool::NONE;
	m_Shape = EShape::RECT;
	m_LineMode = ELineMode::SHARP;
	m_Symmetry = ESymmetry::OFF;

	m_ShapeOutline = false;
	m_ShapeThickness = 1;
	m_ShapeNgonSides = 6;
	m_LineThickness = 1;
	m_FadeAngle = 0;
	m_FadeStart = 100;
	m_FadeEnd = 0;
	m_FadeSoftness = 0;
	m_FadeRandomness = 0;
	m_FadeAir = false;

	CancelDrawing();
}

void CEditorDrawingTools::CancelDrawing()
{
	m_Drawing = false;
	m_Drag.m_vPath.clear();
	if(m_Drag.m_vPath.capacity() > static_cast<size_t>(MAX_PREVIEW_WORK))
		m_Drag.m_vPath = std::vector<ivec2>();
	m_Drag.m_vPreviewCells = std::vector<SCell>();
	m_Drag.m_PreviewCellsClipped = false;
}

void CEditorDrawingTools::SetTool(ETool Tool)
{
	CancelDrawing();
	m_Tool = m_Tool == Tool ? ETool::NONE : Tool;
}

const char *CEditorDrawingTools::ToolName() const
{
	return ToolName(m_Tool);
}

const char *CEditorDrawingTools::ToolName(ETool Tool) const
{
	switch(Tool)
	{
	case ETool::FILL: return Localize("Fill", "Editor");
	case ETool::SHAPE: return Localize("Shape", "Editor");
	case ETool::LINE: return Localize("Line", "Editor");
	case ETool::FADE: return Localize("Fade", "Editor");
	case ETool::NONE: break;
	}
	return Localize("Drawing tools", "Editor");
}

const char *CEditorDrawingTools::ActionName() const
{
	const ETool Tool = m_Drawing ? m_Drag.m_Tool : m_Tool;
	switch(Tool)
	{
	case ETool::FILL: return Localize("Drawing tool: Fill", "Editor");
	case ETool::SHAPE:
		switch(m_Shape)
		{
		case EShape::RECT: return Localize("Drawing tool: Rectangle", "Editor");
		case EShape::ELLIPSE: return Localize("Drawing tool: Ellipse", "Editor");
		case EShape::TRIANGLE: return Localize("Drawing tool: Triangle", "Editor");
		case EShape::NGON: return Localize("Drawing tool: N-gon", "Editor");
		}
		break;
	case ETool::LINE: return Localize("Drawing tool: Line", "Editor");
	case ETool::FADE: return Localize("Drawing tool: Fade", "Editor");
	case ETool::NONE: break;
	}
	return Localize("Drawing tools", "Editor");
}

bool CEditorDrawingTools::HandleWheelInput(CEditor *pEditor, CUIRect View)
{
	if(pEditor->m_Dialog != DIALOG_NONE || CLineInput::GetActiveInput() != nullptr || pEditor->Ui()->IsPopupHovered() || !pEditor->Ui()->MouseInside(&View) || !pEditor->Input()->ModifierIsPressed())
		return false;

	int Delta = 0;
	if(pEditor->Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
		Delta = 1;
	else if(pEditor->Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
		Delta = -1;
	if(Delta == 0)
		return false;

	if(m_Tool == ETool::SHAPE)
	{
		m_ShapeThickness = std::clamp(m_ShapeThickness + Delta, 1, 16);
		str_format(pEditor->m_aTooltip, sizeof(pEditor->m_aTooltip), Localize("Shape thickness: %d", "Editor"), m_ShapeThickness);
		CancelDrawing();
		return true;
	}
	if(m_Tool == ETool::LINE)
	{
		m_LineThickness = std::clamp(m_LineThickness + Delta, 1, 16);
		str_format(pEditor->m_aTooltip, sizeof(pEditor->m_aTooltip), Localize("Line thickness: %d", "Editor"), m_LineThickness);
		CancelDrawing();
		return true;
	}

	return false;
}

void CEditorDrawingTools::DoToolButton(CEditor *pEditor, CUIRect *pToolbar, ETool Tool, const char *pLabel, const char *pTooltip)
{
	CUIRect Button;
	static SButtonId s_aToolIds[5];
	const int ToolIndex = static_cast<int>(Tool);
	s_aToolIds[ToolIndex] = {this, ToolIndex};
	pToolbar->VSplitLeft(40.0f, &Button, pToolbar);
	if(pEditor->DoButton_Editor(&s_aToolIds[ToolIndex], pLabel, m_Tool == Tool, &Button, BUTTONFLAG_LEFT, pTooltip))
		SetTool(Tool);
	pToolbar->VSplitLeft(3.0f, nullptr, pToolbar);
}

void CEditorDrawingTools::DoSmallButton(CEditor *pEditor, CUIRect *pToolbar, const void *pId, const char *pLabel, bool Active, const char *pTooltip, const std::function<void()> &OnClick)
{
	CUIRect Button;
	const float Width = std::clamp(18.0f + str_length(pLabel) * 5.0f, 26.0f, 58.0f);
	pToolbar->VSplitLeft(Width, &Button, pToolbar);
	if(pEditor->DoButton_Editor(pId, pLabel, Active, &Button, BUTTONFLAG_LEFT, pTooltip))
	{
		CancelDrawing();
		OnClick();
	}
	pToolbar->VSplitLeft(2.0f, nullptr, pToolbar);
}

int CEditorDrawingTools::DoValue(CEditor *pEditor, CUIRect *pToolbar, const void *pId, const char *pLabel, int Value, int Min, int Max, int Step, float Scale, const char *pTooltip, bool IsDegree)
{
	CUIRect Button;
	pToolbar->VSplitLeft(46.0f, &Button, pToolbar);
	auto Result = pEditor->UiDoValueSelector(const_cast<void *>(pId), &Button, pLabel, Value, Min, Max, Step, Scale, pTooltip, IsDegree, false, IGraphics::CORNER_ALL);
	pToolbar->VSplitLeft(2.0f, nullptr, pToolbar);
	if(Result.m_State != EEditState::NONE)
		CancelDrawing();
	return Result.m_Value;
}

void CEditorDrawingTools::RenderToolbar(CEditor *pEditor, CUIRect *pToolbar)
{
	static int s_ShapeRectButton;
	static int s_ShapeEllipseButton;
	static int s_ShapeTriangleButton;
	static int s_ShapeNgonButton;
	static int s_LineSmoothButton;
	static int s_LineSharpButton;

	DoToolButton(pEditor, pToolbar, ETool::FILL, Localize("Fill", "Editor"), Localize("Drawing tool: Drag a rectangle to fill it with the current tile. An empty brush erases to air.", "Editor"));
	DoToolButton(pEditor, pToolbar, ETool::SHAPE, Localize("Shape", "Editor"), Localize("Drawing tool: Drag to create a rectangle, ellipse, triangle or N-gon.", "Editor"));
	DoToolButton(pEditor, pToolbar, ETool::LINE, Localize("Line", "Editor"), Localize("Drawing tool: Draw a continuous line along the mouse path.", "Editor"));
	DoToolButton(pEditor, pToolbar, ETool::FADE, Localize("Fade", "Editor"), Localize("Drawing tool: Probabilistically scatter tiles with a fade inside the dragged rectangle.", "Editor"));

	const char *pSym = Localize("Symmetry: Off", "Editor");
	if(m_Symmetry == ESymmetry::X)
		pSym = Localize("Symmetry: X", "Editor");
	else if(m_Symmetry == ESymmetry::Y)
		pSym = Localize("Symmetry: Y", "Editor");
	else if(m_Symmetry == ESymmetry::XY)
		pSym = Localize("Symmetry: XY", "Editor");
	DoSmallButton(pEditor, pToolbar, &m_Symmetry, pSym, m_Symmetry != ESymmetry::OFF, Localize("Symmetry drawing: Off -> X -> Y -> XY. The center is the tile coordinate where the mouse was pressed.", "Editor"), [&]() {
		m_Symmetry = static_cast<ESymmetry>((static_cast<int>(m_Symmetry) + 1) % 4);
	});

	if(m_Tool == ETool::SHAPE)
	{
		DoSmallButton(pEditor, pToolbar, &s_ShapeRectButton, Localize("Rectangle", "Editor"), m_Shape == EShape::RECT, Localize("Rectangle shape.", "Editor"), [&]() { m_Shape = EShape::RECT; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeEllipseButton, Localize("Ellipse", "Editor"), m_Shape == EShape::ELLIPSE, Localize("Ellipse shape.", "Editor"), [&]() { m_Shape = EShape::ELLIPSE; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeTriangleButton, Localize("Triangle", "Editor"), m_Shape == EShape::TRIANGLE, Localize("Triangle shape.", "Editor"), [&]() { m_Shape = EShape::TRIANGLE; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeNgonButton, Localize("N-gon", "Editor"), m_Shape == EShape::NGON, Localize("N-gon shape.", "Editor"), [&]() { m_Shape = EShape::NGON; });
		DoSmallButton(pEditor, pToolbar, &m_ShapeOutline, Localize("Outline", "Editor"), m_ShapeOutline, Localize("Toggle solid/outline drawing.", "Editor"), [&]() { m_ShapeOutline = !m_ShapeOutline; });
		m_ShapeThickness = DoValue(pEditor, pToolbar, &m_ShapeThickness, Localize("Thickness ", "Editor"), m_ShapeThickness, 1, 16, 1, 2.0f, Localize("Outline thickness.", "Editor"));
		if(m_Shape == EShape::NGON)
			m_ShapeNgonSides = DoValue(pEditor, pToolbar, &m_ShapeNgonSides, "N ", m_ShapeNgonSides, 3, 16, 1, 2.0f, Localize("Number of sides for the N-gon.", "Editor"));
	}
	else if(m_Tool == ETool::LINE)
	{
		m_LineThickness = DoValue(pEditor, pToolbar, &m_LineThickness, Localize("Thickness ", "Editor"), m_LineThickness, 1, 16, 1, 2.0f, Localize("Line thickness.", "Editor"));
		DoSmallButton(pEditor, pToolbar, &s_LineSmoothButton, Localize("Smooth", "Editor"), m_LineMode == ELineMode::SMOOTH, Localize("Draw after lightly smoothing the mouse path.", "Editor"), [&]() { m_LineMode = ELineMode::SMOOTH; });
		DoSmallButton(pEditor, pToolbar, &s_LineSharpButton, Localize("Polyline", "Editor"), m_LineMode == ELineMode::SHARP, Localize("Keep the mouse path as a polyline.", "Editor"), [&]() { m_LineMode = ELineMode::SHARP; });
	}
	else if(m_Tool == ETool::FADE)
	{
		m_FadeAngle = DoValue(pEditor, pToolbar, &m_FadeAngle, Localize("Angle ", "Editor"), m_FadeAngle, 0, 359, 1, 2.0f, Localize("Fade angle.", "Editor"), true);
		m_FadeStart = DoValue(pEditor, pToolbar, &m_FadeStart, Localize("Start% ", "Editor"), m_FadeStart, 0, 100, 1, 2.0f, Localize("Start probability strength.", "Editor"));
		m_FadeEnd = DoValue(pEditor, pToolbar, &m_FadeEnd, Localize("End% ", "Editor"), m_FadeEnd, 0, 100, 1, 2.0f, Localize("End probability strength.", "Editor"));
		m_FadeSoftness = DoValue(pEditor, pToolbar, &m_FadeSoftness, Localize("Softness ", "Editor"), m_FadeSoftness, 0, 100, 1, 2.0f, Localize("Edge softness strength.", "Editor"));
		m_FadeRandomness = DoValue(pEditor, pToolbar, &m_FadeRandomness, Localize("Randomness ", "Editor"), m_FadeRandomness, 0, 100, 1, 2.0f, Localize("Random perturbation strength.", "Editor"));
		DoSmallButton(pEditor, pToolbar, &m_FadeAir, Localize("Air", "Editor"), m_FadeAir, Localize("Allow the fade tool to randomly erase to air.", "Editor"), [&]() { m_FadeAir = !m_FadeAir; });
	}
}

bool CEditorDrawingTools::IsTileToolTarget(const CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers) const
{
	if(NumEditLayers == 0 || pEditor->m_ShowPicker)
		return false;

	for(size_t i = 0; i < NumEditLayers; ++i)
	{
		const std::shared_ptr<CLayer> &pLayer = pEditLayers[i].second;
		if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
			return false;
	}
	return true;
}

bool CEditorDrawingTools::WantsQuickFill(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers) const
{
	return !m_Drawing && IsTileToolTarget(pEditor, pEditLayers, NumEditLayers) && pEditor->Ui()->MouseButton(0) && pEditor->Input()->AltIsPressed() && !pEditor->Input()->ModifierIsPressed() && !pEditor->Input()->ShiftIsPressed();
}

void CEditorDrawingTools::BeginDrag(CEditor *pEditor, int TileX, int TileY, ETool Tool)
{
	m_Drawing = true;
	m_Drag.m_Start = ivec2(TileX, TileY);
	m_Drag.m_Current = m_Drag.m_Start;
	m_Drag.m_Tool = Tool;
	m_Drag.m_vPath.clear();
	m_Drag.m_vPath.push_back(m_Drag.m_Start);
	RebuildPreview(pEditor, false);
}

void CEditorDrawingTools::UpdateDrag(CEditor *pEditor, int TileX, int TileY)
{
	if(!m_Drawing)
		return;

	const ivec2 Current(TileX, TileY);
	if(Current == m_Drag.m_Current)
		return;
	// 保留一个超限位置，让松手时拒绝整笔。
	if(m_Drag.m_Tool == ETool::LINE && m_Drag.m_vPath.size() <= static_cast<size_t>(MAX_DRAW_WORK))
		m_Drag.m_vPath.push_back(Current);

	m_Drag.m_Current = Current;
	RebuildPreview(pEditor, false);
}

void CEditorDrawingTools::FinishDrag(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers)
{
	if(!m_Drawing)
		return;

	RebuildPreview(pEditor, true);
	const bool Submitted = SubmitCells(pEditor, pEditLayers, NumEditLayers, m_Drag.m_vPreviewCells);
	const char *pActionName = ActionName();
	CancelDrawing();

	if(Submitted)
	{
		std::shared_ptr<IEditorAction> pAction = std::make_shared<CEditorBrushDrawAction>(pEditor->Map(), pEditor->Map()->m_SelectedGroup);
		if(!pAction->IsEmpty())
			pEditor->Map()->m_EditorHistory.RecordAction(pAction, pActionName);
	}
}

bool CEditorDrawingTools::HandleMapEditorInput(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, bool Inside)
{
	CUi *pUi = pEditor->Ui();
	const ETool ActiveTool = m_Drawing ? m_Drag.m_Tool : (WantsQuickFill(pEditor, pEditLayers, NumEditLayers) ? ETool::FILL : m_Tool);
	const bool CanUseTool = ActiveTool != ETool::NONE && IsTileToolTarget(pEditor, pEditLayers, NumEditLayers) && pEditor->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && !pUi->IsPopupOpen() && (!pEditor->Input()->ModifierIsPressed() || ActiveTool == ETool::FILL);
	if(!CanUseTool || (m_Drawing && !pUi->CheckActiveItem(this)))
	{
		const bool CancelledDrag = m_Drawing;
		CancelDrawing();
		if(pUi->IsActiveItem(this))
			pUi->SetActiveItem(nullptr);
		return CancelledDrag;
	}

	if(!Inside && !m_Drawing)
		return false;

	const auto pGroup = pEditor->Map()->SelectedGroup();
	float aPoints[4];
	const auto World = pGroup ? DrawingMouseWorld(pEditor, *pGroup, aPoints) : std::nullopt;
	if(!World)
	{
		if(m_Drawing && !pUi->MouseButton(0))
		{
			CancelDrawing();
			pUi->SetActiveItem(nullptr);
		}
		return true;
	}

	const auto &Layer = static_cast<const CLayerTiles &>(*pEditLayers[0].second);
	const ivec2 Tile(Layer.ConvertX(World->x), Layer.ConvertY(World->y));
	if(m_Drawing)
	{
		UpdateDrag(pEditor, Tile.x, Tile.y);
		if(!pUi->MouseButton(0))
		{
			pUi->SetActiveItem(nullptr);
			FinishDrag(pEditor, pEditLayers, NumEditLayers);
		}
		return true;
	}

	if(Inside)
	{
		char aTooltip[128];
		str_format(aTooltip, sizeof(aTooltip), Localize("%s: Hold the left mouse button to draw. Alt+left click temporarily fills. Ctrl+mouse wheel changes thickness.", "Editor"), ToolName(ActiveTool));
		str_copy(pEditor->m_aTooltip, aTooltip);
	}

	if(Inside && pUi->CheckActiveItem(nullptr) && pUi->MouseButton(0) && !pEditor->QuadKnife()->IsActive())
	{
		pUi->SetActiveItem(this);
		BeginDrag(pEditor, Tile.x, Tile.y, ActiveTool);
		return true;
	}

	return Inside;
}

void CEditorDrawingTools::AddCell(std::vector<SCell> &vCells, int x, int y) const
{
	vCells.push_back({x, y});
}

void CEditorDrawingTools::AddThickCell(std::vector<SCell> &vCells, int x, int y, int Thickness) const
{
	if(Thickness <= 1)
	{
		AddCell(vCells, x, y);
		return;
	}

	const int MinOffset = -(Thickness / 2);
	const int MaxOffset = MinOffset + Thickness - 1;
	const float Radius = Thickness * 0.5f;
	const float RadiusSquared = Radius * Radius + 0.25f;
	for(int OffsetY = MinOffset; OffsetY <= MaxOffset; ++OffsetY)
	{
		for(int OffsetX = MinOffset; OffsetX <= MaxOffset; ++OffsetX)
		{
			if(Thickness <= 2 || OffsetX * OffsetX + OffsetY * OffsetY <= RadiusSquared)
				AddCell(vCells, x + OffsetX, y + OffsetY);
		}
	}
}

void CEditorDrawingTools::AddLine(std::vector<SCell> &vCells, ivec2 From, ivec2 To, int Thickness) const
{
	int X0 = From.x;
	int Y0 = From.y;
	const int X1 = To.x;
	const int Y1 = To.y;
	const int DeltaX = absolute(X1 - X0);
	const int StepX = X0 < X1 ? 1 : -1;
	const int DeltaY = -absolute(Y1 - Y0);
	const int StepY = Y0 < Y1 ? 1 : -1;
	int Error = DeltaX + DeltaY;

	while(true)
	{
		AddThickCell(vCells, X0, Y0, Thickness);
		if(X0 == X1 && Y0 == Y1)
			break;

		const int Error2 = 2 * Error;
		if(Error2 >= DeltaY)
		{
			Error += DeltaY;
			X0 += StepX;
		}
		if(Error2 <= DeltaX)
		{
			Error += DeltaX;
			Y0 += StepY;
		}
	}
}

void CEditorDrawingTools::BuildFillCells(std::vector<SCell> &vCells) const
{
	const int MinX = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int MaxX = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int MinY = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const int MaxY = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	for(int y = MinY; y <= MaxY; ++y)
		for(int x = MinX; x <= MaxX; ++x)
			AddCell(vCells, x, y);
}

void CEditorDrawingTools::BuildShapeCells(std::vector<SCell> &vCells) const
{
	if(m_Shape == EShape::RECT)
	{
		const int MinX = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int MaxX = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int MinY = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		const int MaxY = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		if(!m_ShapeOutline)
		{
			for(int y = MinY; y <= MaxY; ++y)
				for(int x = MinX; x <= MaxX; ++x)
					AddCell(vCells, x, y);
		}
		else
		{
			for(int Thickness = 0; Thickness < m_ShapeThickness; ++Thickness)
			{
				const int Left = MinX + Thickness;
				const int Right = MaxX - Thickness;
				const int Top = MinY + Thickness;
				const int Bottom = MaxY - Thickness;
				if(Left > Right || Top > Bottom)
					break;
				for(int x = Left; x <= Right; ++x)
				{
					AddCell(vCells, x, Top);
					AddCell(vCells, x, Bottom);
				}
				for(int y = Top; y <= Bottom; ++y)
				{
					AddCell(vCells, Left, y);
					AddCell(vCells, Right, y);
				}
			}
		}
		return;
	}

	const float MinX = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const float MaxX = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const float MinY = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const float MaxY = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const float CenterX = (MinX + MaxX) * 0.5f;
	const float CenterY = (MinY + MaxY) * 0.5f;
	const float RadiusX = maximum(0.5f, (MaxX - MinX + 1.0f) * 0.5f);
	const float RadiusY = maximum(0.5f, (MaxY - MinY + 1.0f) * 0.5f);

	if(m_Shape == EShape::ELLIPSE)
	{
		for(int y = (int)MinY; y <= (int)MaxY; ++y)
		{
			for(int x = (int)MinX; x <= (int)MaxX; ++x)
			{
				const float Nx = (x - CenterX) / RadiusX;
				const float Ny = (y - CenterY) / RadiusY;
				const float Dist = Nx * Nx + Ny * Ny;
				if(!m_ShapeOutline)
				{
					if(Dist <= 1.0f)
						AddCell(vCells, x, y);
				}
				else
				{
					const float InnerX = maximum(0.5f, RadiusX - m_ShapeThickness);
					const float InnerY = maximum(0.5f, RadiusY - m_ShapeThickness);
					const float InnerNx = (x - CenterX) / InnerX;
					const float InnerNy = (y - CenterY) / InnerY;
					if(Dist <= 1.0f && InnerNx * InnerNx + InnerNy * InnerNy >= 1.0f)
						AddCell(vCells, x, y);
				}
			}
		}
		return;
	}

	std::vector<ivec2> vPoints;
	if(m_Shape == EShape::TRIANGLE)
	{
		vPoints.push_back(ivec2(round_to_int(CenterX), (int)MinY));
		vPoints.push_back(ivec2((int)MinX, (int)MaxY));
		vPoints.push_back(ivec2((int)MaxX, (int)MaxY));
	}
	else
	{
		const int Sides = std::clamp(m_ShapeNgonSides, 3, 16);
		for(int i = 0; i < Sides; ++i)
		{
			const float Angle = -pi / 2.0f + 2.0f * pi * i / Sides;
			vPoints.push_back(ivec2(round_to_int(CenterX + std::cos(Angle) * RadiusX), round_to_int(CenterY + std::sin(Angle) * RadiusY)));
		}
	}

	for(size_t i = 0; i < vPoints.size(); ++i)
		AddLine(vCells, vPoints[i], vPoints[(i + 1) % vPoints.size()], m_ShapeOutline ? m_ShapeThickness : 1);

	if(!m_ShapeOutline)
	{
		for(int y = (int)MinY; y <= (int)MaxY; ++y)
		{
			for(int x = (int)MinX; x <= (int)MaxX; ++x)
			{
				bool Inside = false;
				for(size_t i = 0, j = vPoints.size() - 1; i < vPoints.size(); j = i++)
				{
					const ivec2 &Pi = vPoints[i];
					const ivec2 &Pj = vPoints[j];
					if(((Pi.y > y) != (Pj.y > y)) && (x < static_cast<float>(Pj.x - Pi.x) * (y - Pi.y) / (Pj.y - Pi.y) + Pi.x))
						Inside = !Inside;
				}
				if(Inside)
					AddCell(vCells, x, y);
			}
		}
	}
}

void CEditorDrawingTools::BuildLineCells(std::vector<SCell> &vCells) const
{
	if(m_Drag.m_vPath.empty())
		return;

	const bool Smooth = m_LineMode == ELineMode::SMOOTH;
	ivec2 From = m_Drag.m_vPath.front();
	if(m_Drag.m_vPath.size() == 1)
		AddThickCell(vCells, From.x, From.y, m_LineThickness);

	for(size_t i = 1; i < m_Drag.m_vPath.size(); ++i)
	{
		const ivec2 To = LinePoint(m_Drag.m_vPath, i, Smooth);
		AddLine(vCells, From, To, m_LineThickness);
		From = To;
	}
}

void CEditorDrawingTools::BuildFadeCells(std::vector<SCell> &vCells) const
{
	const int MinX = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int MaxX = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int MinY = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const int MaxY = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const float Angle = m_FadeAngle * pi / 180.0f;
	const float DirX = std::cos(Angle);
	const float DirY = std::sin(Angle);
	const float aCorners[] = {
		MinX * DirX + MinY * DirY,
		MaxX * DirX + MinY * DirY,
		MinX * DirX + MaxY * DirY,
		MaxX * DirX + MaxY * DirY,
	};
	const float MinProjection = *std::min_element(std::begin(aCorners), std::end(aCorners));
	const float MaxProjection = *std::max_element(std::begin(aCorners), std::end(aCorners));
	const float ProjectionRange = maximum(1.0f, MaxProjection - MinProjection);

	for(int y = MinY; y <= MaxY; ++y)
	{
		for(int x = MinX; x <= MaxX; ++x)
		{
			float t = (x * DirX + y * DirY - MinProjection) / ProjectionRange;
			t = std::clamp(t, 0.0f, 1.0f);
			float Probability = mix((float)m_FadeStart, (float)m_FadeEnd, t) / 100.0f;
			if(m_FadeSoftness > 0)
			{
				const float Edge = minimum(minimum((float)(x - MinX + 1), (float)(MaxX - x + 1)), minimum((float)(y - MinY + 1), (float)(MaxY - y + 1)));
				const float SoftRange = 1.0f + m_FadeSoftness / 20.0f;
				Probability *= std::clamp(Edge / SoftRange, 0.0f, 1.0f);
			}
			if(m_FadeRandomness > 0)
				Probability += (StableUnitRandom(x, y, m_FadeAngle) * 2.0f - 1.0f) * (m_FadeRandomness / 100.0f);
			Probability = std::clamp(Probability, 0.0f, 1.0f);
			if(StableUnitRandom(x, y, m_FadeStart * 257 + m_FadeEnd) < Probability)
				AddCell(vCells, x, y);
		}
	}
}

void CEditorDrawingTools::ApplySymmetry(std::vector<SCell> &vCells) const
{
	if(m_Symmetry == ESymmetry::OFF)
		return;

	const size_t NumOriginal = vCells.size();
	for(size_t i = 0; i < NumOriginal; ++i)
	{
		const SCell Cell = vCells[i];
		if(m_Symmetry == ESymmetry::X || m_Symmetry == ESymmetry::XY)
			AddCell(vCells, m_Drag.m_Start.x - (Cell.m_X - m_Drag.m_Start.x), Cell.m_Y);
		if(m_Symmetry == ESymmetry::Y || m_Symmetry == ESymmetry::XY)
			AddCell(vCells, Cell.m_X, m_Drag.m_Start.y - (Cell.m_Y - m_Drag.m_Start.y));
		if(m_Symmetry == ESymmetry::XY)
			AddCell(vCells, m_Drag.m_Start.x - (Cell.m_X - m_Drag.m_Start.x), m_Drag.m_Start.y - (Cell.m_Y - m_Drag.m_Start.y));
	}
}

void CEditorDrawingTools::SortUniqueCells(std::vector<SCell> &vCells) const
{
	std::sort(vCells.begin(), vCells.end(), [](const SCell &Lhs, const SCell &Rhs) {
		if(Lhs.m_Y != Rhs.m_Y)
			return Lhs.m_Y < Rhs.m_Y;
		return Lhs.m_X < Rhs.m_X;
	});
	vCells.erase(std::unique(vCells.begin(), vCells.end(), [](const SCell &Lhs, const SCell &Rhs) { return Lhs.m_X == Rhs.m_X && Lhs.m_Y == Rhs.m_Y; }), vCells.end());
}

void CEditorDrawingTools::RebuildPreview(CEditor *pEditor, bool Commit)
{
	const ETool BuildTool = m_Drag.m_Tool;
	const int64_t Width = absolute(m_Drag.m_Start.x - m_Drag.m_Current.x) + 1;
	const int64_t Height = absolute(m_Drag.m_Start.y - m_Drag.m_Current.y) + 1;
	const int64_t Area = Width * Height;
	const bool SolidRectangle = BuildTool == ETool::FILL || (BuildTool == ETool::SHAPE && m_Shape == EShape::RECT && !m_ShapeOutline);
	if(!Commit && SolidRectangle && Area > static_cast<int64_t>(MAX_PREVIEW_CELLS))
	{
		m_Drag.m_vPreviewCells.clear();
		m_Drag.m_PreviewCellsClipped = true;
		return;
	}

	// 按最坏迭代次数检查，连同跳过的点、线宽和还没去重的镜像一起计入。
	const int Copies = m_Symmetry == ESymmetry::XY ? 4 : (m_Symmetry == ESymmetry::OFF ? 1 : 2);
	const int64_t Budget = (Commit ? MAX_DRAW_WORK : MAX_PREVIEW_WORK) / Copies;
	int64_t Work = Area;
	void (CEditorDrawingTools::*pBuild)(std::vector<SCell> &) const = nullptr;
	switch(BuildTool)
	{
	case ETool::FILL:
		pBuild = &CEditorDrawingTools::BuildFillCells;
		break;
	case ETool::SHAPE:
		pBuild = &CEditorDrawingTools::BuildShapeCells;
		if(m_Shape == EShape::RECT && m_ShapeOutline)
		{
			const int64_t Rings = std::min<int64_t>(m_ShapeThickness, (minimum(Width, Height) + 1) / 2);
			Work = 2 * Rings * (Width + Height);
		}
		else if(m_Shape == EShape::TRIANGLE || m_Shape == EShape::NGON)
		{
			const int Sides = m_Shape == EShape::TRIANGLE ? 3 : std::clamp(m_ShapeNgonSides, 3, 16);
			const int Thickness = m_ShapeOutline ? m_ShapeThickness : 1;
			const int64_t EdgeWork = Sides * (maximum(Width, Height) + 2) * Thickness * Thickness;
			Work = m_ShapeOutline ? EdgeWork : Area + EdgeWork;
		}
		break;
	case ETool::LINE:
		pBuild = &CEditorDrawingTools::BuildLineCells;
		Work = LineRasterWork(m_Drag.m_vPath, m_LineMode == ELineMode::SMOOTH, m_LineThickness, Budget);
		break;
	case ETool::FADE:
		pBuild = &CEditorDrawingTools::BuildFadeCells;
		if(m_FadeStart == 0 && m_FadeEnd == 0 && m_FadeRandomness == 0)
		{
			m_Drag.m_vPreviewCells.clear();
			m_Drag.m_PreviewCellsClipped = false;
			return;
		}
		break;
	case ETool::NONE:
		break;
	}
	if(Work > Budget)
	{
		m_Drag.m_vPreviewCells.clear();
		m_Drag.m_PreviewCellsClipped = !Commit;
		if(Commit)
			pEditor->ShowFileDialogError("%s", Localize("The drawing is too large. Split it into smaller strokes.", "Editor"));
		return;
	}

	std::vector<SCell> vCells;
	if(pBuild)
		(this->*pBuild)(vCells);
	ApplySymmetry(vCells);
	SortUniqueCells(vCells);
	m_Drag.m_PreviewCellsClipped = !Commit && vCells.size() > MAX_PREVIEW_CELLS;
	if(m_Drag.m_PreviewCellsClipped)
		vCells.resize(MAX_PREVIEW_CELLS);
	m_Drag.m_vPreviewCells = std::move(vCells);
}

std::shared_ptr<CLayerTiles> CEditorDrawingTools::MakeTileBrush(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pTargetLayer, int BrushTileX, int BrushTileY, bool Empty, bool *pSourceAir) const
{
	if(pSourceAir)
		*pSourceAir = Empty;

	const CLayerTiles *pBrushLayer = nullptr;
	if(!Empty)
	{
		if(!pEditor->m_pBrush)
			return nullptr;

		const ELayerKind TargetKind = TileLayerKind(*pTargetLayer);
		for(const auto &pLayer : pEditor->m_pBrush->m_vpLayers)
		{
			const CLayerTiles *pTiles = UsableTileBrush(pLayer);
			if(!pTiles)
				continue;
			if(TileLayerKind(*pTiles) == TargetKind)
			{
				pBrushLayer = pTiles;
				break;
			}
			if(!pBrushLayer)
				pBrushLayer = pTiles;
		}
		if(!pBrushLayer)
			return nullptr;
	}

	std::shared_ptr<CLayerTiles> pBrush;
	if(pTargetLayer == pEditor->Map()->m_pGameLayer)
	{
		pBrush = std::make_shared<CLayerGame>(pEditor->Map(), 1, 1);
	}
	else if(pTargetLayer == pEditor->Map()->m_pFrontLayer)
	{
		pBrush = std::make_shared<CLayerFront>(pEditor->Map(), 1, 1);
	}
	else if(pTargetLayer == pEditor->Map()->m_pTeleLayer)
	{
		auto pTeleBrush = std::make_shared<CLayerTele>(pEditor->Map(), 1, 1);
		pTeleBrush->m_TeleNumber = pEditor->m_TeleNumber;
		pTeleBrush->m_TeleCheckpointNumber = pEditor->m_TeleCheckpointNumber;
		pBrush = pTeleBrush;
	}
	else if(pTargetLayer == pEditor->Map()->m_pSpeedupLayer)
	{
		auto pSpeedupBrush = std::make_shared<CLayerSpeedup>(pEditor->Map(), 1, 1);
		pSpeedupBrush->m_SpeedupForce = pEditor->m_SpeedupForce;
		pSpeedupBrush->m_SpeedupMaxSpeed = pEditor->m_SpeedupMaxSpeed;
		pSpeedupBrush->m_SpeedupAngle = pEditor->m_SpeedupAngle;
		pBrush = pSpeedupBrush;
	}
	else if(pTargetLayer == pEditor->Map()->m_pSwitchLayer)
	{
		auto pSwitchBrush = std::make_shared<CLayerSwitch>(pEditor->Map(), 1, 1);
		pSwitchBrush->m_SwitchNumber = pEditor->m_SwitchNumber;
		pSwitchBrush->m_SwitchDelay = pEditor->m_SwitchDelay;
		pBrush = pSwitchBrush;
	}
	else if(pTargetLayer == pEditor->Map()->m_pTuneLayer)
	{
		auto pTuneBrush = std::make_shared<CLayerTune>(pEditor->Map(), 1, 1);
		pTuneBrush->m_TuningNumber = pEditor->m_TuningNumber;
		pBrush = pTuneBrush;
	}
	else
	{
		pBrush = std::make_shared<CLayerTiles>(pEditor->Map(), 1, 1);
	}

	if(!pBrush)
		return nullptr;

	pBrush->m_Image = pTargetLayer->m_Image;
	pBrush->m_Color = pTargetLayer->m_Color;
	pBrush->m_Color.a = 255;
	pBrush->m_HasGame = pTargetLayer->m_HasGame;
	pBrush->m_HasFront = pTargetLayer->m_HasFront;
	pBrush->m_HasTele = pTargetLayer->m_HasTele;
	pBrush->m_HasSpeedup = pTargetLayer->m_HasSpeedup;
	pBrush->m_HasSwitch = pTargetLayer->m_HasSwitch;
	pBrush->m_HasTune = pTargetLayer->m_HasTune;
	str_copy(pBrush->m_aFilename, pEditor->Map()->m_aFilename);

	if(Empty)
	{
		pBrush->m_pTiles[0] = CTile{TILE_AIR};
		return pBrush;
	}

	const auto Wrap = [](int Coordinate, int Extent) {
		const int Offset = Coordinate % Extent;
		return Offset < 0 ? Offset + Extent : Offset;
	};
	const int SrcX = Wrap(BrushTileX, pBrushLayer->m_Width);
	const int SrcY = Wrap(BrushTileY, pBrushLayer->m_Height);
	const size_t SrcIndex = static_cast<size_t>(SrcY) * pBrushLayer->m_Width + SrcX;
	pBrush->m_pTiles[0] = pBrushLayer->m_pTiles[SrcIndex];
	if(pSourceAir)
		*pSourceAir = pBrush->m_pTiles[0].m_Index == TILE_AIR;

	if(auto *pTeleTarget = dynamic_cast<CLayerTele *>(pBrush.get()))
	{
		if(const auto *pTeleSource = dynamic_cast<const CLayerTele *>(pBrushLayer))
			pTeleTarget->m_pTeleTile[0] = pTeleSource->m_pTeleTile[SrcIndex];
	}
	else if(auto *pSpeedupTarget = dynamic_cast<CLayerSpeedup *>(pBrush.get()))
	{
		if(const auto *pSpeedupSource = dynamic_cast<const CLayerSpeedup *>(pBrushLayer))
			pSpeedupTarget->m_pSpeedupTile[0] = pSpeedupSource->m_pSpeedupTile[SrcIndex];
	}
	else if(auto *pSwitchTarget = dynamic_cast<CLayerSwitch *>(pBrush.get()))
	{
		if(const auto *pSwitchSource = dynamic_cast<const CLayerSwitch *>(pBrushLayer))
			pSwitchTarget->m_pSwitchTile[0] = pSwitchSource->m_pSwitchTile[SrcIndex];
	}
	else if(auto *pTuneTarget = dynamic_cast<CLayerTune *>(pBrush.get()))
	{
		if(const auto *pTuneSource = dynamic_cast<const CLayerTune *>(pBrushLayer))
			pTuneTarget->m_pTuneTile[0] = pTuneSource->m_pTuneTile[SrcIndex];
	}

	return pBrush;
}

void CEditorDrawingTools::StampCell(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pLayer, int x, int y, bool Empty) const
{
	bool SourceAir = Empty;
	std::shared_ptr<CLayerTiles> pBrush = MakeTileBrush(pEditor, pLayer, x - m_Drag.m_Start.x, y - m_Drag.m_Start.y, Empty, &SourceAir);
	if(SourceAir && !Empty && !pEditor->m_BrushDrawDestructive)
		return;
	if(pBrush && !SourceAir && !IsValidTileForLayer(pEditor, pLayer, pBrush->m_pTiles[0].m_Index) && !pEditor->IsAllowPlaceUnusedTiles())
		return;
	if(pBrush)
		pLayer->BrushDraw(pBrush.get(), vec2(x * 32.0f, y * 32.0f));
}

bool CEditorDrawingTools::IsValidTileForLayer(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pLayer, int Index) const
{
	if(Index == TILE_AIR || pEditor->IsAllowPlaceUnusedTiles())
		return true;
	if(pLayer == pEditor->Map()->m_pGameLayer)
		return IsValidGameTile(Index);
	if(pLayer == pEditor->Map()->m_pFrontLayer)
		return IsValidFrontTile(Index);
	if(pLayer == pEditor->Map()->m_pTeleLayer)
		return IsValidTeleTile(Index);
	if(pLayer == pEditor->Map()->m_pSpeedupLayer)
		return IsValidSpeedupTile(Index);
	if(pLayer == pEditor->Map()->m_pSwitchLayer)
		return IsValidSwitchTile(Index);
	if(pLayer == pEditor->Map()->m_pTuneLayer)
		return IsValidTuneTile(Index);
	return true;
}

bool CEditorDrawingTools::SubmitCells(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, const std::vector<SCell> &vCells)
{
	if(vCells.empty())
		return false;

	const bool EmptyBrush = !pEditor->m_pBrush || pEditor->m_pBrush->IsEmpty();
	if(!EmptyBrush && std::none_of(pEditor->m_pBrush->m_vpLayers.begin(), pEditor->m_pBrush->m_vpLayers.end(), [](const auto &pLayer) { return UsableTileBrush(pLayer) != nullptr; }))
		return false;

	size_t NumWritableLayers = 0;
	for(size_t i = 0; i < NumEditLayers; ++i)
	{
		const auto &pLayer = pEditLayers[i].second;
		if(!pLayer || pLayer->m_Readonly)
			continue;

		// 专用画笔和物理层撤销都依赖地图登记。
		if(!IsCurrentDrawingTarget(*pEditor->Map(), static_cast<const CLayerTiles &>(*pLayer)))
		{
			pEditor->ShowFileDialogError("%s", Localize("Drawing tools require the active physics layers.", "Editor"));
			return false;
		}
		++NumWritableLayers;
	}
	if(NumWritableLayers == 0)
		return false;
	if(vCells.size() > static_cast<size_t>(MAX_DRAW_WORK) / NumWritableLayers)
	{
		pEditor->ShowFileDialogError("%s", Localize("The drawing is too large. Split it into smaller strokes.", "Editor"));
		return false;
	}

	const ETool SubmitTool = m_Drag.m_Tool;
	bool Submitted = false;
	for(size_t i = 0; i < NumEditLayers; ++i)
	{
		std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pEditLayers[i].second);
		if(!pLayer || pLayer->m_Readonly)
			continue;

		for(const SCell &Cell : vCells)
		{
			if(Cell.m_X < 0 || Cell.m_X >= pLayer->m_Width || Cell.m_Y < 0 || Cell.m_Y >= pLayer->m_Height)
				continue;

			bool Empty = EmptyBrush;
			if(SubmitTool == ETool::FADE && m_FadeAir && !EmptyBrush)
				Empty = StableUnitRandom(Cell.m_X, Cell.m_Y, m_FadeAngle + 17) < 0.5f;
			StampCell(pEditor, pLayer, Cell.m_X, Cell.m_Y, Empty);
			Submitted = true;
		}
	}
	return Submitted;
}

void CEditorDrawingTools::RenderPreview(CEditor *pEditor)
{
	if(!m_Drawing || pEditor->m_ShowPicker)
		return;

	const ETool PreviewTool = m_Drag.m_Tool;
	const bool DrawBounds = m_Drag.m_PreviewCellsClipped && (PreviewTool == ETool::FILL || PreviewTool == ETool::FADE || (PreviewTool == ETool::SHAPE && m_Shape == EShape::RECT && !m_ShapeOutline));
	if(!DrawBounds && m_Drag.m_vPreviewCells.empty())
	{
		if(m_Drag.m_PreviewCellsClipped)
			str_copy(pEditor->m_aTooltip, Localize("The drawing preview is too large to display.", "Editor"));
		return;
	}

	const auto pGroup = pEditor->Map()->SelectedGroup();
	float aPoints[4];
	if(!pGroup || !DrawingMouseWorld(pEditor, *pGroup, aPoints))
		return;

	pEditor->Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
	pEditor->Graphics()->TextureClear();
	pEditor->Graphics()->QuadsBegin();
	pEditor->Graphics()->SetColor(ColorRGBA(0.2f, 0.75f, 1.0f, 0.35f));
	if(DrawBounds)
	{
		const int MinX = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int MaxX = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int MinY = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		const int MaxY = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		IGraphics::CQuadItem Quad(MinX * 32.0f, MinY * 32.0f, (MaxX - MinX + 1) * 32.0f, (MaxY - MinY + 1) * 32.0f);
		pEditor->Graphics()->QuadsDrawTL(&Quad, 1);
	}
	else
	{
		for(const SCell &Cell : m_Drag.m_vPreviewCells)
		{
			IGraphics::CQuadItem Quad(Cell.m_X * 32.0f, Cell.m_Y * 32.0f, 32.0f, 32.0f);
			pEditor->Graphics()->QuadsDrawTL(&Quad, 1);
		}
	}
	pEditor->Graphics()->QuadsEnd();
	pEditor->Ui()->MapScreen();
}
