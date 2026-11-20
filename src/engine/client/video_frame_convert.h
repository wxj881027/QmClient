#ifndef ENGINE_CLIENT_VIDEO_FRAME_CONVERT_H
#define ENGINE_CLIENT_VIDEO_FRAME_CONVERT_H

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// 只替换 FFmpeg 边界；缓冲检查和转换结果判定由生产入口负责。
struct SVideoFrameConversionApi
{
	decltype(&av_frame_make_writable) m_pMakeWritable = av_frame_make_writable;
	decltype(&sws_scale) m_pScale = sws_scale;
};

// 编码器可能仍引用上一帧的数据；重新写入前取得可写缓冲，完整转换后才允许提交。
inline int ConvertVideoFrame(SwsContext *pContext, AVFrame *pOutput, const std::vector<uint8_t> &vRgba, int Width, int Height, const SVideoFrameConversionApi &Api = {})
{
	if(!pContext || !pOutput || Width <= 0 || Height <= 0 || Width > std::numeric_limits<int>::max() / 4 || pOutput->width <= 0 || pOutput->height <= 0)
		return AVERROR(EINVAL);
	const int Stride = Width * 4;
	if(static_cast<size_t>(Height) > vRgba.size() / static_cast<size_t>(Stride))
		return AVERROR_INVALIDDATA;
	const int Writable = Api.m_pMakeWritable(pOutput);
	if(Writable < 0)
		return Writable;
	const uint8_t *pData = vRgba.data();
	const int Converted = Api.m_pScale(pContext, &pData, &Stride, 0, Height, pOutput->data, pOutput->linesize);
	return Converted < 0 ? Converted : Converted == pOutput->height ? 0 :
									  AVERROR_INVALIDDATA;
}

#endif
