#ifndef GAME_EDITOR_DRAWING_TOOLS_H
#define GAME_EDITOR_DRAWING_TOOLS_H

#include <base/vmath.h>

#include <game/client/ui_rect.h>

#include <memory>
#include <functional>
#include <utility>
#include <vector>

class CEditor;
class CLayer;
class CLayerTiles;

class CEditorDrawingTools
{
public:
	enum class ETool
	{
		NONE,
		FILL,
		SHAPE,
		LINE,
		FADE,
	};

	enum class EShape
	{
		RECT,
		ELLIPSE,
		TRIANGLE,
		NGON,
	};

	enum class ELineMode
	{
		SHARP,
		SMOOTH,
	};

	enum class ESymmetry
	{
		OFF,
		X,
		Y,
		XY,
	};

	CEditorDrawingTools();

	[[nodiscard]] bool HasActiveTool() const { return m_Tool != ETool::NONE; }
	[[nodiscard]] bool IsDrawing() const { return m_Drawing; }
	void CancelDrawing();
	void Reset();

	void RenderToolbar(CEditor *pEditor, CUIRect *pToolbar);
	bool HandleMapEditorInput(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, bool Inside);
	bool HandleWheelInput(CEditor *pEditor, CUIRect View);
	bool WantsQuickFill(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers) const;
	void RenderPreview(CEditor *pEditor);

private:
	struct SCell
	{
		int m_X;
		int m_Y;
	};

	struct SDragState
	{
		ivec2 m_Start;
		ivec2 m_Current;
		ETool m_Tool;
		std::vector<ivec2> m_vPath;
		std::vector<SCell> m_vPreviewCells;
		bool m_PreviewCellsClipped;
	};

	ETool m_Tool;
	EShape m_Shape;
	ELineMode m_LineMode;
	ESymmetry m_Symmetry;

	bool m_ShapeOutline;
	int m_ShapeThickness;
	int m_ShapeNgonSides;
	int m_LineThickness;
	int m_FadeAngle;
	int m_FadeStart;
	int m_FadeEnd;
	int m_FadeSoftness;
	int m_FadeRandomness;
	bool m_FadeAir;

	bool m_Drawing;
	SDragState m_Drag;

	void SetTool(ETool Tool);
	const char *ToolName(ETool Tool) const;
	const char *ToolName() const;
	const char *ActionName() const;
	bool IsTileToolTarget(const CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers) const;

	void BeginDrag(CEditor *pEditor, int TileX, int TileY, ETool Tool);
	void UpdateDrag(CEditor *pEditor, int TileX, int TileY);
	void FinishDrag(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers);
	void RebuildPreview(CEditor *pEditor, bool Commit);

	void BuildFillCells(std::vector<SCell> &vCells) const;
	void BuildShapeCells(std::vector<SCell> &vCells) const;
	void BuildLineCells(std::vector<SCell> &vCells) const;
	void BuildFadeCells(std::vector<SCell> &vCells) const;

	void AddCell(std::vector<SCell> &vCells, int x, int y) const;
	void AddThickCell(std::vector<SCell> &vCells, int x, int y, int Thickness) const;
	void AddLine(std::vector<SCell> &vCells, ivec2 From, ivec2 To, int Thickness) const;
	void ApplySymmetry(std::vector<SCell> &vCells) const;
	void SortUniqueCells(std::vector<SCell> &vCells) const;

	std::shared_ptr<CLayerTiles> MakeTileBrush(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pTargetLayer, int BrushTileX, int BrushTileY, bool Empty, bool *pSourceAir = nullptr) const;
	void StampCell(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pLayer, int x, int y, bool Empty) const;
	bool SubmitCells(CEditor *pEditor, const std::pair<int, std::shared_ptr<CLayer>> *pEditLayers, size_t NumEditLayers, const std::vector<SCell> &vCells);
	bool IsValidTileForLayer(CEditor *pEditor, const std::shared_ptr<CLayerTiles> &pLayer, int Index) const;

	void DoToolButton(CEditor *pEditor, CUIRect *pToolbar, ETool Tool, const char *pLabel, const char *pTooltip);
	void DoSmallButton(CEditor *pEditor, CUIRect *pToolbar, const void *pId, const char *pLabel, bool Active, const char *pTooltip, const std::function<void()> &OnClick);
	int DoValue(CEditor *pEditor, CUIRect *pToolbar, const void *pId, const char *pLabel, int Value, int Min, int Max, int Step, float Scale, const char *pTooltip, bool IsDegree = false);
};

#endif
