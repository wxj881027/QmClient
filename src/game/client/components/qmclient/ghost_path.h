#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_PATH_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_PATH_H

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace qm_ghost_path
{
	// 外部堆边界；成功分配须清零并满足类型对齐，Free不得抛异常。
	class CAllocation
	{
	public:
		virtual ~CAllocation() = default;
		virtual void *Allocate(size_t Count, size_t Size) { return std::calloc(Count, Size); }
		virtual void Free(void *pMemory) noexcept { std::free(pMemory); }
	};

	template<typename T>
	class CChunkedPath
	{
		// 主线程串行使用，读取与堆回调不得重入本对象。
		static_assert(std::is_trivial_v<T> && std::is_nothrow_copy_assignable_v<T>);
		static_assert(alignof(T) <= alignof(std::max_align_t));
		struct CFree
		{
			CAllocation *m_pAllocation;
			CFree() noexcept :
				m_pAllocation(nullptr) {}
			explicit CFree(CAllocation *pAllocation) noexcept :
				m_pAllocation(pAllocation) {}
			void operator()(T *pMemory) const noexcept
			{
				if(m_pAllocation)
					m_pAllocation->Free(pMemory);
				else
					std::free(pMemory);
			}
		};
		using CChunk = std::unique_ptr<T, CFree>;
		int m_ChunkSize;
		int m_NumItems = 0;
		std::vector<CChunk> m_vChunks;
		CAllocation *m_pAllocation;

	public:
		static constexpr int DEFAULT_CHUNK_SIZE = 25 * 60;
		// 覆盖的堆边界借用到所有分块释放为止；移动会一并转移该借用。
		explicit CChunkedPath(int ChunkSize = DEFAULT_CHUNK_SIZE, CAllocation *pAllocation = nullptr) noexcept :
			m_ChunkSize(ChunkSize), m_pAllocation(pAllocation) {}
		CChunkedPath(const CChunkedPath &) = delete;
		CChunkedPath &operator=(const CChunkedPath &) = delete;
		CChunkedPath(CChunkedPath &&Other) noexcept :
			m_ChunkSize(Other.m_ChunkSize), m_NumItems(std::exchange(Other.m_NumItems, 0)), m_vChunks(std::move(Other.m_vChunks)), m_pAllocation(Other.m_pAllocation)
		{
			Other.m_vChunks.clear();
		}
		CChunkedPath &operator=(CChunkedPath &&Other) noexcept
		{
			if(this != &Other)
			{
				m_vChunks = std::move(Other.m_vChunks);
				m_ChunkSize = Other.m_ChunkSize;
				m_NumItems = std::exchange(Other.m_NumItems, 0);
				m_pAllocation = Other.m_pAllocation;
				Other.m_vChunks.clear();
			}
			return *this;
		}
		void Reset(int ChunkSize = DEFAULT_CHUNK_SIZE) noexcept
		{
			m_vChunks.clear();
			m_ChunkSize = ChunkSize;
			m_NumItems = 0;
		}
		int Size() const noexcept { return m_NumItems; }
		bool SetSize(int Items) noexcept
		{
			if(Items < 0 || m_ChunkSize <= 0 || static_cast<size_t>(m_ChunkSize) > std::numeric_limits<size_t>::max() / sizeof(T))
				return false;
			const size_t Count = static_cast<size_t>(Items);
			const size_t ChunkSize = static_cast<size_t>(m_ChunkSize);
			const size_t Needed = Count / ChunkSize + (Count % ChunkSize != 0);
			const size_t Previous = m_vChunks.size();
			try
			{
				if(Needed > Previous)
				{
					m_vChunks.resize(Needed);
					for(size_t i = Previous; i < Needed; ++i)
					{
						T *pMemory = static_cast<T *>(m_pAllocation ? m_pAllocation->Allocate(ChunkSize, sizeof(T)) : std::calloc(ChunkSize, sizeof(T)));
						if(!pMemory)
						{
							while(m_vChunks.size() > Previous)
								m_vChunks.pop_back();
							return false;
						}
						m_vChunks[i] = CChunk(pMemory, CFree{m_pAllocation});
					}
				}
			}
			catch(...)
			{
				while(m_vChunks.size() > Previous)
					m_vChunks.pop_back();
				return false;
			}
			m_NumItems = Items;
			return true;
		}
		bool Add(const T &Value) noexcept
		{
			if(m_NumItems == std::numeric_limits<int>::max() || !SetSize(m_NumItems + 1))
				return false;
			*Get(m_NumItems - 1) = Value;
			return true;
		}
		template<typename FRead>
		bool ReadAndAdd(FRead &&Read) noexcept
		{
			try
			{
				T Value{};
				return std::forward<FRead>(Read)(Value) && Add(Value);
			}
			catch(...)
			{
				return false;
			}
		}
		const T *Get(int Index) const noexcept
		{
			if(Index < 0 || Index >= m_NumItems)
				return nullptr;
			return m_vChunks[Index / m_ChunkSize].get() + Index % m_ChunkSize;
		}
		T *Get(int Index) noexcept { return const_cast<T *>(std::as_const(*this).Get(Index)); }
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_PATH_H
