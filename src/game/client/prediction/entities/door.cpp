/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "door.h"

#include "character.h"

#include <game/client/components/qmclient/collision_position.h>
#include <game/client/laser_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <cmath>

CDoor::CDoor(CGameWorld *pGameWorld, int Id, const CLaserData *pData) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_DOOR),
	m_Direction(0.0f, 0.0f),
	m_Length(0),
	m_Active(false)
{
	m_Id = Id;

	m_Number = pData->m_SwitchNumber;
	m_Layer = m_Number > 0 ? LAYER_SWITCH : LAYER_GAME;

	Read(pData);
}

CDoor::CDoor(const CDoor &Other) :
	CEntity(Other),
	m_pCollisionNext(nullptr),
	m_To(Other.m_To),
	m_Direction(Other.m_Direction),
	m_Length(Other.m_Length),
	m_Active(Other.m_Active)
{
}

// OnEntity 按这个顺序检查八个相邻 tile 来确定激光门长度。
static const int s_aSideOffsetX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int s_aSideOffsetY[8] = {1, 1, 0, -1, -1, -1, 0, 1};

int CDoor::MapSide(vec2 Direction)
{
	const int OffsetX = round_to_int(Direction.x);
	const int OffsetY = round_to_int(Direction.y);
	for(int Side = 0; Side < 8; Side++)
	{
		if(s_aSideOffsetX[Side] == OffsetX && s_aSideOffsetY[Side] == OffsetY)
			return Side;
	}
	return -1;
}

int CDoor::MapLength(int Side)
{
	const int Nx = round_to_int(m_Pos.x) / 32;
	const int Ny = round_to_int(m_Pos.y) / 32;
	int Length = -1;
	for(const int Layer : {LAYER_GAME, LAYER_FRONT, LAYER_SWITCH})
	{
		if(Layer == LAYER_FRONT && Collision()->FrontLayer() == nullptr)
			continue;
		if(Layer == LAYER_SWITCH && Collision()->SwitchLayer() == nullptr)
			continue;
		if(Collision()->Entity(Nx, Ny, Layer) != ENTITY_DOOR)
			continue;
		const int Index = Collision()->Entity(Nx + s_aSideOffsetX[Side], Ny + s_aSideOffsetY[Side], Layer);
		if(Index < ENTITY_LASER_SHORT || Index > ENTITY_LASER_LONG)
			continue;
		const int Candidate = 32 * 3 + 32 * (Index - ENTITY_LASER_SHORT) * 3;
		if(Length != -1 && Length != Candidate)
			return -1;
		Length = Candidate;
	}
	return Length;
}

std::optional<CDoor::SCollisionGeometry> CDoor::CalcCollisionGeometry(vec2 Pos, vec2 To, int Side, int ConfiguredLength, ivec2 MapSize)
{
	if(MapSize.x <= 0 || MapSize.y <= 0 || !QmCollision::CanRoundPosition(Pos) || !QmCollision::CanRoundPosition(To))
		return std::nullopt;
	const vec2 Dir = To - Pos;
	const float DirLength = length(Dir);
	if(!(DirLength > 0.0f))
		return std::nullopt;
	SCollisionGeometry Geometry{normalize_pre_length(Dir, DirLength), 0};
	if(ConfiguredLength != -1)
	{
		if(Side < 0 || Side >= 8 || ConfiguredLength <= 0)
			return std::nullopt;
		Geometry.m_Length = ConfiguredLength;
		Geometry.m_Direction = vec2(std::sin(pi / 4 * Side), std::cos(pi / 4 * Side));
	}
	else
	{
		const float MaxLength = (static_cast<int64_t>(MapSize.x) + MapSize.y) * 32.0f;
		if(DirLength > MaxLength || !QmCollision::CanRoundPosition(vec2(DirLength, 0.0f)))
			return std::nullopt;
		Geometry.m_Length = round_to_int(DirLength);
	}
	// 写入和清理都只采样 i < length - 1；检查同一浮点表达式的最后实际采样点。
	const vec2 Last = Pos + Geometry.m_Direction * maximum(0, Geometry.m_Length - 2);
	if(!QmCollision::CanRoundPosition(Last))
		return std::nullopt;
	return Geometry;
}

void CDoor::ResetCollision()
{
	if(!QmCollision::CanRoundPosition(m_Pos) || !QmCollision::CanRoundPosition(m_To))
	{
		m_Active = false;
		return;
	}
	if(Collision()->GetTile(m_Pos.x, m_Pos.y) || Collision()->GetFrontTile(m_Pos.x, m_Pos.y))
		return;
	const vec2 Dir = m_To - m_Pos;
	const float DirLength = length(Dir);
	const vec2 Direction = DirLength > 0.0f ? normalize_pre_length(Dir, DirLength) : vec2(0.0f, 0.0f);
	// snapshot 端点可能被 no-laser 缩短，仍按原地图方向与长度恢复。
	const int Side = DirLength > 0.0f ? MapSide(Direction) : -1;
	const int ConfiguredLength = Side == -1 ? -1 : MapLength(Side);
	const auto Geometry = CalcCollisionGeometry(m_Pos, m_To, Side, ConfiguredLength, ivec2(Collision()->GetWidth(), Collision()->GetHeight()));
	if(!Geometry)
	{
		m_Active = false;
		return;
	}
	m_Direction = Geometry->m_Direction;
	m_Length = Geometry->m_Length;
	m_Active = true;

	for(int i = 0; i < m_Length - 1; i++)
	{
		vec2 CurrentPos = m_Pos + m_Direction * i;

		if(Collision()->CheckPoint(CurrentPos))
			break;
		else
			Collision()->SetDoorCollisionAt(CurrentPos.x, CurrentPos.y, TILE_STOPA, 0, m_Number);
	}
}

void CDoor::Destroy()
{
	if(m_Active)
	{
		for(int i = 0; i < m_Length - 1; i++)
		{
			vec2 CurrentPos = m_Pos + m_Direction * i;
			if(Collision()->CheckPoint(CurrentPos))
				break;
			else
				Collision()->SetDoorCollisionAt(CurrentPos.x, CurrentPos.y, TILE_AIR, 0, 0);
		}
	}
	delete this;
}

void CDoor::Read(const CLaserData *pData)
{
	// it's flipped in the laser object
	m_Pos = pData->m_To;
	m_To = pData->m_From;
}

bool CDoor::Match(const CDoor *pDoor) const
{
	return pDoor->m_Pos == m_Pos && pDoor->m_To == m_To && pDoor->m_Number == m_Number;
}
