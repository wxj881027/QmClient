#ifndef ENGINE_CLIENT_GRAPHICS_READBACK_COMPLETION_H
#define ENGINE_CLIENT_GRAPHICS_READBACK_COMPLETION_H

#include <base/sphore.h>

#include <engine/graphics.h>

#include <utility>

// 完成信号的单一持有者；借用图像和信号量直到完成，不接管图像的最终所有权。
class CGraphicsReadbackCompletion
{
	CImageInfo *m_pImage = nullptr;
	CSemaphore *m_pCompletion = nullptr;

public:
	CGraphicsReadbackCompletion() = default;
	CGraphicsReadbackCompletion(CImageInfo *pImage, CSemaphore *pCompletion) noexcept :
		m_pImage(pImage), m_pCompletion(pCompletion) {}
	CGraphicsReadbackCompletion(const CGraphicsReadbackCompletion &) = delete;
	CGraphicsReadbackCompletion &operator=(const CGraphicsReadbackCompletion &) = delete;
	CGraphicsReadbackCompletion(CGraphicsReadbackCompletion &&Other) noexcept :
		m_pImage(std::exchange(Other.m_pImage, nullptr)),
		m_pCompletion(std::exchange(Other.m_pCompletion, nullptr)) {}
	CGraphicsReadbackCompletion &operator=(CGraphicsReadbackCompletion &&Other) noexcept
	{
		if(this != &Other)
		{
			Abort();
			m_pImage = std::exchange(Other.m_pImage, nullptr);
			m_pCompletion = std::exchange(Other.m_pCompletion, nullptr);
		}
		return *this;
	}
	~CGraphicsReadbackCompletion() { Complete(); }

	explicit operator bool() const { return m_pImage != nullptr || m_pCompletion != nullptr; }
	CImageInfo *Image() const { return m_pImage; }

	void Complete() noexcept
	{
		m_pImage = nullptr;
		if(auto *pCompletion = std::exchange(m_pCompletion, nullptr))
			pCompletion->Signal();
	}
	// 调用前必须结束所有 CPU 写入；取消不能发布未完成的像素。
	void Abort() noexcept
	{
		if(m_pImage != nullptr)
			m_pImage->Free();
		Complete();
	}
};

#endif
