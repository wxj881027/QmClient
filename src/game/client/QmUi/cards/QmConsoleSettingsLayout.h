#ifndef GAME_CLIENT_QMUI_CARDS_QMCONSOLESETTINGSLAYOUT_H
#define GAME_CLIENT_QMUI_CARDS_QMCONSOLESETTINGSLAYOUT_H

#include "QmConsoleSchemeList.h"

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/qmclient/console_appearance.h>

#include <algorithm>

namespace QmConsoleSettingsLayout
{
	inline float PreviewHeight(int FontSize)
	{
		return QmConsoleAppearance::FontSize(FontSize) * 2.0f + 12.0f;
	}

	// 列表限高：可见行数有上限，方案再多也不会把预览和重置按钮挤出面板。
	static constexpr int k_MaxVisibleSchemeRows = 8;

	// 一行方案比普通列表行略高：色板是两行 ANSI 方格，压到普通行高会糊在一起。
	inline float SchemeRowHeight(const SSettingsContentMetrics &Metrics)
	{
		return Metrics.m_ListRowHeight + Metrics.m_LineSpacing;
	}

	// 只量可滚动的内置配色；历史三项是固定表头，高度由调用方按行单独占位。
	inline float SchemeListHeight(const SSettingsContentMetrics &Metrics)
	{
		// 历史三项也在这个列表里滚动，可选行数就是整份方案表。
		const int Rows = std::min(qm_console_scheme_list::Count(), k_MaxVisibleSchemeRows);
		return static_cast<float>(std::max(Rows, 1)) * SchemeRowHeight(Metrics);
	}

	inline float ContentHeight(const SSettingsContentMetrics &Metrics, bool Custom, int FontSize)
	{
		// 必须和实际占用逐项对齐：TakeRow 每行是 RowStep（行高+行距），
		// 列表与预览各自额外带一次行距，自定义的九行取色不带行距。
		// 多算会让面板永远判定"内容溢出"，从而在没有滚动必要时也画出滚动条。
		return 4.0f * Metrics.m_RowStep + SchemeListHeight(Metrics) + Metrics.m_LineSpacing +
		       PreviewHeight(FontSize) + Metrics.m_LineSpacing +
		       (Custom ? 9.0f * ResolveSettingsColorRowLayout({}, Metrics, false).m_ConsumedHeight : 0.0f);
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
