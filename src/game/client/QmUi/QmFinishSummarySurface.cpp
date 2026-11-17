#include "QmFinishSummarySurface.h"

#include "QmFinishSummaryCover.h"
#include "QmIslandSurface.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/finish_summary_data.h>

#include <algorithm>
#include <cmath>

void qm_finish_summary::CSurface::Reset(IGraphics &Graphics)
{
	if(m_GraphicsVersion == Graphics.GraphicsResourcesResetVersion())
		Graphics.DestroyRenderTarget(&m_Target);
	else
		m_Target.Invalidate();
	m_GraphicsVersion = Graphics.GraphicsResourcesResetVersion();
	m_Width = m_Height = 0;
	m_RenderedId = 0;
}

bool qm_finish_summary::CSurface::Render(IGraphics &Graphics, ITextRender &TextRender, CRenderTools &RenderTools, const CUIRect &Rect,
	const QmFinishSummary::SSnapshot &Snapshot, const CManagedTeeRenderInfo *pTeeRenderInfo, CContentCache &Content,
	const SSurfaceStyle &Style, double NowSeconds, bool FullyExpanded, int MotionLevel)
{
	if(m_GraphicsVersion != Graphics.GraphicsResourcesResetVersion())
		Reset(Graphics);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;
	const float Alpha = std::clamp(Style.m_Alpha, 0.0f, 1.0f);
	const float ContentAlpha = std::clamp(Style.m_ContentAlpha, 0.0f, 1.0f);
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics.GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const CUIRect TextScreen{ScreenX0, ScreenY0, ScreenX1 - ScreenX0, ScreenY1 - ScreenY0};
	const vec2 PixelSize(TextScreen.w / std::max(1, Graphics.ScreenWidth()), TextScreen.h / std::max(1, Graphics.ScreenHeight()));
	SHudMediaIslandSdfRenderState Background;
	Background.m_MainRect = Rect;
	Background.m_MainRadius = Style.m_Radius;
	Background.m_MainCorners = IGraphics::CORNER_ALL;
	Background.m_BackgroundColor = Style.m_BackgroundColor;
	Background.m_ScreenPixelSize = std::max(0.0001f, std::max(PixelSize.x, PixelSize.y));

	Background.m_Rect = QmHudMediaIslandSdfOuterRect(Background);
	Background.m_BackdropUv = QmHudMediaIslandBackdropUv(Background.m_Rect, Style.m_ScreenRect);
	const auto DrawContents = [&](float DrawAlpha, const CUIRect *pTextScreen) {
		if(Style.m_DrawBackground && DrawAlpha > 0.001f)
		{
			Background.m_BackgroundColor = Style.m_BackgroundColor.WithMultipliedAlpha(DrawAlpha);
			qm_island::Render(&Graphics, Background, pTextScreen ? IGraphics::CRenderTargetHandle() : Style.m_Backdrop);
		}
		Content.Update(Snapshot, NowSeconds, FullyExpanded, MotionLevel);
		const float CoverAlpha = Content.m_Presentation.NumberColorProgress(ENumber::TIME) * DrawAlpha;
		RenderCover(Graphics, Rect, Style.m_CoverTexture, Style.m_Radius, CoverAlpha);
		const bool Complete = qm_finish_summary::Render(Graphics, TextRender, RenderTools, Rect, Snapshot, pTeeRenderInfo, Content,
			ContentAlpha * DrawAlpha, pTextScreen);
		if(!pTextScreen && Style.m_TopShade > 0.0f)
		{
			const ColorRGBA Top(0.0f, 0.0f, 0.0f, Style.m_TopShade * DrawAlpha);
			const ColorRGBA Bottom = Top.WithAlpha(0.0f);
			Graphics.DrawRect4(Rect.x, Rect.y, Rect.w, Rect.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, Style.m_Radius);
		}
		return Complete;
	};
	if(Alpha <= 0.001f || !Style.m_DrawBackground || !Graphics.IsRenderTargetSupported() || PixelSize.x <= 0.0f || PixelSize.y <= 0.0f)
		return DrawContents(Alpha, nullptr);

	const CUIRect &Bounds = Rect;
	// 预留当前屏幕的最终卡片尺寸，生长期间沿用目标；高分辨率只缩小离屏采样密度。
	const float DesiredWidth = std::max(Bounds.w / PixelSize.x, Graphics.ScreenWidth() * 0.375f);
	const float DesiredHeight = std::max(Bounds.h / PixelSize.y, Graphics.ScreenHeight() * 0.30f);
	const float Density = std::min({1.5f, 2048.0f / DesiredWidth, 1024.0f / DesiredHeight});
	const int Width = std::clamp((static_cast<int>(std::ceil(DesiredWidth * Density)) + 31) / 32 * 32, 32, 2048);
	const int Height = std::clamp((static_cast<int>(std::ceil(DesiredHeight * Density)) + 15) / 16 * 16, 16, 1024);
	if(!m_Target.IsValid() || Width > m_Width || Height > m_Height)
	{
		const int NewWidth = std::max(Width, m_Width);
		const int NewHeight = std::max(Height, m_Height);
		Reset(Graphics);
		m_Width = NewWidth;
		m_Height = NewHeight;
		m_Target = Graphics.CreateRenderTarget(m_Width, m_Height, std::min(4, std::max(1, Graphics.MaxMultisampleRenderTargetSamples())));
	}
	if(!m_Target.IsValid())
		return DrawContents(Alpha, nullptr);
	bool Complete = false;
	// 收回期间直接变形上一帧的卡面，保留封面裁切、字形与分隔线的相对位置。
	if(!Style.m_Closing || m_RenderedId != Snapshot.m_Id)
	{
		if(!Graphics.BeginRenderTarget(m_Target, Style.m_BackgroundColor.WithAlpha(1.0f)))
			return DrawContents(Alpha, nullptr);

		Graphics.MapScreen(Rect.x, Rect.y, Rect.x + Rect.w, Rect.y + Rect.h);
		if(Style.m_Backdrop.IsValid())
		{
			// 把已有模糊底图铺入目标，不额外抓取全屏；无底图时保留配置背景实色。
			const vec4 Uv = QmHudMediaIslandBackdropUv(Rect, Style.m_ScreenRect);
			IGraphics::SRenderTargetDrawParams Backdrop;
			Backdrop.m_X = Rect.x;
			Backdrop.m_Y = Rect.y;
			Backdrop.m_W = Rect.w;
			Backdrop.m_H = Rect.h;
			Backdrop.m_U0 = Uv.x;
			Backdrop.m_V0 = Uv.y;
			Backdrop.m_U1 = Uv.x + Uv.z;
			Backdrop.m_V1 = Uv.y + Uv.w;
			Graphics.DrawRenderTarget(Style.m_Backdrop, Backdrop);
		}
		// 字形按离屏像素密度生成，最终缩回 HUD 时仍有足够的笔画采样。
		const float TextDensity = std::clamp(std::min(m_Width * PixelSize.x / Rect.w, m_Height * PixelSize.y / Rect.h), 1.0f, 2.0f);
		const CUIRect GlyphScreen{Rect.x, Rect.y, TextScreen.w / TextDensity, TextScreen.h / TextDensity};
		Complete = DrawContents(1.0f, &GlyphScreen);
		Graphics.EndRenderTarget();
		Graphics.MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		m_RenderedId = Snapshot.m_Id;
	}
	IGraphics::SRenderTargetDrawParams Draw;
	Draw.m_X = Rect.x;
	Draw.m_Y = Rect.y;
	Draw.m_W = Rect.w;
	Draw.m_H = Rect.h;
	Draw.m_Alpha = Alpha;
	Draw.m_TopPinch = Style.m_Pinch;
	Draw.m_TopShade = Style.m_TopShade;
	Draw.m_EdgeFeather = std::max(PixelSize.x, PixelSize.y) * 1.25f;
	Draw.m_TiltX = MotionLevel > 0 && Style.m_DrawBackground ? Style.m_TiltX : 0.0f;
	Draw.m_TiltY = MotionLevel > 0 && Style.m_DrawBackground ? Style.m_TiltY : 0.0f;
	Draw.m_NativeTextureCoordinates = true;
	Draw.m_Corners = IGraphics::CORNER_ALL;
	Draw.m_Rounding = Style.m_Radius;
	Draw.m_CornerSmoothing = std::clamp(g_Config.m_QmUiCornerSmoothing / 100.0f, 0.0f, 1.0f);
	Draw.m_CornerCurve = static_cast<float>(g_Config.m_QmUiCornerCurve);
	Draw.m_CornerSeam = std::clamp(g_Config.m_QmUiCapsuleSeam / 100.0f, 0.0f, 1.0f);
	Graphics.DrawRenderTarget(m_Target, Draw);
	return Complete;
}
