// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_TRAJECTORY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WEAPON_TRAJECTORY_H

#include <base/color.h>

#include <engine/graphics.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/mapitems.h>

#include <vector>

inline ColorRGBA QmWeaponTrajectoryBaseColor(
	int Weapon,
	const ColorRGBA &GrenadeColor,
	const ColorRGBA &RifleInnerColor,
	const ColorRGBA &ShotgunInnerColor)
{
	switch(Weapon)
	{
	case WEAPON_GUN:
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	case WEAPON_SHOTGUN:
		return ShotgunInnerColor;
	case WEAPON_LASER:
		return RifleInnerColor;
	case WEAPON_GRENADE:
	default:
		return GrenadeColor;
	}
}

inline ColorRGBA QmInvertWeaponTrajectoryColor(const ColorRGBA &Color)
{
	return ColorRGBA(1.0f - Color.r, 1.0f - Color.g, 1.0f - Color.b, Color.a);
}

inline bool QmWeaponTrajectoryUsesLineStyle(int Weapon)
{
	return Weapon == WEAPON_GUN || Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_LASER;
}

inline bool QmWeaponTrajectoryEnabledForWeapon(int Weapon, bool PistolGuideEnabled)
{
	if(Weapon == WEAPON_GUN)
		return PistolGuideEnabled;
	return Weapon == WEAPON_GRENADE || Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_LASER;
}

inline bool QmWeaponTrajectoryNinjaEnabled(int Weapon, bool PredictionEnabled, bool LocalPlayer)
{
	return PredictionEnabled && LocalPlayer && Weapon == WEAPON_NINJA;
}

inline bool QmWeaponTrajectoryCanHitOtherPlayers(
	int Weapon,
	bool GrenadeHitDisabled,
	bool LaserHitDisabled,
	bool ShotgunHitDisabled)
{
	switch(Weapon)
	{
	case WEAPON_GUN:
	case WEAPON_GRENADE:
		return !GrenadeHitDisabled;
	case WEAPON_LASER:
		return !LaserHitDisabled;
	case WEAPON_SHOTGUN:
		return !ShotgunHitDisabled;
	default:
		return false;
	}
}

inline bool QmWeaponTrajectoryHasTeleGun(
	int Weapon,
	bool HasGun,
	bool HasGrenade,
	bool HasLaser)
{
	switch(Weapon)
	{
	case WEAPON_GUN:
		return HasGun;
	case WEAPON_GRENADE:
		return HasGrenade;
	case WEAPON_LASER:
		return HasLaser;
	default:
		return false;
	}
}

inline bool QmWeaponTrajectoryIsTeleGunWall(int Weapon, int FrontTileIndex, int SwitchType, int SwitchDelay)
{
	int RequiredSwitchDelay;
	switch(Weapon)
	{
	case WEAPON_GUN:
		RequiredSwitchDelay = 1;
		break;
	case WEAPON_GRENADE:
		RequiredSwitchDelay = 2;
		break;
	case WEAPON_LASER:
		RequiredSwitchDelay = 3;
		break;
	default:
		return false;
	}

	const bool FrontAllowsTeleGun = FrontTileIndex == TILE_ALLOW_TELE_GUN || FrontTileIndex == TILE_ALLOW_BLUE_TELE_GUN;
	const bool SwitchAllowsTeleGun = SwitchType == TILE_ALLOW_TELE_GUN || SwitchType == TILE_ALLOW_BLUE_TELE_GUN;
	return FrontAllowsTeleGun || (SwitchAllowsTeleGun && (SwitchDelay == 0 || SwitchDelay == RequiredSwitchDelay));
}

class CQmWeaponTrajectory : public CComponent
{
	std::vector<vec2> m_vPoints;
	std::vector<IGraphics::CLineItem> m_vLineSegments;
	std::vector<IGraphics::CFreeformItem> m_vLineQuadSegments;

public:
	int Sizeof() const override { return sizeof(*this); }
	bool IsVisible() const;
	bool PredictNinjaEndPosition(vec2 Position, vec2 Direction, vec2 &OutPosition) const;
	void Render(const CNetObj_Character *pPrevChar, const CNetObj_Character *pPlayerChar, int ClientId);
};

#endif
