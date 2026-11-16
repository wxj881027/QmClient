#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_APPEARANCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_APPEARANCE_H

#include <base/color.h>
#include <base/str.h>
#include <base/vmath.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

enum class EQmTitleBadgeStyle
{
	MOON,
	SUN,
	EARTH,
	MORNING_STAR,
	BLACK_HOLE,
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

inline constexpr const char *QM_TITLE_BADGE_STYLE_IDS[] = {"moon", "sun", "earth", "morning_star", "black_hole"};
inline constexpr int QM_TITLE_BADGE_STYLE_COUNT = std::size(QM_TITLE_BADGE_STYLE_IDS);
static_assert(static_cast<int>(EQmTitleBadgeStyle::BLACK_HOLE) + 1 == QM_TITLE_BADGE_STYLE_COUNT);

inline EQmTitleBadgeStyle QmTitleBadgeStyleFromIndex(int Index)
{
	return Index >= 0 && Index < QM_TITLE_BADGE_STYLE_COUNT ? static_cast<EQmTitleBadgeStyle>(Index) : EQmTitleBadgeStyle::MOON;
}

inline int QmTitleBadgeStyleIndex(EQmTitleBadgeStyle Style)
{
	return static_cast<int>(QmTitleBadgeStyleFromIndex(static_cast<int>(Style)));
}

inline const char *QmTitleBadgeStyleId(EQmTitleBadgeStyle Style)
{
	return QM_TITLE_BADGE_STYLE_IDS[QmTitleBadgeStyleIndex(Style)];
}

inline bool QmTitleBadgeStyleFromId(const char *pId, EQmTitleBadgeStyle &Out)
{
	if(pId)
	{
		for(int i = 0; i < QM_TITLE_BADGE_STYLE_COUNT; ++i)
		{
			if(str_comp(pId, QM_TITLE_BADGE_STYLE_IDS[i]) == 0)
			{
				Out = static_cast<EQmTitleBadgeStyle>(i);
				return true;
			}
		}
	}
	return false;
}

inline unsigned QmTitleBadgeDefaultColor(EQmTitleBadgeStyle Style)
{
	switch(Style)
	{
	case EQmTitleBadgeStyle::SUN: return 0xbd862d;
	case EQmTitleBadgeStyle::EARTH: return 0x3b86c4;
	case EQmTitleBadgeStyle::MORNING_STAR: return 0xd4af5a;
	case EQmTitleBadgeStyle::BLACK_HOLE: return 0xd96b27;
	case EQmTitleBadgeStyle::MOON:
	default: return 0x9562ba;
	}
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

inline float QmNameplateTitleHeight(float NameFontSize, int ScalePercent)
{
	return std::max(0.0f, NameFontSize) * 0.8f * std::clamp(ScalePercent, 50, 200) / 100.0f;
}

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
	struct SBaseColors
	{
		unsigned m_Background;
		unsigned m_Border;
		unsigned m_Text;
		unsigned m_Icon;
	};
	SBaseColors Base{0x35273f, 0x695178, 0xedd8ff, 0xc397e2};
	switch(Appearance.m_Style)
	{
	case EQmTitleBadgeStyle::SUN:
		Base = {0x44301b, 0x8d6837, 0xffe6a8, 0xf2c66d};
		break;
	case EQmTitleBadgeStyle::EARTH:
		Base = {0x16293b, 0x3d6e96, 0xdaf0ff, 0x72bef5};
		break;
	case EQmTitleBadgeStyle::MORNING_STAR:
		Base = {0x2d2920, 0x7d6b43, 0xfff5d6, 0xf5db8c};
		break;
	case EQmTitleBadgeStyle::BLACK_HOLE:
		Base = {0x0d0907, 0x7a3b16, 0xffdfc2, 0xf08c3a};
		break;
	case EQmTitleBadgeStyle::MOON:
	default:
		break;
	}
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
	return {Shade(Base.m_Background), Shade(Base.m_Border), Shade(Base.m_Text), Shade(Base.m_Icon)};
}

#endif
