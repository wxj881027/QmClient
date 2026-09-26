#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_ANIMATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_ANIMATION_H

#include <base/math.h>

#include <game/gamecore.h>

#include <algorithm>

struct SQmWeaponReloadRotation
{
	bool m_Active = false;
	float m_Angle = 0.0f;
};

struct SQmWeaponReloadAnimationState
{
	int m_ObservedAttackTick = -1;
	int m_AttackWeapon = -1;
	int m_AttackTuneZone = 0;
	bool m_PlayReloadAnimation = false;

	void ObserveAttack(int AttackTick, int Weapon, int TuneZone, bool PlayReloadAnimation)
	{
		if(AttackTick == m_ObservedAttackTick)
			return;
		m_ObservedAttackTick = AttackTick;
		m_AttackWeapon = AttackTick > 0 ? Weapon : -1;
		m_AttackTuneZone = TuneZone;
		m_PlayReloadAnimation = AttackTick > 0 && PlayReloadAnimation;
	}

	bool MatchesAttack(int AttackTick, int Weapon) const
	{
		return m_PlayReloadAnimation && AttackTick > 0 && AttackTick == m_ObservedAttackTick && Weapon == m_AttackWeapon;
	}
};

inline bool QmWeaponReloadAnimationSelected(int Probability, float RandomValue)
{
	Probability = std::clamp(Probability, 0, 100);
	return Probability >= 100 || (Probability > 0 && RandomValue < Probability / 100.0f);
}

inline bool QmWeaponUsesReloadFlip(int Weapon)
{
	return Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_GRENADE || Weapon == WEAPON_LASER;
}

inline int QmWeaponReloadTicks(float ReloadSeconds, int TickSpeed)
{
	return ReloadSeconds > 0.0f && TickSpeed > 0 ? (int)(ReloadSeconds * TickSpeed) : 0;
}

inline bool QmWeaponReloadAnimationEligible(float ReloadSeconds, float DefaultReloadSeconds, int TickSpeed)
{
	return ReloadSeconds > 0.0f && DefaultReloadSeconds > 0.0f && ReloadSeconds < DefaultReloadSeconds * 1.5f && QmWeaponReloadTicks(ReloadSeconds, TickSpeed) > 0;
}

inline float QmWeaponReloadDelaySeconds(const CTuningParams &Tuning, int Weapon)
{
	if(Weapon < WEAPON_HAMMER || Weapon > WEAPON_NINJA)
		return 0.0f;
	return Tuning.GetWeaponFireDelay(Weapon);
}

inline float QmWeaponFlipAngle(float Progress, bool FacingLeft)
{
	constexpr float aKeyframeProgress[] = {0.0f, 0.10f, 0.35f, 0.60f, 0.78f, 0.90f, 1.0f};
	constexpr float aKeyframeDegrees[] = {0.0f, -8.0f, 120.0f, 240.0f, 370.0f, 355.0f, 360.0f};

	Progress = std::clamp(Progress, 0.0f, 1.0f);
	float AngleDegrees = aKeyframeDegrees[0];
	for(int i = 1; i < 7; ++i)
	{
		if(Progress > aKeyframeProgress[i])
			continue;
		const float SegmentProgress = (Progress - aKeyframeProgress[i - 1]) / (aKeyframeProgress[i] - aKeyframeProgress[i - 1]);
		const float SmoothProgress = SegmentProgress * SegmentProgress * (3.0f - 2.0f * SegmentProgress);
		AngleDegrees = mix(aKeyframeDegrees[i - 1], aKeyframeDegrees[i], SmoothProgress);
		break;
	}

	const float Angle = AngleDegrees * pi / 180.0f;
	return FacingLeft ? -Angle : Angle;
}

inline SQmWeaponReloadRotation QmWeaponReloadRotation(float TimeSinceAttack, float ReloadSeconds, float DefaultReloadSeconds, int TickSpeed, bool FacingLeft)
{
	if(TimeSinceAttack < 0.0f || !QmWeaponReloadAnimationEligible(ReloadSeconds, DefaultReloadSeconds, TickSpeed))
		return {};

	const int ReloadTicks = QmWeaponReloadTicks(ReloadSeconds, TickSpeed);
	const float ReloadDuration = ReloadTicks / (float)TickSpeed;
	if(TimeSinceAttack >= ReloadDuration)
		return {};

	return {true, QmWeaponFlipAngle(TimeSinceAttack / ReloadDuration, FacingLeft)};
}

inline float QmResolveWeaponAnimationRotation(float WeaponSwitchRotation, const SQmWeaponReloadRotation &ReloadRotation, bool FireOverridesSwitchRotation)
{
	if(ReloadRotation.m_Active)
		return ReloadRotation.m_Angle;
	return FireOverridesSwitchRotation ? 0.0f : WeaponSwitchRotation;
}

// Gores 模式自动切锤会让武器在锤子与原武器之间来回切换，每次都播放切换动画会很吵。
// 只有选项开启、Gores 武器循环确实接管了武器、且这次切换涉及锤子时才跳过动画。
inline bool QmShouldSkipGoresHammerSwitchAnimation(bool SuppressEnabled, bool GoresWeaponCycleActive, int PreviousWeapon, int CurrentWeapon)
{
	return SuppressEnabled && GoresWeaponCycleActive && (PreviousWeapon == WEAPON_HAMMER || CurrentWeapon == WEAPON_HAMMER);
}

#endif
