#ifndef ENGINE_CLIENT_AUDIO_SDL3_H
#define ENGINE_CLIENT_AUDIO_SDL3_H

#if defined(CONF_VIDEORECORDER)
#include <engine/shared/video.h>
#endif

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

struct SSdlAudioDevice
{
	SDL_AudioDeviceID m_Id;
	std::string m_Name;
};

inline std::vector<SSdlAudioDevice> SdlAudioDevices(bool Recording)
{
	int Count = 0;
	std::unique_ptr<SDL_AudioDeviceID, decltype(&SDL_free)> pIds(Recording ? SDL_GetAudioRecordingDevices(&Count) : SDL_GetAudioPlaybackDevices(&Count), SDL_free);
	std::vector<SSdlAudioDevice> vDevices;
	if(pIds != nullptr)
	{
		vDevices.reserve(Count);
		for(int Index = 0; Index < Count; ++Index)
		{
			const char *pName = SDL_GetAudioDeviceName(pIds.get()[Index]);
			if(pName != nullptr && pName[0] != '\0')
				vDevices.push_back({pIds.get()[Index], pName});
		}
	}
	return vDevices;
}

// SDL 请求量按应用格式计算；设备周期可能大于预分配缓冲区，分块供给完整采样帧。
template<typename TMix>
bool FillSdlAudioStream(SDL_AudioStream *pStream, int AdditionalBytes, int Channels, int16_t *pBuffer, int CapacityFrames, TMix Mix)
{
	if(AdditionalBytes <= 0)
		return true;
	if(pStream == nullptr || pBuffer == nullptr || Channels < 1 || Channels > 2 || CapacityFrames <= 0)
		return false;
	const int FrameBytes = Channels * sizeof(int16_t);
	if(CapacityFrames > std::numeric_limits<int>::max() / FrameBytes)
		return false;
	int64_t Frames = (static_cast<int64_t>(AdditionalBytes) + FrameBytes - 1) / FrameBytes;
	while(Frames > 0)
	{
		const int Chunk = static_cast<int>(std::min<int64_t>(Frames, CapacityFrames));
		Mix(pBuffer, Chunk);
		if(!SDL_PutAudioStreamData(pStream, pBuffer, Chunk * FrameBytes))
			return false;
		Frames -= Chunk;
	}
	return true;
}

// 游戏音频只能由声卡或当前录像推进；录像选项的后续修改留给下一次录制。
template<typename TMix>
bool FillSdlPlaybackAudioStream(SDL_AudioStream *pStream, int AdditionalBytes, int Channels, int16_t *pBuffer, int CapacityFrames, TMix Mix)
{
#if defined(CONF_VIDEORECORDER)
	const IVideo *pVideo = IVideo::Current();
	const bool RecorderTakesSound = pVideo != nullptr && pVideo->HasAudio();
#else
	const bool RecorderTakesSound = false;
#endif
	return FillSdlAudioStream(pStream, AdditionalBytes, Channels, pBuffer, CapacityFrames, [&](int16_t *pSamples, int Frames) {
		if(RecorderTakesSound)
			std::fill_n(pSamples, Frames * Channels, int16_t{0});
		else
			Mix(pSamples, Frames);
	});
}

#endif // ENGINE_CLIENT_AUDIO_SDL3_H
