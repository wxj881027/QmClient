#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_STYLE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_STYLE_H

#include <base/color.h>
#include <base/str.h>
#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>

enum class EQmSponsorChatStyle
{
	NONE = 0,
	PLATINUM,
	FLUID,
};

inline EQmSponsorChatStyle QmSponsorChatStyleFromId(const char *pId)
{
	if(pId != nullptr && str_comp(pId, "platinum") == 0)
		return EQmSponsorChatStyle::PLATINUM;
	if(pId != nullptr && str_comp(pId, "fluid") == 0)
		return EQmSponsorChatStyle::FLUID;
	return EQmSponsorChatStyle::NONE;
}

inline const char *QmSponsorChatStyleId(EQmSponsorChatStyle Style)
{
	switch(Style)
	{
	case EQmSponsorChatStyle::PLATINUM: return "platinum";
	case EQmSponsorChatStyle::FLUID: return "fluid";
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

struct SQmSponsorChatSweep
{
	int m_Line = -1;
	float m_Progress = 0.0f;
};

inline SQmSponsorChatSweep QmSponsorChatSweepState(double AgeSeconds, int LineCount, bool LatestVisibleSponsor)
{
	if(!std::isfinite(AgeSeconds) || AgeSeconds < 0.0 || LineCount <= 0)
		return {};
	// 每行扫一秒，整条消息完成后停一秒；最新可见赞助消息持续重播。
	const double Phase = LatestVisibleSponsor ? std::fmod(AgeSeconds, static_cast<double>(LineCount) + 1.0) : AgeSeconds;
	if(Phase >= LineCount)
		return {};
	const int Line = static_cast<int>(Phase);
	return {Line, static_cast<float>(Phase - Line)};
}

inline ColorRGBA QmSponsorChatPlatinumColor(float Position, float Alpha)
{
	// 银色底保留明暗层次，为独立扫过的白色高光留出亮度空间。
	static constexpr std::array<float, 5> s_aPositions = {0.0f, 0.42f, 0.64f, 0.78f, 1.0f};
	static const std::array<ColorRGBA, 5> s_aColors = {
		ColorRGBA(0.65f, 0.70f, 0.74f, 1.0f),
		ColorRGBA(0.78f, 0.82f, 0.85f, 1.0f),
		ColorRGBA(0.84f, 0.85f, 0.86f, 1.0f),
		ColorRGBA(0.76f, 0.79f, 0.84f, 1.0f),
		ColorRGBA(0.61f, 0.66f, 0.70f, 1.0f),
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

// 坐标以字号为单位，整段文字共享流场；周期回绕保持连续，避免长时间运行损失精度。
class CQmSponsorChatFluid
{
	float m_Phase;

public:
	explicit CQmSponsorChatFluid(double TimeSeconds) :
		m_Phase(std::isfinite(TimeSeconds) && TimeSeconds >= 0.0 ? static_cast<float>(std::fmod(TimeSeconds, 12.0) / 12.0) * 2.0f * pi : 0.0f)
	{
	}

	ColorRGBA Sample(vec2 Position) const
	{
		const float Warp = 0.35f * std::sin(Position.x * 0.9f + Position.y * 1.7f - m_Phase) +
			0.18f * std::sin(Position.x * 1.9f - Position.y * 0.8f + 2.0f * m_Phase);
		const float Wave = 0.5f + 0.5f * std::sin(Position.y * 4.2f + Position.x * 0.7f + Warp * 2.8f - m_Phase);
		float Crest = 0.5f + 0.5f * std::sin(Position.y * 5.8f - Position.x * 1.1f + Warp * 3.0f + 2.0f * m_Phase);
		Crest *= Crest;
		Crest *= Crest;
		Crest *= Crest;
		const ColorRGBA Deep(0.025f, 0.32f, 0.70f, 1.0f);
		const ColorRGBA Cyan(0.08f, 0.78f, 0.98f, 1.0f);
		const ColorRGBA Foam(0.80f, 0.97f, 1.0f, 1.0f);
		const float Blend = Wave * Wave * (3.0f - 2.0f * Wave);
		return ColorRGBA(mix(mix(Deep.r, Cyan.r, Blend), Foam.r, Crest * 0.75f),
			mix(mix(Deep.g, Cyan.g, Blend), Foam.g, Crest * 0.75f),
			mix(mix(Deep.b, Cyan.b, Blend), Foam.b, Crest * 0.75f), 1.0f);
	}
};

#endif
