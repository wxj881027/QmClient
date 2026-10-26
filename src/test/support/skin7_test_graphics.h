#ifndef TEST_SUPPORT_SKIN7_TEST_GRAPHICS_H
#define TEST_SUPPORT_SKIN7_TEST_GRAPHICS_H

#include <test/support/icon_benchmark_graphics.h>

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

// 只替换纹理设备边界，实际发布器、部件容器和上传限额不替换。
class CSkin7TestGraphics : public CIconBenchmarkGraphics
{
public:
	std::array<bool, 64> m_aLive{};
	int m_Attempts = 0;
	int m_FailAt = 0;
	int m_ThrowAt = 0;
	int m_StaleAt = 0;
	int m_NullAt = 0;
	bool m_FailUnload = false;

	CTextureHandle LoadTextureRaw(const CImageInfo &Image, int Flags, const char *pName) override
	{
		++m_Attempts;
		if(m_Attempts == m_ThrowAt)
			throw std::bad_alloc();
		if(m_Attempts == m_FailAt)
			return {};
		if(m_Attempts == m_NullAt)
			return CreateTextureHandle(0);
		auto Handle = CIconBenchmarkGraphics::LoadTextureRaw(Image, Flags, pName);
		m_aLive.at(Handle.Id()) = m_Attempts != m_StaleAt;
		return Handle;
	}

	void UnloadTexture(CTextureHandle *pHandle) override
	{
		if(m_FailUnload)
			throw std::runtime_error("device queue unavailable");
		m_aLive.at(pHandle->Id()) = false;
		pHandle->Invalidate();
	}

	bool IsTextureHandleAllocated(CTextureHandle Handle) const override
	{
		return Handle.IsValid() && Handle.Id() < static_cast<int>(m_aLive.size()) && m_aLive[Handle.Id()];
	}

	int LiveCount() const
	{
		return std::count(m_aLive.begin(), m_aLive.end(), true);
	}
};

#endif
