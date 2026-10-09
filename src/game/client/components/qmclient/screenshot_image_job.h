#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_IMAGE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_IMAGE_JOB_H

#include <engine/image.h>
#include <engine/shared/jobs.h>

#include <algorithm>
#include <cstdint>
#include <string>

class IStorage;

// 截图页的图片一律异步加载：后台线程读盘、解码并等比缩小，渲染线程只在预算内上传纹理。
// 同步解码整张截图会在滚动时每帧卡住渲染线程，这是图片页卡顿的根因。
enum
{
	QM_SCREENSHOT_THUMBNAIL_MAX_EDGE = 512,
	QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT = 64,
	QM_SCREENSHOT_THUMBNAIL_MAX_CONCURRENT_JOBS = 4,
	QM_SCREENSHOT_THUMBNAIL_MAX_UPLOADS_PER_FRAME = 8,
	QM_SCREENSHOT_IMAGE_MAX_FILE_SIZE = 64 * 1024 * 1024,
	// 四个缩略图解码任务的像素总量最多 512 MiB；标准 8K RGBA 仍在单图预算内。
	QM_SCREENSHOT_IMAGE_MAX_DECODED_BYTES = 128 * 1024 * 1024,
};

// 本帧可以补足的后台解码任务数：只在有可见请求时启动，且不超过并发上限。
inline int QmScreenshotImageJobsToStart(int RunningJobs, int PendingRequests, int MaxConcurrent)
{
	if(RunningJobs < 0 || PendingRequests <= 0 || MaxConcurrent <= 0)
		return 0;
	return std::max(0, std::min(PendingRequests, MaxConcurrent - RunningJobs));
}

// 常驻条目超过上限后需要淘汰的数量；MaxResident 为 0 表示不保留常驻条目。
// 调用方只淘汰非加载中、且本帧不可见的条目。
inline int QmScreenshotImageEvictionCount(int ResidentEntries, int MaxResident)
{
	if(ResidentEntries <= 0)
		return 0;
	if(MaxResident <= 0)
		return ResidentEntries;
	return std::max(0, ResidentEntries - MaxResident);
}

// 后台读取、解码并按最大边等比缩小的图片任务。
// 像素在 State() == STATE_DONE 之后归渲染线程所有，随任务对象一起释放。
class CQmScreenshotImageJob : public IJob
{
	IStorage *m_pStorage;
	std::string m_Path;
	int m_StorageType;
	int m_MaxEdge;
	CImageInfo m_Image;

	void Run() override;

public:
	static bool LoadImageFromDisk(IStorage *pStorage, const std::string &Path, int StorageType, int MaxEdge, CImageInfo &Image);

	CQmScreenshotImageJob(IStorage *pStorage, std::string Path, int StorageType, int MaxEdge);
	~CQmScreenshotImageJob() override;

	// 可直接绘制的结果；State() != STATE_DONE 或解码失败时返回 nullptr。
	CImageInfo *Image() { return State() == STATE_DONE && m_Image.m_pData != nullptr ? &m_Image : nullptr; }
	// 仅在 State() == STATE_DONE 后有意义。
	bool LoadFailed() const { return State() == STATE_DONE && m_Image.m_pData == nullptr; }
};

#endif
