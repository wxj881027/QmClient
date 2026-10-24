#ifndef ENGINE_GHOST_H
#define ENGINE_GHOST_H

#include "kernel.h"

#include <base/hash.h>

#include <engine/shared/protocol.h>

class CGhostInfo
{
public:
	char m_aOwner[MAX_NAME_LENGTH];
	char m_aMap[64];
	int m_NumTicks;
	int m_Time;
};

class IGhostRecorder : public IInterface
{
	MACRO_INTERFACE("ghostrecorder")
public:
	virtual int Start(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, const char *pName) = 0;
	// 仅完整写入、关闭并发布后返回true；可在结束时指定正式文件名。
	virtual bool Stop(int Ticks, int Time, const char *pFilename = nullptr) = 0;

	virtual bool WriteData(int Type, const void *pData, size_t Size) = 0;
	virtual bool IsRecording() const = 0;
};

class IGhostLoader : public IInterface
{
	MACRO_INTERFACE("ghostloader")
public:
	virtual bool Load(const char *pFilename, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc) = 0;
	virtual void Close() = 0;

	virtual const CGhostInfo *GetInfo() const = 0;

	virtual bool ReadNextType(int *pType) = 0;
	virtual bool ReadData(int Type, void *pData, size_t Size) = 0;

	virtual bool GetGhostInfo(const char *pFilename, CGhostInfo *pInfo, const char *pMap, const SHA256_DIGEST &MapSha256, unsigned MapCrc) = 0;
};

#endif
