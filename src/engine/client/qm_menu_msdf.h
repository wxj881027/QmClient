#ifndef ENGINE_CLIENT_QM_MENU_MSDF_H
#define ENGINE_CLIENT_QM_MENU_MSDF_H

#include <base/vmath.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace qm_menu_msdf
{
inline constexpr int SIZE = 2048;
inline constexpr float EM = 32.0f;
inline constexpr float PX_RANGE = 8.0f;

struct SFace
{
	int m_Index;
	const char *m_pDigest;
};

struct SGlyph
{
	int m_Face;
	int m_Codepoint;
	int m_X;
	int m_Y;
	int m_Width;
	int m_Height;
	int m_Left;
	int m_Top;
};

#include "qm_menu_msdf_data.inc"

inline const SGlyph *Find(int Face, int Codepoint)
{
	const auto Key = std::pair{Face, Codepoint};
	const auto *pGlyph = std::lower_bound(std::begin(s_aGlyphs), std::end(s_aGlyphs), Key, [](const SGlyph &Glyph, const auto &Compare) {
		return std::pair{Glyph.m_Face, Glyph.m_Codepoint} < Compare;
	});
	return pGlyph != std::end(s_aGlyphs) && pGlyph->m_Face == Face && pGlyph->m_Codepoint == Codepoint ? pGlyph : nullptr;
}

// 基线坐标为 UI 逻辑单位，图集平面坐标为固定 em 像素。
inline vec4 Quad(const SGlyph &Glyph, vec2 Baseline, float FontSize)
{
	const float Scale = FontSize / EM;
	return vec4(Baseline.x + Glyph.m_Left * Scale, Baseline.y - Glyph.m_Top * Scale,
		Glyph.m_Width * Scale, Glyph.m_Height * Scale);
}
} // namespace qm_menu_msdf

#endif
