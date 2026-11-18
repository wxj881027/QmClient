#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARYSURFACE_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARYSURFACE_H

#include "QmFinishSummary.h"

#include <engine/graphics.h>

namespace qm_finish_summary
{
	struct SSurfaceStyle
	{
		float m_Alpha = 1.0f;
		float m_ContentAlpha = 1.0f;
		float m_TiltX = 0.0f;
		float m_TiltY = 0.0f;
		float m_Radius = 0.0f;
		float m_Pinch = 0.0f;
		float m_TopShade = 0.0f;
		bool m_DrawBackground = true;
		bool m_Closing = false;
		IGraphics::CTextureHandle m_CoverTexture;
		IGraphics::CRenderTargetHandle m_Backdrop;
		CUIRect m_ScreenRect{};
		ColorRGBA m_BackgroundColor{0.0f, 0.0f, 0.0f, 0.7f};
	};

	class CSurface
	{
		IGraphics::CRenderTargetHandle m_Target;
		uint64_t m_GraphicsVersion = 0;
		uint64_t m_RenderedId = 0;
		int m_Width = 0;
		int m_Height = 0;

	public:
		CSurface() = default;
		CSurface(const CSurface &) = delete;
		CSurface &operator=(const CSurface &) = delete;
		// HUD 在释放图形资源前调用，避免析构阶段访问已经销毁的引擎接口。
		void Reset(IGraphics &Graphics);
		bool Render(IGraphics &Graphics, ITextRender &TextRender, CRenderTools &RenderTools, const CUIRect &Rect,
			const QmFinishSummary::SSnapshot &Snapshot, const CManagedTeeRenderInfo *pTeeRenderInfo, CContentCache &Content,
			const SSurfaceStyle &Style, double NowSeconds, bool FullyExpanded, int MotionLevel);
	};
}

#endif
