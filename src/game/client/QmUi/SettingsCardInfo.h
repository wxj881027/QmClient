#ifndef GAME_CLIENT_QMUI_SETTINGSCARDINFO_H
#define GAME_CLIENT_QMUI_SETTINGSCARDINFO_H
#include "SettingsCardGeometry.h"
#include "UiTokens.h"

#include <algorithm>

// 说明入口与右侧宽度、折叠操作使用同一行和同一尺寸，窄卡片时不侵入标题区外。
inline CUIRect ResolveSettingsCardInfoRect(const SSettingsCardFrame &Frame)
{
	const float Gap = ui_token::spacing::XS;
	const float Right = std::max(Frame.m_HeaderRect.x, Frame.m_HandleRect.x - Frame.m_HandleRect.w - 2.0f * Gap);
	const float Width = std::max(0.0f, std::min(Frame.m_HandleRect.w, Right - Frame.m_HeaderRect.x));
	return {Right - Width, Frame.m_HandleRect.y, Width, Width > 0.0f ? Frame.m_HandleRect.h : 0.0f};
}

inline float ResolveSettingsCardInfoWidth(float ScreenWidth, float UiScale)
{
	// 限制阅读行宽，同时保留窄视口的边距；实际换行使用同一宽度测量和绘制。
	return std::max(1.0f, std::min(300.0f * std::max(0.1f, UiScale), ScreenWidth * 0.6f));
}
struct IUiContext;
void RenderSettingsCardInfo(const IUiContext &Ctx, const SSettingsCardSpec &Spec, CUIRect Button, float DrawAlpha = 1.0f);
#endif
