#ifndef GAME_CLIENT_QMUI_AUDIOPACKEDITORLAYOUT_H
#define GAME_CLIENT_QMUI_AUDIOPACKEDITORLAYOUT_H

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_audio_pack
{
	constexpr float STACKED_CATALOG_SHARE = 0.30f;

	inline bool StacksEditorPanels(float Width, float Scale)
	{
		return Width < 520.0f * std::max(0.1f, Scale);
	}

	struct SEditorLayout
	{
		CUIRect m_Catalog;
		CUIRect m_Workspace;
		bool m_Stacked;
	};

	inline SEditorLayout ResolveEditorLayout(CUIRect View, float UiScale)
	{
		SEditorLayout Layout{};
		View.w = std::max(0.0f, View.w);
		View.h = std::max(0.0f, View.h);
		const float Scale = std::max(0.1f, UiScale);
		Layout.m_Stacked = StacksEditorPanels(View.w, Scale);
		if(Layout.m_Stacked)
		{
			const float Gap = std::min(ui_token::panel::GAP * Scale, View.h);
			View.HSplitTop((View.h - Gap) * STACKED_CATALOG_SHARE, &Layout.m_Catalog, &Layout.m_Workspace);
			Layout.m_Workspace.HSplitTop(Gap, nullptr, &Layout.m_Workspace);
		}
		else
		{
			const float Gap = std::min(ui_token::panel::GAP * Scale, View.w);
			const float Width = std::min(280.0f * Scale, (View.w - Gap) * 0.30f);
			View.VSplitLeft(Width, &Layout.m_Catalog, &Layout.m_Workspace);
			Layout.m_Workspace.VSplitLeft(Gap, nullptr, &Layout.m_Workspace);
		}
		return Layout;
	}

	// 矮窗口滚动整个编辑器：目录保留搜索、分类和一行，工作区的两个局部滚动区各保留一个控件高度。
	inline float MinimumEditorHeight(float Width, float Scale, float Line, float Gap, bool HasStatus)
	{
		const float Margin = ui_token::panel::PADDING * Scale;
		const float Padding = ui_token::settings::CARD_PADDING * Scale;
		const float PanelGap = ui_token::panel::GAP * Scale;
		const float Catalog = 2 * Padding + 4 * Line + 2 * Gap;
		const float Workspace = 2 * Padding + 5 * Line + Gap + (HasStatus ? Line : 0.0f);
		const bool Stacked = StacksEditorPanels(Width - 2 * Margin, Scale);
		const float Panels = Stacked ? PanelGap + std::max(Catalog / STACKED_CATALOG_SHARE, Workspace / (1 - STACKED_CATALOG_SHARE)) : std::max(Catalog, Workspace);
		return 2 * Margin + ui_token::page_title::HEIGHT + Line + PanelGap + Panels;
	}

	struct SWorkspaceLayout
	{
		CUIRect m_Details;
		CUIRect m_Search;
		CUIRect m_Candidates;
		CUIRect m_Footer;
	};

	inline SWorkspaceLayout ResolveWorkspaceLayout(CUIRect View, float DetailsHeight, float SearchHeight, float RowHeight, float FooterHeight)
	{
		View.w = std::max(0.0f, View.w);
		View.h = std::max(0.0f, View.h);
		DetailsHeight = std::max(0.0f, DetailsHeight);
		FooterHeight = std::max(0.0f, FooterHeight);
		SearchHeight = std::max(0.0f, SearchHeight);
		RowHeight = std::max(0.0f, RowHeight);
		const float DetailMin = std::min(DetailsHeight, RowHeight * 0.5f);
		const float FooterMin = std::min(FooterHeight, RowHeight * 0.5f);
		const float Minimum = DetailMin + SearchHeight + RowHeight + FooterMin;
		const float Fit = Minimum > View.h ? View.h / Minimum : 1.0f;
		const float Extra = DetailsHeight - DetailMin + FooterHeight - FooterMin;
		const float Expand = Extra > 0.0f ? std::clamp((View.h - Minimum) / Extra, 0.0f, 1.0f) : 0.0f;
		SWorkspaceLayout Result;
		View.HSplitTop(DetailMin * Fit + (DetailsHeight - DetailMin) * Expand, &Result.m_Details, &View);
		View.HSplitBottom(std::min(View.h, FooterMin * Fit + (FooterHeight - FooterMin) * Expand), &View, &Result.m_Footer);
		View.HSplitTop(std::min(View.h, SearchHeight * Fit), &Result.m_Search, &Result.m_Candidates);
		return Result;
	}
}

#endif
