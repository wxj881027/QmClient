#ifndef GAME_EDITOR_QUADART_H
#define GAME_EDITOR_QUADART_H

#include <base/math.h>
#include <base/types.h>

#include <engine/image.h>

#include <game/editor/mapitems/layer_quads.h>
#include <game/mapitems.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class CQuadArtParameters
{
public:
	int m_ImagePixelSize;
	int m_QuadPixelSize;
	bool m_Centralize;
	bool m_Optimize;
	char m_aFilename[IO_MAX_PATH_LENGTH];

	// 最后一列或一行不足一个采样步长时，仍然会生成一个完整的 Quad。
	std::optional<size_t> MaximumQuadCount(const CImageInfo &Image) const
	{
		if(Image.m_Width == 0 || Image.m_Height == 0 || m_ImagePixelSize <= 0 || m_QuadPixelSize <= 0)
			return std::nullopt;
		const size_t Columns = 1 + (Image.m_Width - 1) / m_ImagePixelSize;
		const size_t Rows = 1 + (Image.m_Height - 1) / m_ImagePixelSize;
		constexpr int MaxPosition = std::numeric_limits<int>::max();
		const size_t MaxCellsInImage = MaxPosition / m_ImagePixelSize;
		const size_t MaxCellsInMap = MaxPosition / fxpscale / m_QuadPixelSize;
		const size_t MaxCellsPerAxis = std::min(MaxCellsInImage, MaxCellsInMap);
		if(Columns > MaxCellsPerAxis || Rows > MaxCellsPerAxis || Columns > std::numeric_limits<size_t>::max() / Rows)
			return std::nullopt;
		return Columns * Rows;
	}
};

namespace quad_art
{
inline void MarkVisitedPixels(std::vector<bool> &vVisited, const CImageInfo &Image, ivec2 Pos, ivec2 Size)
{
	// 最后一列或一行可能只剩部分像素，超出的列不能标记到下一行。
	const int EndY = std::min(Pos.y + Size.y, static_cast<int>(Image.m_Height));
	const int EndX = std::min(Pos.x + Size.x, static_cast<int>(Image.m_Width));
	for(int y = Pos.y; y < EndY; y++)
	{
		for(int x = Pos.x; x < EndX; x++)
			vVisited[x + y * Image.m_Width] = true;
	}
}
} // namespace quad_art

class CQuadArt
{
public:
	CQuadArt(CQuadArtParameters Parameters, CImageInfo &&Img);
	~CQuadArt();
	bool Create(std::shared_ptr<CLayerQuads> &pQuadLayer);

private:
	ivec2 GetOptimizedQuadSize(const ColorRGBA &Pixel, const ivec2 &Pos);

	size_t FindSuperPixelSize(const ColorRGBA &Pixel, const ivec2 &Pos, size_t CurrentSize);

	ColorRGBA GetPixelClamped(const ivec2 &Pos) const;
	bool IsPixelOptimizable(const ivec2 &Pos, const ColorRGBA &Pixel) const;

	CQuad CreateNewQuad(const ivec2 &Pos, const ivec2 &Size, const ColorRGBA &Color) const;

	CQuadArtParameters m_Parameters;
	CImageInfo m_Img;
	std::vector<bool> m_vVisitedPixels;
};
#endif
