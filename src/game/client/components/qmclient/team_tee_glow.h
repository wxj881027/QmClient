#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TEAM_TEE_GLOW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TEAM_TEE_GLOW_H

#include <base/color.h>
#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>

// 颜色与强度独立；忽略旧选择器保存的零 alpha，兼容已有配置。
inline bool QmResolveTeamTeeGlowColor(bool Enabled, bool Active, int Team, bool SuperTeam, int Mode, unsigned CustomColor, ColorRGBA TeeColor, ColorRGBA TeamColor, double Seconds, int ClientId, ColorRGBA &Color)
{
	if(!Enabled || !Active || SuperTeam || Team < 0 || ClientId < 0)
		return false;
	if(Team > 0)
		Color = TeamColor.WithAlpha(1.0f);
	else
	{
		switch(Mode)
		{
		case 1: Color = TeeColor.WithAlpha(1.0f); break;
		case 2: Color = color_cast<ColorRGBA>(ColorHSLA(CustomColor)); break;
		case 3:
		{
			// 双精度取相位，避免长时间运行后的时钟精度损失。
			double Hue = std::fmod((std::isfinite(Seconds) ? Seconds : 0.0) / 10.0 + ClientId * static_cast<double>(normalized_golden_angle), 1.0);
			if(Hue < 0.0)
				Hue += 1.0;
			Color = color_cast<ColorRGBA>(ColorHSLA(static_cast<float>(Hue), 1.0f, 0.6f));
			break;
		}
		default: return false;
		}
	}
	return true;
}

struct SQmTeamTeeGlowLayer
{
	float m_Scale;
	float m_Alpha;
};

// 固定三层；强度和尺寸只改变参数，不增加绘制次数。
inline std::array<SQmTeamTeeGlowLayer, 3> QmTeamTeeGlowLayers(int Strength, int Size, float PlayerAlpha)
{
	const float Opacity = std::clamp(Strength, 0, 100) / 100.0f;
	const float Radius = std::clamp(Size, 0, 100) / 100.0f;
	const float Alpha = std::isfinite(PlayerAlpha) ? std::clamp(PlayerAlpha, 0.0f, 1.0f) : 0.0f;
	return {{{1.0f + Radius * 0.60f, Alpha * Opacity * 0.30f},
		{1.0f + Radius * 0.36f, Alpha * Opacity * 0.55f},
		{1.0f + Radius * 0.14f, Alpha * Opacity * 0.85f}}};
}

#endif
