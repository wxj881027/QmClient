#ifndef GAME_MAP_RENDER_SCREEN_H
#define GAME_MAP_RENDER_SCREEN_H

#include <engine/gfx/viewport.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace MapRenderScreen
{
	using GraphicsViewport::IsValidScreen;

	enum class ETileRangeMode
	{
		BUFFERED,
		IMMEDIATE,
	};

	struct STileRange
	{
		int m_X0;
		int m_Y0;
		int m_X1;
		int m_Y1;
	};

	inline std::optional<STileRange> TileRange(float X0, float Y0, float X1, float Y1, float Scale, ETileRangeMode Mode)
	{
		if(!std::isfinite(X0) || !std::isfinite(Y0) || !std::isfinite(X1) || !std::isfinite(Y1) ||
			!std::isfinite(Scale) || !(Scale > 0.0f) || !(X1 > X0) || !(Y1 > Y0) ||
			!std::isfinite(X1 - X0) || !std::isfinite(Y1 - Y0))
			return std::nullopt;
		const bool Immediate = Mode == ETileRangeMode::IMMEDIATE;
		const int Padding = Immediate ? 1 : 0;
		// 保留原 float 除法和两条路径各自的取整方式。
		const float aCoordinates[] = {X0 / Scale, Y0 / Scale, X1 / Scale, Y1 / Scale};
		int aBounds[4];
		constexpr int Max = std::numeric_limits<int>::max();
		for(int i = 0; i < 4; ++i)
		{
			const float Rounded = Immediate ? std::trunc(aCoordinates[i]) : (i < 2 ? std::floor(aCoordinates[i]) : std::ceil(aCoordinates[i]));
			// 排除 INT_MIN，保证边框 abs；同时为即时路径的扩格保留空间。
			if(!std::isfinite(Rounded) || static_cast<double>(Rounded) < -Max + Padding || static_cast<double>(Rounded) > Max - Padding)
				return std::nullopt;
			aBounds[i] = static_cast<int>(Rounded) + (i < 2 ? -Padding : Padding);
		}
		if(static_cast<int64_t>(aBounds[2]) - aBounds[0] > Max || static_cast<int64_t>(aBounds[3]) - aBounds[1] > Max)
			return std::nullopt;
		return STileRange{aBounds[0], aBounds[1], aBounds[2], aBounds[3]};
	}
	struct SClipRect
	{
		int m_X;
		int m_Y;
		int m_Width;
		int m_Height;
	};

	inline std::optional<SClipRect> GroupClip(const std::array<float, 4> &aScreen, int PixelWidth, int PixelHeight, const SClipRect &WorldClip)
	{
		if(!IsValidScreen(aScreen))
			return std::nullopt;
		const float Width = aScreen[2] - aScreen[0];
		const float Height = aScreen[3] - aScreen[1];
		if(PixelWidth <= 0 || PixelHeight <= 0 || WorldClip.m_Width < 0 || WorldClip.m_Height < 0)
			return std::nullopt;
		const float Left = WorldClip.m_X - aScreen[0];
		const float Top = WorldClip.m_Y - aScreen[1];
		// 普通范围仍先整数相加再转 float；拓宽只消除端点加法溢出。
		const float Right = static_cast<float>(static_cast<int64_t>(WorldClip.m_X) + WorldClip.m_Width) - aScreen[0];
		const float Bottom = static_cast<float>(static_cast<int64_t>(WorldClip.m_Y) + WorldClip.m_Height) - aScreen[1];
		if(Right < 0.0f || Left > Width || Bottom < 0.0f || Top > Height)
			return std::nullopt;
		// 保留原 float 乘除与 round，包括负起点和零像素遮罩。
		const float aPixels[] = {
			std::round(Left * PixelWidth / Width), std::round(Top * PixelHeight / Height),
			std::round(Right * PixelWidth / Width), std::round(Bottom * PixelHeight / Height)};
		int aEdges[4];
		for(int i = 0; i < 4; ++i)
		{
			if(!std::isfinite(aPixels[i]) || static_cast<double>(aPixels[i]) < std::numeric_limits<int>::min() || static_cast<double>(aPixels[i]) > std::numeric_limits<int>::max())
				return std::nullopt;
			aEdges[i] = static_cast<int>(aPixels[i]);
		}
		const int64_t ClipWidth = static_cast<int64_t>(aEdges[2]) - aEdges[0];
		const int64_t ClipHeight = static_cast<int64_t>(aEdges[3]) - aEdges[1];
		if(ClipWidth < 0 || ClipWidth > std::numeric_limits<int>::max() || ClipHeight < 0 || ClipHeight > std::numeric_limits<int>::max())
			return std::nullopt;
		return SClipRect{aEdges[0], aEdges[1], static_cast<int>(ClipWidth), static_cast<int>(ClipHeight)};
	}
}

#endif // GAME_MAP_RENDER_SCREEN_H
