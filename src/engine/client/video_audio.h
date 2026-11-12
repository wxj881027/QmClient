#ifndef ENGINE_CLIENT_VIDEO_AUDIO_H
#define ENGINE_CLIENT_VIDEO_AUDIO_H

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// 按有理数帧率推进音频时间轴：每帧先加 sample_rate / FPS 的整数商，
// 再把除数作为余数分子累计，满一帧补一个样本，避免 48 kHz/144 FPS
// 这类非整除组合逐帧截断在长录像中累积成音画漂移。
// 余数是 [0, FPS) 的分子，使用无符号类型以匹配 FPS 的正数约束。
inline void AdvanceVideoAudioTimeline(int64_t &SamplesFrameCount, uint64_t &SamplesFrameRemainder, int SampleRate, int FPS)
{
	if(SampleRate <= 0 || FPS <= 0)
		return;
	SamplesFrameCount += SampleRate / FPS;
	SamplesFrameRemainder += static_cast<uint64_t>(SampleRate % FPS);
	if(SamplesFrameRemainder >= static_cast<uint64_t>(FPS))
	{
		SamplesFrameCount += static_cast<int64_t>(SamplesFrameRemainder / static_cast<uint64_t>(FPS));
		SamplesFrameRemainder %= static_cast<uint64_t>(FPS);
	}
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
