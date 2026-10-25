#include "screenshot_clipboard.h"

#include <base/log.h>
#include <base/mem.h>

#include <cstdint>
#include <limits>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

namespace qm_screenshot_clipboard
{

	namespace
	{
		constexpr size_t DIB_HEADER_SIZE = 40;

		void WriteUint32(uint8_t *pTarget, uint32_t Value)
		{
			for(size_t Index = 0; Index < 4; ++Index)
				pTarget[Index] = (uint8_t)(Value >> (Index * 8));
		}
	}

	size_t DibSize(const CImageInfo &Image)
	{
		if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0 ||
			(Image.m_Format != CImageInfo::FORMAT_RGB && Image.m_Format != CImageInfo::FORMAT_RGBA) ||
			Image.m_Width > (size_t)std::numeric_limits<int32_t>::max() || Image.m_Height > (size_t)std::numeric_limits<int32_t>::max())
			return 0;
		size_t SourceSize;
		if(!Image.DataSize(SourceSize) || Image.m_Width > (std::numeric_limits<size_t>::max() - 3) / 3)
			return 0;
		const size_t Stride = (Image.m_Width * 3 + 3) & ~size_t(3);
		if(Image.m_Height > std::numeric_limits<uint32_t>::max() / Stride ||
			Image.m_Height > (std::numeric_limits<size_t>::max() - DIB_HEADER_SIZE) / Stride)
			return 0;
		return DIB_HEADER_SIZE + Stride * Image.m_Height;
	}

	bool WriteDib(const CImageInfo &Image, void *pBuffer, size_t BufferSize)
	{
		const size_t Size = DibSize(Image);
		if(Size == 0 || pBuffer == nullptr || BufferSize < Size)
			return false;
		auto *pBytes = static_cast<uint8_t *>(pBuffer);
		mem_zero(pBytes, DIB_HEADER_SIZE);
		WriteUint32(pBytes, DIB_HEADER_SIZE);
		WriteUint32(pBytes + 4, (uint32_t)Image.m_Width);
		WriteUint32(pBytes + 8, (uint32_t)Image.m_Height);
		pBytes[12] = 1; // 一个色彩平面。
		pBytes[14] = 24;
		WriteUint32(pBytes + 20, (uint32_t)(Size - DIB_HEADER_SIZE));

		const size_t SourcePixelSize = Image.PixelSize();
		const size_t Stride = (Size - DIB_HEADER_SIZE) / Image.m_Height;
		for(size_t Row = 0; Row < Image.m_Height; ++Row)
		{
			const uint8_t *pSource = Image.m_pData + (Image.m_Height - Row - 1) * Image.m_Width * SourcePixelSize;
			uint8_t *pTarget = pBytes + DIB_HEADER_SIZE + Row * Stride;
			for(size_t Column = 0; Column < Image.m_Width; ++Column)
			{
				pTarget[Column * 3] = pSource[Column * SourcePixelSize + 2];
				pTarget[Column * 3 + 1] = pSource[Column * SourcePixelSize + 1];
				pTarget[Column * 3 + 2] = pSource[Column * SourcePixelSize];
			}
			mem_zero(pTarget + Image.m_Width * 3, Stride - Image.m_Width * 3);
		}
		return true;
	}

	bool Copy(const CImageInfo &Image)
	{
#if defined(CONF_FAMILY_WINDOWS)
		const size_t Size = DibSize(Image);
		const HWND Window = GetActiveWindow();
		if(Size == 0 || Window == nullptr)
			return false;
		struct SMemory
		{
			HGLOBAL m_Handle;
			~SMemory()
			{
				if(m_Handle != nullptr)
					GlobalFree(m_Handle);
			}
		} Memory{GlobalAlloc(GMEM_MOVEABLE, Size)};
		if(Memory.m_Handle == nullptr)
			return false;
		void *pBuffer = GlobalLock(Memory.m_Handle);
		if(pBuffer == nullptr)
			return false;
		const bool Written = WriteDib(Image, pBuffer, Size);
		GlobalUnlock(Memory.m_Handle);
		// 在清空剪贴板前准备好完整像素；获取失败时保留原内容。
		if(!Written || !OpenClipboard(Window))
			return false;
		struct SCloseClipboard
		{
			~SCloseClipboard() { CloseClipboard(); }
		} Close;
		if(!EmptyClipboard() || SetClipboardData(CF_DIB, Memory.m_Handle) == nullptr)
			return false;
		// 成功后由系统持有这块内存，客户端不能再释放。
		Memory.m_Handle = nullptr;
		return true;
#else
		(void)Image;
		return false;
#endif
	}

	IGraphics::FScreenshotCallback CaptureCallback(bool CopyToClipboard, IGraphics::FScreenshotCallback pfnOnTaken, FCopyImage pfnCopy)
	{
		return [CopyToClipboard, pfnOnTaken = std::move(pfnOnTaken), pfnCopy = std::move(pfnCopy)](CImageInfo &&Image, const char *pPath) {
			if(CopyToClipboard && !pfnCopy(Image))
				log_warn("screenshot", "Failed to copy screenshot to the clipboard");
			pfnOnTaken(std::move(Image), pPath);
		};
	}
}
