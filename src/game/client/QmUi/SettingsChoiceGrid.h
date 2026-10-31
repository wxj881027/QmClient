#ifndef GAME_CLIENT_QMUI_SETTINGSCHOICEGRID_H
#define GAME_CLIENT_QMUI_SETTINGSCHOICEGRID_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

struct SSettingsChoiceGrid
{
	int m_Columns = 1;
	int m_Rows = 0;
	float m_CellWidth = 0.0f;
	float m_RowHeight = 0.0f;
	float m_Gap = 0.0f;

	float Height() const { return m_Rows > 0 ? m_Rows * m_RowHeight + (m_Rows - 1) * m_Gap : 0.0f; }
	CUIRect Cell(const CUIRect &Area, int Index) const
	{
		return {Area.x + Index % m_Columns * (m_CellWidth + m_Gap), Area.y + Index / m_Columns * (m_RowHeight + m_Gap), m_CellWidth, m_RowHeight};
	}
};

inline SSettingsChoiceGrid ResolveSettingsToggleGrid(float Width, float MinimumCellWidth, float RowHeight, float Gap, int Count, int MaxColumns)
{
	SSettingsChoiceGrid Grid;
	Grid.m_Gap = std::max(0.0f, Gap);
	Grid.m_RowHeight = std::max(0.0f, RowHeight);
	Width = std::max(0.0f, Width);
	const float FittingColumns = std::floor((Width + Grid.m_Gap) / std::max(1.0f, MinimumCellWidth + Grid.m_Gap));
	const int ColumnLimit = std::clamp(MaxColumns, 1, 4);
	Grid.m_Columns = std::isnan(FittingColumns) ? 1 : static_cast<int>(std::clamp(FittingColumns, 1.0f, static_cast<float>(ColumnLimit)));
	const int Items = std::max(0, Count);
	Grid.m_Rows = Items / Grid.m_Columns + (Items % Grid.m_Columns != 0);
	Grid.m_CellWidth = std::max(0.0f, (Width - (Grid.m_Columns - 1) * Grid.m_Gap) / Grid.m_Columns);
	return Grid;
}

#endif
