#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SCROLL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SCROLL_H

#include <game/client/ui_scrollregion.h>

// 计分板占用鼠标输入时同步转交 UI；截图等组合键继续下传给绑定系统。
template<typename TUi>
inline bool QmScoreboardUiInput(TUi &Ui, bool Interactive, const IInput::CEvent &Event, bool ReservedShortcut = false)
{
	if(!Interactive || ReservedShortcut)
		return false;
	Ui.OnInput(Event);
	return true;
}

// 轨道和全部滑块状态共同跟随面板内容淡出，防止关闭后残留。
inline void QmScoreboardScrollAlpha(CScrollRegionParams &Params, float ContentAlpha)
{
	const float Alpha = std::clamp(ContentAlpha, 0.0f, 1.0f);
	Params.m_ClipBgColor.a *= Alpha;
	Params.m_ScrollbarBgColor.a *= Alpha;
	Params.m_RailBgColor.a *= Alpha;
	Params.m_SliderColor.a *= Alpha;
	Params.m_SliderColorHover.a *= Alpha;
	Params.m_SliderColorGrabbed.a *= Alpha;
}

#endif
