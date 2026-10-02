#ifndef GAME_CLIENT_QMUI_SETTINGSCARDWIDTH_H
#define GAME_CLIENT_QMUI_SETTINGSCARDWIDTH_H

#include "QmCardOrderModel.h"
#include "SettingsCardGeometry.h"

#include <algorithm>

inline CUIRect SettingsCardWidthButtonRect(const SSettingsCardFrame &Frame)
{
	CUIRect Rect = Frame.m_HandleRect;
	Rect.x -= Rect.w + 4.0f;
	return Rect;
}

inline bool ToggleSettingsCardWidth(qm_card_order::CModel &Model, const char *pStableId, int DefaultColumn, int &RestoreColumn, int &RestoreOrder)
{
	const int Index = Model.FindByStableId(pStableId);
	if(Index < 0)
		return false;
	const auto Entry = Model.Entry(Index);
	if(Entry.m_Column != 0)
	{
		RestoreColumn = Entry.m_Column;
		RestoreOrder = Entry.m_OrderInColumn;
		Model.Move(pStableId, 0, static_cast<int>(Model.ColumnIndices(Entry.m_pDefaultTab, 0).size()));
	}
	else
	{
		const int Column = RestoreColumn > 0 ? RestoreColumn : std::clamp(DefaultColumn, 1, 2);
		const int Order = RestoreOrder >= 0 ? RestoreOrder : static_cast<int>(Model.ColumnIndices(Entry.m_pDefaultTab, Column).size());
		Model.Move(pStableId, Column, Order);
	}
	return true;
}

#endif
