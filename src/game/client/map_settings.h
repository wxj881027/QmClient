#ifndef GAME_CLIENT_MAP_SETTINGS_H
#define GAME_CLIENT_MAP_SETTINGS_H

#include <cstddef>
#include <cstring>

template<typename TExecute>
void ForEachMapSetting(const char *pData, int Size, TExecute &&Execute)
{
	if(!pData || Size <= 0)
		return;

	const char *pEnd = pData + Size;
	while(pData < pEnd)
	{
		const auto *pTerminator = static_cast<const char *>(std::memchr(pData, '\0', static_cast<std::size_t>(pEnd - pData)));
		if(!pTerminator)
			break;
		Execute(pData);
		pData = pTerminator + 1;
	}
}

#endif
