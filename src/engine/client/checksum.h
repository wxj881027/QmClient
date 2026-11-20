#ifndef ENGINE_CLIENT_CHECKSUM_H
#define ENGINE_CLIENT_CHECKSUM_H

#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/io.h>

#include <engine/shared/config.h>

#include <algorithm>

struct CChecksumData
{
	int m_SizeofData;
	char m_aVersionStr[128];
	int m_Version;
	char m_aOsVersion[256];
	int64_t m_Start;
	int m_Random;
	int m_SizeofClient;
	int m_SizeofGameClient;
	float m_Zoom;
	int m_SizeofConfig;
	CConfig m_Config;
	int m_NumCommands;
	int m_aCommandsChecksum[1024];
	int m_NumComponents;
	int m_aComponentsChecksum[64];
	int m_NumFiles;
	int m_NumExtra;
	unsigned m_aFiles[1024];

	void InitFiles();
};

union CChecksum
{
	char m_aBytes[sizeof(CChecksumData)];
	CChecksumData m_Data;
};

inline bool ChecksumAppendFileRange(SHA256_CTX &Context, IOHANDLE File, int64_t Offset, unsigned Length)
{
	if(io_seek(File, Offset, IOSEEK_START) != 0)
		return false;

	unsigned char aBuffer[2048];
	while(Length != 0)
	{
		const unsigned ChunkSize = std::min(Length, static_cast<unsigned>(sizeof(aBuffer)));
		if(io_read(File, aBuffer, ChunkSize) != ChunkSize)
			return false;
		sha256_update(&Context, aBuffer, ChunkSize);
		Length -= ChunkSize;
	}
	return true;
}

#endif // ENGINE_CLIENT_CHECKSUM_H
