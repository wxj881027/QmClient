#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_DEVELOPER_BADGE_MOTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_DEVELOPER_BADGE_MOTION_H

#include <base/vmath.h>

#include <array>
#include <cmath>

constexpr float QM_DEVELOPER_BADGE_WIDTH = 91.0f;
constexpr float QM_DEVELOPER_BADGE_HEIGHT = 24.0f;
constexpr double QM_DEVELOPER_BADGE_PERIOD = 6.0;
constexpr int QM_DEVELOPER_BADGE_TRAIL_COUNT = 23;

struct SQmDeveloperBadgeMotion
{
	std::array<vec2, QM_DEVELOPER_BADGE_TRAIL_COUNT> m_aOrbit;
	std::array<vec2, 2> m_aCircuit;
};

// Figma 的轨迹位于 1px 内描边的中心，距离沿直线和两端半圆连续累计。
inline vec2 QmDeveloperBadgeOrbitPoint(float Distance)
{
	constexpr float Radius = 11.5f;
	constexpr float Straight = QM_DEVELOPER_BADGE_WIDTH - QM_DEVELOPER_BADGE_HEIGHT;
	constexpr float Arc = pi * Radius;
	constexpr float Perimeter = 2.0f * (Straight + Arc);
	Distance = std::fmod(Distance, Perimeter);
	if(Distance < 0.0f)
		Distance += Perimeter;
	if(Distance <= Straight)
		return {12.0f + Distance, 0.5f};
	Distance -= Straight;
	if(Distance <= Arc)
	{
		const float Angle = -pi * 0.5f + Distance / Radius;
		return {79.0f + Radius * std::cos(Angle), 12.0f + Radius * std::sin(Angle)};
	}
	Distance -= Arc;
	if(Distance <= Straight)
		return {79.0f - Distance, 23.5f};
	const float Angle = pi * 0.5f + (Distance - Straight) / Radius;
	return {12.0f + Radius * std::cos(Angle), 12.0f + Radius * std::sin(Angle)};
}

inline vec2 QmDeveloperBadgeCircuitPoint(int Index, float Progress)
{
	const vec2 aaRoutes[2][4] = {
		{{43.0f, 1.0f}, {47.0f, 5.0f}, {81.0f, 5.0f}, {84.8f, 1.96f}},
		{{40.0f, 23.0f}, {44.0f, 19.0f}, {80.0f, 19.0f}, {84.0f, 22.2f}},
	};
	const auto &aRoute = aaRoutes[Index];
	float aLengths[3];
	float Total = 0.0f;
	for(int i = 0; i < 3; ++i)
	{
		aLengths[i] = distance(aRoute[i], aRoute[i + 1]);
		Total += aLengths[i];
	}
	float Remaining = Progress * Total;
	for(int i = 0; i < 3; ++i)
	{
		if(Remaining <= aLengths[i] || i == 2)
			return mix(aRoute[i], aRoute[i + 1], Remaining / aLengths[i]);
		Remaining -= aLengths[i];
	}
	return aRoute[3];
}

inline SQmDeveloperBadgeMotion QmSampleDeveloperBadgeMotion(double TimeSec)
{
	// 保留设计中的 72 段线性关键帧；时间先取模，避免长时间运行损失相位精度。
	double Phase = std::fmod(TimeSec, QM_DEVELOPER_BADGE_PERIOD) / QM_DEVELOPER_BADGE_PERIOD;
	if(Phase < 0.0)
		Phase += 1.0;
	const double Frame = Phase * 72.0;
	const float From = static_cast<float>(std::floor(Frame) / 72.0);
	const float To = From + 1.0f / 72.0f;
	const float Blend = static_cast<float>(Frame - std::floor(Frame));
	constexpr float Perimeter = 2.0f * (67.0f + pi * 11.5f);
	SQmDeveloperBadgeMotion Result;
	for(int i = 0; i < QM_DEVELOPER_BADGE_TRAIL_COUNT; ++i)
	{
		const float Offset = 18.75f - i * 0.7f;
		Result.m_aOrbit[i] = mix(QmDeveloperBadgeOrbitPoint(Offset + Perimeter * From),
			QmDeveloperBadgeOrbitPoint(Offset + Perimeter * To), Blend);
	}
	const float ProgressFrom = (1.0f - std::cos(2.0f * pi * From + 0.9f)) * 0.5f;
	const float ProgressTo = (1.0f - std::cos(2.0f * pi * To + 0.9f)) * 0.5f;
	for(int i = 0; i < 2; ++i)
		Result.m_aCircuit[i] = mix(QmDeveloperBadgeCircuitPoint(i, i == 0 ? ProgressFrom : 1.0f - ProgressFrom),
			QmDeveloperBadgeCircuitPoint(i, i == 0 ? ProgressTo : 1.0f - ProgressTo), Blend);
	return Result;
}

#endif
