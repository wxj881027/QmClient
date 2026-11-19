#ifndef GAME_CLIENT_SOUND_PACK_H
#define GAME_CLIENT_SOUND_PACK_H

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

// 加载批次持有自己的包名，后台查找不再读取可由主线程修改的配置。
class CSoundPack
{
	char m_aName[sizeof(CConfig::m_SndPack)];

public:
	explicit CSoundPack(const char *pName) noexcept
	{
		str_copy(m_aName, pName);
	}

	const char *Resolve(const char *pFilename, IStorage *pStorage, char *pBuffer, size_t BufferSize) const
	{
		if(!pStorage || pFilename == nullptr || pFilename[0] == '\0' || m_aName[0] == '\0')
			return pFilename;

		const char *pRelative = pFilename;
		if(str_startswith(pFilename, "audio/"))
			pRelative = pFilename + str_length("audio/");

		str_format(pBuffer, BufferSize, "audio/%s/%s", m_aName, pRelative);
		if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
			return pBuffer;

		str_format(pBuffer, BufferSize, "audio/%s/%s", m_aName, pFilename);
		if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
			return pBuffer;

		return pFilename;
	}
};

#endif
