#ifndef ENGINE_CLIENT_BACKEND_READBACK_IMAGE_H
#define ENGINE_CLIENT_BACKEND_READBACK_IMAGE_H

#include <engine/image.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace readback_image
{
	struct SRgbaLayout
	{
		size_t m_RowBytes = 0;
		size_t m_DataBytes = 0;
		size_t m_RowPitch = 0;
		size_t m_SourceBytes = 0;
	};

	inline bool GetRgbaLayout(size_t Width, size_t Height, uint64_t RowPitch, uint64_t AvailableBytes, SRgbaLayout &Layout) noexcept
	{
		Layout = {};
		if(Width == 0 || Height == 0)
			return false;
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		size_t DataBytes = 0;
		if(!Image.DataSize(DataBytes))
			return false;
		const size_t RowBytes = DataBytes / Height;
		const uint64_t MaxBytes = std::numeric_limits<ptrdiff_t>::max();
		const uint64_t SourceLimit = std::min(AvailableBytes, MaxBytes);
		if(DataBytes > MaxBytes || RowPitch < RowBytes || RowPitch > MaxBytes || SourceLimit < RowBytes)
			return false;
		if(Height - 1 > (SourceLimit - RowBytes) / RowPitch)
			return false;
		Layout = {RowBytes, DataBytes, static_cast<size_t>(RowPitch), static_cast<size_t>((Height - 1) * RowPitch + RowBytes)};
		return true;
	}

	// 源是独立的 GPU 映射，不能借用目标 vector 的内存；所有范围在扩容和复制前校验。
	template<typename TAllocator>
	bool PackRgba(std::vector<uint8_t, TAllocator> &vDestination, const uint8_t *pSource, size_t Width, size_t Height,
		uint64_t RowPitch, uint64_t AvailableBytes, bool SwapRedBlue, bool ResetAlpha) noexcept
	{
		SRgbaLayout Layout;
		if(pSource == nullptr || !GetRgbaLayout(Width, Height, RowPitch, AvailableBytes, Layout))
			return false;
		try
		{
			if(vDestination.size() < Layout.m_DataBytes)
				vDestination.resize(Layout.m_DataBytes);
		}
		catch(...)
		{
			return false;
		}
		// 最后一行只需要有效像素，不能假定其后仍有完整的行尾填充。
		for(size_t Y = 0; Y < Height; ++Y)
			std::memcpy(vDestination.data() + Y * Layout.m_RowBytes, pSource + Y * Layout.m_RowPitch, Layout.m_RowBytes);
		if(SwapRedBlue || ResetAlpha)
		{
			for(size_t Offset = 0; Offset < Layout.m_DataBytes; Offset += 4)
			{
				if(SwapRedBlue)
					std::swap(vDestination[Offset], vDestination[Offset + 2]);
				// 保留旧 BGR 回退路径同时重置 alpha 的行为。
				vDestination[Offset + 3] = 255;
			}
		}
		return true;
	}
}

#endif
