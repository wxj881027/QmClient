#ifndef GAME_CLIENT_QMUI_QMIMERENDERSTATE_H
#define GAME_CLIENT_QMUI_QMIMERENDERSTATE_H

#include <base/color.h>

// 只借用当前绘制作用域内存活的接口；恢复接口仅赋值，不申请资源。
template<typename TGraphics, typename TTextRender>
class CQmImeScreenState
{
	TGraphics &m_Graphics;
	TTextRender &m_TextRender;
	unsigned m_RenderFlags;
	float m_X0, m_Y0, m_X1, m_Y1;

public:
	CQmImeScreenState(TGraphics &Graphics, TTextRender &TextRender) :
		m_Graphics(Graphics), m_TextRender(TextRender), m_RenderFlags(TextRender.GetRenderFlags())
	{
		m_Graphics.GetScreen(&m_X0, &m_Y0, &m_X1, &m_Y1);
	}

	~CQmImeScreenState()
	{
		m_TextRender.SetRenderFlags(m_RenderFlags);
		m_Graphics.MapScreen(m_X0, m_Y0, m_X1, m_Y1);
	}

	CQmImeScreenState(const CQmImeScreenState &) = delete;
	CQmImeScreenState &operator=(const CQmImeScreenState &) = delete;
};

// 颜色单独在文字绘制前捕获，测量/显隐提前退出不额外调用颜色 setter。
template<typename TTextRender>
class CQmImeTextColorState
{
	TTextRender &m_TextRender;
	ColorRGBA m_TextColor;
	ColorRGBA m_OutlineColor;

public:
	explicit CQmImeTextColorState(TTextRender &TextRender) :
		m_TextRender(TextRender), m_TextColor(TextRender.GetTextColor()), m_OutlineColor(TextRender.GetTextOutlineColor())
	{
	}

	~CQmImeTextColorState()
	{
		m_TextRender.TextColor(m_TextColor);
		m_TextRender.TextOutlineColor(m_OutlineColor);
	}

	CQmImeTextColorState(const CQmImeTextColorState &) = delete;
	CQmImeTextColorState &operator=(const CQmImeTextColorState &) = delete;
};

#endif
