#include "qm_title_badge.h"

#include <engine/storage.h>

void CQmTitleBadge::Init(IGraphics *pGraphics)
{
	Shutdown(pGraphics);
	m_LogoGraphicsVersion = pGraphics->GraphicsResourcesResetVersion();
	m_Logo.Init(pGraphics);
	for(auto &Pattern : m_aPatterns)
		Pattern.Init(pGraphics);
	CImageInfo Image;
	if(pGraphics->LoadPng(Image, "qmclient/title_badges/logo_sdf.png", IStorage::TYPE_ALL))
	{
		// 距离值不能经过普通颜色 mipmap 平均，否则小尺寸下耳朵和眼睛的轮廓会消失。
		m_LogoDistance = pGraphics->LoadTextureRawMove(Image, IGraphics::TEXLOAD_NO_MIPMAPS, "qm_title_logo_sdf");
	}
}

void CQmTitleBadge::Shutdown(IGraphics *pGraphics)
{
	m_Logo.Shutdown(pGraphics);
	for(auto &Pattern : m_aPatterns)
		Pattern.Shutdown(pGraphics);
	if(m_LogoDistance.IsValid())
		pGraphics->UnloadTexture(&m_LogoDistance);
}

bool CQmTitleBadge::RenderDistanceLogo(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color) const
{
	if(!m_LogoDistance.IsValid() || m_LogoGraphicsVersion != pGraphics->GraphicsResourcesResetVersion() ||
		!pGraphics->HasRoundedRectSdf() || !pGraphics->HasRoundedRectSdfTexture())
		return false;
	float X0, Y0, X1, Y1;
	pGraphics->GetScreen(&X0, &Y0, &X1, &Y1);
	if(pGraphics->ScreenWidth() <= 0 || pGraphics->ScreenHeight() <= 0 || X1 <= X0 || Y1 <= Y0)
		return false;

	// 与 generate_title_logo_sdf.cjs 一致：16 单位原图外留 2 单位，编码范围为 [-2, 2]。
	const float Padding = Rect.w * (2.0f / 16.0f);
	const CUIRect Field{Rect.x - Padding, Rect.y - Padding, Rect.w + Padding * 2.0f, Rect.h + Padding * 2.0f};
	IGraphics::SRoundedRectSdfParams Params;
	Params.m_Rect = vec4(Field.x, Field.y, Field.w, Field.h);
	Params.m_FillColor = vec4(Color.r, Color.g, Color.b, Color.a);
	Params.m_Params.y = std::max((X1 - X0) / pGraphics->ScreenWidth(), (Y1 - Y0) / pGraphics->ScreenHeight());
	Params.m_TextureParams0 = vec4(IGraphics::ROUNDED_RECT_SDF_TEXTURE_DISTANCE, 1.0f / Field.w, 1.0f / Field.h, 0.5f);
	Params.m_TextureParams1 = vec4(0.5f, 1.0f, 4.0f / 20.0f, 0.0f);
	pGraphics->TextureSet(m_LogoDistance);
	pGraphics->RenderRoundedRectSdf(Params);
	pGraphics->TextureClear();
	return true;
}

void CQmTitleBadge::RenderLogo(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color) const
{
	if(!RenderDistanceLogo(pGraphics, Rect, Color))
		m_Logo.Render(pGraphics, Rect, Color);
}

void CQmTitleBadge::RenderSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmTitleAppearance &Appearance, float Alpha) const
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f || Appearance.m_Opacity <= 0)
		return;
	const float Scale = Rect.h / 24.0f;
	const SQmTitleBadgePalette Palette = QmTitleBadgePalette(Appearance, Alpha);
	const auto DrawPill = [&](const CUIRect &Area, ColorRGBA Color) {
		pGraphics->DrawRect(Area.x, Area.y, Area.w, Area.h, Color, IGraphics::CORNER_ALL, Area.h * 0.5f);
	};
	CUIRect Shadow = Rect;
	Shadow.y += Scale;
	DrawPill(Shadow, Palette.m_Background.WithMultipliedAlpha(0.05f));
	QmDrawBadgeCapsuleGeometry(pGraphics, Rect, Palette.m_Background, Palette.m_Border);

	// 两端保持图标比例，中间只伸展星轨；日月图案继续使用离线展开的几何网格。
	const float RightWidth = std::min(32.0f * Scale, Rect.w);
	const float LeftWidth = std::min(40.0f * Scale, Rect.w - RightWidth);
	const int Pattern = Appearance.m_Style == EQmTitleBadgeStyle::SUN ? 3 : 0;
	const ColorRGBA PatternColor = Palette.m_Icon.WithMultipliedAlpha(0.20f);
	m_aPatterns[Pattern].Render(pGraphics, {Rect.x, Rect.y, 40.0f * Scale, Rect.h}, PatternColor, LeftWidth / Scale);
	m_aPatterns[Pattern + 1].Render(pGraphics, {Rect.x + LeftWidth, Rect.y, Rect.w - LeftWidth - RightWidth, Rect.h}, PatternColor);
	m_aPatterns[Pattern + 2].Render(pGraphics, {Rect.x + Rect.w - RightWidth, Rect.y, RightWidth, Rect.h}, PatternColor);
	const CUIRect Logo{Rect.x + 9.0f * Scale, Rect.y + 4.0f * Scale, 16.0f * Scale, 16.0f * Scale};
	RenderLogo(pGraphics, Logo, Palette.m_Icon);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
