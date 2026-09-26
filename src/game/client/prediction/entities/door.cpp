/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "door.h"

#include "character.h"

#include <game/client/laser_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <cmath>

CDoor::CDoor(CGameWorld *pGameWorld, int Id, const CLaserData *pData) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_DOOR)
{
	m_Id = Id;
	m_Active = false;

	m_Number = pData->m_SwitchNumber;
	m_Layer = m_Number > 0 ? LAYER_SWITCH : LAYER_GAME;

	Read(pData);
}

// OnEntity 按这个顺序检查八个相邻 tile 来确定激光门长度。
static const int s_aSideOffsetX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int s_aSideOffsetY[8] = {1, 1, 0, -1, -1, -1, 0, 1};

int CDoor::MapSide() const
{
	const int OffsetX = round_to_int(m_Direction.x);
	const int OffsetY = round_to_int(m_Direction.y);
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

void CDoor::ResetCollision()
{
	if(Collision()->GetTile(m_Pos.x, m_Pos.y) || Collision()->GetFrontTile(m_Pos.x, m_Pos.y))
		return;

	m_Active = true;

	const vec2 Dir = m_To - m_Pos;
	const float DirLength = length(Dir);
	m_Direction = DirLength > 0.0f ? normalize_pre_length(Dir, DirLength) : vec2(0.0f, 0.0f);

	// snapshot 端点可能被 no-laser tile 缩短，须恢复地图配置的长度，
	// 使本地碰撞预测与服务端激光门几何保持一致。
	const int Side = DirLength > 0.0f ? MapSide() : -1;
	const int ConfiguredLength = Side == -1 ? -1 : MapLength(Side);
	if(ConfiguredLength != -1)
	{
		m_Length = ConfiguredLength;
		m_Direction = vec2(std::sin(pi / 4 * Side), std::cos(pi / 4 * Side));
	}
	else
	{
		// 防止畸形或无界的网络数据使每个 snapshot 执行过长遍历。
		const float MaxLength = (Collision()->GetWidth() + Collision()->GetHeight()) * 32.0f;
		if(DirLength <= 0.0f || DirLength > MaxLength)
		{
			m_Active = false;
			return;
		}
		m_Length = round_to_int(DirLength);
	}

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
