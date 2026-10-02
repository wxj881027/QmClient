#include "screenshot_image_job.h"

#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <game/client/components/assets_preview_scale.h>

#include <utility>
#include <vector>

CQmScreenshotImageJob::CQmScreenshotImageJob(IStorage *pStorage, std::string Path, int StorageType, int MaxEdge) :
	m_pStorage(pStorage),
	m_Path(std::move(Path)),
	m_StorageType(StorageType),
	m_MaxEdge(MaxEdge)
{
}

CQmScreenshotImageJob::~CQmScreenshotImageJob()
{
	m_Image.Free();
}

void CQmScreenshotImageJob::Run()
{
	LoadFromDisk();
}

bool CQmScreenshotImageJob::LoadFromDisk()
{
	if(m_pStorage == nullptr || m_Path.empty())
		return false;

	IOHANDLE File = m_pStorage->OpenFile(m_Path.c_str(), IOFLAG_READ, m_StorageType);
	if(File == nullptr)
		return false;

	io_seek(File, 0, IOSEEK_END);
	const int64_t FileSize = io_tell(File);
	io_seek(File, 0, IOSEEK_START);
	if(FileSize <= 0 || FileSize > QM_SCREENSHOT_IMAGE_MAX_FILE_SIZE)
	{
		io_close(File);
		return false;
	}

	std::vector<uint8_t> vFileData((size_t)FileSize);
	const size_t Read = io_read(File, vFileData.data(), (unsigned)vFileData.size());
	io_close(File);
	if(Read != vFileData.size())
		return false;

	CImageInfo Image;
	// 按内容而不是扩展名判断格式：截图目录里可能出现改名或后缀不符的文件。
	const bool Decoded = CImageLoader::LoadPng(vFileData.data(), vFileData.size(), m_Path.c_str(), Image) ||
			     CImageLoader::LoadWebP(vFileData.data(), vFileData.size(), m_Path.c_str(), Image);
	if(!Decoded || Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
	{
		Image.Free();
		return false;
	}

	// 缩略图只保留最大边以内的像素：全尺寸纹理会把上传和显存开销放大到与屏幕无关的量级。
	if(m_MaxEdge > 0)
	{
		const SPreviewTargetSize Target = ComputePreviewTargetSize((int)Image.m_Width, (int)Image.m_Height, m_MaxEdge);
		if(Target.m_Resized)
			ResizeImage(Image, Target.m_Width, Target.m_Height);
	}

	m_Image = std::move(Image);
	return true;
}
