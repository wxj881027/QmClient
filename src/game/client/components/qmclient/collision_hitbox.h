// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLLISION_HITBOX_H

#include "collision_hitbox_logic.h"

#include <base/color.h>

#include <game/client/component.h>

#include <vector>

class CTile;
class CTeleTile;

// 碰撞体积类型枚举
enum ECollisionHitboxType
{
	HITBOX_NONE = 0,
	HITBOX_SOLID,
	HITBOX_FREEZE,
	HITBOX_DFREEZE,
	HITBOX_DEATH,
};

class CCollisionHitbox : public CComponent
{
private:
	ivec2 m_MapDataSize;
	std::vector<int> m_vMapData;

	// 单位圆方向按分段数预计算并复用，避免每次绘制重复三角运算。
	CQmHitboxCircleDirections m_CircleDirections;

	float HitboxAlpha() const;
	bool HitboxModeEnabled() const;
	bool LegacyModeEnabled() const;
	bool ShouldRenderClient(int ClientId) const;
	bool IsOnScreen(vec2 Position, float Margin = 0.0f) const;
	bool IsLineOnScreen(vec2 From, vec2 To, float Margin = 0.0f) const;
	ColorRGBA FreezeColor(float Alpha) const;
	ColorRGBA TeeColor(int ClientId, float Alpha) const;
	ColorRGBA WeaponColor(float Alpha) const;
	void DrawCross(vec2 Position, float Size, ColorRGBA Color);
	void DrawCircleOutline(vec2 Center, float Radius, ColorRGBA Color, int Segments = 32);
	void DrawBoxOutline(vec2 Center, float Radius, ColorRGBA Color);
	void DrawCapsuleOutline(vec2 From, vec2 To, float Radius, ColorRGBA Color, int ArcSegments = 16);
	bool GetProjectileRenderPosition(const class CProjectileData &Projectile, vec2 &Position, vec2 &PreviousPosition) const;

	void RenderTileHitboxes();
	void RenderTeeHitboxes();
	void RenderPickupHitboxes();
	void RenderWeaponHitboxes();
	void RenderHammerHitboxes();
	void RenderProjectileHitboxes();
	void RenderLaserHitboxes();
	void RenderHookHitboxes();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnRender() override;
};

#endif
