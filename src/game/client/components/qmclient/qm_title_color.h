// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_COLOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_COLOR_H

#include <base/str.h>

#include <engine/textrender.h>

#include <game/client/components/qmclient/qmclient_utils.h>

// [] 内头衔的彩虹渲染：按字符逐段写入 color split，透明度直接并入段颜色。
// 调用方必须在追加文本后清空 Cursor.m_vColorSplits，否则后续文本会继承这些色段。
inline void QmAddTitleRainbowSplits(CTextCursor &Cursor, const char *pTitle, const float Alpha)
{
	if(!pTitle || pTitle[0] == '\0')
		return;

	int CharCount = 0;
	for(const char *pCursor = pTitle; str_utf8_decode(&pCursor) > 0;)
		++CharCount;
	if(CharCount <= 0)
		return;

	// 同一个文本容器内多次追加时，字符序号在游标上是累加的（与 CColoredParts 一致）。
	const int BaseIndex = Cursor.m_CharCount;
	Cursor.m_vColorSplits.reserve(Cursor.m_vColorSplits.size() + CharCount);
	const char *pCurrent = pTitle;
	for(int CharIndex = 0; CharIndex < CharCount; ++CharIndex)
	{
		const char *pNext = pCurrent;
		if(str_utf8_decode(&pNext) <= 0)
			break;
		Cursor.m_vColorSplits.emplace_back(BaseIndex + (int)(pCurrent - pTitle), (int)(pNext - pCurrent), QmTitleRainbowColor(CharIndex, CharCount, Alpha));
		pCurrent = pNext;
	}
}

#endif
