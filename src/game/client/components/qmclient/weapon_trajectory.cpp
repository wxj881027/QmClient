// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "weapon_trajectory.h"

#include <base/color.h>

#include <engine/client/enums.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/components/controls.h>
#include <game/client/components/players.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <vector>

bool CQmWeaponTrajectory::IsVisible() const
{
	if(ShouldHideFocusGuideLines(g_Config.m_QmFocusMode != 0, g_Config.m_QmFocusModeHideGuideLines != 0))
		return false;

	const int TrajectoryMode = std::clamp(g_Config.m_QmWeaponTrajectory, 0, 2);
	const bool ManualTrajectoryVisible = GameClient()->m_Controls.m_aShowWeaponTrajectory[g_Config.m_ClDummy] != 0;
	const bool TrajectoryVisible = TrajectoryMode == 2 || (TrajectoryMode == 1 && ManualTrajectoryVisible);
	return TrajectoryVisible && !GameClient()->m_TClient.ShouldHideGoresGuides(TrajectoryVisible);
}

bool CQmWeaponTrajectory::PredictNinjaEndPosition(vec2 Position, vec2 Direction, vec2 &OutPosition) const
{
	if(length(Direction) < 0.0001f)
		return false;

	int TuneZone = 0;
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_GameWorld.m_WorldConfig.m_UseTuneZones)
		TuneZone = Collision()->IsTune(Collision()->GetMapIndex(Position));
	const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);

	OutPosition = Position;
	Direction = normalize(Direction);
	const int NinjaMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Client()->GameTickSpeed() / 1000;
	const int MovementTicks = std::max(NinjaMoveTime - 1, 0);
	for(int i = 0; i < MovementTicks; ++i)
	{
		vec2 Velocity = Direction * g_pData->m_Weapons.m_Ninja.m_Velocity;
		Collision()->MoveBox(&OutPosition, &Velocity, CCharacterCore::PhysicalSizeVec2(), vec2(pTuning->m_GroundElasticityX, pTuning->m_GroundElasticityY));
		const int MapIndex = Collision()->GetMapIndex(OutPosition);
		const int Teleport = Collision()->IsTeleport(MapIndex);
		if(Teleport > 0 && !Collision()->TeleOuts(Teleport - 1).empty())
			OutPosition = Collision()->TeleOuts(Teleport - 1)[0];
	}
	return true;
}

void CQmWeaponTrajectory::Render(
	const CNetObj_Character *pPrevChar,
	const CNetObj_Character *pPlayerChar,
	int ClientId)
{
	if(ClientId < 0 || !IsVisible())
		return;

	const int Weapon = pPlayerChar->m_Weapon;
	if(!QmWeaponTrajectoryEnabledForWeapon(Weapon, g_Config.m_QmWeaponTrajectoryGun != 0))
		return;

	const float TrajectoryAlpha = std::clamp(g_Config.m_QmWeaponTrajectoryAlpha / 100.0f, 0.0f, 1.0f);
	if(TrajectoryAlpha <= 0.0f)
		return;
	m_vPoints.clear();
	m_vLineSegments.clear();
	m_vLineQuadSegments.clear();
	const float TrajectoryHalfWidth = 0.5f + (float)(std::clamp(g_Config.m_QmWeaponTrajectoryWidth, 1, 10) - 1) * 0.3f;
	ColorRGBA TrajectoryColor = QmWeaponTrajectoryBaseColor(
		Weapon,
		color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmWeaponTrajectoryColor)),
		color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClLaserRifleInnerColor)),
		color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClLaserShotgunInnerColor)));
	TrajectoryColor.a = TrajectoryAlpha;

	float Intra = GameClient()->m_aClients[ClientId].m_IsPredicted ? Client()->PredIntraGameTick(g_Config.m_ClDummy) : Client()->IntraGameTick(g_Config.m_ClDummy);
	const float Angle = GameClient()->m_Players.GetPlayerTargetAngle(pPrevChar, pPlayerChar, ClientId, Intra);
	const vec2 Direction = direction(Angle);
	if(length(Direction) < 0.0001f)
		return;

	const vec2 Position = GameClient()->m_aClients[ClientId].m_RenderPos;
	int TuneZone = 0;
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_GameWorld.m_WorldConfig.m_UseTuneZones)
		TuneZone = Collision()->IsTune(Collision()->GetMapIndex(Position));
	const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
	const CGameClient::CClientData &ShooterData = GameClient()->m_aClients[ClientId];
	const bool CanHitOtherPlayers = QmWeaponTrajectoryCanHitOtherPlayers(
		Weapon,
		ShooterData.m_GrenadeHitDisabled,
		ShooterData.m_LaserHitDisabled,
		ShooterData.m_ShotgunHitDisabled);

	auto CanTeleportToWall = [&](vec2 Pos, vec2 PrevPos) -> bool {
		for(int k = 0; k < 16 && Collision()->CheckPoint(Pos); ++k)
			Pos -= normalize(PrevPos - Pos);

		const vec2 PosInBlock(round_to_int(Pos.x) % 32, round_to_int(Pos.y) % 32);
		const vec2 BlockCenter = vec2(round_to_int(Pos.x), round_to_int(Pos.y)) - PosInBlock + vec2(16.0f, 16.0f);
		const vec2 CandidateX(BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f), Pos.y);
		if(!Collision()->TestBox(CandidateX, CCharacterCore::PhysicalSizeVec2()))
			return true;

		const vec2 CandidateY(Pos.x, BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
		if(!Collision()->TestBox(CandidateY, CCharacterCore::PhysicalSizeVec2()))
			return true;

		const vec2 CandidateCorner(
			BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f),
			BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
		return !Collision()->TestBox(CandidateCorner, CCharacterCore::PhysicalSizeVec2());
	};

	auto IsValidTeleGunWallHit = [&](vec2 ImpactPos, vec2 AirPos, vec2 PrevPos) -> bool {
		const bool HasTeleGun = QmWeaponTrajectoryHasTeleGun(
			Weapon,
			ShooterData.m_HasTelegunGun,
			ShooterData.m_HasTelegunGrenade,
			ShooterData.m_HasTelegunLaser);
		if(!HasTeleGun)
			return false;

		const int MapIndex = Collision()->GetPureMapIndex(ImpactPos);
		if(!QmWeaponTrajectoryIsTeleGunWall(
			   Weapon,
			   Collision()->GetFrontTileIndex(MapIndex),
			   Collision()->GetSwitchType(MapIndex),
			   Collision()->GetSwitchDelay(MapIndex)))
			return false;

		return CanTeleportToWall(AirPos, PrevPos);
	};

	auto FindBlockingTee = [&](const vec2 &From, const vec2 &To, bool IgnoreShooter, vec2 &OutPos) -> bool {
		float ClosestDistance = distance(From, To) + 1.0f;
		bool Found = false;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[i];
			if(!ClientData.m_Active || ClientData.m_Team == TEAM_SPECTATORS)
				continue;
			if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
				continue;
			if(i == ClientId && IgnoreShooter)
				continue;
			if(i != ClientId && !CanHitOtherPlayers)
				continue;
			const bool IsOneSuper = ClientData.m_Super || ShooterData.m_Super;
			const bool IsOneSolo = ClientData.m_Solo || ShooterData.m_Solo;
			if(!IsOneSuper && (!GameClient()->m_Teams.SameTeam(i, ClientId) || IsOneSolo))
				continue;

			vec2 ClosestPoint;
			const vec2 TeePos = ClientData.m_RenderPos;
			if(closest_point_on_line(From, To, TeePos, ClosestPoint))
			{
				if(distance(TeePos, ClosestPoint) < CCharacterCore::PhysicalSize())
				{
					const float Dist = distance(From, ClosestPoint);
					if(Dist < ClosestDistance)
					{
						ClosestDistance = Dist;
						OutPos = ClosestPoint;
						Found = true;
					}
				}
			}
		}
		return Found;
	};

	bool HasValidHit = false;

	if(Weapon == WEAPON_GUN || Weapon == WEAPON_GRENADE)
	{
		const vec2 StartPos = Position + Direction * (CCharacterCore::PhysicalSize() * 0.75f);
		const float Curvature = Weapon == WEAPON_GUN ? pTuning->m_GunCurvature : pTuning->m_GrenadeCurvature;
		const float Speed = Weapon == WEAPON_GUN ? pTuning->m_GunSpeed : pTuning->m_GrenadeSpeed;
		const float Lifetime = Weapon == WEAPON_GUN ? pTuning->m_GunLifetime : pTuning->m_GrenadeLifetime * 10.0f;

		constexpr int PointCount = 180;
		m_vPoints.reserve(PointCount);
		vec2 LandingPos = StartPos;

		vec2 PrevPos = StartPos;
		for(int i = 0; i < PointCount; ++i)
		{
			const float U = PointCount > 1 ? (float)i / (float)(PointCount - 1) : 0.0f;
			const float T = std::pow(U, 2.0f);
			vec2 Pos = CalcPos(StartPos, Direction, Curvature, Speed, Lifetime * T);
			if(i > 0)
			{
				vec2 ColPos, BeforePos;
				const bool CollidesWithWorld = Collision()->IntersectLine(PrevPos, Pos, &ColPos, &BeforePos);
				const vec2 SegmentEnd = CollidesWithWorld ? ColPos : Pos;
				vec2 TeeHitPos;
				if(FindBlockingTee(PrevPos, SegmentEnd, true, TeeHitPos))
				{
					m_vPoints.push_back(TeeHitPos);
					LandingPos = TeeHitPos;
					HasValidHit = true;
					break;
				}
				if(CollidesWithWorld)
				{
					m_vPoints.push_back(ColPos);
					LandingPos = ColPos;
					HasValidHit = IsValidTeleGunWallHit(ColPos, BeforePos, Pos);
					break;
				}
			}
			m_vPoints.push_back(Pos);
			LandingPos = Pos;
			PrevPos = Pos;
		}

		if(m_vPoints.empty())
			return;

		if(!QmWeaponTrajectoryUsesLineStyle(Weapon))
		{
			const ColorRGBA BaseColor = HasValidHit ? QmInvertWeaponTrajectoryColor(TrajectoryColor) : TrajectoryColor;
			const float StartSize = 2.5f + TrajectoryHalfWidth * 1.5f;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(size_t i = 0; i < m_vPoints.size(); ++i)
			{
				const float T = m_vPoints.size() > 1 ? (float)i / (float)(m_vPoints.size() - 1) : 0.0f;
				const float Fade = 1.0f - T;
				if(Fade <= 0.0f)
					continue;
				float Size = StartSize * Fade;
				if(Size < TrajectoryHalfWidth)
					Size = TrajectoryHalfWidth;

				ColorRGBA Color = BaseColor;
				Color.a = TrajectoryAlpha * Fade;
				Graphics()->SetColor(Color);
				Graphics()->DrawCircle(m_vPoints[i].x, m_vPoints[i].y, Size, 12);
			}
			Graphics()->QuadsEnd();

			const IGraphics::CTextureHandle &GrenadeCursor = GameClient()->m_GameSkin.m_SpriteWeaponGrenadeCursor;
			if(GrenadeCursor.IsValid())
			{
				float CursorSpriteScaleX, CursorSpriteScaleY;
				Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[WEAPON_GRENADE].m_pSpriteCursor, CursorSpriteScaleX, CursorSpriteScaleY);

				float CursorScale = (float)g_Config.m_TcCursorScale / 100.0f;
				CursorScale = std::clamp(CursorScale, 0.3f, 3.0f);
				const float CursorSize = 64.0f * CursorScale * 0.8f;
				IGraphics::CQuadItem CursorQuad(
					LandingPos.x,
					LandingPos.y,
					CursorSize * CursorSpriteScaleX,
					CursorSize * CursorSpriteScaleY);

				Graphics()->TextureSet(GrenadeCursor);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.9f);
				Graphics()->QuadsDraw(&CursorQuad, 1);
				Graphics()->QuadsEnd();
			}
			return;
		}

		m_vLineSegments.reserve(m_vPoints.size() - 1);
		for(size_t i = 1; i < m_vPoints.size(); ++i)
		{
			if(distance(m_vPoints[i - 1], m_vPoints[i]) >= 0.0001f)
				m_vLineSegments.emplace_back(m_vPoints[i - 1], m_vPoints[i]);
		}
	}
	else
	{
		float Energy = pTuning->m_LaserReach;
		if(GameClient()->m_GameWorld.m_WorldConfig.m_IsFNG && Energy < 10.0f)
			Energy = 800.0f;

		m_vLineSegments.reserve(pTuning->m_LaserBounceNum + 2);

		vec2 From = Position;
		vec2 Dir = Direction;
		bool ZeroEnergyBounceInLastTick = false;
		int Bounces = 0;

		while(Energy > 0.0f)
		{
			vec2 To = From + Dir * Energy;
			vec2 ColTile;
			vec2 HitPos;
			int Res = Collision()->IntersectLineTeleWeapon(From, To, &ColTile, &HitPos);
			vec2 SegmentEnd = Res ? HitPos : To;
			vec2 TeeHitPos;
			const bool IgnoreShooter =
				Bounces == 0 || g_Config.m_SvOldLaser || !GameClient()->m_GameWorld.m_WorldConfig.m_IsDDRace;
			if(FindBlockingTee(From, SegmentEnd, IgnoreShooter, TeeHitPos))
			{
				m_vLineSegments.emplace_back(From, TeeHitPos);
				HasValidHit = true;
				break;
			}
			if(!Res)
			{
				m_vLineSegments.emplace_back(From, To);
				break;
			}

			m_vLineSegments.emplace_back(From, SegmentEnd);

			vec2 TempPos = SegmentEnd;
			vec2 TempDir = Dir * 4.0f;
			int SavedTile = 0;
			if(Res == -1)
			{
				SavedTile = Collision()->GetTile(round_to_int(ColTile.x), round_to_int(ColTile.y));
				Collision()->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), TILE_SOLID);
			}
			Collision()->MovePoint(&TempPos, &TempDir, 1.0f, nullptr);
			if(Res == -1)
			{
				Collision()->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), SavedTile);
			}
			const bool ValidTeleGunWallHit = IsValidTeleGunWallHit(ColTile, TempPos, From);

			const float Distance = distance(From, TempPos);
			if(Distance == 0.0f && ZeroEnergyBounceInLastTick)
			{
				HasValidHit = ValidTeleGunWallHit;
				break;
			}

			Energy -= Distance + pTuning->m_LaserBounceCost;
			ZeroEnergyBounceInLastTick = Distance == 0.0f;
			if(Energy <= 0.0f)
			{
				HasValidHit = ValidTeleGunWallHit;
				break;
			}

			Bounces++;
			if(Bounces > pTuning->m_LaserBounceNum)
			{
				HasValidHit = ValidTeleGunWallHit;
				break;
			}

			if(length(TempDir) < 0.0001f)
			{
				HasValidHit = ValidTeleGunWallHit;
				break;
			}

			Dir = normalize(TempDir);
			From = TempPos;
		}
	}

	if(HasValidHit)
		TrajectoryColor = QmInvertWeaponTrajectoryColor(TrajectoryColor);

	if(m_vLineSegments.empty())
		return;

	Graphics()->TextureClear();
	if(g_Config.m_QmWeaponTrajectoryWidth > 1)
	{
		m_vLineQuadSegments.reserve(m_vLineSegments.size());
		for(const IGraphics::CLineItem &LineSegment : m_vLineSegments)
		{
			const vec2 FromPos(LineSegment.m_X0, LineSegment.m_Y0);
			const vec2 ToPos(LineSegment.m_X1, LineSegment.m_Y1);
			const vec2 Delta = ToPos - FromPos;
			if(length(Delta) < 0.0001f)
				continue;
			const vec2 Perp = normalize(vec2(-Delta.y, Delta.x));
			m_vLineQuadSegments.emplace_back(
				ToPos + Perp * -TrajectoryHalfWidth,
				ToPos + Perp * TrajectoryHalfWidth,
				FromPos + Perp * -TrajectoryHalfWidth,
				FromPos + Perp * TrajectoryHalfWidth);
		}

		if(m_vLineQuadSegments.empty())
			return;

		Graphics()->QuadsBegin();
		Graphics()->SetColor(TrajectoryColor);
		Graphics()->QuadsDrawFreeform(m_vLineQuadSegments.data(), m_vLineQuadSegments.size());
		Graphics()->QuadsEnd();
	}
	else
	{
		Graphics()->LinesBegin();
		Graphics()->SetColor(TrajectoryColor);
		Graphics()->LinesDraw(m_vLineSegments.data(), m_vLineSegments.size());
		Graphics()->LinesEnd();
	}
}
