#ifndef ENGINE_CLIENT_RENDER_TARGET_POOL_H
#define ENGINE_CLIENT_RENDER_TARGET_POOL_H

#include <base/vmath.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

class CRenderTargetPool
{
	static constexpr int LIVE = -1;
	static constexpr int PENDING_DESTROY = -2;
	struct SSlot
	{
		ivec2 m_Size = ivec2(0, 0);
		int m_NextFree = 0;
	};
	std::vector<SSlot> m_vSlots;
	std::vector<int> m_vDeferredDestroy;
	size_t m_FirstFree = 0;

	void Recycle(int Id)
	{
		m_vSlots[Id].m_Size = ivec2(0, 0);
		m_vSlots[Id].m_NextFree = static_cast<int>(m_FirstFree);
		m_FirstFree = Id;
	}

public:
	int Allocate(int Width, int Height)
	{
		if(Width <= 0 || Height <= 0)
			return -1;
		if(m_FirstFree == m_vSlots.size())
		{
			const size_t OldSize = m_vSlots.size();
			const size_t MaxSize = std::numeric_limits<int>::max();
			if(OldSize >= MaxSize)
				return -1;
			const size_t NewSize = std::min(OldSize == 0 ? size_t{16} : OldSize * 2, MaxSize);
			m_vSlots.resize(NewSize);
			for(size_t Index = OldSize; Index < NewSize; ++Index)
				m_vSlots[Index].m_NextFree = static_cast<int>(Index + 1);
		}
		const int Id = static_cast<int>(m_FirstFree);
		m_FirstFree = m_vSlots[Id].m_NextFree;
		m_vSlots[Id].m_NextFree = LIVE;
		m_vSlots[Id].m_Size = ivec2(Width, Height);
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

	template<typename TDestroy>
	void Release(int Id, bool Defer, TDestroy &&Destroy)
	{
		if(!IsLive(Id))
			return;
		if(Defer)
			m_vDeferredDestroy.push_back(Id);
		m_vSlots[Id].m_NextFree = PENDING_DESTROY;
		if(!Defer)
		{
			Destroy(Id);
			Recycle(Id);
		}
	}

	template<typename TDestroy>
	void FlushDeferred(TDestroy &&Destroy)
	{
		// 先在 End 命令之后排入销毁，再允许新建命令复用同一槽位。
		for(int Id : m_vDeferredDestroy)
		{
			Destroy(Id);
			Recycle(Id);
		}
		m_vDeferredDestroy.clear();
	}

	void Clear()
	{
		// 后端已负责旧设备的资源清理，不能向新设备发送旧销毁请求。
		m_vSlots.clear();
		m_vDeferredDestroy.clear();
		m_FirstFree = 0;
	}
};

#endif
