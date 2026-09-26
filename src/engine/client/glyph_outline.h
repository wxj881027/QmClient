#ifndef ENGINE_CLIENT_GLYPH_OUTLINE_H
#define ENGINE_CLIENT_GLYPH_OUTLINE_H

#include <base/dbg.h>

#include <algorithm>
#include <cmath>

// 输入输出缓冲不重叠；字形描边当前最大半径为 4。
inline void QmGrowGlyphOutline(const unsigned char *pIn, unsigned char *pOut, int Width, int Height, int Radius)
{
	dbg_assert(Radius >= 0 && Radius <= 4, "unsupported glyph outline radius");
	float aMask[9][9] = {};
	for(int dy = -Radius; dy <= Radius; ++dy)
		for(int dx = -Radius; dx <= Radius; ++dx)
			aMask[dy + Radius][dx + Radius] = 1.f - std::clamp(std::sqrt((float)(dx * dx + dy * dy)) - Radius, 0.f, 1.f);

	for(int y = 0; y < Height; ++y)
	{
		for(int x = 0; x < Width; ++x)
		{
			int Value = pIn[y * Width + x];
			for(int dy = std::max(-Radius, -y); dy <= std::min(Radius, Height - 1 - y) && Value < 255; ++dy)
			{
				for(int dx = std::max(-Radius, -x); dx <= std::min(Radius, Width - 1 - x) && Value < 255; ++dx)
					Value = std::max(Value, int(pIn[(y + dy) * Width + x + dx] * aMask[dy + Radius][dx + Radius]));
			}
			pOut[y * Width + x] = Value;
		}
	}
}

#endif
