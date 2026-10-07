// 混合图标标签按连续正文和单个图标分段，不分配内存，不改写持久化字符串。
#ifndef GAME_CLIENT_QM_ICON_LABEL_RUNS_H
#define GAME_CLIENT_QM_ICON_LABEL_RUNS_H

#include "qm_icon.h"

#include <engine/textrender.h>

struct SQmIconLabelRun
{
	const char *m_pText;
	int m_Length;
	bool m_IsIcon;
	EQmIcon m_Icon;
	// 已知旧字形的随包回退；未知字形由调用者使用原片段。
	const char *m_pFallback;
};

template<typename F>
bool QmVisitIconLabelRuns(const char *pText, F &&Visit)
{
	if(pText == nullptr)
		return true;
	const char *pCursor = pText;
	while(*pCursor != '\0')
	{
		const char *pStart = pCursor;
		const int Codepoint = str_utf8_decode(&pCursor);
		if(Codepoint <= 0)
			return false;
		const bool IsIcon = Codepoint >= 0xE000 && Codepoint <= 0xF8FF;
		if(!IsIcon)
		{
			while(*pCursor != '\0')
			{
				const char *pNext = pCursor;
				const int NextCodepoint = str_utf8_decode(&pNext);
				if(NextCodepoint <= 0 || (NextCodepoint >= 0xE000 && NextCodepoint <= 0xF8FF))
					break;
				pCursor = pNext;
			}
		}
		SQmIconLabelRun Run{pStart, static_cast<int>(pCursor - pStart), IsIcon, EQmIcon::COUNT, nullptr};
		if(IsIcon)
		{
			Run.m_Icon = CQmIconRegistry::IconFromGlyph(pStart, Run.m_Length);
			// 已有配置仅在绘制时映射，保持原标签与编号不变。
			if(Codepoint == 0xF0C9)
			{
				Run.m_Icon = EQmIcon::LIST_UL;
				Run.m_pFallback = FontIcons::FONT_ICON_LIST_UL;
			}
			else if(Codepoint == 0xF14E || Codepoint == 0xF550)
			{
				Run.m_Icon = EQmIcon::GEAR;
				Run.m_pFallback = FontIcons::FONT_ICON_GEAR;
			}
		}
		Visit(Run);
	}
	return true;
}

#endif
