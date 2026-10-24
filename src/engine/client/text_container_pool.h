#ifndef ENGINE_CLIENT_TEXT_CONTAINER_POOL_H
#define ENGINE_CLIENT_TEXT_CONTAINER_POOL_H

#include <base/dbg.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

template<typename TContainer>
struct STextContainerSlot
{
	std::unique_ptr<TContainer> m_pContainer;
	int m_NextFree = -2;

	explicit STextContainerSlot(std::unique_ptr<TContainer> pContainer) :
		m_pContainer(std::move(pContainer)) {}
};

// 索引和 CPU 容器一起登记；失败不领取槽位，对象在数组扩容失败时自动回收。
template<typename TContainer, typename TAllocator = std::allocator<STextContainerSlot<TContainer>>>
class CTextContainerPool
{
	std::vector<STextContainerSlot<TContainer>, TAllocator> m_vSlots;
	int m_FirstFree = -1;

public:
	explicit CTextContainerPool(const TAllocator &Allocator = TAllocator()) :
		m_vSlots(Allocator) {}
	CTextContainerPool(const CTextContainerPool &) = delete;
	CTextContainerPool &operator=(const CTextContainerPool &) = delete;

	int Allocate()
	{
		if(m_FirstFree != -1)
		{
			const int Index = m_FirstFree;
			auto &Slot = m_vSlots[Index];
			m_FirstFree = Slot.m_NextFree;
			Slot.m_NextFree = -2;
			return Index;
		}
		if(m_vSlots.size() >= static_cast<size_t>(std::numeric_limits<int>::max()) || m_vSlots.size() >= m_vSlots.max_size())
			return -1;
		try
		{
			auto pContainer = std::make_unique<TContainer>();
			const int Index = static_cast<int>(m_vSlots.size());
			m_vSlots.emplace_back(std::move(pContainer));
			return Index;
		}
		catch(const std::bad_alloc &)
		{
			return -1;
		}
		catch(const std::length_error &)
		{
			return -1;
		}
	}

	bool IsAllocated(int Index) const
	{
		return Index >= 0 && static_cast<size_t>(Index) < m_vSlots.size() && m_vSlots[Index].m_NextFree == -2;
	}

	// GPU 资源释放、句柄失效和容器 Reset 仍由文字渲染器按原次序完成。
	void Release(int Index)
	{
		if(!IsAllocated(Index))
			return;
		m_vSlots[Index].m_NextFree = m_FirstFree;
		m_FirstFree = Index;
	}

	TContainer &Get(size_t Index) const
	{
		dbg_assert(Index < m_vSlots.size(), "Text container slot was not prepared.");
		return *m_vSlots[Index].m_pContainer;
	}
	size_t Size() const { return m_vSlots.size(); }
	void Clear()
	{
		m_vSlots.clear();
		m_FirstFree = -1;
	}
};

#endif
