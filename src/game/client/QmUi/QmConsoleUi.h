#ifndef GAME_CLIENT_QMUI_QMCONSOLEUI_H
#define GAME_CLIENT_QMUI_QMCONSOLEUI_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/components/qmclient/console_appearance.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

#include <limits>

namespace QmConsoleUi
{
	struct SToolbarLayout
	{
		bool m_SplitRows;
		float m_Height;
		float m_FilterScale;
		float m_ActionScale;
		float m_ActionX;
	};

	inline SToolbarLayout LayoutToolbar(float Width, float FilterWidth, float ActionWidth, float RowHeight = 26.0f)
	{
		const float Available = std::max(0.0f, Width - 20.0f);
		const bool Split = FilterWidth > 0.0f && ActionWidth > 0.0f && FilterWidth + ActionWidth + 10.0f > Available;
		const float FilterScale = FilterWidth > 0.0f ? std::min(1.0f, Available / FilterWidth) : 1.0f;
		const float ActionScale = ActionWidth > 0.0f ? std::min(1.0f, Available / ActionWidth) : 1.0f;
		return {Split, RowHeight * (Split ? 2.0f : 1.0f), FilterScale, ActionScale,
			std::max(10.0f, Width - 10.0f - ActionWidth * ActionScale)};
	}

	struct SButtonContentLayout
	{
		CUIRect m_Icon;
		CUIRect m_Label;
	};

	// 按实际显示内容居中，长标签保留省略空间；无图标时不占用空槽。
	inline SButtonContentLayout LayoutButtonContent(const CUIRect &Rect, float TextWidth, float FontSize, float IconSlotWidth)
	{
		const float Width = std::max(0.0f, Rect.w);
		const float Height = std::max(0.0f, Rect.h);
		const float Padding = std::min(std::max(0.0f, FontSize) * 0.5f, Width * 0.25f);
		const float Available = Width - Padding * 2.0f;
		const float IconSize = IconSlotWidth > 0.0f ? std::min({IconSlotWidth, Height, std::max(0.0f, FontSize) * 1.25f, Available * 0.35f}) : 0.0f;
		const float Gap = IconSize > 0.0f && TextWidth > 0.0f ? std::min(std::min(std::max(0.0f, FontSize) * 0.35f, IconSize * 0.35f), (Available - IconSize) * 0.25f) : 0.0f;
		const float LabelWidth = std::clamp(TextWidth, 0.0f, Available - IconSize - Gap);
		const float Start = Rect.x + (Width - IconSize - Gap - LabelWidth) * 0.5f;
		return {{Start, Rect.y + (Height - IconSize) * 0.5f, IconSize, IconSize},
			{Start + IconSize + Gap, Rect.y, LabelWidth, Height}};
	}

	// 控制台普通标签隔离调用方图标字体与特殊 bearing，测量和绘制使用同一状态。
	class CButtonTextStyle
	{
		ITextRender &m_TextRender;
		unsigned m_Flags;
		EFontPreset m_Preset;

	public:
		explicit CButtonTextStyle(ITextRender &TextRender) :
			m_TextRender(TextRender), m_Flags(TextRender.GetRenderFlags()), m_Preset(TextRender.GetFontPreset())
		{
			TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender.SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
		}
		~CButtonTextStyle()
		{
			m_TextRender.SetRenderFlags(m_Flags);
			m_TextRender.SetFontPreset(m_Preset);
		}
	};

	struct SButtonLabelLayout
	{
		SButtonContentLayout m_Content;
		vec2 m_Position;
		float m_LineWidth;
		int m_Flags;
		float m_VisualWidth;
	};

	inline SButtonLabelLayout MeasureButtonLabel(ITextRender &TextRender, const CUIRect &Rect, const char *pLabel, float FontSize, float IconSlotWidth)
	{
		const CButtonTextStyle TextStyle(TextRender);
		CTextCursor Cursor;
		Cursor.m_Flags = TEXTFLAG_DISALLOW_NEWLINE | TEXTFLAG_STOP_AT_END;
		Cursor.m_FontSize = FontSize;
		// 正行宽保持普通 bearing；无限内容宽度不触发无行宽的首尾特殊度量。
		Cursor.m_LineWidth = std::numeric_limits<float>::max();
		Cursor.m_CalculateVisualBoundingBox = true;
		TextRender.TextEx(&Cursor, pLabel);
		auto VisualWidth = [](const CTextCursor &Measured) {
			return Measured.m_HasVisualBoundingBox ? std::max(0.0f, Measured.m_VisualRight - Measured.m_VisualLeft) : 0.0f;
		};
		auto Content = LayoutButtonContent(Rect, VisualWidth(Cursor), FontSize, IconSlotWidth);
		if(VisualWidth(Cursor) > Content.m_Label.w && Content.m_Label.w > 0.0f)
		{
			Cursor = CTextCursor();
			Cursor.m_Flags = TEXTFLAG_DISALLOW_NEWLINE | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = Content.m_Label.w;
			Cursor.m_CalculateVisualBoundingBox = true;
			TextRender.TextEx(&Cursor, pLabel);
			// 极窄按钮连省略号也放不下时只保留图标，不让字形越过按钮。
			if(VisualWidth(Cursor) > Content.m_Label.w)
				return {LayoutButtonContent(Rect, 0.0f, FontSize, IconSlotWidth), Rect.TopLeft(), 0.0f, 0, 0.0f};
			Content = LayoutButtonContent(Rect, VisualWidth(Cursor), FontSize, IconSlotWidth);
		}
		const float Left = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualLeft : 0.0f;
		const float Top = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualTop : 0.0f;
		const float Height = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualBottom - Top : 0.0f;
		return {Content, vec2(Content.m_Label.x + (Content.m_Label.w - VisualWidth(Cursor)) * 0.5f - Left, Rect.y + (std::max(0.0f, Rect.h) - Height) * 0.5f - Top), Cursor.m_LineWidth, Cursor.m_Flags, VisualWidth(Cursor)};
	}

	// 导出框按实际条目范围缩放，留出边缘间距，避免点框选中相邻日志。
	inline CUIRect LayoutExportCheckbox(const CUIRect &EntryRect)
	{
		const float Width = std::max(0.0f, EntryRect.w);
		const float Height = std::max(0.0f, EntryRect.h);
		const float Inset = std::min(0.25f, Height * 0.1f);
		const float Size = std::min({11.0f, Height - Inset * 2.0f, Width});
		const float LeftInset = std::min(5.0f, (Width - Size) * 0.5f);
		return {EntryRect.x + LeftInset, EntryRect.y + (Height - Size) * 0.5f, Size, Size};
	}

	// 选区覆盖完整行高，相邻行直接相接；横向保留少量字形余量。
	inline CUIRect LayoutSelectionBackground(const IGraphics::CQuadItem &Quad, float FontSize)
	{
		if(Quad.m_Width <= 0.0f || Quad.m_Height <= 0.0f)
			return {Quad.m_X, Quad.m_Y, 0.0f, 0.0f};
		const float Padding = std::max(0.0f, FontSize) * 0.1f;
		return {Quad.m_X - Padding, Quad.m_Y, Quad.m_Width + Padding * 2.0f, Quad.m_Height};
	}

	// 筛选后的正文独立淡入，快速切换从当前透明度重新过渡。
	class CFilterContentMotion
	{
		int m_Mask = -1;
		double m_Started = 0.0;
		float m_StartAlpha = 1.0f;
		float m_Alpha = 1.0f;

	public:
		float Resolve(int Mask, double Now, bool Animate)
		{
			if(m_Mask != Mask)
			{
				m_StartAlpha = m_Mask < 0 ? 1.0f : std::min(m_Alpha, 0.15f);
				m_Mask = Mask;
				m_Started = Now;
			}
			const float T = Animate ? std::clamp(static_cast<float>((Now - m_Started) / 0.26), 0.0f, 1.0f) : 1.0f;
			m_Alpha = m_StartAlpha + (1.0f - m_StartAlpha) * (T * T * (3.0f - 2.0f * T));
			return m_Alpha;
		}
		float Offset() const { return 8.0f * (1.0f - m_Alpha); }
	};

	inline float LogBottomBeforeSeparator(float SeparatorY, float FontSize)
	{
		return SeparatorY - std::max(3.0f, FontSize * 0.35f);
	}

	// 使用公共可打断动画轨道，禁用标识与标签位置共用同一个进度。
	inline float ResolveFilterDisabled(CQmAnimationBackend *pRuntime, uint64_t NodeKey, bool Selected)
	{
		const float Target = Selected ? 0.0f : 1.0f;
		if(pRuntime == nullptr || NodeKey == 0)
			return Target;
		SUiAnimTransition Transition;
		Transition.m_DurationSec = 0.16f;
		Transition.m_Easing = EEasing::EASE_IN_OUT;
		Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		return std::clamp(pRuntime->ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, Target, Transition), 0.0f, 1.0f);
	}

	inline void DrawPanel(CUi *pUi, const CUIRect &Rect, ColorRGBA Color)
	{
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);
	}

	// 控制台使用独立的鼠标/触摸按下位置；统一在按钮内松开时触发一次。
	inline bool Button(CUi *pUi, const CUIRect &Rect, const char *pLabel, float FontSize, bool Selected,
		vec2 PressPosition, vec2 MousePosition, bool MouseDown, bool Released, bool Enabled, float FilterIndicatorWidth = 0.0f, const QmConsoleAppearance::SPalette *pPalette = nullptr, uint64_t AnimationKey = 0)
	{
		const bool Hovered = Rect.Inside(MousePosition);
		const bool PressedInside = Rect.Inside(PressPosition);
		const bool Pressed = Enabled && MouseDown && PressedInside;
		const float Disabled = ResolveFilterDisabled(pUi->QmAnimationRuntime(), AnimationKey, Selected);
		ColorRGBA Fill;
		if(pPalette != nullptr)
		{
			const float Blend = AnimationKey != 0 ? Disabled : (Selected ? 0.0f : 1.0f);
			const auto &From = pPalette->m_SelectedButton;
			const auto &To = pPalette->m_Panel;
			Fill = ColorRGBA(mix(From.r, To.r, Blend), mix(From.g, To.g, Blend), mix(From.b, To.b, Blend), mix(From.a, To.a, Blend));
		}
		else
			Fill = Selected ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(Enabled ? 1.0f : 0.65f) : ResolveConfiguredControlSurface(Enabled);
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		DrawRoundedSurface(pUi, Rect, Fill, ColorRGBA(), ui_token::radius::BASE);
		const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Fill), Enabled, Hovered, Pressed);
		if(Feedback.a > 0.0f)
			DrawRoundedSurface(pUi, Rect, Feedback, ColorRGBA(), ui_token::radius::BASE);
		const CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
		if(pPalette != nullptr)
		{
			pUi->TextRender()->TextColor(pPalette->m_aColors[QmConsoleAppearance::TEXT]);
			pUi->TextRender()->TextOutlineColor(ResolveUiSurfaceForeground(pPalette->m_aColors[QmConsoleAppearance::TEXT]).WithAlpha(pUi->TextRender()->GetTextOutlineColor().a));
		}
		const CButtonTextStyle TextStyle(*pUi->TextRender());
		const auto LabelLayout = MeasureButtonLabel(*pUi->TextRender(), Rect, pLabel, FontSize, FilterIndicatorWidth * Disabled);
		const auto &Content = LabelLayout.m_Content;
		if(Content.m_Icon.w > 0.0f)
		{
			const CQmIconSemanticColorScope SemanticColorScope;
			pUi->DrawQmIcon(Content.m_Icon, EQmIcon::BAN, FontIcons::FONT_ICON_BAN, pUi->TextRender()->GetTextColor().WithAlpha(pUi->TextRender()->GetTextColor().a * Disabled));
		}
		if(Content.m_Label.w > 0.0f)
		{
			CTextCursor Cursor;
			Cursor.SetPosition(LabelLayout.m_Position);
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = LabelLayout.m_LineWidth;
			Cursor.m_Flags |= LabelLayout.m_Flags;
			pUi->FlushQuadBatch();
			pUi->TextRender()->TextEx(&Cursor, pLabel);
		}
		return Enabled && Released && PressedInside && Hovered;
	}
}

#endif
