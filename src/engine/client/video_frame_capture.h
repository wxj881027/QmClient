#ifndef ENGINE_CLIENT_VIDEO_FRAME_CAPTURE_H
#define ENGINE_CLIENT_VIDEO_FRAME_CAPTURE_H

#include <engine/graphics.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// 图形线程只发布失败状态，录制对象仍由主线程停止和释放。
class CVideoFrameCapture
{
public:
	enum class EError
	{
		NONE,
		NO_READER,
		READ_FAILED,
		SIZE_MISMATCH,
		FORMAT_MISMATCH,
		INCOMPLETE_DATA,
	};

	CVideoFrameCapture(int Width, int Height) :
		m_Width(Width), m_Height(Height)
	{
	}

	bool Read(const TGLBackendReadPresentedImageData &Reader, std::vector<uint8_t> &vBuffer)
	{
		if(HasError())
			return false;
		if(!Reader)
			return Fail(EError::NO_READER);

		uint32_t Width = 0, Height = 0;
		CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGB;
		if(!Reader(Width, Height, Format, vBuffer))
			return Fail(EError::READ_FAILED);
		if(m_Width <= 0 || m_Height <= 0 || Width != static_cast<uint32_t>(m_Width) || Height != static_cast<uint32_t>(m_Height))
			return Fail(EError::SIZE_MISMATCH);
		if(Format != CImageInfo::FORMAT_RGBA)
			return Fail(EError::FORMAT_MISMATCH);
		if(Height > std::numeric_limits<size_t>::max() / Width / 4 || vBuffer.size() < static_cast<size_t>(Width) * Height * 4)
			return Fail(EError::INCOMPLETE_DATA);
		return true;
	}

	EError Error() const { return m_Error.load(std::memory_order_acquire); }
	bool HasError() const { return Error() != EError::NONE; }
	const char *ErrorDescription() const
	{
		switch(Error())
		{
		case EError::NONE: return "";
		case EError::NO_READER: return "Video frame reader is unavailable";
		case EError::READ_FAILED: return "Could not read the presented video frame";
		case EError::SIZE_MISMATCH: return "Video frame dimensions changed or are invalid";
		case EError::FORMAT_MISMATCH: return "Video frame is not RGBA";
		case EError::INCOMPLETE_DATA: return "Video frame data is incomplete";
		}
		return "Unknown video frame capture error";
	}

private:
	bool Fail(EError Error)
	{
		m_Error.store(Error, std::memory_order_release);
		return false;
	}

	const int m_Width;
	const int m_Height;
	std::atomic<EError> m_Error{EError::NONE};
};

#endif
