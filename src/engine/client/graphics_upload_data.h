#ifndef ENGINE_CLIENT_GRAPHICS_UPLOAD_DATA_H
#define ENGINE_CLIENT_GRAPHICS_UPLOAD_DATA_H

#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

static_assert(std::is_trivially_copyable_v<SGraphicsUploadRelease>);

inline void ReleaseGraphicsUpload(SGraphicsUploadRelease &Release) noexcept
{
	const auto Owned = std::exchange(Release, {});
	if(Owned.m_pfnRelease != nullptr)
		Owned.m_pfnRelease(Owned.m_pContext);
}

// 先构造额度 guard，再接管像素；消费者的像素 owner 须先于本 guard 析构。
class CGraphicsUploadReleaseGuard
{
	SGraphicsUploadRelease m_Release;

public:
	explicit CGraphicsUploadReleaseGuard(SGraphicsUploadRelease Release) noexcept :
		m_Release(Release) {}
	CGraphicsUploadReleaseGuard(const CGraphicsUploadReleaseGuard &) = delete;
	CGraphicsUploadReleaseGuard &operator=(const CGraphicsUploadReleaseGuard &) = delete;
	CGraphicsUploadReleaseGuard(CGraphicsUploadReleaseGuard &&Other) noexcept :
		m_Release(std::exchange(Other.m_Release, {})) {}
	CGraphicsUploadReleaseGuard &operator=(CGraphicsUploadReleaseGuard &&) = delete;
	~CGraphicsUploadReleaseGuard() { ReleaseGraphicsUpload(m_Release); }
};

inline CGraphicsUploadReleaseGuard TakeGraphicsUploadRelease(SGraphicsUploadRelease &Release) noexcept
{
	return CGraphicsUploadReleaseGuard(std::exchange(Release, {}));
}

struct CGraphicsUploadDeleter
{
	void operator()(void *pData) const noexcept { std::free(pData); }
};

template<typename T>
using CGraphicsUploadData = std::unique_ptr<T, CGraphicsUploadDeleter>;

// 命令区只保存可平凡复制的原始指针；消费时清空来源，并在处理器栈上持有资源。
// 借用的命令区数据不转移，也不由处理器释放。
template<typename T, typename TRelease = CGraphicsUploadDeleter>
std::unique_ptr<T, TRelease> TakeGraphicsUpload(T *&pData, bool Owned = true, TRelease Release = {}) noexcept
{
	static_assert(std::is_nothrow_move_constructible_v<TRelease>);
	static_assert(std::is_nothrow_invocable_v<TRelease &, T *>);
	return std::unique_ptr<T, TRelease>(Owned ? std::exchange(pData, nullptr) : nullptr, std::move(Release));
}

// Append 只把借用指针和 token 复制到尚未提交的当前缓冲：false/异常不能接管，
// true 后不能同步消费。提交后的像素与额度转移不再分配；旧缓冲的 kick 仍可抛异常。
template<typename TSlots, typename TAppend>
bool TryQueueGraphicsTextureUploadOwned(CImageInfo &Image, SGraphicsUploadRelease &Release, TSlots &Slots, TAppend &&Append,
	int &TextureId, uint32_t &TextureGeneration, bool (*pConvert)(uint8_t *&, const CImageInfo &) = ConvertToRgbaAlloc)
{
	TextureId = -1;
	TextureGeneration = 0;
	CImageInfo Rgba;
	Rgba.m_Width = Image.m_Width;
	Rgba.m_Height = Image.m_Height;
	Rgba.m_Format = CImageInfo::FORMAT_RGBA;
	size_t RgbaBytes = 0;
	const bool SupportedFormat = Image.m_Format == CImageInfo::FORMAT_RGBA || Image.m_Format == CImageInfo::FORMAT_RGB ||
				     Image.m_Format == CImageInfo::FORMAT_R || Image.m_Format == CImageInfo::FORMAT_RA;
	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0 ||
		Image.m_Width > static_cast<size_t>(std::numeric_limits<int>::max()) || Image.m_Height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		!SupportedFormat || !Rgba.DataSize(RgbaBytes) || RgbaBytes == 0)
	{
		Image.Free();
		ReleaseGraphicsUpload(Release);
		return true;
	}
	auto Reservation = Slots.template Reserve<1>();
	if(!Reservation.IsValid())
		return false;
	struct SFreeImage
	{
		CImageInfo &m_Image;
		~SFreeImage() { m_Image.Free(); }
	} FreeRgba{Rgba};
	const bool Converted = Image.m_Format != CImageInfo::FORMAT_RGBA;
	if(Converted)
	{
		(void)pConvert(Rgba.m_pData, Image);
		if(Rgba.m_pData == nullptr)
			return false;
	}
	uint8_t *pData = Converted ? Rgba.m_pData : Image.m_pData;
	if(!Append(Reservation.Id(), Image.m_Width, Image.m_Height, pData, Release))
		return false;
	if(Converted)
		Rgba.m_pData = nullptr;
	else
		Image.m_pData = nullptr;
	// RGB 原图必须先于当前命令可能执行的下一次 kick 释放；token 继续归命令所有。
	Image.Free();
	Release = {};
	TextureId = Reservation.Id();
	TextureGeneration = Reservation.Generation();
	Reservation.Commit();
	return true;
}

#endif
