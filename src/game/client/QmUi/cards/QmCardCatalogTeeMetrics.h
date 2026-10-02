#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEEMETRICS_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGTEEMETRICS_H

#include <game/client/QmUi/SettingsPageLayout.h>

#include <array>

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
	Previews.VSplitMid(&Layout.m_aPreviews[0], &Layout.m_aPreviews[1], Gap * 2.0f);
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
	const float DownloadsHeight = ResolveSettingsRowsHeight(5, Metrics.m_LineHeight, Metrics.m_LineSpacing);
	const float PrefixHeight = ResolveSettingsRowsHeight(3, Metrics.m_InputHeight, Metrics.m_LineSpacing);
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
	CUIRect FilterRow{View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
	Layout.m_Height += FilterRow.h + Gap;
	const float SortWidth = 152.0f * Metrics.m_UiScale;
	CUIRect SortRow;
	if(View.w < SortWidth + Gap + 210.0f * Metrics.m_UiScale)
	{
		Layout.m_Collection = FilterRow;
		SortRow = {View.x, View.y + Layout.m_Height, View.w, Metrics.m_InputHeight};
		Layout.m_Height += SortRow.h + Gap;
	}
	else
	{
		FilterRow.VSplitRight(SortWidth, &Layout.m_Collection, &SortRow);
		Layout.m_Collection.VSplitRight(Gap, &Layout.m_Collection, nullptr);
	}
	SortRow.VSplitLeft(std::min(SortRow.w * 0.42f, 68.0f * Metrics.m_UiScale), &Layout.m_SortLabel, &Layout.m_Sort);
	Layout.m_Sort.VSplitLeft(Gap, nullptr, &Layout.m_Sort);
	return Layout;
}

inline int ResolveSettingsTeeSkinColumns(float Width, float UiScale)
{
	return std::clamp(static_cast<int>(std::max(0.0f, Width) / (180.0f * UiScale)), 1, 6);
}

#endif
