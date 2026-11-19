#ifndef BASE_AIO_BUFFER_H
#define BASE_AIO_BUFFER_H

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

// AIO 锁内使用；分配失败时保留原队列，调用方负责记录错误。
class CAsyncIOBuffer
{
	struct CDeleter
	{
		void operator()(unsigned char *pBuffer) const { std::free(pBuffer); }
	};
	std::unique_ptr<unsigned char, CDeleter> m_pBuffer;
	unsigned m_Capacity;
	unsigned m_ReadPos = 0;
	unsigned m_WritePos = 0;

	unsigned Advance(unsigned Pos, unsigned Size) const
	{
		const unsigned Remaining = m_Capacity - Pos;
		return Size >= Remaining ? Size - Remaining : Pos + Size;
	}

public:
	using FAllocate = void *(*)(size_t);

	// 注入分配器仍须返回可由 free 释放的完整缓冲区，或返回空指针。
	explicit CAsyncIOBuffer(unsigned Capacity = 8 * 1024, FAllocate Allocate = std::malloc) :
		m_pBuffer(Capacity >= 2 ? static_cast<unsigned char *>(Allocate(Capacity)) : nullptr),
		m_Capacity(Capacity)
	{
	}

	bool IsValid() const { return m_pBuffer != nullptr; }
	bool Empty() const { return m_ReadPos == m_WritePos; }
	unsigned Size() const
	{
		return m_WritePos >= m_ReadPos ? m_WritePos - m_ReadPos : m_Capacity - m_ReadPos + m_WritePos;
	}

	bool Append(const void *pData, unsigned Size, FAllocate Allocate = std::malloc)
	{
		const unsigned Used = this->Size();
		// 始终保留一个空位，且在加法、扩容或读取输入前拒绝不可表示的长度。
		if(!IsValid() || Size > std::numeric_limits<unsigned>::max() - Used - 1)
			return false;
		if(Size == 0)
			return true;
		if(Size >= m_Capacity - Used)
		{
			const unsigned Required = Used + Size + 1;
			unsigned Capacity = m_Capacity;
			while(Capacity < Required)
			{
				Capacity = Capacity > std::numeric_limits<unsigned>::max() / 2 ? Required : Capacity * 2;
			}
			std::unique_ptr<unsigned char, CDeleter> pBuffer(static_cast<unsigned char *>(Allocate(Capacity)));
			if(!pBuffer)
				return false;
			const unsigned First = std::min(Used, m_Capacity - m_ReadPos);
			if(First != 0)
				std::memcpy(pBuffer.get(), m_pBuffer.get() + m_ReadPos, First);
			if(Used != First)
				std::memcpy(pBuffer.get() + First, m_pBuffer.get(), Used - First);
			std::memcpy(pBuffer.get() + Used, pData, Size);
			m_pBuffer.swap(pBuffer);
			m_Capacity = Capacity;
			m_ReadPos = 0;
			m_WritePos = Used + Size;
			return true;
		}
		const unsigned First = std::min(Size, m_Capacity - m_WritePos);
		std::memcpy(m_pBuffer.get() + m_WritePos, pData, First);
		if(Size != First)
			std::memcpy(m_pBuffer.get(), static_cast<const unsigned char *>(pData) + First, Size - First);
		m_WritePos = Advance(m_WritePos, Size);
		return true;
	}

	unsigned Read(void *pOutput, unsigned MaxSize)
	{
		const unsigned Size = std::min(MaxSize, this->Size());
		if(Size == 0)
			return 0;
		const unsigned First = std::min(Size, m_Capacity - m_ReadPos);
		std::memcpy(pOutput, m_pBuffer.get() + m_ReadPos, First);
		if(Size != First)
			std::memcpy(static_cast<unsigned char *>(pOutput) + First, m_pBuffer.get(), Size - First);
		m_ReadPos = Advance(m_ReadPos, Size);
		return Size;
	}
};

#endif
