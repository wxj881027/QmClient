#ifndef GAME_CLIENT_COMPONENTS_TOOLTIPS_H
#define GAME_CLIENT_COMPONENTS_TOOLTIPS_H

#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/QmUi/QmCardLabelHints.h>
#include <game/client/QmUi/UiConfigHintText.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/component.h>
#include <game/client/components/qm_tooltip_text_layout.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

struct SConfigVariable;

inline float QmTooltipScale(float ElapsedSeconds, bool AnimationEnabled)
{
	if(!AnimationEnabled || ElapsedSeconds >= 0.18f)
		return 1.0f;
	// EASE_OUT_QUART 单调收口（0.88 → 1.0，无过冲回弹）：气泡常锚在控件/弹层
	// 边缘，ease-out-back 的过冲会被读作「弹层展开在弹」（用户明确要求下拉
	// 弹出优雅无弹跳），故入场只保留缩放渐入、去掉回弹段。
	const float T = std::clamp(ElapsedSeconds / 0.18f, 0.0f, 1.0f);
	const float Inv = 1.0f - T;
	const float Ease = 1.0f - Inv * Inv * Inv * Inv;
	return 0.88f + 0.12f * Ease;
}

// 气泡跟随目标矩形；指针在目标内部移动不会改变气泡位置。
inline CUIRect QmTooltipRect(const CUIRect &Anchor, const CUIRect &Screen, vec2 Size, float Margin)
{
	Margin = std::max(0.0f, std::min(Margin, std::min(Screen.w, Screen.h) * 0.5f));
	CUIRect Rect{Anchor.x + (Anchor.w - Size.x) * 0.5f, Anchor.y - Size.y - Margin,
		std::clamp(Size.x, 0.0f, std::max(0.0f, Screen.w - 2.0f * Margin)),
		std::clamp(Size.y, 0.0f, std::max(0.0f, Screen.h - 2.0f * Margin))};
	Rect.x = Anchor.x + (Anchor.w - Rect.w) * 0.5f;
	Rect.y = Anchor.y - Rect.h - Margin;
	if(Rect.y < Screen.y + Margin)
	{
		Rect.y = Anchor.y + Anchor.h + Margin;
		if(Rect.y + Rect.h > Screen.y + Screen.h - Margin)
		{
			Rect.x = Anchor.x + Anchor.w + Margin;
			if(Rect.x + Rect.w > Screen.x + Screen.w - Margin)
				Rect.x = Anchor.x - Rect.w - Margin;
			Rect.y = Anchor.y + (Anchor.h - Rect.h) * 0.5f;
		}
	}
	Rect.x = std::clamp(Rect.x, Screen.x + Margin, Screen.x + Screen.w - Rect.w - Margin);
	Rect.y = std::clamp(Rect.y, Screen.y + Margin, Screen.y + Screen.h - Rect.h - Margin);
	return Rect;
}

// 先确定避让后的完整矩形，再围绕固定中心缩放；屏幕边缘限制回弹幅度。
inline CUIRect QmTooltipAnimatedRect(const CUIRect &Rect, const CUIRect &Screen, float Scale)
{
	const vec2 Center = Rect.Center();
	const float MaxScaleX = Rect.w > 0.0f ? 2.0f * std::max(0.0f, std::min(Center.x - Screen.x, Screen.x + Screen.w - Center.x)) / Rect.w : 1.0f;
	const float MaxScaleY = Rect.h > 0.0f ? 2.0f * std::max(0.0f, std::min(Center.y - Screen.y, Screen.y + Screen.h - Center.y)) / Rect.h : 1.0f;
	Scale = std::clamp(Scale, 0.0f, std::min(MaxScaleX, MaxScaleY));
	return {Center.x - Rect.w * Scale * 0.5f, Center.y - Rect.h * Scale * 0.5f, Rect.w * Scale, Rect.h * Scale};
}

struct CTooltip
{
	const void *m_pId = nullptr;
	CUIRect m_Rect{};
	std::string m_Text;
	float m_WidthHint = -1.0f;
	bool m_OnScreen = false; // 登记时的悬浮资格，保留内容裁剪与弹层屏蔽结果。
	float m_FadeTime = 0.75f;
	float m_FontSize = 14.0f;
	bool m_SmallInstant = false;
	bool m_HoverByRect = false;
	bool m_Immediate = false;
	bool m_Fallback = false;
	uint64_t m_RegisteredFrame = 0;
	CUIRect m_Anchor{};
	bool m_HasTextAnchor = false;
	uint64_t m_SourceFrame = 0;
	bool m_SourceAvailable = true;
};

// 只记录实际提交的来源证据；没有登记不能证明来源被隐藏或销毁。
inline void QmTooltipRecordSource(CTooltip &Tooltip, uint64_t Frame, const CUIRect &Rect, const CUIRect *pClip, vec2 Pointer, bool HasText = true)
{
	Tooltip.m_SourceFrame = Frame;
	Tooltip.m_SourceAvailable = HasText && Rect.w > 0.0f && Rect.h > 0.0f;
	if(pClip != nullptr)
	{
		const bool Visible = pClip->w > 0.0f && pClip->h > 0.0f &&
				     Rect.x < pClip->x + pClip->w && Rect.x + Rect.w > pClip->x &&
				     Rect.y < pClip->y + pClip->h && Rect.y + Rect.h > pClip->y;
		// 指针仍在来源矩形内、却落在被裁掉的部分，不属于控件之间的间隙。
		Tooltip.m_SourceAvailable &= Visible && (!Rect.Inside(Pointer) || pClip->Inside(Pointer));
	}
}

inline bool QmTooltipSourceInvalidated(const CTooltip &Tooltip, uint64_t Frame)
{
	return Tooltip.m_SourceFrame == Frame && !Tooltip.m_SourceAvailable;
}

inline float QmTooltipDelay(const CTooltip &Tooltip)
{
	return Tooltip.m_SmallInstant || Tooltip.m_Immediate ? 0.0f : Tooltip.m_FadeTime;
}

// 首次悬浮等待后再显示；已显示的气泡允许短暂跨过控件间隙。
class CQmTooltipHoverState
{
	const void *m_pTarget = nullptr;
	double m_VisibleAt = 0.0;
	double m_LastHoveredAt = 0.0;
	bool m_Visible = false;

public:
	static constexpr float FADE_IN_SECONDS = 0.25f;
	static constexpr double SWITCH_GRACE_SECONDS = 0.25;

	float Update(const CTooltip &Tooltip, double Now)
	{
		const void *pTarget = Tooltip.m_pId != nullptr ? Tooltip.m_pId : &Tooltip;
		if(m_pTarget != pTarget)
		{
			if(m_Visible && Now - m_LastHoveredAt > SWITCH_GRACE_SECONDS)
				Clear();
			m_pTarget = pTarget;
			m_VisibleAt = Now + (m_Visible ? -FADE_IN_SECONDS : QmTooltipDelay(Tooltip));
		}
		m_LastHoveredAt = Now;
		const float Seconds = VisibleSeconds(Now);
		m_Visible = Seconds >= 0.0f;
		return Seconds;
	}

	float VisibleSeconds(double Now) const { return static_cast<float>(Now - m_VisibleAt); }

	float RetainAlpha(double Now, bool Animate) const
	{
		if(!m_Visible)
			return 0.0f;
		const double UnhoveredSeconds = Now - m_LastHoveredAt;
		if(!Animate || UnhoveredSeconds <= 0.0)
			return 1.0f;
		// 前 50ms 保持满不透明度以支持跨控件无缝切换；随后平滑淡出，消除 250ms 到期瞬间的硬切闪退。
		constexpr double GRACE_HOLD_SECONDS = 0.05;
		if(UnhoveredSeconds <= GRACE_HOLD_SECONDS)
			return 1.0f;
		const float FadeProgress = std::clamp(static_cast<float>((UnhoveredSeconds - GRACE_HOLD_SECONDS) / (SWITCH_GRACE_SECONDS - GRACE_HOLD_SECONDS)), 0.0f, 1.0f);
		return 1.0f - FadeProgress * FadeProgress;
	}

	bool Retain(double Now, const CTooltip *pSource = nullptr, uint64_t Frame = 0)
	{
		const bool SourceInvalidated = pSource != nullptr && QmTooltipSourceInvalidated(*pSource, Frame);
		if(!SourceInvalidated && m_Visible && Now - m_LastHoveredAt <= SWITCH_GRACE_SECONDS)
			return true;
		Clear();
		return false;
	}

	void Clear()
	{
		m_pTarget = nullptr;
		m_VisibleAt = 0.0;
		m_LastHoveredAt = 0.0;
		m_Visible = false;
	}
};

// 新目标从当前可见矩形接着移动，快速连续切换不会回跳到上一个起点。
// 2026-10-14 迁移到动画引擎：位置/尺寸四条 tween 轨道由 CQmAnimationBackend
// 驱动（打断续接 MERGE_TARGET、时长/曲线收口 ui_token::motion::TOOLTIP_MOVE、
// 动效等级自动响应 ApplyMotionLevel），不再手写插值公式。
class CQmTooltipMotionState
{
	CQmAnimationBackend m_Anim;
	CUIRect m_Target{};
	double m_LastUpdateAt = 0.0;
	bool m_Initialized = false;

	static uint64_t MotionNodeKey()
	{
		return BuildUiAnimNodeKey(static_cast<uint64_t>(str_quickhash("tooltip_motion")), 0);
	}

	void SnapTo(const CUIRect &Target)
	{
		m_Anim.Reset();
		m_Anim.SetValue(MotionNodeKey(), EUiAnimProperty::POS_X, Target.x);
		m_Anim.SetValue(MotionNodeKey(), EUiAnimProperty::POS_Y, Target.y);
		m_Anim.SetValue(MotionNodeKey(), EUiAnimProperty::WIDTH, Target.w);
		m_Anim.SetValue(MotionNodeKey(), EUiAnimProperty::HEIGHT, Target.h);
	}

	CUIRect Sample() const
	{
		const uint64_t NodeKey = MotionNodeKey();
		CUIRect Out = m_Target;
		Out.x = m_Anim.GetValue(NodeKey, EUiAnimProperty::POS_X, m_Target.x);
		Out.y = m_Anim.GetValue(NodeKey, EUiAnimProperty::POS_Y, m_Target.y);
		Out.w = m_Anim.GetValue(NodeKey, EUiAnimProperty::WIDTH, m_Target.w);
		Out.h = m_Anim.GetValue(NodeKey, EUiAnimProperty::HEIGHT, m_Target.h);
		return Out;
	}

public:
	// 兼容引用保留；实际时长/曲线唯一来源是 ui_token::motion::TOOLTIP_MOVE。
	static constexpr double MOVE_SECONDS = 0.16;

	CUIRect Update(const CUIRect &Target, double Now, bool Enabled)
	{
		if(!m_Initialized || !Enabled)
		{
			m_Target = Target;
			m_Initialized = true;
			m_LastUpdateAt = Now;
			SnapTo(Target);
			return Target;
		}
		// 目标变化的那一帧从当前位置启动新轨道（dt=0 不先积分），保证同帧
		// 返回值连续；后续帧按真实时间推进引擎 tween。
		const bool TargetChanged = m_Target.x != Target.x || m_Target.y != Target.y || m_Target.w != Target.w || m_Target.h != Target.h;
		const float Dt = TargetChanged ? 0.0f : static_cast<float>(std::max(0.0, Now - m_LastUpdateAt));
		m_LastUpdateAt = Now;
		m_Anim.Advance(Dt);
		m_Target = Target;
		const uint64_t NodeKey = MotionNodeKey();
		(void)ResolveUiAnimValue(m_Anim, NodeKey, EUiAnimProperty::POS_X, Target.x, ui_token::motion::TOOLTIP_MOVE.m_DurationSec, ui_token::motion::TOOLTIP_MOVE.m_Easing);
		(void)ResolveUiAnimValue(m_Anim, NodeKey, EUiAnimProperty::POS_Y, Target.y, ui_token::motion::TOOLTIP_MOVE.m_DurationSec, ui_token::motion::TOOLTIP_MOVE.m_Easing);
		(void)ResolveUiAnimValue(m_Anim, NodeKey, EUiAnimProperty::WIDTH, Target.w, ui_token::motion::TOOLTIP_MOVE.m_DurationSec, ui_token::motion::TOOLTIP_MOVE.m_Easing);
		(void)ResolveUiAnimValue(m_Anim, NodeKey, EUiAnimProperty::HEIGHT, Target.h, ui_token::motion::TOOLTIP_MOVE.m_DurationSec, ui_token::motion::TOOLTIP_MOVE.m_Easing);
		return Sample();
	}

	void Clear()
	{
		m_Initialized = false;
		m_Anim.Reset();
	}
};

// 同一气泡持有一个文本容器；位移只改变绘制偏移，内容或排版改变时原地更新。
class CQmTooltipTextCache
{
	STextContainerIndex m_Index;
	std::string m_Text;
	float m_FontSize = 0.0f;
	float m_WrapWidth = 0.0f;
	int m_MaxLines = 0;
	// 保存真实生成的字形范围与断行结果；缓存命中时不重复排版。
	CTextCursor m_Cursor;

public:
	const STextContainerIndex &Index() const { return m_Index; }
	const CTextCursor &LayoutCursor() const { return m_Cursor; }

	template<typename TTextRender>
	void Update(TTextRender &TextRender, CTextCursor Cursor, const std::string &Text)
	{
		if(!m_Index.Valid() || m_Text != Text || m_FontSize != Cursor.m_FontSize || m_WrapWidth != Cursor.m_LineWidth || m_MaxLines != Cursor.m_MaxLines)
		{
			Cursor.SetPosition(vec2(0, 0));
			TextRender.RecreateTextContainerSoft(m_Index, &Cursor, Text.c_str());
			m_Text = Text;
			m_FontSize = Cursor.m_FontSize;
			m_WrapWidth = Cursor.m_LineWidth;
			m_MaxLines = Cursor.m_MaxLines;
			m_Cursor = std::move(Cursor);
		}
	}

	template<typename TTextRender>
	void Clear(TTextRender &TextRender)
	{
		TextRender.DeleteTextContainer(m_Index);
		m_Index.Reset();
		m_Text.clear();
		m_Cursor = {};
	}
};

// 字形顶点使用不透明白色，最终颜色由气泡渲染传入；不继承前一个控件的透明度或图标字体。
template<typename TTextRender>
class CQmTooltipTextScope
{
	TTextRender &m_TextRender;
	ColorRGBA m_PreviousColor;
	EFontPreset m_PreviousPreset;
	unsigned m_PreviousFlags;

public:
	explicit CQmTooltipTextScope(TTextRender &TextRender) :
		m_TextRender(TextRender),
		m_PreviousColor(TextRender.GetTextColor()),
		m_PreviousPreset(TextRender.GetFontPreset()),
		m_PreviousFlags(TextRender.GetRenderFlags())
	{
		m_TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
		m_TextRender.TextColor(ColorRGBA(1, 1, 1, 1));
		m_TextRender.SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	}

	~CQmTooltipTextScope()
	{
		m_TextRender.SetRenderFlags(m_PreviousFlags);
		m_TextRender.SetFontPreset(m_PreviousPreset);
		m_TextRender.TextColor(m_PreviousColor);
	}

	CQmTooltipTextScope(const CQmTooltipTextScope &) = delete;
	CQmTooltipTextScope &operator=(const CQmTooltipTextScope &) = delete;
};

inline bool QmTooltipAnimate(const CTooltip &Tooltip, bool AnimationEnabled)
{
	return !Tooltip.m_SmallInstant && AnimationEnabled;
}

inline bool QmTooltipRegistered(const CTooltip &Tooltip, uint64_t Frame)
{
	return Tooltip.m_RegisteredFrame == Frame;
}

// 自动配置描述只作兜底，不覆盖本帧已提供的专属说明。
inline bool QmTooltipMayReplace(const CTooltip &Candidate, const CTooltip &Current, uint64_t Frame, bool CurrentHovered)
{
	return !CurrentHovered || !QmTooltipRegistered(Current, Frame) || !Candidate.m_Fallback || Current.m_Fallback;
}

// 标题锚点可补全自动登记的配置提示；内部控件兜底不缩小整行命中区，也不覆盖专属说明。
inline bool QmTooltipMayUpdate(const CTooltip &Current, uint64_t Frame, bool Fallback, bool CurrentHovered, bool HasTextAnchor = false)
{
	return !Fallback || !CurrentHovered || !QmTooltipRegistered(Current, Frame) || (Current.m_Fallback && HasTextAnchor && !Current.m_HasTextAnchor);
}

// 注册与最终绘制共享同一悬浮资格，弹层屏蔽和裁剪变化立即使矩形提示失效。
template<typename TUi>
inline bool QmTooltipHovered(const CTooltip &Tooltip, TUi &Ui)
{
	return Ui.MouseHovered(&Tooltip.m_Rect) && (Tooltip.m_HoverByRect || Ui.HotItem() == Tooltip.m_pId);
}

// 最终绘制同时检查登记时和当前的悬浮资格，离开裁剪区后不能被恢复的全屏命中激活。
template<typename TUi>
inline bool QmTooltipActive(const CTooltip &Tooltip, uint64_t Frame, TUi &Ui)
{
	return Tooltip.m_OnScreen && QmTooltipRegistered(Tooltip, Frame) && !QmTooltipSourceInvalidated(Tooltip, Frame) && QmTooltipHovered(Tooltip, Ui);
}

/**
 * A component that manages and renders UI tooltips.
 *
 * Should be among the last components to render.
 */
class CTooltips : public CComponent
{
	std::unordered_map<uintptr_t, CTooltip> m_Tooltips;
	std::unordered_map<uintptr_t, CUiConfigHintText> m_ConfigHints;
	std::optional<std::reference_wrapper<CTooltip>> m_ActiveTooltip;
	CQmTooltipHoverState m_HoverState;
	CQmTooltipMotionState m_MotionState;
	CQmTooltipTextCache m_TextCache;
	CTooltip m_DisplayTooltip;
	CTooltip m_CardLabelHint;
	CQmCardLabelHintCache m_CardLabelTexts;
	std::optional<std::array<int, 9>> m_Context;
	uint64_t m_Frame = 1;
	bool m_ConfigHelpInitialized = false;
	std::unordered_map<const void *, const SConfigVariable *> m_ConfigHelp;

	/**
	 * @param Tooltip A reference to the tooltip that should be active.
	 */
	void SetActiveTooltip(CTooltip &Tooltip);
	void DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize, bool SmallInstant, bool HoverByRect, bool Immediate = false, bool Fallback = false, const CUIRect *pAnchor = nullptr);

	void RecordSource(const void *pId, const CUIRect &Rect);
	void ClearActiveTooltip();
	void ResetPresentation();

public:
	CTooltips();
	int Sizeof() const override { return sizeof(*this); }

	/**
	 * Adds the tooltip to a cache and renders it when active.
	 *
	 * On the first call to this function, the data passed is cached, afterwards the calls are used to detect if the tooltip should be activated.
	 * 重叠时后登记的专属说明优先，自动配置描述只作兜底。
	 *
	 * @param pId The ID of the tooltip. Usually a reference to some g_Config value.
	 * @param pNearRect Place the tooltip near this rect.
	 * @param pText The text to display in the tooltip.
	 * @param WidthHint 提示最大宽度，-1.0f 表示默认阅读宽度。
	 */
	void DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint = -1.0f);
	// 说明标签不抢占控件的 HotItem，通过可见悬浮区域触发提示。
	void DoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint = -1.0f);
	// 控件显式提供配置绑定；后续普通提示会追加同一命令，保留原说明。
	void DoConfigToolTip(const void *pId, const CUIRect *pNearRect, const void *pValue, const void *pSecondValue = nullptr);

	// 设置页内统一使用即时的小字气泡，独立说明保留自动换行。
	void DoInfoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize);

	// 设置内为紧凑深色小字提示，页面外使用普通气泡；均立即显示在控件附近。
	void DoSmallToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float FontSize, float WidthHint = -1.0f);

	// 从已有配置描述取得选项帮助；命中整行时仍可锚定到标题文本。
	void DoSettingsToolTipForConfig(const void *pId, const CUIRect *pRect, const void *pConfigValue, const CUIRect *pAnchor = nullptr, const void *pSecondConfigValue = nullptr);

	const char *PrepareCardLabel(const CUIRect *pRect, const char *pText, bool Render);

	void OnReset() override;
	void OnRender() override;
	void OnShutdown() override { ClearActiveTooltip(); }
	void OnWindowResize() override { ClearActiveTooltip(); }

	// TClient
	void SetFadeTime(const void *pId, float Time);
};

#endif
