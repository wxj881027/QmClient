#ifndef GAME_CLIENT_COMPONENTS_MOTD_MESSAGE_H
#define GAME_CLIENT_COMPONENTS_MOTD_MESSAGE_H

#include <cstddef>
#include <string_view>

// 返回值借用目标缓冲；容量始终包含终止符，沿用逐字节的反斜杠换行转换。
inline std::string_view DecodeMotdMessage(const char *pMessage, char *pBuffer, size_t BufferSize) noexcept
{
	if(BufferSize == 0)
		return {};

	size_t Length = 0;
	while(*pMessage != '\0' && Length + 1 < BufferSize)
	{
		if(pMessage[0] == '\\' && pMessage[1] == 'n')
		{
			pBuffer[Length++] = '\n';
			pMessage += 2;
		}
		else
			pBuffer[Length++] = *pMessage++;
	}
	pBuffer[Length] = '\0';
	return {pBuffer, Length};
}

// 保留换行前的空行，不额外输出末尾换行后的空尾行；不读取有效长度之外。
template<typename FLine>
void ForEachMotdLine(std::string_view Message, FLine &&Line)
{
	while(!Message.empty())
	{
		const size_t End = Message.find('\n');
		if(End == std::string_view::npos)
		{
			Line(Message);
			return;
		}
		Line(Message.substr(0, End));
		Message.remove_prefix(End + 1);
	}
}

#endif
