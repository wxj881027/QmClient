/* 请抬头享受阳光｜日子很好 我很我---------致咩子 */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmSlotReflow.h"

#include <base/system.h>

#include <algorithm>
#include <cmath>

namespace
{
	// 欠阻尼弹簧：ζ = c / (2√k) ≈ 0.66，收尾有一次轻微回弹（果冻感），
	// 但不会来回晃到让人眼晕。60Hz 下显式积分稳定（Δt·ω ≈ 0.38）。
	constexpr float SLOT_REFLOW_STIFFNESS = 380.0f;
	constexpr float SLOT_REFLOW_DAMPING = 26.0f;
	constexpr float SLOT_REFLOW_REST_OFFSET = 0.05f;
	constexpr float SLOT_REFLOW_REST_VELOCITY = 0.5f;
	// 一次换位跨过太多槽位时不当作「让位」，当作换了一整个列表，直接对齐。
	// 注意：跨行换位时列号会跳（删掉第 0 列会让后面所有卡片列号减 1 并折行），
	// 所以只按「槽位数差」和「行差」判断，不按列差。
	constexpr int SLOT_REFLOW_MAX_ROW_DELTA = 3;
	constexpr int SLOT_REFLOW_MAX_SLOT_DELTA = 8;
	constexpr size_t SLOT_REFLOW_MAX_ENTRIES = 400;
	constexpr int SLOT_REFLOW_ENTRY_TTL_FRAMES = 180;

	void StepSpring(float &Offset, float &Velocity, float Dt)
	{
		if(Offset == 0.0f && Velocity == 0.0f)
			return;
		const float Accel = -SLOT_REFLOW_STIFFNESS * Offset - SLOT_REFLOW_DAMPING * Velocity;
		Velocity += Accel * Dt;
		Offset += Velocity * Dt;
		if(std::abs(Offset) < SLOT_REFLOW_REST_OFFSET && std::abs(Velocity) < SLOT_REFLOW_REST_VELOCITY)
		{
			Offset = 0.0f;
			Velocity = 0.0f;
		}
	}
} // namespace

vec2 CQmSlotReflow::SlotDeltaOffset(int PrevSlot, int Slot, int Columns, float StepX, float StepY)
{
	const int Cols = maximum(1, Columns);
	const int PrevColumn = PrevSlot % Cols;
	const int PrevRow = PrevSlot / Cols;
	const int Column = Slot % Cols;
	const int Row = Slot / Cols;
	// 旧位置 - 新位置：叠加到布局上，卡片看起来还停在原处。
	return vec2((float)(PrevColumn - Column) * StepX, (float)(PrevRow - Row) * StepY);
}

void CQmSlotReflow::BeginFrame(float DtSec, int Columns, bool Snap)
{
	++m_Frame;
	m_Dt = std::clamp(DtSec, 0.0f, 0.1f);
	const bool ColumnsChanged = Columns != m_Columns;
	m_Columns = maximum(1, Columns);
	// 对齐帧不在这里清表：Track 会把当前槽位重记为基准并返回零偏移，
	// 这样下一帧才知道「从哪儿来」。
	m_Snap = Snap || ColumnsChanged || m_Dt <= 0.0f;
	PruneIfNeeded();
}

vec2 CQmSlotReflow::Track(const char *pKey, int Slot, float StepX, float StepY)
{
	if(pKey == nullptr)
		return vec2(0.0f, 0.0f);

	auto It = m_States.find(pKey);
	if(It == m_States.end())
	{
		SState State;
		State.m_Seen = true;
		State.m_Slot = Slot;
		State.m_LastFrame = m_Frame;
		m_States.emplace(pKey, State);
		return vec2(0.0f, 0.0f);
	}

	SState &State = It->second;
	State.m_LastFrame = m_Frame;
	if(!State.m_Seen || m_Snap)
	{
		State.m_Seen = true;
		State.m_Slot = Slot;
		State.m_OffsetX = 0.0f;
		State.m_OffsetY = 0.0f;
		State.m_VelocityX = 0.0f;
		State.m_VelocityY = 0.0f;
		return vec2(0.0f, 0.0f);
	}

	if(State.m_Slot != Slot)
	{
		const int Cols = maximum(1, m_Columns);
		const int RowDelta = std::abs(State.m_Slot / Cols - Slot / Cols);
		const int SlotDelta = std::abs(State.m_Slot - Slot);
		if(RowDelta > SLOT_REFLOW_MAX_ROW_DELTA || SlotDelta > SLOT_REFLOW_MAX_SLOT_DELTA)
		{
			// 跳得太远：多半是换了筛选或换了一整个列表，直接对齐不做动画。
			State.m_OffsetX = 0.0f;
			State.m_OffsetY = 0.0f;
			State.m_VelocityX = 0.0f;
			State.m_VelocityY = 0.0f;
		}
		else
		{
			const vec2 Delta = SlotDeltaOffset(State.m_Slot, Slot, m_Columns, StepX, StepY);
			State.m_OffsetX += Delta.x;
			State.m_OffsetY += Delta.y;
		}
		State.m_Slot = Slot;
	}

	// 先返回本帧位置再推进弹簧：换位那一帧必须完整停在旧位置，
	// 否则第一帧就已经挪动了一小段，看起来像「跳了一下才开始滑」。
	const vec2 Offset(State.m_OffsetX, State.m_OffsetY);
	StepSpring(State.m_OffsetX, State.m_VelocityX, m_Dt);
	StepSpring(State.m_OffsetY, State.m_VelocityY, m_Dt);
	return Offset;
}

void CQmSlotReflow::Reset()
{
	m_States.clear();
}

void CQmSlotReflow::PruneIfNeeded()
{
	if(m_States.size() <= SLOT_REFLOW_MAX_ENTRIES)
		return;
	for(auto It = m_States.begin(); It != m_States.end();)
	{
		if(It->second.m_LastFrame + SLOT_REFLOW_ENTRY_TTL_FRAMES < m_Frame)
			It = m_States.erase(It);
		else
			++It;
	}
}
