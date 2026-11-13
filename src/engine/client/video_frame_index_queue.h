#ifndef ENGINE_CLIENT_VIDEO_FRAME_INDEX_QUEUE_H
#define ENGINE_CLIENT_VIDEO_FRAME_INDEX_QUEUE_H

#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

// 仅保存捕获槽索引；初始化后入队和出队不分配，串行访问由录像队列锁保证。
template<typename TAllocator = std::allocator<size_t>>
class CVideoFrameIndexQueue
{
	std::vector<size_t, TAllocator> m_vSlots;
	size_t m_Head = 0;
	size_t m_Count = 0;

public:
	explicit CVideoFrameIndexQueue(const TAllocator &Allocator = {}) :
		m_vSlots(Allocator) {}

	// 初始化失败保留原队列，未知异常仍交给调用方；运行期间不重设容量。
	bool TryInit(size_t Capacity)
	{
		if(Capacity == 0)
			return false;
		try
		{
			std::vector<size_t, TAllocator> vCandidate(Capacity, 0, m_vSlots.get_allocator());
			m_vSlots.swap(vCandidate);
			m_Head = m_Count = 0;
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
	bool Empty() const noexcept { return m_Count == 0; }
	bool Push(size_t Slot) noexcept
	{
		if(m_Count == m_vSlots.size())
			return false;
		size_t Tail = m_Head + m_Count;
		if(Tail >= m_vSlots.size())
			Tail -= m_vSlots.size();
		m_vSlots[Tail] = Slot;
		++m_Count;
		return true;
	}
	bool Pop(size_t &Slot) noexcept
	{
		if(Empty())
			return false;
		Slot = m_vSlots[m_Head];
		if(++m_Head == m_vSlots.size())
			m_Head = 0;
		--m_Count;
		return true;
	}
};

#endif
