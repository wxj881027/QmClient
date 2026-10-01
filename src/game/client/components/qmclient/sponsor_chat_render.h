#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_RENDER_H

#include "sponsor_chat_style.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui_rect.h>

void QmSponsorChatAddPlatinumSplits(CTextCursor &Cursor, const char *pText, float Alpha);
void QmRenderSponsorChatText(ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, float Alpha, vec2 PixelSize, float X, float Y, bool DrawGlow = true);

class CQmSponsorChatRenderer
{
	IGraphics::CRenderTargetHandle m_Source;
	std::array<IGraphics::CRenderTargetHandle, IGraphics::DUAL_KAWASE_PYRAMID_LEVELS> m_aTemporary;
	IGraphics::CRenderTargetHandle m_Blurred;
	int m_Width = 0;
	int m_Height = 0;
	uint32_t m_GraphicsVersion = 0;

	bool RenderGlow(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, const CUIRect &Bounds, float RadiusPx, float Alpha, vec2 PixelSize, float X, float Y);

public:
	CQmSponsorChatRenderer() = default;
	CQmSponsorChatRenderer(const CQmSponsorChatRenderer &) = delete;
	CQmSponsorChatRenderer &operator=(const CQmSponsorChatRenderer &) = delete;
	void Reset(IGraphics *pGraphics);
	void Render(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, const CUIRect &Bounds, float FontSize, float Alpha, float X, float Y);
};

#endif
