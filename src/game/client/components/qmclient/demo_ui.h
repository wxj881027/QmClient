#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_demo_ui
{
	constexpr float DISPLAY_HEIGHT = 116.0f;

	inline CUIRect PlayerRect(const CUIRect &Screen, float Height)
	{
		const float Width = std::min(760.0f, std::max(0.0f, Screen.w - 24.0f));
		return {Screen.x + (Screen.w - Width) * 0.5f, Screen.y + Screen.h - Height - 12.0f, Width, Height};
	}

	inline CUIRect DraggedPlayerRect(const CUIRect &Screen, const CUIRect &Base, float OffsetX, float OffsetY)
	{
		return {
			std::clamp(Base.x + OffsetX, Screen.x, Screen.x + Screen.w - Base.w),
			std::clamp(Base.y + OffsetY, Screen.y, std::max(Screen.y, Screen.y + Screen.h - Base.h)),
			Base.w,
			Base.h};
	}

	inline float SliceContentHeight(int SegmentCount, bool DisplayExpanded)
	{
		const int VisibleSegments = std::min(std::max(SegmentCount, 0), 4);
		const float SegmentsHeight = SegmentCount > 0 ? 20.0f + VisibleSegments * 20.0f + (SegmentCount > 4 ? 16.0f : 0.0f) + 10.0f : 0.0f;
		return 154.0f + SegmentsHeight + (DisplayExpanded ? DISPLAY_HEIGHT + 4.0f : 0.0f);
	}

	inline float RenderContentHeight(bool DisplayExpanded, bool Online)
	{
		return 120.0f + (DisplayExpanded ? DISPLAY_HEIGHT + 4.0f : 0.0f) + (Online ? 30.0f : 0.0f);
	}

	inline CUIRect PopupRect(const CUIRect &Screen, float ContentHeight, float Width = 560.0f)
	{
		const float PopupWidth = std::min(Width, std::max(0.0f, Screen.w - 24.0f));
		const float PopupHeight = std::min(ContentHeight, std::max(0.0f, Screen.h - 24.0f));
		return {Screen.x + (Screen.w - PopupWidth) * 0.5f, Screen.y + (Screen.h - PopupHeight) * 0.5f, PopupWidth, PopupHeight};
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
