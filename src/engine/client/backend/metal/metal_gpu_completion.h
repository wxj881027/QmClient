#ifndef ENGINE_CLIENT_BACKEND_METAL_METAL_GPU_COMPLETION_H
#define ENGINE_CLIENT_BACKEND_METAL_METAL_GPU_COMPLETION_H

#include "metal_frame_state.h"

#include <engine/client/backend/backend_base.h>

#include <atomic>
#include <utility>

// GPU 回调只发布首份完整故障；可选诊断失败不能跳过帧完成状态。
class CMetalGpuCompletion
{
public:
	struct SFailure
	{
		uint64_t m_FrameId = 0;
		int m_CommandId = -1;
		int m_Status = 0;
		char m_aDescription[1024]{};
	};

private:
	enum class EState
	{
		EMPTY,
		WRITING,
		PUBLISHED,
	};
	std::atomic<EState> m_State{EState::EMPTY};
	SFailure m_Failure;

	template<typename TDescribe>
	void Record(const CMetalFrameState::SFrameCapture &Capture, int CommandId, int Status, TDescribe &&Describe) noexcept
	{
		EState Expected = EState::EMPTY;
		if(!m_State.compare_exchange_strong(Expected, EState::WRITING, std::memory_order_acq_rel))
			return;
		m_Failure.m_FrameId = Capture.m_FrameId;
		m_Failure.m_CommandId = CommandId;
		m_Failure.m_Status = Status;
		try
		{
			Describe(m_Failure.m_aDescription, sizeof(m_Failure.m_aDescription));
			m_Failure.m_aDescription[sizeof(m_Failure.m_aDescription) - 1] = '\0';
		}
		catch(...)
		{
			// 丢弃半份文本，保留无需分配的故障身份和状态。
			m_Failure.m_aDescription[0] = '\0';
		}
		m_State.store(EState::PUBLISHED, std::memory_order_release);
	}

public:
	template<typename TDescribe>
	bool CompleteFrame(CMetalFrameState &Frames, const CMetalFrameState::SFrameCapture &Capture, bool Success, int CommandId, int Status, TDescribe &&Describe)
	{
		if(!Success)
			Record(Capture, CommandId, Status, std::forward<TDescribe>(Describe));
		return Frames.CompleteFrame(Capture, Success);
	}

	const SFailure *Get() const noexcept
	{
		return m_State.load(std::memory_order_acquire) == EState::PUBLISHED ? &m_Failure : nullptr;
	}

	template<typename TWriteDetails>
	bool Publish(SGfxErrorContainer &Error, TWriteDetails &&WriteDetails) const noexcept
	{
		const SFailure *pFailure = Get();
		if(pFailure == nullptr)
			return false;
		if(Error.m_ErrorType != GFX_ERROR_TYPE_NONE)
			return true;
		Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
		try
		{
			WriteDetails(Error, *pFailure);
		}
		catch(...)
		{
			// 报告文本分配失败仍保留停止提交所需的错误类型。
		}
		return true;
	}

	// 调用方必须先等待全部已提交 GPU 回调结束，再开始新设备生命周期。
	void Reset() noexcept
	{
		m_Failure = {};
		m_State.store(EState::EMPTY, std::memory_order_release);
	}
};

#endif
