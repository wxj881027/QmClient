#ifndef GAME_CLIENT_QMUI_SECONDARYPANEL_H
#define GAME_CLIENT_QMUI_SECONDARYPANEL_H

#include "SettingsPageLayout.h"
#include "UiForms.h"
#include "UiSurface.h"
#include "UiTheme.h"

#include <game/client/qm_icon.h>
#include <game/client/ui.h>

namespace ui_widget
{
	// 更新页先保留标题，再按内容区预算压缩字号和间距；版本与状态小字在标题栏，
	// 固定区只剩单行操作与下载进度，进度之外仍保证两行发布说明的空间。
	inline float ResolveUpdatePanelContentScale(const CUIRect &Content, float RequestedScale)
	{
		const float HeightBudget = ui_token::font::BODY * (ui_token::secondary_panel::UPDATE_ACTION_LINE_FACTOR + 1.7f + 2.0f) + 4.0f * ui_token::spacing::MD;
		const float WidthBudget = ui_token::secondary_panel::UPDATE_WIDTH - 2.0f * ui_token::spacing::LG;
		return std::min({std::clamp(RequestedScale, 0.5f, 2.0f), std::max(0.0f, Content.h) / HeightBudget, std::max(0.0f, Content.w) / WidthBudget});
	}

	struct SSecondaryPanelMetrics
	{
		float m_FontSize = ui_token::secondary_panel::BODY_SIZE;
		float m_Margin = ui_token::secondary_panel::MARGIN;
		float m_TitleHeight = ui_token::secondary_panel::TITLE_HEIGHT;
		float m_TitleFontSize = ui_token::secondary_panel::BODY_SIZE;
		float m_DividerHeight = 0.0f;
		float m_RowHeight = ui_token::secondary_panel::ROW_HEIGHT;
		float m_LabelHeight = ui_token::secondary_panel::LABEL_HEIGHT;
		float m_DropdownHeight = ui_token::secondary_panel::DROPDOWN_HEIGHT;
		float m_Spacing = ui_token::secondary_panel::GAP;

		float ContentHeight(int Toggles, int Dropdowns, int Notices) const
		{
			return 2.0f * m_Margin + m_TitleHeight + (m_DividerHeight > 0.0f ? 2.0f * m_Spacing + m_DividerHeight : 0.0f) +
			       (Toggles + Notices) * (m_Spacing + m_RowHeight) +
			       Dropdowns * (m_Spacing + std::max(m_LabelHeight, m_DropdownHeight));
		}
	};

	inline SSecondaryPanelMetrics ResolveSecondaryPanelMetrics(float ViewportWidth, const ESettingsSecondaryPanelKind Kind = ESettingsSecondaryPanelKind::LIBRARY, const float UiScale = 1.0f)
	{
		SSecondaryPanelMetrics Metrics;
		const SSettingsContentMetrics Settings = ResolveSettingsContentMetrics(ViewportWidth);
		Metrics.m_TitleHeight = Settings.m_LineHeight;
		Metrics.m_TitleFontSize = Settings.m_BodySize;
		Metrics.m_DividerHeight = 1.0f;
		if(Kind == ESettingsSecondaryPanelKind::NEWS)
			Metrics.m_Margin = ui_token::spacing::LG * std::clamp(UiScale, 0.5f, 2.0f);
		else if(Kind == ESettingsSecondaryPanelKind::UPDATE)
			Metrics.m_Margin = std::min(ui_token::spacing::LG * std::clamp(UiScale, 0.5f, 2.0f), std::max(0.0f, ViewportWidth) * 0.04f);
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

	enum class ESecondaryPanelPlacement
	{
		CENTERED,
		ANCHORED,
	};

	inline SPopupMenuProperties SecondaryPanelProperties(ESecondaryPanelPlacement Placement = ESecondaryPanelPlacement::CENTERED)
	{
		SPopupMenuProperties Props;
		Props.m_CenterInViewport = Placement == ESecondaryPanelPlacement::CENTERED;
		Props.m_BlockUnderlyingPointerInput = true;
		Props.m_BlockUnderlyingScroll = true;
		Props.m_Animate = true;
		Props.m_ReturnFocus = true;
		Props.m_IgnoreOpeningClick = true;
		const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
		Props.m_BackgroundColor = Theme.m_Surface;
		Props.m_BorderColor = Theme.m_Border;
		return Props;
	}

	// 全屏业务对话框和弹层栈中的二级面板共用外框尺寸及主题。
	inline CUIRect DrawSecondaryPanelShell(const IUiContext &Ctx, const CUIRect &Viewport, const ESettingsSecondaryPanelKind Kind = ESettingsSecondaryPanelKind::LIBRARY, const float UiScale = 1.0f)
	{
		// 遮罩覆盖根视口，面板沿用调用方的既有安全区，避免修遮罩时放大布局。
		const CUIRect MaskViewport = Ctx.m_pUi != nullptr ? *Ctx.m_pUi->Screen() : Viewport;
		MaskViewport.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_NONE, 0.0f);
		const CUIRect Panel = ResolveSettingsSecondaryPanelRect(Viewport, Kind, UiScale);
		const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
		DrawRoundedSurface(Ctx, Panel, Theme.m_Surface, Theme.m_Border, ui_token::radius::CARD);
		return Panel;
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
				if(!State.m_Element.TryInit(m_Ctx.m_pUi, 1))
					return;
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

		bool Header(SSecondaryPanelLabel &State, CButtonContainer &CloseButton, const char *pText, SSecondaryPanelLabel *pCaptionState = nullptr, const char *pCaption = nullptr)
		{
			CUIRect Row = TakeRow(m_Metrics.m_TitleHeight);
			const SSecondaryPanelHeaderLayout Layout = ResolveSecondaryPanelHeaderLayout(Row, m_Metrics.m_Spacing);
			Row = Layout.m_Title;
			const CUIRect Close = Layout.m_Close;
			const bool Clicked = m_Ctx.m_pUi->DoButton_QmIcon(&CloseButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &Close, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
			const float BodyFontSize = m_Metrics.m_FontSize;
			m_Metrics.m_FontSize = m_Metrics.m_TitleFontSize;
			if(pCaptionState != nullptr && pCaption != nullptr && pCaption[0] != '\0')
			{
				// 标题右侧小字（版本与更新状态）；按实测宽度截掉右端，标题用剩余宽度并继续省略。
				const float CaptionSize = m_Metrics.m_TitleFontSize * 0.85f;
				const float MaxCaptionWidth = std::max(0.0f, Row.w * 0.6f);
				const float NaturalWidth = m_Ctx.m_pUi->TextRender()->TextWidth(CaptionSize, pCaption, -1, -1.0f);
				const float CaptionWidth = std::clamp(NaturalWidth, 0.0f, MaxCaptionWidth);
				CUIRect CaptionRect;
				Row.VSplitRight(CaptionWidth, &Row, &CaptionRect);
				m_Metrics.m_FontSize = CaptionSize;
				ITextRender *pTextRender = m_Ctx.m_pUi->TextRender();
				const ColorRGBA PreviousColor = pTextRender->GetTextColor();
				pTextRender->TextColor(PreviousColor.WithMultipliedAlpha(0.65f));
				Label(*pCaptionState, CaptionRect, pCaption, TEXTALIGN_MR);
				pTextRender->TextColor(PreviousColor);
				m_Metrics.m_FontSize = m_Metrics.m_TitleFontSize;
			}
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
			const float SwitchHeight = ui_token::secondary_panel::SWITCH_HEIGHT;
			const float SwitchWidth = ui_token::secondary_panel::SWITCH_WIDTH;
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
			CUIRect Row = TakeRow(std::max(m_Metrics.m_LabelHeight, m_Metrics.m_DropdownHeight));
			CUIRect LabelRect, Control;
			const float Gap = std::min(m_Metrics.m_Spacing, Row.w * 0.1f);
			Row.VSplitLeft(Row.w * 0.45f, &LabelRect, &Row);
			Row.VSplitLeft(Gap, nullptr, &Control);
			Label(LabelState, LabelRect, pText, TEXTALIGN_ML);
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
			Row.Draw(ColorRGBA(0.7f, 0.3f, 0.3f, 0.6f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
			Row.VMargin(4.0f, &Row);
			Label(State, Row, pText, TEXTALIGN_ML);
		}
	};
}

#endif
