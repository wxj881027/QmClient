#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_LAYOUT_H

#include "bind_menu.h"
#include "bindwheel_state.h"

#include <base/vmath.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace qm_bind_menu_preview
{
	inline float LayoutScale(float Value, float UiScale)
	{
		return Value * std::max(0.75f, UiScale);
	}

	struct SLayout
	{
		qm_bind_menu::ELayout m_Layout = qm_bind_menu::ELayout::PANEL;
		CUIRect m_Panel{};
		CUIRect m_Title{};
		CUIRect m_Rows{};
		float m_RowHeight = 0.0f;
		float m_RowSpacing = 0.0f;
		int m_VisibleRows = 0;
		int m_Scroll = 0;
		vec2 m_Center{};
		float m_WheelRadius = 0.0f;
		float m_WheelInnerRadius = 0.0f;
		float m_WheelBackgroundOuterRadius = 0.0f;
		float m_WheelCardWidth = 0.0f;
		float m_WheelCardHeight = 0.0f;
		bool m_WheelDoubleRing = false;
	};

	struct SIdentityRowLayout
	{
		CUIRect m_Avatar{};
		CUIRect m_Name{};
		CUIRect m_Clan{};
	};

	struct SIdentityLayout
	{
		std::array<SIdentityRowLayout, 2> m_aRows{};
		CUIRect m_CompactAvatars{};
		CUIRect m_CompactNames{};
		CUIRect m_CompactClans{};
		int m_TeeCount = 0;
		bool m_Compact = false;
	};

	inline SIdentityLayout BuildIdentityLayout(const CUIRect &Rect, bool Main, bool Dummy, float UiScale)
	{
		SIdentityLayout Layout;
		Layout.m_TeeCount = (Main ? 1 : 0) + (Dummy ? 1 : 0);
		if(Layout.m_TeeCount == 0)
			return Layout;

		const float Gap = LayoutScale(ui_token::spacing::XS, UiScale);
		Layout.m_Compact = Rect.w < LayoutScale(ui_token::bind_menu::IDENTITY_COLUMN_MIN_WIDTH, UiScale);
		if(Layout.m_Compact)
		{
			const float AvatarHeight = std::max(0.0f, Rect.h * ui_token::bind_menu::IDENTITY_COMPACT_AVATAR_HEIGHT_RATIO);
			const float TextHeight = std::max(0.0f, Rect.h - AvatarHeight);
			const float NameHeight = TextHeight * ui_token::bind_menu::IDENTITY_COMPACT_NAME_HEIGHT_RATIO;
			Layout.m_CompactAvatars = CUIRect{Rect.x, Rect.y, Rect.w, AvatarHeight};
			Layout.m_CompactNames = CUIRect{Rect.x, Rect.y + AvatarHeight, Rect.w, NameHeight};
			Layout.m_CompactClans = CUIRect{Rect.x, Rect.y + AvatarHeight + NameHeight, Rect.w,
				std::max(0.0f, TextHeight - NameHeight)};
			const float SlotWidth = Rect.w / Layout.m_TeeCount;
			int AvatarIndex = 0;
			for(const bool Present : {Main, Dummy})
			{
				if(!Present)
					continue;
				const float AvatarSize = std::max(0.0f, std::min(SlotWidth * 0.72f, AvatarHeight));
				SIdentityRowLayout &Item = Layout.m_aRows[AvatarIndex++];
				const int SlotIndex = AvatarIndex - 1;
				Item.m_Avatar = CUIRect{Rect.x + SlotWidth * (SlotIndex + 0.5f) - AvatarSize * 0.5f,
					Layout.m_CompactAvatars.Center().y - AvatarSize * 0.5f, AvatarSize, AvatarSize};
			}
			return Layout;
		}

		const float ColumnWidth = Rect.w / Layout.m_TeeCount;
		int RowIndex = 0;
		for(const bool Present : {Main, Dummy})
		{
			if(!Present)
				continue;
			const CUIRect Column{Rect.x + ColumnWidth * RowIndex, Rect.y, ColumnWidth, Rect.h};
			const float AvatarSize = std::max(0.0f, std::min(Column.h * ui_token::bind_menu::IDENTITY_AVATAR_HEIGHT_RATIO,
									Column.w * ui_token::bind_menu::IDENTITY_AVATAR_WIDTH_RATIO));
			SIdentityRowLayout &Item = Layout.m_aRows[RowIndex++];
			Item.m_Avatar = CUIRect{Column.x, Column.Center().y - AvatarSize * 0.5f, AvatarSize, AvatarSize};
			const float TextX = Item.m_Avatar.x + Item.m_Avatar.w + Gap;
			const float TextWidth = std::max(0.0f, Column.x + Column.w - TextX);
			const float NameHeight = Column.h * ui_token::bind_menu::IDENTITY_NAME_HEIGHT_RATIO;
			Item.m_Name = CUIRect{TextX, Column.y, TextWidth, NameHeight};
			Item.m_Clan = CUIRect{TextX, Column.y + NameHeight, TextWidth, std::max(0.0f, Column.h - NameHeight)};
		}
		return Layout;
	}

	inline CUIRect WheelItemRect(const SLayout &Layout, int Index, int Count)
	{
		const float Theta = 2.0f * pi / std::max(1, Count);
		const bool Inner = Layout.m_WheelDoubleRing && (Index % 2) != 0;
		const float Radius = Inner ? Layout.m_WheelInnerRadius : Layout.m_WheelRadius;
		const float CardScale = Inner ? ui_token::bind_menu::WHEEL_INNER_CARD_SCALE : 1.0f;
		const float CardWidth = Layout.m_WheelCardWidth * CardScale;
		const float CardHeight = Layout.m_WheelCardHeight * CardScale;
		const vec2 Center = Layout.m_Center + direction(Theta * Index) * Radius;
		return CUIRect{Center.x - CardWidth * 0.5f, Center.y - CardHeight * 0.5f, CardWidth, CardHeight};
	}

	inline float RevealScaleFor(qm_bind_menu::ELayout Layout, float Reveal)
	{
		const float MinScale = Layout == qm_bind_menu::ELayout::WHEEL ?
					       ui_token::bind_menu::WHEEL_ENTER_SCALE :
					       ui_token::bind_menu::PANEL_ENTER_SCALE;
		return MinScale + (1.0f - MinScale) * std::clamp(Reveal, 0.0f, 1.0f);
	}

	inline SLayout BuildLayout(const CUIRect &Area, qm_bind_menu::ELayout LayoutKind, int OptionCount, int Scroll, float UiScale,
		bool ScreenOverlay = false, qm_bind_menu::EMenu Menu = qm_bind_menu::EMenu::CUSTOM)
	{
		SLayout Layout;
		Layout.m_Layout = LayoutKind;
		const float Padding = LayoutScale(ui_token::panel::PADDING, UiScale);
		const float Gap = LayoutScale(ui_token::panel::GAP, UiScale);
		if(LayoutKind == qm_bind_menu::ELayout::WHEEL)
		{
			const float ShortSide = std::min(Area.w, Area.h);
			const float CardHeightRows = Menu == qm_bind_menu::EMenu::ASSETS ?
							     ui_token::bind_menu::ASSET_WHEEL_CARD_HEIGHT_ROWS :
							     ui_token::bind_menu::WHEEL_CARD_HEIGHT_ROWS;
			Layout.m_WheelCardHeight = LayoutScale(ui_token::list::ROW_HEIGHT * CardHeightRows, UiScale);
			Layout.m_Center = Area.Center();
			Layout.m_WheelDoubleRing = OptionCount > ui_token::bind_menu::WHEEL_SINGLE_RING_MAX;
			const float RadiusRatio = ScreenOverlay ? ui_token::bind_menu::WHEEL_SCREEN_RADIUS_RATIO :
								  ui_token::bind_menu::WHEEL_RADIUS_RATIO;
			const float DesiredRadius = std::max(1.0f,
				std::min(ShortSide * RadiusRatio,
					std::max(0.0f, Area.h * 0.5f - Padding)));
			Layout.m_WheelRadius = DesiredRadius;

			const auto ResolveCardWidth = [&](float Radius, float InnerRadius) {
				const float ArcWidth = OptionCount > 0 ?
							       [&]() {
								       if(!Layout.m_WheelDoubleRing)
									       return 2.0f * pi * Radius / OptionCount;
								       const int OuterCount = (OptionCount + 1) / 2;
								       const int InnerCount = OptionCount / 2;
								       return std::min(2.0f * pi * Radius / OuterCount,
									       2.0f * pi * InnerRadius / std::max(1, InnerCount));
							       }() *
								       ui_token::bind_menu::WHEEL_CARD_WIDTH_ARC_RATIO :
							       std::numeric_limits<float>::max();
				const float PreferredCardWidth = std::max(LayoutScale(ui_token::bind_menu::WHEEL_CARD_WIDTH_MIN, UiScale),
					Radius * ui_token::bind_menu::WHEEL_CARD_WIDTH_RADIUS_RATIO);
				return std::max(1.0f, std::min(PreferredCardWidth,
							      std::min(LayoutScale(ui_token::bind_menu::WHEEL_CARD_WIDTH_MAX, UiScale), ArcWidth)));
			};

			for(int Iteration = 0; Iteration < 3; ++Iteration)
			{
				Layout.m_WheelInnerRadius = Layout.m_WheelRadius * ui_token::bind_menu::WHEEL_INNER_RADIUS_RATIO;
				Layout.m_WheelCardWidth = ResolveCardWidth(Layout.m_WheelRadius, Layout.m_WheelInnerRadius);
				const float HalfDiagonal = std::sqrt(Layout.m_WheelCardWidth * Layout.m_WheelCardWidth +
								     Layout.m_WheelCardHeight * Layout.m_WheelCardHeight) *
							   0.5f;
				const float FittedRadius = std::max(1.0f,
					std::min(Area.w * 0.5f - HalfDiagonal - Padding,
						Area.h * 0.5f - HalfDiagonal - Padding));
				Layout.m_WheelRadius = std::min(DesiredRadius, FittedRadius);
			}
			Layout.m_WheelInnerRadius = Layout.m_WheelRadius * ui_token::bind_menu::WHEEL_INNER_RADIUS_RATIO;
			Layout.m_WheelCardWidth = ResolveCardWidth(Layout.m_WheelRadius, Layout.m_WheelInnerRadius);
			const float HalfDiagonal = std::sqrt(Layout.m_WheelCardWidth * Layout.m_WheelCardWidth +
							     Layout.m_WheelCardHeight * Layout.m_WheelCardHeight) *
						   0.5f;
			const float BackgroundGap = LayoutScale(ui_token::spacing::SM, UiScale);
			Layout.m_WheelBackgroundOuterRadius = Layout.m_WheelRadius + HalfDiagonal + BackgroundGap;
			Layout.m_Panel = CUIRect{Area.Center().x - Layout.m_WheelRadius - Layout.m_WheelCardWidth * 0.5f,
				Layout.m_Center.y - Layout.m_WheelRadius - Layout.m_WheelCardHeight * 0.5f,
				(Layout.m_WheelRadius + Layout.m_WheelCardWidth * 0.5f) * 2.0f,
				(Layout.m_WheelRadius + Layout.m_WheelCardHeight * 0.5f) * 2.0f};
			return Layout;
		}

		const float MaxWidth = std::min(Area.w, LayoutScale(ui_token::bind_menu::PANEL_MAX_WIDTH, UiScale));
		const float MinWidth = std::min(LayoutScale(ui_token::bind_menu::PANEL_MIN_WIDTH, UiScale), MaxWidth);
		const float PanelWidth = std::clamp(Area.w * ui_token::bind_menu::PANEL_WIDTH_RATIO, MinWidth, MaxWidth);
		const float HeaderHeight = LayoutScale(ui_token::font::HEADLINE * 2.0f, UiScale);
		const float RowHeightRows = Menu == qm_bind_menu::EMenu::ASSETS ?
						    ui_token::bind_menu::ASSET_PANEL_ROW_HEIGHT_ROWS :
						    ui_token::bind_menu::PANEL_ROW_HEIGHT_ROWS;
		const float RowHeight = LayoutScale(ui_token::list::ROW_HEIGHT * RowHeightRows, UiScale);
		const float RowSpacing = LayoutScale(ui_token::list::ROW_SPACING, UiScale);
		const int MaxVisibleRows = ScreenOverlay ? ui_token::bind_menu::PANEL_MAX_VISIBLE_ROWS : ui_token::bind_menu::PREVIEW_MAX_VISIBLE_ROWS;
		const float AvailableHeight = ScreenOverlay ?
						      std::min(LayoutScale(ui_token::bind_menu::PANEL_MAX_HEIGHT, UiScale),
							      std::max(0.0f, Area.h - Padding * 2.0f)) :
						      std::max(0.0f, Area.h - Padding * 2.0f);
		const float FixedHeight = HeaderHeight + Gap + Padding * 2.0f;
		const float RowPitch = RowHeight + RowSpacing;
		const int AreaRows = std::max(0, (int)std::floor((AvailableHeight - FixedHeight + RowSpacing) / std::max(1.0f, RowPitch)));
		const int VisibleRows = std::clamp(OptionCount, 0, std::min(MaxVisibleRows, AreaRows));
		Layout.m_VisibleRows = VisibleRows;
		Layout.m_RowHeight = RowHeight;
		Layout.m_RowSpacing = RowSpacing;
		const float RowsHeight = VisibleRows > 0 ? ui_token::list::RowsHeight(VisibleRows, RowHeight, RowSpacing) :
							   std::max(RowHeight, LayoutScale(ui_token::font::BODY * 2.0f, UiScale));
		const float PanelHeight = std::min(AvailableHeight, FixedHeight + RowsHeight);
		Layout.m_Panel = CUIRect{Area.Center().x - PanelWidth * 0.5f, Area.y + (Area.h - PanelHeight) * 0.5f, PanelWidth, PanelHeight};
		const float InnerHeight = std::max(0.0f, PanelHeight - Padding * 2.0f);
		const float TitleHeight = std::min(HeaderHeight, InnerHeight);
		const float RowsHeightFinal = std::min(RowsHeight,
			std::max(0.0f, InnerHeight - TitleHeight - Gap));
		Layout.m_Title = CUIRect{Layout.m_Panel.x + Padding, Layout.m_Panel.y + Padding, Layout.m_Panel.w - Padding * 2.0f, TitleHeight};
		Layout.m_Rows = CUIRect{Layout.m_Panel.x + Padding, Layout.m_Title.y + Layout.m_Title.h + Gap, Layout.m_Panel.w - Padding * 2.0f, RowsHeightFinal};
		Layout.m_Scroll = std::clamp(Scroll, 0, std::max(0, OptionCount - VisibleRows));
		return Layout;
	}

	inline int HitTest(const SLayout &Layout, vec2 Mouse, int OptionCount, int Scroll)
	{
		if(OptionCount <= 0)
			return -1;
		if(Layout.m_Layout == qm_bind_menu::ELayout::PANEL)
			return CQmBindWheelState::SelectedPanel(Mouse, Layout.m_Rows.x, Layout.m_Rows.y, Layout.m_Rows.w,
				Layout.m_RowHeight, OptionCount, Layout.m_Scroll, Layout.m_RowSpacing);

		for(int Index = 0; Index < OptionCount; ++Index)
		{
			if(WheelItemRect(Layout, Index, OptionCount).Inside(Mouse))
				return Index;
		}
		const vec2 Relative = Mouse - Layout.m_Center;
		const float Distance = length(Relative);
		if(Distance < Layout.m_WheelRadius * ui_token::bind_menu::WHEEL_HUB_RADIUS_RATIO ||
			Distance > Layout.m_WheelRadius + std::max(Layout.m_WheelCardWidth, Layout.m_WheelCardHeight) * 0.65f)
			return -1;
		const int Index = CQmBindWheelState::SelectedWheel(Relative, OptionCount, Layout.m_WheelRadius * ui_token::bind_menu::WHEEL_HUB_RADIUS_RATIO);
		return Index >= 0 && Index < OptionCount ? Index : -1;
	}
} // namespace qm_bind_menu_preview

#endif
