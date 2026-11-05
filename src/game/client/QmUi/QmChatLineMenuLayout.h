#ifndef GAME_CLIENT_QMUI_QMCHATLINEMENULAYOUT_H
#define GAME_CLIENT_QMUI_QMCHATLINEMENULAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>

namespace qm_chat_line_menu
{
	enum EEntry
	{
		COPY,
		ADD_ONE,
		REPLY,
		SPECTATE,
		MUTE_PLAYER,
		ADD_BLOCKED_WORD,
		COPY_NAME,
		NUM_ENTRIES,
	};

	constexpr float WIDTH = 200.0f;
	// 显式传给弹层的总内边距，包含原有外壳与内容各 5 个单位的留白。
	constexpr float CONTENT_INSET = 10.0f;
	constexpr float SCREEN_MARGIN = 5.0f;
	constexpr float HEADER_HEIGHT = 34.0f;
	constexpr float HEADER_DIVIDER_HEIGHT = 4.0f;
	constexpr float HEADER_SPACING = 4.0f;
	constexpr float BUTTON_HEIGHT = 23.0f;
	constexpr float BUTTON_SPACING = 3.0f;
	constexpr float GROUP_DIVIDER_HEIGHT = 2.0f;
	constexpr float GROUP_SPACING = 3.0f;
	constexpr float HEIGHT = 2.0f * CONTENT_INSET + HEADER_HEIGHT + HEADER_DIVIDER_HEIGHT + HEADER_SPACING +
				 NUM_ENTRIES * (BUTTON_HEIGHT + BUTTON_SPACING) + GROUP_DIVIDER_HEIGHT + GROUP_SPACING;

	struct SLayout
	{
		CUIRect m_Header;
		CUIRect m_HeaderDivider;
		CUIRect m_GroupDivider;
		std::array<CUIRect, NUM_ENTRIES> m_aButtons;
	};

	inline SLayout LayoutContent(CUIRect View)
	{
		SLayout Layout;
		View.HSplitTop(HEADER_HEIGHT, &Layout.m_Header, &View);
		View.HSplitTop(HEADER_DIVIDER_HEIGHT, &Layout.m_HeaderDivider, &View);
		View.HSplitTop(HEADER_SPACING, nullptr, &View);
		for(int Entry = 0; Entry < NUM_ENTRIES; ++Entry)
		{
			if(Entry == MUTE_PLAYER)
			{
				View.HSplitTop(GROUP_DIVIDER_HEIGHT, &Layout.m_GroupDivider, &View);
				View.HSplitTop(GROUP_SPACING, nullptr, &View);
			}
			View.HSplitTop(BUTTON_HEIGHT, &Layout.m_aButtons[Entry], &View);
			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		}
		return Layout;
	}

	inline CUIRect PopupRect(vec2 MousePos, const CUIRect &Screen)
	{
		CUIRect Rect{MousePos.x, MousePos.y, WIDTH, HEIGHT};
		const float MinX = Screen.x + SCREEN_MARGIN;
		const float MinY = Screen.y + SCREEN_MARGIN;
		const float MaxX = std::max(MinX, Screen.x + Screen.w - SCREEN_MARGIN - Rect.w);
		const float MaxY = std::max(MinY, Screen.y + Screen.h - SCREEN_MARGIN - Rect.h);
		// 沿用靠边时向左、向上展开，再限制外框位置，四边均保留留白。
		if(Rect.x > MaxX)
			Rect.x -= Rect.w;
		if(Rect.y > MaxY)
			Rect.y -= Rect.h;
		Rect.x = std::clamp(Rect.x, MinX, MaxX);
		Rect.y = std::clamp(Rect.y, MinY, MaxY);
		return Rect;
	}
}

#endif
