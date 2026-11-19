#ifndef ENGINE_CLIENT_RENDER_TARGET_POOL_H
#define ENGINE_CLIENT_RENDER_TARGET_POOL_H

#include <base/vmath.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <vector>

class CRenderTargetPool
{
	static constexpr int LIVE = -1;
	static constexpr int PENDING_DESTROY = -2;
	static constexpr int NO_DEFERRED = -1;
	struct SSlot
	{
		ivec2 m_Size = ivec2(0, 0);
		int m_SampleCount = 1;
		int m_NextFree = 0;
		int m_NextDeferred = NO_DEFERRED;
	};
	std::vector<SSlot> m_vSlots;
	size_t m_FirstFree = 0;
	int m_FirstDeferred = NO_DEFERRED;
	int m_LastDeferred = NO_DEFERRED;

	void Recycle(int Id)
	{
		m_vSlots[Id].m_Size = ivec2(0, 0);
		m_vSlots[Id].m_SampleCount = 1;
		m_vSlots[Id].m_NextFree = static_cast<int>(m_FirstFree);
		m_vSlots[Id].m_NextDeferred = NO_DEFERRED;
		m_FirstFree = Id;
	}

public:
	int Allocate(int Width, int Height, int SampleCount = 1)
	{
		if(Width <= 0 || Height <= 0 || SampleCount <= 0)
			return -1;
		if(m_FirstFree == m_vSlots.size())
		{
			const size_t OldSize = m_vSlots.size();
			const size_t MaxSize = std::min(static_cast<size_t>(std::numeric_limits<int>::max()), m_vSlots.max_size());
			if(OldSize >= MaxSize)
				return -1;
			const size_t NewSize = std::min(OldSize == 0 ? size_t{16} : OldSize * 2, MaxSize);
			try
			{
				m_vSlots.resize(NewSize);
			}
			catch(const std::bad_alloc &)
			{
				return -1;
			}
			for(size_t Index = OldSize; Index < NewSize; ++Index)
				m_vSlots[Index].m_NextFree = static_cast<int>(Index + 1);
		}
		const int Id = static_cast<int>(m_FirstFree);
		m_FirstFree = m_vSlots[Id].m_NextFree;
		m_vSlots[Id].m_NextFree = LIVE;
		m_vSlots[Id].m_Size = ivec2(Width, Height);
		m_vSlots[Id].m_SampleCount = SampleCount;
		return Id;
	}

	bool IsLive(int Id) const
	{
		return Id >= 0 && static_cast<size_t>(Id) < m_vSlots.size() && m_vSlots[Id].m_NextFree == LIVE;
	}

	ivec2 Size(int Id) const
	{
		return IsLive(Id) ? m_vSlots[Id].m_Size : ivec2(0, 0);
	}

	int SampleCount(int Id) const
	{
		return IsLive(Id) ? m_vSlots[Id].m_SampleCount : 1;
	}

	template<typename TDestroy>
	void Release(int Id, bool Defer, TDestroy &&Destroy)
	{
		if(!IsLive(Id))
			return;
		m_vSlots[Id].m_NextFree = PENDING_DESTROY;
		if(Defer)
		{
			// 用现有槽位排队，延迟释放不需要分配内存。
			if(m_LastDeferred == NO_DEFERRED)
				m_FirstDeferred = Id;
			else
				m_vSlots[m_LastDeferred].m_NextDeferred = Id;
			m_LastDeferred = Id;
			return;
		}
		try
		{
			Destroy(Id);
		}
		catch(...)
		{
			m_vSlots[Id].m_NextFree = LIVE;
			throw;
		}
		Recycle(Id);
	}

	template<typename TDestroy>
	void FlushDeferred(TDestroy &&Destroy)
	{
		// 先在 End 命令之后排入销毁，再允许新建命令复用同一槽位。
		while(m_FirstDeferred != NO_DEFERRED)
		{
			const int Id = m_FirstDeferred;
			Destroy(Id);
			m_FirstDeferred = m_vSlots[Id].m_NextDeferred;
			if(m_FirstDeferred == NO_DEFERRED)
				m_LastDeferred = NO_DEFERRED;
			Recycle(Id);
		}
	}

	void Clear()
	{
		// 后端已负责旧设备的资源清理，不能向新设备发送旧销毁请求。
		m_vSlots.clear();
		m_FirstFree = 0;
		m_FirstDeferred = NO_DEFERRED;
		m_LastDeferred = NO_DEFERRED;
	}
};

#endif
