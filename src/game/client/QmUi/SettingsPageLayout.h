/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_SETTINGSPAGELAYOUT_H
#define GAME_CLIENT_QMUI_SETTINGSPAGELAYOUT_H

#include "UiTokens.h"

#include <base/color.h>
#include <base/math.h>

#include <generated/protocol.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

struct SSettingsPageLayoutFrame
{
	CUIRect m_PageRect;
	CUIRect m_UnreservedScrollViewport;
	CUIRect m_ScrollViewport;
	CUIRect m_ContentViewport;
	CUIRect m_SubTabRect;
	CUIRect m_aColumns[2];
	float m_CardGap = 0.0f;
	bool m_TwoColumns = false;
};

struct SSettingsShellLayoutFrame
{
	CUIRect m_ShellRect;
	CUIRect m_ContentPanelRect;
	CUIRect m_ContentRect;
	CUIRect m_TabBarRect;
	CUIRect m_RestartBarRect;
	CUIRect m_ScrollViewport;
	CUIRect m_aColumns[2];
	float m_TabBarWidth = 0.0f;
	float m_UiScale = 1.0f;
	float m_CardGap = 0.0f;
	bool m_TwoColumns = false;
};

enum class ESettingsSecondaryPanelKind
{
	LIBRARY,
	NEWS,
	UPDATE,
};

// 共享布局策略沿用各用途原有尺寸；狭小视口仍夹在可用范围内。
inline CUIRect ResolveSettingsSecondaryPanelRect(const CUIRect &Viewport, const ESettingsSecondaryPanelKind Kind = ESettingsSecondaryPanelKind::LIBRARY, const float UiScale = 1.0f)
{
	const float Scale = std::clamp(UiScale, 0.5f, 2.0f);
	float Width;
	float Height;
	if(Kind == ESettingsSecondaryPanelKind::NEWS)
	{
		Width = std::min(ui_token::secondary_panel::NEWS_WIDTH * Scale, std::max(0.0f, Viewport.w - 2.0f * ui_token::spacing::LG));
		Height = std::max(0.0f, Viewport.h - 4.0f * ui_token::spacing::LG);
	}
	else if(Kind == ESettingsSecondaryPanelKind::UPDATE)
	{
		const float Padding = std::min(ui_token::spacing::LG * Scale, std::max(0.0f, Viewport.w) * 0.04f);
		Width = std::min(ui_token::secondary_panel::UPDATE_WIDTH * Scale, std::max(0.0f, Viewport.w - 2.0f * Padding));
		// 发布说明通常很长，高度与公告面板一致占满可用视口，不再使用固定扁窗。
		Height = std::max(0.0f, Viewport.h - 4.0f * ui_token::spacing::LG);
	}
	else
	{
		Width = std::min(ui_token::secondary_panel::LIBRARY_WIDTH, std::max(0.0f, Viewport.w) * ui_token::secondary_panel::LIBRARY_VIEWPORT_FRACTION);
		Height = std::min(ui_token::secondary_panel::LIBRARY_HEIGHT, std::max(0.0f, Viewport.h) * ui_token::secondary_panel::LIBRARY_VIEWPORT_FRACTION);
	}
	return CUIRect{Viewport.x + (Viewport.w - Width) * 0.5f, Viewport.y + (Viewport.h - Height) * 0.5f, Width, Height};
}

struct SSettingsSubTabLayoutFrame
{
	CUIRect m_TabBarRect;
	CUIRect m_ContentRect;
};

// 文本、预览和多行编辑器消费实际测量高度，不参与单行控件的高度策略。
inline void ConsumeSettingsMeasuredRow(CUIRect &Content, const float RowHeight, CUIRect *pRow)
{
	Content.HSplitTop(RowHeight, pRow, &Content);
}
inline float ResolveSettingsControlFontSize(const float BodySize)
{
	return std::max(ui_token::font::BODY, BodySize);
}

inline float ResolveSettingsControlHeight(const float BodySize, const float UiScale = 1.0f)
{
	const float ExistingRowHeight = std::clamp(ui_token::settings::ROW_HEIGHT * UiScale, ui_token::settings::MIN_ROW_HEIGHT, ui_token::settings::ROW_HEIGHT);
	return std::max(ExistingRowHeight, ResolveSettingsControlFontSize(BodySize) + 2.0f * ui_token::settings::CONTROL_PADDING_Y);
}

struct SSettingsContentMetrics
{
	float m_UiScale = 1.0f;
	float m_LineHeight = ResolveSettingsControlHeight(ui_token::font::BODY);
	float m_BodySize = ui_token::font::BODY;
	float m_SmallSize = ui_token::font::SMALL;
	float m_HeadlineSize = ui_token::font::HEADLINE;
	float m_LineSpacing = ui_token::settings::ROW_GAP;
	float m_RowStep = ResolveSettingsControlHeight(ui_token::font::BODY) + ui_token::settings::ROW_GAP;
	float m_InputHeight = ResolveSettingsControlHeight(ui_token::font::BODY);
	float m_ButtonHeight = ResolveSettingsControlHeight(ui_token::font::BODY);
	float m_SectionGap = 2.0f * ui_token::settings::ROW_GAP;
	float m_BadgeHeight = ui_token::settings::BADGE_HEIGHT;
	float m_ListRowHeight = ui_token::settings::ROW_HEIGHT;
	float m_LabelWidth = 120.0f;
	float m_CardGap = ui_token::settings::CARD_GAP;
};

// 普通单行控件由公共 Metrics 决定高度，页面不能另外传入局部数值。
inline void ConsumeSettingsControlRow(CUIRect &Content, const SSettingsContentMetrics &Metrics, CUIRect *pRow)
{
	ConsumeSettingsMeasuredRow(Content, Metrics.m_LineHeight, pRow);
}

struct SSettingsCardDeckDisplayCycleState
{
	bool EnterPage(const int Page)
	{
		return EnterView((uint64_t)(uint32_t)Page);
	}

	bool EnterView(const uint64_t Key)
	{
		const bool StartCycle = !m_Visible || m_Key != Key;
		m_Visible = true;
		m_Key = Key;
		return StartCycle;
	}

	void LeaveSettings()
	{
		m_Visible = false;
	}

private:
	uint64_t m_Key = 0;
	bool m_Visible = false;
};

// 页面和当前子 Tab 共同定义一次 card deck 的可见视图。
// 子 Tab 改变时必须重新播放入场动画，但不能影响卡片在 model 中的持久化顺序。
inline uint64_t ResolveSettingsCardDisplayViewKey(const int Page, const int PlayerTab, const int AppearanceTab, const int TClientTab, const int QmClientTab, const int CreditsTab = 0)
{
	uint64_t Key = 1469598103934665603ULL;
	const auto Mix = [&Key](const int Value) {
		Key ^= (uint32_t)(Value + 1);
		Key *= 1099511628211ULL;
	};
	Mix(Page);
	Mix(PlayerTab);
	Mix(AppearanceTab);
	Mix(TClientTab);
	Mix(QmClientTab);
	Mix(CreditsTab);
	return Key;
}

inline uint64_t ResolveSettingsCardDefinitionsRevision(uint64_t DisplayCycle, uint64_t TextGeneration, float ContentWidth, uint64_t LayoutRevision = 0)
{
	const uint64_t QuantizedWidth = (uint64_t)std::max(0, (int)(ContentWidth * 100.0f + 0.5f));
	uint64_t Revision = 1469598103934665603ULL;
	const auto Mix = [&Revision](uint64_t Value) {
		Revision ^= Value;
		Revision *= 1099511628211ULL;
	};
	Mix(DisplayCycle);
	Mix(TextGeneration);
	Mix(QuantizedWidth);
	Mix(LayoutRevision);
	return Revision;
}

struct SSettingsConfigRowMetrics
{
	float m_ControlLineHeight = 0.0f;
	float m_ControlBlockHeight = 0.0f;
	float m_HelpGap = 0.0f;
	float m_RowHeight = 0.0f;
};

// 胶囊的最长选项宽度按文字、字号与字体版本缓存，稳定帧不重复测字。
class CSettingsChoiceWidthCache
{
	std::vector<std::string> m_vLabels;
	float m_FontSize = -1.0f;
	float m_Width = 0.0f;
	uint64_t m_FontRevision = 0;
	bool m_Initialized = false;

public:
	template<typename TMeasure>
	float Resolve(const std::vector<const char *> &vLabels, const float FontSize, const uint64_t FontRevision, TMeasure &&Measure)
	{
		return Resolve(vLabels.data(), vLabels.size(), FontSize, FontRevision, std::forward<TMeasure>(Measure));
	}

	template<typename TMeasure>
	float Resolve(const char *const *ppLabels, const size_t Count, const float FontSize, const uint64_t FontRevision, TMeasure &&Measure)
	{
		bool Changed = !m_Initialized || m_FontSize != FontSize || m_FontRevision != FontRevision || m_vLabels.size() != Count;
		for(size_t i = 0; !Changed && i < Count; ++i)
			Changed = m_vLabels[i] != (ppLabels[i] != nullptr ? ppLabels[i] : "");
		if(!Changed)
			return m_Width;
		m_vLabels.resize(Count);
		m_Width = 0.0f;
		for(size_t i = 0; i < Count; ++i)
		{
			m_vLabels[i] = ppLabels[i] != nullptr ? ppLabels[i] : "";
			if(!m_vLabels[i].empty())
				m_Width = std::max(m_Width, Measure(m_vLabels[i].c_str(), FontSize));
		}
		m_FontSize = FontSize;
		m_FontRevision = FontRevision;
		m_Initialized = true;
		return m_Width;
	}
};

struct SSettingsRadioRowLayout
{
	CUIRect m_LabelRect{};
	CUIRect m_ButtonsRect{};
	float m_Height = 0.0f;
	bool m_UseDropdown = false;
};

inline float ResolveSettingsUiScale(const float ContentWidth)
{
	// 在窄窗口保留紧凑基线，并平滑过渡到 800px 处的标准缩放，避免跨阈值跳变。
	const float CompactBaseline = 0.78f + std::clamp((ContentWidth - 680.0f) / 120.0f, 0.0f, 1.0f) * 0.07f;
	return std::clamp(std::max(ContentWidth / 1000.0f, CompactBaseline), 0.78f, 1.0f);
}

inline float ResolveSettingsSmallFontSize(const float UiScale)
{
	return std::clamp(ui_token::font::SMALL * std::max(0.0f, UiScale), 9.0f, ui_token::font::SMALL);
}

inline SSettingsContentMetrics ResolveSettingsContentMetrics(const float ContentWidth)
{
	SSettingsContentMetrics Metrics;
	Metrics.m_UiScale = ResolveSettingsUiScale(ContentWidth);
	Metrics.m_BodySize = std::clamp(ui_token::font::BODY * Metrics.m_UiScale, 10.0f, ui_token::font::BODY);
	// 所有单行控件共用高度；窄视口保留胶囊文字的可读下限和上下内边距。
	Metrics.m_LineHeight = ResolveSettingsControlHeight(Metrics.m_BodySize, Metrics.m_UiScale);
	Metrics.m_SmallSize = ResolveSettingsSmallFontSize(Metrics.m_UiScale);
	Metrics.m_HeadlineSize = std::clamp(ui_token::font::HEADLINE * Metrics.m_UiScale, 12.0f, ui_token::font::HEADLINE);
	Metrics.m_LineSpacing = std::clamp(ui_token::settings::ROW_GAP * Metrics.m_UiScale, 3.0f, ui_token::settings::ROW_GAP);
	Metrics.m_RowStep = Metrics.m_LineHeight + Metrics.m_LineSpacing;
	Metrics.m_InputHeight = Metrics.m_LineHeight;
	Metrics.m_ButtonHeight = Metrics.m_LineHeight;
	Metrics.m_SectionGap = 2.0f * Metrics.m_LineSpacing;
	Metrics.m_BadgeHeight = std::max(12.0f, Metrics.m_LineHeight - Metrics.m_LineSpacing);
	Metrics.m_ListRowHeight = Metrics.m_RowStep;
	Metrics.m_CardGap = ui_token::settings::CARD_GAP * Metrics.m_UiScale;
	const float MinimumControlWidth = 160.0f * Metrics.m_UiScale;
	const float Gap = 8.0f * Metrics.m_UiScale;
	const float PreferredLabelWidth = ContentWidth * 0.32f;
	Metrics.m_LabelWidth = std::clamp(PreferredLabelWidth, 96.0f * Metrics.m_UiScale, std::max(96.0f * Metrics.m_UiScale, ContentWidth - MinimumControlWidth - Gap));
	return Metrics;
}

inline float ResolveSettingsCardLabelWidth(const float CardContentWidth, const SSettingsContentMetrics &Metrics)
{
	const float Scale = Metrics.m_UiScale > 0.0f ? Metrics.m_UiScale : 1.0f;
	const float MinimumControlWidth = 160.0f * Scale;
	const float Gap = 8.0f * Scale;
	const float MinimumLabelWidth = 96.0f * Scale;
	const float PreferredLabelWidth = std::max(0.0f, CardContentWidth) * 0.32f;
	return std::min(std::max(0.0f, CardContentWidth) * 0.45f, std::clamp(PreferredLabelWidth, MinimumLabelWidth, std::max(MinimumLabelWidth, CardContentWidth - MinimumControlWidth - Gap)));
}

// 普通设置行只声明控件用途；同一用途由公共列布局决定宽度和对齐。
enum class ESettingsControlWidth
{
	NUMBER,
	SHORT_CHOICE,
	LONG_CHOICE,
	ACTION,
	FILL,
};

struct SSettingsControlRowLayout
{
	CUIRect m_LabelRect{};
	CUIRect m_ControlRect{};
};

inline float ResolveSettingsControlWidth(float AvailableWidth, const SSettingsContentMetrics &Metrics, ESettingsControlWidth Role, float MeasuredWidth = 0.0f)
{
	const float Available = std::max(0.0f, AvailableWidth);
	const float Scale = Metrics.m_UiScale > 0.0f ? Metrics.m_UiScale : 1.0f;
	if(Role == ESettingsControlWidth::NUMBER)
		// 数值框沿用既有 26%、74–128 范围；单位宽度计入下限。
		return std::min(Available, std::clamp(Available * 0.26f, std::min(128.0f, 74.0f + std::max(0.0f, MeasuredWidth)), 128.0f));
	if(Role == ESettingsControlWidth::SHORT_CHOICE)
		return std::min(Available, std::max(160.0f * Scale, MeasuredWidth));
	if(Role == ESettingsControlWidth::ACTION)
		return std::min(Available, std::max(72.0f * Scale, MeasuredWidth));
	return Available;
}

inline SSettingsControlRowLayout ResolveSettingsControlRowLayout(const CUIRect &Row, const SSettingsContentMetrics &Metrics, ESettingsControlWidth Role = ESettingsControlWidth::FILL, float MeasuredWidth = 0.0f)
{
	SSettingsControlRowLayout Layout;
	const float Width = std::max(0.0f, Row.w);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Width, Metrics);
	const float Gap = std::min(std::max(0.0f, Metrics.m_LineSpacing), std::max(0.0f, Width - LabelWidth));
	const float ControlX = Row.x + LabelWidth + Gap;
	const float Available = std::max(0.0f, Row.x + Width - ControlX);
	const float ControlWidth = ResolveSettingsControlWidth(Available, Metrics, Role, MeasuredWidth);
	Layout.m_LabelRect = {Row.x, Row.y, LabelWidth, Row.h};
	Layout.m_ControlRect = {ControlX, Row.y, ControlWidth, Row.h};
	return Layout;
}

inline void SplitSettingsControlRow(const CUIRect &Row, const SSettingsContentMetrics &Metrics, CUIRect *pLabel, CUIRect *pControl, ESettingsControlWidth Role = ESettingsControlWidth::FILL, float MeasuredWidth = 0.0f)
{
	const auto Layout = ResolveSettingsControlRowLayout(Row, Metrics, Role, MeasuredWidth);
	if(pLabel != nullptr)
		*pLabel = Layout.m_LabelRect;
	if(pControl != nullptr)
		*pControl = Layout.m_ControlRect;
}

struct SSettingsChoiceLayout
{
	static constexpr int MAX_SEGMENTS = 16;
	std::array<CUIRect, MAX_SEGMENTS> m_aSlots{};
	int m_SegmentCount = 0;
	bool m_UseDropdown = false;
};

// 主控件填充余宽，同行短选项最多占一半，窄行不挤出父区域。
inline void SplitSettingsPairedControls(const CUIRect &Row, const SSettingsContentMetrics &Metrics, CUIRect *pPrimary, CUIRect *pSecondary)
{
	const float Available = std::max(0.0f, Row.w);
	const float Gap = std::min(std::max(0.0f, Metrics.m_LineSpacing), Available * 0.1f);
	const float SecondaryWidth = ResolveSettingsControlWidth((Available - Gap) * 0.5f, Metrics, ESettingsControlWidth::SHORT_CHOICE);
	const CUIRect Primary{Row.x, Row.y, Available - Gap - SecondaryWidth, Row.h};
	const CUIRect Secondary{Row.x + Primary.w + Gap, Row.y, SecondaryWidth, Row.h};
	if(pPrimary != nullptr)
		*pPrimary = Primary;
	if(pSecondary != nullptr)
		*pSecondary = Secondary;
}

struct SSettingsActionGroupLayout
{
	std::array<CUIRect, SSettingsChoiceLayout::MAX_SEGMENTS> m_aButtons{};
	int m_Count = 0;
};

enum class ESettingsActionGroupWidth
{
	FILL,
	CONTENT
};

// 纯按钮行默认等分填满；混合行显式按内容取宽。窄屏保留独立按钮，不新增行高。
inline SSettingsActionGroupLayout ResolveSettingsActionGroupLayout(const CUIRect &Row, int Count, const SSettingsContentMetrics &Metrics, float MeasuredWidth, ESettingsActionGroupWidth WidthPolicy = ESettingsActionGroupWidth::FILL)
{
	SSettingsActionGroupLayout Layout;
	if(Count <= 0 || Count > (int)Layout.m_aButtons.size())
		return Layout;
	Layout.m_Count = Count;
	const float Available = std::max(0.0f, Row.w);
	const float Gap = Count > 1 ? std::min(std::max(0.0f, Metrics.m_LineSpacing), Available / (2.0f * Count - 1.0f)) : 0.0f;
	const float SlotWidth = std::max(0.0f, Available - Gap * (Count - 1)) / Count;
	const float Width = WidthPolicy == ESettingsActionGroupWidth::FILL ? SlotWidth : ResolveSettingsControlWidth(SlotWidth, Metrics, ESettingsControlWidth::ACTION, MeasuredWidth);
	for(int Index = 0; Index < Count; ++Index)
		Layout.m_aButtons[Index] = {Row.x + Index * (Width + Gap), Row.y, Width, std::max(0.0f, Row.h)};
	return Layout;
}

struct SSettingsInputActionsLayout
{
	CUIRect m_Input{};
	SSettingsActionGroupLayout m_Actions;
};

// 输入同行的图标按行高取正方形；窄屏最多占一半，保留输入可用空间。
inline SSettingsInputActionsLayout ResolveSettingsInputActionsLayout(const CUIRect &Controls, int Count, const SSettingsContentMetrics &Metrics, float MeasuredActionWidth = -1.0f)
{
	SSettingsInputActionsLayout Layout;
	Layout.m_Input = Controls;
	Layout.m_Input.w = std::max(0.0f, Controls.w);
	if(Count <= 0 || Count > SSettingsChoiceLayout::MAX_SEGMENTS)
		return Layout;
	const float Gap = std::min(std::max(0.0f, Metrics.m_LineSpacing), Layout.m_Input.w * 0.1f);
	const float PreferredWidth = MeasuredActionWidth >= 0.0f ? MeasuredActionWidth : std::max(0.0f, Controls.h);
	const float ActionWidth = std::min(PreferredWidth * Count + Gap * (Count - 1), std::max(0.0f, Layout.m_Input.w - Gap) * 0.5f);
	Layout.m_Input.w = std::max(0.0f, Layout.m_Input.w - ActionWidth - Gap);
	const CUIRect Actions{Controls.x + Layout.m_Input.w + Gap, Controls.y, ActionWidth, Controls.h};
	Layout.m_Actions = ResolveSettingsActionGroupLayout(Actions, Count, Metrics, PreferredWidth, ESettingsActionGroupWidth::CONTENT);
	return Layout;
}

inline SSettingsChoiceLayout ResolveSettingsChoiceLayout(const CUIRect &Controls, const int Count, const float MeasuredOptionWidth)
{
	SSettingsChoiceLayout Layout;
	if(Count <= 0)
		return Layout;
	const float MinimumWidth = std::max({72.0f, Controls.h * 3.0f, MeasuredOptionWidth});
	Layout.m_UseDropdown = Count > SSettingsChoiceLayout::MAX_SEGMENTS || std::max(0.0f, Controls.w) / Count < MinimumWidth;
	if(!Layout.m_UseDropdown)
	{
		Layout.m_SegmentCount = Count;
		CUIRect Remaining = Controls;
		for(int i = 0; i < Count; ++i)
			Remaining.VSplitLeft(Remaining.w / (Count - i), &Layout.m_aSlots[i], &Remaining);
	}
	return Layout;
}

inline SSettingsConfigRowMetrics ResolveSettingsConfigRowMetrics(const bool Compact, const bool Stacked, const float LineHeight, const float RowSpacing, const float HelpHeight, const float ColorControlHeight, const float VerticalPadding)
{
	SSettingsConfigRowMetrics Metrics;
	Metrics.m_ControlLineHeight = std::max(std::max(0.0f, LineHeight), std::max(0.0f, ColorControlHeight));
	Metrics.m_ControlBlockHeight = Stacked ? std::max(0.0f, LineHeight) + std::max(0.0f, RowSpacing) + Metrics.m_ControlLineHeight : Metrics.m_ControlLineHeight;
	Metrics.m_HelpGap = Compact ? 0.0f : 2.0f;
	Metrics.m_RowHeight = std::max(0.0f, VerticalPadding) * 2.0f + Metrics.m_ControlBlockHeight;
	if(!Compact)
		Metrics.m_RowHeight += Metrics.m_HelpGap + std::max(0.0f, HelpHeight);
	return Metrics;
}

struct SSettingsConfigEntryLayout
{
	CUIRect m_Name;
	CUIRect m_Description;
	CUIRect m_Editor;
	CUIRect m_Reset;
	float m_Height = 0.0f;
	bool m_Stacked = false;
};

// 配置列表共用名称／说明、编辑、重置三列；不足一行时把编辑区移到说明下方。
inline SSettingsConfigEntryLayout ResolveSettingsConfigEntryLayout(const CUIRect &Area, const SSettingsContentMetrics &Metrics, bool Compact, float ResetWidth, float DescriptionHeight)
{
	SSettingsConfigEntryLayout Layout;
	const float Width = std::max(0.0f, Area.w);
	const float Padding = std::min(Metrics.m_LineSpacing, Width * 0.5f);
	const float InnerWidth = Width - Padding * 2.0f;
	const float Gap = std::min(Metrics.m_LineSpacing, InnerWidth * 0.25f);
	const float LineHeight = Metrics.m_LineHeight;
	const float ControlHeight = std::max(Metrics.m_InputHeight, Metrics.m_ButtonHeight);
	const float PreferredResetWidth = std::max(0.0f, ResetWidth);
	const float MinimumEditorWidth = 100.0f * Metrics.m_UiScale;
	const float MinimumNameWidth = 160.0f * Metrics.m_UiScale;
	Layout.m_Stacked = InnerWidth < MinimumNameWidth + MinimumEditorWidth + PreferredResetWidth + Gap * 2.0f;
	const float ControlWidth = Layout.m_Stacked ? InnerWidth : std::min(InnerWidth * 0.45f, 320.0f * Metrics.m_UiScale);
	const float NameWidth = Layout.m_Stacked ? InnerWidth : InnerWidth - ControlWidth - Gap;
	const float HelpHeight = Compact ? 0.0f : std::max(0.0f, DescriptionHeight);
	const float NameHeight = LineHeight + (Compact ? 0.0f : Gap + HelpHeight);
	Layout.m_Height = Padding * 2.0f + (Layout.m_Stacked ? NameHeight + Gap + ControlHeight : std::max(NameHeight, ControlHeight));
	Layout.m_Name = {Area.x + Padding, Area.y + Padding, NameWidth, LineHeight};
	Layout.m_Description = {Layout.m_Name.x, Layout.m_Name.y + LineHeight + Gap, NameWidth, HelpHeight};
	const float ControlX = Layout.m_Stacked ? Layout.m_Name.x : Area.x + Width - Padding - ControlWidth;
	const float ControlY = Layout.m_Stacked ? Area.y + Padding + NameHeight + Gap : Area.y + (Layout.m_Height - ControlHeight) * 0.5f;
	const float ActualResetWidth = std::min(PreferredResetWidth, std::max(0.0f, (ControlWidth - Gap) * 0.4f));
	Layout.m_Editor = {ControlX, ControlY, std::max(0.0f, ControlWidth - Gap - ActualResetWidth), ControlHeight};
	Layout.m_Reset = {Area.x + Width - Padding - ActualResetWidth, ControlY, ActualResetWidth, ControlHeight};
	return Layout;
}

inline float ResolveSettingsGridHeight(const int ItemCount, const int ItemsPerRow, const float RowHeight, const float RowSpacing)
{
	if(ItemCount <= 0 || ItemsPerRow <= 0)
		return 0.0f;
	const int Rows = (ItemCount + ItemsPerRow - 1) / ItemsPerRow;
	return Rows * std::max(0.0f, RowHeight) + std::max(0, Rows - 1) * std::max(0.0f, RowSpacing);
}

inline float ResolveSettingsRowsHeight(const int RowCount, const float RowHeight, const float RowSpacing)
{
	if(RowCount <= 0)
		return 0.0f;
	return RowCount * std::max(0.0f, RowHeight) + std::max(0, RowCount - 1) * std::max(0.0f, RowSpacing);
}

struct SSettingsContentFlowEntry
{
	float m_Height = 0.0f;
	bool m_Visible = true;
};

inline SSettingsContentFlowEntry MakeSettingsContentFlowEntry(const float Height, const bool Visible = true)
{
	return {Height, Visible};
}

// 卡片内容的行间距只由这个行流消费：首行无前间距，后续每一可见行恰好一个标准间距。
// 条件行在测量、预布局和绘制阶段均省略 NextIf 调用即可，不再手算 RowsRemaining。
class CSettingsContentRowFlow
{
	CUIRect &m_View;
	const SSettingsContentMetrics &m_Metrics;
	bool m_HasPreviousRow = false;

public:
	CSettingsContentRowFlow(CUIRect &View, const SSettingsContentMetrics &Metrics) :
		m_View(View),
		m_Metrics(Metrics)
	{
	}

	CUIRect Next(const float Height)
	{
		if(m_HasPreviousRow)
			m_View.HSplitTop(std::max(0.0f, m_Metrics.m_LineSpacing), nullptr, &m_View);
		CUIRect Row;
		m_View.HSplitTop(std::max(0.0f, Height), &Row, &m_View);
		m_HasPreviousRow = true;
		return Row;
	}

	CUIRect NextLine() { return Next(m_Metrics.m_LineHeight); }
	CUIRect NextButton() { return Next(m_Metrics.m_ButtonHeight); }
	CUIRect NextIf(const bool Visible, const float Height) { return Visible ? Next(Height) : CUIRect{}; }
};

inline float ResolveSettingsContentFlowHeight(const SSettingsContentMetrics &Metrics, const std::initializer_list<float> RowHeights)
{
	float Height = 0.0f;
	bool HasPreviousRow = false;
	for(const float RowHeight : RowHeights)
	{
		if(HasPreviousRow)
			Height += std::max(0.0f, Metrics.m_LineSpacing);
		Height += std::max(0.0f, RowHeight);
		HasPreviousRow = true;
	}
	return Height;
}

inline float ResolveSettingsContentFlowHeight(const SSettingsContentMetrics &Metrics, const std::initializer_list<SSettingsContentFlowEntry> Entries)
{
	float Height = 0.0f;
	bool HasPreviousRow = false;
	for(const SSettingsContentFlowEntry &Entry : Entries)
	{
		if(!Entry.m_Visible)
			continue;
		if(HasPreviousRow)
			Height += std::max(0.0f, Metrics.m_LineSpacing);
		Height += std::max(0.0f, Entry.m_Height);
		HasPreviousRow = true;
	}
	return Height;
}

// 一行控件的最小高度与文本自然高度取较大值。文本测量由调用方提供，
// 这样行流和卡片高度可以共享同一条换行规则。
inline float ResolveSettingsAutoRowHeight(const float MinimumHeight, const float ContentHeight)
{
	return std::max(std::max(0.0f, MinimumHeight), std::max(0.0f, ContentHeight));
}

// 说明文字只保留左侧缩进；测量和绘制共用有效宽度，后续控件从实际文本底部开始。
template<typename TMeasureHeight>
inline CUIRect ConsumeSettingsWrappedTextRow(CUIRect &Content, float Indent, float MinimumHeight, float Spacing, TMeasureHeight MeasureHeight)
{
	const float LeftInset = std::clamp(Indent, 0.0f, std::max(0.0f, Content.w) * 0.3f);
	const float TextWidth = std::max(1.0f, Content.w - LeftInset);
	CUIRect Row;
	Content.HSplitTop(ResolveSettingsAutoRowHeight(MinimumHeight, MeasureHeight(TextWidth)), &Row, &Content);
	Row.x += LeftInset;
	Row.w = TextWidth;
	Content.HSplitTop(std::max(0.0f, Spacing), nullptr, &Content);
	return Row;
}

// 双列内容各自消费自己的矩形后，把较长一列提交回父内容区。
// 父区的底部保持不变，这样后续行和自动测量都能看到最长列的真实高度。
inline void CommitSettingsColumnContentFlow(CUIRect &Content, const CUIRect &LeftColumn, const CUIRect &RightColumn)
{
	const float ContentBottom = Content.y + Content.h;
	Content.y = std::max(LeftColumn.y, RightColumn.y);
	Content.h = std::max(0.0f, ContentBottom - Content.y);
}

inline int ResolveSettingsSelectionWithCustomFallback(const int MatchedIndex, const int SupportedCount)
{
	return MatchedIndex >= 0 && MatchedIndex < SupportedCount ? MatchedIndex : std::max(0, SupportedCount);
}

// 显示策略同时供卡片测高和渲染消费，单卡或枚举失败不显示无可选项的控件。
struct SSettingsGpuControl
{
	bool m_ShowSelector = false;
	bool m_ShowSystemSettings = false;

	int RowCount() const { return (m_ShowSelector ? 1 : 0) + (m_ShowSystemSettings ? 1 : 0); }
};

inline SSettingsGpuControl ResolveSettingsGpuControl(size_t AdapterCount, bool CanSelect, bool HasSystemSettings)
{
	if(AdapterCount <= 1)
		return {};
	return {CanSelect, HasSystemSettings};
}

struct SSettingsGraphicsRuntimeInfo
{
	std::string m_Backend;
	std::string m_Driver;
};

// 后端已有运行时字符串包含 API 与驱动两部分；只拆分已知格式，不猜测缺失的驱动版本。
inline SSettingsGraphicsRuntimeInfo ResolveSettingsGraphicsRuntimeInfo(const char *pVersion, const char *pDetectedBackend, int Major, int Minor, int Patch)
{
	SSettingsGraphicsRuntimeInfo Info;
	const std::string Version = pVersion != nullptr ? pVersion : "";
	const size_t Driver = Version.find(" (driver: ");
	if(Driver != std::string::npos)
	{
		Info.m_Backend = Version.substr(0, Driver);
		const size_t End = Version.find(')', Driver + 10);
		Info.m_Driver = Version.substr(Driver + 10, End == std::string::npos ? std::string::npos : End - Driver - 10);
	}
	else if(Major > 0 && pDetectedBackend != nullptr && pDetectedBackend[0] != '\0')
	{
		Info.m_Backend = std::string(pDetectedBackend) + " " + std::to_string(Major) + "." + std::to_string(Minor) + "." + std::to_string(Patch);
		// OpenGL / GLES 的驱动描述位于 API 版本之后，ES 可带厂商配置前缀。
		const size_t ApiStart = Version.find_first_of("0123456789");
		if(ApiStart != std::string::npos)
		{
			const size_t ApiEnd = Version.find(' ', ApiStart);
			if(ApiEnd != std::string::npos)
			{
				const size_t Description = Version.find_first_not_of(' ', ApiEnd);
				if(Description != std::string::npos)
					Info.m_Driver = Version.substr(Description);
			}
		}
	}
	else
		Info.m_Backend = Version;
	return Info;
}

// 列表 viewport 只允许显示完整行。列表控件的行距由调用方显式消费，
// 不能把卡片剩余高度直接传进去，否则最后会露出半行内容。
inline float ResolveSettingsListViewportHeight(const int VisibleRows, const float RowHeight, const float RowSpacing)
{
	return ResolveSettingsRowsHeight(std::clamp(VisibleRows, 0, 8), RowHeight, RowSpacing);
}

struct SSettingsListCardGeometry
{
	int m_VisibleRows = 0;
	float m_ListViewportHeight = 0.0f;
	float m_ContentHeight = 0.0f;
};

inline SSettingsListCardGeometry ResolveSettingsGeneralLanguageListGeometry(const int ItemCount, const SSettingsContentMetrics &Metrics)
{
	SSettingsListCardGeometry Geometry;
	Geometry.m_VisibleRows = std::clamp(ItemCount, 1, 8);
	Geometry.m_ListViewportHeight = ResolveSettingsListViewportHeight(Geometry.m_VisibleRows, Metrics.m_ListRowHeight, 0.0f);
	Geometry.m_ContentHeight = Geometry.m_ListViewportHeight;
	return Geometry;
}

inline SSettingsListCardGeometry ResolveSettingsGeneralThemeListGeometry(const int ItemCount, const SSettingsContentMetrics &Metrics)
{
	SSettingsListCardGeometry Geometry = ResolveSettingsGeneralLanguageListGeometry(ItemCount, Metrics);
	Geometry.m_ContentHeight += Metrics.m_LineHeight + Metrics.m_LineSpacing;
	return Geometry;
}

inline SSettingsListCardGeometry ResolveSettingsGraphicsModesGeometry(const int ModeCount, const SSettingsContentMetrics &Metrics)
{
	SSettingsListCardGeometry Geometry = ResolveSettingsGeneralLanguageListGeometry(ModeCount, Metrics);
	// 窗口模式和当前显示模式各消费一行，模式列表使用剩余 viewport。
	Geometry.m_ContentHeight += Metrics.m_RowStep * 2.0f;
	return Geometry;
}

inline int ResolveSettingsSoundAudioPackVisibleRows(const int ItemCount)
{
	return std::clamp(ItemCount, 1, 8);
}

inline SSettingsListCardGeometry ResolveSettingsSoundAudioPackGeometry(const int ItemCount, const SSettingsContentMetrics &Metrics)
{
	SSettingsListCardGeometry Geometry;
	Geometry.m_VisibleRows = ResolveSettingsSoundAudioPackVisibleRows(ItemCount);
	Geometry.m_ListViewportHeight = ResolveSettingsListViewportHeight(Geometry.m_VisibleRows, Metrics.m_ListRowHeight, 0.0f);
	// 外层 Margin(2) 与列表 Margin(6) 各作用于上下两侧。
	constexpr float OuterPadding = 2.0f;
	constexpr float ListPadding = 6.0f;
	Geometry.m_ContentHeight = Metrics.m_LineHeight + Metrics.m_LineSpacing + Geometry.m_ListViewportHeight + 2.0f * (OuterPadding + ListPadding);
	return Geometry;
}

inline float ResolveSettingsGeneralClientContentHeight(const SSettingsContentMetrics &Metrics, const float ThemeContentHeight)
{
	// 跳过主菜单单独成段；更新率和节能开关共享一段；文件按钮共享一段。
	// 三段之间以及主题列表前各消费 SectionGap，各段内部只消费一次普通行距。
	return Metrics.m_LineHeight + Metrics.m_SectionGap +
	       ResolveSettingsRowsHeight(2, Metrics.m_LineHeight, Metrics.m_LineSpacing) + Metrics.m_SectionGap +
	       ResolveSettingsRowsHeight(2, Metrics.m_ButtonHeight, Metrics.m_LineSpacing) + Metrics.m_SectionGap +
	       ThemeContentHeight;
}

inline float ResolveSettingsGeneralGameContentHeight(const SSettingsContentMetrics &Metrics, const bool DynamicCameraExpanded)
{
	// 默认重生武器已移除（远程同名删除），本卡只剩 3 行：动态镜头、拾取换枪、弹药耗尽换枪。
	return ResolveSettingsRowsHeight(3 + (DynamicCameraExpanded ? 1 : 0), Metrics.m_LineHeight, Metrics.m_LineSpacing);
}

inline uint64_t ResolveSettingsGeneralLayoutRevision(const bool RenderOnly, const uint64_t ToggleMask, const float ContentHeight, const int LanguageCount, const int ThemeCount)
{
	uint64_t Revision = 1469598103934665603ULL;
	const auto Mix = [&Revision](const uint64_t Value) {
		Revision ^= Value;
		Revision *= 1099511628211ULL;
	};
	Mix(RenderOnly ? 1u : 0u);
	Mix(ToggleMask);
	Mix((uint64_t)std::max(0, (int)(ContentHeight * 100.0f + 0.5f)));
	Mix((uint64_t)std::max(0, LanguageCount));
	Mix((uint64_t)std::max(0, ThemeCount));
	return Revision;
}

inline float ResolveSettingsProfilesListHeight(const SSettingsContentMetrics &Metrics, const float ContentWidth, const int ProfileCount)
{
	const float ItemWidth = std::max(Metrics.m_ListRowHeight * 6.0f, Metrics.m_LabelWidth + Metrics.m_ButtonHeight * 2.0f);
	const int ItemsPerRow = std::max(1, (int)(std::max(0.0f, ContentWidth) / ItemWidth));
	const int RequiredRows = ProfileCount > 0 ? (ProfileCount + ItemsPerRow - 1) / ItemsPerRow : 0;
	const int VisibleRows = std::clamp(RequiredRows, 1, 3);
	const float ItemHeight = std::max(Metrics.m_ListRowHeight * 3.0f, Metrics.m_ButtonHeight * 3.0f + Metrics.m_LineSpacing * 2.0f);
	const float ListHeight = ProfileCount > 0 ? VisibleRows * ItemHeight : Metrics.m_ListRowHeight * 2.0f;
	return Metrics.m_ButtonHeight + Metrics.m_LineSpacing + ListHeight;
}

inline float ResolveSettingsTeeQueuePresetHeight(const SSettingsContentMetrics &Metrics, const int VisiblePresetRows)
{
	// 操作图标与标题同一行，列表区域仅保留必要内边距。
	const float PresetRowSpacing = Metrics.m_LineSpacing * 0.5f;
	return Metrics.m_LineSpacing * 5.0f + Metrics.m_LineHeight + Metrics.m_ButtonHeight + ResolveSettingsListViewportHeight(VisiblePresetRows, Metrics.m_ListRowHeight, PresetRowSpacing);
}

inline int ResolveSettingsTeeVisiblePresetRows(const int PresetCount)
{
	return std::clamp(PresetCount, 2, 3);
}

inline int ResolveSettingsTeeVisibleQueueRows(const int QueueCount)
{
	return std::clamp(QueueCount, 1, 8);
}

inline uint64_t ResolveSettingsSoundLayoutRevision(const bool RenderOnly, const bool SoundEnabled, const int AudioPackCount)
{
	return ((uint64_t)(RenderOnly ? 1 : 0) << 63) |
	       ((uint64_t)(SoundEnabled ? 1 : 0) << 32) |
	       (uint64_t)ResolveSettingsSoundAudioPackVisibleRows(AudioPackCount);
}

struct SSettingsTeeQueuePanelGeometry
{
	int m_VisibleQueueRows = 0;
	int m_VisiblePresetRows = 0;
	bool m_StackInterval = false;
	float m_IntervalHeight = 0.0f;
	float m_QueueListViewportHeight = 0.0f;
	float m_QueueListSurfaceHeight = 0.0f;
	float m_QueuePresetHeight = 0.0f;
	float m_ContentHeight = 0.0f;
};

inline SSettingsTeeQueuePanelGeometry ResolveSettingsTeeQueuePanelGeometry(const SSettingsContentMetrics &Metrics, const int QueueCount, const int PresetCount, float ContentWidth = 0.0f)
{
	SSettingsTeeQueuePanelGeometry Geometry;
	Geometry.m_VisibleQueueRows = ResolveSettingsTeeVisibleQueueRows(QueueCount);
	Geometry.m_VisiblePresetRows = ResolveSettingsTeeVisiblePresetRows(PresetCount);
	Geometry.m_QueueListViewportHeight = ResolveSettingsListViewportHeight(Geometry.m_VisibleQueueRows, Metrics.m_ListRowHeight, 0.0f);
	// 列表 Surface 包含上下内边距、标题与标题到首行的标准间距，viewport 始终只显示完整行。
	Geometry.m_QueueListSurfaceHeight = Metrics.m_LineSpacing * 3.0f + Metrics.m_LineHeight + Geometry.m_QueueListViewportHeight;
	Geometry.m_QueuePresetHeight = ResolveSettingsTeeQueuePresetHeight(Metrics, Geometry.m_VisiblePresetRows);
	Geometry.m_StackInterval = ContentWidth < 250.0f * Metrics.m_UiScale;
	Geometry.m_IntervalHeight = Metrics.m_InputHeight + (Geometry.m_StackInterval ? Metrics.m_LineHeight + Metrics.m_LineSpacing : 0.0f);
	const float RequiredHeight =
		Metrics.m_LineSpacing * 5.0f + Metrics.m_LineHeight * 3.0f + Geometry.m_IntervalHeight +
		Geometry.m_QueueListSurfaceHeight + Geometry.m_QueuePresetHeight;
	Geometry.m_ContentHeight = RequiredHeight;
	return Geometry;
}

inline float ResolveSettingsTeeQueuePanelHeight(const SSettingsContentMetrics &Metrics, const int QueueCount, const int PresetCount, float ContentWidth = 0.0f)
{
	return ResolveSettingsTeeQueuePanelGeometry(Metrics, QueueCount, PresetCount, ContentWidth).m_ContentHeight;
}

struct SSettingsTeeEmoteSliderLayout
{
	CUIRect m_TrackRect{};
	std::array<CUIRect, NUM_EMOTES> m_aSlotRects{};
	float m_TeeSize = 0.0f;
	float m_Height = 0.0f;
};

inline SSettingsTeeEmoteSliderLayout ResolveSettingsTeeEmoteSliderLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics)
{
	SSettingsTeeEmoteSliderLayout Layout;
	const float BaseHeight = std::max(44.0f * Metrics.m_UiScale, Metrics.m_LineHeight * 1.5f);
	Layout.m_Height = BaseHeight;
	Layout.m_TrackRect = {View.x, View.y, View.w, BaseHeight};
	CUIRect Remainder = Layout.m_TrackRect;
	for(int i = 0; i < NUM_EMOTES; ++i)
	{
		Remainder.VSplitLeft(Remainder.w / static_cast<float>(NUM_EMOTES - i), &Layout.m_aSlotRects[i], &Remainder);
	}
	Layout.m_TeeSize = std::clamp(BaseHeight - 10.0f * Metrics.m_UiScale, 26.0f * Metrics.m_UiScale, 34.0f * Metrics.m_UiScale);
	return Layout;
}

inline int ResolveTeeEmoteSliderTargetFromPoint(const CUIRect &TrackRect, float MouseX)
{
	if(TrackRect.w <= 0.0f)
		return 0;
	const float RelX = std::clamp(MouseX - TrackRect.x, 0.0f, TrackRect.w - 0.001f);
	const int Index = static_cast<int>(RelX / (TrackRect.w / static_cast<float>(NUM_EMOTES)));
	return std::clamp(Index, 0, NUM_EMOTES - 1);
}

inline int StepTeeEmoteSlider(int CurrentEmote, int Delta)
{
	const int Current = std::clamp(CurrentEmote, 0, NUM_EMOTES - 1);
	return (Current + Delta % NUM_EMOTES + NUM_EMOTES) % NUM_EMOTES;
}

inline float ResolveQmVisualWeaponAnimationHeight(const SSettingsContentMetrics &Metrics, const bool SwitchEnabled, const bool ReloadEnabled)
{
	// 两个独立开关始终可见；装填概率、共享范围和切枪参数按各自开关展开。
	const int Rows = 2 + (ReloadEnabled ? 1 : 0) + (SwitchEnabled || ReloadEnabled ? 1 : 0) + (SwitchEnabled ? 4 : 0);
	return Rows * Metrics.m_RowStep + Metrics.m_LineSpacing;
}

inline float ResolveQmVisualCollisionHitboxHeight(const SSettingsContentMetrics &Metrics, const bool Enabled)
{
	// 总开关、十一个语义开关、玩家范围、三项颜色和透明度共十七行。
	return (Enabled ? 17.0f : 1.0f) * Metrics.m_RowStep;
}

inline float ResolveSettingsHslaRowsHeight(const SSettingsContentMetrics &Metrics, const bool Alpha)
{
	return ResolveSettingsRowsHeight(Alpha ? 4 : 3, Metrics.m_LineHeight, Metrics.m_LineSpacing);
}

struct SSettingsTeeCustomColorsLayout
{
	CUIRect m_BodyGroup{};
	CUIRect m_BodyTitle{};
	CUIRect m_BodyControls{};
	CUIRect m_FeetGroup{};
	CUIRect m_FeetTitle{};
	CUIRect m_FeetControls{};
	float m_Height = 0.0f;
};

inline SSettingsTeeCustomColorsLayout ResolveSettingsTeeCustomColorsLayout(const CUIRect &View, const bool Enabled, const SSettingsContentMetrics &Metrics)
{
	SSettingsTeeCustomColorsLayout Layout;
	const float Spacing = std::max(0.0f, Metrics.m_LineSpacing);
	Layout.m_Height = Spacing * 2.0f;
	if(!Enabled)
		return Layout;

	const float ControlsHeight = ResolveSettingsHslaRowsHeight(Metrics, false);
	const float GroupHeight = Spacing * 2.0f + Metrics.m_LineHeight + Spacing + ControlsHeight;
	const bool SideBySide = View.w >= 540.0f * Metrics.m_UiScale;
	const float GroupWidth = SideBySide ? (View.w - Metrics.m_SectionGap) * 0.5f : View.w;
	Layout.m_BodyGroup = {View.x, View.y + Spacing, GroupWidth, GroupHeight};
	Layout.m_FeetGroup = SideBySide ?
				     CUIRect{View.x + GroupWidth + Metrics.m_SectionGap, Layout.m_BodyGroup.y, GroupWidth, GroupHeight} :
				     CUIRect{View.x, Layout.m_BodyGroup.y + GroupHeight + Metrics.m_SectionGap, GroupWidth, GroupHeight};
	const auto ResolveGroup = [&](const CUIRect &Group, CUIRect &Title, CUIRect &Controls) {
		CUIRect Inner;
		Group.Margin(Spacing, &Inner);
		Inner.HSplitTop(Metrics.m_LineHeight, &Title, &Inner);
		Inner.HSplitTop(Spacing, nullptr, &Inner);
		Inner.HSplitTop(ControlsHeight, &Controls, nullptr);
	};
	ResolveGroup(Layout.m_BodyGroup, Layout.m_BodyTitle, Layout.m_BodyControls);
	ResolveGroup(Layout.m_FeetGroup, Layout.m_FeetTitle, Layout.m_FeetControls);
	Layout.m_Height = Layout.m_FeetGroup.y + Layout.m_FeetGroup.h - View.y + Spacing;
	return Layout;
}

struct SSettingsColorRowLayout
{
	CUIRect m_RowRect{};
	CUIRect m_LabelRect{};
	CUIRect m_ColorButtonRect{};
	CUIRect m_ResetButtonRect{};
	float m_ConsumedHeight = 0.0f;
};

inline SSettingsColorRowLayout ResolveSettingsColorRowLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics, const bool ReserveCheckboxIndent, const bool TrailingSpacing = true)
{
	SSettingsColorRowLayout Layout;
	const float RowHeight = std::max(0.0f, Metrics.m_ButtonHeight);
	const float ColorButtonWidth = std::min(RowHeight, std::max(0.0f, View.w * 0.20f));
	const float StandardHorizontalGap = std::clamp(ui_token::settings::ROW_GAP * Metrics.m_UiScale, 3.0f, ui_token::settings::ROW_GAP);
	const float Gap = std::min(StandardHorizontalGap, std::max(0.0f, (View.w - ColorButtonWidth) * 0.10f));
	Layout.m_RowRect = {View.x, View.y, View.w, RowHeight};
	Layout.m_ConsumedHeight = RowHeight + (TrailingSpacing ? std::max(0.0f, Metrics.m_LineSpacing) : 0.0f);

	const float MaximumResetWidth = std::max(0.0f, View.w - ColorButtonWidth - Gap * 2.0f);
	const float MinimumResetWidth = std::min(60.0f * Metrics.m_UiScale, MaximumResetWidth);
	const float PreferredResetWidth = 72.0f * Metrics.m_UiScale;
	const float ResetWidth = std::clamp(std::min(PreferredResetWidth, View.w * 0.30f), MinimumResetWidth, MaximumResetWidth);
	CUIRect Remaining = Layout.m_RowRect;
	Remaining.VSplitRight(ResetWidth, &Remaining, &Layout.m_ResetButtonRect);
	Remaining.VSplitRight(Gap, &Remaining, nullptr);
	Remaining.VSplitRight(ColorButtonWidth, &Remaining, &Layout.m_ColorButtonRect);
	Remaining.VSplitRight(Gap, &Layout.m_LabelRect, nullptr);
	if(ReserveCheckboxIndent)
	{
		const float Indent = std::min(Layout.m_LabelRect.w, RowHeight + Gap);
		Layout.m_LabelRect.VSplitLeft(Indent, nullptr, &Layout.m_LabelRect);
	}
	return Layout;
}

inline float ResolveSettingsRadioFontSize(const SSettingsContentMetrics &Metrics)
{
	return ResolveSettingsControlFontSize(Metrics.m_BodySize);
}

inline float ResolveSettingsRadioButtonHeight(const SSettingsContentMetrics &Metrics)
{
	// 胶囊与输入框、下拉及按钮消费同一个尺寸，不能另算一套行高。
	return Metrics.m_ButtonHeight;
}

inline SSettingsRadioRowLayout ResolveSettingsRadioRowLayout(const CUIRect &View, const int OptionCount, const SSettingsContentMetrics &Metrics, const float MeasuredOptionWidth = 0.0f)
{
	SSettingsRadioRowLayout Layout;
	if(OptionCount <= 0 || View.w <= 0.0f)
		return Layout;

	const float ButtonHeight = ResolveSettingsRadioButtonHeight(Metrics);
	// 标签和控件始终同一行。文字放不下时切换共享下拉，不叠行或压缩字号。
	Layout.m_Height = std::max(Metrics.m_LineHeight, ButtonHeight);
	const auto Columns = ResolveSettingsControlRowLayout({View.x, View.y, View.w, Layout.m_Height}, Metrics, ESettingsControlWidth::LONG_CHOICE);
	Layout.m_LabelRect = Columns.m_LabelRect;
	Layout.m_ButtonsRect = Columns.m_ControlRect;
	Layout.m_ButtonsRect.h = ButtonHeight;
	Layout.m_UseDropdown = ResolveSettingsChoiceLayout(Layout.m_ButtonsRect, OptionCount, MeasuredOptionWidth).m_UseDropdown;
	return Layout;
}

// 两级分段选择行（例：昵称显示范围）：一级选项等宽排布，激活的一级选项按子项数加宽，
// 子级菜单就在同一行内、紧跟在激活的一级标签右侧 —— 对应设计稿的「主滑块 + 子级菜单」。
// 行高固定为「标签一行 + 控件一行」：一级选项带不带子级都不改行高，卡片高度因此不随选择跳动。
struct SSettingsNestedRadioRowLayout
{
	CUIRect m_LabelRect{};
	CUIRect m_ContainerRect{};
	float m_Height = 0.0f;
};

inline SSettingsNestedRadioRowLayout ResolveSettingsNestedRadioRowLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics)
{
	SSettingsNestedRadioRowLayout Layout;
	if(View.w <= 0.0f)
		return Layout;

	Layout.m_LabelRect = {View.x, View.y, View.w, Metrics.m_LineHeight};
	const float ButtonHeight = ResolveSettingsRadioButtonHeight(Metrics);
	Layout.m_ContainerRect = {View.x, View.y + Metrics.m_LineHeight + Metrics.m_LineSpacing, View.w, ButtonHeight};
	Layout.m_Height = Metrics.m_LineHeight + Metrics.m_LineSpacing + ButtonHeight;
	return Layout;
}

// 两级分段行的槽位表：一级槽位与子级槽位都不含动画，动画只发生在滑块胶囊上，
// 这样文字宽度稳定、文本缓存键不会每帧变化。
// 激活的一级项带子级时，它的一级槽位整段让给子级菜单（一级标签被替换掉，不再绘制），
// 主滑块因此盖住整段子级区域，次级滑块再在主滑块之上标出当前子项。
struct SSettingsNestedRadioSlots
{
	static constexpr int MAX_SLOTS = 8;
	CUIRect m_aMain[MAX_SLOTS]{};
	CUIRect m_aSub[MAX_SLOTS]{};
	int m_MainCount = 0;
	int m_SubCount = 0;
};

inline SSettingsNestedRadioSlots ResolveSettingsNestedRadioSlots(const CUIRect &Container, const int OptionCount, const int ActiveIndex, const int SubOptionCount, const float Inset)
{
	SSettingsNestedRadioSlots Slots;
	if(OptionCount <= 0 || Container.w <= 0.0f)
		return Slots;

	CUIRect Inner = Container;
	Inner.Margin(Inset, &Inner);
	if(Inner.w <= 0.0f || Inner.h <= 0.0f)
		return Slots;

	const int MainCount = std::clamp(OptionCount, 0, SSettingsNestedRadioSlots::MAX_SLOTS);
	const bool SubInline = ActiveIndex >= 0 && ActiveIndex < MainCount;
	const int SubCount = SubInline ? std::clamp(SubOptionCount, 0, SSettingsNestedRadioSlots::MAX_SLOTS - 1) : 0;
	// 激活项不再占一个单位（标签被子级替换），所以单位数是「其它一级项 + 子项」。
	const int UnitCount = std::max(1, MainCount + (SubCount > 0 ? SubCount - 1 : 0));
	const float UnitWidth = Inner.w / (float)UnitCount;
	CUIRect Remainder = Inner;
	for(int i = 0; i < MainCount; ++i)
	{
		const bool Expanded = i == ActiveIndex && SubCount > 0;
		const int Span = Expanded ? SubCount : 1;
		CUIRect Slot;
		Remainder.VSplitLeft(UnitWidth * Span, &Slot, &Remainder);
		Slots.m_aMain[i] = Slot;
		if(!Expanded)
			continue;
		for(int s = 0; s < SubCount; ++s)
		{
			CUIRect SubSlot;
			Slot.VSplitLeft(Slot.w / (float)(SubCount - s), &SubSlot, &Slot);
			Slots.m_aSub[s] = SubSlot;
		}
		Slots.m_SubCount = SubCount;
	}
	Slots.m_MainCount = MainCount;
	return Slots;
}

inline float ResolveSettingsControllerAxisPickerHeight(const int AxisCount, const int MaxAxisCount, const float RowHeight, const float RowSpacing)
{
	return (std::clamp(AxisCount, 0, std::max(0, MaxAxisCount)) + 1) * (std::max(0.0f, RowHeight) + std::max(0.0f, RowSpacing));
}

inline float ResolveSettingsControllerContentHeight(const float ContentWidth, const bool Enabled, const bool HasJoystick, const bool AbsoluteMode, const int AxisCount, const int MaxAxisCount, const float RowHeight, const float RowSpacing)
{
	float Height = std::max(0.0f, RowHeight) + std::max(0.0f, RowSpacing);
	if(!Enabled)
		return Height;

	Height += std::max(0.0f, RowHeight) + std::max(0.0f, RowSpacing);
	if(!HasJoystick)
		return Height + std::max(0.0f, RowSpacing);

	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(ContentWidth);
	const SSettingsRadioRowLayout Radio = ResolveSettingsRadioRowLayout({0.0f, 0.0f, std::max(0.0f, ContentWidth), RowHeight * 2.0f + RowSpacing}, 2, Metrics);
	Height += Radio.m_Height;
	if(!AbsoluteMode)
		Height += RowHeight + RowSpacing;
	Height += 2.0f * (RowHeight + RowSpacing);
	Height += RowSpacing + ResolveSettingsControllerAxisPickerHeight(AxisCount, MaxAxisCount, RowHeight, RowSpacing);
	return Height;
}

inline int ResolveSettingsStatusCodeRows(const int ItemCount, const float ContentWidth)
{
	if(ItemCount <= 0)
		return 0;
	return ContentWidth > 360.0f ? (ItemCount + 1) / 2 : ItemCount;
}

inline float ResolveSettingsInlineRowMinimumWidth(const float FixedControlsWidth, const float Gap, const int GapCount)
{
	return std::max(0.0f, FixedControlsWidth) + std::max(0.0f, Gap) * std::max(0, GapCount);
}

inline float ResolveAppearanceChatMessagesHeight(const SSettingsContentMetrics &Metrics)
{
	constexpr int MessageGradientCount = 6;
	const float ControlHeight = std::max(Metrics.m_LineHeight, Metrics.m_ButtonHeight);
	const float MessageGradientHeight = 2.0f * ControlHeight + 2.0f * Metrics.m_LineSpacing;
	const float ColorPickerHeight = ControlHeight + Metrics.m_LineSpacing;
	const float SystemPrefixToggleHeight = Metrics.m_LineHeight + Metrics.m_LineSpacing;
	const float GeometryEditorHeight = 8.0f * (ControlHeight + Metrics.m_LineSpacing);
	return MessageGradientCount * MessageGradientHeight + SystemPrefixToggleHeight + ColorPickerHeight + GeometryEditorHeight;
}

inline float ResolveQmHudCoordsHeight(const SSettingsContentMetrics &Metrics)
{
	// 六个复选框和一个数值输入保留行距；末尾颜色行不再追加卡片底部空白。
	return 7.0f * Metrics.m_RowStep + Metrics.m_ButtonHeight;
}

inline float ResolveQmHudNotificationsHeight(const SSettingsContentMetrics &Metrics, const bool Advanced, const bool CategoryFilters)
{
	// 基础区域：两个开关、两个数值输入和高级选项开关。
	float Height = 5.0f * Metrics.m_RowStep;
	if(!Advanced)
		return Height - Metrics.m_LineSpacing;

	// 高级区域固定消费十行；分类过滤启用后再显示四个分类开关。
	Height += (10.0f + (CategoryFilters ? 4.0f : 0.0f)) * Metrics.m_RowStep;
	// 说明文字使用 Small 语义，并保留与其他行相同的安全间距。
	return Height + Metrics.m_SmallSize + Metrics.m_LineSpacing;
}

inline float ResolveQmHudPlayerStatsHeight(const SSettingsContentMetrics &Metrics, const bool MapProgress, const bool EmbeddedProgress)
{
	if(!MapProgress)
		return 3.0f * Metrics.m_RowStep - Metrics.m_LineSpacing;
	return (EmbeddedProgress ? 5.0f : 10.0f) * Metrics.m_RowStep - Metrics.m_LineSpacing;
}

inline float ResolveQmHudInputOverlayHeight(const SSettingsContentMetrics &Metrics, const bool Enabled)
{
	if(!Enabled)
		return Metrics.m_LineHeight;
	// 八个基本控件，加字号、颜色、标签和外观恢复；实际页面优先使用内容探针。
	return 12.0f * Metrics.m_RowStep;
}

inline float ResolveQmHudDummyMiniViewHeight(const SSettingsContentMetrics &Metrics, const bool Expanded)
{
	const float PreviewGap = Metrics.m_LineHeight * 0.8f + Metrics.m_LineSpacing;
	return Expanded ? 4.0f * Metrics.m_RowStep + PreviewGap : Metrics.m_RowStep;
}

inline float ResolveQmHudDynamicIslandHeight(const SSettingsContentMetrics &Metrics, const bool OriginalStyle, const bool SwitchCountdownEnabled, const float ContentWidth)
{
	// 常驻行：原始样式、显示队伍、钩子倒计时和开关倒计时。
	float Height = 4.0f * Metrics.m_RowStep;
	if(!OriginalStyle)
	{
		const CUIRect ColorRowView{0.0f, 0.0f, std::max(0.0f, ContentWidth), 0.0f};
		Height += ResolveSettingsColorRowLayout(ColorRowView, Metrics, false).m_ConsumedHeight + Metrics.m_RowStep;
	}
	// 开关倒计时启用时增加跟随 Tee / 灵动岛两个位置开关，不再保留位置标题行。
	Height += (SwitchCountdownEnabled ? 2.0f : 0.0f) * Metrics.m_RowStep;
	return Height;
}

inline float ResolveQmHudVoiceHeight(const SSettingsContentMetrics &Metrics, const bool Enabled, const bool Advanced, const bool ShowStatus, const int NoiseSuppressMode, const bool VadEnabled, const bool StereoEnabled)
{
	if(!Enabled)
		return Metrics.m_LineHeight;

	// 常规区域：启用、房间密码、静音、麦克风音量、VAD 和高级选项。
	float Height = 6.0f * Metrics.m_RowStep;
	if(!Advanced)
		return Height - Metrics.m_LineSpacing;

	// 高级固定区域：状态开关、服务器/设备/编码/降噪、AGC、播放、立体声、半径和房间范围。
	Height += 11.0f * Metrics.m_RowStep + Metrics.m_LineSpacing * 1.15f;
	// 可选 WebSocket endpoint 与 UDP 地址并列显示，始终占一行避免配置切换时卡片跳动。
	Height += Metrics.m_RowStep;
	if(NoiseSuppressMode == 1)
		Height += Metrics.m_RowStep;
	if(VadEnabled)
		Height += 2.0f * Metrics.m_RowStep;
	if(StereoEnabled)
		Height += Metrics.m_RowStep;
	if(ShowStatus)
	{
		// 标题、提示、十个稳定状态行以及状态区尾间距。
		Height += Metrics.m_LineHeight * 1.46f + Metrics.m_LineSpacing * 0.75f;
		Height += 10.0f * (Metrics.m_LineHeight + Metrics.m_LineSpacing * 0.75f);
		Height += Metrics.m_LineSpacing * 0.5f;
	}
	return Height - Metrics.m_LineSpacing;
}

inline float ResolveQmHudBackground3DHeight(const SSettingsContentMetrics &Metrics, const float ContentWidth, const bool Enabled, const bool CustomColor, const bool Glow, const bool Trail, const bool Pulse, const bool Twinkle)
{
	if(!Enabled)
		return Metrics.m_LineHeight;
	const SSettingsRadioRowLayout Radio = ResolveSettingsRadioRowLayout({0.0f, 0.0f, ContentWidth, Metrics.m_LineHeight * 2.0f + Metrics.m_LineSpacing}, 2, Metrics);
	float Height = 18.0f * Metrics.m_RowStep + Radio.m_Height + Metrics.m_LineSpacing;
	Height += CustomColor ? Metrics.m_RowStep : 0.0f;
	Height += Glow ? 2.0f * Metrics.m_RowStep : 0.0f;
	Height += Trail ? 2.0f * Metrics.m_RowStep : 0.0f;
	Height += Pulse ? 2.0f * Metrics.m_RowStep : 0.0f;
	Height += Twinkle ? Metrics.m_RowStep : 0.0f;
	return Height - Metrics.m_LineSpacing;
}

inline uint64_t ResolveQmHudVoiceRevision(const bool Enabled, const bool Advanced, const bool ShowStatus, const int NoiseSuppressMode, const bool VadEnabled, const bool StereoEnabled)
{
	return (Enabled ? 1u : 0u) |
	       (Advanced ? 1u << 1 : 0u) |
	       (ShowStatus ? 1u << 2 : 0u) |
	       ((uint64_t)std::clamp(NoiseSuppressMode, 0, 3) << 3) |
	       (VadEnabled ? 1u << 5 : 0u) |
	       (StereoEnabled ? 1u << 6 : 0u);
}

inline uint64_t ResolveQmHudBackground3DRevision(const bool Enabled, const bool CustomColor, const bool Glow, const bool Trail, const bool Pulse, const bool Twinkle)
{
	return (Enabled ? 1u : 0u) |
	       (CustomColor ? 1u << 1 : 0u) |
	       (Glow ? 1u << 2 : 0u) |
	       (Trail ? 1u << 3 : 0u) |
	       (Pulse ? 1u << 4 : 0u) |
	       (Twinkle ? 1u << 5 : 0u);
}

inline float ResolveAppearanceLaserColorsHeight(const SSettingsContentMetrics &Metrics)
{
	constexpr int ColorPickerCount = 10;
	constexpr float EntitySectionGap = 10.0f;
	const float HeadingHeight = Metrics.m_LineHeight + 2.0f * Metrics.m_LineSpacing;
	const float ColorPickerHeight = std::max(Metrics.m_LineHeight, Metrics.m_ButtonHeight) + Metrics.m_LineSpacing;
	return 2.0f * HeadingHeight + 8.0f * Metrics.m_LineSpacing + ColorPickerCount * ColorPickerHeight + EntitySectionGap + 2.0f * Metrics.m_LineHeight;
}

inline float ResolveAppearanceLaserEnhancedHeight(const SSettingsContentMetrics &Metrics, const bool Enhanced)
{
	const int RowCount = Enhanced ? 7 : 5;
	const int SpacingCount = Enhanced ? 6 : 4;
	return RowCount * Metrics.m_LineHeight + SpacingCount * Metrics.m_LineSpacing;
}

inline float ResolveDDNetDemoRows(const bool ReplaysEnabled, const bool RaceGhostEnabled, const bool SaveGhostEnabled)
{
	return 3.0f + (ReplaysEnabled ? 2.0f : 0.0f) + (RaceGhostEnabled ? 3.0f + (SaveGhostEnabled ? 1.0f : 0.0f) : 0.0f);
}

inline float ResolveDDNetGameplayRows(const bool TextEntitiesEnabled, const bool AntiPingEnabled)
{
	(void)TextEntitiesEnabled;
	return 9.0f + (AntiPingEnabled ? 3.0f : 0.0f);
}

inline SSettingsSubTabLayoutFrame ResolveSettingsSubTabLayout(const CUIRect &MainView, const float UiScale = 1.0f)
{
	const float Scale = UiScale > 0.0f ? UiScale : 1.0f;
	const float TabHeight = std::min(ui_token::settings::SUB_TAB_HEIGHT * Scale, std::max(0.0f, MainView.h));
	const float Gap = std::min(ui_token::settings::SUB_TAB_GAP * Scale, std::max(0.0f, MainView.h - TabHeight));
	SSettingsSubTabLayoutFrame Frame;
	Frame.m_TabBarRect = {MainView.x, MainView.y, MainView.w, TabHeight};
	Frame.m_ContentRect = {MainView.x, MainView.y + TabHeight + Gap, MainView.w, std::max(0.0f, MainView.h - TabHeight - Gap)};
	return Frame;
}

inline SSettingsPageLayoutFrame ResolveSettingsPageLayout(const CUIRect &PageRect, const bool HasSubTabs, const float UiScale = 1.0f)
{
	const float Scale = UiScale > 0.0f ? UiScale : 1.0f;
	const float Inset = ui_token::settings::PAGE_INSET * Scale;
	SSettingsPageLayoutFrame Frame{};
	Frame.m_PageRect = PageRect;
	float ContentTop = PageRect.y;
	float ContentHeight = std::max(0.0f, PageRect.h);
	if(HasSubTabs)
	{
		const SSettingsSubTabLayoutFrame SubTab = ResolveSettingsSubTabLayout(PageRect, Scale);
		Frame.m_SubTabRect = SubTab.m_TabBarRect;
		ContentTop = SubTab.m_ContentRect.y;
		ContentHeight = SubTab.m_ContentRect.h;
	}
	Frame.m_ScrollViewport = {
		PageRect.x + Inset,
		ContentTop + Inset,
		std::max(0.0f, PageRect.w - Inset * 2.0f),
		std::max(0.0f, ContentHeight - Inset * 2.0f),
	};
	Frame.m_UnreservedScrollViewport = Frame.m_ScrollViewport;
	Frame.m_ContentViewport = Frame.m_ScrollViewport;
	Frame.m_CardGap = ui_token::settings::CARD_GAP * Scale;
	Frame.m_TwoColumns = Frame.m_ContentViewport.w >= ui_token::settings::TWO_COLUMN_MIN_WIDTH;
	if(Frame.m_TwoColumns)
	{
		const float ColumnWidth = std::max(0.0f, (Frame.m_ContentViewport.w - Frame.m_CardGap) * 0.5f);
		Frame.m_aColumns[0] = {Frame.m_ContentViewport.x, Frame.m_ContentViewport.y, ColumnWidth, Frame.m_ContentViewport.h};
		Frame.m_aColumns[1] = {Frame.m_aColumns[0].x + ColumnWidth + Frame.m_CardGap, Frame.m_ContentViewport.y, ColumnWidth, Frame.m_ContentViewport.h};
	}
	else
		Frame.m_aColumns[0] = Frame.m_ContentViewport;
	return Frame;
}

// 滚动条占用空间后，基于有效 viewport 重新计算内容列，避免卡片落入滚动条轨道。
inline SSettingsPageLayoutFrame ResolveSettingsPageLayoutForScrollViewport(const SSettingsPageLayoutFrame &Layout, const CUIRect &ScrollViewport, const float UiScale = 1.0f)
{
	(void)UiScale;
	SSettingsPageLayoutFrame Frame = Layout;
	Frame.m_ScrollViewport = ScrollViewport;
	Frame.m_ContentViewport = ScrollViewport;
	Frame.m_TwoColumns = Frame.m_ContentViewport.w >= ui_token::settings::TWO_COLUMN_MIN_WIDTH;
	Frame.m_aColumns[0] = {};
	Frame.m_aColumns[1] = {};
	if(Frame.m_TwoColumns)
	{
		const float ColumnWidth = std::max(0.0f, (Frame.m_ContentViewport.w - Frame.m_CardGap) * 0.5f);
		Frame.m_aColumns[0] = {Frame.m_ContentViewport.x, Frame.m_ContentViewport.y, ColumnWidth, Frame.m_ContentViewport.h};
		Frame.m_aColumns[1] = {Frame.m_aColumns[0].x + ColumnWidth + Frame.m_CardGap, Frame.m_ContentViewport.y, ColumnWidth, Frame.m_ContentViewport.h};
	}
	else
		Frame.m_aColumns[0] = Frame.m_ContentViewport;
	return Frame;
}

inline SSettingsShellLayoutFrame ResolveSettingsShellLayout(const CUIRect &AvailableRect, const float BottomReservedHeight = 0.0f)
{
	constexpr float ShellGap = 10.0f;
	constexpr float PanelMargin = 10.0f;
	constexpr float RestartBarHeight = 20.0f;
	SSettingsShellLayoutFrame Frame;
	CUIRect ShellAvailable = AvailableRect;
	ShellAvailable.h = std::max(0.0f, ShellAvailable.h - std::max(0.0f, BottomReservedHeight));
	Frame.m_TabBarWidth = std::clamp(AvailableRect.w * 0.16f, 132.0f, 168.0f);
	const float AvailablePanelWidth = std::max(0.0f, ShellAvailable.w - Frame.m_TabBarWidth - ShellGap);
	const float MaxPanelWidth = ui_token::settings::MAX_CONTENT_WIDTH + PanelMargin * 2.0f;
	const float PanelWidth = std::min(AvailablePanelWidth, MaxPanelWidth);
	const float ShellWidth = PanelWidth + ShellGap + Frame.m_TabBarWidth;
	ShellAvailable.VMargin(std::max(0.0f, (ShellAvailable.w - ShellWidth) * 0.5f), &Frame.m_ShellRect);
	Frame.m_ShellRect.VSplitRight(Frame.m_TabBarWidth, &Frame.m_ContentPanelRect, &Frame.m_TabBarRect);
	Frame.m_ContentPanelRect.VSplitRight(ShellGap, &Frame.m_ContentPanelRect, nullptr);
	Frame.m_ContentRect = Frame.m_ContentPanelRect;
	Frame.m_ContentRect.Margin(PanelMargin, &Frame.m_ContentRect);
	if(BottomReservedHeight >= RestartBarHeight)
		Frame.m_RestartBarRect = {Frame.m_ContentPanelRect.x, AvailableRect.y + AvailableRect.h - RestartBarHeight, Frame.m_ContentPanelRect.w, RestartBarHeight};

	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Frame.m_ContentRect.w);
	const SSettingsPageLayoutFrame Page = ResolveSettingsPageLayout(Frame.m_ContentRect, false, Metrics.m_UiScale);
	Frame.m_UiScale = Metrics.m_UiScale;
	Frame.m_ScrollViewport = Page.m_ScrollViewport;
	Frame.m_ScrollViewport.w = std::max(0.0f, Frame.m_ScrollViewport.w - ui_token::settings::OUTER_SCROLLBAR_SLOT);
	const SSettingsPageLayoutFrame EffectivePage = ResolveSettingsPageLayoutForScrollViewport(Page, Frame.m_ScrollViewport, Metrics.m_UiScale);
	Frame.m_aColumns[0] = EffectivePage.m_aColumns[0];
	Frame.m_aColumns[1] = EffectivePage.m_aColumns[1];
	Frame.m_CardGap = Page.m_CardGap;
	Frame.m_TwoColumns = EffectivePage.m_TwoColumns;
	return Frame;
}

#endif
