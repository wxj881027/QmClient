#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H

#include <engine/shared/config.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/render.h>
#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_spectator_layout
{
	struct SDensity
	{
		float m_HalfWidth = 300.0f;
		float m_FontSize = 20.0f;
		float m_RowHeight = 60.0f;
		float m_TeeSizeMod = 1.0f;
		float m_Radius = 30.0f;
		int m_RowsPerColumn = 8;
		float m_BoxMove = -10.0f;
		float m_BoxOffset = 0.0f;
	};

	inline SDensity Density(int PlayerCount)
	{
		SDensity Result;
		if(PlayerCount > 128)
		{
			Result.m_RowsPerColumn = (PlayerCount + 3) / 4;
			Result.m_RowHeight = 576.0f / Result.m_RowsPerColumn;
			Result.m_FontSize = std::max(6.0f, Result.m_RowHeight - ui_token::spacing::XS);
			Result.m_TeeSizeMod = Result.m_RowHeight / 60.0f;
			Result.m_Radius = 3.0f;
			Result.m_BoxMove = 0.0f;
		}
		else if(PlayerCount > 32)
		{
			// 各密度档保留原列容量，增加行高；面板和 CP 底栏由内容高度扩展。
			Result.m_RowsPerColumn = PlayerCount > 96 ? 32 : (PlayerCount > 64 ? 24 : 16);
			Result.m_RowHeight = 576.0f / Result.m_RowsPerColumn;
			Result.m_FontSize = PlayerCount > 96 ? 15.0f : (PlayerCount > 64 ? 16.0f : 18.0f);
			Result.m_TeeSizeMod = PlayerCount > 96 ? 0.3f : (PlayerCount > 64 ? 0.45f : 0.7f);
			Result.m_Radius = PlayerCount > 96 ? 5.0f : (PlayerCount > 64 ? 6.0f : 10.0f);
			Result.m_BoxMove = 3.0f;
			Result.m_BoxOffset = 6.0f;
		}
		if(PlayerCount > 16)
			Result.m_HalfWidth = 600.0f;
		return Result;
	}

	inline void FitTeeToRow(CTeeRenderInfo &Info, float RowHeight)
	{
		const CAnimState *pIdle = CAnimState::GetIdle();
		float RenderedHeight;
		if(Info.m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY).IsValid())
			RenderedHeight = Info.m_Size * 1.24f;
		else
		{
			vec2 Offset, BodyOffset;
			float BodyWidth, BodyHeight;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &Info, Offset);
			CRenderTools::GetRenderTeeBodySize(pIdle, &Info, BodyOffset, BodyWidth, BodyHeight);
			const float BodyTop = (pIdle->GetBody()->m_Y - 32.0f) * Info.m_Size / 64.0f + BodyOffset.y;
			RenderedHeight = -2.0f * (BodyTop + Offset.y);
		}
		// 按皮肤实际墨迹缩放，包含胖皮肤和脚部；上下各留出一成行高。
		const float AvailableHeight = RowHeight * 0.8f;
		if(RenderedHeight > AvailableHeight)
			Info.m_Size *= AvailableHeight / RenderedHeight;
	}

	struct SSelectorLayout
	{
		vec2 m_ContentCenter;
		CUIRect m_Panel;
		CUIRect m_Mouse;
		CUIRect m_SearchRow;
		CUIRect m_Status;
	};

	struct SGroupTitleLayout
	{
		CUIRect m_Label;
		CUIRect m_Divider;
		float m_Height;
	};

	inline SGroupTitleLayout GroupTitle(vec2 Position, float Width, float TitleHeight, float Scale)
	{
		const float Padding = ui_token::spacing::SM * Scale;
		const float DividerGap = ui_token::spacing::XS * Scale;
		SGroupTitleLayout Layout{};
		Layout.m_Label = {Position.x, Position.y + Padding, Width, TitleHeight};
		Layout.m_Divider = {Position.x, Layout.m_Label.y + TitleHeight + DividerGap, Width, Scale};
		// 测量与绘制共用包含上下留白的高度，底栏跟随最后一行下移。
		Layout.m_Height = Padding + TitleHeight + DividerGap + Layout.m_Divider.h + Padding;
		return Layout;
	}

	template<typename F>
	int TeamBandEnd(int FirstRow, int GroupEnd, F &&TeamAt)
	{
		const int Team = TeamAt(FirstRow);
		int End = FirstRow + 1;
		while(End < GroupEnd && TeamAt(End) == Team)
			++End;
		return End;
	}

	// HalfHeight 默认 300（桌面 600 高的面板）；手机放大整块时传入按比例加高的半高。
	inline SSelectorLayout Build(vec2 Center, float HalfWidth, bool HasTeleSearch, float PlayersBottom = 0.0f, float HalfHeight = 300.0f)
	{
		constexpr float Padding = 20.0f;
		constexpr float SearchHeight = 40.0f;
		constexpr float StatusGap = 10.0f;
		constexpr float StatusHeight = 20.0f;
		const float PanelWidth = HalfWidth * 2.0f;
		const float SearchTop = std::max(310.0f, PlayersBottom + Padding);
		const float PanelBottom = HasTeleSearch ? SearchTop + SearchHeight + StatusGap + StatusHeight + Padding : std::max(HalfHeight, PlayersBottom + Padding);
		const float PanelHeight = HalfHeight + PanelBottom;
		SSelectorLayout Layout{};
		Layout.m_Panel = {Center.x - HalfWidth, Center.y - PanelHeight * 0.5f, PanelWidth, PanelHeight};
		Layout.m_ContentCenter = {Center.x, Layout.m_Panel.y + HalfHeight};
		Layout.m_Panel.Margin(Padding, &Layout.m_Mouse);
		if(HasTeleSearch)
		{
			const float SearchWidth = std::max(0.0f, std::min(560.0f, PanelWidth - Padding * 2.0f));
			Layout.m_SearchRow = {Center.x - SearchWidth * 0.5f, Layout.m_ContentCenter.y + SearchTop, SearchWidth, SearchHeight};
			Layout.m_Status = {Layout.m_SearchRow.x, Layout.m_SearchRow.y + SearchHeight + StatusGap, SearchWidth, StatusHeight};
		}
		return Layout;
	}

	inline CUIRect ViewTab(const SSelectorLayout &Layout, int Index, float Scale)
	{
		const float Step = Layout.m_Panel.w / 3.0f;
		return {Layout.m_Panel.x + 20.0f * Scale + Step * Index, Layout.m_Panel.y + 20.0f * Scale, Step - 40.0f * Scale, 60.0f * Scale};
	}

	inline float PlayerListStart(float Scale)
	{
		return -190.0f * Scale;
	}

	inline vec2 ClampMouse(const SSelectorLayout &Layout, vec2 Center, vec2 Mouse)
	{
		return vec2(std::clamp(Mouse.x, Layout.m_Mouse.x - Center.x, Layout.m_Mouse.x + Layout.m_Mouse.w - Center.x),
			std::clamp(Mouse.y, Layout.m_Mouse.y - Center.y, Layout.m_Mouse.y + Layout.m_Mouse.h - Center.y));
	}

	inline vec2 TouchPosition(vec2 Position, vec2 ScreenSize, vec2 Center)
	{
		// 面板动画会改变中心；触点仍对应原始屏幕位置。
		return Position * ScreenSize - Center;
	}
}

#endif
