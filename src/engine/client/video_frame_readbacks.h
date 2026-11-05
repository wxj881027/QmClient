#ifndef ENGINE_CLIENT_VIDEO_FRAME_READBACKS_H
#define ENGINE_CLIENT_VIDEO_FRAME_READBACKS_H

#include <engine/client/render_target_readback_queue.h>

#include <array>
#include <cstdint>

// 只在主线程管理视频在途帧；请求及像素仍由共享读回队列持有。
class CVideoFrameReadbacks
{
	struct SRequest
	{
		int m_Index = -1;
		uint32_t m_Generation = 0;
	};
	std::array<SRequest, 2> m_aPending;
	int m_Count = 0;

public:
	bool Empty() const { return m_Count == 0; }
	// 仅在读回队列已排空或随设备一起清空后复位。
	void Reset() noexcept
	{
		m_aPending = {};
		m_Count = 0;
	}

	// true 且无像素表示正常预热；false 表示申请或已到期的读回失败。
	template<typename TWaitForIdle>
	bool Enqueue(CRenderTargetReadbackQueue &Queue, int Index, uint32_t Generation, bool CompletionSignal, CImageInfo &Image, TWaitForIdle WaitForIdle)
	{
		// 不能把申请失败的空句柄推进流水线，也不能等待尚未被下一笔读回结算的 PBO。
		if(Index < 0)
			return false;
		const SRequest Incoming{Index, Generation};
		bool Success = true;
		if(CompletionSignal)
		{
			if(m_Count == 2)
			{
				Success = Queue.ResolveBlocking(m_aPending[0].m_Index, m_aPending[0].m_Generation, Image);
				m_aPending[0] = m_aPending[1];
				m_aPending[1] = Incoming;
			}
			else
				m_aPending[m_Count++] = Incoming;
		}
		else
		{
			WaitForIdle();
			for(int i = 0; i < m_Count; ++i)
			{
				CImageInfo Discard;
				const SRequest &Pending = m_aPending[i];
				const bool Resolved = Queue.ResolveAfterIdle(Pending.m_Index, Pending.m_Generation, i == 0 ? Image : Discard);
				Success = Resolved && Success;
				Discard.Free();
			}
			m_aPending[0] = Incoming;
			m_Count = 1;
		}
		return Success;
	}

	// Prepare 必须结算后端在途 DMA 并等待渲染线程空闲。
	// 失败返回 -1；已转移的图像仍归调用方释放，不能把剩余成功帧拼成完整成片。
	template<typename TPrepare>
	int Flush(CRenderTargetReadbackQueue &Queue, bool CompletionSignal, CImageInfo *pImages, int MaxImages, TPrepare Prepare)
	{
		if(Empty())
			return 0;
		Prepare();
		int Written = 0;
		bool Success = true;
		for(int i = 0; i < m_Count; ++i)
		{
			CImageInfo Discard;
			const bool HasOutput = pImages != nullptr && Written < MaxImages;
			CImageInfo &Output = HasOutput ? pImages[Written] : Discard;
			const SRequest &Pending = m_aPending[i];
			const bool Resolved = CompletionSignal ?
						      Queue.ResolveBlocking(Pending.m_Index, Pending.m_Generation, Output) :
						      Queue.ResolveAfterIdle(Pending.m_Index, Pending.m_Generation, Output);
			if(Resolved && HasOutput)
				++Written;
			Success = Resolved && HasOutput && Success;
			Discard.Free();
		}
		Reset();
		return Success ? Written : -1;
	}
};

#endif
