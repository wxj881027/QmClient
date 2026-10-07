#ifndef GAME_CLIENT_COMPONENTS_TOOLTIPS_H
#define GAME_CLIENT_COMPONENTS_TOOLTIPS_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

// 气泡内可绘制的完整文本行数；最终绘制仍裁剪于可见内容区。
inline int QmTooltipVisibleLines(float Height, float FontSize)
{
	return std::max(1, static_cast<int>(std::floor(std::max(0.0f, Height) / std::max(1.0f, FontSize))));
}

struct CTooltip
{
	const void *m_pId;
	CUIRect m_Rect;
	std::string m_Text;
	float m_WidthHint;
	bool m_OnScreen; // used to know if the tooltip should be rendered.
	float m_FadeTime = 0.75f;
	float m_FontSize = 14.0f;
	bool m_SmallInstant = false;
	bool m_HoverByRect = false;
	bool m_Immediate = false;
};

// 注册与最终绘制共享同一悬浮资格，弹层屏蔽和裁剪变化立即使矩形提示失效。
template<typename TUi>
inline bool QmTooltipHovered(const CTooltip &Tooltip, TUi &Ui)
{
	return Tooltip.m_HoverByRect ? Ui.MouseHovered(&Tooltip.m_Rect) : Ui.HotItem() == Tooltip.m_pId && Tooltip.m_Rect.Inside(Ui.MousePos());
}

/**
 * A component that manages and renders UI tooltips.
 *
 * Should be among the last components to render.
 */
class CTooltips : public CComponent
{
	std::unordered_map<uintptr_t, CTooltip> m_Tooltips;
	std::optional<std::reference_wrapper<CTooltip>> m_ActiveTooltip;
	std::optional<std::reference_wrapper<CTooltip>> m_PreviousTooltip;
	int64_t m_HoverTime;

	/**
	 * @param Tooltip A reference to the tooltip that should be active.
	 */
	void SetActiveTooltip(CTooltip &Tooltip);
	void DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize, bool SmallInstant, bool HoverByRect, bool Immediate = false);

	inline void ClearActiveTooltip();

public:
	CTooltips();
	int Sizeof() const override { return sizeof(*this); }

	/**
	 * Adds the tooltip to a cache and renders it when active.
	 *
	 * On the first call to this function, the data passed is cached, afterwards the calls are used to detect if the tooltip should be activated.
	 * If multiple tooltips cover the same rect or the rects intersect, then the tooltip that is added later has priority.
	 *
	 * @param pId The ID of the tooltip. Usually a reference to some g_Config value.
	 * @param pNearRect Place the tooltip near this rect.
	 * @param pText The text to display in the tooltip.
	 * @param WidthHint The maximum width of the tooltip, or -1.0f for unlimited.
	 */
	void DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint = -1.0f);
	// 说明标签不抢占控件的 HotItem，通过可见悬浮区域触发提示。
	void DoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint = -1.0f);

	// 卡片说明等长文本使用气泡，不受设置页的小字提示模式影响。
	void DoInfoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize);

	// 无背景的小字提示，悬停时立即显示在控件上方。
	void DoSmallToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float FontSize, float WidthHint = -1.0f);

	void OnReset() override;
	void OnRender() override;

	// TClient
	void SetFadeTime(const void *pId, float Time);
};

#endif
