#ifndef GAME_CLIENT_COMPONENTS_TOOLTIPS_H
#define GAME_CLIENT_COMPONENTS_TOOLTIPS_H

#include <engine/textrender.h>

#include <game/client/QmUi/UiConfigHintText.h>
#include <game/client/component.h>
#include <game/client/components/qm_tooltip_text_layout.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

struct SConfigVariable;

inline float QmTooltipScale(float ElapsedSeconds, bool AnimationEnabled)
{
	if(!AnimationEnabled || ElapsedSeconds >= 0.18f)
		return 1.0f;
	const float T = std::clamp(ElapsedSeconds / 0.18f, 0.0f, 1.0f) - 1.0f;
	const float Ease = 1.0f + 2.70158f * T * T * T + 1.70158f * T * T;
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
};

inline float QmTooltipDelay(const CTooltip &Tooltip)
{
	return Tooltip.m_SmallInstant || Tooltip.m_Immediate ? 0.0f : Tooltip.m_FadeTime;
}

// 首次悬浮等待后再显示；已显示的气泡换目标时直接沿用可见状态。
class CQmTooltipHoverState
{
	const CTooltip *m_pTarget = nullptr;
	double m_VisibleAt = 0.0;
	bool m_Visible = false;

public:
	static constexpr float FADE_IN_SECONDS = 0.25f;

	float Update(const CTooltip &Tooltip, double Now)
	{
		if(m_pTarget != &Tooltip)
		{
			m_pTarget = &Tooltip;
			m_VisibleAt = Now + (m_Visible ? -FADE_IN_SECONDS : QmTooltipDelay(Tooltip));
		}
		const float VisibleSeconds = static_cast<float>(Now - m_VisibleAt);
		m_Visible = VisibleSeconds >= 0.0f;
		return VisibleSeconds;
	}

	void Clear()
	{
		m_pTarget = nullptr;
		m_VisibleAt = 0.0;
		m_Visible = false;
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
		m_TextRender.SetRenderFlags(TEXT_RENDER_FLAG_ONE_TIME_USE | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
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
	return Tooltip.m_OnScreen && QmTooltipRegistered(Tooltip, Frame) && QmTooltipHovered(Tooltip, Ui);
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
	uint64_t m_Frame = 1;
	bool m_ConfigHelpInitialized = false;
	std::unordered_map<const void *, const SConfigVariable *> m_ConfigHelp;

	/**
	 * @param Tooltip A reference to the tooltip that should be active.
	 */
	void SetActiveTooltip(CTooltip &Tooltip);
	void DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize, bool SmallInstant, bool HoverByRect, bool Immediate = false, bool Fallback = false, const CUIRect *pAnchor = nullptr);

	inline void ClearActiveTooltip();

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

	void OnReset() override;
	void OnRender() override;

	// TClient
	void SetFadeTime(const void *pId, float Time);
};

#endif
