// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIFORMS_H
#define GAME_CLIENT_QMUI_UIFORMS_H

#include "UiContext.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

class CLineInput;
class CLineInputNumber;
class CUIRect;
class IScrollbarScale;

namespace ui_widget
{
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
		CUIRect m_FocusRingRect;
		CUIRect m_ContentRect;
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

	inline SInputFieldLayout ResolveInputFieldLayout(const CUIRect &Rect, bool HasIcon, bool Clearable, float UiScale = 1.0f, float TrailingWidth = 0.0f)
	{
		SInputFieldLayout Layout{};
		Layout.m_ShellRect = Rect;
		const float Scale = std::max(UiScale, 0.1f);
		Layout.m_FocusRingRect = Rect;
		const float FocusOutset = std::max(1.0f, 2.0f * Scale);
		Layout.m_FocusRingRect.x -= FocusOutset;
		Layout.m_FocusRingRect.y -= FocusOutset;
		Layout.m_FocusRingRect.w += FocusOutset * 2.0f;
		Layout.m_FocusRingRect.h += FocusOutset * 2.0f;
		Layout.m_ContentRect = Rect;
		const float SlotWidth = std::max(Rect.h, 18.0f * Scale);
		const float Gap = 6.0f * Scale;
		Layout.m_ContentRect.x += Gap;
		Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Gap * 2.0f);
		if(HasIcon)
		{
			Layout.m_IconRect = Layout.m_ContentRect;
			Layout.m_IconRect.w = std::min(SlotWidth, Layout.m_ContentRect.w);
			const float Consumed = std::min(Layout.m_ContentRect.w, SlotWidth + Gap);
			Layout.m_ContentRect.x += Consumed;
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		if(Clearable)
		{
			Layout.m_ClearRect = Rect;
			Layout.m_ClearRect.w = std::min(SlotWidth, std::max(0.0f, Rect.w));
			Layout.m_ClearRect.x = Rect.x + std::max(0.0f, Rect.w - Layout.m_ClearRect.w);
			const float Consumed = std::min(Layout.m_ContentRect.w, SlotWidth + Gap);
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		if(TrailingWidth > 0.0f)
		{
			Layout.m_TrailingRect = Rect;
			Layout.m_TrailingRect.w = std::min(TrailingWidth, std::max(0.0f, Rect.w));
			const float TrailingRight = Clearable ? Layout.m_ClearRect.x : Rect.x + Rect.w;
			Layout.m_TrailingRect.x = std::max(Rect.x, TrailingRight - Layout.m_TrailingRect.w);
			Layout.m_TrailingRect.w = std::max(0.0f, TrailingRight - Layout.m_TrailingRect.x);
			const float Consumed = std::min(Layout.m_ContentRect.w, Layout.m_TrailingRect.w + Gap);
			Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Consumed);
		}
		return Layout;
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
		// 复合控件拖拽期间可保留外层的鼠标捕获，同时显示输入框。
		bool m_ProcessInput = true;
		int m_Corners = IGraphics::CORNER_ALL;
		int m_TextAlign = -1;
		float m_FontSize = ui_token::font::BODY;
		float m_LineSpacing = 1.0f;
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

	// 整数输入框，自动与外部 int 同步。
	SInputFieldResult IntegerField(const IUiContext &Ctx, CLineInputNumber *pInput, int *pValue, int Min, int Max, const CUIRect &Rect, const STextFieldOptions &Options);

	inline int SliderInputStoredMinimum(int DisplayMin, int ValueMultiplier) { return DisplayMin / std::max(1, ValueMultiplier); }
	inline int SliderInputStoredMaximum(int DisplayMax, int ValueMultiplier) { return DisplayMax / std::max(1, ValueMultiplier); }
	inline int SliderInputDisplayValue(int StoredValue, int ValueMultiplier) { return StoredValue * std::max(1, ValueMultiplier); }
	inline int SliderInputStoredValue(int DisplayValue, int ValueMultiplier) { return (int)std::round(DisplayValue / (float)std::max(1, ValueMultiplier)); }
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
