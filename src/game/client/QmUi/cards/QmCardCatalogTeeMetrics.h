#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEEMETRICS_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEEMETRICS_H

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/SettingsPageLayout.h>

#include <array>

inline float ResolveSettingsTeeGlowHeight(const SSettingsContentMetrics &Metrics, bool CustomColor)
{
	return ResolveSettingsRowsHeight(4, Metrics.m_LineHeight, Metrics.m_LineSpacing) +
	       (CustomColor ? Metrics.m_LineSpacing + std::max(0.0f, Metrics.m_ButtonHeight) : 0.0f);
}

inline CUIRect ResolveSettingsTeeEmoteSliderThumb(const SSettingsTeeEmoteSliderLayout &Layout, int Emote, float UiScale, CUiV2AnimationRuntime *pAnimRuntime, uint64_t NodeKey)
{
	CUIRect Thumb;
	Layout.m_aSlotRects[std::clamp(Emote, 0, NUM_EMOTES - 1)].Margin(2.5f * UiScale, &Thumb);
	if(pAnimRuntime != nullptr)
	{
		// 纵向位置与尺寸直接跟随轨道，切换动画只作用于轨道内的水平偏移。
		const float OffsetX = Thumb.x - Layout.m_TrackRect.x;
		Thumb.x = Layout.m_TrackRect.x + ResolveUiAnimSpringValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, OffsetX, ui_token::motion::NAVIGATION_SPRING, 2);
	}
	return Thumb;
}

struct SSettingsTeeIdentityFieldsLayout
{
	CUIRect m_NameLabel;
	CUIRect m_NameInput;
	CUIRect m_ClanLabel;
	CUIRect m_ClanInput;
	CUIRect m_FlagButton;
	float m_Height = 0.0f;
};

inline SSettingsTeeIdentityFieldsLayout ResolveSettingsTeeIdentityFieldsLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics, bool Stacked)
{
	SSettingsTeeIdentityFieldsLayout Layout;
	const float Gap = Metrics.m_LineSpacing;
	CUIRect NameRow{View.x, View.y, View.w, Metrics.m_InputHeight};
	CUIRect ClanRow = NameRow;
	Layout.m_Height = NameRow.h;
	if(Stacked)
	{
		ClanRow.y += NameRow.h + Gap;
		Layout.m_Height += ClanRow.h + Gap;
	}
	else
	{
		NameRow.VSplitMid(&NameRow, &ClanRow, Gap * 2.0f);
	}
	NameRow.VSplitLeft(std::min(NameRow.w * 0.32f, 52.0f * Metrics.m_UiScale), &Layout.m_NameLabel, &Layout.m_NameInput);
	ClanRow.VSplitLeft(std::min(ClanRow.w * 0.26f, 45.0f * Metrics.m_UiScale), &Layout.m_ClanLabel, &ClanRow);
	ClanRow.VSplitRight(std::min(ClanRow.w * 0.36f, Metrics.m_ButtonHeight * 2.0f), &Layout.m_ClanInput, &Layout.m_FlagButton);
	Layout.m_ClanInput.VSplitRight(std::min(Gap, Layout.m_ClanInput.w), &Layout.m_ClanInput, nullptr);
	return Layout;
}

struct SSettingsTeeEditorLayout
{
	std::array<CUIRect, 2> m_aPreviews;
	CUIRect m_TargetLabel;
	CUIRect m_Identity;
	CUIRect m_SkinLabel;
	CUIRect m_SkinInput;
	CUIRect m_RandomSkin;
	std::array<CUIRect, 3> m_aSkinTransfers;
	CUIRect m_Eyes;
	CUIRect m_CustomColors;
	CUIRect m_RandomColors;
	SSettingsTeeCustomColorsLayout m_Colors;
	bool m_StackIdentity = false;
	float m_Height = 0.0f;
};

inline SSettingsTeeEditorLayout ResolveSettingsTeeEditorLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics, bool CustomColors)
{
	SSettingsTeeEditorLayout Layout;
	const float Gap = Metrics.m_LineSpacing;
	const bool Wide = View.w >= 700.0f * Metrics.m_UiScale;
	CUIRect Editor = View;
	CUIRect Previews;
	if(Wide)
	{
		View.VSplitLeft(std::min(View.w * 0.4f, 340.0f * Metrics.m_UiScale), &Previews, &Editor);
		Editor.VSplitLeft(Metrics.m_SectionGap, nullptr, &Editor);
	}
	float Y = View.y;
	const auto NextRow = [&](float Height) {
		const CUIRect Row{Editor.x, Y, std::max(0.0f, Editor.w), Height};
		Y += Height + Gap;
		return Row;
	};
	if(!Wide)
		Previews = NextRow(112.0f * Metrics.m_UiScale);
	Layout.m_TargetLabel = NextRow(Metrics.m_LineHeight);
	Layout.m_StackIdentity = Editor.w < 460.0f * Metrics.m_UiScale;
	const float IdentityHeight = ResolveSettingsTeeIdentityFieldsLayout({Editor.x, Y, Editor.w, 0.0f}, Metrics, Layout.m_StackIdentity).m_Height;
	Layout.m_Identity = NextRow(IdentityHeight);
	CUIRect SkinRow = NextRow(Metrics.m_InputHeight);
	SkinRow.VSplitLeft(std::min(SkinRow.w * 0.28f, 84.0f * Metrics.m_UiScale), &Layout.m_SkinLabel, &SkinRow);
	SkinRow.VSplitRight(Metrics.m_ButtonHeight, &Layout.m_SkinInput, &Layout.m_RandomSkin);
	Layout.m_SkinInput.VSplitRight(Gap, &Layout.m_SkinInput, nullptr);
	Layout.m_Eyes = NextRow(ResolveSettingsTeeEmoteSliderLayout({}, Metrics).m_Height);
	CUIRect ColorsRow = NextRow(Metrics.m_LineHeight);
	ColorsRow.VSplitRight(Metrics.m_ButtonHeight, &Layout.m_CustomColors, &Layout.m_RandomColors);
	Layout.m_CustomColors.VSplitRight(Gap, &Layout.m_CustomColors, nullptr);
	if(Wide)
	{
		Previews.h = std::max(112.0f * Metrics.m_UiScale, Y - View.y - Gap);
		Y = std::max(Y, Previews.y + Previews.h + Gap);
	}
	// 两张预览中间竖排三个方向明确的按钮，每个操作都可直接点击。
	const float ButtonSize = Metrics.m_ButtonHeight;
	Previews.VSplitMid(&Layout.m_aPreviews[0], &Layout.m_aPreviews[1], ButtonSize + Gap * 2.0f);
	const float TransfersHeight = ButtonSize * 3.0f + Gap * 2.0f;
	for(size_t Index = 0; Index < Layout.m_aSkinTransfers.size(); ++Index)
		Layout.m_aSkinTransfers[Index] = {Previews.x + (Previews.w - ButtonSize) * 0.5f,
			Previews.y + (Previews.h - TransfersHeight) * 0.5f + Index * (ButtonSize + Gap), ButtonSize, ButtonSize};
	if(CustomColors)
	{
		Layout.m_Colors = ResolveSettingsTeeCustomColorsLayout({View.x, Y, View.w, 0.0f}, true, Metrics);
		Y += Layout.m_Colors.m_Height;
	}
	Layout.m_Height = Y - View.y;
	return Layout;
}

struct SSettingsTeeOptionsLayout
{
	CUIRect m_Downloads;
	CUIRect m_Prefix;
	float m_Height = 0.0f;
};

inline SSettingsTeeOptionsLayout ResolveSettingsTeeOptionsLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics)
{
	SSettingsTeeOptionsLayout Layout;
	const float DownloadsHeight = ResolveSettingsRowsHeight(4, Metrics.m_LineHeight, Metrics.m_LineSpacing);
	// 前缀区只有一行（标签 + 输入框 + 预设胶囊同排）。这里曾按改造前的三行布局
	// 预留 3 行高度，改成单行后测量没跟着收，卡片底部凭空多出两行的空白——
	// 「最后一行到卡片边框的边距」因此比别的卡大得多。
	const float PrefixHeight = Metrics.m_InputHeight;
	if(View.w >= 540.0f * Metrics.m_UiScale)
	{
		View.VSplitMid(&Layout.m_Downloads, &Layout.m_Prefix, Metrics.m_SectionGap);
		Layout.m_Downloads.h = DownloadsHeight;
		Layout.m_Prefix.h = PrefixHeight;
		Layout.m_Height = std::max(DownloadsHeight, PrefixHeight);
	}
	else
	{
		Layout.m_Downloads = {View.x, View.y, View.w, DownloadsHeight};
		Layout.m_Prefix = {View.x, View.y + DownloadsHeight + Metrics.m_SectionGap, View.w, PrefixHeight};
		Layout.m_Height = DownloadsHeight + Metrics.m_SectionGap + PrefixHeight;
	}
	return Layout;
}

struct SSettingsTeeToolbarLayout
{
	CUIRect m_Search;
	std::array<CUIRect, 4> m_aTools;
	CUIRect m_Collection;
	CUIRect m_Count;
	CUIRect m_FilterLabel;
	CUIRect m_Filter;
	CUIRect m_SortLabel;
	CUIRect m_Sort;
	float m_Height = 0.0f;
};

inline SSettingsTeeToolbarLayout ResolveSettingsTeeToolbarLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics)
{
	SSettingsTeeToolbarLayout Layout;
	const float Gap = Metrics.m_LineSpacing;
	const float Button = Metrics.m_ButtonHeight;
	const float ToolsWidth = Button * 4.0f + Gap * 3.0f;
	CUIRect SearchRow{View.x, View.y, View.w, Metrics.m_InputHeight};
	CUIRect Tools = SearchRow;
	Layout.m_Height = SearchRow.h + Gap;
	if(View.w < ToolsWidth + Gap + 150.0f * Metrics.m_UiScale)
	{
		Layout.m_Search = SearchRow;
		Tools = {View.x + std::max(0.0f, View.w - ToolsWidth), View.y + Layout.m_Height, ToolsWidth, Button};
		Layout.m_Height += Button + Gap;
	}
	else
	{
		SearchRow.VSplitRight(ToolsWidth, &Layout.m_Search, &Tools);
		Layout.m_Search.VSplitRight(Gap, &Layout.m_Search, nullptr);
	}
	for(CUIRect &Tool : Layout.m_aTools)
	{
		Tools.VSplitLeft(Button, &Tool, &Tools);
		Tools.VSplitLeft(std::min(Gap, Tools.w), nullptr, &Tools);
	}
	const float LabelWidth = std::min(76.0f * Metrics.m_UiScale, std::max(0.0f, View.w * 0.32f));
	const float ChoiceWidth = 152.0f * Metrics.m_UiScale;
	const float CountWidth = 96.0f * Metrics.m_UiScale;
	const float CollectionMin = 220.0f * Metrics.m_UiScale;
	const float CollectionMax = 360.0f * Metrics.m_UiScale;
	const float FilterGroupWidth = LabelWidth + Gap + ChoiceWidth;
	const float SortGroupWidth = LabelWidth + Gap + ChoiceWidth;
	const auto SplitGroup = [&](const CUIRect &Group, CUIRect &Label, CUIRect &Control) {
		Label = Group;
		Control = Group;
		const float LabelPart = std::min(LabelWidth, std::max(0.0f, Group.w - Gap));
		Label.VSplitLeft(LabelPart, &Label, &Control);
		Control.VSplitLeft(std::min(Gap, Control.w), nullptr, &Control);
	};

	const float InlineRequired = CollectionMin + CountWidth + FilterGroupWidth + SortGroupWidth + Gap * 4.0f;
	if(View.w >= InlineRequired)
	{
		CUIRect Row{View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
		Layout.m_Height += Row.h + Gap;
		const float CollectionWidth = std::min(CollectionMax, std::max(0.0f, Row.w - (CountWidth + FilterGroupWidth + SortGroupWidth + Gap * 4.0f)));
		Row.VSplitLeft(CollectionWidth, &Layout.m_Collection, &Row);
		Row.VSplitLeft(Gap, nullptr, &Row);
		Row.VSplitLeft(std::min(CountWidth, Row.w), &Layout.m_Count, &Row);
		Row.VSplitLeft(std::min(Gap, Row.w), nullptr, &Row);
		CUIRect FilterGroup, SortGroup;
		Row.VSplitRight(std::min(SortGroupWidth, Row.w), &Row, &SortGroup);
		Row.VSplitRight(std::min(Gap, Row.w), &Row, nullptr);
		Row.VSplitRight(std::min(FilterGroupWidth, Row.w), &Row, &FilterGroup);
		SplitGroup(FilterGroup, Layout.m_FilterLabel, Layout.m_Filter);
		SplitGroup(SortGroup, Layout.m_SortLabel, Layout.m_Sort);
		return Layout;
	}

	CUIRect FirstRow{View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
	Layout.m_Height += FirstRow.h + Gap;
	if(View.w >= CollectionMin + CountWidth + Gap)
	{
		const float CollectionWidth = std::min(CollectionMax, std::max(0.0f, FirstRow.w - CountWidth - Gap));
		FirstRow.VSplitLeft(CollectionWidth, &Layout.m_Collection, &FirstRow);
		FirstRow.VSplitLeft(std::min(Gap, FirstRow.w), nullptr, &FirstRow);
		FirstRow.VSplitLeft(std::min(CountWidth, FirstRow.w), &Layout.m_Count, &FirstRow);
	}
	else
	{
		Layout.m_Collection = FirstRow;
		Layout.m_Count = {View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
		Layout.m_Height += Layout.m_Count.h + Gap;
	}

	CUIRect GroupRow{View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
	if(GroupRow.w >= FilterGroupWidth + SortGroupWidth + Gap)
	{
		Layout.m_Height += GroupRow.h + Gap;
		CUIRect FilterGroup, SortGroup;
		GroupRow.VSplitRight(SortGroupWidth, &GroupRow, &SortGroup);
		GroupRow.VSplitRight(Gap, &GroupRow, nullptr);
		GroupRow.VSplitRight(FilterGroupWidth, &GroupRow, &FilterGroup);
		SplitGroup(FilterGroup, Layout.m_FilterLabel, Layout.m_Filter);
		SplitGroup(SortGroup, Layout.m_SortLabel, Layout.m_Sort);
	}
	else
	{
		CUIRect FilterGroup{View.x, GroupRow.y, std::min(FilterGroupWidth, View.w), Metrics.m_InputHeight};
		SplitGroup(FilterGroup, Layout.m_FilterLabel, Layout.m_Filter);
		Layout.m_Height += GroupRow.h + Gap;
		CUIRect SortGroup{View.x, View.y + Layout.m_Height, std::min(SortGroupWidth, View.w), Metrics.m_InputHeight};
		SplitGroup(SortGroup, Layout.m_SortLabel, Layout.m_Sort);
		Layout.m_Height += SortGroup.h + Gap;
	}
	return Layout;
}

inline int ResolveSettingsTeeSkinColumns(float Width, float UiScale)
{
	return std::clamp(static_cast<int>(std::max(0.0f, Width) / (180.0f * UiScale)), 1, 6);
}

#endif
