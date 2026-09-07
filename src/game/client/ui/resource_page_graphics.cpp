/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "resource_page_graphics.h"

bool CResourcePageGraphics::Upload(const char *pPageId, SResourcePageArtifact &&Artifact)
{
	if(!m_pGraphics || !pPageId || Artifact.m_Image.m_pData == nullptr)
		return false;
	auto Texture = m_pGraphics->LoadTextureRawMove(Artifact.m_Image, 0, Artifact.m_Path.c_str());
	if(!Texture.IsValid() || Texture.IsNullTexture())
		return false;
	m_vTextures.push_back({pPageId, Artifact.m_Path, Texture, false});
	return true;
}

void CResourcePageGraphics::Finish(const char *pPageId, bool Success)
{
	if(!pPageId)
		return;
	if(!Success)
	{
		ClearPage(pPageId);
		return;
	}
	for(auto &Texture : m_vTextures)
		if(Texture.m_PageId == pPageId)
			Texture.m_Ready = true;
}

IGraphics::CTextureHandle CResourcePageGraphics::Find(const char *pPageId, const char *pPath) const
{
	if(pPageId && pPath)
		for(const auto &Texture : m_vTextures)
			if(Texture.m_Ready && Texture.m_PageId == pPageId && Texture.m_Path == pPath)
				return Texture.m_Handle;
	return {};
}

void CResourcePageGraphics::ClearPage(const char *pPageId)
{
	for(auto It = m_vTextures.begin(); It != m_vTextures.end();)
	{
		if(!pPageId || It->m_PageId == pPageId)
		{
			if(m_pGraphics)
				m_pGraphics->UnloadTexture(&It->m_Handle);
			It = m_vTextures.erase(It);
		}
		else
			++It;
	}
}

void CResourcePageGraphics::Clear()
{
	ClearPage(nullptr);
}

CResourcePageGraphics::~CResourcePageGraphics()
{
	Clear();
}
