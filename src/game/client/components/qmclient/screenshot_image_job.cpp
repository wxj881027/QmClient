#include "screenshot_image_job.h"

#include "bounded_file_reader.h"

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>

#include <game/client/components/assets_preview_scale.h>

#include <cstdlib>
#include <exception>
#include <utility>

CQmScreenshotImageJob::CQmScreenshotImageJob(IStorage *pStorage, std::string Path, int StorageType, int MaxEdge) :
	m_pStorage(pStorage),
	m_Path(std::move(Path)),
	m_StorageType(StorageType),
	m_MaxEdge(MaxEdge)
{
	Abortable(true);
}

CQmScreenshotImageJob::~CQmScreenshotImageJob()
{
	m_Image.Free();
}

void CQmScreenshotImageJob::Run()
{
	if(State() == STATE_ABORTED)
		return;

	try
	{
		LoadImageFromDisk(m_pStorage, m_Path, m_StorageType, m_MaxEdge, m_Image);
		if(State() == STATE_ABORTED)
			m_Image.Free();
	}
	catch(const std::exception &)
	{
		m_Image.Free();
	}
}

bool CQmScreenshotImageJob::LoadRgbaImageFromDisk(IStorage *pStorage, const std::string &Path, int StorageType, CImageInfo &Image, size_t MaxDecodedBytes)
{
	if(!LoadImageFromDisk(pStorage, Path, StorageType, 0, Image, MaxDecodedBytes))
		return false;
	// 旧接口返回原图是否已是 RGBA；转换是否成功应检查转换后的像素和格式。
	ConvertToRgba(Image);
	if(Image.m_pData == nullptr || Image.m_Format != CImageInfo::FORMAT_RGBA)
	{
		Image.Free();
		return false;
	}
	return true;
}

bool CQmScreenshotImageJob::LoadImageFromDisk(IStorage *pStorage, const std::string &Path, int StorageType, int MaxEdge, CImageInfo &Image, size_t MaxDecodedBytes)
{
	Image.Free();
	if(pStorage == nullptr || Path.empty())
		return false;

	{
		// 文件读取与解码沿用调用方的预算，查看器可以读取更大的超采样静帧。
		CQmCheckedFileData FileData;
		if(!FileData.Read(*pStorage, Path.c_str(), StorageType, MaxDecodedBytes))
			return false;

		// 按内容而不是扩展名判断格式：截图目录里可能出现改名或后缀不符的文件。
		const bool Decoded = CImageLoader::LoadPng(FileData.Data(), FileData.Size(), Path.c_str(), Image, MaxDecodedBytes) ||
				     CImageLoader::LoadWebP(FileData.Data(), FileData.Size(), Path.c_str(), Image, MaxDecodedBytes);
		if(!Decoded || Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
		{
			Image.Free();
			return false;
		}
	}

	// 缩略图只保留最大边以内的像素：全尺寸纹理会把上传和显存开销放大到与屏幕无关的量级。
	if(MaxEdge > 0)
	{
		const SPreviewTargetSize Target = ComputePreviewTargetSize((int)Image.m_Width, (int)Image.m_Height, MaxEdge);
		if(Target.m_Resized)
		{
			ResizeImage(Image, Target.m_Width, Target.m_Height);
			if(Image.m_Width != (size_t)Target.m_Width || Image.m_Height != (size_t)Target.m_Height)
			{
				// 缩图分配失败会保留原图，不能继续把超限原图交给缩略图上传。
				Image.Free();
				return false;
			}
		}
	}

	if(Image.m_Format != CImageInfo::FORMAT_R)
		return true;

	// PNG 的单通道值是亮度。展开成不透明 RGBA 后，查看器和纹理上传可共用像素。
	CImageInfo Rgba;
	Rgba.m_Width = Image.m_Width;
	Rgba.m_Height = Image.m_Height;
	Rgba.m_Format = CImageInfo::FORMAT_RGBA;
	Rgba.m_pData = static_cast<uint8_t *>(std::malloc(Rgba.DataSize()));
	if(Rgba.m_pData == nullptr)
	{
		Image.Free();
		return false;
	}

	const size_t PixelCount = Image.DataSize();
	const size_t RgbaPixelSize = Rgba.PixelSize();
	for(size_t Index = 0; Index < PixelCount; ++Index)
	{
		const uint8_t Gray = Image.m_pData[Index];
		uint8_t *pPixel = &Rgba.m_pData[Index * RgbaPixelSize];
		pPixel[0] = pPixel[1] = pPixel[2] = Gray;
		pPixel[3] = 255;
	}
	Image = std::move(Rgba);
	return true;
}
