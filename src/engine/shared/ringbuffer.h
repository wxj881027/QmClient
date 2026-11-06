/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_RINGBUFFER_H
#define ENGINE_SHARED_RINGBUFFER_H

#include <base/system.h>

#include <functional>

class CRingBufferBase
{
	class CItem
	{
	public:
		CItem *m_pPrev;
		CItem *m_pNext;
		int m_Free;
		int m_Size;
	};

	CItem *m_pBuffer = nullptr;

	CItem *m_pProduce = nullptr;
	CItem *m_pConsume = nullptr;

	CItem *m_pFirst = nullptr;
	CItem *m_pLast = nullptr;

	int m_Size = 0;
	int m_Flags = 0;

	std::function<void(void *pCurrent)> m_PopCallback = nullptr;

	CItem *NextBlock(CItem *pItem);
	CItem *PrevBlock(CItem *pItem);
	CItem *MergeBack(CItem *pItem);

protected:
	void *Allocate(int Size);

	void *Prev(void *pCurrent);
	void *Next(void *pCurrent);
	void *First();
	void *Last();

	void Init(void *pMemory, int Size, int Flags);
	int PopFirst();
	void SetPopCallback(std::function<void(void *pCurrent)> PopCallback);

public:
	enum
	{
		// Will start to destroy items to try to fit the next one
		FLAG_RECYCLE = 1
	};
	static constexpr int ITEM_SIZE = sizeof(CItem);

	void Clear();
};

template<typename T>
class CTypedRingBuffer : public CRingBufferBase
{
public:
	// 保留与 CRingBufferBase 一致的调用名，但返回类型收窄为 T* 作为类型安全包装。
	// NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
	T *Allocate(int Size) { return (T *)CRingBufferBase::Allocate(Size); }
	// NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
	int PopFirst() { return CRingBufferBase::PopFirst(); }
	void SetPopCallback(const std::function<void(T *pCurrent)> &PopCallback)
	{
		CRingBufferBase::SetPopCallback([PopCallback](void *pCurrent) {
			PopCallback((T *)pCurrent);
		});
	}

	T *Prev(T *pCurrent) { return (T *)CRingBufferBase::Prev(pCurrent); }
	T *Next(T *pCurrent) { return (T *)CRingBufferBase::Next(pCurrent); }
	// NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
	T *First() { return (T *)CRingBufferBase::First(); }
	// NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
	T *Last() { return (T *)CRingBufferBase::Last(); }
};

template<typename T, int TSIZE, int TFLAGS = 0>
class CStaticRingBuffer : public CTypedRingBuffer<T>
{
	unsigned char m_aBuffer[TSIZE];

public:
	CStaticRingBuffer() { Init(); }

	void Init() { CRingBufferBase::Init(m_aBuffer, TSIZE, TFLAGS); }
};

template<typename T>
class CDynamicRingBuffer : public CTypedRingBuffer<T>
{
	unsigned char *m_pBuffer = nullptr;

public:
	using FAllocate = void *(*)(size_t);

	CDynamicRingBuffer(int Size, int Flags = 0, FAllocate pAllocate = malloc) { Init(Size, Flags, pAllocate); }

	~CDynamicRingBuffer()
	{
		free(m_pBuffer);
	}

	bool IsInitialized() const { return m_pBuffer != nullptr; }

	// 分配器须返回可由 free 释放的内存，失败返回空指针；失败不改动原记录。
	bool Init(int Size, int Flags, FAllocate pAllocate = malloc)
	{
		if(Size < CRingBufferBase::ITEM_SIZE || pAllocate == nullptr)
			return false;
		auto *pNewBuffer = static_cast<unsigned char *>(pAllocate(static_cast<size_t>(Size)));
		if(pNewBuffer == nullptr)
			return false;
		free(m_pBuffer);
		m_pBuffer = pNewBuffer;
		CRingBufferBase::Init(m_pBuffer, Size, Flags);
		return true;
	}
};

#endif
