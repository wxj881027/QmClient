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

	// 会话活跃度：选源优先级只认这四档。数值只用于比较，不参与任何显示逻辑。
	enum class ESessionActivity
	{
		STOPPED = 0,
		UNKNOWN = 1,
		PAUSED = 2,
		PLAYING = 3,
	};

	inline int SessionActivityPriority(ESessionActivity Activity)
	{
		return static_cast<int>(Activity);
	}

	// 多个媒体会话里挑一个：分数最高者，同分保留系统当前会话（CurrentIndex），
	// 当前会话无效时取第一个最高分。返回下标；空列表返回 -1。
	// 系统的「当前会话」在浏览器视频停下后不会自动让位给另一个正在播放的播放器
	// （要等下一次媒体事件，典型就是切歌），所以不能只信 GetCurrentSession()。
	inline int PickMediaSession(const int *pPriorities, int Count, int CurrentIndex)
	{
		if(Count <= 0)
			return -1;
		int Best = (CurrentIndex >= 0 && CurrentIndex < Count) ? CurrentIndex : 0;
		for(int i = 0; i < Count; ++i)
		{
			if(pPriorities[i] > pPriorities[Best])
				Best = i;
		}
		return Best;
	}

	// 「状态说在播、时间轴却停住」判定：有明确时长（>1s）且最近一次位置更新时间
	// 超过 5 秒，按暂停参与选源。只影响选中哪个会话，不改写实际播放状态。
	inline bool MediaTimelineLooksStale(bool Playing, int64_t DurationMs, int64_t LastUpdatedUtc100ns, int64_t NowUtc100ns)
	{
		// 有的播放器根本不填 LastUpdatedTime（值为 0）：那是「没上报」，不是「很久没更新」。
		if(!Playing || DurationMs <= 1000 || LastUpdatedUtc100ns <= 0)
			return false;
		constexpr int64_t STALE_SECONDS = 5;
		return NowUtc100ns - LastUpdatedUtc100ns > STALE_SECONDS * 10'000'000LL;
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

// Windows 之外的平台改用 MPRIS（org.mpris.MediaPlayer2）D-Bus 接口，
// 由 CMake 在找到 libdbus-1 时定义 CONF_MPRIS。
#if !SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED && defined(CONF_FAMILY_UNIX) && defined(CONF_MPRIS)
#define SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED 1
#else
#define SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED 0
#endif

// 两个后端共用同一份后台线程状态，因此共享部分只在任一后端可用时编译。
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED || SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
#define SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED 1
#else
#define SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED 0
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
#endif

#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	struct SMpris;
#endif

#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
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
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	std::unique_ptr<SMpris> m_pMpris;
#endif
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	std::unique_ptr<SShared> m_pShared;
	std::thread m_Thread;
	std::atomic_bool m_StopThread{false};

	void ThreadMain();

	// 主线程持有的后端状态；同一时间只有一个后端存在，因此这里无需再加后端分支。
	SState *MainState();
	const SState *MainState() const;
	bool MainHasMedia() const;
	void SetMainHasMedia(bool HasMedia);

	// 只在开关打开、共享状态已就绪且当前确实有媒体时返回主线程状态。
	const SState *ActiveState() const;
#endif
};

#endif
