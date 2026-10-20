#include "QmCardReflow.h"

#include <algorithm>
#include <cmath>

namespace
{
	// 欠阻尼弹簧：ζ≈0.6，收尾带一次轻微回弹，读作「果冻感」但不甩出去。
	constexpr float REFLOW_STIFFNESS = 700.0f;
	constexpr float REFLOW_DAMPING = 32.0f;
	constexpr float REFLOW_REST_OFFSET = 0.05f;
	constexpr float REFLOW_REST_VELOCITY = 0.5f;
	constexpr float REFLOW_MAX_DT = 0.05f;
	constexpr int REFLOW_REAP_FRAMES = 240;
} // namespace

void CQmCardReflow::BeginFrame(float Dt, bool Snap)
{
	m_Dt = std::clamp(Dt, 0.0f, REFLOW_MAX_DT);
	m_Snap = Snap;
	++m_Frame;
	if(m_Frame % REFLOW_REAP_FRAMES == 0)
		Reap();
}

void CQmCardReflow::Reset()
{
	m_States.clear();
}

void CQmCardReflow::Reap()
{
	for(auto It = m_States.begin(); It != m_States.end();)
	{
		if(m_Frame - It->second.m_LastFrame > REFLOW_REAP_FRAMES)
			It = m_States.erase(It);
		else
			++It;
	}
}

void CQmCardReflow::StepSpring(float &Offset, float &Velocity, float Dt)
{
	if(Dt <= 0.0f)
		return;
	// 半隐式欧拉：先更新速度再更新位置，Dt 已被钳制，保证稳定。
	Velocity += (-REFLOW_STIFFNESS * Offset - REFLOW_DAMPING * Velocity) * Dt;
	Offset += Velocity * Dt;
	if(std::fabs(Offset) < REFLOW_REST_OFFSET && std::fabs(Velocity) < REFLOW_REST_VELOCITY)
	{
		Offset = 0.0f;
		Velocity = 0.0f;
	}
}

vec2 CQmCardReflow::Track(const char *pKey, const CUIRect &Current, const CUIRect *pSeedFrom)
{
	if(pKey == nullptr)
		return vec2(0.0f, 0.0f);
	SState &State = m_States[pKey];
	State.m_LastFrame = m_Frame;

	// 首见或强制对齐：直接贴住当前布局。首见且给了种子时，从种子位置出发。
	if(!State.m_Known || m_Snap)
	{
		const bool Fresh = !State.m_Known;
		State.m_Known = true;
		State.m_LastRect = Current;
		State.m_VelocityX = 0.0f;
		State.m_VelocityY = 0.0f;
		State.m_OffsetX = 0.0f;
		State.m_OffsetY = 0.0f;
		if(Fresh && !m_Snap && pSeedFrom != nullptr)
		{
			State.m_OffsetX = pSeedFrom->x - Current.x;
			State.m_OffsetY = pSeedFrom->y - Current.y;
		}
		return vec2(State.m_OffsetX, State.m_OffsetY);
	}

	// 先推进上一帧的残余，再叠加本帧位移：换位瞬间视觉刚好停在旧位置，
	// 之后从静止出发弹回新位置（回弹即果冻感）。
	StepSpring(State.m_OffsetX, State.m_VelocityX, m_Dt);
	StepSpring(State.m_OffsetY, State.m_VelocityY, m_Dt);

	// FLIP 的 Invert：布局已经换位，把差额并进偏移，视觉上还停在上一帧的位置。
	State.m_OffsetX += State.m_LastRect.x - Current.x;
	State.m_OffsetY += State.m_LastRect.y - Current.y;
	State.m_LastRect = Current;
	return vec2(State.m_OffsetX, State.m_OffsetY);
}
