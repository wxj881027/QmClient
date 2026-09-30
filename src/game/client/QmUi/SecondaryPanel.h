#ifndef GAME_CLIENT_QMUI_SECONDARYPANEL_H
#define GAME_CLIENT_QMUI_SECONDARYPANEL_H

#include "UiForms.h"
#include "UiTheme.h"

#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>

namespace ui_widget
{
	struct SSecondaryPanelMetrics
	{
		float m_FontSize = 7.5f;
		float m_Margin = 3.0f;
		float m_TitleHeight = 16.0f;
		float m_RowHeight = 16.0f;
		float m_LabelHeight = 11.0f;
		float m_DropdownHeight = 18.0f;
		float m_Spacing = 4.0f;

		float ContentHeight(int Toggles, int Dropdowns, int Notices) const
		{
			return 2.0f * m_Margin + m_TitleHeight +
				(Toggles + Notices) * (m_Spacing + m_RowHeight) +
				Dropdowns * (m_Spacing + m_LabelHeight + m_DropdownHeight);
		}
	};

	struct SSecondaryPanelLabel
	{
		CUIElement m_Element;
		bool m_Initialized = false;
	};

	// 二级界面共用行布局与嵌套下拉策略，功能模块只提供状态、文字和主题。
	class CSecondaryPanel
	{
		IUiContext m_Ctx;
		CUIRect m_View;
		bool m_Active;
		SSecondaryPanelMetrics m_Metrics;
		SQmDropdownVisualStyle m_DropdownStyle;

		CUIRect TakeRow(float Height)
		{
			CUIRect Row;
			m_View.HSplitTop(Height, &Row, &m_View);
			return Row;
		}

		void Space() { m_View.HSplitTop(m_Metrics.m_Spacing, nullptr, &m_View); }

		void Label(SSecondaryPanelLabel &State, const CUIRect &Rect, const char *pText, int Align)
		{
			if(!State.m_Initialized)
			{
				State.m_Element.Init(m_Ctx.m_pUi, 1);
				State.m_Initialized = true;
			}
			SLabelProperties Props;
			Props.m_MaxWidth = maximum(0.0f, Rect.w - 2.0f);
			Props.m_EllipsisAtEnd = true;
			m_Ctx.m_pUi->DoLabelStreamed(*State.m_Element.Rect(0), &Rect, pText, m_Metrics.m_FontSize, Align, Props);
		}

	public:
		CSecondaryPanel(const IUiContext &Ctx, CUIRect View, bool Active, const SSecondaryPanelMetrics &Metrics, const SQmDropdownVisualStyle &DropdownStyle) :
			m_Ctx(Ctx), m_View(View), m_Active(Active), m_Metrics(Metrics), m_DropdownStyle(DropdownStyle)
		{
			m_View.Margin(Metrics.m_Margin, &m_View);
		}

		bool Header(SSecondaryPanelLabel &State, CButtonContainer &CloseButton, const char *pText)
		{
			CUIRect Row = TakeRow(m_Metrics.m_TitleHeight);
			CUIRect Close;
			Row.VSplitRight(m_Metrics.m_TitleHeight + 6.0f, &Row, &Close);
			const bool Clicked = m_Ctx.m_pUi->DoButton_QmIcon(&CloseButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &Close, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
			Label(State, Row, pText, TEXTALIGN_ML);
			return m_Active && Clicked;
		}

		void ToggleRow(SSecondaryPanelLabel &State, const char *pText, int &Value)
		{
			Space();
			CUIRect Row = TakeRow(m_Metrics.m_RowHeight);
			CUIRect ToggleRect;
			Row.VSplitRight(m_Metrics.m_RowHeight * 1.8f, &Row, &ToggleRect);
			ToggleRect.HMargin(2.0f, &ToggleRect);
			Label(State, Row, pText, TEXTALIGN_ML);
			bool Enabled = Value != 0;
			if(Toggle(m_Ctx, &Value, &Enabled, ToggleRect, m_Active))
				Value = Enabled ? 1 : 0;
		}

		int DropdownRow(SSecondaryPanelLabel &LabelState, const char *pText, int Selected, const char *const *ppNames, int Count, CUi::SDropDownState &State)
		{
			Space();
			const CUIRect LabelRect = TakeRow(m_Metrics.m_LabelHeight);
			Label(LabelState, LabelRect, pText, TEXTALIGN_ML);
			CUIRect Control = TakeRow(m_Metrics.m_DropdownHeight);
			CUi::SDropDownProperties Props;
			Props.m_Enabled = m_Active;
			// 父层失焦只禁用触发器，子菜单保留来源帧；不依赖功能开关。
			Props.m_ClosePopupWhenDisabled = false;
			Props.m_FontSize = m_Metrics.m_FontSize;
			Props.m_VisualStyle = m_DropdownStyle;
			Props.m_pPopupViewport = m_Ctx.m_pUi->Screen();
			return m_Ctx.m_pUi->DoDropDown(&Control, Selected, ppNames, Count, State, Props);
		}

		void Notice(SSecondaryPanelLabel &State, const char *pText)
		{
			Space();
			CUIRect Row = TakeRow(m_Metrics.m_RowHeight);
			if(pText == nullptr)
				return;
			Row.Draw(ColorRGBA(0.7f, 0.3f, 0.3f, 0.6f), IGraphics::CORNER_ALL, 4.0f);
			Row.VMargin(4.0f, &Row);
			Label(State, Row, pText, TEXTALIGN_ML);
		}
	};
}

#endif
