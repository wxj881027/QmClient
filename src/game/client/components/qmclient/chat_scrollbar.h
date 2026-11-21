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
// 一帧内最多累积的滚轮档数：输入事件可能一帧来好几条，推进要等渲染帧。
// 上界是为了防止聊天被配置或焦点过滤隐藏时步数无限攒着，重新显示时一次跳掉半屏历史。
constexpr int QM_CHAT_WHEEL_MAX_PENDING_STEPS = 3;
// 聊天是行型滚动面：默认每档 3 行，极端大倍率最多 8 行；窄聊天区再按
// 可见行数压低。用户只调全站滚动倍率，不增加聊天专用速度入口。
constexpr int QM_CHAT_WHEEL_MAX_LINES = 8;

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

// 轨道宽度：手机端手指比鼠标粗，要更宽才按得到；设置页预览用 Scale 放大整条滚动条。
inline float QmChatScrollbarWidth(const CUIRect &Bounds, float Scale)
{
	const float Scaled = std::max(0.0f, Scale);
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	return std::min(std::max(0.0f, Bounds.w), QM_CHAT_SCROLLBAR_WIDTH * Scaled * 2.5f);
#else
	return std::min(std::max(0.0f, Bounds.w), QM_CHAT_SCROLLBAR_WIDTH * Scaled);
#endif
}

// 轨道离聊天区边缘的一档外边距（未夹取）；手机端离屏边更远，靠左时再往内收避开圆角。
inline float QmChatScrollbarBaseMargin(bool OnRight, float Scale)
{
	const float Scaled = std::max(0.0f, Scale);
#if defined(CONF_PLATFORM_ANDROID) || defined(CONF_PLATFORM_IOS)
	const float MarginScale = OnRight ? 1.5f : 3.0f;
	return QM_CHAT_SCROLLBAR_MARGIN * Scaled * MarginScale;
#else
	(void)OnRight; // 桌面端左右同档；手机端才按位置加宽。
	return QM_CHAT_SCROLLBAR_MARGIN * Scaled;
#endif
}

// 轨道实际使用的内缩量：同一个外边距，另外夹进聊天区宽度，窄屏也不把轨道挤出屏幕。
// 它同时是轨道沿长度方向两端的内缩量（与列表覆盖式轨道同一条语义）。
inline float QmChatScrollbarMargin(const CUIRect &Bounds, bool OnRight, float Width, float Scale)
{
	return std::min(QmChatScrollbarBaseMargin(OnRight, Scale), std::max(0.0f, (Bounds.w - Width) * 0.5f));
}

// 一次算完的滚动条几何：轨道、滑块、可移动行程与显隐。
struct SQmChatScrollbarFrame
{
	CUIRect m_Rail{};
	CUIRect m_Thumb{};
	float m_TrackRange = 0.0f;
	bool m_Visible = false;
};

// 聊天历史的滚动条几何：轨道、滑块与显隐全部来自公共覆盖式滚动条
// （ui_widget::ResolveOverlayScrollbarFrame，与列表覆盖式轨道同一条算法）。
// 聊天只回答三件事：视口多高、内容总高多少、当前滚到哪（Progress 0 = 顶部/最旧）。
inline SQmChatScrollbarFrame QmResolveChatScrollbarFrame(const CUIRect &Bounds, float Top, float Height, float ContentHeight, float Progress, bool OnRight, float Scale = 1.0f)
{
	SQmChatScrollbarFrame Result;
	const float Width = QmChatScrollbarWidth(Bounds, Scale);
	const float Margin = QmChatScrollbarMargin(Bounds, OnRight, Width, Scale);
	const float SafeProgress = std::isfinite(Progress) ? Progress : 1.0f;
	const float ClampedProgress = std::clamp(SafeProgress, 0.0f, 1.0f);
	const auto Frame = ui_widget::ResolveOverlayScrollbarFrame(Bounds, Top, Height, ContentHeight, ClampedProgress, OnRight, Width, Margin,
		ui_token::chat::SCROLLBAR_MIN_THUMB * std::max(0.0f, Scale));
	if(!Frame.m_Visible)
		return Result;
	Result.m_Rail = Frame.m_Rail;
	Result.m_Thumb = Frame.m_Thumb;
	// 内容刚超过一屏时，按比例算出的滑块会几乎铺满轨道。列表滑块保留可拖动行程，
	// 聊天也采用同一比例公式，只增加一档最长占比，避免看起来像“没有滑块”。
	const float MinThumb = std::min(Result.m_Rail.h, ui_token::chat::SCROLLBAR_MIN_THUMB * std::max(0.0f, Scale));
	const float MaxThumb = std::max(MinThumb, Result.m_Rail.h * ui_token::chat::SCROLLBAR_MAX_THUMB_RATIO);
	Result.m_Thumb.h = std::clamp(Result.m_Thumb.h, MinThumb, MaxThumb);
	Result.m_TrackRange = std::max(0.0f, Result.m_Rail.h - Result.m_Thumb.h);
	Result.m_Thumb.y = Result.m_Rail.y + ClampedProgress * Result.m_TrackRange;
	// 连续偏移越过合法端点时，滑块跟着内容一起越界，回弹时再回到轨道内。
	// Progress 0 = 最旧/顶部，1 = 最新/底部；正越界量表示滑块向下。
	const float Overshoot = SafeProgress - ClampedProgress;
	Result.m_Thumb.y += Overshoot * Result.m_TrackRange;
	Result.m_Visible = Frame.m_Visible;
	return Result;
}

// 只要轨道矩形的位置与长度（设置页预览、聊天布局基准与游戏内绘制共用同一条几何）。
// 轨道与内容多少无关，这里给一个必然溢出的内容高度，只为从公共容器取回轨道那一段。
inline CUIRect QmChatScrollbarRail(const CUIRect &Bounds, float Top, float Height, bool OnRight, float Scale = 1.0f)
{
	return QmResolveChatScrollbarFrame(Bounds, Top, Height, std::max(0.0f, Height) * 2.0f, 0.0f, OnRight, Scale).m_Rail;
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

// 聊天行高不一，可见容量不能按默认行高估算：滚动上限必须从最旧一端量，
// 否则末尾几条偏高时，滚动条已经到顶，最旧一条却还被挡在视口上方。
// pExtentsNewestFirst 按“最新 → 最旧”排列；返回最旧一侧能完整放下的条数。
// 至少保留 1 条，保证单条消息高于视口时仍能被滚到。
inline int QmChatOldestFittingLineCount(const float *pExtentsNewestFirst, int Count, float ViewportHeight)
{
	if(pExtentsNewestFirst == nullptr || Count <= 0)
		return 1;
	const float SafeViewport = std::max(0.0f, ViewportHeight);
	float UsedHeight = 0.0f;
	int Fitting = 0;
	for(int i = Count - 1; i >= 0; --i)
	{
		const float Extent = std::max(0.0f, pExtentsNewestFirst[i]);
		if(Fitting > 0 && UsedHeight + Extent > SafeViewport + 0.001f)
			break;
		UsedHeight += Extent;
		++Fitting;
	}
	return std::max(1, Fitting);
}

// 聊天偏移以消息行为单位，而公共橡皮筋配置是像素。这里按当前行高换算，
// 避免 25px 的回弹被误读成 25 行。
inline float QmChatHistoryBounceLines(float BouncePixels, float LineExtent)
{
	return std::max(0.0f, BouncePixels) / std::max(1.0f, LineExtent);
}

// 手感档位（原版 / 顺滑 / 弹簧）、弹簧响应与阻尼、橡皮筋、动效等级全部来自
// 公共配置 QmNativeWheelScrollConfig；聊天只声明"默认一档 3 行"，不再自己写
// 平滑时长、lerp 系数或阻尼。LineExtent 用于把公共像素配置换算到行单位。
inline SQmScrollConfig QmChatHistoryScrollConfig(float LineExtent = 1.0f, int VisibleLines = 0)
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	// 偏移单位是「行」，全局步长只作为倍率；基准行数与视口上限由公共行数
	// 解析器统一兜底，避免把像素步长直接当成聊天行数。
	const int WheelLines = QmResolveWheelLineCount(
		ui_token::chat::WHEEL_LINES_PER_STEP, QmWheelStepScale(), VisibleLines, QM_CHAT_WHEEL_MAX_LINES);
	Config.m_WheelScale = static_cast<float>(WheelLines);
	Config.m_MaxOverscroll = QmChatHistoryBounceLines((float)g_Config.m_QmUiScrollBounce, LineExtent);
	return Config;
}

// 连续偏移 → 可见行号。行号只决定从哪一条消息开始排版，余量交给
// QmChatHistoryLayoutOffset 做连续像素位移，弹簧和越界回弹才看得见。
inline int QmChatHistoryScrollLine(float Offset, const SQmScrollMetrics &Metrics)
{
	const float Line = std::clamp(Offset, 0.0f, Metrics.MaxOffset());
	return (int)std::floor(Line);
}

// 连续滚动偏移 → 滚动条进度。越界值不夹取：滑块要能跟着弹簧一起过冲回弹。
// 0 = 最旧/顶部，1 = 最新/底部，与公共覆盖式滚动条同口径。
inline float QmChatHistoryScrollbarValue(float Offset, const SQmScrollMetrics &Metrics)
{
	const float MaxOffset = Metrics.MaxOffset();
	if(MaxOffset <= 0.0f)
		return 1.0f;
	return 1.0f - (std::isfinite(Offset) ? Offset : 0.0f) / MaxOffset;
}

// 当前行锚之外的亚行余量，换算成正文整体的像素位移。负数/超过最大偏移
// 时余量自然落在橡皮筋区，回弹过程会连续经过这些位置。
inline float QmChatHistoryLayoutOffset(float Offset, int BacklogLine, float LineExtent)
{
	return (Offset - (float)BacklogLine) * std::max(0.0f, LineExtent);
}

// 滚轮：Steps > 0 表示往更早的历史（偏移增大）。公共实现里 WheelDelta 的正负
// 与偏移方向相反，符号换算只留在这一处，调用点只报方向。
inline void QmChatHistoryWheel(CQmScrollState &State, int Steps, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	if(Steps == 0)
		return;
	// 原始增量必须按滚轮档报送；Config.m_WheelScale 只负责把一档换算成行。
	State.AddWheelImpulse(-(float)Steps * QM_SCROLL_WHEEL_NOTCH, Metrics, Config);
}

// 拖动直接跟手：把目标行写进同一份滚动状态（不经过弹簧）。松手后交给
// Advance——内容变矮或滑块被拖到界外时，越界回弹会把偏移收回合法范围，
// 与列表拖动滚动条是同一条语义。
inline void QmChatHistoryDragToLine(CQmScrollState &State, int Line, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	State.SetOffset((float)Line, Metrics, Config, true);
}

inline void QmChatHistoryDragToOffset(CQmScrollState &State, float Offset, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	State.SetOffset(Offset, Metrics, Config, true);
}

// ---------------------------------------------------------------------------
// 滑块长度、位置与拖动换算（与列表滚动条同一实现）
// ---------------------------------------------------------------------------

// 滑块长度：视口/内容 × 轨道，下夹 ui_token::chat::SCROLLBAR_MIN_THUMB（列表滑块同一档），
// 上不越过轨道。视口与内容只要是同一把尺子比例就成立——这里把内容折成"轨道高"下的
// 等价值再交给同一个公共容器，聊天不维护第二套比例、上限或内缩。
inline float QmChatScrollbarThumbHeight(const CUIRect &Rail, float ViewportHeight, float ContentHeight, float Scale = 1.0f)
{
	const float Height = std::max(0.0f, Rail.h);
	const float Viewport = std::max(0.0f, ViewportHeight);
	const float Content = std::max(0.0f, ContentHeight);
	if(Height <= 0.0f || Viewport <= 0.0f)
		return 0.0f;
	// 内容装得下：滑块占满轨道（列表在不溢出时同样把滑块画成整条）。
	if(Content <= Viewport)
		return Height;
	const float Scaled = std::max(0.0f, Scale);
	const float Margin = QmChatScrollbarBaseMargin(true, Scaled);
	const float Slot = QM_CHAT_SCROLLBAR_WIDTH * Scaled + Margin * 2.0f;
	// 视口取「轨道高 + 两端内缩」，与帧内部减回去的两端内缩正好抵消，轨道长度不变。
	const CUIRect View{Rail.x, Rail.y, Slot, Height + Margin * 2.0f};
	return QmResolveChatScrollbarFrame(View, Rail.y, View.h, View.h * Content / Viewport, 0.0f, true, Scaled).m_Thumb.h;
}

// 兼容入口（设置页预览与聊天布局基准）：给定轨道长度与「可见/总行数」问滑块多长。
// 游戏内走 QmResolveChatScrollbarFrame（真实高度），这里只把行数比例交给同一条实现。
inline float QmChatScrollbarHandleHeight(float RailHeight, int VisibleLines, int TotalLines, float Scale = 1.0f, float RailWidth = 0.0f)
{
	const float Width = RailWidth > 0.0f ? RailWidth : QM_CHAT_SCROLLBAR_WIDTH * std::max(0.0f, Scale);
	const CUIRect Rail{0.0f, 0.0f, std::max(0.0f, Width), std::max(0.0f, RailHeight)};
	return QmChatScrollbarThumbHeight(Rail, (float)std::max(0, VisibleLines), (float)std::max(0, TotalLines), Scale);
}

// 滑块矩形：进度 × 可移动行程，与公共容器同一摆放规则（0 = 顶部/最旧，1 = 底部/最新）。
inline CUIRect QmChatScrollbarHandle(const CUIRect &Rail, float Height, float Value)
{
	const float Clamped = std::clamp(Height, 0.0f, std::max(0.0f, Rail.h));
	const float Travel = std::max(0.0f, Rail.h - Clamped);
	return {Rail.x, Rail.y + Travel * std::clamp(Value, 0.0f, 1.0f), Rail.w, Clamped};
}

// 拖动换算：指针位置 → 进度（0 = 顶部/最旧，1 = 底部/最新）。滑块顶端跟手；
// 指针把滑块拖到轨道外时夹在两端，与列表拖动滚动条同一条边界语义。
inline float QmChatScrollbarProgressFromPointer(const SQmChatScrollbarFrame &Frame, float PointerY, float GrabOffset)
{
	if(Frame.m_TrackRange <= 0.0f)
		return 1.0f;
	const float Top = std::clamp(PointerY - GrabOffset, Frame.m_Rail.y, Frame.m_Rail.y + Frame.m_TrackRange);
	return std::clamp((Top - Frame.m_Rail.y) / Frame.m_TrackRange, 0.0f, 1.0f);
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

// 绘制滑块：横向贴住轨道，纵向保留几何阶段算出的位置。越界回弹时滑块本来就应
// 越过轨道端头，这里若再把 Y 夹回轨道，弹簧手感会在最后一笔被抹掉。
inline CUIRect QmChatScrollbarDrawHandleRect(const CUIRect &Rail, const CUIRect &Thumb)
{
	CUIRect Handle = Thumb;
	Handle.x = Rail.x;
	Handle.w = Rail.w;
	Handle.h = std::min(std::max(0.0f, Handle.h), Rail.h);
	return Handle;
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
	const CUIRect Handle = QmChatScrollbarDrawHandleRect(Rail, Thumb);
	if(Handle.h <= 0.0f)
		return;
	const ColorRGBA Accent = pUi->QmControlTheme().m_Accent;
	const bool PublicFeedbackActive = pUi->VisualHotItem(QmChatScrollbarId()) || pUi->CheckActiveItem(QmChatScrollbarId());
	const CUIRect FeedbackHandle = QmChatScrollbarFeedbackRect(Handle, Interactive && Hovered, Interactive && Dragging, PublicFeedbackActive);
	ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(pUi), QmChatScrollbarId(), FeedbackHandle, Interactive, nullptr, &Accent);
}

#endif
