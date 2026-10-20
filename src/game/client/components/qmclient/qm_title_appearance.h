#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_APPEARANCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_APPEARANCE_H

#include <base/color.h>
#include <base/str.h>
#include <base/vmath.h>

#include <algorithm>
#include <cmath>
#include <string>

enum class EQmTitleBadgeStyle
{
	MOON,
	SUN,
};

struct SQmTitleAppearance
{
	EQmTitleBadgeStyle m_Style = EQmTitleBadgeStyle::MOON;
	unsigned m_Color = 0x9562ba;
	int m_Opacity = 100;

	bool operator==(const SQmTitleAppearance &Other) const
	{
		return m_Style == Other.m_Style && m_Color == Other.m_Color && m_Opacity == Other.m_Opacity;
	}
	bool operator!=(const SQmTitleAppearance &Other) const { return !(*this == Other); }
};

inline const char *QmTitleBadgeStyleId(EQmTitleBadgeStyle Style)
{
	return Style == EQmTitleBadgeStyle::SUN ? "sun" : "moon";
}

inline unsigned QmTitleBadgeDefaultColor(EQmTitleBadgeStyle Style)
{
	return Style == EQmTitleBadgeStyle::SUN ? 0xbd862d : 0x9562ba;
}

inline std::string QmTitleBadgeText(const char *pTitle)
{
	if(!pTitle)
		return {};
	const int Length = str_length(pTitle);
	if(Length >= 2 && pTitle[0] == '[' && pTitle[Length - 1] == ']')
		return std::string(pTitle + 1, Length - 2);
	return pTitle;
}

constexpr float QM_TITLE_BADGE_HEIGHT = 24.0f;
constexpr float QM_TITLE_BADGE_LABEL_FONT_SIZE = 16.0f;

inline float QmTitleBadgeLabelFontSize(float Height)
{
	return std::max(0.0f, Height) * QM_TITLE_BADGE_LABEL_FONT_SIZE / QM_TITLE_BADGE_HEIGHT;
}

// 保持胶囊和图标高度，标签放大后只增加文字所需的宽度。
inline vec2 QmTitleBadgeSize(float LabelWidth, float LabelFontSize)
{
	const float Scale = std::max(0.0f, LabelFontSize) / QM_TITLE_BADGE_LABEL_FONT_SIZE;
	return vec2(std::max(0.0f, LabelWidth) + 39.0f * Scale, QM_TITLE_BADGE_HEIGHT * Scale);
}

struct SQmTitleBadgePalette
{
	ColorRGBA m_Background;
	ColorRGBA m_Border;
	ColorRGBA m_Text;
	ColorRGBA m_Icon;
};

inline SQmTitleBadgePalette QmTitleBadgePalette(const SQmTitleAppearance &Appearance, float Alpha = 1.0f)
{
	const bool Sun = Appearance.m_Style == EQmTitleBadgeStyle::SUN;
	const ColorHSLA Reference = color_cast<ColorHSLA>(ColorRGBA(QmTitleBadgeDefaultColor(Appearance.m_Style)));
	const ColorHSLA Tint = color_cast<ColorHSLA>(ColorRGBA(Appearance.m_Color & 0xffffff));
	const float Opacity = std::clamp(Alpha, 0.0f, 1.0f) * std::clamp(Appearance.m_Opacity, 0, 100) / 100.0f;
	const auto Shade = [&](unsigned Rgb) {
		ColorHSLA Color = color_cast<ColorHSLA>(ColorRGBA(Rgb));
		Color.h += Tint.h - Reference.h;
		Color.h -= std::floor(Color.h);
		Color.s = std::clamp(Color.s * Tint.s / Reference.s, 0.0f, 1.0f);
		Color.l = std::clamp(Color.l + Tint.l - Reference.l, 0.0f, 1.0f);
		return color_cast<ColorRGBA>(Color.WithAlpha(Opacity));
	};
	return {Shade(Sun ? 0x44301b : 0x35273f), Shade(Sun ? 0x8d6837 : 0x695178),
		Shade(Sun ? 0xffe6a8 : 0xedd8ff), Shade(Sun ? 0xf2c66d : 0xc397e2)};
}

#endif
