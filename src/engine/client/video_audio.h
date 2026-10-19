#ifndef ENGINE_CLIENT_VIDEO_AUDIO_H
#define ENGINE_CLIENT_VIDEO_AUDIO_H

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// 只替换 FFmpeg 边界以验证转换失败；生产默认直接调用 FFmpeg。
struct SVideoAudioConversionApi
{
	decltype(&av_samples_fill_arrays) m_pFill = av_samples_fill_arrays;
	decltype(&av_frame_make_writable) m_pMakeWritable = av_frame_make_writable;
	int (*m_pConvert)(SwrContext *, uint8_t **, int, const uint8_t **, int) = [](SwrContext *pContext, uint8_t **ppOutput, int OutputSamples, const uint8_t **ppInput, int InputSamples) {
		return swr_convert(pContext, ppOutput, OutputSamples, ppInput, InputSamples);
	};
};

// 返回负值时输出不可提交编码；短转换同样拒绝，避免把旧样本作为本帧尾部写出。
inline int ConvertVideoAudioFrame(AVFrame *pInput, AVFrame *pOutput, SwrContext *pContext, const int16_t *pSamples, const SVideoAudioConversionApi &Api = {})
{
	const int FillResult = Api.m_pFill(pInput->data, nullptr, reinterpret_cast<const uint8_t *>(pSamples), 2, pInput->nb_samples, AV_SAMPLE_FMT_S16, 0);
	if(FillResult < 0)
		return FillResult;
	const int WritableResult = Api.m_pMakeWritable(pOutput);
	if(WritableResult < 0)
		return WritableResult;
	const int Result = Api.m_pConvert(pContext, pOutput->data, pOutput->nb_samples, const_cast<const uint8_t **>(pInput->data), pInput->nb_samples);
	return Result < 0 ? Result : Result == pOutput->nb_samples ? 0 :
								     AVERROR_INVALIDDATA;
}

#endif
