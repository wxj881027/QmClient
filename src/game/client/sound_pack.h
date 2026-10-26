#ifndef GAME_CLIENT_SOUND_PACK_H
#define GAME_CLIENT_SOUND_PACK_H

#include <base/str.h>

#include <engine/storage.h>

#include <string>

// 加载批次持有自己的包名，后台查找不再读取可由主线程修改的配置。
class CSoundPack
{
	const std::string m_Name;

public:
	explicit CSoundPack(const char *pName) :
		m_Name(pName)
	{
	}

	const char *Resolve(const char *pFilename, IStorage *pStorage, char *pBuffer, size_t BufferSize) const
	{
		if(!pStorage || pFilename == nullptr || pFilename[0] == '\0' || m_Name.empty())
			return pFilename;

		const char *pRelative = pFilename;
		if(str_startswith(pFilename, "audio/"))
			pRelative = pFilename + str_length("audio/");

		str_format(pBuffer, BufferSize, "audio/%s/%s", m_Name.c_str(), pRelative);
		if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
			return pBuffer;

		str_format(pBuffer, BufferSize, "audio/%s/%s", m_Name.c_str(), pFilename);
		if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
			return pBuffer;

		return pFilename;
	}
};

#endif
