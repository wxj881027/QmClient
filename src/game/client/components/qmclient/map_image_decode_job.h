#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_IMAGE_DECODE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_IMAGE_DECODE_JOB_H

#include <engine/image.h>
#include <engine/shared/jobs.h>

#include <string>
#include <vector>

class IStorage;

// 每个任务独占源字节和像素。换图后仍在运行的任务不访问旧地图或图形接口。
class CMapImageDecodeJob final : public IJob
{
	IStorage *m_pStorage;
	bool m_Trace;

public:
	CMapImageDecodeJob(IStorage *pStorage, const char *pPath, bool Trace);
	~CMapImageDecodeJob() override;
	void Run() override;

	std::string m_Path;
	std::vector<unsigned char> m_vCompressed;
	unsigned m_UncompressedSize = 0;
	CImageInfo m_Image;
	int m_PngliteIncompatible = 0;
	double m_ReadMs = 0.0;
	double m_DecodeMs = 0.0;
};

#endif
