#ifndef ENGINE_CLIENT_TEXT_UTF8_CURSOR_H
#define ENGINE_CLIENT_TEXT_UTF8_CURSOR_H

#include <base/str.h>

// 显式字节范围内解码；0 表示结束，-1 表示跳过一段无效编码。
class CQmTextUtf8Cursor
{
	const char *m_pText;
	int m_Remaining;

public:
	CQmTextUtf8Cursor(const char *pText, int Length) :
		m_pText(pText), m_Remaining(Length) {}

	int Next()
	{
		if(m_pText == nullptr || m_Remaining <= 0 || *m_pText == '\0')
		{
			m_Remaining = 0;
			return 0;
		}
		// 原解码器要求 NUL 结尾。最多复制一个码点，不能读取显式上限之外的字节。
		char aBytes[5] = {};
		for(int Index = 0; Index < 4 && Index < m_Remaining && m_pText[Index] != '\0'; ++Index)
			aBytes[Index] = m_pText[Index];
		const char *pNext = aBytes;
		const int Codepoint = str_utf8_decode(&pNext);
		const int Consumed = static_cast<int>(pNext - aBytes);
		m_pText += Consumed;
		m_Remaining -= Consumed;
		return Codepoint;
	}
};

#endif
