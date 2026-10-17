#ifndef ENGINE_CLIENT_SOUND_BUFFER_H
#define ENGINE_CLIENT_SOUND_BUFFER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>

// 混音索引和解码器的缓冲区长度均使用 int，先检查完整的交错采样数。
inline bool QmSoundSampleCount(int64_t Frames, int Channels, size_t &Count)
{
	Count = 0;
	if(Frames <= 0 || Channels < 1 || Channels > 2 || Frames > std::numeric_limits<int>::max() / Channels)
		return false;
	Count = static_cast<size_t>(Frames) * Channels;
	return true;
}

// 分配失败时不调用解码器，填充失败时不发布残缺 PCM；Allocate 可替换外部分配边界。
template<typename TSample, typename TFill, typename TAllocate = decltype(&calloc)>
std::unique_ptr<TSample, decltype(&free)> QmCreateSoundBuffer(int64_t Frames, int Channels, TFill Fill, TAllocate Allocate = &calloc)
{
	std::unique_ptr<TSample, decltype(&free)> pBuffer(nullptr, &free);
	size_t Count = 0;
	if(!QmSoundSampleCount(Frames, Channels, Count) || Count > std::numeric_limits<size_t>::max() / sizeof(TSample))
		return pBuffer;
	pBuffer.reset(static_cast<TSample *>(Allocate(Count, sizeof(TSample))));
	if(pBuffer && !Fill(pBuffer.get(), static_cast<int>(Count)))
		pBuffer.reset();
	return pBuffer;
}

inline int QmSoundResampledFrames(int Frames, int Channels, int SourceRate, int TargetRate)
{
	size_t Count = 0;
	if(!QmSoundSampleCount(Frames, Channels, Count) || SourceRate <= 0 || TargetRate <= 0)
		return 0;
	const double Resampled = static_cast<double>(Frames) * TargetRate / SourceRate;
	if(Resampled < 1.0 || Resampled > std::numeric_limits<int>::max() / Channels)
		return 0;
	return static_cast<int>(Resampled);
}

inline int QmSoundResampledLoopStart(int LoopStart, int SourceRate, int TargetRate, int Frames)
{
	if(LoopStart < 0 || SourceRate <= 0 || TargetRate <= 0 || Frames <= 0)
		return 0;
	// 下采样的四舍五入可能恰好落在末尾，循环起点必须指向一个真实采样帧。
	return static_cast<int>(std::clamp(std::round(static_cast<double>(LoopStart) * TargetRate / SourceRate), 0.0, static_cast<double>(Frames - 1)));
}

#endif
