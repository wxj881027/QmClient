// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_CLIENT_TEXT_LAYOUT_STRING_H
#define ENGINE_CLIENT_TEXT_LAYOUT_STRING_H

#include <base/str.h>

// 逐词测量已给出字节上限，避免为每个词重新扫描整段文字的剩余后缀。
inline int QmTextLayoutByteLength(const char *pText, int Length)
{
	if(Length < 0)
		return str_length(pText);

	int Result = 0;
	while(Result < Length && pText[Result] != '\0')
		++Result;
	return Result;
}

#endif // ENGINE_CLIENT_TEXT_LAYOUT_STRING_H
