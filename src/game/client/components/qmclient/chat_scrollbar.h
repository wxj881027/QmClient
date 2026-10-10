#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/ui.h>

#include <algorithm>

constexpr float QM_CHAT_SCROLLBAR_WIDTH = 3.0f;
constexpr float QM_CHAT_SCROLLBAR_MARGIN = 2.0f;
constexpr float QM_CHAT_SCROLLBAR_RESERVE = QM_CHAT_SCROLLBAR_WIDTH + QM_CHAT_SCROLLBAR_MARGIN * 2.0f;

// 保留聊天整行的横向空间，轨道隐藏、重开或换边时不会被上一帧正文边界挤出屏幕。
// 输入、按钮和命令提示超出预估宽度时也纳入 HUD 的实际测量范围。
class CQmChatVisibleBounds
{
	CUIRect m_Rect;
	bool m_Valid = false;

public:
	explicit CQmChatVisibleBounds(const CUIRect &ChatRect) :
		m_Rect{ChatRect.x, 0.0f, std::max(0.0f, ChatRect.w), 0.0f}
	{
	}

	void Extend(const CUIRect &Rect)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		const float Left = std::min(m_Rect.x, Rect.x);
		const float Right = std::max(m_Rect.x + m_Rect.w, Rect.x + Rect.w);
		const float Top = m_Valid ? std::min(m_Rect.y, Rect.y) : Rect.y;
		const float Bottom = m_Valid ? std::max(m_Rect.y + m_Rect.h, Rect.y + Rect.h) : Rect.y + Rect.h;
		m_Rect = {Left, Top, Right - Left, Bottom - Top};
		m_Valid = true;
	}

	bool Valid() const { return m_Valid; }
	const CUIRect &Rect() const { return m_Rect; }
};

inline CUIRect QmChatScrollbarRail(const CUIRect &Bounds, float Top, float Height, bool OnRight, float Scale = 1.0f)
{
	const float Width = std::min(std::max(0.0f, Bounds.w), QM_CHAT_SCROLLBAR_WIDTH * Scale);
	const float Margin = std::min(QM_CHAT_SCROLLBAR_MARGIN * Scale, std::max(0.0f, (Bounds.w - Width) * 0.5f));
	return {OnRight ? Bounds.x + Bounds.w - Margin - Width : Bounds.x + Margin, Top, Width, std::max(0.0f, Height)};
}

// 历史正文与预览都避开轨道及其两侧间距；切换位置不改变内容可用宽度。
inline float QmChatHistoryStartX(bool OnRight)
{
	return OnRight ? 5.0f : QM_CHAT_SCROLLBAR_RESERVE;
}

inline float QmChatScrollbarHandleHeight(float Height, int VisibleLines, int TotalLines, float Scale = 1.0f)
{
	Height = std::max(0.0f, Height);
	const float Ratio = std::clamp(VisibleLines / (float)std::max(TotalLines, 1), 0.08f, 1.0f);
	return std::clamp(Height * Ratio, std::min(12.0f * Scale, Height), Height);
}

inline CUIRect QmChatScrollbarHandle(const CUIRect &Rail, float Height, float Value)
{
	Height = std::clamp(Height, 0.0f, Rail.h);
	return {Rail.x, Rail.y + (Rail.h - Height) * std::clamp(Value, 0.0f, 1.0f), Rail.w, Height};
}

// 游戏内与设置预览复用公共强调色、卡片描边及圆角绘制入口。
inline void QmDrawChatScrollbar(CUi *pUi, const CUIRect &Rail, float HandleY, float HandleHeight, bool Active)
{
	if(Rail.w <= 0.0f || Rail.h <= 0.0f)
		return;
	const SUiTheme &Theme = pUi->QmControlTheme();
	const ColorRGBA Border = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardBorderColor, true));
	DrawRoundedSurface(pUi, Rail, Theme.m_Surface, Border, Rail.w * 0.5f, 0.5f);
	const CUIRect Handle{Rail.x, HandleY, Rail.w, HandleHeight};
	DrawRoundedSurface(pUi, Handle, Theme.m_Accent.WithMultipliedAlpha(Active ? 1.0f : 0.85f), Border, Rail.w * 0.5f, 0.5f);
}

#endif
