#include "qm_title_badge.h"

void CQmTitleBadge::Init(IGraphics *pGraphics)
{
	m_Logo.Init(pGraphics);
	for(auto &Pattern : m_aPatterns)
		Pattern.Init(pGraphics);
}

void CQmTitleBadge::Shutdown(IGraphics *pGraphics)
{
	m_Logo.Shutdown(pGraphics);
	for(auto &Pattern : m_aPatterns)
		Pattern.Shutdown(pGraphics);
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

	// 两端保持图标比例，中间只伸展星轨；所有图案使用离线展开的几何网格。
	const float RightWidth = std::min(32.0f * Scale, Rect.w);
	const float LeftWidth = std::min(40.0f * Scale, Rect.w - RightWidth);
	const int Pattern = Appearance.m_Style == EQmTitleBadgeStyle::SUN ? 3 : 0;
	const ColorRGBA PatternColor = Palette.m_Icon.WithMultipliedAlpha(0.20f);
	m_aPatterns[Pattern].Render(pGraphics, {Rect.x, Rect.y, 40.0f * Scale, Rect.h}, PatternColor, LeftWidth / Scale);
	m_aPatterns[Pattern + 1].Render(pGraphics, {Rect.x + LeftWidth, Rect.y, Rect.w - LeftWidth - RightWidth, Rect.h}, PatternColor);
	m_aPatterns[Pattern + 2].Render(pGraphics, {Rect.x + Rect.w - RightWidth, Rect.y, RightWidth, Rect.h}, PatternColor);
	m_Logo.Render(pGraphics, {Rect.x + 9.0f * Scale, Rect.y + 4.0f * Scale, 16.0f * Scale, 16.0f * Scale}, Palette.m_Icon);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
