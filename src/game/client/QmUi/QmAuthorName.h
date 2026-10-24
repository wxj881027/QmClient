#ifndef GAME_CLIENT_QMUI_QMAUTHORNAME_H
#define GAME_CLIENT_QMUI_QMAUTHORNAME_H

#include <engine/graphics.h>

#include <array>

namespace QmSponsorAuthors
{
	struct SAuthor;
}

class CUi;
class CUIRect;

// 只持有夏日名字使用的小尺寸工作纹理，图形接口仍存活时由菜单释放。
class CQmAuthorName
{
	IGraphics::CRenderTargetHandle m_Source;
	std::array<IGraphics::CRenderTargetHandle, IGraphics::DUAL_KAWASE_PYRAMID_LEVELS> m_aTemporary;
	IGraphics::CRenderTargetHandle m_Blurred;
	uint32_t m_GraphicsVersion = 0;
	int m_Width = 0;
	int m_Height = 0;
	bool m_Redacted = false;
	uint64_t m_LastRenderFrame = 0;

public:
	CQmAuthorName() = default;
	CQmAuthorName(const CQmAuthorName &) = delete;
	CQmAuthorName &operator=(const CQmAuthorName &) = delete;
	void Reset(IGraphics *pGraphics);
	void Render(CUi *pUi, IGraphics *pGraphics, const QmSponsorAuthors::SAuthor &Author, const CUIRect &Label, float FontSize, bool PointerActive);
};

#endif
