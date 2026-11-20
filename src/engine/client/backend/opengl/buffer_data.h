#ifndef ENGINE_CLIENT_BACKEND_OPENGL_BUFFER_DATA_H
#define ENGINE_CLIENT_BACKEND_OPENGL_BUFFER_DATA_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

// 兼容后端保留的 CPU 数据副本；候选分配失败时保留现有内容。
class COpenGLBufferData
{
	struct CFree
	{
		void operator()(uint8_t *pData) const noexcept { std::free(pData); }
	};
	std::unique_ptr<uint8_t, CFree> m_pData;
	size_t m_Size = 0;

public:
	COpenGLBufferData() = default;
	COpenGLBufferData(const COpenGLBufferData &) = delete;
	COpenGLBufferData &operator=(const COpenGLBufferData &) = delete;
	COpenGLBufferData(COpenGLBufferData &&Other) noexcept :
		m_pData(std::move(Other.m_pData)), m_Size(std::exchange(Other.m_Size, 0)) {}
	COpenGLBufferData &operator=(COpenGLBufferData &&Other) noexcept
	{
		if(this != &Other)
		{
			m_pData = std::move(Other.m_pData);
			m_Size = std::exchange(Other.m_Size, 0);
		}
		return *this;
	}

	// 自定义分配边界返回 nullptr 或可由 free 释放的内存。
	template<typename TAllocate>
	bool Assign(size_t Size, const void *pSource, TAllocate &&Allocate) noexcept
	{
		if(Size > static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()))
			return false;
		try
		{
			std::unique_ptr<uint8_t, CFree> pData(Size ? static_cast<uint8_t *>(Allocate(Size)) : nullptr);
			if(Size && !pData)
				return false;
			if(Size && pSource)
				std::memcpy(pData.get(), pSource, Size);
			m_pData = std::move(pData);
			m_Size = Size;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool Assign(size_t Size, const void *pSource) noexcept
	{
		return Assign(Size, pSource, std::malloc);
	}
	void Reset() noexcept
	{
		m_pData.reset();
		m_Size = 0;
	}

	uint8_t *Data() { return m_pData.get(); }
	const uint8_t *Data() const { return m_pData.get(); }
	size_t Size() const { return m_Size; }
	bool Contains(size_t Offset, size_t Size) const
	{
		return Offset <= m_Size && Size <= m_Size - Offset && (Size == 0 || m_pData != nullptr);
	}
	bool Write(size_t Offset, size_t Size, const void *pSource) noexcept
	{
		if(!Contains(Offset, Size) || (Size != 0 && pSource == nullptr))
			return false;
		if(Size)
			std::memmove(m_pData.get() + Offset, pSource, Size);
		return true;
	}
	bool CopyFrom(const COpenGLBufferData &Source, size_t ReadOffset, size_t WriteOffset, size_t Size) noexcept
	{
		if(!Source.Contains(ReadOffset, Size) || !Contains(WriteOffset, Size))
			return false;
		if(Size)
			std::memmove(m_pData.get() + WriteOffset, Source.Data() + ReadOffset, Size);
		return true;
	}
};

#endif
