/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "memheap.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

// allocates a new chunk to be used
bool CHeap::NewChunk(size_t ChunkSize) noexcept
try
{
	if(!m_pfnAllocate || ChunkSize > std::numeric_limits<size_t>::max() - sizeof(CChunk) ||
		ChunkSize > static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()))
		return false;
	// the chunk structure is located in the beginning of the chunk
	// init it and return the chunk
	CChunk *pChunk = static_cast<CChunk *>(m_pfnAllocate(sizeof(CChunk) + ChunkSize));
	if(!pChunk)
		return false;
	pChunk->m_pMemory = static_cast<char *>(static_cast<void *>(pChunk + 1));
	pChunk->m_pCurrent = pChunk->m_pMemory;
	pChunk->m_pEnd = pChunk->m_pMemory + ChunkSize;
	pChunk->m_pNext = nullptr;

	pChunk->m_pNext = m_pCurrent;
	m_pCurrent = pChunk;
	return true;
}
catch(...)
{
	return false;
}

//****************
void *CHeap::AllocateFromChunk(size_t Size, size_t Alignment)
{
	if(!m_pCurrent)
		return nullptr;
	size_t Offset = reinterpret_cast<uintptr_t>(m_pCurrent->m_pCurrent) % Alignment;
	if(Offset)
		Offset = Alignment - Offset;

	// 先比较剩余字节，不能先形成可能超出块边界的指针。
	const size_t Remaining = static_cast<size_t>(m_pCurrent->m_pEnd - m_pCurrent->m_pCurrent);
	if(Offset > Remaining || Size > Remaining - Offset)
		return nullptr;

	// get memory and move the pointer forward
	char *pMem = m_pCurrent->m_pCurrent + Offset;
	m_pCurrent->m_pCurrent += Offset + Size;
	return pMem;
}

// creates a heap
CHeap::CHeap(FAllocate Allocate) noexcept :
	m_pCurrent(nullptr),
	m_pfnAllocate(Allocate)
{
	Reset();
}

CHeap::~CHeap()
{
	Clear();
}

void CHeap::Reset()
{
	Clear();
	NewChunk(CHUNK_SIZE);
}

void CHeap::Absorb(CHeap &Other) noexcept
{
	if(this == &Other || !Other.m_pCurrent)
		return;
	CChunk *pTail = Other.m_pCurrent;
	while(pTail->m_pNext)
		pTail = pTail->m_pNext;
	pTail->m_pNext = m_pCurrent;
	m_pCurrent = std::exchange(Other.m_pCurrent, nullptr);
}

void CHeap::SwapStorage(CHeap &Other) noexcept
{
	std::swap(m_pCurrent, Other.m_pCurrent);
}

// destroys the heap
void CHeap::Clear()
{
	while(m_pCurrent)
	{
		CChunk *pNext = m_pCurrent->m_pNext;
		free(m_pCurrent);
		m_pCurrent = pNext;
	}
}

//
void *CHeap::Allocate(size_t Size, size_t Alignment)
{
	if(Alignment == 0)
		return nullptr;
	// try to allocate from current chunk
	void *pMem = AllocateFromChunk(Size, Alignment);
	if(!pMem)
	{
		// allocate new chunk and add it to the heap
		if(Size > std::numeric_limits<size_t>::max() - (Alignment - 1) ||
			!NewChunk(std::max(CHUNK_SIZE, Size + Alignment - 1)))
			return nullptr;

		// try to allocate again
		pMem = AllocateFromChunk(Size, Alignment);
	}

	return pMem;
}

const char *CHeap::StoreString(const char *pSrc)
{
	const size_t Length = std::strlen(pSrc);
	if(Length == std::numeric_limits<size_t>::max())
		throw std::bad_alloc();
	char *pMem = static_cast<char *>(Allocate(Length + 1));
	if(!pMem)
		throw std::bad_alloc();
	std::memcpy(pMem, pSrc, Length + 1);
	return pMem;
}
