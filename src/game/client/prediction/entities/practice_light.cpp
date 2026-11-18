#include "practice_light.h"

#include "character.h"

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>

CPracticeLight::CPracticeLight(CGameWorld *pGameWorld, vec2 Pos, float Rotation, int Length, int Layer, int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LIGHT)
{
	m_To = vec2(0.0f, 0.0f);
	m_Core = vec2(0.0f, 0.0f);
	m_Layer = Layer;
	m_Number = Number;
	m_Tick = SERVER_TICK_SPEED * 0.15f;
	m_Pos = Pos;
	m_Rotation = Rotation;
	m_Length = Length;
	m_EvalTick = GameWorld()->GameTick();
	GameWorld()->InsertEntity(this);
	RefreshGeometry();
}

bool CPracticeLight::HitCharacter()
{
	std::vector<CCharacter *> vpHitCharacters = GameWorld()->IntersectedCharacters(m_Pos, m_To, 0.0f, nullptr);
	if(vpHitCharacters.empty())
		return false;
	for(CCharacter *pChar : vpHitCharacters)
	{
		if(m_Layer == LAYER_SWITCH && m_Number > 0)
		{
			const auto &vSwitchers = GameWorld()->Switchers();
			const int Team = pChar->Team();
			if(m_Number >= (int)vSwitchers.size() || Team < 0 || Team >= NUM_DDRACE_TEAMS || !vSwitchers[m_Number].m_aStatus[Team])
				continue;
		}
		pChar->Freeze();
	}
	return true;
}

void CPracticeLight::Move()
{
	if(m_Speed != 0)
	{
		if((m_CurveLength >= m_Length && m_Speed > 0) || (m_CurveLength <= 0 && m_Speed < 0))
			m_Speed = -m_Speed;
		m_CurveLength += m_Speed * m_Tick + m_LengthL;
		m_LengthL = 0;
		if(m_CurveLength > m_Length)
		{
			m_LengthL = m_CurveLength - m_Length;
			m_CurveLength = m_Length;
		}
		else if(m_CurveLength < 0)
		{
			m_LengthL = m_CurveLength;
			m_CurveLength = 0;
		}
	}

	m_Rotation += m_AngularSpeed * m_Tick;
	if(m_Rotation > pi * 2)
		m_Rotation -= pi * 2;
	else if(m_Rotation < 0)
		m_Rotation += pi * 2;
}

void CPracticeLight::Step()
{
	Move();
	RefreshGeometry();
}

void CPracticeLight::RefreshGeometry()
{
	const vec2 Direction = vec2(std::sin(m_Rotation), std::cos(m_Rotation));
	const vec2 NextPosition = m_Pos + normalize(Direction) * m_CurveLength;
	Collision()->IntersectNoLaser(m_Pos, NextPosition, &m_To, nullptr);
}

bool CPracticeLight::InitFromSnapshot(vec2 SnapshotHub, vec2 SnapshotFrom, int SnapshotEvalTick)
{
	const vec2 Delta = SnapshotHub - SnapshotFrom;
	const float Length = length(Delta);
	// 快照端点重合说明服务器这束激光当前不可见（开关关闭或长度为零），
	// 没有相位可继承，保留地图初始角度等下一帧自行恢复。
	if(Length <= 0.5f)
		return false;
	m_Rotation = std::atan2(Delta.x, Delta.y);
	if(m_Rotation < 0.0f)
		m_Rotation += 2.0f * pi;
	m_CurveLength = minimum((int)std::lround(Length), m_Length);
	m_LengthL = 0;
	m_EvalTick = SnapshotEvalTick > 0 ? SnapshotEvalTick : GameWorld()->GameTick();
	RefreshGeometry();
	return true;
}

std::optional<CLaserData> CPracticeLight::RenderData(int Team)
{
	if(m_Layer == LAYER_SWITCH && m_Number > 0)
	{
		const auto &vSwitchers = GameWorld()->Switchers();
		if(m_Number >= (int)vSwitchers.size() || Team < 0 || Team >= NUM_DDRACE_TEAMS || !vSwitchers[m_Number].m_aStatus[Team])
			return std::nullopt;
	}
	CLaserData Data;
	// 与服务器 CLight::Snap 的端点约定一致：m_To 是激光头，m_From 是当前末端。
	Data.m_From = m_To;
	Data.m_To = m_Pos;
	Data.m_StartTick = m_EvalTick;
	Data.m_ExtraInfo = true;
	Data.m_Owner = -1;
	Data.m_Type = LASERTYPE_FREEZE;
	Data.m_SwitchNumber = m_Number;
	Data.m_Subtype = 0;
	Data.m_Predict = false;
	Data.m_TuneZone = GameWorld()->m_WorldConfig.m_UseTuneZones ? Collision()->IsTune(Collision()->GetMapIndex(m_Pos)) : 0;
	return Data;
}

void CPracticeLight::Tick()
{
	if(GameWorld()->GameTick() % m_Tick == 0)
	{
		m_EvalTick = GameWorld()->GameTick();
		Collision()->MoverSpeed(m_Pos.x, m_Pos.y, &m_Core);
		m_Pos += m_Core;
		Step();
	}
	HitCharacter();
}
