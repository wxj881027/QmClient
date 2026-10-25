#include "QmLyricsHud.h"

#include "UiTokens.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud_editor.h>
#include <game/client/components/hud_media_island_logic.h>

#include <cmath>

namespace
{
	bool EnableMappedClip(IGraphics &Graphics, const CUIRect &Rect)
	{
		float X0, Y0, X1, Y1;
		Graphics.GetScreen(&X0, &Y0, &X1, &Y1);
		const float ScaleX = Graphics.ScreenWidth() / std::max(0.001f, X1 - X0);
		const float ScaleY = Graphics.ScreenHeight() / std::max(0.001f, Y1 - Y0);
		const int Left = std::clamp(static_cast<int>(std::floor((Rect.x - X0) * ScaleX)), 0, Graphics.ScreenWidth());
		const int Top = std::clamp(static_cast<int>(std::floor((Rect.y - Y0) * ScaleY)), 0, Graphics.ScreenHeight());
		const int Right = std::clamp(static_cast<int>(std::ceil((Rect.x + Rect.w - X0) * ScaleX)), 0, Graphics.ScreenWidth());
		const int Bottom = std::clamp(static_cast<int>(std::ceil((Rect.y + Rect.h - Y0) * ScaleY)), 0, Graphics.ScreenHeight());
		if(Right <= Left || Bottom <= Top)
			return false;
		Graphics.ClipEnable(Left, Top, Right - Left, Bottom - Top);
		return true;
	}
}

void QmRenderLyricsRows(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect,
	const char *const *ppRows, int RowCount, float FontSize, ColorRGBA Color, float ElapsedSeconds)
{
	RowCount = std::clamp(RowCount, 0, 2);
	if(RowCount == 0 || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const ColorRGBA PreviousColor = TextRender.GetTextColor();
	const float LineHeight = Rect.h / RowCount;
	for(int Index = 0; Index < RowCount; ++Index)
	{
		const char *pText = ppRows[Index];
		if(pText == nullptr || pText[0] == '\0')
			continue;
		const CUIRect Row{Rect.x, Rect.y + Index * LineHeight, Rect.w, LineHeight};
		const float Size = std::min(LineHeight, FontSize * (Index == 0 ? 1.0f : 0.9f));
		float Top = 0.0f;
		float Bottom = 0.0f;
		STextSizeProperties Props{};
		Props.m_pVisualTop = &Top;
		Props.m_pVisualBottom = &Bottom;
		const float Width = TextRender.TextWidth(Size, pText, -1, -1.0f, TEXTFLAG_DISALLOW_NEWLINE, Props);
		const float Offset = QmHudMediaIslandMarqueeOffset(Width, Row.w, ElapsedSeconds, Size * 5.1f);
		const float X = Width <= Row.w ? Row.x + (Row.w - Width) * 0.5f : Row.x - Offset;
		const float Y = Row.y + Row.h * 0.5f - (Top + Bottom) * 0.5f;
		TextRender.TextColor(Color.WithMultipliedAlpha(Index == 0 ? 1.0f : 0.8f));
		if(EnableMappedClip(Graphics, Row))
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = Size;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_DISALLOW_NEWLINE;
			Cursor.SetPosition(vec2(X, Y));
			TextRender.TextEx(&Cursor, pText);
			Graphics.ClipDisable();
		}
	}
	TextRender.TextColor(PreviousColor);
}

void QmRenderLyricsIsland(IGraphics &Graphics, ITextRender &TextRender, CHudEditor &Editor,
	const CUIRect &Anchor, float ScreenWidth, float ScreenHeight, const char *const *ppRows, int RowCount,
	float ElapsedSeconds, IGraphics::CRenderTargetHandle Backdrop)
{
	const bool FollowWidth = g_Config.m_QmLyricsIslandFollowWidth != 0;
	const CUIRect Panel = QmLyricsIslandLayout(Anchor, ScreenWidth, ScreenHeight, RowCount, FollowWidth);
	if(Panel.w <= 0.0f || Panel.h <= 0.0f)
		return;
	// 跟随时横向边界完全取自主岛，旧横向位置和缩放只在关闭跟随后恢复。
	const auto Placement = FollowWidth ? QmHudEditor::EHorizontalPlacement::DefaultPosition : QmHudEditor::EHorizontalPlacement::VisibleBounds;
	const auto Scope = Editor.BeginTransform(EHudEditorElement::Lyrics, Panel, Panel, Placement, !FollowWidth);
	const auto PreviousPreset = TextRender.GetFontPreset();
	const auto PreviousFlags = TextRender.GetRenderFlags();
	const auto PreviousOutline = TextRender.GetTextOutlineColor();
	TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender.SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender.TextOutlineColor(0.0f, 0.0f, 0.0f, 0.42f);
	const float Radius = std::min(Panel.h * 0.5f, 4.9f);
	ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandBgColor));
	Background.a = std::clamp(g_Config.m_QmHudIslandBgOpacity / 100.0f, 0.0f, 1.0f);
	float X0, Y0, X1, Y1;
	Graphics.GetScreen(&X0, &Y0, &X1, &Y1);
	SHudMediaIslandSdfRenderState State;
	State.m_MainRect = Panel;
	State.m_MainRadius = Radius;
	State.m_MainCorners = Scope.m_Corners;
	State.m_BackgroundColor = Background;
	State.m_ScreenPixelSize = std::max((X1 - X0) / std::max(1, Graphics.ScreenWidth()), (Y1 - Y0) / std::max(1, Graphics.ScreenHeight()));
	State.m_OuterShadowSize = State.m_ScreenPixelSize * 5.0f;
	State.m_OuterShadowOpacity = Background.a * 0.35f;
	State.m_Rect = QmHudMediaIslandSdfOuterRect(State);
	State.m_BackdropUv = QmHudMediaIslandBackdropUv(State.m_Rect, {X0, Y0, X1 - X0, Y1 - Y0});
	IGraphics::SMediaIslandSdfParams Params;
	if(Graphics.HasMediaIslandSdf() && QmHudMediaIslandBuildGpuSdfParams(State, Params))
		Graphics.RenderMediaIslandSdf(Params, Backdrop);
	else
		Graphics.DrawRect(Panel.x, Panel.y, Panel.w, Panel.h, Background, Scope.m_Corners, Radius);
	const CUIRect TextRect{Panel.x + 4.9f, Panel.y + 1.75f, std::max(0.0f, Panel.w - 9.8f), std::max(0.0f, Panel.h - 3.5f)};
	QmRenderLyricsRows(Graphics, TextRender, TextRect, ppRows, RowCount, 4.368f,
		ui_token::color::ADAPTIVE_ON_DARK.WithAlpha(0.90f), ElapsedSeconds);
	TextRender.SetFontPreset(PreviousPreset);
	TextRender.SetRenderFlags(PreviousFlags);
	TextRender.TextOutlineColor(PreviousOutline);
	Editor.EndTransform(Scope);
}
