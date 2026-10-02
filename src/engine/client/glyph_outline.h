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

// 世界名牌保持固定字形比例；最大栅格字号 128 对应半径 6.4。
inline float QmNameplateGlyphOutlineRadius(int FontSize)
{
	return std::clamp(FontSize, 6, 128) / 20.0f;
}

// 小数半径以覆盖度扩张，保留原填充。输入输出不重叠；超出边界不读邻居。
inline void QmGrowGlyphOutlineContinuous(const unsigned char *pIn, unsigned char *pOut, int Width, int Height, float Radius)
{
	dbg_assert(std::isfinite(Radius) && Radius >= 0.0f && Radius <= 8.0f, "unsupported continuous glyph outline radius");
	const int Extent = static_cast<int>(std::ceil(Radius));
	float aMask[17][17] = {};
	for(int dy = -Extent; dy <= Extent; ++dy)
		for(int dx = -Extent; dx <= Extent; ++dx)
			aMask[dy + Extent][dx + Extent] = 1.0f - std::clamp(std::sqrt(float(dx * dx + dy * dy)) - Radius, 0.0f, 1.0f);
	if(Width <= 0 || Height <= 0)
		return;
	std::copy_n(pIn, static_cast<size_t>(Width) * Height, pOut);
	// 从非透明填充向外扩张，透明背景不扫描邻域；已饱和的目标像素跳过乘法。
	for(int y = 0; y < Height; ++y)
		for(int x = 0; x < Width; ++x)
		{
			const int Source = pIn[y * Width + x];
			if(Source == 0)
				continue;
			for(int dy = std::max(-Extent, -y); dy <= std::min(Extent, Height - 1 - y); ++dy)
				for(int dx = std::max(-Extent, -x); dx <= std::min(Extent, Width - 1 - x); ++dx)
				{
					unsigned char &Target = pOut[(y + dy) * Width + x + dx];
					if(Target < 255)
						Target = static_cast<unsigned char>(std::max(int(Target), int(Source * aMask[dy + Extent][dx + Extent])));
				}
		}
}

#endif
