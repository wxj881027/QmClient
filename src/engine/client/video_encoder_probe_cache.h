#ifndef ENGINE_CLIENT_VIDEO_ENCODER_PROBE_CACHE_H
#define ENGINE_CLIENT_VIDEO_ENCODER_PROBE_CACHE_H

#include "video_encoder.h"

#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

// 成功结果只发布一次；资源失败返回独立空快照，避免与其它线程的后续发布竞态。
template<typename TAllocator = std::allocator<video_encoder::SProbeResult>>
class CVideoEncoderProbeCache
{
public:
	using CResults = std::vector<video_encoder::SProbeResult, TAllocator>;

private:
	std::once_flag m_Once;
	CResults m_vResults;

	static const CResults &Unavailable()
	{
		static const CResults s_vUnavailable;
		return s_vUnavailable;
	}

public:
	explicit CVideoEncoderProbeCache(const TAllocator &Allocator = {}) :
		m_vResults(Allocator) {}

	// Probe 只填充局部候选，异常不会使 once_flag 完成；下一次调用可重新探测。
	template<typename TProbe>
	const CResults &Get(TProbe &&Probe)
	{
		try
		{
			std::call_once(m_Once, [this, &Probe]() {
				CResults vCandidate(m_vResults.get_allocator());
				Probe(vCandidate);
				m_vResults.swap(vCandidate);
			});
			return m_vResults;
		}
		catch(const std::bad_alloc &)
		{
			return Unavailable();
		}
		catch(const std::length_error &)
		{
			return Unavailable();
		}
	}
};

#endif
