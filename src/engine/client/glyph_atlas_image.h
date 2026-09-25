#ifndef ENGINE_CLIENT_GLYPH_ATLAS_IMAGE_H
#define ENGINE_CLIENT_GLYPH_ATLAS_IMAGE_H

#include <base/dbg.h>
#include <base/mem.h>

#include <cstddef>
#include <cstdint>

// 输入输出不重叠；旧区域直接复制，只清零右侧和底部新增区域。
inline void QmCopyExpandedGlyphAtlas(uint8_t *pDestination, const uint8_t *pSource, size_t OldDimension, size_t NewDimension)
{
	dbg_assert(NewDimension > OldDimension, "glyph atlas must grow");
	for(size_t Y = 0; Y < OldDimension; ++Y)
	{
		uint8_t *pRow = pDestination + Y * NewDimension;
		mem_copy(pRow, pSource + Y * OldDimension, OldDimension);
		mem_zero(pRow + OldDimension, NewDimension - OldDimension);
	}
	mem_zero(pDestination + OldDimension * NewDimension, (NewDimension - OldDimension) * NewDimension);
}

#endif
