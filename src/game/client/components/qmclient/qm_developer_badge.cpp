#include "qm_developer_badge.h"

#include <engine/storage.h>

void CQmDeveloperBadge::Init(IGraphics *pGraphics)
{
	Shutdown(pGraphics);
	CImageInfo Image;
	if(pGraphics->LoadPng(Image, "qmclient/developer_badge.png", IStorage::TYPE_ALL))
		m_Texture = pGraphics->LoadTextureRawMove(Image, 0, "qm_developer_badge");
}

void CQmDeveloperBadge::Shutdown(IGraphics *pGraphics)
{
	if(m_Texture.IsValid())
		pGraphics->UnloadTexture(&m_Texture);
}

void CQmDeveloperBadge::Render(IGraphics *pGraphics, const CUIRect &Rect, float Alpha) const
{
	if(!Available() || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f)
		return;
	pGraphics->TextureSet(m_Texture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, std::clamp(Alpha, 0.0f, 1.0f));
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
