#ifndef ENGINE_CLIENT_BUFFER_CONTAINER_POOL_H
#define ENGINE_CLIENT_BUFFER_CONTAINER_POOL_H

#include <base/dbg.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

struct SBufferContainerSlot
{
	int m_AssociatedBufferObject = -1;
	int m_NextFree = -2;
};

// 创建命令被接管前保留槽位，提交抛出时只归还前端身份，不删除调用方的 BO。
template<typename TAllocator = std::allocator<SBufferContainerSlot>>
class CBufferContainerPool
{
	std::vector<SBufferContainerSlot, TAllocator> m_vSlots;
	int m_FirstFree = -1;

	int Allocate()
	{
		if(m_FirstFree != -1)
		{
			const int Index = m_FirstFree;
			m_FirstFree = m_vSlots[Index].m_NextFree;
			m_vSlots[Index].m_NextFree = -2;
			return Index;
		}
		if(m_vSlots.size() >= static_cast<size_t>(std::numeric_limits<int>::max()) || m_vSlots.size() >= m_vSlots.max_size())
			throw std::length_error("Buffer container index capacity exceeded");
		const int Index = static_cast<int>(m_vSlots.size());
		m_vSlots.emplace_back();
		return Index;
	}

	class CReservation
	{
		CBufferContainerPool &m_Pool;
		int m_Index;

	public:
		explicit CReservation(CBufferContainerPool &Pool) :
			m_Pool(Pool), m_Index(Pool.Allocate()) {}
		CReservation(const CReservation &) = delete;
		CReservation &operator=(const CReservation &) = delete;
		~CReservation()
		{
			if(m_Index >= 0)
				m_Pool.Release(m_Index);
		}
		int Index() const { return m_Index; }
		int Commit(int BufferObject) noexcept
		{
			m_Pool.Bind(m_Index, BufferObject);
			return std::exchange(m_Index, -1);
		}
	};

public:
	explicit CBufferContainerPool(const TAllocator &Allocator = TAllocator()) :
		m_vSlots(Allocator) {}
	CBufferContainerPool(const CBufferContainerPool &) = delete;
	CBufferContainerPool &operator=(const CBufferContainerPool &) = delete;

	// Submit 只提交当前创建命令；成功返回后不再进行可失败的准备，也不重置本池。
	template<typename TSubmit>
	int Create(int BufferObject, TSubmit &&Submit)
	{
		CReservation Reservation(*this);
		std::forward<TSubmit>(Submit)(Reservation.Index());
		return Reservation.Commit(BufferObject);
	}

	bool IsAllocated(int Index) const noexcept
	{
		return Index >= 0 && static_cast<size_t>(Index) < m_vSlots.size() && m_vSlots[Index].m_NextFree == -2;
	}
	int AssociatedBufferObject(int Index) const
	{
		dbg_assert(IsAllocated(Index), "Buffer container was not allocated.");
		return m_vSlots[Index].m_AssociatedBufferObject;
	}
	void Bind(int Index, int BufferObject) noexcept
	{
		dbg_assert(IsAllocated(Index), "Buffer container was not allocated.");
		m_vSlots[Index].m_AssociatedBufferObject = BufferObject;
	}
	void Release(int Index) noexcept
	{
		if(!IsAllocated(Index))
			return;
		m_vSlots[Index].m_AssociatedBufferObject = -1;
		m_vSlots[Index].m_NextFree = m_FirstFree;
		m_FirstFree = Index;
	}
	size_t Size() const { return m_vSlots.size(); }
	void Clear()
	{
		m_vSlots.clear();
		m_FirstFree = -1;
	}
};

#endif
