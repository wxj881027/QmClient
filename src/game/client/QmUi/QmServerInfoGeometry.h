#ifndef GAME_CLIENT_QMUI_QMSERVERINFOGEOMETRY_H
#define GAME_CLIENT_QMUI_QMSERVERINFOGEOMETRY_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

namespace QmServerInfoGeometry
{
	constexpr float GAP = 4.0f;

	struct SLayout
	{
		CUIRect m_Server;
		CUIRect m_Game;
		CUIRect m_Motd;
		CUIRect m_Tags;
		CUIRect m_aDividers[3];
	};

	inline float SplitSize(float Available, int Ratio, float MinimumFirst, float MinimumSecond)
	{
		Available = std::max(0.0f, Available);
		// 小窗口仍保留两侧，不让最小尺寸相互挤压成负数。
		if(Available < MinimumFirst + MinimumSecond)
			return Available * MinimumFirst / (MinimumFirst + MinimumSecond);
		return std::clamp(Available * std::clamp(Ratio, 1, 999) / 1000.0f, MinimumFirst, Available - MinimumSecond);
	}

	inline int DragRatio(float Pointer, float Origin, float GrabOffset, float Available, float MinimumFirst, float MinimumSecond)
	{
		if(Available <= 0.0f)
			return 500;
		const int Ratio = (int)std::lround(std::clamp((Pointer - Origin - GrabOffset) / Available, 0.0f, 1.0f) * 1000.0f);
		return std::clamp((int)std::lround(SplitSize(Available, Ratio, MinimumFirst, MinimumSecond) / Available * 1000.0f), 1, 999);
	}

	inline SLayout Layout(CUIRect View, int Rows, int TopColumns, int BottomColumns, bool ShowTags)
	{
		View.w = std::max(0.0f, View.w);
		View.h = std::max(0.0f, View.h);
		const float GapX = std::min(GAP, View.w);
		const float GapY = std::min(GAP, View.h);
		const float TopHeight = SplitSize(View.h - GapY, Rows, 110.0f, ShowTags ? 160.0f : 100.0f);
		const float TopWidth = SplitSize(View.w - GapX, TopColumns, 280.0f, 280.0f);
		const float BottomWidth = ShowTags ? SplitSize(View.w - GapX, BottomColumns, 180.0f, 220.0f) : View.w;
		const float BottomY = View.y + TopHeight + GapY;
		const float BottomHeight = View.h - TopHeight - GapY;
		return {
			{View.x, View.y, TopWidth, TopHeight},
			{View.x + TopWidth + GapX, View.y, View.w - TopWidth - GapX, TopHeight},
			{View.x, BottomY, BottomWidth, BottomHeight},
			{View.x + BottomWidth + GapX, BottomY, ShowTags ? View.w - BottomWidth - GapX : 0.0f, BottomHeight},
			{{View.x, View.y + TopHeight, View.w, GapY},
				{View.x + TopWidth, View.y, GapX, TopHeight},
				{View.x + BottomWidth, BottomY, ShowTags ? GapX : 0.0f, BottomHeight}}};
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
}

#endif
