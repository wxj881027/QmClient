#ifndef GAME_CLIENT_QMUI_QMSERVERINFOGEOMETRY_H
#define GAME_CLIENT_QMUI_QMSERVERINFOGEOMETRY_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace QmServerInfoGeometry
{
	constexpr float GAP = 4.0f;

	struct SLayout
	{
		CUIRect m_Server;
		CUIRect m_Game;
		CUIRect m_Motd;
		CUIRect m_Tags;
	};

	inline float FitScale(CUIRect View, float ContentWidth, float ContentHeight)
	{
		if(View.w <= 0.0f || View.h <= 0.0f)
			return 0.0f;
		return std::min({1.0f, View.w / std::max(ContentWidth, 1.0f), View.h / std::max(ContentHeight, 1.0f)});
	}

	inline CUIRect ContentRect(CUIRect View)
	{
		const float Margin = std::min({10.0f, std::max(0.0f, View.w) * 0.5f, std::max(0.0f, View.h) * 0.5f});
		return {View.x + Margin, View.y + Margin, std::max(0.0f, View.w - 2.0f * Margin), std::max(0.0f, View.h - 2.0f * Margin)};
	}

	inline SLayout Layout(CUIRect View, bool ShowTags)
	{
		View.w = std::max(0.0f, View.w);
		View.h = std::max(0.0f, View.h);
		const float GapX = std::min(GAP, View.w);
		const float GapY = std::min(GAP, View.h);
		const float TopHeight = (View.h - GapY) * 0.5f;
		const float TopWidth = (View.w - GapX) * 0.5f;
		const float BottomWidth = ShowTags ? (View.w - GapX) * 0.38f : View.w;
		const float BottomY = View.y + TopHeight + GapY;
		const float BottomHeight = View.h - TopHeight - GapY;
		return {
			{View.x, View.y, TopWidth, TopHeight},
			{View.x + TopWidth + GapX, View.y, View.w - TopWidth - GapX, TopHeight},
			{View.x, BottomY, BottomWidth, BottomHeight},
			{ShowTags ? View.x + BottomWidth + GapX : View.x + View.w, BottomY, ShowTags ? View.w - BottomWidth - GapX : 0.0f, BottomHeight}};
	}

	inline float ServerContentHeight(bool HasPing, bool HasCommunity)
	{
		// 标题、名称及间隔、信息行、底部收藏/复制操作与留白。
		return 37.0f + 40.0f + (3 + (int)HasPing + (int)HasCommunity) * 20.0f + 80.0f;
	}

	inline float GameContentHeight(int OptionalFields, bool HasTeams, bool HasNote)
	{
		return 37.0f + (3 + std::clamp(OptionalFields, 0, 3) + (int)HasTeams) * 20.0f + (HasNote ? 34.0f : 0.0f);
	}

	struct STagGrid
	{
		int m_Columns = 1;
		float m_Scale = 0.0f;
		float m_CellWidth = 0.0f;
	};

	inline STagGrid TagGrid(CUIRect View, int Count, float MaxTagWidth, float MaxScale)
	{
		STagGrid Result;
		if(Count <= 0)
			return Result;
		MaxTagWidth = std::max(1.0f, MaxTagWidth);
		// 在能完整放下所有标签的网格中，选择字号最大的一种排列。
		for(int Columns = 1; Columns <= Count; ++Columns)
		{
			const int Rows = (Count + Columns - 1) / Columns;
			const float Scale = std::min(MaxScale, FitScale(View, Columns * MaxTagWidth + (Columns - 1) * 6.0f, Rows * 26.0f + (Rows - 1) * 6.0f));
			if(Scale > 0.0f && Scale >= Result.m_Scale)
				Result = {Columns, Scale, MaxTagWidth * Scale};
		}
		return Result;
	}

	inline CUIRect TagRect(CUIRect View, const STagGrid &Grid, int Index)
	{
		return {View.x + (Index % Grid.m_Columns) * (Grid.m_CellWidth + 6.0f * Grid.m_Scale),
			View.y + (Index / Grid.m_Columns) * 32.0f * Grid.m_Scale, Grid.m_CellWidth, 26.0f * Grid.m_Scale};
	}
}

#endif
