#ifndef TEST_SUPPORT_IMAGE_WRITER_TEST_RESOURCE_H
#define TEST_SUPPORT_IMAGE_WRITER_TEST_RESOURCE_H

#include <engine/gfx/image_loader.h>

#include <functional>
#include <new>

// 仅替换实际输出vector的分配边界，不替换编码器或编码回调。
class CImageWriterTestResource
{
	static void *Allocate(size_t Size, size_t Alignment, void *pUser)
	{
		auto &Self = *static_cast<CImageWriterTestResource *>(pUser);
		++Self.m_Calls;
		if(Self.m_Reject && (!Self.m_When || Self.m_When()))
		{
			++Self.m_Rejections;
			if(Self.m_ReturnNull)
				return nullptr;
			if(Self.m_Unknown)
				throw 7;
			throw std::bad_alloc();
		}
		void *pData = Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__ ? ::operator new(Size, std::align_val_t(Alignment)) : ::operator new(Size);
		++Self.m_Live;
		return pData;
	}
	static void Deallocate(void *pData, size_t, size_t Alignment, void *pUser) noexcept
	{
		auto &Self = *static_cast<CImageWriterTestResource *>(pUser);
		--Self.m_Live;
		if(Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
			::operator delete(pData, std::align_val_t(Alignment));
		else
			::operator delete(pData);
	}

public:
	CImageWriterTestResource() = default;
	CImageWriterTestResource(const CImageWriterTestResource &) = delete;
	CImageWriterTestResource &operator=(const CImageWriterTestResource &) = delete;
	const CByteBufferWriter::SAllocation m_Allocation{Allocate, Deallocate, this};
	int m_Calls = 0;
	int m_Live = 0;
	int m_Rejections = 0;
	bool m_Reject = false;
	bool m_Unknown = false;
	bool m_ReturnNull = false;
	std::function<bool()> m_When;
};

#endif
