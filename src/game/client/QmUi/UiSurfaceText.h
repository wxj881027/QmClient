#ifndef GAME_CLIENT_QMUI_UISURFACETEXT_H
#define GAME_CLIENT_QMUI_UISURFACETEXT_H

#include "UiTheme.h"

#include <engine/textrender.h>

// 表面前景仅在组件绘制期间生效，嵌套弹层和提前返回都恢复调用方状态。
class CUiScopedSurfaceText
{
	ITextRender *m_pTextRender;
	ColorRGBA m_PreviousText;
	ColorRGBA m_PreviousOutline;
	ColorRGBA m_PreviousSurface;
	bool m_PreviousSurfaceKnown;
	inline static thread_local bool ms_SurfaceKnown = false;
	inline static thread_local ColorRGBA ms_Surface = ui_token::color::SURFACE_BACKDROP;

public:
	CUiScopedSurfaceText(ITextRender *pTextRender, ColorRGBA Surface, bool Enabled = true) : m_pTextRender(pTextRender), m_PreviousText(pTextRender ? pTextRender->GetTextColor() : ColorRGBA()), m_PreviousOutline(pTextRender ? pTextRender->GetTextOutlineColor() : ColorRGBA()), m_PreviousSurface(ms_Surface), m_PreviousSurfaceKnown(ms_SurfaceKnown)
	{
		if(Enabled)
		{
			// 嵌套控件读取父表面，透明按钮不会退回固定深色背板。
			ms_Surface = CompositeUiSurface(Surface, m_PreviousSurface);
			// 半透明层只有父背景已知才可确定合成颜色；地图上的透明覆盖仍须弱保护。
			ms_SurfaceKnown = m_PreviousSurfaceKnown || Surface.a >= 1.0f;
			if(pTextRender)
			{
				const ColorRGBA Foreground = ResolveConfiguredTextColor(ms_Surface);
				pTextRender->TextColor(Foreground.WithAlpha(m_PreviousText.a));
				pTextRender->TextOutlineColor(ResolveUiSurfaceForeground(Foreground).WithAlpha(m_PreviousOutline.a));
			}
		}
	}
	static ColorRGBA CurrentSurface() { return ms_Surface; }
	static bool HasKnownSurface() { return ms_SurfaceKnown; }
	ColorRGBA Surface() const { return CurrentSurface(); }
	~CUiScopedSurfaceText()
	{
		ms_Surface = m_PreviousSurface;
		ms_SurfaceKnown = m_PreviousSurfaceKnown;
		if(m_pTextRender)
		{
			m_pTextRender->TextColor(m_PreviousText);
			m_pTextRender->TextOutlineColor(m_PreviousOutline);
		}
	}
	CUiScopedSurfaceText(const CUiScopedSurfaceText &) = delete;
	CUiScopedSurfaceText &operator=(const CUiScopedSurfaceText &) = delete;
};

#endif
