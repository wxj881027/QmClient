#ifndef ENGINE_CLIENT_TEXTURE_SLOT_POOL_H
#define ENGINE_CLIENT_TEXTURE_SLOT_POOL_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

struct STextureSlot
{
	int m_NextFree = 0;
	uint32_t m_Generation = 0;
};

// 同一槽位的空闲链和代数一起扩容，分配失败不能发布一半状态。
template<typename TAllocator = std::allocator<STextureSlot>>
class CTextureSlotPool
{
	std::vector<STextureSlot, TAllocator> m_vSlots;
	size_t m_FirstFree = 0;
	uint32_t m_Epoch = 0;

	size_t MaxSize() const { return std::min<size_t>(std::numeric_limits<int>::max(), m_vSlots.max_size()); }

	bool Resize(size_t Size)
	{
		if(Size > MaxSize())
			return false;
		try
		{
			m_vSlots.resize(Size);
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

public:
	explicit CTextureSlotPool(const TAllocator &Allocator = TAllocator()) :
		m_vSlots(Allocator) {}

	int Allocate()
	{
		if(m_FirstFree == m_vSlots.size())
		{
			const size_t OldSize = m_vSlots.size();
			if(OldSize >= MaxSize() || !Resize(std::min(OldSize == 0 ? size_t{16} : OldSize * 2, MaxSize())))
				return -1;
			for(size_t Index = OldSize; Index < m_vSlots.size(); ++Index)
				m_vSlots[Index].m_NextFree = static_cast<int>(Index + 1);
		}
		const int Id = static_cast<int>(m_FirstFree);
		m_FirstFree = m_vSlots[Id].m_NextFree;
		m_vSlots[Id].m_NextFree = -1;
		return Id;
	}

	// Id 必须来自成功的 Allocate；保留现有的设备纪元与槽位代数编码。
	uint32_t Generation(int Id) const { return (m_Epoch << 16) | (m_vSlots[Id].m_Generation & 0xffffu); }

	bool IsAllocated(int Id, uint32_t ExpectedGeneration) const
	{
		return Id >= 0 && static_cast<size_t>(Id) < m_vSlots.size() &&
		       m_vSlots[Id].m_NextFree == -1 && Generation(Id) == ExpectedGeneration;
	}

	void Release(int Id, uint32_t ExpectedGeneration)
	{
		if(!IsAllocated(Id, ExpectedGeneration))
			return;
		m_vSlots[Id].m_NextFree = static_cast<int>(m_FirstFree);
		++m_vSlots[Id].m_Generation;
		m_FirstFree = Id;
	}

	// 调用前旧设备已销毁；即使新容量分配失败，也必须使全部旧句柄失效。
	bool Reset(size_t InitialSize)
	{
		const bool Resized = Resize(InitialSize);
		++m_Epoch;
		for(size_t Index = 0; Index < m_vSlots.size(); ++Index)
		{
			++m_vSlots[Index].m_Generation;
			m_vSlots[Index].m_NextFree = static_cast<int>(Index + 1);
		}
		m_FirstFree = 0;
		return Resized;
	}

	// 命令接管前保留槽位所有权；申请部分失败或退出时自动归还。
	template<size_t Count>
	class CReservation
	{
		CTextureSlotPool *m_pPool;
		std::array<int, Count> m_aIds;
		std::array<uint32_t, Count> m_aGenerations{};
		bool m_Valid = false;

	public:
		explicit CReservation(CTextureSlotPool &Pool) :
			m_pPool(&Pool)
		{
			static_assert(Count > 0);
			m_aIds.fill(-1);
			for(size_t Index = 0; Index < Count; ++Index)
			{
				m_aIds[Index] = Pool.Allocate();
				if(m_aIds[Index] < 0)
					return;
				m_aGenerations[Index] = Pool.Generation(m_aIds[Index]);
			}
			m_Valid = true;
		}
		CReservation(const CReservation &) = delete;
		CReservation &operator=(const CReservation &) = delete;
		~CReservation()
		{
			if(m_pPool != nullptr)
				for(size_t Index = 0; Index < Count; ++Index)
					m_pPool->Release(m_aIds[Index], m_aGenerations[Index]);
		}
		bool IsValid() const { return m_Valid; }
		int Id(size_t Index = 0) const { return m_aIds[Index]; }
		uint32_t Generation(size_t Index = 0) const { return m_aGenerations[Index]; }
		void Commit() { m_pPool = nullptr; }
	};

	template<size_t Count>
	CReservation<Count> Reserve()
	{
		return CReservation<Count>(*this);
	}
};

#endif
