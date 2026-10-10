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
	Grid.m_Columns = std::clamp(FittingColumns, 1, std::min(std::max(1, Count), std::clamp(MaxColumns, 1, 4)));
	Grid.m_Rows = (std::max(0, Count) + Grid.m_Columns - 1) / Grid.m_Columns;
	Grid.m_CellWidth = std::max(0.0f, (Width - (Grid.m_Columns - 1) * Grid.m_Gap) / Grid.m_Columns);
	return Grid;
}

enum class ESettingsToggleGroupPass
{
	RENDER,
	LAYOUT,
	INPUT
};

// 同组短开关的数据不拥有配置或文案；调用时提供当帧有效的地址与译文。
struct SSettingsToggleEntry
{
	int *m_pValue;
	const char *m_pTextId;
	const char *m_pLabel;
	const void *m_pId = nullptr;
	int m_Mask = 0;
};

inline bool SettingsToggleEntryValue(const SSettingsToggleEntry &Entry)
{
	return Entry.m_Mask != 0 ? (*Entry.m_pValue & Entry.m_Mask) != 0 : *Entry.m_pValue != 0;
}

inline bool ApplySettingsToggleEntry(const SSettingsToggleEntry &Entry, bool Clicked, bool ProcessInput, bool Overridden)
{
	if(!Clicked || !ProcessInput || Overridden)
		return false;
	if(Entry.m_Mask != 0)
		*Entry.m_pValue ^= Entry.m_Mask;
	else
		*Entry.m_pValue ^= 1;
	return true;
}

inline int ResolveSettingsToggleGroupColumnLimit(float Width, float HalfWidth, bool TwoColumns)
{
	return TwoColumns && Width <= HalfWidth ? 2 : 4;
}

struct SSettingsToggleCell
{
	CUIRect m_Label;
	CUIRect m_Control;
};

inline SSettingsToggleCell ResolveSettingsToggleCell(const CUIRect &Cell, float LabelHeight, float ControlHeight, float Spacing)
{
	SSettingsToggleCell Layout;
	Layout.m_Label = {Cell.x, Cell.y, Cell.w, std::max(0.0f, LabelHeight)};
	const float Width = std::min(std::max(0.0f, Cell.w), std::max(0.0f, ControlHeight) * 1.65f);
	Layout.m_Control = {Cell.x + (Cell.w - Width) * 0.5f, Cell.y + Layout.m_Label.h + std::max(0.0f, Spacing), Width, std::max(0.0f, ControlHeight)};
	return Layout;
}

#endif
