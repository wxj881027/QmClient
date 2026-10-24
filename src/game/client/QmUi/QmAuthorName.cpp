#include "QmAuthorName.h"

#include <engine/client.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/qm_sponsor_authors.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

void CQmAuthorName::Reset(IGraphics *pGraphics)
{
	if(m_GraphicsVersion == pGraphics->GraphicsResourcesResetVersion())
	{
		pGraphics->DestroyRenderTarget(&m_Source);
		for(auto &Target : m_aTemporary)
			pGraphics->DestroyRenderTarget(&Target);
		pGraphics->DestroyRenderTarget(&m_Blurred);
	}
	else
	{
		m_Source.Invalidate();
		for(auto &Target : m_aTemporary)
			Target.Invalidate();
		m_Blurred.Invalidate();
	}
	m_GraphicsVersion = pGraphics->GraphicsResourcesResetVersion();
	m_Width = m_Height = 0;
	m_Redacted = false;
}

void CQmAuthorName::Render(CUi *pUi, IGraphics *pGraphics, const QmSponsorAuthors::SAuthor &Author, const CUIRect &Label, float FontSize, bool PointerActive)
{
	if(pUi->DrawingSuppressed() || Label.w <= 0.0f || Label.h <= 0.0f)
		return;
	if(m_GraphicsVersion != pGraphics->GraphicsResourcesResetVersion())
		Reset(pGraphics);
	if(!pUi->AnimationsBlocked())
	{
		const uint64_t Frame = pUi->Client()->PerfFrame();
		if(Frame != m_LastRenderFrame && Frame != m_LastRenderFrame + 1)
			m_Redacted = false;
		m_LastRenderFrame = Frame;
	}
	ITextRender *pTextRender = pUi->TextRender();
	const auto Bounds = [&](const char *pText) {
		const float Width = std::min(Label.w, pTextRender->TextWidth(FontSize, pText));
		return CUIRect{Label.x + (Label.w - Width) * 0.5f, Label.y, Width, Label.h};
	};
	const auto Presentation = QmSponsorAuthors::NamePresentation(Author, Bounds(Author.m_pName), Bounds("[数据删除]"), pUi->MousePos(), PointerActive, m_Redacted);
	if(!pUi->AnimationsBlocked())
		m_Redacted = Presentation.m_Redacted;
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_DisallowNewline = true;
	Props.m_StopAtEnd = true;
	const auto DrawLabel = [&] { pUi->DoLabel(&Label, Presentation.m_pText, FontSize, TEXTALIGN_MC, Props); };
	if(Presentation.m_Redacted)
	{
		DrawLabel();
		return;
	}
	float X0, Y0, X1, Y1;
	pGraphics->GetScreen(&X0, &Y0, &X1, &Y1);
	const vec2 PixelSize((X1 - X0) / std::max(pGraphics->ScreenWidth(), 1), (Y1 - Y0) / std::max(pGraphics->ScreenHeight(), 1));
	const float Radius = FontSize * (0.035f + 0.20f * Presentation.m_Blur);
	// 固定最大留白并对齐屏幕像素，鼠标距离只改变卷积核，不改变文字纹理的位置。
	const float Padding = FontSize * (0.035f + 0.20f) * 3.0f + std::max(PixelSize.x, PixelSize.y) * 2.0f;
	const CUIRect Name = Bounds(Author.m_pName);
	CUIRect Area{Name.x - Padding, Label.y - Padding, 0.0f, 0.0f};
	if(PixelSize.x > 0.0f)
		Area.x = X0 + std::floor((Area.x - X0) / PixelSize.x) * PixelSize.x;
	if(PixelSize.y > 0.0f)
		Area.y = Y0 + std::floor((Area.y - Y0) / PixelSize.y) * PixelSize.y;
	Area.w = Name.x + Name.w + Padding - Area.x;
	Area.h = Label.y + Label.h + Padding - Area.y;
	const int Width = PixelSize.x > 0.0f ? static_cast<int>(std::ceil(Area.w / PixelSize.x)) : 0;
	const int Height = PixelSize.y > 0.0f ? static_cast<int>(std::ceil(Area.h / PixelSize.y)) : 0;
	// 先按屏幕的字号与像素比例排版；小纹理的投影只用于绘制已有字形。
	CUIElement::SUIElementRect Text;
	pUi->DoLabel(Text, &Label, Presentation.m_pText, FontSize, TEXTALIGN_MC, Props);
	pUi->FlushQuadBatch();
	bool Rendered = false;
	if(pGraphics->IsRenderTargetGaussianBlurSupported() && Width > 0 && Height > 0 && Width <= 1024 && Height <= 512)
	{
		if(Width > m_Width || Height > m_Height || !m_Source.IsValid() || !m_aTemporary[0].IsValid() || !m_Blurred.IsValid())
		{
			const int NewWidth = std::max(m_Width, (Width + 31) / 32 * 32);
			const int NewHeight = std::max(m_Height, (Height + 15) / 16 * 16);
			Reset(pGraphics);
			m_Redacted = Presentation.m_Redacted;
			m_Width = NewWidth;
			m_Height = NewHeight;
			m_Source = pGraphics->CreateRenderTarget(m_Width, m_Height);
			m_aTemporary[0] = pGraphics->CreateRenderTarget(m_Width, m_Height);
			m_Blurred = pGraphics->CreateRenderTarget(m_Width, m_Height);
		}
		if(m_Source.IsValid() && m_aTemporary[0].IsValid() && m_Blurred.IsValid() && pGraphics->BeginRenderTarget(m_Source, ColorRGBA(0, 0, 0, 0)))
		{
			pGraphics->MapScreen(Area.x, Area.y, Area.x + m_Width * PixelSize.x, Area.y + m_Height * PixelSize.y);
			pTextRender->RenderTextContainer(Text.m_UITextContainer, Text.m_TextColor, Text.m_TextOutlineColor);
			pGraphics->EndRenderTarget();
			pGraphics->MapScreen(X0, Y0, X1, Y1);
			IGraphics::SGaussianBlurParams Params;
			Params.m_Radius = std::clamp(static_cast<int>(std::ceil(Radius * 3.0f / PixelSize.y)), 1, IGraphics::GAUSSIAN_BLUR_MAX_RADIUS);
			Params.m_Sigma = std::max(0.5f, Radius / PixelSize.y);
			if(pGraphics->GaussianBlurRenderTarget(m_Source, m_aTemporary, m_Blurred, Params))
			{
				IGraphics::SRenderTargetDrawParams Draw;
				Draw.m_X = Area.x;
				Draw.m_Y = Area.y;
				Draw.m_W = m_Width * PixelSize.x;
				Draw.m_H = m_Height * PixelSize.y;
				pGraphics->DrawRenderTarget(m_Blurred, Draw);
				Rendered = true;
			}
		}
	}
	if(!Rendered)
	{
		// 回退也复用同一份排版，避免每个采样点独立像素取整后让名字偏移。
		for(int Y = -1; Y <= 1; ++Y)
			for(int X = -1; X <= 1; ++X)
			{
				pGraphics->MapScreen(X0 - X * Radius, Y0 - Y * Radius, X1 - X * Radius, Y1 - Y * Radius);
				pTextRender->RenderTextContainer(Text.m_UITextContainer, Text.m_TextColor.WithMultipliedAlpha(1.0f / 9.0f), Text.m_TextOutlineColor.WithMultipliedAlpha(1.0f / 9.0f));
			}
		pGraphics->MapScreen(X0, Y0, X1, Y1);
	}
	pTextRender->DeleteTextContainer(Text.m_UITextContainer);
}
