#ifndef ENGINE_CLIENT_BACKEND_SCREENSHOT_IMAGE_H
#define ENGINE_CLIENT_BACKEND_SCREENSHOT_IMAGE_H

#include <engine/image.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace screenshot_image
{
	enum class ERowOrder
	{
		TOP_DOWN,
		BOTTOM_UP,
	};

	// 仅在完整读回后发布截图。分配边界返回 nullptr 或可由 free 释放的内存。
	template<typename TRead, typename TAllocate>
	bool CaptureRgba(CImageInfo &Image, size_t Width, size_t Height, ERowOrder RowOrder, TRead &&Read, TAllocate &&Allocate) noexcept
	{
		Image.Free();
		if(Width == 0 || Height == 0)
			return false;

		CImageInfo Candidate;
		Candidate.m_Width = Width;
		Candidate.m_Height = Height;
		Candidate.m_Format = CImageInfo::FORMAT_RGBA;
		size_t DataBytes = 0;
		if(!Candidate.DataSize(DataBytes))
			return false;
		const size_t RowBytes = DataBytes / Height;
		const size_t ScratchBytes = RowOrder == ERowOrder::BOTTOM_UP ? RowBytes : 0;
		const size_t MaxBytes = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
		if(DataBytes > MaxBytes || ScratchBytes > MaxBytes - DataBytes)
			return false;

		try
		{
			std::unique_ptr<uint8_t, decltype(&std::free)> pData(static_cast<uint8_t *>(Allocate(DataBytes + ScratchBytes)), &std::free);
			if(!pData || !Read(pData.get(), DataBytes))
				return false;

			if(RowOrder == ERowOrder::BOTTOM_UP)
			{
				uint8_t *pScratch = pData.get() + DataBytes;
				for(size_t Y = 0; Y < Height / 2; ++Y)
				{
					uint8_t *pTop = pData.get() + Y * RowBytes;
					uint8_t *pBottom = pData.get() + (Height - Y - 1) * RowBytes;
					std::memcpy(pScratch, pTop, RowBytes);
					std::memcpy(pTop, pBottom, RowBytes);
					std::memcpy(pBottom, pScratch, RowBytes);
				}
			}
			// 奇数高度的中间行也必须与其余截图像素一样不透明。
			for(size_t Offset = 3; Offset < DataBytes; Offset += 4)
				pData.get()[Offset] = 255;

			Candidate.m_pData = pData.release();
			Image = std::move(Candidate);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	template<typename TRead>
	bool CaptureRgba(CImageInfo &Image, size_t Width, size_t Height, ERowOrder RowOrder, TRead &&Read) noexcept
	{
		return CaptureRgba(Image, Width, Height, RowOrder, std::forward<TRead>(Read), std::malloc);
	}
}

#endif
