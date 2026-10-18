#ifndef ENGINE_CLIENT_GLYPH_ATLAS_GROWTH_H
#define ENGINE_CLIENT_GLYPH_ATLAS_GROWTH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

// 完整准备新图集后才交给调用方替换；任一分配失败均保留旧数据。
template<size_t TextureCount, typename TAllocate>
std::array<std::unique_ptr<uint8_t[]>, TextureCount> PrepareGlyphAtlasGrowth(
	uint8_t *const (&apOldData)[TextureCount], size_t OldDimension, size_t NewDimension, TAllocate &&Allocate)
{
	if(OldDimension == 0 || NewDimension <= OldDimension || NewDimension > std::numeric_limits<size_t>::max() / NewDimension)
		return {};
	for(const auto *pOldData : apOldData)
		if(pOldData == nullptr)
			return {};

	std::array<std::unique_ptr<uint8_t[]>, TextureCount> apNewData;
	const size_t NewSize = NewDimension * NewDimension;
	for(size_t Index = 0; Index < TextureCount; ++Index)
	{
		apNewData[Index] = Allocate(NewSize);
		if(apNewData[Index] == nullptr)
			return {};
		std::memset(apNewData[Index].get(), 0, NewSize);
		for(size_t Row = 0; Row < OldDimension; ++Row)
			std::memcpy(apNewData[Index].get() + Row * NewDimension, apOldData[Index] + Row * OldDimension, OldDimension);
	}
	return apNewData;
}

template<size_t TextureCount>
std::array<std::unique_ptr<uint8_t[]>, TextureCount> PrepareGlyphAtlasGrowth(
	uint8_t *const (&apOldData)[TextureCount], size_t OldDimension, size_t NewDimension)
{
	return PrepareGlyphAtlasGrowth(apOldData, OldDimension, NewDimension, [](size_t Size) {
		return std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[Size]);
	});
}

#endif
