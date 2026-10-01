#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_demo_ui
{
	constexpr float DISPLAY_HEIGHT = 116.0f;

	inline CUIRect PlayerRect(const CUIRect &Screen, bool DisplayExpanded)
	{
		const float Width = std::min(640.0f, std::max(0.0f, Screen.w - 24.0f));
		const float Height = 120.0f + (DisplayExpanded ? DISPLAY_HEIGHT + 6.0f : 0.0f);
		return {Screen.x + (Screen.w - Width) * 0.5f, Screen.y + Screen.h - Height - 12.0f, Width, Height};
	}

	constexpr float TransportWidth(float ButtonSize, bool ShowSkipDuration)
	{
		// 十个播放图标、倍速文字与间距；短回放没有时长下拉框。
		return 10.0f * ButtonSize + 36.0f + 39.0f + (ShowSkipDuration ? 59.0f : 0.0f);
	}

	inline float TransportButtonSize(float PanelWidth, bool ShowSkipDuration)
	{
		return std::clamp((PanelWidth - 16.0f - TransportWidth(0.0f, ShowSkipDuration)) / 10.0f, 0.0f, 22.0f);
	}

	struct SCutControls
	{
		CUIRect m_Start;
		CUIRect m_End;
		CUIRect m_Add;
		CUIRect m_Clear;
		CUIRect m_Preview;
		CUIRect m_Export;
	};

	inline SCutControls CutControls(CUIRect Bar, float ButtonSize)
	{
		SCutControls Controls;
		Bar.VSplitRight(Bar.h, &Bar, &Controls.m_Export);
		Bar.VSplitRight(3.0f, &Bar, nullptr);
		Bar.VSplitRight(Bar.h, &Bar, &Controls.m_Preview);
		Bar.VSplitRight(8.0f, &Bar, nullptr);
		Bar.VSplitRight(ButtonSize, &Bar, &Controls.m_Clear);
		Bar.VSplitRight(3.0f, &Bar, nullptr);
		Bar.VSplitRight(ButtonSize, &Bar, &Controls.m_Add);
		Bar.VSplitRight(8.0f, &Bar, nullptr);
		Bar.VSplitMid(&Controls.m_Start, &Controls.m_End, 3.0f);
		return Controls;
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
