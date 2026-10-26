#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_ICON_LOAD_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_ICON_LOAD_JOB_H

#include <base/lock.h>
#include <base/math.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <utility>

// 解码边界由生产背景格式分发器提供；文件、像素与完成状态由任务持有。
class CQmThemeIconLoadJob : public IJob
{
public:
	using FDecode = bool (*)(const void *, unsigned, const char *, CImageInfo &);
	struct SResult
	{
		std::string m_ThemeName;
		CImageInfo m_Image;
		bool m_Success = false;
		SResult() = default;
		SResult(SResult &&) = default;
		SResult &operator=(SResult &&) = default;
		~SResult() { m_Image.Free(); }
	};

private:
	IStorage *m_pStorage;
	std::string m_IconPath;
	FDecode m_pDecode;
	mutable CLock m_Lock;
	SResult m_Result;
	bool m_Completed = false;

	static bool ResizeIcon(CImageInfo &Image)
	{
		constexpr int MaxDimension = 512;
		if(Image.m_Width <= MaxDimension && Image.m_Height <= MaxDimension)
			return true;
		const double Scale = minimum((double)MaxDimension / (double)Image.m_Width, (double)MaxDimension / (double)Image.m_Height);
		const int Width = maximum(1, (int)std::floor((double)Image.m_Width * Scale));
		const int Height = maximum(1, (int)std::floor((double)Image.m_Height * Scale));
		ResizeImage(Image, Width, Height);
		// ResizeImage 分配失败会保留原图，不能把超限像素作为成功图标交给上传。
		return Image.m_Width == (size_t)Width && Image.m_Height == (size_t)Height;
	}

	void Run() override REQUIRES(!m_Lock)
	{
		SResult Result;
		try
		{
			void *pData = nullptr;
			unsigned Size = 0;
			const bool Read = m_pStorage->ReadFile(m_IconPath.c_str(), IStorage::TYPE_ALL, &pData, &Size);
			const std::unique_ptr<void, decltype(&free)> pFile(pData, free);
			if(Read && m_pDecode(pFile.get(), Size, m_IconPath.c_str(), Result.m_Image))
			{
				auto &Image = Result.m_Image;
				Result.m_Success = Image.m_pData != nullptr && Image.m_Width > 0 && Image.m_Height > 0 &&
						   (ConvertToRgba(Image) || (Image.m_pData != nullptr && Image.m_Format == CImageInfo::FORMAT_RGBA)) && ResizeIcon(Image);
			}
		}
		catch(const std::exception &)
		{
			// 与普通读取/解码失败共用终态，避免池已结束而消费者仍等完成标记。
			Result.m_Success = false;
		}
		if(!Result.m_Success)
			Result.m_Image.Free();
		const CLockScope Lock(m_Lock);
		m_Result.m_Image = std::move(Result.m_Image);
		m_Result.m_Success = Result.m_Success;
		m_Completed = true;
	}

public:
	CQmThemeIconLoadJob(const char *pThemeName, const char *pIconPath, IStorage *pStorage, FDecode pDecode) :
		m_pStorage(pStorage),
		m_IconPath(pIconPath),
		m_pDecode(pDecode)
	{
		m_Result.m_ThemeName = pThemeName;
		Abortable(true);
	}
	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}
	SResult TakeResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

#endif
