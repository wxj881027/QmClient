#ifndef ENGINE_SHARED_WEBSOCKET_LOG_H
#define ENGINE_SHARED_WEBSOCKET_LOG_H

#include <base/str.h>

// 原生LWS日志不保证总有时间前缀或足够长的正文，裁剪只访问实际存在的尾字符。
template<size_t N>
void CopyWebsocketLogLine(char (&aBuffer)[N], const char *pLine) noexcept
{
	const char *pSource = pLine ? pLine : "";
	const char *pTimeEnd = str_find(pSource, "] ");
	str_copy(aBuffer, pTimeEnd ? pTimeEnd + 2 : pSource);
	size_t Length = str_length(aBuffer);
	if(Length > 0 && aBuffer[Length - 1] == '\n')
		aBuffer[--Length] = '\0';
	if(Length > 0 && aBuffer[Length - 1] == '\r')
		aBuffer[--Length] = '\0';
}

#endif
