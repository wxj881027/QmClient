// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIFORMS_H
#define GAME_CLIENT_QMUI_UIFORMS_H

#include "QmScroll.h"
#include "UiContext.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>

#include <algorithm>
#include <array>
#include <cmath>

class CLineInput;
class CLineInputNumber;
class CUIRect;
class IScrollbarScale;

namespace ui_widget
{
	struct SBooleanControlLayout
	{
		CUIRect m_LabelRect;
		CUIRect m_ControlRect;
	};

	// 开关/复选框的横向分配只认一个正文字号：轨道按「行高 × SwitchTrackAspect」
	// 取宽，但不小于 SwitchMinWidth(正文)，窄行里开关仍保持可点尺寸；标签与控件
	// 之间只留 LabelControlGap(正文)。三处都随字号缩放，不再写死 1.65 / 30 / 8。
	inline SBooleanControlLayout ResolveBooleanControlLayout(const CUIRect &Rect, bool HasLabel, float BodySize = ui_token::font::BODY)
	{
		SBooleanControlLayout Layout{};
		const float RowHeight = std::max(0.0f, Rect.h);
		const float TrackWidth = std::max(ui_token::control::SwitchMinWidth(BodySize), RowHeight * ui_token::control::SwitchTrackAspect());
		const float Width = std::min(std::max(0.0f, Rect.w), TrackWidth);
		Layout.m_ControlRect = {HasLabel ? Rect.x + Rect.w - Width : Rect.x + (Rect.w - Width) * 0.5f, Rect.y, Width, RowHeight};
		Layout.m_LabelRect = {Rect.x, Rect.y, HasLabel ? std::max(0.0f, Rect.w - Width - ui_token::control::LabelControlGap(BodySize)) : 0.0f, RowHeight};
		return Layout;
	}

	// 开关行的标签列：行尾说明（约定 B）要贴这一列的右端，才会停在开关左侧；
	// 直接传整行会把提示推到行右缘，正好压在开关上。这里与 ResolveBooleanControlLayout
	// 同源，开关宽度或间距令牌改了，提示位置自动跟着走，不用在调用点猜控件宽度。
	inline CUIRect ResolveSwitchRowLabelColumn(const CUIRect &Rect, float BodySize = ui_token::font::BODY)
	{
		return ResolveBooleanControlLayout(Rect, true, BodySize).m_LabelRect;
	}

	struct SToggleLayout
	{
		CUIRect m_Track;
		CUIRect m_Knob;
		CUIRect m_Halo;
	};

	inline SToggleLayout ResolveToggleLayout(const CUIRect &Rect, float Progress, float Press = 0.0f)
	{
		SToggleLayout Layout{};
		const float Width = std::max(0.0f, Rect.w);
		const float Height = std::min(std::max(0.0f, Rect.h), Width / ui_token::control::SwitchTrackAspect());
		Layout.m_Track = {Rect.x, Rect.y + (Rect.h - Height) * 0.5f, Width, Height};
		const float RestRatio = std::lerp(ui_token::control::SWITCH_KNOB_OFF_RATIO, ui_token::control::SWITCH_KNOB_ON_RATIO, std::clamp(Progress, 0.0f, 1.0f));
		const float KnobSize = Height * std::lerp(RestRatio, ui_token::control::SWITCH_KNOB_PRESS_RATIO, std::clamp(Press, 0.0f, 1.0f));
		const float KnobRadius = KnobSize * 0.5f;
		const float HaloSize = Height * ui_token::control::SWITCH_HALO_RATIO;
		// 滑块绕自身圆心缩放。位移保留小幅过冲，尺寸钳制在轨道内。
		const float CenterX = Rect.x + std::clamp(std::lerp(Height * 0.5f, Width - Height * 0.5f, Progress), KnobRadius, Width - KnobRadius);
		const float CenterY = Layout.m_Track.y + Height * 0.5f;
		Layout.m_Knob = {CenterX - KnobRadius, CenterY - KnobRadius, KnobSize, KnobSize};
		Layout.m_Halo = {CenterX - HaloSize * 0.5f, CenterY - HaloSize * 0.5f, HaloSize, HaloSize};
		return Layout;
	}

	struct SHorizontalSliderLayout
	{
		CUIRect m_Handle;
		CUIRect m_HandleArea;
		CUIRect m_Track;
		CUIRect m_Fill;
	};

	inline SHorizontalSliderLayout ResolveHorizontalSliderLayout(const CUIRect &Rect, float Current)
	{
		SHorizontalSliderLayout Layout{};
		const float Width = std::max(0.0f, Rect.w);
		const float Height = std::max(0.0f, Rect.h);
		const float HandleSize = std::min({Width, Height, std::clamp(Height * 0.75f, 8.0f, 16.0f)});
		const float Progress = std::clamp(Current, 0.0f, 1.0f);
		Layout.m_Handle = {Rect.x + (Width - HandleSize) * Progress, Rect.y + (Height - HandleSize) * 0.5f, HandleSize, HandleSize};
		Layout.m_HandleArea = {Layout.m_Handle.x - 3.0f, Rect.y + Height * 0.05f, HandleSize + 6.0f, Height * 0.9f};
		const float TrackHeight = std::min(Height, std::clamp(Height * 0.12f, 2.0f, 3.0f));
		Layout.m_Track = {Rect.x + HandleSize * 0.5f, Rect.y + (Height - TrackHeight) * 0.5f, Width - HandleSize, TrackHeight};
		Layout.m_Fill = Layout.m_Track;
		Layout.m_Fill.w *= Progress;
		return Layout;
	}

	void DrawToggle(const IUiContext &Ctx, const void *pId, bool Value, const CUIRect &Rect, bool Enabled = true, bool Animate = true, const CUIRect *pHitRect = nullptr);
	void DrawMarkedControl(const IUiContext &Ctx, const void *pId, const char *pMark, const CUIRect &Rect, const CUIRect *pHitRect = nullptr);

	// 两端渐隐的细分隔线：面板内分组用，替代背景板。
	void DrawFadeDivider(CUi *pUi, const CUIRect &Rect);
	// 多选筛选胶囊：选中态由调用方传入，返回是否被点击。标签按控件宽度省略，不换行。
	// FontSize 缺省取胶囊档，但调用点应传正文档：按钮文字与正文同号才是同一条排版逻辑。
	bool FilterChip(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, const char *pLabel, bool Selected, bool Enabled = true, float FontSize = ui_token::server_filter::CHIP_FONT_SIZE);
	void DrawScrollbarHandle(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, bool Enabled = true, const ColorRGBA *pColorInner = nullptr, const ColorRGBA *pHandleColorOverride = nullptr);

	// 覆盖式滚动条几何（列表、聊天历史、控制台日志共用同一条算法）：
	// 轨道沿宿主视口铺开并在两端内缩一档，滑块长度按「视口 / 内容」求，夹在最小滑块与轨道之间，
	// 纵向位置由进度决定（0 = 顶部/最旧）。调用点只提供「视口多高、内容多高、滚到哪」。
	struct SOverlayScrollbarFrame
	{
		CUIRect m_Rail{};
		CUIRect m_Thumb{};
		float m_TrackRange = 0.0f;
		bool m_Visible = false;
	};
	inline SOverlayScrollbarFrame ResolveOverlayScrollbarFrame(const CUIRect &Owner, float Top, float Height, float ContentHeight, float Progress,
		bool OnRight, float Width, float Margin, float MinThumb)
	{
		SOverlayScrollbarFrame Result;
		const float ViewHeight = std::max(0.0f, Height);
		const float Content = std::max(0.0f, ContentHeight);
		// 内容装得下就不出现滚动条：与列表一致，不留一根占满轨道却动不了的滑块。
		if(Owner.w <= 0.0f || ViewHeight <= 0.0f || Content <= ViewHeight || Width <= 0.0f)
			return Result;
		SQmScrollContainerStyle Style;
		Style.m_Axis = EQmScrollAxis::VERTICAL;
		Style.m_ScrollbarWidth = Width + Margin * 2.0f;
		Style.m_ScrollbarMargin = Margin;
		Style.m_MinThumbHeight = std::max(0.0f, MinThumb);
		Style.m_ReserveScrollbarSpace = false;

		const CUIRect View{Owner.x, Top, Owner.w, ViewHeight};
		SQmScrollMetrics Metrics;
		Metrics.m_ViewportSize = ViewHeight;
		Metrics.m_ContentSize = Content;
		CQmScrollState ThumbState;
		ThumbState.SetOffset(std::clamp(Progress, 0.0f, 1.0f) * Metrics.MaxOffset(), Metrics);
		const CQmScrollController Controller;
		const SQmScrollContainerFrame Frame = Controller.PreviewFrame(ThumbState, View, Content, Style);
		if(!Frame.m_ScrollbarVisible)
			return Result;
		Result.m_Visible = true;
		Result.m_Rail = Frame.m_ScrollbarTrackRect;
		Result.m_Thumb = Frame.m_ScrollbarThumbRect;
		if(!OnRight)
		{
			// 公共容器只画右侧轨道；靠左时把整条轨道镜像到宿主左缘，内缩量同档。
			Result.m_Rail.x = Owner.x + (Owner.w - (Result.m_Rail.x - Owner.x) - Result.m_Rail.w);
			Result.m_Thumb.x = Result.m_Rail.x;
		}
		Result.m_TrackRange = std::max(0.0f, Result.m_Rail.h - Result.m_Thumb.h);
		return Result;
	}
	void RenderHorizontalSlider(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, float Current, const ColorRGBA *pColorInner = nullptr);
	enum class EInputFieldCapability : unsigned
	{
		DOUBLE_CLICK_SELECT_ALL = 1u << 0,
		CLICK_AWAY_COMMIT = 1u << 1,
		CURSOR_INSERTION = 1u << 2,
		MOUSE_DRAG_SELECTION = 1u << 3,
		CLEAR_BUTTON = 1u << 4,
		SEARCH_HOTKEY = 1u << 5,
	};

	constexpr unsigned InputFieldCapabilities()
	{
		return static_cast<unsigned>(EInputFieldCapability::DOUBLE_CLICK_SELECT_ALL) |
		       static_cast<unsigned>(EInputFieldCapability::CLICK_AWAY_COMMIT) |
		       static_cast<unsigned>(EInputFieldCapability::CURSOR_INSERTION) |
		       static_cast<unsigned>(EInputFieldCapability::MOUSE_DRAG_SELECTION);
	}

	constexpr unsigned ClearableInputFieldCapabilities()
	{
		return InputFieldCapabilities() |
		       static_cast<unsigned>(EInputFieldCapability::CLEAR_BUTTON);
	}

	constexpr unsigned SearchFieldCapabilities()
	{
		return ClearableInputFieldCapabilities() |
		       static_cast<unsigned>(EInputFieldCapability::SEARCH_HOTKEY);
	}

	struct SInputFieldResult
	{
		bool m_Changed = false;
		bool m_Committed = false;
		bool m_Submitted = false;
		bool m_Deactivated = false;
		bool m_Cleared = false;
		bool m_TrailingAction = false;
	};

	inline SInputFieldResult BuildInputFieldResult(bool WasActive, bool IsActive, bool Changed, bool SubmitPressed, bool WasEmpty, bool IsEmpty, bool Clearable)
	{
		SInputFieldResult Result;
		Result.m_Changed = Changed;
		Result.m_Deactivated = WasActive && !IsActive;
		Result.m_Committed = Result.m_Deactivated;
		Result.m_Submitted = Result.m_Deactivated && SubmitPressed;
		Result.m_Cleared = Clearable && Changed && !WasEmpty && IsEmpty;
		return Result;
	}

	struct SInputFieldLayout
	{
		CUIRect m_ShellRect;
		// 内容框：外壳按 control::PadX 左右内退后的整块可用区域。多行输入直接用它，
		// 因为多行从顶部起排，再上下退让会让首行错位。
		CUIRect m_ContentRect;
		// 单行正文框：内容框上下各退 control::PadY，剩余高度正好等于正文字号，
		// 即 LineHeight = Body + 2 × PadY。DoEditBox 在矩形内垂直居中，对称退让
		// 不移动文字视觉中心，只是把「可排正文」显式收成正文高度。
		CUIRect m_TextRect;
		CUIRect m_IconRect;
		CUIRect m_ClearRect;
		CUIRect m_TrailingRect;
	};

	// 用实际文本宽度将数值和单位作为一个整体居中；编辑矩形保留最小宽度，
	// 但其右边界仍贴合数值末尾，避免固定单位槽制造视觉空白。
	struct SInlineTrailingTextLayout
	{
		CUIRect m_TextRect;
		CUIRect m_TrailingRect;
		CUIRect m_VisualGroupRect;
	};

	inline SInlineTrailingTextLayout ResolveInlineTrailingTextLayout(const CUIRect &ContentRect, float TextWidth, float TrailingTextWidth, float UiScale = 1.0f)
	{
		SInlineTrailingTextLayout Layout{};
		const float Scale = std::max(UiScale, 0.1f);
		const float AvailableWidth = std::max(0.0f, ContentRect.w);
		const float ResolvedTextWidth = std::min(std::max(0.0f, TextWidth), AvailableWidth);
		const float ResolvedTrailingWidth = std::min(std::max(0.0f, TrailingTextWidth), std::max(0.0f, AvailableWidth - ResolvedTextWidth));
		const float Gap = std::min(3.0f * Scale, std::max(0.0f, AvailableWidth - ResolvedTextWidth - ResolvedTrailingWidth));
		const float GroupWidth = ResolvedTextWidth + Gap + ResolvedTrailingWidth;
		Layout.m_VisualGroupRect = {ContentRect.x + (AvailableWidth - GroupWidth) * 0.5f, ContentRect.y, GroupWidth, ContentRect.h};

		const float TextRight = Layout.m_VisualGroupRect.x + ResolvedTextWidth;
		const float MinimumEditWidth = 52.0f * Scale;
		const float EditWidth = std::min(AvailableWidth, std::max(MinimumEditWidth, ResolvedTextWidth));
		Layout.m_TextRect = {std::max(ContentRect.x, TextRight - EditWidth), ContentRect.y, std::min(EditWidth, TextRight - ContentRect.x), ContentRect.h};
		Layout.m_TrailingRect = {TextRight + Gap, ContentRect.y, ResolvedTrailingWidth, ContentRect.h};
		return Layout;
	}

	// 清除按钮只在有内容时出现；刚清空的那一帧仍保留旧状态，让 × 随内容
	// 淡出，而不是在空态常显成一个“关闭输入框”的按钮。
	inline bool QmInputFieldClearVisible(bool Empty, bool Active, bool WasEmpty)
	{
		return !Empty || (Active && !WasEmpty);
	}

	// 输入框的内边距只有两个来源：外壳左右内退 control::PadX，图标（前导图标、
	// 清除叉、尾部动作）与文字之间留 control::IconTextGap，都按该输入框实际使用的
	// 正文字号计算；调用方没给字号时按 UiScale 走与其他控件同一条夹取。
	inline SInputFieldLayout ResolveInputFieldLayout(const CUIRect &Rect, bool HasIcon, bool Clearable, float UiScale = 1.0f, float TrailingWidth = 0.0f, float BodySize = 0.0f, float MinTextWidth = 0.0f)
	{
		SInputFieldLayout Layout{};
		Layout.m_ShellRect = Rect;
		const float Scale = std::max(UiScale, 0.1f);
		const float Body = BodySize > 0.0f ? BodySize : ui_token::control::BodySizeFromScale(Scale);
		const float InsetX = ui_token::control::PadX(Body);
		const float IconGapX = ui_token::control::IconTextGap(Body);
		Layout.m_ContentRect = Rect;
		const float SlotWidth = std::max(Rect.h, 18.0f * Scale);
		Layout.m_ContentRect.x += InsetX;
		Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - InsetX * 2.0f);
		if(MinTextWidth > 0.0f)
		{
			// 先给正文和尾部动作留位，剩余宽度优先放清空叉，最后放前导图标。
			float Available = Layout.m_ContentRect.w - MinTextWidth;
			if(TrailingWidth > 0.0f)
				Available -= TrailingWidth + IconGapX;
			Clearable = Clearable && Available >= SlotWidth + IconGapX;
			if(Clearable)
				Available -= SlotWidth + IconGapX;
			HasIcon = HasIcon && Available >= SlotWidth + IconGapX;
		}
		if(HasIcon)
		{
			Layout.m_IconRect = Layout.m_ContentRect;
			Layout.m_IconRect.w = std::min(SlotWidth, Layout.m_ContentRect.w);
			const float Consumed = std::min(Layout.m_ContentRect.w, SlotWidth + IconGapX);
			Layout.m_ContentRect.x += Consumed;
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		if(Clearable)
		{
			Layout.m_ClearRect = Rect;
			Layout.m_ClearRect.w = std::min(SlotWidth, std::max(0.0f, Rect.w));
			Layout.m_ClearRect.x = Rect.x + std::max(0.0f, Rect.w - Layout.m_ClearRect.w);
			const float Consumed = std::min(Layout.m_ContentRect.w, SlotWidth + IconGapX);
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		if(TrailingWidth > 0.0f)
		{
			Layout.m_TrailingRect = Rect;
			Layout.m_TrailingRect.w = std::min(TrailingWidth, std::max(0.0f, Rect.w));
			const float TrailingRight = Clearable ? Layout.m_ClearRect.x : Rect.x + Rect.w;
			Layout.m_TrailingRect.x = std::max(Rect.x, TrailingRight - Layout.m_TrailingRect.w);
			Layout.m_TrailingRect.w = std::max(0.0f, TrailingRight - Layout.m_TrailingRect.x);
			const float Consumed = std::min(Layout.m_ContentRect.w, Layout.m_TrailingRect.w + IconGapX);
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		// 正文框沿用内容框的横向范围，只在纵向退让：行高不变 → 文字视觉中心不变。
		Layout.m_TextRect = Layout.m_ContentRect;
		const float PadY = ui_token::control::PadY(Layout.m_TextRect.h, Body);
		Layout.m_TextRect.y += PadY;
		Layout.m_TextRect.h = std::max(0.0f, Layout.m_TextRect.h - PadY * 2.0f);
		return Layout;
	}

	// 单行输入框的外壳圆角：取行高一半，超过卡片档就夹回 radius::CARD。
	// 与 ui_token::list::RowRadius 是同一套「按尺寸取圆角」的思路——中高控件（24~28）
	// 正好落成完整胶囊，比卡片档更高的行停在卡片档，不会被拉成体育场；绘制层仍按
	// 短边一半兜底。只有外壳走这里：内容框不跟着缩，光标/选区仍用原内容框，
	// 否则会出现「光标上沿被裁」的老问题（见 InputField 里 TextRect 的注释）。
	// 多行编辑器（EInputFieldMode::MULTILINE）不走这一档，保持 radius::BASE 方角。
	inline float ResolveInputCapsuleRadius(const CUIRect &Rect)
	{
		return std::min(std::max(0.0f, Rect.h) * 0.5f, ui_token::radius::CARD);
	}

	// 输入框聚焦光晕的一层：宽度、衰减指数与外壳边缘处的强度。
	struct SInputFocusGlowLayer
	{
		float m_Spread = 0.0f;
		float m_Falloff = 1.0f;
		float m_Alpha = 0.0f;
	};

	// 输入框聚焦光晕的层表（外层在前、内层在后，按这个顺序绘制）。
	// FocusAlpha 是外壳聚焦描边那条淡入轨道的当前进度：光晕不另开动画，
	// 聚焦淡入多少，光晕就亮多少；进度为 0 时返回两层全零，调用点直接跳过绘制。
	// 宽度按外壳高度取比例并夹取，非法矩形（宽或高 <= 0）不产生任何层。
	// 与输入框模式无关：文本、搜索、多行三种模式共用同一份层表，
	// 「哪个框亮」只由焦点决定，不由控件类型决定。
	// Enabled 来自 qm_ui_focus_glow：关掉时返回两层全零，绘制路径与未聚焦完全一致，
	// 不会留下「描边还在、光晕没了」之外的多余分支。
	inline std::array<SInputFocusGlowLayer, 2> ResolveInputFocusGlowLayers(const CUIRect &ShellRect, const float FocusAlpha, const bool Enabled = true)
	{
		std::array<SInputFocusGlowLayer, 2> aLayers{};
		if(!Enabled || ShellRect.w <= 0.0f || ShellRect.h <= 0.0f || FocusAlpha <= 0.001f)
			return aLayers;
		const float Intensity = std::clamp(FocusAlpha, 0.0f, 1.0f);
		const float Spread = ui_token::glow::InputFocusSpread(ShellRect.h);
		aLayers[0] = {Spread, ui_token::glow::INPUT_FOCUS_OUTER_FALLOFF, Intensity * ui_token::glow::INPUT_FOCUS_OUTER_ALPHA};
		aLayers[1] = {Spread * ui_token::glow::INPUT_FOCUS_INNER_RATIO, ui_token::glow::INPUT_FOCUS_INNER_FALLOFF, Intensity * ui_token::glow::INPUT_FOCUS_INNER_ALPHA};
		return aLayers;
	}

	struct STextFieldOptions
	{
		const char *m_pPlaceholder = nullptr;
		float m_FontSize = ui_token::font::BODY;
		float m_LineSpacing = 1.0f;
		int m_Corners = IGraphics::CORNER_ALL;
		float m_CornerRadius = ui_token::radius::BASE;
		int m_TextAlign = TEXTALIGN_ML;
	};

	enum class EInputFieldMode
	{
		TEXT,
		SEARCH,
		MULTILINE,
	};

	enum class EInputTextStyle
	{
		SMALL,
		BODY,
	};

	struct SInputFieldOptions
	{
		const char *m_pPlaceholder = nullptr;
		const char *m_pLeadingIcon = nullptr;
		// -1 时按 SEARCH 模式取放大镜；>= 0 时用指定的图集图标（例如排除框用 BAN）。
		int m_LeadingQmIcon = -1;
		const char *m_pTrailingText = nullptr;
		const void *m_pTrailingActionId = nullptr;
		const char *m_pTrailingActionIcon = nullptr;
		// -1 使用字体字形回退；Qm 图集图标使用统一的方形视图框。
		int m_TrailingActionQmIcon = -1;
		float m_TrailingWidth = 0.0f;
		EInputFieldMode m_Mode = EInputFieldMode::TEXT;
		EInputTextStyle m_TextStyle = EInputTextStyle::BODY;
		bool m_Clearable = false;
		bool m_SearchHotkeyEnabled = false;
		bool m_InlineTrailingText = false;
		// 回车默认释放输入焦点；嵌入弹层搜索框时由调用方保留焦点，
		// 让外层继续处理候选提交或空结果提示。
		bool m_ReleaseFocusOnEnter = true;
		// 复合控件拖拽期间可保留外层的鼠标捕获，同时显示输入框。
		bool m_ProcessInput = true;
		int m_Corners = IGraphics::CORNER_ALL;
		int m_TextAlign = -1;
		float m_FontSize = ui_token::font::BODY;
		float m_LineSpacing = 1.0f;
		float m_MinTextWidth = 0.0f;
	};

	inline int ResolveInputFieldTextAlign(const SInputFieldOptions &Options)
	{
		if(Options.m_TextAlign >= 0)
			return Options.m_TextAlign;
		return Options.m_Mode == EInputFieldMode::MULTILINE ? TEXTALIGN_TL : TEXTALIGN_ML;
	}

	SInputFieldResult InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const SInputFieldOptions &Options);
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder, float FontSize);
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, float FontSize, bool SearchHotkeyEnabled);
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder, float FontSize, const char *pIcon, bool Clearable);
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const STextFieldOptions &Options);
	// 只读输入框（不接收输入，仅渲染文本）。
	void ReadOnlyTextField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder = nullptr, float FontSize = ui_token::font::BODY);

	// 密码输入框：统一封装「内容隐藏 + 尾部眼睛按钮 + 显隐状态」。
	// 所有密码类输入框都走这里，显隐图标的外观与动画只在 UiForms 一处定义。
	struct SPasswordFieldOptions
	{
		const char *m_pPlaceholder = nullptr;
		float m_FontSize = ui_token::font::BODY;
		int m_Corners = IGraphics::CORNER_ALL;
		// 尾部按钮的方形槽位宽度；<= 0 时按输入框高度取。
		float m_TrailingWidth = 0.0f;
		bool m_Clearable = false;
	};

	// 调用点持有：显隐状态与尾部按钮 id 必须跨帧稳定。
	struct SPasswordFieldState
	{
		CButtonContainer m_Toggle;
		bool m_Visible = false;
	};

	SInputFieldResult PasswordField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, SPasswordFieldState *pState, const SPasswordFieldOptions &Options = {});

	// 整数输入框，自动与外部 int 同步。
	SInputFieldResult IntegerField(const IUiContext &Ctx, CLineInputNumber *pInput, int *pValue, int Min, int Max, const CUIRect &Rect, const STextFieldOptions &Options);

	inline int SliderInputStoredMinimum(int DisplayMin, int ValueMultiplier) { return DisplayMin / std::max(1, ValueMultiplier); }
	inline int SliderInputStoredMaximum(int DisplayMax, int ValueMultiplier) { return DisplayMax / std::max(1, ValueMultiplier); }
	inline int SliderInputDisplayValue(int StoredValue, int ValueMultiplier) { return StoredValue * std::max(1, ValueMultiplier); }
	inline int SliderInputStoredValue(int DisplayValue, int ValueMultiplier)
	{
		// 保留 int 上界的精度，避免转换回 int 前被舍入到范围外。
		return static_cast<int>(std::round(static_cast<double>(DisplayValue) / std::max(1, ValueMultiplier)));
	}
	inline int QuantizeNumericFieldStoredValue(int Value, int Minimum, int Maximum, int Step)
	{
		const int ClampedValue = std::clamp(Value, Minimum, Maximum);
		const int ValueStep = std::max(1, Step);
		return std::clamp(Minimum + (int)std::round((ClampedValue - Minimum) / (float)ValueStep) * ValueStep, Minimum, Maximum);
	}
	// 文本输入可拥有与滑条不同的上限；无限值始终使用 0 这个存储哨兵。
	inline int NumericFieldTextInputStoredValue(int DisplayValue, int ValueMultiplier, int InputMin, int SliderMax, int InputMax, bool ParsedInfinite)
	{
		if(ParsedInfinite)
			return 0;
		const int TextInputMax = InputMax >= 0 ? std::max(InputMin, InputMax) : SliderMax;
		return std::clamp(SliderInputStoredValue(DisplayValue, ValueMultiplier), InputMin, TextInputMax);
	}
	inline bool SliderInputIsInfiniteValue(int StoredValue, bool Infinite) { return Infinite && StoredValue == 0; }
	// 将无限值留出稳定的拖拽区域，避免它与有限最大值挤在不足一个像素的末端。
	inline float NumericFieldInfiniteEndpointStart(float SliderWidth, float UiScale)
	{
		const float EndpointWidth = std::clamp(12.0f * std::max(UiScale, 0.1f) / std::max(SliderWidth, 1.0f), 0.04f, 0.20f);
		return 1.0f - EndpointWidth;
	}
	inline int SliderInputWheelStoredValue(int StoredValue, int SliderMin, int SliderMax, bool Infinite, int Increment)
	{
		const int SliderValue = SliderInputIsInfiniteValue(StoredValue, Infinite) ? SliderMax : std::clamp(StoredValue, SliderMin, SliderMax);
		const int NewSliderValue = std::clamp(SliderValue + Increment, SliderMin, SliderMax);
		return Infinite && NewSliderValue == SliderMax ? 0 : NewSliderValue;
	}
	CUIRect SliderInputFieldLabelRect(const CUIRect &Rect, bool HasLabel, unsigned Flags = 0u);

	enum class EInputCommitPolicy
	{
		LIVE,
		ON_RELEASE_OR_SUBMIT,
	};

	struct SNumericFieldCommitState
	{
		int m_PendingStoredValue = 0;
		bool m_HasPendingValue = false;
		bool m_SliderWasActive = false;
	};

	struct SNumericFieldState : public SNumericFieldCommitState
	{
		CLineInputNumber m_Input;
		int m_LastSyncedStoredValue = 0;
		bool m_HasSyncedValue = false;
	};

	// 横向滚动条 + 输入框 + 单位组合。支持整数/浮点、线性/对数刻度、
	// ♾️ 无限符号和最大值文本。
	struct SNumericFieldOptions
	{
		const char *m_pLabel = nullptr;
		const char *m_pSuffix = nullptr;
		const IScrollbarScale *m_pScale = nullptr; // nullptr = 线性
		unsigned m_Flags = 0; // CUi::SCROLLBAR_OPTION_* 标志
		const char *m_pMaxText = nullptr; // 当值为 Max 且非 Infinite 时显示
		float m_FontSize = ui_token::font::BODY;
		float m_LineSpacing = 1.0f;
		float m_TrailingWidth = 34.0f;
		int m_LabelAlign = TEXTALIGN_ML;
		int m_ValueMultiplier = 1; // 滑动条以 Min/Multiplier..Max/Multiplier 为单位，显示/编辑真实值
		int m_ValueStep = 1; // 滑动条、滚轮和提交值按该 stored-value 步进量化
		int m_InputMin = -1; // -1 沿用滑动条下限；其他值为文本输入的 stored-value 下限
		int m_InputMax = -1; // -1 沿用滑动条上限；其他值为文本输入的 stored-value 上限
		EInputCommitPolicy m_CommitPolicy = EInputCommitPolicy::LIVE;
		CUIElement *m_pLabelElement = nullptr;
	};
	bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options);

	// Boolean switch. Slider position animates with a spring driver between left
	// (off) 与右侧 (on) 之间。调用者拥有更大的点击区域时可关闭内部输入；
	// 预布局时可同时关闭动画。点击时返回 true。
	bool Toggle(const IUiContext &Ctx, const void *pId, bool *pValue, const CUIRect &Rect, bool ProcessInput = true, bool Animate = true);

	// Horizontal slider with a numeric label on the right. Wraps DoScrollbarH.
	// Returns true when the value changed this frame.
	bool Slider(const IUiContext &Ctx, const void *pId, float *pValue, float Min, float Max, const CUIRect &Rect, const char *pSuffix = "");

} // namespace ui_widget

#endif
