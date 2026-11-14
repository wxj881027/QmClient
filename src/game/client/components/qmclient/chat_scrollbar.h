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

// 聊天历史的滚动条几何。轨道与滑块不再由聊天自己算：这里把"视口"和"内容"交给
// 公共滚动容器 CQmScrollController，它与列表覆盖式轨道（CScrollRegion::DoSlider）
// 是同一条算法——轨道 = 视口，两端各让出一档内缩；滑块 = 视口/内容 × 轨道，夹在
// ui_token::chat::SCROLLBAR_MIN_THUMB 与轨道之间；位置 = 进度 × (轨道 − 滑块)。
// 聊天只回答三件事：视口多高、内容总高多少、当前滚到哪（Progress 0 = 顶部/最旧）。
inline SQmChatScrollbarFrame QmResolveChatScrollbarFrame(const CUIRect &Bounds, float Top, float Height, float ContentHeight, float Progress, bool OnRight, float Scale = 1.0f)
{
	SQmChatScrollbarFrame Result;
	const float ViewHeight = std::max(0.0f, Height);
	const float Content = std::max(0.0f, ContentHeight);
	// 内容装得下就没有滚动条：与列表一致，不留一根占满轨道却动不了的滑块。
	if(Bounds.w <= 0.0f || ViewHeight <= 0.0f || Content <= ViewHeight)
		return Result;
	const float Width = QmChatScrollbarWidth(Bounds, Scale);
	const float Margin = QmChatScrollbarMargin(Bounds, OnRight, Width, Scale);
	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::VERTICAL;
	// 公共容器按 margin 把轨道在宽度方向两侧各收一次；槽位宽度补回两份，
	// 轨道净宽才仍然是一档 SCROLLBAR_WIDTH。
	Style.m_ScrollbarWidth = Width + Margin * 2.0f;
	Style.m_ScrollbarMargin = Margin;
	Style.m_MinThumbHeight = ui_token::chat::SCROLLBAR_MIN_THUMB * std::max(0.0f, Scale);
	Style.m_ReserveScrollbarSpace = false;

	const CUIRect View{Bounds.x, Top, Bounds.w, ViewHeight};
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = ViewHeight;
	Metrics.m_ContentSize = Content;
	// 进度换成公共容器认的"偏移"，行程换算由它自己完成，聊天不再维护一套 lerp。
	CQmScrollState ThumbState;
	ThumbState.SetOffset(std::clamp(Progress, 0.0f, 1.0f) * Metrics.MaxOffset(), Metrics);
	const CQmScrollController Controller;
	const SQmScrollContainerFrame Frame = Controller.PreviewFrame(ThumbState, View, Content, Style);
	if(!Frame.m_ScrollbarVisible)
		return Result;
	Result.m_Visible = true;
	Result.m_Rail = Frame.m_ScrollbarTrackRect;
	Result.m_Thumb = Frame.m_ScrollbarThumbRect;
	if(!OnRight)
	{
		// 公共容器只画右侧轨道；靠左时把整条轨道镜像到聊天区左缘，内缩量同档。
		Result.m_Rail.x = Bounds.x + (Bounds.w - (Result.m_Rail.x - Bounds.x) - Result.m_Rail.w);
		Result.m_Thumb.x = Result.m_Rail.x;
	}
	Result.m_TrackRange = std::max(0.0f, Result.m_Rail.h - Result.m_Thumb.h);
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

// 手感档位（原版 / 顺滑 / 弹簧）、弹簧响应与阻尼、橡皮筋、动效等级全部来自
// 公共配置 QmNativeWheelScrollConfig；聊天只声明"一档 = 一行"，不再自己写
// 平滑时长、lerp 系数或阻尼。
inline SQmScrollConfig QmChatHistoryScrollConfig()
{
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	// 偏移单位是「行」，所以一档的尺度写成行数；步长系数与列表同一份配置
	// （qm_ui_scroll_step 默认 10 时为 1.0），不再用固定 120 像素一次跳到底。
	// 防零回退与 QmScroll 同口径：测试环境的 g_Config 是零初始化。
	const float Step = g_Config.m_QmUiScrollStep > 0.0f ? (float)g_Config.m_QmUiScrollStep : 10.0f;
	const float StepScale = Step / 10.0f;
	Config.m_WheelScale = ui_token::chat::WHEEL_LINES_PER_STEP * StepScale;
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
	// 一档 = Config.m_WheelScale 行，与列表「一次两行」的手感对齐。
	State.AddWheelImpulse(-(float)Steps * std::max(1.0f, Config.m_WheelScale), Metrics, Config);
}

// 拖动直接跟手：把目标行写进同一份滚动状态（不经过弹簧）。松手后交给
// Advance——内容变矮或滑块被拖到界外时，越界回弹会把偏移收回合法范围，
// 与列表拖动滚动条是同一条语义。
inline void QmChatHistoryDragToLine(CQmScrollState &State, int Line, const SQmScrollMetrics &Metrics, const SQmScrollConfig &Config)
{
	State.SetOffset((float)Line, Metrics, Config, true);
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
	// 滑块落在轨道内（几何已由公共容器算好，这里只做防御性夹取）。
	Handle.h = std::min(std::max(0.0f, Handle.h), Rail.h);
	if(Handle.h <= 0.0f)
		return;
	Handle.y = std::clamp(Handle.y, Rail.y, Rail.y + Rail.h - Handle.h);
	const ColorRGBA Accent = pUi->QmControlTheme().m_Accent;
	const bool PublicFeedbackActive = pUi->VisualHotItem(QmChatScrollbarId()) || pUi->CheckActiveItem(QmChatScrollbarId());
	const CUIRect FeedbackHandle = QmChatScrollbarFeedbackRect(Handle, Interactive && Hovered, Interactive && Dragging, PublicFeedbackActive);
	ui_widget::DrawScrollbarHandle(ui_widget::ControlContext(pUi), QmChatScrollbarId(), FeedbackHandle, Interactive, nullptr, &Accent);
}

#endif
