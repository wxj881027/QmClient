#ifndef GAME_CLIENT_QMUI_CARDS_QMCONSOLESETTINGSLAYOUT_H
#define GAME_CLIENT_QMUI_CARDS_QMCONSOLESETTINGSLAYOUT_H

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/components/qmclient/console_appearance.h>

namespace QmConsoleSettingsLayout
{
	inline float PreviewHeight(int FontSize)
	{
		return QmConsoleAppearance::FontSize(FontSize) * 2.0f + 12.0f;
	}

	inline float ContentHeight(const SSettingsContentMetrics &Metrics, bool Custom, int FontSize)
	{
		// 两个数值、一个开关、方案标题、三个方案按钮及重置按钮。
		return 8.0f * Metrics.m_RowStep + PreviewHeight(FontSize) + Metrics.m_LineSpacing +
		       (Custom ? 9.0f * Metrics.m_ButtonHeight : 0.0f);
	}

	inline CUIRect PanelRect(const CUIRect &Viewport, bool Custom, int FontSize)
	{
		const float Width = std::min(440.0f, std::max(0.0f, Viewport.w) * 0.90f);
		const float Inset = CUi::PopupMenuContentInset();
		const auto Header = ui_widget::ResolveSecondaryPanelMetrics(Width);
		const auto Metrics = ResolveSettingsContentMetrics(std::max(0.0f, Width - Inset - 2.0f * Header.m_Margin));
		const float DesiredHeight = Inset + Header.ContentHeight(0, 0, 0) + ContentHeight(Metrics, Custom, FontSize);
		const float Height = std::min(DesiredHeight, std::max(0.0f, Viewport.h) * 0.90f);
		return {Viewport.x + (Viewport.w - Width) * 0.5f, Viewport.y + (Viewport.h - Height) * 0.5f, Width, Height};
	}
}

#endif
