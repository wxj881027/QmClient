#ifndef GAME_CLIENT_QMUI_UITEXTRENDERSTATE_H
#define GAME_CLIENT_QMUI_UITEXTRENDERSTATE_H

#include <engine/textrender.h>

// 仅借用当前绘制接口，保存和恢复只读写现有状态，不申请资源。
template<typename TTextRender>
class CUiScopedTextRenderState
{
	TTextRender *m_pTextRender;
	ColorRGBA m_TextColor;
	ColorRGBA m_OutlineColor;
	ColorRGBA m_SelectionColor;
	unsigned m_RenderFlags;
	EFontPreset m_FontPreset;

public:
	explicit CUiScopedTextRenderState(TTextRender &TextRender) :
		m_pTextRender(&TextRender), m_TextColor(TextRender.GetTextColor()), m_OutlineColor(TextRender.GetTextOutlineColor()), m_SelectionColor(TextRender.GetTextSelectionColor()), m_RenderFlags(TextRender.GetRenderFlags()), m_FontPreset(TextRender.GetFontPreset())
	{
	}

	// 可在原绘制阶段提前恢复；之后的绘制状态不再由本对象接管。
	void Restore()
	{
		if(m_pTextRender == nullptr)
			return;
		m_pTextRender->SetRenderFlags(m_RenderFlags);
		m_pTextRender->SetFontPreset(m_FontPreset);
		m_pTextRender->TextOutlineColor(m_OutlineColor);
		m_pTextRender->TextSelectionColor(m_SelectionColor);
		m_pTextRender->TextColor(m_TextColor);
		m_pTextRender = nullptr;
	}

	~CUiScopedTextRenderState() { Restore(); }
	CUiScopedTextRenderState(const CUiScopedTextRenderState &) = delete;
	CUiScopedTextRenderState &operator=(const CUiScopedTextRenderState &) = delete;
};

#endif
