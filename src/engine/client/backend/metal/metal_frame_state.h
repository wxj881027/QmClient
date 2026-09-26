#ifndef ENGINE_CLIENT_BACKEND_METAL_METAL_FRAME_STATE_H
#define ENGINE_CLIENT_BACKEND_METAL_METAL_FRAME_STATE_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class CMetalFrameState
{
public:
	struct SFrameCapture
	{
		uint64_t m_FrameId = 0;
		size_t m_Slot = 0;
	};

	enum class EFinalizeResult
	{
		PRESENTED,
		ALREADY_FINALIZED,
		FAILED,
	};

	enum class ESlotState
	{
		AVAILABLE,
		IN_FLIGHT,
		COMPLETED,
		FAILED,
	};

	explicit CMetalFrameState(size_t SlotCount = 3);

	bool BeginFrame(size_t Slot);
	EFinalizeResult FinalizeFrameForPresent(bool DrawableAvailable);
	bool FinalizeFrameWithoutPresent();
	void MarkReadbackPresented();
	bool ConsumeReadbackPresented();
	void ClearReadbackPresented();
	bool CompleteFrame(const SFrameCapture &Capture, bool Success);
	size_t DrainFrames();
	bool ReadLastPresentedFrame(SFrameCapture &Capture) const;

	uint64_t CurrentFrameId() const;
	uint64_t LastPresentedFrameId() const;
	bool CurrentFrameFailed() const;
	bool CurrentFrameFinalized() const;
	bool CaptureRetained() const;
	bool ReadbackPresented() const;
	ESlotState SlotState(size_t Slot) const;

private:
	size_t m_SlotCount;
	size_t m_CurrentSlot = 0;
	uint64_t m_NextFrameId = 1;
	uint64_t m_CurrentFrameId = 0;
	uint64_t m_LastPresentedFrameId = 0;
	size_t m_LastPresentedSlot = 0;
	bool m_CurrentFrameFinalized = false;
	bool m_CurrentFrameFailed = false;
	bool m_CaptureRetained = false;
	bool m_ReadbackPresented = false;
	struct SSlot
	{
		uint64_t m_FrameId = 0;
		ESlotState m_State = ESlotState::AVAILABLE;
	};
	std::vector<SSlot> m_vSlots;
	mutable std::mutex m_Mutex;
};

#endif
