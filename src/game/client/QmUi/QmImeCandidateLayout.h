#ifndef GAME_CLIENT_QMUI_QMIMECANDIDATELAYOUT_H
#define GAME_CLIENT_QMUI_QMIMECANDIDATELAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>

namespace qm_ime_overlay
{

	inline constexpr int MAX_CANDIDATES = 16;

	struct SCandidateMeasure
	{
		float m_FixedWidth = 0.0f;
		float m_TextWidth = 0.0f;
	};

	struct SCandidateLayoutConfig
	{
		float m_Gap = 0.0f;
		float m_TrailingWidth = 0.0f;
		float m_PaddingX = 0.0f;
		float m_MinPanelWidth = 0.0f;
		float m_MaxPanelWidth = 0.0f;
		float m_MinTextWidth = 0.0f;
	};

	struct SCandidateCellLayout
	{
		float m_X = 0.0f;
		float m_Width = 0.0f;
		float m_TextWidth = 0.0f;
	};

	struct SCandidateRowLayout
	{
		int m_Count = 0;
		std::array<SCandidateCellLayout, MAX_CANDIDATES> m_aCells{};
		float m_ContentWidth = 0.0f;
		float m_PanelWidth = 0.0f;
	};

	inline SCandidateRowLayout BuildCandidateRowLayout(const std::array<SCandidateMeasure, MAX_CANDIDATES> &aMeasures, int Count, const SCandidateLayoutConfig &Config)
	{
		SCandidateRowLayout Layout;
		Layout.m_Count = std::clamp(Count, 0, MAX_CANDIDATES);
		if(Layout.m_Count == 0)
			return Layout;

		const float MaxPanelWidth = std::max(1.0f, Config.m_MaxPanelWidth);
		float FixedWidth = 2.0f * Config.m_PaddingX + Config.m_TrailingWidth + (Layout.m_Count - 1) * Config.m_Gap;
		float MinTextBudget = 0.0f;
		std::array<float, MAX_CANDIDATES> aSortedTextWidths{};
		for(int i = 0; i < Layout.m_Count; ++i)
		{
			FixedWidth += aMeasures[i].m_FixedWidth;
			MinTextBudget += std::min(aMeasures[i].m_TextWidth, Config.m_MinTextWidth);
			aSortedTextWidths[i] = aMeasures[i].m_TextWidth;
		}
		std::sort(aSortedTextWidths.begin(), aSortedTextWidths.begin() + Layout.m_Count);

		// 短词保留原宽度，剩余空间由长词共享；极窄屏保留可辨认的词头后再整体缩放。
		float RemainingTextWidth = std::max(MaxPanelWidth - FixedWidth, MinTextBudget);
		float TextWidthCap = 0.0f;
		for(int i = 0; i < Layout.m_Count; ++i)
		{
			TextWidthCap = RemainingTextWidth / (Layout.m_Count - i);
			if(aSortedTextWidths[i] > TextWidthCap)
				break;
			RemainingTextWidth -= aSortedTextWidths[i];
		}

		float CursorX = 0.0f;
		for(int i = 0; i < Layout.m_Count; ++i)
		{
			if(i > 0)
				CursorX += Config.m_Gap;
			SCandidateCellLayout &Cell = Layout.m_aCells[i];
			Cell.m_X = CursorX;
			Cell.m_TextWidth = std::min(aMeasures[i].m_TextWidth, TextWidthCap);
			Cell.m_Width = aMeasures[i].m_FixedWidth + Cell.m_TextWidth;
			CursorX += Cell.m_Width;
		}
		Layout.m_ContentWidth = CursorX + Config.m_TrailingWidth;
		Layout.m_PanelWidth = std::clamp(Layout.m_ContentWidth + 2.0f * Config.m_PaddingX,
			std::clamp(Config.m_MinPanelWidth, 0.0f, MaxPanelWidth), MaxPanelWidth);
		Layout.m_ContentWidth = std::max(Layout.m_ContentWidth, Layout.m_PanelWidth - 2.0f * Config.m_PaddingX);
		return Layout;
	}

	inline CUIRect FitCandidatePanel(const SCandidateRowLayout &Layout, CUIRect Panel, float PanelHeight, const CUIRect &Bounds)
	{
		// 扩张时立即容纳当前页，收缩仍可过渡；文字不随宽度弹簧改变字号。
		Panel.w = std::min(std::max(Panel.w, Layout.m_PanelWidth), Bounds.w);
		Panel.h = std::min(std::max(Panel.h, PanelHeight), Bounds.h);
		Panel.x = std::clamp(Panel.x, Bounds.x, Bounds.x + Bounds.w - Panel.w);
		Panel.y = std::clamp(Panel.y, Bounds.y, Bounds.y + Bounds.h - Panel.h);
		return Panel;
	}

	struct SCandidateRowPresentation
	{
		vec2 m_Origin = vec2(0.0f, 0.0f);
		float m_Scale = 0.0f;

		CUIRect Transform(const CUIRect &Rect) const
		{
			return {m_Origin.x + Rect.x * m_Scale, m_Origin.y + Rect.y * m_Scale, Rect.w * m_Scale, Rect.h * m_Scale};
		}
	};

	inline SCandidateRowPresentation BuildCandidateRowPresentation(const SCandidateRowLayout &Layout, const CUIRect &Panel, float RowHeight, float PaddingX, float PaddingY, float ContentScale)
	{
		SCandidateRowPresentation Presentation;
		if(Layout.m_ContentWidth <= 0.0f || RowHeight <= 0.0f)
			return Presentation;

		const float AvailableWidth = std::max(0.0f, Panel.w - 2.0f * PaddingX);
		const float AvailableHeight = std::max(0.0f, Panel.h - 2.0f * PaddingY);
		Presentation.m_Scale = std::clamp(ContentScale, 0.0f, 1.0f) * std::min({1.0f, AvailableWidth / Layout.m_ContentWidth, AvailableHeight / RowHeight});
		Presentation.m_Origin = vec2(Panel.x + (Panel.w - Layout.m_ContentWidth * Presentation.m_Scale) * 0.5f,
			Panel.y + (Panel.h - RowHeight * Presentation.m_Scale) * 0.5f);
		return Presentation;
	}

} // namespace qm_ime_overlay

#endif
