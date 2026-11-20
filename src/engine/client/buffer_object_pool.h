#ifndef ENGINE_CLIENT_BUFFER_OBJECT_POOL_H
#define ENGINE_CLIENT_BUFFER_OBJECT_POOL_H

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

struct SBufferObjectSlot
{
	int m_NextFree = -2;
	// -2 表示正常身份，-3 表示本次删除正在提交，-1 表示待删除链尾。
	int m_NextPending = -2;
};

// 已接管创建命令的索引必须等删除命令也被接管后才能复用。
template<typename TAllocator = std::allocator<SBufferObjectSlot>>
class CBufferObjectPool
{
	std::vector<SBufferObjectSlot, TAllocator> m_vSlots;
	int m_FirstFree = -1;
	int m_FirstPending = -1;

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
			throw std::length_error("Buffer object index capacity exceeded");
		const int Index = static_cast<int>(m_vSlots.size());
		m_vSlots.emplace_back();
		return Index;
	}

	void Defer(int Index) noexcept
	{
		m_vSlots[Index].m_NextPending = m_FirstPending;
		m_FirstPending = Index;
	}

	class CCreation
	{
		CBufferObjectPool &m_Pool;
		int m_Index;
		bool m_Accepted = false;

	public:
		explicit CCreation(CBufferObjectPool &Pool) :
			m_Pool(Pool), m_Index(Pool.Allocate()) {}
		CCreation(const CCreation &) = delete;
		CCreation &operator=(const CCreation &) = delete;
		~CCreation()
		{
			if(m_Index < 0)
				return;
			if(m_Accepted)
				m_Pool.Defer(m_Index);
			else
				m_Pool.Release(m_Index);
		}
		int Index() const { return m_Index; }
		// 首条创建命令接管后立即调用，随后分块失败不能再当作未创建。
		void Accepted() noexcept { m_Accepted = true; }

	private:
		friend class CBufferObjectPool<TAllocator>;
		int Publish()
		{
			if(!m_Accepted)
				throw std::logic_error("Buffer creation was not accepted");
			return std::exchange(m_Index, -1);
		}
	};

public:
	explicit CBufferObjectPool(const TAllocator &Allocator = TAllocator()) :
		m_vSlots(Allocator) {}
	CBufferObjectPool(const CBufferObjectPool &) = delete;
	CBufferObjectPool &operator=(const CBufferObjectPool &) = delete;

	// 回调只登记本次上传；Accepted/发布和清理回调期间不能重置池。
	template<typename TBuild>
	int Create(TBuild &&Build)
	{
		CCreation Creation(*this);
		std::forward<TBuild>(Build)(Creation);
		return Creation.Publish();
	}
	bool IsLive(int Index) const noexcept
	{
		return Index >= 0 && static_cast<size_t>(Index) < m_vSlots.size() &&
		       m_vSlots[Index].m_NextFree == -2 && m_vSlots[Index].m_NextPending == -2;
	}
	void Release(int Index) noexcept
	{
		if(!IsLive(Index))
			return;
		m_vSlots[Index].m_NextFree = m_FirstFree;
		m_FirstFree = Index;
	}
	// 删除回调成功返回即代表接管完成，此后不能再执行可抛出的准备操作。
	template<typename TSubmitDelete>
	void FlushPending(TSubmitDelete &&SubmitDelete)
	{
		while(m_FirstPending != -1)
		{
			const int Index = m_FirstPending;
			m_FirstPending = m_vSlots[Index].m_NextPending;
			m_vSlots[Index].m_NextPending = -3;
			try
			{
				SubmitDelete(Index);
			}
			catch(...)
			{
				// 回调期间可能登记了其他失败创建，恢复时接到当前链首。
				Defer(Index);
				throw;
			}
			m_vSlots[Index].m_NextPending = -2;
			Release(Index);
		}
	}
	bool HasPending() const { return m_FirstPending != -1; }
	size_t Size() const { return m_vSlots.size(); }
	void Clear()
	{
		m_vSlots.clear();
		m_FirstFree = m_FirstPending = -1;
	}
};

#endif
