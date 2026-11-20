#ifndef ENGINE_CLIENT_SIXUP_MAP_CHANGE_H
#define ENGINE_CLIENT_SIXUP_MAP_CHANGE_H

#include <engine/shared/packer.h>
#include <engine/shared/translation_context.h>

namespace sixup_translation
{
	inline bool TranslateMapChange(CTranslationContext &Context, CUnpacker &Unpacker, CPacker &Packer)
	{
		const char *pMapName = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
		const int MapCrc = Unpacker.GetInt();
		const int Size = Unpacker.GetInt();
		const int ChunksPerRequest = Unpacker.GetInt();
		const int ChunkSize = Unpacker.GetInt();
		if(Unpacker.Error())
			return false;

		Packer.AddString(pMapName, 0);
		Packer.AddInt(MapCrc);
		Packer.AddInt(Size);
		if(Packer.Error())
			return false;

		// 完整解析并重打包后再发布，避免解析错误被调用方的Reset抹掉。
		Context.m_MapDownloadChunksPerRequest = ChunksPerRequest;
		Context.m_MapdownloadTotalsize = Size;
		Context.m_MapDownloadChunkSize = ChunkSize;
		return true;
	}
}

#endif
