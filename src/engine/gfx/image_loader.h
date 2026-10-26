#ifndef ENGINE_GFX_IMAGE_LOADER_H
#define ENGINE_GFX_IMAGE_LOADER_H

#include <base/types.h>

#include <engine/image.h>

#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

class CByteBufferReader
{
	const uint8_t *m_pData;
	size_t m_Size;
	size_t m_ReadOffset = 0;
	bool m_Error = false;

public:
	CByteBufferReader(const uint8_t *pData, size_t Size) :
		m_pData(pData),
		m_Size(Size) {}

	bool Read(void *pData, size_t Size);
	bool Error() const { return m_Error; }
	size_t Size() const { return m_Size; }
};

class CByteBufferWriter
{
public:
	struct SAllocation
	{
		void *(*m_pfnAllocate)(size_t Size, size_t Alignment, void *pUser);
		void (*m_pfnDeallocate)(void *pData, size_t Size, size_t Alignment, void *pUser) noexcept;
		void *m_pUser;
	};

private:
	friend class CImageLoader;
	// 使用普通标准分配器，避免给旧系统增加PMR运行库依赖。
	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		const SAllocation *m_pAllocation = nullptr;
		CAllocator() = default;
		explicit CAllocator(const SAllocation *pAllocation) noexcept :
			m_pAllocation(pAllocation) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) noexcept :
			m_pAllocation(Other.m_pAllocation)
		{
		}
		template<typename U>
		struct rebind
		{
			using other = CAllocator<U>;
		};
		T *allocate(size_t Count)
		{
			if(Count > std::numeric_limits<size_t>::max() / sizeof(T))
				throw std::bad_array_new_length();
			if(!m_pAllocation)
				return std::allocator<T>{}.allocate(Count);
			auto *pData = m_pAllocation->m_pfnAllocate(Count * sizeof(T), alignof(T), m_pAllocation->m_pUser);
			if(!pData)
				throw std::bad_alloc();
			return static_cast<T *>(pData);
		}
		void deallocate(T *pData, size_t Count) noexcept
		{
			if(m_pAllocation)
				m_pAllocation->m_pfnDeallocate(pData, Count * sizeof(T), alignof(T), m_pAllocation->m_pUser);
			else
				std::allocator<T>{}.deallocate(pData, Count);
		}
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const noexcept
		{
			return m_pAllocation == Other.m_pAllocation;
		}
	};
	std::vector<uint8_t, CAllocator<uint8_t>> m_vBuffer;

	// 只回退本次编码；成功标志在最后一个可longjmp的调用结束后才修改。
	class CWriteScope
	{
		CByteBufferWriter &m_Writer;
		const size_t m_PreviousSize;
		bool m_Committed = false;

	public:
		explicit CWriteScope(CByteBufferWriter &Writer) :
			m_Writer(Writer), m_PreviousSize(Writer.m_vBuffer.size()) {}
		~CWriteScope()
		{
			if(!m_Committed)
				m_Writer.m_vBuffer.resize(m_PreviousSize);
		}
		CWriteScope(const CWriteScope &) = delete;
		CWriteScope &operator=(const CWriteScope &) = delete;
		void Commit() noexcept { m_Committed = true; }
	};

public:
	// 分配表及上下文须比writer活得久，分配/释放配对且释放不抛异常。
	explicit CByteBufferWriter(const SAllocation *pAllocation = nullptr) :
		m_vBuffer(CAllocator<uint8_t>(pAllocation)) {}
	// 输入须为独立有效字节区；失败保留原字节，不让分配异常跨过C回调。
	bool Write(const void *pData, size_t Size) noexcept;
	const uint8_t *Data() const { return m_vBuffer.data(); }
	size_t Size() const { return m_vBuffer.size(); }
};

class CImageLoader
{
public:
	CImageLoader() = delete;

	enum
	{
		PNGLITE_COLOR_TYPE = 1 << 0,
		PNGLITE_BIT_DEPTH = 1 << 1,
		PNGLITE_INTERLACE_TYPE = 1 << 2,
		PNGLITE_COMPRESSION_TYPE = 1 << 3,
		PNGLITE_FILTER_TYPE = 1 << 4,
	};

	static bool LoadPng(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, int &PngliteIncompatible, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());
	static bool LoadPng(IOHANDLE File, const char *pFilename, CImageInfo &Image, int &PngliteIncompatible);
	static bool LoadPng(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());

	static bool LoadWebP(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());
	static bool LoadWebP(IOHANDLE File, const char *pFilename, CImageInfo &Image);
	static bool LoadWebP(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes = std::numeric_limits<size_t>::max());

	// Comment 使用 UTF-8 iTXt 写在像素前；读取只扫描 PNG 头，不解码图片。
	static bool ReadPngComment(IOHANDLE File, const char *pFilename, std::string &Comment);
	static bool SavePng(CByteBufferWriter &Writer, const CImageInfo &Image, const char *pComment = nullptr);
	static bool SavePng(IOHANDLE File, const char *pFilename, const CImageInfo &Image, const char *pComment = nullptr);

	static bool SaveWebP(CByteBufferWriter &Writer, const CImageInfo &Image);
	static bool SaveWebP(IOHANDLE File, const char *pFilename, const CImageInfo &Image);
};

#endif // ENGINE_GFX_IMAGE_LOADER_H
