#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>

// 聊天滚动条的几何全部来自 ui_token::chat：单位是聊天逻辑单位（1 单位 ≈ 屏幕高度/300），
// 不是 UI 像素，换算口径见 UiTokens.h 该命名空间的说明。
constexpr float QM_CHAT_SCROLLBAR_WIDTH = ui_token::chat::SCROLLBAR_WIDTH;
constexpr float QM_CHAT_SCROLLBAR_MARGIN = ui_token::chat::SCROLLBAR_MARGIN;
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
	// 手机上手指比鼠标粗：轨道要更宽、离屏边更远，否则根本按不到、拖不动。
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	const float Width = std::min(std::max(0.0f, Bounds.w), QM_CHAT_SCROLLBAR_WIDTH * Scale * 2.5f);
	const float MarginScale = OnRight ? 1.5f : 3.0f; // 靠左时再往内收，避开手机圆角
	const float Margin = std::min(QM_CHAT_SCROLLBAR_MARGIN * Scale * MarginScale, std::max(0.0f, (Bounds.w - Width) * 0.5f));
#else
	const float Width = std::min(std::max(0.0f, Bounds.w), QM_CHAT_SCROLLBAR_WIDTH * Scale);
	const float Margin = std::min(QM_CHAT_SCROLLBAR_MARGIN * Scale, std::max(0.0f, (Bounds.w - Width) * 0.5f));
#endif
	// 轨道沿长度方向两端各让出一档外边距：滑块端头不顶到聊天区上下边缘，
	// 整条轨道也不至于顶满历史区那么高（与列表覆盖式轨道同一条内缩语义）。
	const float LengthMargin = QM_CHAT_SCROLLBAR_MARGIN * Scale;
	const float RailHeight = std::max(0.0f, Height - LengthMargin * 2.0f);
	return {OnRight ? Bounds.x + Bounds.w - Margin - Width : Bounds.x + Margin, Top + LengthMargin, Width, RailHeight};
}

// 历史正文与预览都避开轨道及其两侧间距；切换位置不改变内容可用宽度。
inline float QmChatHistoryStartX(bool OnRight)
{
	return OnRight ? 5.0f : QM_CHAT_SCROLLBAR_RESERVE;
}

// 滑块长度：与 CScrollRegion 同一条公式（可见/内容比例 × 轨道长度，夹在最短长度
// 与轨道之间），再按聊天框自己的一档上限压住。聊天一次能看到的行数远少于列表，
// 纯比例算出来的滑块会拉成几乎整条轨道，看起来是一根长条而不是「还能滚多少」。
inline float QmChatScrollbarHandleHeight(float Height, int VisibleLines, int TotalLines, float Scale = 1.0f)
{
	Height = std::max(0.0f, Height);
	const float Ratio = std::clamp(VisibleLines / (float)std::max(TotalLines, 1), 0.0f, 1.0f);
	const float MinThumb = std::min(ui_token::chat::SCROLLBAR_MIN_THUMB * Scale, Height);
	const float MaxThumb = std::max(MinThumb, std::min(Height, Height * ui_token::chat::SCROLLBAR_MAX_THUMB_RATIO));
	return std::clamp(Height * Ratio, MinThumb, MaxThumb);
}

inline CUIRect QmChatScrollbarHandle(const CUIRect &Rail, float Height, float Value)
{
	Height = std::clamp(Height, 0.0f, Rail.h);
	return {Rail.x, Rail.y + (Rail.h - Height) * std::clamp(Value, 0.0f, 1.0f), Rail.w, Height};
}

// 滑块在 CUi 里的身份。聊天是每帧自绘的 HUD，不走菜单树；这里只借 CUi 的
// 悬浮/按下登记给公共滑块绘制入口用，生命周期与本文件内的滚动条绘制一致。
inline const void *QmChatScrollbarId()
{
	static char s_ChatScrollbarId = 0;
	return &s_ChatScrollbarId;
}

// 轨道与滑块都走公共滚动条实现：轨道色直接取列表覆盖式轨道的默认轨道色（CScrollRegionParams），
// 并按当前背景透明度作用域缩放，与 CScrollRegion::DrawOverlayRail 逐项一致；滑块走
// ui_widget::DrawScrollbarHandle（与 CListBox / CScrollRegion 同一个绘制入口，含悬浮/按下
// 反馈），聊天不再自绘一套卡片描边配色。
inline void QmDrawChatScrollbar(CUi *pUi, const CUIRect &Rail, const CUIRect &Thumb, bool Interactive)
{
	if(pUi == nullptr || pUi->MeasurementOnly() || Rail.w <= 0.0f || Rail.h <= 0.0f)
		return;
	// 默认参数只在首次调用时构造一次：它要读 g_Config，不该每帧重算。
	static const CScrollRegionParams s_DefaultListParams;
	DrawRoundedSurface(pUi, Rail, pUi->ScaleBackgroundAlpha(s_DefaultListParams.m_RailBgColor), ui_token::color::COLOR_TRANSPARENT, ui_token::chat::SCROLLBAR_RADIUS);
	CUIRect Handle = Thumb;
	Handle.x = Rail.x;
	Handle.w = Rail.w;
	Handle.h = std::min(std::max(0.0f, Handle.h), Rail.h);
	if(Handle.h <= 0.0f)
		return;
	const ColorRGBA Accent = pUi->QmControlTheme().m_Accent;
	ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(pUi), QmChatScrollbarId(), Handle, Interactive, nullptr, &Accent);
}

#endif
