#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_CLIPBOARD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_CLIPBOARD_H

#include <base/detect.h>

#include <engine/graphics.h>
#include <engine/image.h>

#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <utility>

namespace qm_screenshot_clipboard
{

	constexpr bool Supported()
	{
#if defined(CONF_FAMILY_WINDOWS)
		return true;
#else
		return false;
#endif
	}

	// Windows 剪贴板使用不含文件头的 24 位 DIB；无效图像返回 0。
	size_t DibSize(const CImageInfo &Image);
	bool WriteDib(const CImageInfo &Image, void *pBuffer, size_t BufferSize);
	bool Copy(const CImageInfo &Image);

	using FCopyImage = std::function<bool(const CImageInfo &)>;
	// 拍摄请求固定开关值；先复制原始帧，再将所有权交给会修改像素的预览。
	IGraphics::FScreenshotCallback CaptureCallback(bool CopyToClipboard, IGraphics::FScreenshotCallback pfnOnTaken, FCopyImage pfnCopy = Copy);

	// 手动截图先准备完整回调和处理器，再交给图形层；原类型参数避免在保护范围外分配。
	template<typename FOnTaken, typename FProcessorFactory>
	bool RequestCapture(IGraphics &Graphics, bool CopyToClipboard, FOnTaken &&OnTaken, FProcessorFactory &&CreateProcessor)
	{
		try
		{
			auto Callback = CaptureCallback(CopyToClipboard, std::forward<FOnTaken>(OnTaken));
			auto Processor = std::forward<FProcessorFactory>(CreateProcessor)();
			Graphics.TakeScreenshot(nullptr, std::move(Callback), std::move(Processor));
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}

#endif
