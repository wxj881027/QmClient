// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_provider.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace SystemMediaControls
{
	constexpr uint32_t ALBUM_ART_MAX_DIMENSION = 256;
	constexpr uint64_t NETEASE_HOOK_REFRESH_INTERVAL_FRAMES = 3;

	inline bool ShouldRefreshNeteaseHookSnapshot(uint64_t CurrentFrame, uint64_t LastReadFrame, bool HasReadFrame)
	{
		if(!HasReadFrame || CurrentFrame < LastReadFrame)
			return true;
		return CurrentFrame - LastReadFrame >= NETEASE_HOOK_REFRESH_INTERVAL_FRAMES;
	}

	inline bool MediaSourceChanged(const char *pPrevious, const char *pCurrent)
	{
		return std::string_view(pPrevious != nullptr ? pPrevious : "") != std::string_view(pCurrent != nullptr ? pCurrent : "");
	}

	inline bool AnyMediaSourceEnabled(bool SmtcEnabled, bool NeteaseHookEnabled)
	{
		return SmtcEnabled || NeteaseHookEnabled;
	}

	inline bool ShouldStopNeteaseHookForConfigurationChange(bool ConfigurationInitialized, bool WasHookEnabled)
	{
		return ConfigurationInitialized && WasHookEnabled;
	}

	struct SAlbumArtDecodeSize
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	inline SAlbumArtDecodeSize CalculateAlbumArtDecodeSize(uint32_t Width, uint32_t Height)
	{
		if(Width == 0 || Height == 0)
			return {};
		if(Width <= ALBUM_ART_MAX_DIMENSION && Height <= ALBUM_ART_MAX_DIMENSION)
			return {Width, Height};

		if(Width >= Height)
		{
			const uint32_t ScaledHeight = (uint32_t)(((uint64_t)Height * ALBUM_ART_MAX_DIMENSION + Width / 2) / Width);
			return {ALBUM_ART_MAX_DIMENSION, ScaledHeight > 0 ? ScaledHeight : 1};
		}
		const uint32_t ScaledWidth = (uint32_t)(((uint64_t)Width * ALBUM_ART_MAX_DIMENSION + Height / 2) / Height);
		return {ScaledWidth > 0 ? ScaledWidth : 1, ALBUM_ART_MAX_DIMENSION};
	}

	inline float AlbumArtCircleMaskAlpha(float PixelCenterX, float PixelCenterY, uint32_t Width, uint32_t Height, float Feather)
	{
		if(Width == 0 || Height == 0 || Feather <= 0.0f)
			return 0.0f;

		const float CenterX = Width * 0.5f;
		const float CenterY = Height * 0.5f;
		const float RadiusX = CenterX - 0.5f;
		const float RadiusY = CenterY - 0.5f;
		if(RadiusX <= 0.0f || RadiusY <= 0.0f)
			return 0.0f;

		const float NormalizedX = (PixelCenterX - CenterX) / RadiusX;
		const float NormalizedY = (PixelCenterY - CenterY) / RadiusY;
		const float NormalizedDistance = std::sqrt(NormalizedX * NormalizedX + NormalizedY * NormalizedY);
		const float DistanceInside = (1.0f - NormalizedDistance) * std::min(RadiusX, RadiusY);
		const float Coverage = std::clamp(DistanceInside / Feather, 0.0f, 1.0f);
		return Coverage * Coverage * (3.0f - 2.0f * Coverage);
	}

} // namespace SystemMediaControls

#if defined(CONF_FAMILY_WINDOWS) && defined(_MSC_VER)
#define SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED 1
#else
#define SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED 0
#endif

class CSystemMediaControls : public CComponent
{
public:
	enum class EPlaybackState : uint8_t
	{
		Unknown = 0,
		Playing,
		Paused,
		Stopped,
	};

	struct SState
	{
		bool m_CanPlay = false;
		bool m_CanPause = false;
		bool m_CanPrev = false;
		bool m_CanNext = false;
		bool m_CanSetVolume = false;
		float m_Volume = 0.0f;
		uint64_t m_VolumeGeneration = 0;
		EPlaybackState m_PlaybackState = EPlaybackState::Unknown;
		bool m_Playing = false;
		char m_aSourceAppId[128] = {};
		char m_aTitle[128] = {};
		char m_aArtist[128] = {};
		char m_aAlbum[128] = {};
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		int64_t m_PositionUpdatedTick = 0;
		uint64_t m_TimelineGeneration = 0;
		double m_PlaybackRate = 1.0;
		IGraphics::CTextureHandle m_AlbumArt;
		IGraphics::CTextureHandle m_AlbumArtCircular;
		int m_AlbumArtWidth = 0;
		int m_AlbumArtHeight = 0;
	};

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	struct SWinrt;
	struct SShared;
#endif

	CSystemMediaControls();
	~CSystemMediaControls() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;

	bool GetStateSnapshot(SState &State) const;
	// 读取网易云私有 v5 快照；不会把其字段合并到标准 SMTC 状态。
	bool GetNeteaseSnapshot(QmNeteaseHook::SSnapshotV5 &Snapshot) const;
	void Previous();
	void PlayPause();
	void Next();
	void SetVolume(uint64_t Generation, float Volume);

private:
	void SyncNeteaseHookConfiguration();

	std::unique_ptr<CQmNeteaseHookProvider> m_pNeteaseHook;
	bool m_NeteaseHookConfigInitialized = false;
	bool m_LastNeteaseHookEnabled = false;
	std::string m_LastNeteaseHookHelperPath;
	QmNeteaseHook::SSnapshotV5 m_NeteaseSnapshot{};
	bool m_HasNeteaseSnapshot = false;
	uint64_t m_LastNeteaseHookReadFrame = 0;
	bool m_NeteaseHookReadFrameInitialized = false;
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	std::unique_ptr<SWinrt> m_pWinrt;
	std::unique_ptr<SShared> m_pShared;
	std::thread m_Thread;
	std::atomic_bool m_StopThread{false};

	void ThreadMain();
#endif
};

#endif
