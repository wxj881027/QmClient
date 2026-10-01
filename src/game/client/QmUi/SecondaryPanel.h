#ifndef GAME_CLIENT_QMUI_SECONDARYPANEL_H
#define GAME_CLIENT_QMUI_SECONDARYPANEL_H

#include "SettingsPageLayout.h"
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
		float m_TitleFontSize = 7.5f;
		float m_DividerHeight = 0.0f;
		float m_RowHeight = 16.0f;
		float m_LabelHeight = 11.0f;
		float m_DropdownHeight = 18.0f;
		float m_Spacing = 4.0f;

		float ContentHeight(int Toggles, int Dropdowns, int Notices) const
		{
			return 2.0f * m_Margin + m_TitleHeight + (m_DividerHeight > 0.0f ? 2.0f * m_Spacing + m_DividerHeight : 0.0f) +
			       (Toggles + Notices) * (m_Spacing + m_RowHeight) +
			       Dropdowns * (m_Spacing + m_LabelHeight + m_DropdownHeight);
		}
	};

	inline SSecondaryPanelMetrics ResolveSecondaryPanelMetrics(float ViewportWidth, bool NewUi)
	{
		SSecondaryPanelMetrics Metrics;
		if(NewUi)
		{
			const SSettingsContentMetrics Settings = ResolveSettingsContentMetrics(ViewportWidth);
			Metrics.m_TitleHeight = Settings.m_LineHeight;
			Metrics.m_TitleFontSize = Settings.m_BodySize;
			Metrics.m_DividerHeight = 1.0f;
		}
		return Metrics;
	}

	struct SSecondaryPanelHeaderLayout
	{
		CUIRect m_Title;
		CUIRect m_Close;
	};

	inline SSecondaryPanelHeaderLayout ResolveSecondaryPanelHeaderLayout(CUIRect Row, float Spacing)
	{
		SSecondaryPanelHeaderLayout Result;
		const float Side = std::max(0.0f, std::min(Row.w, Row.h));
		Row.VSplitRight(Side, &Result.m_Title, &Result.m_Close);
		Result.m_Close = QmUiSquareIconButtonRect(Result.m_Close);
		Result.m_Title.VSplitRight(std::clamp(Spacing, 0.0f, std::max(0.0f, Result.m_Title.w)), &Result.m_Title, nullptr);
		return Result;
	}

	inline SPopupMenuProperties SecondaryPanelProperties()
	{
		SPopupMenuProperties Props;
		Props.m_CenterInViewport = true;
		Props.m_BlockUnderlyingPointerInput = true;
		Props.m_BlockUnderlyingScroll = true;
		Props.m_Animate = true;
		const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
		Props.m_BackgroundColor = Theme.m_Surface;
		Props.m_BorderColor = Theme.m_Border;
		return Props;
	}

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
			const SSecondaryPanelHeaderLayout Layout = ResolveSecondaryPanelHeaderLayout(Row, m_Metrics.m_Spacing);
			Row = Layout.m_Title;
			const CUIRect Close = Layout.m_Close;
			const bool Clicked = m_Ctx.m_pUi->DoButton_QmIcon(&CloseButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &Close, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
			const float BodyFontSize = m_Metrics.m_FontSize;
			m_Metrics.m_FontSize = m_Metrics.m_TitleFontSize;
			Label(State, Row, pText, TEXTALIGN_ML);
			m_Metrics.m_FontSize = BodyFontSize;
			if(m_Metrics.m_DividerHeight > 0.0f)
			{
				Space();
				CUIRect Divider = TakeRow(m_Metrics.m_DividerHeight);
				Divider.Draw(ResolveConfiguredSecondaryPanelTheme().m_Border, IGraphics::CORNER_NONE, 0.0f);
				Space();
			}
			return m_Active && Clicked;
		}

		CUIRect ContentRect() const { return m_View; }

		void ToggleRow(SSecondaryPanelLabel &State, const char *pText, int &Value)
		{
			Space();
			CUIRect Row = TakeRow(m_Metrics.m_RowHeight);
			const CUIRect FullRow = Row;
			CUIRect ToggleRect;
			const float SwitchWidth = g_Config.m_QmNewUi ? 30.0f : m_Metrics.m_RowHeight * 1.8f;
			const float SwitchHeight = g_Config.m_QmNewUi ? 16.0f : m_Metrics.m_RowHeight - 4.0f;
			Row.VSplitRight(SwitchWidth, &Row, &ToggleRect);
			ToggleRect.y += (ToggleRect.h - SwitchHeight) * 0.5f;
			ToggleRect.h = SwitchHeight;
			Label(State, Row, pText, TEXTALIGN_ML);
			bool Enabled = Value != 0;
			const bool Clicked = m_Active && m_Ctx.m_pUi != nullptr && m_Ctx.m_pUi->DoButtonLogic(&Value, 0, &FullRow, BUTTONFLAG_LEFT);
			Toggle(m_Ctx, &Value, &Enabled, ToggleRect, false);
			if(Clicked)
				Value = !Value;
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
