// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "collision_hitbox.h"

#include <base/log.h>
#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/laser_data.h>
#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/projectile_data.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// 地图层类型
enum class HitboxLayer
{
	GAME,
	FRONT
};

class CHitboxLayer
{
private:
	CMapItemLayerTilemap *GetLayer(CGameClient *pThis) const
	{
		if(m_Type == HitboxLayer::GAME)
			return pThis->Layers()->GameLayer();
		if(m_Type == HitboxLayer::FRONT)
			return pThis->Layers()->FrontLayer();
		return nullptr;
	}
	int GetLayerData(CGameClient *pThis) const
	{
		if(m_Type == HitboxLayer::GAME)
			return pThis->Layers()->GameLayer()->m_Data;
		if(m_Type == HitboxLayer::FRONT)
			return pThis->Layers()->FrontLayer()->m_Front;
		return -1;
	}

public:
	const HitboxLayer m_Type;

	void GetMeta(CGameClient *pThis, ivec2 &Size) const
	{
		Size = {0, 0};
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const int DataSize = pThis->Layers()->Map()->GetDataSize(GetLayerData(pThis));
		if(DataSize <= 0 || (size_t)DataSize < (size_t)pLayer->m_Width * (size_t)pLayer->m_Height * sizeof(CTile))
			return;
		Size = {pLayer->m_Width, pLayer->m_Height};
	}

	void SetData(CGameClient *pThis, int *pData, const ivec2 &Size) const
	{
		const auto *pLayer = GetLayer(pThis);
		if(!pLayer)
			return;
		const auto *pTiles = (CTile *)pThis->Layers()->Map()->GetData(GetLayerData(pThis));
		if(!pTiles)
			return;

		for(int y = 0; y < pLayer->m_Height; ++y)
		{
			for(int x = 0; x < pLayer->m_Width; ++x)
			{
				const int Index = y * pLayer->m_Width + x;
				const int IndexOut = y * Size.x + x;
				const auto Tile = pTiles[Index].m_Index;

				// 按优先级设置碰撞类型
				if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
				{
					if(pData[IndexOut] < HITBOX_SOLID)
						pData[IndexOut] = HITBOX_SOLID;
				}
				else if(Tile == TILE_FREEZE || Tile == TILE_LFREEZE)
				{
					if(pData[IndexOut] < HITBOX_FREEZE)
						pData[IndexOut] = HITBOX_FREEZE;
				}
				else if(Tile == TILE_DFREEZE)
				{
					if(pData[IndexOut] < HITBOX_DFREEZE)
						pData[IndexOut] = HITBOX_DFREEZE;
				}
				else if(Tile == TILE_DEATH)
				{
					if(pData[IndexOut] < HITBOX_DEATH)
						pData[IndexOut] = HITBOX_DEATH;
				}
			}
		}
	}
};

// 地图层顺序
static constexpr CHitboxLayer HITBOX_LAYERS[] = {{HitboxLayer::GAME}, {HitboxLayer::FRONT}};
static constexpr size_t MAX_HITBOX_MAP_TILES = 4096 * 4096;

float CCollisionHitbox::HitboxAlpha() const
{
	const int Alpha = HitboxModeEnabled() ? g_Config.m_QmHitboxAlpha : g_Config.m_QmCollisionHitboxAlpha;
	return std::clamp(Alpha, 0, 100) / 100.0f;
}

bool CCollisionHitbox::HitboxModeEnabled() const
{
	return g_Config.m_QmHitboxMode != 0;
}

bool CCollisionHitbox::LegacyModeEnabled() const
{
	return !HitboxModeEnabled() && g_Config.m_QmShowCollisionHitbox != 0;
}

bool CCollisionHitbox::ShouldRenderClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(!HitboxModeEnabled())
		return true;

	const int Scope = std::clamp(g_Config.m_QmHitboxPlayerScope, 0, 2);
	if(Scope == 2)
		return true;

	const int ActiveLocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(Scope == 0)
		return ClientId == ActiveLocalId;

	return ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
}

bool CCollisionHitbox::IsOnScreen(vec2 Position, float Margin) const
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	return Position.x >= ScreenX0 - Margin && Position.x <= ScreenX1 + Margin &&
	       Position.y >= ScreenY0 - Margin && Position.y <= ScreenY1 + Margin;
}

bool CCollisionHitbox::IsLineOnScreen(vec2 From, vec2 To, float Margin) const
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float MinX = std::min(From.x, To.x);
	const float MaxX = std::max(From.x, To.x);
	const float MinY = std::min(From.y, To.y);
	const float MaxY = std::max(From.y, To.y);
	return MaxX >= ScreenX0 - Margin && MinX <= ScreenX1 + Margin &&
	       MaxY >= ScreenY0 - Margin && MinY <= ScreenY1 + Margin;
}

ColorRGBA CCollisionHitbox::FreezeColor(float Alpha) const
{
	const int ColorConfig = HitboxModeEnabled() ? g_Config.m_QmHitboxColorFreeze : g_Config.m_QmCollisionHitboxColorFreeze;
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(ColorConfig));
	Color.a = Alpha;
	return Color;
}

ColorRGBA CCollisionHitbox::TeeColor(int ClientId, float Alpha) const
{
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHitboxColorTee));
	if(ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
		Color.a = Alpha;
	else if(ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		Color.a = Alpha * 0.8f;
	else
		Color.a = Alpha * 0.6f;
	return Color;
}

ColorRGBA CCollisionHitbox::WeaponColor(float Alpha) const
{
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHitboxColorWeapon));
	Color.a = Alpha;
	return Color;
}

void CCollisionHitbox::DrawCross(vec2 Position, float Size, ColorRGBA Color)
{
	if(Color.a <= 0.0f)
		return;

	Graphics()->SetColor(Color);
	IGraphics::CLineItem aLines[2] = {
		{Position.x - Size, Position.y, Position.x + Size, Position.y},
		{Position.x, Position.y - Size, Position.x, Position.y + Size}};
	Graphics()->LinesDraw(aLines, 2);
}

void CCollisionHitbox::DrawCircleOutline(vec2 Center, float Radius, ColorRGBA Color, int Segments)
{
	if(Radius <= 0.0f || Color.a <= 0.0f)
		return;

	Segments = std::clamp(Segments, CQmHitboxCircleDirections::MIN_SEGMENTS, CQmHitboxCircleDirections::MAX_SEGMENTS);
	IGraphics::CLineItem aLines[CQmHitboxCircleDirections::MAX_SEGMENTS];
	const vec2 *pDirections = m_CircleDirections.Get(Segments);
	vec2 Previous = Center + pDirections[0] * Radius;
	for(int Segment = 0; Segment < Segments; ++Segment)
	{
		const vec2 Current = Center + pDirections[Segment + 1] * Radius;
		aLines[Segment] = IGraphics::CLineItem(Previous, Current);
		Previous = Current;
	}

	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, Segments);
}

void CCollisionHitbox::DrawBoxOutline(vec2 Center, float Radius, ColorRGBA Color)
{
	if(Radius <= 0.0f || Color.a <= 0.0f)
		return;

	const float Left = Center.x - Radius;
	const float Right = Center.x + Radius;
	const float Top = Center.y - Radius;
	const float Bottom = Center.y + Radius;
	IGraphics::CLineItem aLines[4] = {
		{Left, Top, Right, Top},
		{Left, Bottom, Right, Bottom},
		{Left, Top, Left, Bottom},
		{Right, Top, Right, Bottom}};

	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, 4);
}

void CCollisionHitbox::DrawCapsuleOutline(vec2 From, vec2 To, float Radius, ColorRGBA Color, int ArcSegments)
{
	if(Color.a <= 0.0f)
		return;

	IGraphics::CLineItem aLines[COLLISION_HITBOX_CAPSULE_MAX_LINES];
	int NumLines = 0;
	BuildHitboxCapsuleOutline(From, To, Radius, ArcSegments, [&](vec2 LineFrom, vec2 LineTo) {
		aLines[NumLines++] = IGraphics::CLineItem(LineFrom, LineTo);
	});
	if(NumLines == 0)
		return;

	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, NumLines);
}

bool CCollisionHitbox::GetProjectileRenderPosition(const CProjectileData &Projectile, vec2 &Position, vec2 &PreviousPosition) const
{
	const int Weapon = std::clamp(Projectile.m_Type, 0, NUM_WEAPONS - 1);
	float Curvature = 0.0f;
	float Speed = 0.0f;
	const int TuneZone = std::clamp(Projectile.m_TuneZone, 0, NUM_TUNEZONES - 1);
	const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
	if(Weapon == WEAPON_GRENADE)
	{
		Curvature = pTuning->m_GrenadeCurvature;
		Speed = pTuning->m_GrenadeSpeed;
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		Curvature = pTuning->m_ShotgunCurvature;
		Speed = pTuning->m_ShotgunSpeed;
	}
	else if(Weapon == WEAPON_GUN)
	{
		Curvature = pTuning->m_GunCurvature;
		Speed = pTuning->m_GunSpeed;
	}
	else
	{
		return false;
	}

	bool LocalPlayerInGame = false;
	if(GameClient()->m_Snap.m_pLocalInfo)
		LocalPlayerInGame = GameClient()->m_aClients[GameClient()->m_Snap.m_pLocalInfo->m_ClientId].m_Team != TEAM_SPECTATORS;

	static float s_LastGameTickTime = Client()->GameTickTime(g_Config.m_ClDummy);
	if(GameClient()->m_Snap.m_pGameInfoObj && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
		s_LastGameTickTime = Client()->GameTickTime(g_Config.m_ClDummy);

	const bool IsOtherTeam = Projectile.m_ExtraInfo && Projectile.m_Owner >= 0 && GameClient()->IsOtherTeam(Projectile.m_Owner);
	const int PredictionTick = Client()->GetPredictionTick();

	float CurrentTime;
	if(GameClient()->Predict() && GameClient()->AntiPingGrenade() && LocalPlayerInGame && !IsOtherTeam)
		CurrentTime = ((float)(PredictionTick - 1 - Projectile.m_StartTick) + Client()->PredIntraGameTick(g_Config.m_ClDummy)) / (float)Client()->GameTickSpeed();
	else
		CurrentTime = (Client()->PrevGameTick(g_Config.m_ClDummy) - Projectile.m_StartTick) / (float)Client()->GameTickSpeed() + s_LastGameTickTime;

	if(CurrentTime < 0.0f)
	{
		if(CurrentTime > -s_LastGameTickTime / 2.0f)
			CurrentTime = 0.0f;
		else
			return false;
	}

	Position = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, CurrentTime);
	PreviousPosition = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, CurrentTime - 0.001f);
	return true;
}

void CCollisionHitbox::OnMapLoad()
{
	m_vMapData.clear();

	// 查找有效的图层并计算尺寸
	std::vector<const CHitboxLayer *> vValidLayers;
	m_MapDataSize = {0, 0};
	for(const auto &Layer : HITBOX_LAYERS)
	{
		ivec2 LayerSize;
		Layer.GetMeta(GameClient(), LayerSize);
		if(LayerSize.x <= 0 || LayerSize.y <= 0)
			continue;
		m_MapDataSize.x = std::max(m_MapDataSize.x, LayerSize.x);
		m_MapDataSize.y = std::max(m_MapDataSize.y, LayerSize.y);
		vValidLayers.push_back(&Layer);
	}

	if(m_MapDataSize.x <= 0 || m_MapDataSize.y <= 0)
		return;

	const size_t MapWidth = (size_t)m_MapDataSize.x;
	const size_t MapHeight = (size_t)m_MapDataSize.y;
	if(MapWidth > std::numeric_limits<size_t>::max() / MapHeight)
	{
		log_warn("collision_hitbox", "Map size overflow for hitbox cache: %dx%d", m_MapDataSize.x, m_MapDataSize.y);
		m_MapDataSize = {0, 0};
		return;
	}
	const size_t NumTiles = MapWidth * MapHeight;
	if(NumTiles > MAX_HITBOX_MAP_TILES)
	{
		log_warn("collision_hitbox", "Map too large for hitbox cache: %dx%d", m_MapDataSize.x, m_MapDataSize.y);
		m_MapDataSize = {0, 0};
		return;
	}

	m_vMapData.assign(NumTiles, HITBOX_NONE);

	// 填充碰撞数据
	for(const auto *pLayer : vValidLayers)
	{
		pLayer->SetData(GameClient(), m_vMapData.data(), m_MapDataSize);
	}
}

void CCollisionHitbox::RenderTileHitboxes()
{
	if(m_vMapData.empty())
		return;

	const float Scale = 32.0f;
	const float Alpha = HitboxAlpha();
	if(Alpha <= 0.0f)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	int StartY = (int)(ScreenY0 / Scale) - 1;
	int StartX = (int)(ScreenX0 / Scale) - 1;
	int EndY = (int)(ScreenY1 / Scale) + 1;
	int EndX = (int)(ScreenX1 / Scale) + 1;

	// 限制渲染范围避免性能问题
	int MaxScale = 12;
	if(EndX - StartX > Graphics()->ScreenWidth() / MaxScale || EndY - StartY > Graphics()->ScreenHeight() / MaxScale)
	{
		int EdgeX = (EndX - StartX) - (Graphics()->ScreenWidth() / MaxScale);
		StartX += EdgeX / 2;
		EndX -= EdgeX / 2;
		int EdgeY = (EndY - StartY) - (Graphics()->ScreenHeight() / MaxScale);
		StartY += EdgeY / 2;
		EndY -= EdgeY / 2;
	}

	auto GetTile = [&](int x, int y) -> int {
		if(x < 0 || x >= m_MapDataSize.x || y < 0 || y >= m_MapDataSize.y)
			return HITBOX_NONE;
		return m_vMapData[y * m_MapDataSize.x + x];
	};

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	for(int y = StartY; y < EndY; y++)
	{
		for(int x = StartX; x < EndX; x++)
		{
			const int Type = GetTile(x, y);
			if(Type == HITBOX_NONE)
				continue;

			// 只检查freeze与death类型
			ColorRGBA RgbaColor;

			if(Type == HITBOX_FREEZE || Type == HITBOX_DFREEZE)
			{
				RgbaColor = FreezeColor(Alpha);
			}
			else if(Type == HITBOX_DEATH)
			{
				RgbaColor = ColorRGBA(0.0f, 0.0f, 0.0f, Alpha);
			}
			else
			{
				continue; // 跳过其他类型
			}

			// 设置颜色
			Graphics()->SetColor(RgbaColor);

			// 计算tile边界
			float TileX = x * Scale;
			float TileY = y * Scale;

			// 检查邻居 - 只有当邻居不是相同类型时才绘制边界
			// 这样就能绘制出最外层的线，表示碰撞发生的边界
			bool LeftNeighbor = (GetTile(x - 1, y) == Type);
			bool RightNeighbor = (GetTile(x + 1, y) == Type);
			bool TopNeighbor = (GetTile(x, y - 1) == Type);
			bool BottomNeighbor = (GetTile(x, y + 1) == Type);

			// 绘制边框线（只绘制最外层边界）
			// 当Tee的圆形碰撞体积与这些边界相交时，就会触发效果
			IGraphics::CLineItem aLines[4];
			int NumLines = 0;

			// 上边 - Tee从上方接触此边界时会被freeze
			if(!TopNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY, TileX + Scale, TileY);
			// 下边 - Tee从下方接触此边界时会被freeze
			if(!BottomNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY + Scale, TileX + Scale, TileY + Scale);
			// 左边 - Tee从左侧接触此边界时会被freeze
			if(!LeftNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX, TileY, TileX, TileY + Scale);
			// 右边 - Tee从右侧接触此边界时会被freeze
			if(!RightNeighbor)
				aLines[NumLines++] = IGraphics::CLineItem(TileX + Scale, TileY, TileX + Scale, TileY + Scale);

			if(NumLines > 0)
				Graphics()->LinesDraw(aLines, NumLines);
		}
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::RenderTeeHitboxes()
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float Alpha = HitboxAlpha();
	if(Alpha <= 0.0f)
		return;

	const bool Legacy = LegacyModeEnabled();
	const bool ShowTeeCollision = Legacy || g_Config.m_QmHitboxShowTeeCollision;
	const bool ShowTeeFreeze = Legacy || g_Config.m_QmHitboxShowTeeFreeze;
	const bool ShowTeeDeath = Legacy || g_Config.m_QmHitboxShowTeeDeath;
	if(!ShowTeeCollision && !ShowTeeFreeze && !ShowTeeDeath)
		return;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	const float PointSize = 3.0f;
	const float SampleOffset = CCharacterCore::PhysicalSize() / 3.0f;

	auto IsFreezeTile = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};

	for(const auto &Player : GameClient()->m_aClients)
	{
		const int ClientId = Player.ClientId();
		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active || !Player.m_Active)
			continue;
		if(Player.m_Team < 0)
			continue;
		if(!ShouldRenderClient(ClientId))
			continue;

		// 检查是否在屏幕范围内
		if(!(Player.m_RenderPos.x >= ScreenX0 && Player.m_RenderPos.x <= ScreenX1 &&
			   Player.m_RenderPos.y >= ScreenY0 && Player.m_RenderPos.y <= ScreenY1))
			continue;

		float PlayerAlpha = Alpha;
		if(GameClient()->IsOtherTeam(ClientId))
			PlayerAlpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;

		if(PlayerAlpha <= 0.0f)
			continue;

		vec2 Position = Player.m_RenderPos;
		if(ShowTeeCollision && HitboxModeEnabled())
			DrawCircleOutline(Position, CCharacterCore::PhysicalSize(), TeeColor(ClientId, PlayerAlpha), 36);

		if(ShowTeeFreeze)
		{
			// Freeze: center sample (tile-based)
			const int Index = Collision()->GetPureMapIndex(Position);
			const int Tile = Collision()->GetTileIndex(Index);
			const int FrontTile = Collision()->GetFrontTileIndex(Index);
			const int SwitchTile = Collision()->GetSwitchType(Index);
			const bool FreezeHit = IsFreezeTile(Tile) || IsFreezeTile(FrontTile) || IsFreezeTile(SwitchTile);
			const float FreezeAlpha = FreezeHit ? PlayerAlpha : PlayerAlpha * 0.35f;
			DrawCross(Position, PointSize, FreezeColor(FreezeAlpha));
		}

		if(ShowTeeDeath)
		{
			// Death: 4-point samples (collision-based)
			const vec2 DeathOffsets[4] = {
				{SampleOffset, SampleOffset},
				{SampleOffset, -SampleOffset},
				{-SampleOffset, SampleOffset},
				{-SampleOffset, -SampleOffset}};
			for(const vec2 &Offset : DeathOffsets)
			{
				const vec2 SamplePos = Position + Offset;
				const bool DeathHit = Collision()->GetCollisionAt(SamplePos.x, SamplePos.y) == TILE_DEATH ||
						      Collision()->GetFrontCollisionAt(SamplePos.x, SamplePos.y) == TILE_DEATH;
				const float DeathAlpha = DeathHit ? PlayerAlpha : PlayerAlpha * 0.35f;
				DrawCross(SamplePos, PointSize, ColorRGBA(0.0f, 0.0f, 0.0f, DeathAlpha));
			}
		}
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::RenderPickupHitboxes()
{
	const float Alpha = HitboxAlpha();
	if(Alpha <= 0.0f)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float PickupRadius = 14.0f + 6.0f; // pickup phys size + extra collision size
	const ColorRGBA ShieldColor(1.0f, 1.0f, 0.0f, Alpha);

	const bool IsSuper = GameClient()->IsLocalCharSuper();
	const int SwitcherTeam = GameClient()->SwitchStateTeam();
	auto &aSwitchers = GameClient()->Switchers();
	const int Ticks = Client()->GameTick(g_Config.m_ClDummy) % Client()->GameTickSpeed();
	const bool BlinkingPickup = (Ticks % 22) < 4;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	for(const CSnapEntities &Ent : GameClient()->SnapEntities())
	{
		const IClient::CSnapItem Item = Ent.m_Item;
		if(Item.m_Type != NETOBJTYPE_PICKUP && Item.m_Type != NETOBJTYPE_DDNETPICKUP)
			continue;

		const CPickupData Data = ExtractPickupInfo(Item.m_Type, Item.m_pData, Ent.m_pDataEx);
		if(Data.m_Type != POWERUP_ARMOR)
			continue;

		const bool Inactive = !IsSuper && Data.m_SwitchNumber > 0 && Data.m_SwitchNumber < (int)aSwitchers.size() &&
				      !aSwitchers[Data.m_SwitchNumber].m_aStatus[SwitcherTeam];
		if(Inactive && BlinkingPickup)
			continue;

		if(Data.m_Pos.x + PickupRadius < ScreenX0 || Data.m_Pos.x - PickupRadius > ScreenX1 ||
			Data.m_Pos.y + PickupRadius < ScreenY0 || Data.m_Pos.y - PickupRadius > ScreenY1)
			continue;

		DrawBoxOutline(Data.m_Pos, PickupRadius, ShieldColor);
	}

	Graphics()->LinesEnd();
}

void CCollisionHitbox::RenderHammerHitboxes()
{
	const float Alpha = HitboxAlpha();
	for(const auto &Player : GameClient()->m_aClients)
	{
		const int ClientId = Player.ClientId();
		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active || !Player.m_Active || Player.m_Team < 0)
			continue;
		if(!ShouldRenderClient(ClientId))
			continue;

		CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
		if(!pChar || !IsOnScreen(Player.m_RenderPos, 128.0f))
			continue;

		const vec2 RenderDelta = Player.m_RenderPos - pChar->GetPos();
		vec2 HitPosition;
		float HitRadius = 0.0f;
		if(!GameClient()->GetPotentialHammerHitArea(pChar, HitPosition, HitRadius))
			continue;
		const vec2 RenderHitPosition = HitPosition + RenderDelta;

		float PlayerAlpha = Alpha;
		if(GameClient()->IsOtherTeam(ClientId))
			PlayerAlpha *= g_Config.m_ClShowOthersAlpha / 100.0f;
		if(PlayerAlpha <= 0.0f)
			continue;

		const ColorRGBA HammerColor = WeaponColor(PlayerAlpha);
		const IGraphics::CLineItem AimLine(Player.m_RenderPos, RenderHitPosition);
		Graphics()->SetColor(HammerColor.WithMultipliedAlpha(0.6f));
		Graphics()->LinesDraw(&AimLine, 1);
		DrawCircleOutline(RenderHitPosition, HitRadius, HammerColor, 28);
		DrawCross(RenderHitPosition, 4.0f, HammerColor);

		int aTargetIds[MAX_CLIENTS];
		const int NumTargets = GameClient()->FindPotentialHammerHitTargets(pChar, HitPosition, HitRadius, aTargetIds, MAX_CLIENTS);
		for(int TargetIndex = 0; TargetIndex < NumTargets; ++TargetIndex)
		{
			const int TargetId = aTargetIds[TargetIndex];
			CCharacter *pTarget = GameClient()->m_PredictedWorld.GetCharacterById(TargetId);
			if(!pTarget || !IsOnScreen(GameClient()->m_aClients[TargetId].m_RenderPos, CCharacterCore::PhysicalSize()))
				continue;
			DrawCircleOutline(GameClient()->m_aClients[TargetId].m_RenderPos, CCharacterCore::PhysicalSize(), ColorRGBA(1.0f, 0.25f, 0.25f, PlayerAlpha), 36);
		}
	}
}

void CCollisionHitbox::RenderProjectileHitboxes()
{
	const float Alpha = HitboxAlpha();
	static constexpr float ExplosionRadius = 135.0f;
	static constexpr float ExplosionInnerRadius = 48.0f;

	for(const CSnapEntities &Ent : GameClient()->SnapEntities())
	{
		const IClient::CSnapItem Item = Ent.m_Item;
		if(Item.m_Type != NETOBJTYPE_PROJECTILE && Item.m_Type != NETOBJTYPE_DDRACEPROJECTILE && Item.m_Type != NETOBJTYPE_DDNETPROJECTILE)
			continue;

		const CProjectileData Data = ExtractProjectileInfo(Item.m_Type, Item.m_pData, &GameClient()->m_GameWorld, Ent.m_pDataEx);
		if(Data.m_ExtraInfo && Data.m_Owner >= 0 && !ShouldRenderClient(Data.m_Owner))
			continue;

		vec2 Position;
		vec2 PreviousPosition;
		if(!GetProjectileRenderPosition(Data, Position, PreviousPosition))
			continue;
		if(!IsLineOnScreen(PreviousPosition, Position, ExplosionRadius))
			continue;

		float ProjectileAlpha = Alpha;
		if(Data.m_ExtraInfo && Data.m_Owner >= 0)
		{
			if(GameClient()->IsOtherTeam(Data.m_Owner))
				ProjectileAlpha *= g_Config.m_ClShowOthersAlpha / 100.0f;
		}
		if(ProjectileAlpha <= 0.0f)
			continue;

		const ColorRGBA ProjectileColor = WeaponColor(ProjectileAlpha * 0.85f);
		const IGraphics::CLineItem TrailLine(PreviousPosition, Position);
		Graphics()->SetColor(ProjectileColor);
		Graphics()->LinesDraw(&TrailLine, 1);
		DrawCross(Position, 4.0f, ProjectileColor);

		if(Data.m_Type == WEAPON_GRENADE || Data.m_Explosive)
		{
			DrawCircleOutline(Position, ExplosionRadius, WeaponColor(ProjectileAlpha * 0.65f), 48);
			DrawCircleOutline(Position, ExplosionInnerRadius, WeaponColor(ProjectileAlpha), 36);
		}
	}
}

void CCollisionHitbox::RenderLaserHitboxes()
{
	const float Alpha = HitboxAlpha();
	if(Alpha <= 0.0f)
		return;

	const auto &aSwitchers = GameClient()->Switchers();
	const int SwitcherTeam = std::clamp(GameClient()->SwitchStateTeam(), 0, NUM_DDRACE_TEAMS - 1);
	const bool IsSuper = GameClient()->IsLocalCharSuper();
	const auto IsFreezeLaser = [](const CLaserData &Data) {
		return Data.m_Type == LASERTYPE_FREEZE ||
		       ((Data.m_Type == LASERTYPE_GUN || Data.m_Type == LASERTYPE_PLASMA) &&
			       (Data.m_Subtype == LASERGUNTYPE_FREEZE || Data.m_Subtype == LASERGUNTYPE_EXPFREEZE));
	};

	for(const CSnapEntities &Ent : GameClient()->SnapEntities())
	{
		const IClient::CSnapItem Item = Ent.m_Item;
		if(Item.m_Type != NETOBJTYPE_LASER && Item.m_Type != NETOBJTYPE_DDNETLASER)
			continue;

		const CLaserData Data = ExtractLaserInfo(Item.m_Type, Item.m_pData, &GameClient()->m_GameWorld, Ent.m_pDataEx);
		if(Data.m_ExtraInfo && Data.m_Owner >= 0 && !ShouldRenderClient(Data.m_Owner))
			continue;

		const bool FreezeLaser = IsFreezeLaser(Data);
		if((FreezeLaser && !g_Config.m_QmHitboxShowFreezeLasers) || (!FreezeLaser && !g_Config.m_QmHitboxShowLasers))
			continue;
		if(Data.m_SwitchNumber > 0 && Data.m_SwitchNumber < (int)aSwitchers.size() && !IsSuper && !aSwitchers[Data.m_SwitchNumber].m_aStatus[SwitcherTeam])
			continue;

		const float ScreenMargin = FreezeLaser ? CCharacterCore::PhysicalSize() : 16.0f;
		if(!IsLineOnScreen(Data.m_From, Data.m_To, ScreenMargin))
			continue;

		float LaserAlpha = Alpha;
		if(Data.m_ExtraInfo && Data.m_Owner >= 0)
		{
			if(GameClient()->IsOtherTeam(Data.m_Owner))
				LaserAlpha *= g_Config.m_ClShowOthersAlpha / 100.0f;
		}
		if(LaserAlpha <= 0.0f)
			continue;

		const ColorRGBA LaserColor = FreezeLaser ? FreezeColor(LaserAlpha) : WeaponColor(LaserAlpha);
		const IGraphics::CLineItem LaserLine(Data.m_From, Data.m_To);
		Graphics()->SetColor(LaserColor);
		Graphics()->LinesDraw(&LaserLine, 1);
		if(FreezeLaser)
		{
			// 冻结光束实际按角色半径命中，显示为沿光束扫掠的胶囊体积。
			DrawCapsuleOutline(Data.m_From, Data.m_To, CCharacterCore::PhysicalSize(), LaserColor.WithMultipliedAlpha(0.85f), 16);
			DrawCircleOutline(Data.m_From, 5.0f, LaserColor.WithMultipliedAlpha(0.7f), 16);
			DrawCircleOutline(Data.m_To, 7.0f, LaserColor, 20);
		}
		else
		{
			DrawCircleOutline(Data.m_From, 5.0f, LaserColor.WithMultipliedAlpha(0.7f), 16);
			DrawCircleOutline(Data.m_To, 7.0f, LaserColor, 20);
		}
	}
}

void CCollisionHitbox::RenderHookHitboxes()
{
	const float Alpha = HitboxAlpha();
	for(const auto &Player : GameClient()->m_aClients)
	{
		const int ClientId = Player.ClientId();
		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active || !Player.m_Active || Player.m_Team < 0)
			continue;
		if(!ShouldRenderClient(ClientId))
			continue;

		CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
		if(!pChar)
			continue;

		const CCharacterCore *pCore = pChar->Core();
		if(!pCore || pCore->m_HookState <= HOOK_IDLE)
			continue;

		const vec2 RenderDelta = Player.m_RenderPos - pChar->GetPos();
		const vec2 StartPosition = Player.m_RenderPos;
		const vec2 HookPosition = pCore->m_HookPos + RenderDelta;
		if(!IsLineOnScreen(StartPosition, HookPosition, CCharacterCore::PhysicalSize()))
			continue;

		float HookAlpha = Alpha;
		if(GameClient()->IsOtherTeam(ClientId))
			HookAlpha *= g_Config.m_ClShowOthersAlpha / 100.0f;
		if(HookAlpha <= 0.0f)
			continue;

		const ColorRGBA HookColor = WeaponColor(HookAlpha * 0.8f);
		const IGraphics::CLineItem HookLine(StartPosition, HookPosition);
		Graphics()->SetColor(HookColor);
		Graphics()->LinesDraw(&HookLine, 1);
		DrawCircleOutline(HookPosition, 6.0f, HookColor, 16);

		const int HookedClientId = pCore->HookedPlayer();
		if(HookedClientId >= 0 && HookedClientId < MAX_CLIENTS)
		{
			CCharacter *pHookedChar = GameClient()->m_PredictedWorld.GetCharacterById(HookedClientId);
			if(pHookedChar && IsOnScreen(GameClient()->m_aClients[HookedClientId].m_RenderPos, CCharacterCore::PhysicalSize()))
				DrawCircleOutline(GameClient()->m_aClients[HookedClientId].m_RenderPos, CCharacterCore::PhysicalSize(), ColorRGBA(1.0f, 0.55f, 0.1f, HookAlpha), 36);
		}
	}
}

void CCollisionHitbox::RenderWeaponHitboxes()
{
	const float Alpha = HitboxAlpha();
	if(Alpha <= 0.0f || (!g_Config.m_QmHitboxShowHammer && !g_Config.m_QmHitboxShowProjectiles && !g_Config.m_QmHitboxShowLasers && !g_Config.m_QmHitboxShowFreezeLasers && !g_Config.m_QmHitboxShowHook))
		return;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	if(g_Config.m_QmHitboxShowHammer)
		RenderHammerHitboxes();
	if(g_Config.m_QmHitboxShowProjectiles)
		RenderProjectileHitboxes();
	if(g_Config.m_QmHitboxShowLasers || g_Config.m_QmHitboxShowFreezeLasers)
		RenderLaserHitboxes();
	if(g_Config.m_QmHitboxShowHook)
		RenderHookHitboxes();
	Graphics()->LinesEnd();
}

void CCollisionHitbox::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

#if defined(CONF_VIDEORECORDER)
	// 渲染（录制）视频时跟随 cl_video_showhud，避免碰撞箱被一起录进视频
	if(IVideo::Current() && !g_Config.m_ClVideoShowhud)
		return;
#endif

	const bool HitboxMode = HitboxModeEnabled();
	const bool LegacyMode = LegacyModeEnabled();
	if(!HitboxMode && !LegacyMode)
		return;

	float SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1;
	Graphics()->GetScreen(&SavedScreenX0, &SavedScreenY0, &SavedScreenX1, &SavedScreenY1);
	float aPoints[4];
	Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x,
		GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f,
		0.0f, 0.0f,
		Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	if(LegacyMode || g_Config.m_QmHitboxShowMap)
	{
		// 绘制地图tile的碰撞体积
		RenderTileHitboxes();
	}

	if(LegacyMode || g_Config.m_QmHitboxShowTeeCollision || g_Config.m_QmHitboxShowTeeFreeze || g_Config.m_QmHitboxShowTeeDeath)
	{
		// 按 Tee↔Tee、Tee↔Freeze 和 Tee↔Death 语义分别绘制。
		RenderTeeHitboxes();
	}

	if(LegacyMode || g_Config.m_QmHitboxShowPickups)
	{
		// 绘制盾牌拾取的碰撞体积
		RenderPickupHitboxes();
	}

	if(HitboxMode && (g_Config.m_QmHitboxShowHammer || g_Config.m_QmHitboxShowProjectiles || g_Config.m_QmHitboxShowLasers || g_Config.m_QmHitboxShowFreezeLasers || g_Config.m_QmHitboxShowHook))
	{
		// 各类武器交互范围由独立开关控制。
		RenderWeaponHitboxes();
	}

	Graphics()->MapScreen(SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1);
}
