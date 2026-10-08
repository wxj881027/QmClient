#ifndef GAME_CLIENT_QMUI_SETTINGSCARDINFO_H
#define GAME_CLIENT_QMUI_SETTINGSCARDINFO_H
#include "SettingsCardGeometry.h"
#include "SettingsCardWidth.h"
#include "UiTokens.h"

#include <algorithm>

// 说明入口与右侧宽度、折叠操作使用同一行和同一尺寸，窄卡片时不侵入标题区外。
inline CUIRect ResolveSettingsCardInfoRect(const SSettingsCardFrame &Frame)
{
	const float Gap = ui_token::spacing::XS;
	const float Right = std::max(Frame.m_HeaderRect.x, SettingsCardWidthButtonRect(Frame).x - Gap);
	const float Width = std::max(0.0f, std::min(Frame.m_HandleRect.w, Right - Frame.m_HeaderRect.x));
	return {Right - Width, Frame.m_HandleRect.y, Width, Width > 0.0f ? Frame.m_HandleRect.h : 0.0f};
}

// 卡头从右向左分配折叠、宽度、说明及定位入口；所有文字使用剩余区域。
inline SSettingsCardFrame ResolveSettingsCardHeaderActions(SSettingsCardFrame Frame, float LeadingWidth, bool HasInfo)
{
	const float Gap = ui_token::spacing::XS;
	float ActionLeft = HasInfo ? ResolveSettingsCardInfoRect(Frame).x : SettingsCardWidthButtonRect(Frame).x;
	if(LeadingWidth > 0.0f)
	{
		const float Right = std::max(Frame.m_HeaderRect.x, ActionLeft - Gap);
		const float AvailableWidth = std::max(0.0f, Right - Frame.m_HeaderRect.x);
		// 窄卡片将定位压为图标，给标题保留可读宽度。
		const float DesiredWidth = AvailableWidth < LeadingWidth + Frame.m_HandleRect.w * 4.0f ? Frame.m_HandleRect.w : LeadingWidth;
		const float Width = std::min(DesiredWidth, AvailableWidth);
		Frame.m_LeadingHeaderActionRect = {Right - Width, Frame.m_HandleRect.y, Width, Width > 0.0f ? Frame.m_HandleRect.h : 0.0f};
		ActionLeft = Frame.m_LeadingHeaderActionRect.x;
	}
	Frame.m_TitleRect.w = std::min(Frame.m_TitleRect.w, std::max(0.0f, ActionLeft - Gap - Frame.m_TitleRect.x));
	Frame.m_SubtitleRect.w = Frame.m_TitleRect.w;
	return Frame;
}

inline float ResolveSettingsCardInfoWidth(float ScreenWidth, float UiScale)
{
	// 限制阅读行宽，同时保留窄视口的边距；实际换行使用同一宽度测量和绘制。
	return std::max(1.0f, std::min(300.0f * std::max(0.1f, UiScale), ScreenWidth * 0.6f));
}
struct IUiContext;
void RenderSettingsCardInfo(const IUiContext &Ctx, const SSettingsCardSpec &Spec, CUIRect Button, float DrawAlpha = 1.0f);
#endif
