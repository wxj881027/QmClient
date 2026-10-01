#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_STYLE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_STYLE_H

#include <base/color.h>
#include <base/str.h>

#include <algorithm>
#include <array>
#include <cmath>

enum class EQmSponsorChatStyle
{
	NONE = 0,
	SOFT_GLOW,
	PLATINUM,
};

inline EQmSponsorChatStyle QmSponsorChatStyleFromId(const char *pId)
{
	if(pId != nullptr && str_comp(pId, "soft_glow") == 0)
		return EQmSponsorChatStyle::SOFT_GLOW;
	if(pId != nullptr && str_comp(pId, "platinum") == 0)
		return EQmSponsorChatStyle::PLATINUM;
	return EQmSponsorChatStyle::NONE;
}

inline const char *QmSponsorChatStyleId(EQmSponsorChatStyle Style)
{
	switch(Style)
	{
	case EQmSponsorChatStyle::SOFT_GLOW: return "soft_glow";
	case EQmSponsorChatStyle::PLATINUM: return "platinum";
	default: return "";
	}
}

inline EQmSponsorChatStyle QmSponsorChatMessageStyle(EQmSponsorChatStyle Style, bool Highlighted, bool Team, bool Whisper, bool CustomColor, bool Emoji)
{
	return Highlighted || Team || Whisper || CustomColor || Emoji ? EQmSponsorChatStyle::NONE : Style;
}

inline EQmSponsorChatStyle QmSponsorChatMergedStyle(EQmSponsorChatStyle Previous, EQmSponsorChatStyle Incoming)
{
	return Previous == Incoming ? Previous : EQmSponsorChatStyle::NONE;
}

inline float QmSponsorChatGlowRadius(float FontSizePx)
{
	return std::isfinite(FontSizePx) && FontSizePx > 0.0f ? std::clamp(FontSizePx * 0.1f, 1.0f, 2.5f) : 0.0f;
}

inline ColorRGBA QmSponsorChatPlatinumColor(float Position, float Alpha)
{
	// 静态窄亮带，只改变亮度和低饱和色温，不循环色相。
	static constexpr std::array<float, 5> s_aPositions = {0.0f, 0.42f, 0.64f, 0.78f, 1.0f};
	static const std::array<ColorRGBA, 5> s_aColors = {
		ColorRGBA(0.76f, 0.80f, 0.85f, 1.0f),
		ColorRGBA(0.87f, 0.90f, 0.93f, 1.0f),
		ColorRGBA(1.0f, 0.98f, 0.92f, 1.0f),
		ColorRGBA(0.90f, 0.92f, 0.94f, 1.0f),
		ColorRGBA(0.78f, 0.82f, 0.87f, 1.0f),
	};
	Position = std::isfinite(Position) ? std::clamp(Position, 0.0f, 1.0f) : 0.0f;
	size_t Index = 0;
	while(Index + 2 < s_aPositions.size() && Position > s_aPositions[Index + 1])
		++Index;
	const float Amount = (Position - s_aPositions[Index]) / (s_aPositions[Index + 1] - s_aPositions[Index]);
	const ColorRGBA &From = s_aColors[Index];
	const ColorRGBA &To = s_aColors[Index + 1];
	return ColorRGBA(From.r + (To.r - From.r) * Amount, From.g + (To.g - From.g) * Amount,
		From.b + (To.b - From.b) * Amount, Alpha);
}

#endif
