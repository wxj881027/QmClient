#ifndef GAME_CLIENT_QMUI_SETTINGSTOGGLEGRID_H
#define GAME_CLIENT_QMUI_SETTINGSTOGGLEGRID_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

struct SSettingsToggleGrid
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

inline SSettingsToggleGrid ResolveSettingsToggleGrid(float Width, float MinimumCellWidth, float RowHeight, float Gap, int Count, int MaxColumns)
{
	SSettingsToggleGrid Grid;
	Grid.m_Gap = std::max(0.0f, Gap);
	Grid.m_RowHeight = std::max(0.0f, RowHeight);
	Width = std::max(0.0f, Width);
	const int FittingColumns = static_cast<int>(std::floor((Width + Grid.m_Gap) / std::max(1.0f, MinimumCellWidth + Grid.m_Gap)));
	Grid.m_Columns = std::clamp(FittingColumns, 1, std::clamp(MaxColumns, 1, 4));
	Grid.m_Rows = (std::max(0, Count) + Grid.m_Columns - 1) / Grid.m_Columns;
	Grid.m_CellWidth = std::max(0.0f, (Width - (Grid.m_Columns - 1) * Grid.m_Gap) / Grid.m_Columns);
	return Grid;
}

#endif
