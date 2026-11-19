#ifndef GAME_CLIENT_QM_ICON_FONT_RENDER_H
#define GAME_CLIENT_QM_ICON_FONT_RENDER_H

#include <engine/textrender.h>

// 即时图标以白色顶点建容器，颜色与保护在最终绘制时传入；正文 TextEx 语义不变。
// 模板只替换绘制设备边界，允许测试观察真实调用参数和生命周期。
template<typename TTextRender>
void QmRenderImmediateFontIcon(TTextRender &TextRender, CTextCursor *pCursor, const char *pText, int Length, const ColorRGBA &Color, const ColorRGBA &Protection)
{
	struct SRestoreState
	{
		TTextRender &m_TextRender;
		CTextCursor *m_pCursor;
		ColorRGBA m_Color;
		ColorRGBA m_Outline;
		unsigned m_Flags;
		std::vector<STextColorSplit> m_vColorSplits;

		SRestoreState(TTextRender &Render, CTextCursor *pCursor) :
			m_TextRender(Render), m_pCursor(pCursor), m_Color(Render.GetTextColor()), m_Outline(Render.GetTextOutlineColor()), m_Flags(Render.GetRenderFlags())
		{
			// 交换保留原色段及容量，构造临时句柄失败时也能无申请地恢复。
			m_vColorSplits.swap(m_pCursor->m_vColorSplits);
		}
		void Restore()
		{
			if(m_pCursor == nullptr)
				return;
			m_vColorSplits.swap(m_pCursor->m_vColorSplits);
			m_TextRender.SetRenderFlags(m_Flags);
			m_TextRender.TextColor(m_Color);
			m_TextRender.TextOutlineColor(m_Outline);
			m_pCursor = nullptr;
		}
		~SRestoreState() { Restore(); }
		SRestoreState(const SRestoreState &) = delete;
		SRestoreState &operator=(const SRestoreState &) = delete;
	} State(TextRender, pCursor);

	TextRender.TextColor(ColorRGBA(1, 1, 1, 1));
	TextRender.TextOutlineColor(ColorRGBA(1, 1, 1, 1));
	TextRender.SetRenderFlags(State.m_Flags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	STextContainerIndex Container;
	try
	{
		TextRender.CreateTextContainer(Container, pCursor, pText, Length);
		State.Restore();
		if(Container.Valid() && (pCursor->m_Flags & TEXTFLAG_RENDER) != 0)
			TextRender.RenderTextContainer(Container, Color, Protection);
		TextRender.DeleteTextContainer(Container);
	}
	catch(...)
	{
		// 删除本身也可能失败；只转交已有槽位，不能在展开中再次提交 GPU 清理。
		TextRender.ReleaseTemporaryTextContainer(Container);
		throw;
	}
}

#endif
