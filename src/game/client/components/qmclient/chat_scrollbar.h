#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_SCROLLBAR_H

#include <engine/shared/config.h>

#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <cmath>

// 聊天滚动条的几何全部来自 ui_token::chat：单位是聊天逻辑单位（1 单位 ≈ 屏幕高度/300），
// 不是 UI 像素，换算口径见 UiTokens.h 该命名空间的说明。
constexpr float QM_CHAT_SCROLLBAR_WIDTH = ui_token::chat::SCROLLBAR_WIDTH;
constexpr float QM_CHAT_SCROLLBAR_MARGIN = ui_token::chat::SCROLLBAR_MARGIN;
constexpr float QM_CHAT_SCROLLBAR_RESERVE = QM_CHAT_SCROLLBAR_WIDTH + QM_CHAT_SCROLLBAR_MARGIN * 2.0f;

// 一个滚轮档的标准增量（与 CUi 滚轮归属帧同口径：上滚 +120、下滚 -120）。
// 滚轮是离散输入，聊天只在输入事件里记方向，偏移推进统一交给 QmScroll 的
// AddWheelImpulse，所以这里声明的是"一档"的语义常量，不让调用点自己写 120。
constexpr float QM_CHAT_WHEEL_NOTCH = 120.0f;
// 一帧内最多累积的滚轮档数：输入事件可能一帧来好几条，推进要等渲染帧。
// 上界是为了防止聊天被配置或焦点过滤隐藏时步数无限攒着，重新显示时一次跳掉半屏历史。
constexpr int QM_CHAT_WHEEL_MAX_PENDING_STEPS = 3;

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

// ---------------------------------------------------------------------------
// 历史滚动推进（与列表 / 设置页共用 QmScroll）
// ---------------------------------------------------------------------------

// 聊天历史的滚动偏移单位是"行"：视口 = 一次能显示的行数，内容 = 总行数，
// 于是最大偏移（行）= 总行数 − 可见行数，与旧的 ClampBacklogLine 完全等价。
// 单位选"行"而不是像素，是为了让正文（按整行渲染）与滚动条共用同一份偏移。
inline SQmScrollMetrics QmChatHistoryScrollMetrics(int TotalLines, int VisibleLines)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = (float)std::max(1, VisibleLines);
	Metrics.m_ContentSize = (float)std::max(0, TotalLines);
	return Metrics;
}

// 手感档位（原版 / 顺滑 / 弹簧）、弹簧响应与阻尼、橡皮筋、动效等级全部来自
// 公共配置 QmNativeWheelScrollConfig；聊天只声明"一档 = 一行"，不再自己写
// 平滑时长、lerp 系数或阻尼。
inline SQmScrollConfig QmChatHistoryScrollConfig()
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	Config.m_WheelScale = 1.0f;
	return Config;
}

// 连续偏移 → 可见行号：正文是按整行渲染的，绘制前取最近的整行。
inline int QmChatHistoryScrollLine(float Offset, const SQmScrollMetrics &Metrics)
{
	const float Line = std::clamp(Offset, 0.0f, Metrics.MaxOffset());
	return (int)std::lround(Line);
}

// 滚轮：Steps > 0 表示往更早的历史（偏移增大）。公共实现里 WheelDelta 的正负
// 与偏移方向相反，符号换算只留在这一处，调用点只报方向。
inline void QmChatHistoryWheel(CQmScrollState &State, int Steps, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	if(Steps == 0)
		return;
	State.AddWheelImpulse(-(float)Steps * QM_CHAT_WHEEL_NOTCH, Metrics, Config);
}

// 拖动直接跟手：把目标行写进同一份滚动状态（不经过弹簧）。松手后交给
// Advance——内容变矮或滑块被拖到界外时，越界回弹会把偏移收回合法范围，
// 与列表拖动滚动条是同一条语义。
inline void QmChatHistoryDragToLine(CQmScrollState &State, int Line, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	State.SetOffset((float)Line, Metrics, Config, true);
}

// ---------------------------------------------------------------------------
// 滚动条几何与反馈
// ---------------------------------------------------------------------------

// 滑块悬浮/按下放大在轨道长度方向要预留的余量（两端各一份）。
// 放大档来自公共滑块（ui_token::feedback::HANDLE_*_GROW），基准宽是一档
// SCROLLBAR_WIDTH，所以按「轨道实际宽度 × 放大档 / 基准宽」等比换算：
// 预览放大（Scale = 2）与手机加宽轨道都能自动同位。
inline float QmChatScrollbarFeedbackInset(float RailWidth, float RailHeight)
{
	const float Desired = std::max(0.0f, RailWidth) * (ui_token::feedback::HANDLE_PRESS_GROW / ui_token::chat::SCROLLBAR_WIDTH);
	const float Cap = std::max(0.0f, RailHeight) * ui_token::chat::SCROLLBAR_FEEDBACK_INSET_MAX_RATIO;
	return std::min(Desired, Cap);
}

// 轨道内可用的滑块长度：扣掉两端反馈余量，放大后的滑块仍完整落在轨道内。
inline float QmChatScrollbarUsableLength(float RailWidth, float RailHeight)
{
	const float Inset = QmChatScrollbarFeedbackInset(RailWidth, RailHeight);
	return std::max(0.0f, std::max(0.0f, RailHeight) - Inset * 2.0f);
}

// 滑块长度：与 CScrollRegion 同一条公式（可见/内容比例 × 可用轨道长度，夹在最短
// 长度与浓度上限之间），再按聊天框自己的一档上限压住。聊天一次能看到的行数远少于
// 列表，纯比例算出来的滑块会拉成几乎整条轨道，看起来是一根长条而不是「还能滚多少」。
// 结果永不超过扣掉反馈余量后的可用长度，所以滑块不会长出轨道。
inline float QmChatScrollbarHandleHeight(float RailHeight, int VisibleLines, int TotalLines, float Scale = 1.0f, float RailWidth = 0.0f)
{
	const float ScaledWidth = RailWidth > 0.0f ? RailWidth : QM_CHAT_SCROLLBAR_WIDTH * std::max(0.0f, Scale);
	const float Available = QmChatScrollbarUsableLength(ScaledWidth, RailHeight);
	if(Available <= 0.0f)
		return 0.0f;
	const float Ratio = std::clamp(VisibleLines / (float)std::max(TotalLines, 1), 0.0f, 1.0f);
	const float MinThumb = std::min(ui_token::chat::SCROLLBAR_MIN_THUMB * std::max(0.0f, Scale), Available);
	const float MaxThumb = std::max(MinThumb, Available * ui_token::chat::SCROLLBAR_MAX_THUMB_RATIO);
	return std::clamp(Available * Ratio, MinThumb, std::min(MaxThumb, Available));
}

// 滑块在轨道里的可移动行程：可用长度再扣掉滑块自己，两端就是反馈余量。
inline float QmChatScrollbarTrackRange(const CUIRect &Rail, float HandleHeight)
{
	const float Available = QmChatScrollbarUsableLength(Rail.w, Rail.h);
	return std::max(0.0f, Available - std::clamp(HandleHeight, 0.0f, Available));
}

inline CUIRect QmChatScrollbarHandle(const CUIRect &Rail, float Height, float Value)
{
	const float Inset = QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	const float Available = QmChatScrollbarUsableLength(Rail.w, Rail.h);
	Height = std::clamp(Height, 0.0f, Available);
	const float Travel = std::max(0.0f, Available - Height);
	return {Rail.x, Rail.y + Inset + Travel * std::clamp(Value, 0.0f, 1.0f), Rail.w, Height};
}

// 鼠标位置 → 0..1 的滑块比例（拖动时只换算比例，滚动值仍由 BacklogLineToScrollbarValue 负责）。
inline float QmChatScrollbarValueFromTop(const CUIRect &Rail, float HandleTop, float TrackRange)
{
	if(TrackRange <= 0.0f)
		return 1.0f;
	const float Start = Rail.y + QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	return std::clamp((HandleTop - Start) / TrackRange, 0.0f, 1.0f);
}

// 拖动时滑块顶端的合法位置：指针不能把滑块拖出轨道（含两端反馈余量）。
inline float QmChatScrollbarClampTop(const CUIRect &Rail, float HandleTop, float TrackRange)
{
	const float Start = Rail.y + QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	return std::clamp(HandleTop, Start, Start + TrackRange);
}

// 悬浮/按下的放大：公共绘制入口（ui_widget::DrawScrollbarHandle）按 CUi 的热项与
// 活动项自己放大，但游戏内 HUD 帧没有菜单的 CUi::Update() 去提交热项，悬浮态读不到，
// 于是滑块静止时"悬浮不放大"。这里用同一档参数补上缺失的那一档；热项已被提交
// （菜单 / 计分板帧）或正在拖动（活动项）时不补，避免同一帧放大两次。
inline CUIRect QmChatScrollbarFeedbackRect(const CUIRect &Handle, bool Hovered, bool Dragging, bool PublicFeedbackActive)
{
	if(!Hovered || Dragging || PublicFeedbackActive)
		return Handle;
	CUIRect Grown = Handle;
	Grown.Margin(-ui_token::feedback::HANDLE_HOVER_GROW, &Grown);
	return Grown;
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
// 反馈），聊天不再自绘一套卡片描边配色。Interactive = false 供设置页的静态预览使用。
inline void QmDrawChatScrollbar(CUi *pUi, const CUIRect &Rail, const CUIRect &Thumb, bool Interactive, bool Hovered = false, bool Dragging = false)
{
	if(pUi == nullptr || pUi->MeasurementOnly() || Rail.w <= 0.0f || Rail.h <= 0.0f)
		return;
	// 默认参数只在首次调用时构造一次：它要读 g_Config，不该每帧重算。
	static const CScrollRegionParams s_DefaultListParams;
	DrawRoundedSurface(pUi, Rail, pUi->ScaleBackgroundAlpha(s_DefaultListParams.m_RailBgColor), ui_token::color::COLOR_TRANSPARENT, ui_token::chat::SCROLLBAR_RADIUS);
	CUIRect Handle = Thumb;
	Handle.x = Rail.x;
	Handle.w = Rail.w;
	// 滑块严格落在轨道内：高度不超可用长度，顶端不越过两端反馈余量。
	// 旧实现直接把比例长度交给公共入口，按下时公共入口向外扩两档，顶端会顶出轨道上端。
	const float Inset = QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	const float Available = QmChatScrollbarUsableLength(Rail.w, Rail.h);
	Handle.h = std::min(std::max(0.0f, Handle.h), Available);
	if(Handle.h <= 0.0f)
		return;
	const float TrackRange = std::max(0.0f, Available - Handle.h);
	Handle.y = std::clamp(Handle.y, Rail.y + Inset, Rail.y + Inset + TrackRange);
	const ColorRGBA Accent = pUi->QmControlTheme().m_Accent;
	const bool PublicFeedbackActive = pUi->VisualHotItem(QmChatScrollbarId()) || pUi->CheckActiveItem(QmChatScrollbarId());
	const CUIRect FeedbackHandle = QmChatScrollbarFeedbackRect(Handle, Interactive && Hovered, Interactive && Dragging, PublicFeedbackActive);
	ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(pUi), QmChatScrollbarId(), FeedbackHandle, Interactive, nullptr, &Accent);
}

#endif
