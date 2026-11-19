#ifndef GAME_CLIENT_QMUI_SETTINGSPARAGRAPH_H
#define GAME_CLIENT_QMUI_SETTINGSPARAGRAPH_H

#include <game/client/ui.h>

#include <algorithm>

struct SSettingsParagraphState
{
	CUIElement m_Element;
	float m_LineSpacing = -1.0f;
};

inline float MeasureSettingsParagraph(ITextRender *pText, const char *pValue, float Width, float FontSize, float LineSpacing)
{
	if(!pText || Width <= 0.0f || FontSize <= 0.0f)
		return 0.0f;
	return pText->TextBoundingBox(FontSize, pValue, -1, Width, LineSpacing).m_H + FontSize * 0.25f;
}

// 测量与绘制使用相同行距；复用公共文本容器，滚动和换色无需逐帧重新排版。
inline void DrawSettingsParagraph(CUi *pUi, SSettingsParagraphState &State, const CUIRect &Rect, const char *pValue, float FontSize, float LineSpacing)
{
	if(!pUi || Rect.w <= 0.0f || Rect.h <= 0.0f || FontSize <= 0.0f || pUi->DrawingSuppressed())
		return;
	if(!State.m_Element.IsRegistered() && !State.m_Element.TryInit(pUi, 1))
		return;
	auto *pElement = State.m_Element.Rect(0);
	if(State.m_LineSpacing != LineSpacing)
	{
		pUi->TextRender()->DeleteTextContainer(pElement->m_UITextContainer);
		pElement->Reset();
		State.m_LineSpacing = LineSpacing;
	}
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(0, 0));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineSpacing = std::max(0.0f, LineSpacing);
	Cursor.m_LineWidth = Rect.w;
	SLabelProperties Props;
	Props.m_MaxWidth = Rect.w;
	const CQmCardLabelHintScope LiteralText(pUi, false);
	pUi->DoLabelStreamed(*pElement, &Rect, pValue, FontSize, TEXTALIGN_TL, Props, -1, &Cursor, true);
}

#endif
