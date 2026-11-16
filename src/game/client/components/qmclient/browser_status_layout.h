#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_STATUS_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_STATUS_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

struct SQmBrowserStatusLayout
{
	CUIRect m_RefreshBar{};
	CUIRect m_SearchLabel{}, m_SearchInput{};
	CUIRect m_ExcludeLabel{}, m_ExcludeInput{};
	CUIRect m_AddressLabel{}, m_AddressInput{};
	CUIRect m_Players{}, m_Servers{};
	CUIRect m_RefreshButton{}, m_ConnectButton{};
	CUIRect m_MapFilter{}, m_Notice{};
	float m_ContentHeight = 0.0f;
};

inline float QmBrowserLayoutExtent(float Value)
{
	return std::isfinite(Value) ? std::max(0.0f, Value) : 0.0f;
}

struct SQmBrowserChipGrid
{
	static constexpr int MAX_CHIPS = 16;
	CUIRect m_aRects[MAX_CHIPS]{};
	int m_Count = 0;
	int m_PerRow = 0;
	float m_ContentHeight = 0.0f;
};

// 等宽胶囊网格：宽度决定每行个数，行数决定内容高度；所有矩形都落在 Area 内。
// 底部快捷条与右侧筛选面板共用同一套度量，避免两处密度不一致。
inline SQmBrowserChipGrid QmBrowserChipGrid(CUIRect Area, int Count, float MinChipWidth, float ChipHeight = 18.0f, float Gap = 4.0f)
{
	SQmBrowserChipGrid Result;
	const float Width = QmBrowserLayoutExtent(Area.w);
	Result.m_Count = std::clamp(Count, 0, SQmBrowserChipGrid::MAX_CHIPS);
	if(Result.m_Count == 0 || Width <= 0.0f)
		return Result;
	const float SafeMin = std::max(MinChipWidth, 1.0f);
	const float SafeGap = std::max(Gap, 0.0f);
	Result.m_PerRow = std::clamp((int)((Width + SafeGap) / (SafeMin + SafeGap)), 1, Result.m_Count);
	const float ChipWidth = std::max(0.0f, (Width - SafeGap * (Result.m_PerRow - 1)) / Result.m_PerRow);
	const int Rows = (Result.m_Count + Result.m_PerRow - 1) / Result.m_PerRow;
	for(int Index = 0; Index < Result.m_Count; ++Index)
	{
		const int Row = Index / Result.m_PerRow;
		const int Column = Index % Result.m_PerRow;
		Result.m_aRects[Index] = {Area.x + Column * (ChipWidth + SafeGap), Area.y + Row * (ChipHeight + SafeGap), ChipWidth, ChipHeight};
	}
	Result.m_ContentHeight = Rows * (ChipHeight + SafeGap) - SafeGap;
	return Result;
}

struct SQmBrowserMapFilterLayout
{
	// 沿用厚胶囊滑条：标题 + 左侧档位文本 + 滑条，第二行是空服/收藏开关。
	CUIRect m_Heading{}, m_CurrentLabel{}, m_Slider{};
	CUIRect m_EmptyGroup{}, m_EmptyLabel{}, m_EmptyToggle{};
	CUIRect m_FavoriteGroup{}, m_FavoriteLabel{}, m_FavoriteToggle{}, m_FavoriteIcon{};
	float m_ContentHeight = 0.0f;
};

// 标签可省略显示，但滑块、开关与星标的矩形始终位于各自内容列内。
inline SQmBrowserMapFilterLayout QmBrowserMapFilterLayout(CUIRect Area, float FavoriteTextWidth)
{
	SQmBrowserMapFilterLayout Result;
	const float Width = QmBrowserLayoutExtent(Area.w);
	Result.m_Heading = {Area.x, Area.y, Width, 14.0f};
	const float CurrentWidth = std::min(58.0f, Width * 0.4f);
	const float SliderGap = std::min(4.0f, Width * 0.04f);
	Result.m_CurrentLabel = {Area.x, Area.y + 18.0f, CurrentWidth, 20.0f};
	Result.m_Slider = {Area.x + CurrentWidth + SliderGap, Area.y + 18.0f, std::max(0.0f, Width - CurrentWidth - SliderGap), 20.0f};

	const float ToggleY = Area.y + 44.0f;
	const float HalfWidth = Width * 0.5f;
	const float LeftWidth = std::max(0.0f, HalfWidth - 4.0f);
	const float EmptyToggleWidth = std::min(28.0f, LeftWidth * 0.35f);
	Result.m_EmptyGroup = {Area.x, ToggleY, LeftWidth, 18.0f};
	Result.m_EmptyLabel = {Area.x, ToggleY, std::max(0.0f, LeftWidth - EmptyToggleWidth - SliderGap), 18.0f};
	Result.m_EmptyToggle = {Area.x + LeftWidth - EmptyToggleWidth, ToggleY, EmptyToggleWidth, 18.0f};

	const float ToggleWidth = std::min(28.0f, Width * 0.3f);
	const float IconWidth = std::min(18.0f, Width * 0.2f);
	const float LabelGap = std::min(6.0f, Width * 0.05f);
	const float RightX = Area.x + std::min(HalfWidth + 4.0f, Width);
	const float RightWidth = std::max(0.0f, Area.x + Width - RightX);
	const float LabelWidth = std::clamp(QmBrowserLayoutExtent(FavoriteTextWidth), 0.0f,
		std::max(0.0f, RightWidth - ToggleWidth - IconWidth - LabelGap));
	const float GroupWidth = LabelWidth + LabelGap + ToggleWidth + IconWidth;
	const float X = RightX + RightWidth - GroupWidth;
	Result.m_FavoriteGroup = {X, ToggleY, GroupWidth, 18.0f};
	Result.m_FavoriteLabel = {X, ToggleY, LabelWidth, 18.0f};
	Result.m_FavoriteToggle = {X + LabelWidth + LabelGap, ToggleY, ToggleWidth, 18.0f};
	Result.m_FavoriteIcon = {X + LabelWidth + LabelGap + ToggleWidth, ToggleY, IconWidth, 18.0f};
	Result.m_ContentHeight = ToggleY + 18.0f - Area.y;
	return Result;
}

// 状态栏按同一份度量预留筛选块高度，滑条与开关行不会和上方控件重叠。
inline float QmBrowserMapFilterContentHeight(float Width)
{
	return QmBrowserMapFilterLayout({0.0f, 0.0f, Width, 0.0f}, 0.0f).m_ContentHeight;
}

// 底部控件按内容测量，不从剩余高度反推字号或按钮高度；高度不足由滚动容器承接。
inline SQmBrowserStatusLayout QmBrowserStatusLayout(CUIRect Area, bool ShowNotice)
{
	SQmBrowserStatusLayout Result;
	const float Width = QmBrowserLayoutExtent(Area.w);
	const bool SideActions = Width >= 430.0f;
	const bool InlineMapFilter = Width >= 550.0f;
	const float FieldWidth = InlineMapFilter ? std::min(350.0f, Width - 143.0f - 24.0f - 140.0f) : SideActions ? Width - 143.0f :
														     Width;
	const bool StackedLabels = FieldWidth < 220.0f;
	const float RowHeight = StackedLabels ? 40.0f : 24.0f;
	Result.m_RefreshBar = {Area.x, Area.y, Width, 2.0f};
	const float FieldsY = Area.y + 5.0f;
	auto Field = [&](int Index, CUIRect &Label, CUIRect &Input) {
		const float Y = FieldsY + Index * RowHeight;
		if(StackedLabels)
		{
			Label = {Area.x, Y, FieldWidth, 14.0f};
			Input = {Area.x, Y + 16.0f, FieldWidth, 20.0f};
		}
		else
		{
			const float LabelWidth = std::min(120.0f, FieldWidth * 0.35f);
			Label = {Area.x, Y + 2.0f, LabelWidth, 20.0f};
			Input = {Area.x + LabelWidth + 10.0f, Y + 2.0f, FieldWidth - LabelWidth - 10.0f, 20.0f};
		}
	};
	Field(0, Result.m_SearchLabel, Result.m_SearchInput);
	Field(1, Result.m_ExcludeLabel, Result.m_ExcludeInput);
	Field(2, Result.m_AddressLabel, Result.m_AddressInput);
	float Bottom = FieldsY + 3.0f * RowHeight;
	// 筛选块高度按滑条+开关行的实际度量预留，宽度变化时不会和上方控件重叠。
	const float MapFilterWidth = InlineMapFilter ? Width - FieldWidth - 143.0f - 24.0f : Width;
	const float MapFilterHeight = QmBrowserMapFilterContentHeight(MapFilterWidth);
	if(InlineMapFilter)
	{
		Result.m_MapFilter = {Area.x + FieldWidth + 12.0f, FieldsY, MapFilterWidth, MapFilterHeight};
		Bottom = std::max(Bottom, FieldsY + MapFilterHeight);
	}
	else
	{
		Result.m_MapFilter = {Area.x, Bottom + 8.0f, Width, MapFilterHeight};
		Bottom += MapFilterHeight + 8.0f;
	}
	const float ActionsX = SideActions ? Area.x + Width - 135.0f : Area.x;
	const float ActionsY = SideActions ? FieldsY : Bottom + 8.0f;
	const float ActionsWidth = SideActions ? 135.0f : Width;
	Result.m_Players = {ActionsX, ActionsY, ActionsWidth, 18.0f};
	Result.m_Servers = {ActionsX, ActionsY + 18.0f, ActionsWidth, 18.0f};
	const float ButtonGap = std::min(10.0f, ActionsWidth * 0.1f);
	const float ButtonWidth = (ActionsWidth - ButtonGap) * 0.5f;
	Result.m_RefreshButton = {ActionsX, ActionsY + 48.0f, ButtonWidth, 24.0f};
	Result.m_ConnectButton = {ActionsX + ButtonWidth + ButtonGap, ActionsY + 48.0f, ButtonWidth, 24.0f};
	Bottom = std::max(Bottom, ActionsY + 72.0f);
	if(ShowNotice)
	{
		Result.m_Notice = {Area.x, Bottom + 2.0f, Width, 12.0f};
		Bottom += 14.0f;
	}
	Result.m_ContentHeight = Bottom - Area.y;
	return Result;
}

struct SQmBrowserPanelLayout
{
	CUIRect m_List{}, m_Status{}, m_Toolbox{};
	CUIRect m_ListContent{}, m_StatusViewport{}, m_ToolboxContent{};
};

// 列表先让出底部卡片所需高度；极低窗口只缩短视口，卡片内的真实控件仍保持原高。
inline SQmBrowserPanelLayout QmBrowserPanelLayout(CUIRect View, bool ShowNotice)
{
	SQmBrowserPanelLayout Result;
	const float Width = QmBrowserLayoutExtent(View.w);
	const float Height = QmBrowserLayoutExtent(View.h);
	float ToolboxWidth = std::min(205.0f, std::max(0.0f, Width - 248.0f));
	// 连工具箱的最小内容列都放不下时，让主操作区使用全宽。
	if(ToolboxWidth < 120.0f)
		ToolboxWidth = 0.0f;
	const float ColumnGap = ToolboxWidth > 0.0f ? 8.0f : 0.0f;
	const float ListWidth = Width - ToolboxWidth - ColumnGap;
	const float PaddingX = std::min(10.0f, ListWidth * 0.1f);
	const float NaturalHeight = QmBrowserStatusLayout({0.0f, 0.0f, ListWidth - 2.0f * PaddingX, 0.0f}, ShowNotice).m_ContentHeight + 20.0f;
	const float StatusHeight = std::min(Height, NaturalHeight);
	const float ListHeight = std::max(0.0f, Height - StatusHeight - 8.0f);
	Result.m_List = {View.x, View.y, ListWidth, ListHeight};
	Result.m_Status = {View.x, View.y + Height - StatusHeight, ListWidth, StatusHeight};
	Result.m_Toolbox = {View.x + Width - ToolboxWidth, View.y, ToolboxWidth, Height};
	const float ListPadding = std::min(2.0f, ListHeight * 0.5f);
	Result.m_ListContent = {View.x + std::min(2.0f, ListWidth * 0.5f), View.y + ListPadding,
		std::max(0.0f, ListWidth - 4.0f), std::max(0.0f, ListHeight - 4.0f)};
	const float PaddingY = std::min(10.0f, StatusHeight * 0.1f);
	Result.m_StatusViewport = {Result.m_Status.x + PaddingX, Result.m_Status.y + PaddingY,
		ListWidth - 2.0f * PaddingX, StatusHeight - 2.0f * PaddingY};
	const float ToolboxPaddingY = std::min(10.0f, Height * 0.5f);
	Result.m_ToolboxContent = {Result.m_Toolbox.x + std::min(10.0f, ToolboxWidth * 0.5f), View.y + ToolboxPaddingY,
		std::max(0.0f, ToolboxWidth - 20.0f), Height - 2.0f * ToolboxPaddingY};
	return Result;
}

#endif
