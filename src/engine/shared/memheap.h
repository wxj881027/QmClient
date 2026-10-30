/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_MEMHEAP_H
#define ENGINE_SHARED_MEMHEAP_H

#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

class CHeap
{
public:
	// 分配器返回的内存必须可由free释放；借用函数，不接管外部状态。
	using FAllocate = void *(*)(size_t);

private:
	struct CChunk
	{
		char *m_pMemory;
		char *m_pCurrent;
		char *m_pEnd;
		CChunk *m_pNext;
	};

	/**
	 * How large each chunk should be.
	 */
	static constexpr size_t CHUNK_SIZE = 1025 * 64;

	CChunk *m_pCurrent;
	FAllocate m_pfnAllocate;

	void Clear();
	bool NewChunk(size_t ChunkSize) noexcept;
	void *AllocateFromChunk(size_t Size, size_t Alignment);

public:
	explicit CHeap(FAllocate Allocate = std::malloc) noexcept;
	~CHeap();
	void Reset();
	void *Allocate(size_t Size, size_t Alignment = alignof(std::max_align_t));
	const char *StoreString(const char *pSrc);

	template<typename T, typename... TArgs>
	T *Allocate(TArgs &&...Args)
	{
		return new(Allocate(sizeof(T), alignof(T))) T(std::forward<TArgs>(Args)...);
	}
};

#endif
