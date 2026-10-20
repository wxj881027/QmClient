#include "qm_title_badge.h"

#include <engine/storage.h>

void CQmTitleBadge::Init(IGraphics *pGraphics)
{
	Shutdown(pGraphics);
	const auto Load = [&](const char *pPath, IGraphics::CTextureHandle &Texture) {
		CImageInfo Image;
		if(pGraphics->LoadPng(Image, pPath, IStorage::TYPE_ALL))
			Texture = pGraphics->LoadTextureRawMove(Image, 0, pPath);
	};
	Load("qmclient/title_badges/logo.png", m_Logo);
	Load("qmclient/title_badges/moon.png", m_aPatterns[0]);
	Load("qmclient/title_badges/sun.png", m_aPatterns[1]);
}

void CQmTitleBadge::Shutdown(IGraphics *pGraphics)
{
	if(m_Logo.IsValid())
		pGraphics->UnloadTexture(&m_Logo);
	for(auto &Pattern : m_aPatterns)
		if(Pattern.IsValid())
			pGraphics->UnloadTexture(&Pattern);
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
	DrawPill(Rect, Palette.m_Border);
	CUIRect Inside;
	Rect.Margin(Scale, &Inside);
	DrawPill(Inside, Palette.m_Background);
	const CUIRect Glint{Inside.x + Inside.h * 0.5f, Inside.y, std::max(0.0f, Inside.w - Inside.h), Scale};
	pGraphics->DrawRect(Glint.x, Glint.y, Glint.w, Glint.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.23f * Palette.m_Background.a));

	const auto Draw = [&](IGraphics::CTextureHandle Texture, const CUIRect &Area, ColorRGBA Color, float U0, float U1) {
		if(!Texture.IsValid() || Area.w <= 0.0f)
			return;
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->QuadsSetRotation(0.0f);
		pGraphics->QuadsSetSubset(U0, 0.0f, U1, 1.0f);
		pGraphics->SetColor(Color);
		IGraphics::CQuadItem Quad(Area.x, Area.y, Area.w, Area.h);
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsEnd();
	};
	// 两端保持图标比例；中间只伸展星轨，文字长度不会拉宽月亮或太阳。
	const float RightWidth = std::min(32.0f * Scale, Rect.w);
	const float LeftWidth = std::min(40.0f * Scale, Rect.w - RightWidth);
	const auto Pattern = m_aPatterns[Appearance.m_Style == EQmTitleBadgeStyle::SUN ? 1 : 0];
	const ColorRGBA PatternColor = Palette.m_Icon.WithMultipliedAlpha(0.20f);
	Draw(Pattern, {Rect.x, Rect.y, LeftWidth, Rect.h}, PatternColor, 0.0f, LeftWidth / (117.0f * Scale));
	Draw(Pattern, {Rect.x + LeftWidth, Rect.y, Rect.w - LeftWidth - RightWidth, Rect.h}, PatternColor, 40.0f / 117.0f, 85.0f / 117.0f);
	Draw(Pattern, {Rect.x + Rect.w - RightWidth, Rect.y, RightWidth, Rect.h}, PatternColor, 85.0f / 117.0f, 1.0f);
	Draw(m_Logo, {Rect.x + 9.0f * Scale, Rect.y + 4.0f * Scale, 16.0f * Scale, 16.0f * Scale}, Palette.m_Icon, 0.0f, 1.0f);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
