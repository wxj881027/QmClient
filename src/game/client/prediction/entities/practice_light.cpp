#include "practice_light.h"

#include "character.h"

#include <game/client/components/qmclient/practice_light_phase.h>
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
	m_Tick = (float)SERVER_TICK_SPEED * 0.15f;
	m_Pos = Pos;
	m_Rotation = Rotation;
	m_Length = Length;
	m_EvalTick = GameWorld()->GameTick();
	m_PhaseStartPos = m_Pos;
	m_PhaseStartRotation = m_Rotation;
	m_PhaseStartCurveLength = m_CurveLength;
	m_PhaseStartSpeed = m_Speed;
	m_PhaseTick = 0;
	m_PhaseOriginCaptured = true;
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
	qm_practice_light::SPhase Phase{m_Rotation, m_CurveLength, m_LengthL, m_Speed};
	qm_practice_light::Step(Phase, m_Length, m_AngularSpeed, m_Tick);
	m_Rotation = Phase.m_Rotation;
	m_CurveLength = Phase.m_CurveLength;
	m_LengthL = Phase.m_LengthL;
	m_Speed = Phase.m_Speed;
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

void CPracticeLight::CapturePhaseOrigin()
{
	m_PhaseStartPos = m_Pos;
	m_PhaseStartRotation = m_Rotation;
	m_PhaseStartCurveLength = m_CurveLength;
	m_PhaseStartSpeed = m_Speed;
	m_PhaseTick = 0;
	m_PhaseOriginCaptured = true;
}

void CPracticeLight::SetPhaseTick(int Tick, int RenderTick)
{
	if(!m_PhaseOriginCaptured)
		CapturePhaseOrigin();
	if(Tick < m_PhaseTick)
	{
		// 移动平台：位置也要回到零点，不能只重置角度。
		m_Pos = m_PhaseStartPos;
		m_Core = vec2(0.0f, 0.0f);
		// 拖动回退：回到地图初始相位，再从零点重放到目标 tick。
		m_Rotation = m_PhaseStartRotation;
		m_CurveLength = m_PhaseStartCurveLength;
		m_LengthL = 0;
		m_Speed = m_PhaseStartSpeed;
		m_PhaseTick = 0;
	}
	while(m_PhaseTick + m_Tick <= Tick)
	{
		m_PhaseTick += m_Tick;
		Collision()->MoverSpeed(m_Pos.x, m_Pos.y, &m_Core);
		m_Pos += m_Core;
		Move();
	}
	m_EvalTick = RenderTick;
	RefreshGeometry();
}

bool CPracticeLight::InitFromSnapshot(vec2 SnapshotHub, vec2 SnapshotFrom, int SnapshotEvalTick)
{
	const vec2 Delta = SnapshotFrom - SnapshotHub;
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
