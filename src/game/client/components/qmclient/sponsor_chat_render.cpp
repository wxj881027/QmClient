#include "sponsor_chat_render.h"

void QmSponsorChatAddPlatinumSplits(CTextCursor &Cursor, const char *pText, float Alpha)
{
	int Count = 0;
	for(const char *pCurrent = pText; *pCurrent;)
	{
		const int Codepoint = str_utf8_decode(&pCurrent);
		if(Codepoint <= 0)
			break;
		if(Codepoint != '\n')
			++Count;
	}
	if(Count == 0)
		return;
	const int Start = Cursor.m_CharCount;
	int Character = 0;
	for(const char *pCurrent = pText; *pCurrent;)
	{
		const char *pNext = pCurrent;
		const int Codepoint = str_utf8_decode(&pNext);
		if(Codepoint <= 0)
			break;
		if(Codepoint != '\n')
		{
			const float Position = Count > 1 ? float(Character) / float(Count - 1) : 0.64f;
			Cursor.m_vColorSplits.emplace_back(Start + int(pCurrent - pText), int(pNext - pCurrent), QmSponsorChatPlatinumColor(Position, Alpha));
			++Character;
		}
		pCurrent = pNext;
	}
}

void QmRenderSponsorChatText(ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, float Alpha, vec2 PixelSize, float X, float Y, bool DrawGlow)
{
	if(!Index.Valid() || Alpha <= 0.0f)
		return;
	const ColorRGBA Empty(0.0f, 0.0f, 0.0f, 0.0f);
	if(Style == EQmSponsorChatStyle::SOFT_GLOW && DrawGlow)
	{
		// 无模糊能力时只画一圈低强度辉光，避免复制字形形成重影。
		static constexpr vec2 s_aDirections[] = {
			vec2(1.0f, 0.0f), vec2(-1.0f, 0.0f), vec2(0.0f, 1.0f), vec2(0.0f, -1.0f),
			vec2(1.0f, 1.0f), vec2(-1.0f, 1.0f), vec2(1.0f, -1.0f), vec2(-1.0f, -1.0f)};
		for(const vec2 &Direction : s_aDirections)
			pTextRender->RenderTextContainer(Index, Empty, ColorRGBA(1.0f, 1.0f, 1.0f, 0.025f * Alpha), X + Direction.x * PixelSize.x, Y + Direction.y * PixelSize.y);
	}
	if(Style == EQmSponsorChatStyle::PLATINUM)
		pTextRender->RenderTextContainer(Index, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha), Empty, X, Y - PixelSize.y);
	const ColorRGBA Outline = Style == EQmSponsorChatStyle::NONE ? pTextRender->DefaultTextOutlineColor() : ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f);
	pTextRender->RenderTextContainer(Index, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), Outline.WithMultipliedAlpha(Alpha), X, Y);
}

void CQmSponsorChatRenderer::Reset(IGraphics *pGraphics)
{
	const uint32_t Version = pGraphics->GraphicsResourcesResetVersion();
	if(m_GraphicsVersion == Version)
	{
		pGraphics->DestroyRenderTarget(&m_Source);
		for(auto &Target : m_aTemporary)
			pGraphics->DestroyRenderTarget(&Target);
		pGraphics->DestroyRenderTarget(&m_Blurred);
	}
	else
	{
		// 设备重建后旧目标已经释放，不得用旧编号销毁新设备的资源。
		m_Source.Invalidate();
		for(auto &Target : m_aTemporary)
			Target.Invalidate();
		m_Blurred.Invalidate();
	}
	m_GraphicsVersion = Version;
	m_Width = 0;
	m_Height = 0;
}

bool CQmSponsorChatRenderer::RenderGlow(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, const CUIRect &Bounds, float RadiusPx, float Alpha, vec2 PixelSize, float X, float Y)
{
	if(!pGraphics->IsRenderTargetGaussianBlurSupported())
	{
		if(m_Source.IsValid())
			Reset(pGraphics);
		return false;
	}
	const int Padding = int(std::ceil(RadiusPx)) + 2;
	const float Left = std::floor(Bounds.x / PixelSize.x) * PixelSize.x - Padding * PixelSize.x;
	const float Top = std::floor(Bounds.y / PixelSize.y) * PixelSize.y - Padding * PixelSize.y;
	const int Width = int(std::ceil((Bounds.x + Bounds.w - Left) / PixelSize.x)) + Padding;
	const int Height = int(std::ceil((Bounds.y + Bounds.h - Top) / PixelSize.y)) + Padding;
	// 三张共享工作纹理按需增长，不为每条消息持有一套模糊资源。
	// 超长消息超过工作纹理预算时仍完整绘制正文，并使用轻量辉光。
	if(Width <= 0 || Height <= 0 || Width > 2048 || Height > 1024)
		return false;
	if(Width > m_Width || Height > m_Height || !m_Source.IsValid() || !m_aTemporary[0].IsValid() || !m_Blurred.IsValid())
	{
		const int NewWidth = std::max(m_Width, (Width + 63) / 64 * 64);
		const int NewHeight = std::max(m_Height, (Height + 31) / 32 * 32);
		Reset(pGraphics);
		m_Source = pGraphics->CreateRenderTarget(NewWidth, NewHeight);
		m_aTemporary[0] = pGraphics->CreateRenderTarget(NewWidth, NewHeight);
		m_Blurred = pGraphics->CreateRenderTarget(NewWidth, NewHeight);
		if(!m_Source.IsValid() || !m_aTemporary[0].IsValid() || !m_Blurred.IsValid())
		{
			Reset(pGraphics);
			return false;
		}
		m_Width = NewWidth;
		m_Height = NewHeight;
	}
	if(!pGraphics->BeginRenderTarget(m_Source, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)))
		return false;
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	pGraphics->MapScreen(Left, Top, Left + m_Width * PixelSize.x, Top + m_Height * PixelSize.y);
	// 轮廓也进入遮罩，使柔光能越过正文的深色描边。
	pTextRender->RenderTextContainer(Index, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f));
	pGraphics->EndRenderTarget();
	pGraphics->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
	IGraphics::SGaussianBlurParams Params;
	Params.m_Radius = int(std::ceil(RadiusPx));
	Params.m_Sigma = std::max(0.5f, RadiusPx * 0.55f);
	if(!pGraphics->GaussianBlurRenderTarget(m_Source, m_aTemporary, m_Blurred, Params))
		return false;
	IGraphics::SRenderTargetDrawParams Draw;
	Draw.m_X = Left + std::round(X / PixelSize.x) * PixelSize.x;
	Draw.m_Y = Top + std::round(Y / PixelSize.y) * PixelSize.y;
	Draw.m_W = m_Width * PixelSize.x;
	Draw.m_H = m_Height * PixelSize.y;
	Draw.m_Alpha = 0.18f * Alpha;
	pGraphics->BlendAdditive();
	pGraphics->DrawRenderTarget(m_Blurred, Draw);
	pGraphics->BlendNormal();
	return true;
}

void CQmSponsorChatRenderer::Render(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, const CUIRect &Bounds, float FontSize, float Alpha, float X, float Y)
{
	if(!Index.Valid() || Alpha <= 0.0f)
		return;
	if(m_GraphicsVersion != pGraphics->GraphicsResourcesResetVersion())
		Reset(pGraphics);
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const vec2 PixelSize((ScreenX1 - ScreenX0) / std::max(1, pGraphics->ScreenWidth()), (ScreenY1 - ScreenY0) / std::max(1, pGraphics->ScreenHeight()));
	bool GlowRendered = false;
	if(Style == EQmSponsorChatStyle::SOFT_GLOW && PixelSize.x > 0.0f && PixelSize.y > 0.0f)
	{
		const float Radius = QmSponsorChatGlowRadius(FontSize / PixelSize.y);
		if(Radius > 0.0f)
			GlowRendered = RenderGlow(pGraphics, pTextRender, Index, Bounds, Radius, Alpha, PixelSize, X, Y);
	}
	QmRenderSponsorChatText(pTextRender, Index, Style, Alpha, PixelSize, X, Y, !GlowRendered);
}
