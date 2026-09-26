#include "metal_frame_state.h"

#include <base/dbg.h>

CMetalFrameState::CMetalFrameState(size_t SlotCount) :
	m_SlotCount(SlotCount)
{
	dbg_assert(m_SlotCount > 0, "Metal frame state requires at least one slot");
	if(m_SlotCount == 0)
		m_SlotCount = 1;
	m_vSlots.resize(m_SlotCount);
}

bool CMetalFrameState::BeginFrame(size_t Slot)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_CurrentFrameId != 0 && !m_CurrentFrameFinalized && !m_CurrentFrameFailed)
		return false;

	m_CurrentSlot = Slot % m_SlotCount;
	SSlot &FrameSlot = m_vSlots[m_CurrentSlot];
	if(FrameSlot.m_State == ESlotState::IN_FLIGHT)
		return false;

	m_CurrentFrameId = m_NextFrameId++;
	m_CurrentFrameFinalized = false;
	m_CurrentFrameFailed = false;
	FrameSlot.m_FrameId = m_CurrentFrameId;
	FrameSlot.m_State = ESlotState::IN_FLIGHT;
	return true;
}

CMetalFrameState::EFinalizeResult CMetalFrameState::FinalizeFrameForPresent(bool DrawableAvailable)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_CurrentFrameId == 0)
		return EFinalizeResult::FAILED;
	if(m_CurrentFrameFinalized)
		return EFinalizeResult::ALREADY_FINALIZED;
	if(m_CurrentFrameFailed || !DrawableAvailable)
	{
		m_CurrentFrameFailed = true;
		m_vSlots[m_CurrentSlot].m_State = ESlotState::FAILED;
		return EFinalizeResult::FAILED;
	}

	m_CurrentFrameFinalized = true;
	m_LastPresentedFrameId = m_CurrentFrameId;
	m_LastPresentedSlot = m_CurrentSlot;
	m_CaptureRetained = true;
	return EFinalizeResult::PRESENTED;
}

bool CMetalFrameState::FinalizeFrameWithoutPresent()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_CurrentFrameId == 0 || m_CurrentFrameFinalized || m_CurrentFrameFailed)
		return false;
	m_CurrentFrameFinalized = true;
	return true;
}

void CMetalFrameState::MarkReadbackPresented()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_ReadbackPresented = true;
}

bool CMetalFrameState::ConsumeReadbackPresented()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(!m_ReadbackPresented)
		return false;
	m_ReadbackPresented = false;
	return true;
}

void CMetalFrameState::ClearReadbackPresented()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_ReadbackPresented = false;
}

bool CMetalFrameState::CompleteFrame(const SFrameCapture &Capture, bool Success)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(Capture.m_Slot >= m_vSlots.size())
		return false;
	SSlot &FrameSlot = m_vSlots[Capture.m_Slot];
	if(FrameSlot.m_FrameId != Capture.m_FrameId || FrameSlot.m_State != ESlotState::IN_FLIGHT)
		return false;
	if(Capture.m_FrameId == m_CurrentFrameId && !m_CurrentFrameFinalized)
		return false;

	FrameSlot.m_State = Success ? ESlotState::COMPLETED : ESlotState::FAILED;
	if(!Success && Capture.m_FrameId == m_CurrentFrameId)
		m_CurrentFrameFailed = true;
	return true;
}

size_t CMetalFrameState::DrainFrames()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	size_t Drained = 0;
	for(SSlot &FrameSlot : m_vSlots)
	{
		if(FrameSlot.m_State == ESlotState::IN_FLIGHT)
			++Drained;
		FrameSlot.m_State = ESlotState::AVAILABLE;
		FrameSlot.m_FrameId = 0;
	}
	m_CurrentFrameId = 0;
	m_CurrentFrameFinalized = false;
	m_CurrentFrameFailed = false;
	// Drain 是生命周期边界：即将释放的资源所属 capture/readback 不能泄漏到下一帧序列。
	m_LastPresentedFrameId = 0;
	m_LastPresentedSlot = 0;
	m_CaptureRetained = false;
	m_ReadbackPresented = false;
	return Drained;
}

bool CMetalFrameState::ReadLastPresentedFrame(SFrameCapture &Capture) const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(!m_CaptureRetained || m_LastPresentedFrameId == 0)
		return false;

	Capture.m_FrameId = m_LastPresentedFrameId;
	Capture.m_Slot = m_LastPresentedSlot;
	return true;
}

uint64_t CMetalFrameState::CurrentFrameId() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_CurrentFrameId;
}

uint64_t CMetalFrameState::LastPresentedFrameId() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_LastPresentedFrameId;
}

bool CMetalFrameState::CurrentFrameFailed() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_CurrentFrameFailed;
}

bool CMetalFrameState::CurrentFrameFinalized() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_CurrentFrameFinalized;
}

bool CMetalFrameState::CaptureRetained() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_CaptureRetained;
}

bool CMetalFrameState::ReadbackPresented() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_ReadbackPresented;
}

CMetalFrameState::ESlotState CMetalFrameState::SlotState(size_t Slot) const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(Slot >= m_vSlots.size())
		return ESlotState::FAILED;
	return m_vSlots[Slot].m_State;
}
