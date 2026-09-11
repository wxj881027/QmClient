// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UICONTAINERS_H
#define GAME_CLIENT_QMUI_UICONTAINERS_H

#include "QmScroll.h"
#include "UiContext.h"
#include "UiSurface.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/keys.h>

#include <game/client/ui.h>
#include <game/client/ui_rect.h>

namespace ui_widget
{

	struct SCardProps
	{
		float m_Padding = ui_token::spacing::MD;
		float m_Radius = ui_token::radius::CARD;
		int m_Elevation = 1; // 0 = flat, 1 = medium shadow, 2 = high shadow
		const char *m_pTitle = nullptr;
		float m_TitleFontSize = ui_token::font::HEADLINE;
		bool m_DrawBorder = false;
		ColorRGBA m_FillColor = ui_token::color::SURFACE_GLASS;
		ColorRGBA m_HighlightColor = ui_token::color::SURFACE_HIGHLIGHT;
		ColorRGBA m_BorderColor = ui_token::color::BORDER_SUBTLE;
	};

	inline SCardProps QmClientCardProps(float UiScale = 1.0f, const SUiTheme *pTheme = nullptr)
	{
		SCardProps Props;
		Props.m_Padding = 14.0f * UiScale;
		Props.m_Radius = ui_token::radius::CARD * UiScale;
		Props.m_DrawBorder = true;
		if(pTheme != nullptr)
		{
			Props.m_FillColor = pTheme->m_Surface;
			Props.m_HighlightColor = pTheme->m_SurfaceHovered.WithAlpha(std::clamp(std::max(0.04f, pTheme->m_Surface.a * 0.10f), 0.0f, 0.16f));
			Props.m_BorderColor = pTheme->m_Border;
		}
		else
		{
			Props.m_FillColor = ColorRGBA(0.17f, 0.18f, 0.22f, 0.72f);
			Props.m_HighlightColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f);
			Props.m_BorderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f);
		}
		return Props;
	}

	struct SScrollContainerProps
	{
		SQmScrollContainerStyle m_Style;
		SQmScrollConfig m_Config;
		ColorRGBA m_TrackColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f);
		ColorRGBA m_ThumbColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.42f);
		float m_Radius = ui_token::radius::PILL; // 滚动条轨/滑块按胶囊绘制
		bool m_ContentDragAllowed = false;
	};

	// Renders a glass-surface card (drop shadow + main fill + 1px top highlight,
	// optional title and border). After drawing the chrome, invokes Body(Content)
	// with the inner content rect already inset by m_Padding.
	template<typename BodyFn>
	void DrawCard(const IUiContext &Ctx, const CUIRect &Rect, const SCardProps &Props, BodyFn &&Body)
	{
		// 1) Shadow
		if(Props.m_Elevation > 0)
		{
			const float ShadowX = Props.m_Elevation >= 2 ? ui_token::elevation::SHADOW_X_HIGH : ui_token::elevation::SHADOW_X_MED;
			const float ShadowY = Props.m_Elevation >= 2 ? ui_token::elevation::SHADOW_Y_HIGH : ui_token::elevation::SHADOW_Y_MED;
			CUIRect Shadow = Rect;
			Shadow.x += ShadowX;
			Shadow.y += ShadowY;
			DrawRoundedSurface(Ctx, Shadow, ui_token::color::SURFACE_SHADOW, ColorRGBA(), Props.m_Radius);
		}

		// 2) 保持外扩细描边的几何尺寸，同时使用统一表面绘制路径。
		if(Props.m_DrawBorder)
		{
			CUIRect BorderBg = Rect;
			BorderBg.Margin(-1.0f, &BorderBg);
			DrawRoundedSurface(Ctx, BorderBg, Props.m_BorderColor, ColorRGBA(), Props.m_Radius + 1.0f);
		}

		// 3) Card fill (glass)
		DrawRoundedSurface(Ctx, Rect, Props.m_FillColor, ColorRGBA(), Props.m_Radius);

		// 4) Top 1px highlight (sub-pixel sliver near upper edge for the lift cue)
		CUIRect Highlight = Rect;
		Highlight.h = 1.0f;
		Highlight.x += Props.m_Radius * 0.5f;
		Highlight.w -= Props.m_Radius;
		Highlight.Draw(Props.m_HighlightColor, IGraphics::CORNER_T, 0.0f);

		// 5) Content rect
		CUIRect Content = Rect;
		Content.Margin(Props.m_Padding, &Content);

		if(Props.m_pTitle != nullptr && Ctx.m_pUi != nullptr)
		{
			CUIRect Title;
			Content.HSplitTop(Props.m_TitleFontSize + ui_token::spacing::XS, &Title, &Content);
			Content.HSplitTop(ui_token::spacing::SM, nullptr, &Content);
			Ctx.m_pUi->DoLabel(&Title, Props.m_pTitle, Props.m_TitleFontSize, TEXTALIGN_ML);
		}

		Body(Content);
	}

	template<typename BodyFn>
	SQmScrollContainerFrame ScrollContainer(const IUiContext &Ctx, CQmScrollState &State, CQmScrollContainer &Controller, const CUIRect &ViewRect, float ContentHeight, const SScrollContainerProps &Props, BodyFn &&Body)
	{
		SQmScrollContainerInput Input;
		if(Ctx.m_pUi != nullptr)
		{
			Input.m_Hovered = Ctx.m_pUi->MouseHovered(&ViewRect);
			Input.m_MouseValid = true;
			Input.m_MouseX = Ctx.m_pUi->MouseX();
			Input.m_MouseY = Ctx.m_pUi->MouseY();
			Input.m_MouseDown = Ctx.m_pUi->MouseButton(0);
			Input.m_MousePressed = Ctx.m_pUi->MouseButtonClicked(0);
			Input.m_ModifierPressed = Ctx.m_pUi->Input()->ModifierIsPressed();
			Input.m_ContentDragAllowed = Props.m_ContentDragAllowed;
			Input.m_ContentDragBlocked = Ctx.m_pUi->ActiveItem() != nullptr;
			if(Input.m_Hovered && !Input.m_ModifierPressed)
			{
				if(Ctx.m_pUi->Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					Input.m_WheelDelta += 120.0f;
				if(Ctx.m_pUi->Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					Input.m_WheelDelta -= 120.0f;
			}
		}

		const SQmScrollContainerFrame ProbeFrame = Controller.PreviewFrame(State, ViewRect, ContentHeight, Props.m_Style);
		if(Ctx.m_pUi != nullptr && ProbeFrame.m_ScrollbarVisible)
		{
			const void *pScrollbarId = &Controller;
			Input.m_ThumbHovered = Ctx.m_pUi->MouseHovered(&ProbeFrame.m_ScrollbarThumbRect);
			Input.m_TrackHovered = Ctx.m_pUi->MouseHovered(&ProbeFrame.m_ScrollbarTrackRect) && !Input.m_ThumbHovered;
			if(Input.m_ThumbHovered || Input.m_TrackHovered)
				Ctx.m_pUi->SetHotItem(pScrollbarId);
			if((Ctx.m_pUi->HotItem() == pScrollbarId || Input.m_ThumbHovered || Input.m_TrackHovered) && Input.m_MousePressed)
				Ctx.m_pUi->SetActiveItem(pScrollbarId);
			if(Ctx.m_pUi->CheckActiveItem(pScrollbarId))
			{
				Input.m_ThumbHovered = Input.m_ThumbHovered || Controller.ScrollbarDragActive(State);
				Input.m_TrackHovered = Input.m_TrackHovered && !Controller.ScrollbarDragActive(State);
				if(!Input.m_MouseDown)
					Ctx.m_pUi->SetActiveItem(nullptr);
			}
		}

		const SQmScrollContainerFrame Frame = Controller.Update(State, ViewRect, ContentHeight, Ctx.m_FrameDt, Input, Props.m_Style, Props.m_Config);
		if(Ctx.m_pUi == nullptr)
		{
			Body(Frame.m_ContentRect);
			return Frame;
		}

		Ctx.m_pUi->ClipEnable(&Frame.m_ClipRect);
		Body(Frame.m_ContentRect);
		Ctx.m_pUi->ClipDisable();

		if(Frame.m_ScrollbarVisible)
		{
			if(Props.m_TrackColor.a > 0.0f)
				DrawRoundedSurface(Ctx, Frame.m_ScrollbarTrackRect, Props.m_TrackColor, ColorRGBA(), Props.m_Radius);
			if(Props.m_ThumbColor.a > 0.0f)
				DrawRoundedSurface(Ctx, Frame.m_ScrollbarThumbRect, Props.m_ThumbColor, ColorRGBA(), Props.m_Radius);
		}
		return Frame;
	}

} // namespace ui_widget

#endif
