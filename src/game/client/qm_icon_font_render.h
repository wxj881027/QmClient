#ifndef GAME_CLIENT_QM_ICON_FONT_RENDER_H
#define GAME_CLIENT_QM_ICON_FONT_RENDER_H

#include <engine/textrender.h>

// 即时图标以白色顶点建容器，颜色与保护在最终绘制时传入；正文 TextEx 语义不变。
// 模板只替换绘制设备边界，允许测试观察真实调用参数和生命周期。
template<typename TTextRender>
void QmRenderImmediateFontIcon(TTextRender &TextRender, CTextCursor *pCursor, const char *pText, int Length, const ColorRGBA &Color, const ColorRGBA &Protection)
{
	const ColorRGBA PreviousColor = TextRender.GetTextColor();
	const ColorRGBA PreviousOutline = TextRender.GetTextOutlineColor();
	const unsigned PreviousFlags = TextRender.GetRenderFlags();
	TextRender.TextColor(ColorRGBA(1, 1, 1, 1));
	TextRender.TextOutlineColor(ColorRGBA(1, 1, 1, 1));
	TextRender.SetRenderFlags(PreviousFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	// 整段是图标：正文分段颜色不得再次乘进白色顶点；交换保留调用方数据且不分配。
	std::vector<STextColorSplit> SavedColorSplits;
	SavedColorSplits.swap(pCursor->m_vColorSplits);
	STextContainerIndex Container;
	TextRender.CreateTextContainer(Container, pCursor, pText, Length);
	SavedColorSplits.swap(pCursor->m_vColorSplits);
	TextRender.SetRenderFlags(PreviousFlags);
	TextRender.TextColor(PreviousColor);
	TextRender.TextOutlineColor(PreviousOutline);
	if(Container.Valid() && (pCursor->m_Flags & TEXTFLAG_RENDER) != 0)
		TextRender.RenderTextContainer(Container, Color, Protection);
	TextRender.DeleteTextContainer(Container);
}

#endif
