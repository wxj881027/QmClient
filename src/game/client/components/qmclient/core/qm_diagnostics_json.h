/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_JSON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DIAGNOSTICS_JSON_H

#include <base/mem.h>
#include <base/str.h>

#include <cstddef>

namespace QmDiagnostics
{
inline bool AppendJsonByte(char *pBuffer, size_t BufferSize, size_t &Offset, char Byte)
{
	if(Offset + 1 >= BufferSize)
		return false;
	pBuffer[Offset++] = Byte;
	return true;
}

inline bool AppendJsonEscape(char *pBuffer, size_t BufferSize, size_t &Offset, const char *pEscape)
{
	const size_t Length = static_cast<size_t>(str_length(pEscape));
	if(Offset + Length >= BufferSize)
		return false;
	mem_copy(pBuffer + Offset, pEscape, Length);
	Offset += Length;
	return true;
}

/**
 * 转义诊断字段，同时对非法 UTF-8 和截断做容错处理。
 *
 * 诊断数据来自图形驱动、平台和外部字符串，不能使用官方
 * EscapeJson 的非法 UTF-8 断言路径，也不能产生半截 JSON 转义。
 */
inline bool EscapeJson(char *pBuffer, size_t BufferSize, const char *pString)
{
	if(BufferSize == 0)
		return false;

	const char *pCurrent = pString ? pString : "";
	size_t Offset = 0;
	bool Complete = true;
	while(*pCurrent)
	{
		const unsigned char Byte = static_cast<unsigned char>(*pCurrent);
		if(Byte == '"')
		{
			if(!AppendJsonEscape(pBuffer, BufferSize, Offset, "\\\""))
			{
				Complete = false;
				break;
			}
			++pCurrent;
		}
		else if(Byte == '\\')
		{
			if(!AppendJsonEscape(pBuffer, BufferSize, Offset, "\\\\"))
			{
				Complete = false;
				break;
			}
			++pCurrent;
		}
		else if(Byte == '\b' || Byte == '\n' || Byte == '\r' || Byte == '\t' || Byte == '\f')
		{
			const char *pEscapes[] = {"\\b", "\\n", "\\r", "\\t", "\\f"};
			const char *pEscape = pEscapes[Byte == '\b' ? 0 : Byte == '\n' ? 1 : Byte == '\r' ? 2 : Byte == '\t' ? 3 : 4];
			if(!AppendJsonEscape(pBuffer, BufferSize, Offset, pEscape))
			{
				Complete = false;
				break;
			}
			++pCurrent;
		}
		else if(Byte < 0x20)
		{
			char aEscape[7];
			str_format(aEscape, sizeof(aEscape), "\\u%04x", Byte);
			if(!AppendJsonEscape(pBuffer, BufferSize, Offset, aEscape))
			{
				Complete = false;
				break;
			}
			++pCurrent;
		}
		else if(Byte < 0x80)
		{
			if(!AppendJsonByte(pBuffer, BufferSize, Offset, static_cast<char>(Byte)))
			{
				Complete = false;
				break;
			}
			++pCurrent;
		}
		else
		{
			const char *pUtf8End = pCurrent;
			const int Codepoint = str_utf8_decode(&pUtf8End);
			if(Codepoint < 0)
			{
				char aEscape[7];
				str_format(aEscape, sizeof(aEscape), "\\u%04x", Byte);
				if(!AppendJsonEscape(pBuffer, BufferSize, Offset, aEscape))
				{
					Complete = false;
					break;
				}
				++pCurrent;
			}
			else
			{
				const size_t Length = static_cast<size_t>(pUtf8End - pCurrent);
				if(Offset + Length >= BufferSize)
				{
					Complete = false;
					break;
				}
				mem_copy(pBuffer + Offset, pCurrent, Length);
				Offset += Length;
				pCurrent = pUtf8End;
			}
		}
	}
	pBuffer[Offset] = '\0';
	return Complete;
}
}

#endif
