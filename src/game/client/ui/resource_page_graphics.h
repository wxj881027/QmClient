/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_RESOURCE_PAGE_GRAPHICS_H
#define GAME_CLIENT_UI_RESOURCE_PAGE_GRAPHICS_H

#include "resource_page_loader.h"

#include <engine/graphics.h>

#include <string>
#include <vector>

class CResourcePageGraphics final : public IResourcePageSink
{
	struct STexture
	{
		std::string m_PageId;
		std::string m_Path;
		IGraphics::CTextureHandle m_Handle;
		bool m_Ready = false;
	};
	IGraphics *m_pGraphics = nullptr;
	std::vector<STexture> m_vTextures;

public:
	explicit CResourcePageGraphics(IGraphics *pGraphics) :
		m_pGraphics(pGraphics) {}
	~CResourcePageGraphics();

	CResourcePageGraphics(const CResourcePageGraphics &) = delete;
	CResourcePageGraphics &operator=(const CResourcePageGraphics &) = delete;

	bool Upload(const char *pPageId, SResourcePageArtifact &&Artifact) override;
	void Finish(const char *pPageId, bool Success) override;
	IGraphics::CTextureHandle Find(const char *pPageId, const char *pPath) const;
	void ClearPage(const char *pPageId);
	void Clear();
};

#endif
