/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_LOGIC_H

#include <base/vmath.h>

#include <algorithm>

struct SQmPlayerIndicatorCandidate
{
	bool m_OtherActive = false;
	bool m_OtherIsLocal = false;
	bool m_OtherSpectator = false;
	int m_OtherTeam = 0;
	int m_LocalTeam = 0;
	int m_LocalRaceTeam = 0;
	bool m_TeamOnly = true;
	bool m_FrozenOnly = false;
	bool m_OtherFrozen = false;
	bool m_HideVisible = false;
	bool m_OtherVisible = false;
};

inline bool QmPlayerIndicatorShouldRender(const SQmPlayerIndicatorCandidate &Candidate)
{
	if(!Candidate.m_OtherActive || Candidate.m_OtherIsLocal || Candidate.m_OtherSpectator)
		return false;
	if(Candidate.m_TeamOnly && Candidate.m_LocalRaceTeam == 0)
		return false;
	if(Candidate.m_TeamOnly && Candidate.m_OtherTeam != Candidate.m_LocalTeam)
		return false;
	if(Candidate.m_FrozenOnly && !Candidate.m_OtherFrozen)
		return false;
	if(Candidate.m_HideVisible && Candidate.m_OtherVisible)
		return false;
	return true;
}

inline float QmPlayerIndicatorOffset(float BaseOffset, float MaxOffset, bool VariableDistance, int MaxDistance, float Distance)
{
	if(!VariableDistance || MaxDistance <= 0)
		return BaseOffset;
	const float Progress = std::min(Distance / static_cast<float>(MaxDistance), 1.0f);
	return BaseOffset + (MaxOffset - BaseOffset) * Progress;
}

inline bool QmPlayerIndicatorIsUnfreezing(bool Frozen, bool InFreeze)
{
	return Frozen && !InFreeze;
}

inline vec2 QmPlayerIndicatorPosition(vec2 LocalPosition, vec2 OtherPosition, float Offset)
{
	const vec2 Delta = OtherPosition - LocalPosition;
	const float Distance = length(Delta);
	if(Distance <= 0.0001f)
		return LocalPosition;
	return LocalPosition + Delta / Distance * Offset;
}

#endif
