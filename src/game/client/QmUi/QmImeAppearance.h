#ifndef GAME_CLIENT_QMUI_QMIMEAPPEARANCE_H
#define GAME_CLIENT_QMUI_QMIMEAPPEARANCE_H

#include "QmColorGradient.h"
#include "QmTheme.h"

#include <engine/shared/config.h>

struct SQmImeAppearance
{
	SQmColorGradient m_Background;
	float m_BackgroundOpacity = 1.0f;
	SQmColorGradient m_Text;
	SQmColorGradient m_SelectedText;
	SQmColorGradient m_Selection;
	qm_theme::SImeTheme m_Theme;
};

inline SQmImeAppearance QmImeAppearance(const CConfig &Config)
{
	SQmImeAppearance Result;
	Result.m_Theme = qm_theme::ImeTheme(true);
	Result.m_BackgroundOpacity = std::clamp(Config.m_QmImeOpacity, 0, 100) / 100.0f;
	Result.m_Background = SQmColorGradient::FromConfig(Config.m_QmImeBgGradient,
		color_cast<ColorRGBA>(ColorHSLA(Config.m_QmImeBgColor)).WithAlpha(Result.m_BackgroundOpacity),
		Config.m_QmImeBgGradientType, Config.m_QmImeBgGradientAngle, Config.m_QmImeBgGradientCenterX, Config.m_QmImeBgGradientCenterY, Config.m_QmImeBgGradientRange, Config.m_QmImeBgGradientReverse != 0);
	Result.m_Text = SQmColorGradient::FromConfig(Config.m_QmImeTextGradient, color_cast<ColorRGBA>(ColorHSLA(Config.m_QmImeTextColor, true)),
		Config.m_QmImeTextGradientType, Config.m_QmImeTextGradientAngle, Config.m_QmImeTextGradientCenterX, Config.m_QmImeTextGradientCenterY, Config.m_QmImeTextGradientRange, Config.m_QmImeTextGradientReverse != 0);
	Result.m_SelectedText = SQmColorGradient::FromConfig(Config.m_QmImeSelectedTextGradient, color_cast<ColorRGBA>(ColorHSLA(Config.m_QmImeSelectedTextColor, true)),
		Config.m_QmImeSelectedTextGradientType, Config.m_QmImeSelectedTextGradientAngle, Config.m_QmImeSelectedTextGradientCenterX, Config.m_QmImeSelectedTextGradientCenterY, Config.m_QmImeSelectedTextGradientRange, Config.m_QmImeSelectedTextGradientReverse != 0);
	Result.m_Selection = SQmColorGradient::FromConfig(Config.m_QmImeSelectedGradient, color_cast<ColorRGBA>(ColorHSLA(Config.m_QmImeSelectedColor, true)),
		Config.m_QmImeSelectedGradientType, Config.m_QmImeSelectedGradientAngle, Config.m_QmImeSelectedGradientCenterX, Config.m_QmImeSelectedGradientCenterY, Config.m_QmImeSelectedGradientRange, Config.m_QmImeSelectedGradientReverse != 0);
	Result.m_Theme.m_Text = Result.m_Text.m_aColors[0];
	Result.m_Theme.m_TextMuted = Result.m_Theme.m_Text;
	Result.m_Theme.m_TextMuted.a *= 0.62f;
	Result.m_Theme.m_TextSelected = Result.m_SelectedText.m_aColors[0];
	Result.m_Theme.m_SelectedBg = Result.m_Selection.m_aColors[0];
	const float Scale = std::clamp(Config.m_QmImeFontSize, 75, 200) / 100.0f;
	Result.m_Theme.m_FontCandidate *= Scale;
	Result.m_Theme.m_FontComposition *= Scale;
	return Result;
}

inline float QmImeAppearanceCardHeight(float LineHeight, float ButtonHeight, float Spacing)
{
	return 19.0f * std::max(LineHeight, ButtonHeight) + 16.0f * Spacing;
}

#endif
