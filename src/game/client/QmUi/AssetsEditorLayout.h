#ifndef GAME_CLIENT_QMUI_ASSETSEDITORLAYOUT_H
#define GAME_CLIENT_QMUI_ASSETSEDITORLAYOUT_H

#include <game/client/QmUi/UiTokens.h>

namespace qmclient::assets_editor
{
	struct SCanvasLayout
	{
		CUIRect m_Panel, m_Title, m_Search, m_Selector, m_Canvas;
	};
	struct SWorkbenchLayout
	{
		CUIRect m_Header, m_Tools, m_Export, m_Inspector, m_Status;
		SCanvasLayout m_Donor, m_Target;
		bool m_WrapInspector;
		float m_ContentHeight;
	};

	inline SWorkbenchLayout ResolveWorkbench(CUIRect View, float Scale, float Line)
	{
		SWorkbenchLayout Layout{};
		const float Gap = ui_token::panel::GAP * Scale;
		const float Padding = ui_token::settings::CARD_PADDING * Scale;
		const float Margin = ui_token::panel::PADDING * Scale;
		const float Width = std::max(0.0f, View.w - 2 * Margin);
		const bool Stack = Width < 19 * Line;
		const bool Wrap = Width < 28 * Line;
		Layout.m_WrapInspector = Wrap;
		const float PanelWidth = Stack ? Width : (Width - Gap) / 2;
		const bool WrapSelectors = PanelWidth - 2 * Padding < 17 * Line;
		const float CardHeader = WrapSelectors ? 2 * Line + Gap : Line;
		const float MinPanel = 2 * Padding + CardHeader + Gap + 4 * Line;
		const float Header = std::max(Line, ui_token::page_title::HEIGHT * Scale);
		const float Toolbar = Wrap ? 2 * Line + Gap : Line;
		const float Inspector = 2 * Padding + (Layout.m_WrapInspector ? 2 * Line + Gap : Line);
		const float MinHeight = 2 * Margin + Header + Toolbar + Inspector + Line + 4 * Gap + (Stack ? 2 * MinPanel + Gap : MinPanel);
		Layout.m_ContentHeight = std::max(View.h, MinHeight);
		View.h = Layout.m_ContentHeight;
		View.Margin(std::min(Margin, View.w / 2), &View);
		View.HSplitTop(Header, &Layout.m_Header, &View);
		View.HSplitTop(Gap, nullptr, &View);
		CUIRect Row;
		View.HSplitTop(Toolbar, &Row, &View);
		if(Wrap)
		{
			Row.HSplitTop(Line, &Layout.m_Tools, &Row);
			Row.HSplitTop(Gap, nullptr, &Layout.m_Export);
		}
		else
			Row.VSplitLeft(Row.w * 0.60f, &Layout.m_Tools, &Layout.m_Export);
		View.HSplitTop(Gap, nullptr, &View);
		View.HSplitBottom(Line, &View, &Layout.m_Status);
		View.HSplitBottom(Gap, &View, nullptr);
		View.HSplitBottom(Inspector, &View, &Layout.m_Inspector);
		View.HSplitBottom(Gap, &View, nullptr);
		if(Stack)
			View.HSplitMid(&Layout.m_Donor.m_Panel, &Layout.m_Target.m_Panel, Gap);
		else
			View.VSplitMid(&Layout.m_Donor.m_Panel, &Layout.m_Target.m_Panel, Gap);
		for(auto *pCard : {&Layout.m_Donor, &Layout.m_Target})
		{
			CUIRect Area = pCard->m_Panel;
			Area.Margin(std::min(Padding, Area.w / 2), &Area);
			Area.HSplitTop(CardHeader, &Row, &Area);
			Area.HSplitTop(Gap, nullptr, &pCard->m_Canvas);
			if(WrapSelectors)
			{
				Row.HSplitTop(Line, &pCard->m_Title, &Row);
				Row.HSplitTop(Gap, nullptr, &Row);
			}
			else
				Row.VSplitLeft(Row.w * 0.32f, &pCard->m_Title, &Row);
			Row.VSplitMid(&pCard->m_Search, &pCard->m_Selector, std::min(Row.w, Gap));
		}
		return Layout;
	}
}

#endif
