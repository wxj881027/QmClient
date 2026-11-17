#ifndef GAME_CLIENT_QMUI_QMCONSOLEUI_H
#define GAME_CLIENT_QMUI_QMCONSOLEUI_H

#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/QmUi/UiTextCaret.h>
#include <game/client/components/qmclient/console_appearance.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

#include <array>
#include <cmath>
#include <limits>

namespace QmConsoleUi
{
	// 输入框可能从底部向上换行，提示符必须沿用排版后的首行原点。
	inline CTextCursor DrawInputPrompt(ITextRender &TextRender, const STextBoundingBox &InputBounds, float X, float FontSize, const char *pPrompt)
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.SetPosition({X, InputBounds.m_Y});
		TextRender.TextEx(&Cursor, pPrompt);
		return Cursor;
	}

	struct SToolbarLayout
	{
		bool m_SplitRows;
		float m_Height;
		float m_FilterScale;
		float m_ActionScale;
		float m_ActionX;
	};

	// 显隐中的按钮宽度逐帧变化，但换行档位必须按目标集合决定：
	// 否则窄屏下按钮收缩到一半时会短暂折成单行，顶栏高度整帧跳一下。
	inline SToolbarLayout LayoutToolbarAnimated(float Width, float TargetFilterWidth, float TargetActionWidth, float CurrentFilterWidth, float CurrentActionWidth, float RowHeight = 26.0f)
	{
		const float Available = std::max(0.0f, Width - 20.0f);
		const bool Split = TargetFilterWidth > 0.0f && TargetActionWidth > 0.0f && TargetFilterWidth + TargetActionWidth + 10.0f > Available;
		const float FilterScale = CurrentFilterWidth > 0.0f ? std::min(1.0f, Available / CurrentFilterWidth) : 1.0f;
		const float ActionScale = CurrentActionWidth > 0.0f ? std::min(1.0f, Available / CurrentActionWidth) : 1.0f;
		return {Split, RowHeight * (Split ? 2.0f : 1.0f), FilterScale, ActionScale,
			std::max(10.0f, Width - 10.0f - CurrentActionWidth * ActionScale)};
	}

	inline SToolbarLayout LayoutToolbar(float Width, float FilterWidth, float ActionWidth, float RowHeight = 26.0f)
	{
		return LayoutToolbarAnimated(Width, FilterWidth, ActionWidth, FilterWidth, ActionWidth, RowHeight);
	}

	// 间距只存在于实际可见的相邻按钮之间：动作集合使用固定 ID 顺序，
	// 中间隐藏的槽位不能吞掉左右两颗按钮之间的缝。
	inline float ToolbarFlowGap(float Gap, bool HasPrevious, float Presence)
	{
		if(!HasPrevious || Presence <= 0.001f)
			return 0.0f;
		return std::max(0.0f, Gap);
	}

	template<size_t Count>
	inline float ToolbarFlowWidth(const std::array<float, Count> &aBaseWidths, const std::array<float, Count> &aPresences, float Gap)
	{
		float Total = 0.0f;
		bool HasPrevious = false;
		for(size_t i = 0; i < Count; ++i)
		{
			const float Presence = std::clamp(aPresences[i], 0.0f, 1.0f);
			if(Presence <= 0.001f)
				continue;
			Total += ToolbarFlowGap(Gap, HasPrevious, Presence);
			Total += std::max(0.0f, aBaseWidths[i]) * Presence;
			HasPrevious = true;
		}
		return Total;
	}

	// 同一按钮换文案时只做一次宽度过渡；反复点击从当前宽度折返，
	// 不把按钮拆成两颗一缩一长（那会临时多占一档间距并造成顿挫）。
	inline float ResolveToolbarWidth(CQmAnimationBackend *pRuntime, uint64_t NodeKey, float Target, bool MotionEnabled, const SUiAnimTransition &Transition)
	{
		Target = std::max(0.0f, Target);
		if(pRuntime == nullptr || !MotionEnabled)
		{
			if(pRuntime != nullptr)
				pRuntime->SetValue(NodeKey, EUiAnimProperty::WIDTH, Target);
			return Target;
		}
		SUiAnimTransition Effective = Transition;
		Effective.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Effective.m_Priority = std::max(Effective.m_Priority, 2);
		return std::max(0.0f, pRuntime->ResolveTargetValue(NodeKey, EUiAnimProperty::WIDTH, Target, Effective));
	}

	// 控制台渲染会临时启用 UI 以便自己处理输入，帧末再恢复进入本帧时的启用态。
	// 但本帧内状态机若已切换（例如点击面板外，OPEN → CLOSING），Toggle 写入的
	// 启用态才是正确结果；再用旧值覆盖会让下一帧主菜单一直保持禁用，按钮点不动。
	inline bool RestoreUiEnabledAtFrameEnd(bool EnabledBeforeFrame, bool EnabledNow, bool StateChangedDuringFrame)
	{
		return StateChangedDuringFrame ? EnabledNow : EnabledBeforeFrame;
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

	// 工具栏按钮的存在度：0 = 不占位、不接收点击，1 = 完全展开。
	// 宽度、表面透明度与命中都读同一条全局动画轨道；快速折返从当前值续接，
	// 不会先缩到 0 再重新长出来。
	template<size_t MaxItems>
	class CToolbarPresenceMotion
	{
		struct SState
		{
			bool m_Initialized = false;
			bool m_TargetPresent = false;
		};
		std::array<SState, MaxItems> m_aStates{};

	public:
		float Resolve(CQmAnimationBackend *pRuntime, uint64_t NodeKey, size_t Index, bool Present, bool MotionEnabled, const SUiAnimTransition &Transition)
		{
			const float Target = Present ? 1.0f : 0.0f;
			if(Index >= MaxItems)
				return Target;

			SState &State = m_aStates[Index];
			if(pRuntime == nullptr || !MotionEnabled)
			{
				State = {true, Present};
				if(pRuntime != nullptr)
					pRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, Target);
				return Target;
			}

			if(!State.m_Initialized)
			{
				State = {true, Present};
				pRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
				if(!Present)
					return 0.0f;
				RequestTarget(pRuntime, NodeKey, Target, Transition);
			}
			else if(State.m_TargetPresent != Present)
			{
				State.m_TargetPresent = Present;
				RequestTarget(pRuntime, NodeKey, Target, Transition);
			}
			else if(!pRuntime->HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA))
			{
				const float Current = pRuntime->GetValue(NodeKey, EUiAnimProperty::ALPHA, Target);
				if(std::abs(Current - Target) > 0.0001f)
					pRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, Target);
			}

			return std::clamp(pRuntime->GetValue(NodeKey, EUiAnimProperty::ALPHA, Target), 0.0f, 1.0f);
		}

		void Reset()
		{
			m_aStates = {};
		}

	private:
		static void RequestTarget(CQmAnimationBackend *pRuntime, uint64_t NodeKey, float Target, const SUiAnimTransition &Transition)
		{
			SUiAnimTransition Effective = Transition;
			Effective.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
			Effective.m_Priority = std::max(Effective.m_Priority, 2);
			pRuntime->ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, Target, Effective);
		}
	};

	// 选中底是全局强调色的派生档，不再取控制台命令语法色。
	// InactiveBlend=0 表示选中，1 表示普通；中间值由公共动画轨道给出。
	inline ColorRGBA ResolveConsoleButtonFill(const SUiTheme &Theme, const QmConsoleAppearance::SPalette &Palette, float InactiveBlend, bool Enabled)
	{
		const float Blend = std::clamp(InactiveBlend, 0.0f, 1.0f);
		const ColorRGBA Selected = Theme.m_Selected.WithMultipliedAlpha(Enabled ? 1.0f : 0.65f);
		if(Blend <= 0.0f)
			return Selected;
		if(Blend >= 1.0f)
			return Palette.m_Panel;
		return ColorRGBA(mix(Selected.r, Palette.m_Panel.r, Blend), mix(Selected.g, Palette.m_Panel.g, Blend),
			mix(Selected.b, Palette.m_Panel.b, Blend), mix(Selected.a, Palette.m_Panel.a, Blend));
	}

	inline void DrawPanel(CUi *pUi, const CUIRect &Rect, ColorRGBA Color)
	{
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);
	}

	// 控制台光标（日志定位光标）：几何与闪烁相位都由 qm_text_caret 统一解析，
	// 这里只按解析出的圆角把整块画出来，保证控制台与聊天/设置输入同一套线宽与相位。
	//
	// 相位参数由调用点每帧现算：时间取 qm_text_caret::NowSeconds()（全站同一条进程单调时钟），
	// 不允许把时间缓存成静态值或把相位钳成常量——那正是控制台光标之前常亮不闪的原因。
	// 只有两种时刻才传 m_Blink=false：光标此刻不该闪（拖选、输入法组合），
	// 以及 qm_ui_motion_level = 0（动效关闭 → 取亮相终点恒亮，唯一例外）。
	inline void DrawCaret(CUi *pUi, const qm_text_caret::SCaretParams &Params, ColorRGBA Color)
	{
		if(pUi == nullptr)
			return;
		const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(Params);
		if(!Shape.m_Visible || Shape.m_Rect.w <= 0.0f || Shape.m_Rect.h <= 0.0f)
			return;
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		SRoundedSurfaceParams Surface;
		Surface.m_Radius = Shape.m_Radius;
		Surface.m_PixelSize = std::max(Shape.m_PixelSize, 0.0001f);
		DrawRoundedSurface(pUi, Shape.m_Rect, Color, ColorRGBA(), Surface);
	}

	// 控制台使用独立的鼠标/触摸按下位置；统一在按钮内松开时触发一次。
	inline bool Button(CUi *pUi, const CUIRect &Rect, const char *pLabel, float FontSize, bool Selected,
		vec2 PressPosition, vec2 MousePosition, bool MouseDown, bool Released, bool Enabled, float FilterIndicatorWidth = 0.0f,
		const QmConsoleAppearance::SPalette *pPalette = nullptr, uint64_t AnimationKey = 0, float Presence = 1.0f)
	{
		const float Opacity = std::clamp(Presence, 0.0f, 1.0f);
		if(Opacity <= 0.001f)
			return false;
		const bool FullyPresent = Opacity >= 0.999f;
		const bool Hovered = FullyPresent && Rect.Inside(MousePosition);
		const bool PressedInside = Rect.Inside(PressPosition);
		const bool Pressed = Enabled && FullyPresent && MouseDown && PressedInside;
		const float Disabled = ResolveFilterDisabled(pUi->QmAnimationRuntime(), AnimationKey, Selected);
		ColorRGBA Fill;
		if(pPalette != nullptr)
		{
			const float Blend = AnimationKey != 0 ? Disabled : (Selected ? 0.0f : 1.0f);
			Fill = ResolveConsoleButtonFill(pUi->QmControlTheme(), *pPalette, Blend, Enabled);
		}
		else
			Fill = Selected ? ResolveConfiguredAccentColor().WithAlpha(Enabled ? 1.0f : 0.65f) : ResolveConfiguredControlSurface(Enabled);
		const ColorRGBA SurfaceTextFill = Fill;
		Fill = Fill.WithMultipliedAlpha(Opacity);
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		// 控制台顶部动作与筛选都是胶囊：半径取半高，与控制台设置里的滑块按钮同一形制。
		const float Radius = std::min(std::max(0.0f, Rect.w), std::max(0.0f, Rect.h)) * 0.5f;
		DrawRoundedSurface(pUi, Rect, Fill, ColorRGBA(), Radius);
		const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Fill), Enabled, Hovered, Pressed);
		if(Feedback.a > 0.0f)
			DrawRoundedSurface(pUi, Rect, Feedback, ColorRGBA(), Radius);
		const CUiScopedSurfaceText SurfaceText(pUi->TextRender(), SurfaceTextFill);
		if(pPalette != nullptr)
		{
			pUi->TextRender()->TextColor(pPalette->m_aColors[QmConsoleAppearance::TEXT]);
			pUi->TextRender()->TextOutlineColor(ResolveUiSurfaceForeground(pPalette->m_aColors[QmConsoleAppearance::TEXT]).WithAlpha(pUi->TextRender()->GetTextOutlineColor().a));
		}
		const float TextOpacity = Opacity * Opacity * Opacity;
		pUi->TextRender()->TextColor(pUi->TextRender()->GetTextColor().WithMultipliedAlpha(TextOpacity));
		pUi->TextRender()->TextOutlineColor(pUi->TextRender()->GetTextOutlineColor().WithMultipliedAlpha(TextOpacity));
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
			pUi->ClipEnable(&Rect);
			CTextCursor Cursor;
			Cursor.SetPosition(LabelLayout.m_Position);
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = LabelLayout.m_LineWidth;
			Cursor.m_Flags |= LabelLayout.m_Flags;
			pUi->FlushQuadBatch();
			pUi->TextRender()->TextEx(&Cursor, pLabel);
			pUi->ClipDisable();
		}
		return Enabled && FullyPresent && Released && PressedInside && Hovered;
	}
}

#endif
