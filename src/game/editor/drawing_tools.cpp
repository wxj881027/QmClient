#include "drawing_tools.h"

#include "editor.h"
#include "editor_actions.h"

#include <engine/keys.h>

#include <base/math.h>
#include <base/system.h>

#include <game/client/lineinput.h>
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
#include <iterator>

namespace
{
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

ivec2 MouseTile(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pLayer)
{
	std::shared_ptr<CLayerGroup> pGroup = pEditor->GetSelectedGroup();
	if(!pGroup || !pLayer)
		return ivec2(0, 0);

	float aPoints[4];
	pGroup->Mapping(aPoints);

	const vec2 MousePos = pEditor->Ui()->UpdatedMousePos();
	const float WorldWidth = aPoints[2] - aPoints[0];
	const float WorldHeight = aPoints[3] - aPoints[1];
	const float LayerX = aPoints[0] + WorldWidth * (MousePos.x / pEditor->Graphics()->WindowWidth());
	const float LayerY = aPoints[1] + WorldHeight * (MousePos.y / pEditor->Graphics()->WindowHeight());

	return ivec2(pLayer->ConvertX(LayerX), pLayer->ConvertY(LayerY));
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
	m_Drag.m_vPreviewCells.clear();
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
	case ETool::FILL: return "填充";
	case ETool::SHAPE: return "形状";
	case ETool::LINE: return "线条";
	case ETool::FADE: return "渐变";
	case ETool::NONE: break;
	}
	return "绘图工具";
}

const char *CEditorDrawingTools::ActionName() const
{
	const ETool Tool = m_Drawing ? m_Drag.m_Tool : m_Tool;
	switch(Tool)
	{
	case ETool::FILL: return "绘图工具：填充";
	case ETool::SHAPE:
		switch(m_Shape)
		{
		case EShape::RECT: return "绘图工具：矩形";
		case EShape::ELLIPSE: return "绘图工具：椭圆";
		case EShape::TRIANGLE: return "绘图工具：三角形";
		case EShape::NGON: return "绘图工具：N 边形";
		}
		break;
	case ETool::LINE: return "绘图工具：线条";
	case ETool::FADE: return "绘图工具：Fade";
	case ETool::NONE: break;
	}
	return "绘图工具";
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
		str_format(pEditor->m_aTooltip, sizeof(pEditor->m_aTooltip), "形状粗细：%d", m_ShapeThickness);
		CancelDrawing();
		return true;
	}
	if(m_Tool == ETool::LINE)
	{
		m_LineThickness = std::clamp(m_LineThickness + Delta, 1, 16);
		str_format(pEditor->m_aTooltip, sizeof(pEditor->m_aTooltip), "线条粗细：%d", m_LineThickness);
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

	DoToolButton(pEditor, pToolbar, ETool::FILL, "填充", "绘图工具：拖拽矩形区域，用当前图块填充。空画笔会擦除为空气。");
	DoToolButton(pEditor, pToolbar, ETool::SHAPE, "形状", "绘图工具：拖拽生成矩形、椭圆、三角形或 N 边形。");
	DoToolButton(pEditor, pToolbar, ETool::LINE, "线条", "绘图工具：沿鼠标路径绘制连续线条。");
	DoToolButton(pEditor, pToolbar, ETool::FADE, "渐变", "绘图工具：在拖拽矩形内按概率渐变散布图块。");

	const char *pSym = "对称:关";
	if(m_Symmetry == ESymmetry::X)
		pSym = "对称:X";
	else if(m_Symmetry == ESymmetry::Y)
		pSym = "对称:Y";
	else if(m_Symmetry == ESymmetry::XY)
		pSym = "对称:XY";
	DoSmallButton(pEditor, pToolbar, &m_Symmetry, pSym, m_Symmetry != ESymmetry::OFF, "对称绘制：Off -> X -> Y -> XY。中心为本次按下的图块坐标。", [&]() {
		m_Symmetry = static_cast<ESymmetry>((static_cast<int>(m_Symmetry) + 1) % 4);
	});

	if(m_Tool == ETool::SHAPE)
	{
		DoSmallButton(pEditor, pToolbar, &s_ShapeRectButton, "矩形", m_Shape == EShape::RECT, "矩形形状。", [&]() { m_Shape = EShape::RECT; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeEllipseButton, "椭圆", m_Shape == EShape::ELLIPSE, "椭圆形状。", [&]() { m_Shape = EShape::ELLIPSE; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeTriangleButton, "三角", m_Shape == EShape::TRIANGLE, "三角形形状。", [&]() { m_Shape = EShape::TRIANGLE; });
		DoSmallButton(pEditor, pToolbar, &s_ShapeNgonButton, "N边", m_Shape == EShape::NGON, "N 边形形状。", [&]() { m_Shape = EShape::NGON; });
		DoSmallButton(pEditor, pToolbar, &m_ShapeOutline, "轮廓", m_ShapeOutline, "切换实心/轮廓绘制。", [&]() { m_ShapeOutline = !m_ShapeOutline; });
		m_ShapeThickness = DoValue(pEditor, pToolbar, &m_ShapeThickness, "粗细 ", m_ShapeThickness, 1, 16, 1, 2.0f, "轮廓粗细。");
		if(m_Shape == EShape::NGON)
			m_ShapeNgonSides = DoValue(pEditor, pToolbar, &m_ShapeNgonSides, "N ", m_ShapeNgonSides, 3, 16, 1, 2.0f, "N 边形边数。");
	}
	else if(m_Tool == ETool::LINE)
	{
		m_LineThickness = DoValue(pEditor, pToolbar, &m_LineThickness, "粗细 ", m_LineThickness, 1, 16, 1, 2.0f, "线条粗细。");
		DoSmallButton(pEditor, pToolbar, &s_LineSmoothButton, "平滑", m_LineMode == ELineMode::SMOOTH, "轻量平滑鼠标路径后绘制。", [&]() { m_LineMode = ELineMode::SMOOTH; });
		DoSmallButton(pEditor, pToolbar, &s_LineSharpButton, "折线", m_LineMode == ELineMode::SHARP, "保留鼠标路径折线。", [&]() { m_LineMode = ELineMode::SHARP; });
	}
	else if(m_Tool == ETool::FADE)
	{
		m_FadeAngle = DoValue(pEditor, pToolbar, &m_FadeAngle, "角度 ", m_FadeAngle, 0, 359, 1, 2.0f, "渐变角度。", true);
		m_FadeStart = DoValue(pEditor, pToolbar, &m_FadeStart, "起% ", m_FadeStart, 0, 100, 1, 2.0f, "起点概率强度。");
		m_FadeEnd = DoValue(pEditor, pToolbar, &m_FadeEnd, "止% ", m_FadeEnd, 0, 100, 1, 2.0f, "终点概率强度。");
		m_FadeSoftness = DoValue(pEditor, pToolbar, &m_FadeSoftness, "柔化 ", m_FadeSoftness, 0, 100, 1, 2.0f, "边缘柔化强度。");
		m_FadeRandomness = DoValue(pEditor, pToolbar, &m_FadeRandomness, "随机 ", m_FadeRandomness, 0, 100, 1, 2.0f, "随机扰动强度。");
		DoSmallButton(pEditor, pToolbar, &m_FadeAir, "空气", m_FadeAir, "允许渐变随机擦除为空气。", [&]() { m_FadeAir = !m_FadeAir; });
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
	if(Current == m_Drag.m_Current && m_Drag.m_Tool != ETool::LINE)
		return;
	if(m_Drag.m_Tool == ETool::LINE && (m_Drag.m_vPath.empty() || m_Drag.m_vPath.back() != Current))
		m_Drag.m_vPath.push_back(Current);

	m_Drag.m_Current = Current;
	RebuildPreview(pEditor, false);
}

void CEditorDrawingTools::FinishDrag(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers)
{
	if(!m_Drawing)
		return;

	RebuildPreview(pEditor, true);
	std::vector<SCell> vCells = m_Drag.m_vPreviewCells;
	const bool Submitted = SubmitCells(pEditor, pEditLayers, NumEditLayers, vCells);
	const char *pActionName = ActionName();
	CancelDrawing();

	if(Submitted)
	{
		std::shared_ptr<IEditorAction> pAction = std::make_shared<CEditorBrushDrawAction>(&pEditor->m_Map, pEditor->m_SelectedGroup);
		if(!pAction->IsEmpty())
			pEditor->m_Map.m_EditorHistory.RecordAction(pAction, pActionName);
	}
}

bool CEditorDrawingTools::HandleMapEditorInput(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, bool Inside)
{
	const ETool ActiveTool = m_Drawing ? m_Drag.m_Tool : (WantsQuickFill(pEditor, pEditLayers, NumEditLayers) ? ETool::FILL : m_Tool);
	const bool CanUseTool = ActiveTool != ETool::NONE && IsTileToolTarget(pEditor, pEditLayers, NumEditLayers) && pEditor->m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && !pEditor->Ui()->IsPopupOpen() && (!pEditor->Input()->ModifierIsPressed() || ActiveTool == ETool::FILL);
	if(!CanUseTool)
	{
		CancelDrawing();
		return false;
	}

	if(!Inside && !m_Drawing)
		return false;

	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pEditLayers[0].second);
	const ivec2 Tile = MouseTile(pEditor, pLayer);

	if(m_Drawing)
	{
		UpdateDrag(pEditor, Tile.x, Tile.y);
		if(!pEditor->Ui()->MouseButton(0))
			FinishDrag(pEditor, pEditLayers, NumEditLayers);
		return true;
	}

	if(Inside)
	{
		char aTooltip[128];
		str_format(aTooltip, sizeof(aTooltip), "%s：按住鼠标左键拖拽绘制。Alt+左键可临时填充，Ctrl+滚轮调粗细。", ToolName(ActiveTool));
		str_copy(pEditor->m_aTooltip, aTooltip);
	}

	if(Inside && pEditor->Ui()->CheckActiveItem(nullptr) && pEditor->Ui()->MouseButton(0) && !pEditor->m_QuadKnifeActive)
	{
		pEditor->Ui()->SetActiveItem(this);
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
	for(int yy = MinOffset; yy <= MaxOffset; ++yy)
	{
		for(int xx = MinOffset; xx <= MaxOffset; ++xx)
		{
			if(Thickness <= 2 || xx * xx + yy * yy <= RadiusSquared)
				AddCell(vCells, x + xx, y + yy);
		}
	}
}

void CEditorDrawingTools::AddLine(std::vector<SCell> &vCells, ivec2 From, ivec2 To, int Thickness) const
{
	int x0 = From.x;
	int y0 = From.y;
	const int x1 = To.x;
	const int y1 = To.y;
	const int dx = absolute(x1 - x0);
	const int sx = x0 < x1 ? 1 : -1;
	const int dy = -absolute(y1 - y0);
	const int sy = y0 < y1 ? 1 : -1;
	int Error = dx + dy;

	while(true)
	{
		AddThickCell(vCells, x0, y0, Thickness);
		if(x0 == x1 && y0 == y1)
			break;

		const int Error2 = 2 * Error;
		if(Error2 >= dy)
		{
			Error += dy;
			x0 += sx;
		}
		if(Error2 <= dx)
		{
			Error += dx;
			y0 += sy;
		}
	}
}

void CEditorDrawingTools::BuildFillCells(std::vector<SCell> &vCells) const
{
	const int x0 = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int x1 = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int y0 = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const int y1 = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	for(int y = y0; y <= y1; ++y)
		for(int x = x0; x <= x1; ++x)
			AddCell(vCells, x, y);
}

void CEditorDrawingTools::BuildShapeCells(std::vector<SCell> &vCells) const
{
	if(m_Shape == EShape::RECT)
	{
		const int x0 = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int x1 = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int y0 = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		const int y1 = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		if(!m_ShapeOutline)
		{
			for(int y = y0; y <= y1; ++y)
				for(int x = x0; x <= x1; ++x)
					AddCell(vCells, x, y);
		}
		else
		{
			for(int Thickness = 0; Thickness < m_ShapeThickness; ++Thickness)
			{
				for(int x = x0 + Thickness; x <= x1 - Thickness; ++x)
				{
					AddCell(vCells, x, y0 + Thickness);
					AddCell(vCells, x, y1 - Thickness);
				}
				for(int y = y0 + Thickness; y <= y1 - Thickness; ++y)
				{
					AddCell(vCells, x0 + Thickness, y);
					AddCell(vCells, x1 - Thickness, y);
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
					if(((Pi.y > y) != (Pj.y > y)) && (x < (Pj.x - Pi.x) * (y - Pi.y) / (float)(Pj.y - Pi.y) + Pi.x))
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

	std::vector<ivec2> vPath = m_Drag.m_vPath;
	if(m_LineMode == ELineMode::SMOOTH && vPath.size() >= 3)
	{
		std::vector<ivec2> vSmoothed = vPath;
		for(size_t i = 1; i + 1 < vPath.size(); ++i)
			vSmoothed[i] = ivec2(round_to_int((vPath[i - 1].x + vPath[i].x * 2.0f + vPath[i + 1].x) / 4.0f), round_to_int((vPath[i - 1].y + vPath[i].y * 2.0f + vPath[i + 1].y) / 4.0f));
		vPath = vSmoothed;
	}

	for(size_t i = 1; i < vPath.size(); ++i)
		AddLine(vCells, vPath[i - 1], vPath[i], m_LineThickness);
	if(vPath.size() == 1)
		AddThickCell(vCells, vPath[0].x, vPath[0].y, m_LineThickness);
}

void CEditorDrawingTools::BuildFadeCells(std::vector<SCell> &vCells) const
{
	const int x0 = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int x1 = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
	const int y0 = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const int y1 = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
	const float Angle = m_FadeAngle * pi / 180.0f;
	const float DirX = std::cos(Angle);
	const float DirY = std::sin(Angle);
	const float aCorners[] = {
		x0 * DirX + y0 * DirY,
		x1 * DirX + y0 * DirY,
		x0 * DirX + y1 * DirY,
		x1 * DirX + y1 * DirY,
	};
	const float MinProjection = *std::min_element(std::begin(aCorners), std::end(aCorners));
	const float MaxProjection = *std::max_element(std::begin(aCorners), std::end(aCorners));
	const float ProjectionRange = maximum(1.0f, MaxProjection - MinProjection);

	for(int y = y0; y <= y1; ++y)
	{
		for(int x = x0; x <= x1; ++x)
		{
			float t = (x * DirX + y * DirY - MinProjection) / ProjectionRange;
			t = std::clamp(t, 0.0f, 1.0f);
			float Probability = mix((float)m_FadeStart, (float)m_FadeEnd, t) / 100.0f;
			if(m_FadeSoftness > 0)
			{
				const float Edge = minimum(minimum((float)(x - x0 + 1), (float)(x1 - x + 1)), minimum((float)(y - y0 + 1), (float)(y1 - y + 1)));
				const float SoftRange = 1.0f + m_FadeSoftness / 20.0f;
				Probability *= std::clamp(Edge / SoftRange, 0.0f, 1.0f);
			}
			if(m_FadeRandomness > 0)
				Probability += (StableUnitRandom(x, y, m_FadeAngle) * 2.0f - 1.0f) * (m_FadeRandomness / 100.0f);
			Probability = std::clamp(Probability, 0.0f, 1.0f);
			if(StableUnitRandom(x, y, m_FadeStart * 257 + m_FadeEnd) <= Probability)
				AddCell(vCells, x, y);
		}
	}
}

void CEditorDrawingTools::ApplySymmetry(std::vector<SCell> &vCells) const
{
	if(m_Symmetry == ESymmetry::OFF)
		return;

	const std::vector<SCell> vOriginal = vCells;
	for(const SCell &Cell : vOriginal)
	{
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
	std::vector<SCell> vCells;
	const ETool BuildTool = m_Drawing ? m_Drag.m_Tool : m_Tool;
	switch(BuildTool)
	{
	case ETool::FILL:
		BuildFillCells(vCells);
		break;
	case ETool::SHAPE:
		BuildShapeCells(vCells);
		break;
	case ETool::LINE:
		BuildLineCells(vCells);
		break;
	case ETool::FADE:
		BuildFadeCells(vCells);
		break;
	case ETool::NONE:
		break;
	}
	ApplySymmetry(vCells);
	SortUniqueCells(vCells);
	m_Drag.m_PreviewCellsClipped = !Commit && vCells.size() > 4096;
	if(m_Drag.m_PreviewCellsClipped)
		vCells.resize(4096);
	m_Drag.m_vPreviewCells = vCells;
	(void)pEditor;
}

std::shared_ptr<CLayerTiles> CEditorDrawingTools::MakeTileBrush(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pTargetLayer, int BrushTileX, int BrushTileY, bool Empty, bool *pSourceAir) const
{
	if(pSourceAir)
		*pSourceAir = Empty;

	std::shared_ptr<CLayerTiles> pBrush;
	if(pTargetLayer == pEditor->m_Map.m_pGameLayer)
		pBrush = std::make_shared<CLayerGame>(&pEditor->m_Map, 1, 1);
	else if(pTargetLayer == pEditor->m_Map.m_pFrontLayer)
		pBrush = std::make_shared<CLayerFront>(&pEditor->m_Map, 1, 1);
	else if(pTargetLayer == pEditor->m_Map.m_pTeleLayer)
	{
		auto pTeleBrush = std::make_shared<CLayerTele>(&pEditor->m_Map, 1, 1);
		pTeleBrush->m_TeleNumber = pEditor->m_TeleNumber;
		pTeleBrush->m_TeleCheckpointNumber = pEditor->m_TeleCheckpointNumber;
		pBrush = pTeleBrush;
	}
	else if(pTargetLayer == pEditor->m_Map.m_pSpeedupLayer)
	{
		auto pSpeedupBrush = std::make_shared<CLayerSpeedup>(&pEditor->m_Map, 1, 1);
		pSpeedupBrush->m_SpeedupForce = pEditor->m_SpeedupForce;
		pSpeedupBrush->m_SpeedupMaxSpeed = pEditor->m_SpeedupMaxSpeed;
		pSpeedupBrush->m_SpeedupAngle = pEditor->m_SpeedupAngle;
		pBrush = pSpeedupBrush;
	}
	else if(pTargetLayer == pEditor->m_Map.m_pSwitchLayer)
	{
		auto pSwitchBrush = std::make_shared<CLayerSwitch>(&pEditor->m_Map, 1, 1);
		pSwitchBrush->m_SwitchNumber = pEditor->m_SwitchNumber;
		pSwitchBrush->m_SwitchDelay = pEditor->m_SwitchDelay;
		pBrush = pSwitchBrush;
	}
	else if(pTargetLayer == pEditor->m_Map.m_pTuneLayer)
	{
		auto pTuneBrush = std::make_shared<CLayerTune>(&pEditor->m_Map, 1, 1);
		pTuneBrush->m_TuningNumber = pEditor->m_TuningNumber;
		pBrush = pTuneBrush;
	}
	else
		pBrush = std::make_shared<CLayerTiles>(&pEditor->m_Map, 1, 1);

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
	str_copy(pBrush->m_aFilename, pEditor->m_aFilename);

	if(Empty)
	{
		pBrush->m_pTiles[0] = CTile{TILE_AIR};
		return pBrush;
	}

	if(pEditor->m_pBrush && !pEditor->m_pBrush->IsEmpty())
	{
		for(const auto &pLayer : pEditor->m_pBrush->m_vpLayers)
		{
			if(pLayer && pLayer->m_Type == LAYERTYPE_TILES)
			{
				std::shared_ptr<CLayerTiles> pBrushLayer = std::static_pointer_cast<CLayerTiles>(pLayer);
				if(pBrushLayer->m_Width > 0 && pBrushLayer->m_Height > 0)
				{
					const int SrcX = ((BrushTileX % pBrushLayer->m_Width) + pBrushLayer->m_Width) % pBrushLayer->m_Width;
					const int SrcY = ((BrushTileY % pBrushLayer->m_Height) + pBrushLayer->m_Height) % pBrushLayer->m_Height;
					const int SrcIndex = SrcY * pBrushLayer->m_Width + SrcX;
					pBrush->m_pTiles[0] = pBrushLayer->m_pTiles[SrcIndex];
					if(pSourceAir)
						*pSourceAir = pBrush->m_pTiles[0].m_Index == TILE_AIR;

					if(auto pTeleTarget = std::dynamic_pointer_cast<CLayerTele>(pBrush))
					{
						if(auto pTeleSource = std::dynamic_pointer_cast<CLayerTele>(pBrushLayer))
							pTeleTarget->m_pTeleTile[0] = pTeleSource->m_pTeleTile[SrcIndex];
					}
					else if(auto pSpeedupTarget = std::dynamic_pointer_cast<CLayerSpeedup>(pBrush))
					{
						if(auto pSpeedupSource = std::dynamic_pointer_cast<CLayerSpeedup>(pBrushLayer))
							pSpeedupTarget->m_pSpeedupTile[0] = pSpeedupSource->m_pSpeedupTile[SrcIndex];
					}
					else if(auto pSwitchTarget = std::dynamic_pointer_cast<CLayerSwitch>(pBrush))
					{
						if(auto pSwitchSource = std::dynamic_pointer_cast<CLayerSwitch>(pBrushLayer))
							pSwitchTarget->m_pSwitchTile[0] = pSwitchSource->m_pSwitchTile[SrcIndex];
					}
					else if(auto pTuneTarget = std::dynamic_pointer_cast<CLayerTune>(pBrush))
					{
						if(auto pTuneSource = std::dynamic_pointer_cast<CLayerTune>(pBrushLayer))
							pTuneTarget->m_pTuneTile[0] = pTuneSource->m_pTuneTile[SrcIndex];
					}
					return pBrush;
				}
			}
		}
	}

	if(pTargetLayer->m_Width > 0 && pTargetLayer->m_Height > 0)
	{
		const int SrcX = std::clamp(BrushTileX, 0, pTargetLayer->m_Width - 1);
		const int SrcY = std::clamp(BrushTileY, 0, pTargetLayer->m_Height - 1);
		pBrush->m_pTiles[0] = pTargetLayer->m_pTiles[SrcY * pTargetLayer->m_Width + SrcX];
		if(pSourceAir)
			*pSourceAir = pBrush->m_pTiles[0].m_Index == TILE_AIR;
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
	if(pLayer == pEditor->m_Map.m_pGameLayer)
		return IsValidGameTile(Index);
	if(pLayer == pEditor->m_Map.m_pFrontLayer)
		return IsValidFrontTile(Index);
	if(pLayer == pEditor->m_Map.m_pTeleLayer)
		return IsValidTeleTile(Index);
	if(pLayer == pEditor->m_Map.m_pSpeedupLayer)
		return IsValidSpeedupTile(Index);
	if(pLayer == pEditor->m_Map.m_pSwitchLayer)
		return IsValidSwitchTile(Index);
	if(pLayer == pEditor->m_Map.m_pTuneLayer)
		return IsValidTuneTile(Index);
	return true;
}

bool CEditorDrawingTools::SubmitCells(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, const std::vector<SCell> &vCells)
{
	if(vCells.empty())
		return false;

	const ETool SubmitTool = m_Drawing ? m_Drag.m_Tool : m_Tool;
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

			const bool EmptyBrush = !pEditor->m_pBrush || pEditor->m_pBrush->IsEmpty();
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

	std::shared_ptr<CLayerGroup> pGroup = pEditor->GetSelectedGroup();
	if(pGroup)
		pGroup->MapScreen();

	pEditor->Graphics()->TextureClear();
	pEditor->Graphics()->QuadsBegin();
	pEditor->Graphics()->SetColor(ColorRGBA(0.2f, 0.75f, 1.0f, 0.35f));
	const ETool PreviewTool = m_Drawing ? m_Drag.m_Tool : m_Tool;
	if(m_Drag.m_PreviewCellsClipped && (PreviewTool == ETool::FILL || PreviewTool == ETool::FADE || (PreviewTool == ETool::SHAPE && m_Shape == EShape::RECT && !m_ShapeOutline)))
	{
		const int x0 = minimum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int x1 = maximum(m_Drag.m_Start.x, m_Drag.m_Current.x);
		const int y0 = minimum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		const int y1 = maximum(m_Drag.m_Start.y, m_Drag.m_Current.y);
		IGraphics::CQuadItem Quad(x0 * 32.0f, y0 * 32.0f, (x1 - x0 + 1) * 32.0f, (y1 - y0 + 1) * 32.0f);
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
