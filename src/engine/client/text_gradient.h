#ifndef ENGINE_CLIENT_TEXT_GRADIENT_H
#define ENGINE_CLIENT_TEXT_GRADIENT_H

#include <engine/textrender.h>

#include <array>

struct STextGradientCell
{
	std::array<vec2, 4> m_aPositions;
	std::array<vec2, 4> m_aUvs;
	std::array<ColorRGBA, 4> m_aColors;
};

// 仅对显式启用二维取色的字形细分；固定预算让单个汉字也能显示七色和中心渐变。
template<typename TEmit>
void ForEachTextGradientCell(vec2 TopLeft, vec2 BottomRight, vec2 UvTopLeft, vec2 UvBottomRight,
	CTextCursor::FColorSampler pfnSample, const void *pContext, TEmit Emit)
{
	constexpr int COLUMNS = 8;
	constexpr int ROWS = 8;
	for(int Row = 0; Row < ROWS; ++Row)
	{
		for(int Column = 0; Column < COLUMNS; ++Column)
		{
			const std::array<vec2, 4> aFractions = {
				vec2(Column / static_cast<float>(COLUMNS), Row / static_cast<float>(ROWS)), vec2((Column + 1) / static_cast<float>(COLUMNS), Row / static_cast<float>(ROWS)),
				vec2(Column / static_cast<float>(COLUMNS), (Row + 1) / static_cast<float>(ROWS)), vec2((Column + 1) / static_cast<float>(COLUMNS), (Row + 1) / static_cast<float>(ROWS))};
			STextGradientCell Cell;
			for(int i = 0; i < 4; ++i)
			{
				Cell.m_aPositions[i] = TopLeft + (BottomRight - TopLeft) * aFractions[i];
				Cell.m_aUvs[i] = UvTopLeft + (UvBottomRight - UvTopLeft) * aFractions[i];
				Cell.m_aColors[i] = pfnSample(Cell.m_aPositions[i], pContext);
			}
			Emit(Cell);
		}
	}
}

#endif
