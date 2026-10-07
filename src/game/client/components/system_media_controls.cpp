// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "system_media_controls.h"

#include "system_media_controls_timeline.h"

#include <base/time.h>

#include <engine/client/client.h>
// SyncNeteaseHookConfiguration / OnUpdate 在非 WINRT 平台也会编译
#include <engine/client.h>
#include <engine/shared/config.h>

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>

#include <game/client/components/qmclient/media_volume_logic.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/player_volume.h>
#include <game/client/components/qmclient/prepared_media_art.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#endif

#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
#include "system_media_controls_mpris_policy.h"

#include <base/str.h>

#include <game/client/components/qmclient/media_volume_logic.h>
#include <game/client/components/qmclient/prepared_media_art.h>

#include <dbus/dbus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#endif

#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SPlainState
{
	bool m_CanPlay = false;
	bool m_CanPause = false;
	bool m_CanPrev = false;
	bool m_CanNext = false;
	bool m_CanSetVolume = false;
	float m_Volume = 0.0f;
	uint64_t m_VolumeGeneration = 0;
	CSystemMediaControls::EPlaybackState m_PlaybackState = CSystemMediaControls::EPlaybackState::Unknown;
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
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum class ECommand
{
	Prev,
	PlayPause,
	Next,
};

struct SMediaCommand
{
	ECommand m_Type;
	std::string m_SourceAppId;
	uint64_t m_Generation = 0;
};

struct CSystemMediaControls::SShared
{
	std::mutex m_Mutex;
	SPlainState m_State{};
	bool m_HasMedia = false;
	std::deque<SMediaCommand> m_Commands;
	QmMediaVolume::CPendingVolume m_PendingVolume;
	std::unique_ptr<CQmPreparedMediaArt> m_pAlbumArt;
	int m_AlbumArtWidth = 0;
	int m_AlbumArtHeight = 0;
	bool m_AlbumArtDirty = false;
};

// 把后台线程发布的状态搬到主线程副本；SMTC 与 MPRIS 后端共用这段搬运逻辑。
static void ApplySharedState(const SPlainState &Source, CSystemMediaControls::SState &Target)
{
	Target.m_CanPlay = Source.m_CanPlay;
	Target.m_CanPause = Source.m_CanPause;
	Target.m_CanPrev = Source.m_CanPrev;
	Target.m_CanNext = Source.m_CanNext;
	Target.m_CanSetVolume = Source.m_CanSetVolume;
	Target.m_Volume = Source.m_Volume;
	Target.m_VolumeGeneration = Source.m_VolumeGeneration;
	Target.m_PlaybackState = Source.m_PlaybackState;
	Target.m_Playing = Source.m_Playing;
	str_copy(Target.m_aSourceAppId, Source.m_aSourceAppId, sizeof(Target.m_aSourceAppId));
	str_copy(Target.m_aTitle, Source.m_aTitle, sizeof(Target.m_aTitle));
	str_copy(Target.m_aArtist, Source.m_aArtist, sizeof(Target.m_aArtist));
	str_copy(Target.m_aAlbum, Source.m_aAlbum, sizeof(Target.m_aAlbum));
	Target.m_PositionMs = Source.m_PositionMs;
	Target.m_DurationMs = Source.m_DurationMs;
	Target.m_PositionUpdatedTick = Source.m_PositionUpdatedTick;
	Target.m_TimelineGeneration = Source.m_TimelineGeneration;
	Target.m_PlaybackRate = Source.m_PlaybackRate;
}

// 两个后端都不持有专辑封面以外的主线程资源，清理逻辑因此可以共用。
static void ClearAlbumArtLocal(CSystemMediaControls::SState &State, IGraphics *pGraphics)
{
	if(pGraphics && State.m_AlbumArt.IsValid())
	{
		pGraphics->UnloadTexture(&State.m_AlbumArt);
	}
	if(pGraphics && State.m_AlbumArtCircular.IsValid())
	{
		pGraphics->UnloadTexture(&State.m_AlbumArtCircular);
	}
	State.m_AlbumArt.Invalidate();
	State.m_AlbumArtCircular.Invalidate();
	State.m_AlbumArtWidth = 0;
	State.m_AlbumArtHeight = 0;
}

static void ClearState(CSystemMediaControls::SState &State, IGraphics *pGraphics)
{
	ClearAlbumArtLocal(State, pGraphics);
	State = CSystemMediaControls::SState{};
}
#endif

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
using namespace winrt::Windows::Media::Control;

struct CSystemMediaControls::SWinrt
{
	CSystemMediaControls::SState m_State{};
	bool m_HasMedia = false;
};

template<typename TAsyncOp>
static bool WaitForAsync(const TAsyncOp &Operation, const std::atomic_bool &StopFlag)
{
	using winrt::Windows::Foundation::AsyncStatus;
	while(true)
	{
		const AsyncStatus Status = Operation.Status();
		if(Status == AsyncStatus::Completed)
			return true;
		if(Status == AsyncStatus::Canceled || Status == AsyncStatus::Error)
			return false;
		if(StopFlag.load(std::memory_order_relaxed))
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

static void ClearSharedAlbumArt(CSystemMediaControls::SShared *pShared)
{
	// 旧封面在离开锁后析构：上传线程只做一次指针移交，不再复制整块像素。
	std::unique_ptr<CQmPreparedMediaArt> pDiscarded;
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		pDiscarded = std::move(pShared->m_pAlbumArt);
		pShared->m_AlbumArtWidth = 0;
		pShared->m_AlbumArtHeight = 0;
		pShared->m_AlbumArtDirty = true;
	}
}

static void SetSharedAlbumArt(CSystemMediaControls::SShared *pShared, const std::vector<uint8_t> &Pixels, const std::vector<uint8_t> &CircularPixels, int Width, int Height)
{
	// 分配与像素复制在后台、锁外完成；旧封面同样在离开锁后析构。
	auto pPrepared = std::make_unique<CQmPreparedMediaArt>(Pixels, CircularPixels, Width, Height);
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		pShared->m_pAlbumArt.swap(pPrepared);
		pShared->m_AlbumArtWidth = Width;
		pShared->m_AlbumArtHeight = Height;
		pShared->m_AlbumArtDirty = true;
	}
}

static void ClearMediaText(SPlainState &State)
{
	State.m_aTitle[0] = '\0';
	State.m_aArtist[0] = '\0';
	State.m_aAlbum[0] = '\0';
}

static bool IsAppleMusicPlayerId(const char *pSourceAppId)
{
	return pSourceAppId != nullptr &&
	       (str_find_nocase(pSourceAppId, "AppleMusic.exe") != nullptr ||
		       str_startswith_nocase(pSourceAppId, "AppleInc.AppleMusicWin_") != nullptr);
}

static void RemoveSuffixNoCase(std::string &Value, const char *pSuffix)
{
	if(pSuffix == nullptr)
		return;
	const char *pMatch = str_endswith_nocase(Value.c_str(), pSuffix);
	if(pMatch != nullptr)
		Value.erase((size_t)(pMatch - Value.c_str()));
}

static void ApplyAppleMusicMetadataFix(const char *pSourceAppId, std::string &Artist, std::string &Album)
{
	if(!IsAppleMusicPlayerId(pSourceAppId))
		return;
	constexpr char APPLE_MUSIC_ARTIST_ALBUM_SEPARATOR[] = " \xE2\x80\x94 ";
	const size_t Separator = Artist.find(APPLE_MUSIC_ARTIST_ALBUM_SEPARATOR);
	if(Separator == std::string::npos)
		return;
	Album = Artist.substr(Separator + str_length(APPLE_MUSIC_ARTIST_ALBUM_SEPARATOR));
	Artist = Artist.substr(0, Separator);
	RemoveSuffixNoCase(Album, " - Single");
	RemoveSuffixNoCase(Album, " - EP");
}

static void ClearMediaDetails(SPlainState &State, std::string &AlbumArtKey, CSystemMediaControls::SShared *pShared)
{
	ClearMediaText(State);
	AlbumArtKey.clear();
	ClearSharedAlbumArt(pShared);
}

static void ResetSharedState(CSystemMediaControls::SShared *pShared, SPlainState &State, bool &HasMedia, std::string &AlbumArtKey)
{
	HasMedia = false;
	State = SPlainState{};
	AlbumArtKey.clear();
	ClearSharedAlbumArt(pShared);
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_State = State;
	pShared->m_HasMedia = false;
	pShared->m_PendingVolume.Reset();
	pShared->m_Commands.clear();
}

static void ApplyRoundedMask(std::vector<uint8_t> &Pixels, int Width, int Height, float Radius);
static void ApplyCircularFeatherMask(std::vector<uint8_t> &Pixels, int Width, int Height);

static void UpdateAlbumArtData(CSystemMediaControls::SShared *pShared, const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Thumbnail, const std::atomic_bool &StopFlag)
{
	if(!Thumbnail)
	{
		ClearSharedAlbumArt(pShared);
		return;
	}

	try
	{
		const auto StreamOp = Thumbnail.OpenReadAsync();
		if(!WaitForAsync(StreamOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto Stream = StreamOp.GetResults();
		if(!Stream)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto DecoderOp = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(Stream);
		if(!WaitForAsync(DecoderOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto Decoder = DecoderOp.GetResults();
		if(!Decoder)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const SystemMediaControls::SAlbumArtDecodeSize DecodeSize = SystemMediaControls::CalculateAlbumArtDecodeSize(Decoder.PixelWidth(), Decoder.PixelHeight());
		if(DecodeSize.m_Width == 0 || DecodeSize.m_Height == 0)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		winrt::Windows::Graphics::Imaging::BitmapTransform Transform;
		Transform.ScaledWidth(DecodeSize.m_Width);
		Transform.ScaledHeight(DecodeSize.m_Height);
		Transform.InterpolationMode(winrt::Windows::Graphics::Imaging::BitmapInterpolationMode::Fant);
		const auto PixelDataOp = Decoder.GetPixelDataAsync(
			winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
			winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
			Transform,
			winrt::Windows::Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,
			winrt::Windows::Graphics::Imaging::ColorManagementMode::DoNotColorManage);
		if(!WaitForAsync(PixelDataOp, StopFlag))
		{
			ClearSharedAlbumArt(pShared);
			return;
		}
		const auto PixelData = PixelDataOp.GetResults();
		if(!PixelData)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto Pixels = PixelData.DetachPixelData();
		const size_t ExpectedSize = (size_t)DecodeSize.m_Width * (size_t)DecodeSize.m_Height * 4;
		if(Pixels.size() < ExpectedSize)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		std::vector<uint8_t> Copy(Pixels.begin(), Pixels.begin() + ExpectedSize);
		std::vector<uint8_t> CircularCopy = Copy;
		ApplyCircularFeatherMask(CircularCopy, (int)DecodeSize.m_Width, (int)DecodeSize.m_Height);
		const float RoundingRatio = 2.0f / 14.0f;
		const float Radius = (float)std::min(DecodeSize.m_Width, DecodeSize.m_Height) * RoundingRatio;
		ApplyRoundedMask(Copy, (int)DecodeSize.m_Width, (int)DecodeSize.m_Height, Radius);
		SetSharedAlbumArt(pShared, Copy, CircularCopy, (int)DecodeSize.m_Width, (int)DecodeSize.m_Height);
	}
	catch(const winrt::hresult_error &)
	{
		ClearSharedAlbumArt(pShared);
	}
}

static void ApplyRoundedMask(std::vector<uint8_t> &Pixels, int Width, int Height, float Radius)
{
	if(Pixels.empty() || Width <= 0 || Height <= 0 || Radius <= 0.0f)
		return;

	const float MaxRadius = 0.5f * (float)std::min(Width, Height);
	const float R = std::min(Radius, MaxRadius);
	if(R <= 0.0f)
		return;

	const float Left = R;
	const float Right = (float)Width - R;
	const float Top = R;
	const float Bottom = (float)Height - R;
	const float OuterR2 = R * R;
	const float InnerR = R - 1.0f;
	const float InnerR2 = InnerR > 0.0f ? InnerR * InnerR : 0.0f;
	const bool UseSoftEdge = InnerR > 0.0f;

	for(int y = 0; y < Height; ++y)
	{
		const float Fy = (float)y + 0.5f;
		for(int x = 0; x < Width; ++x)
		{
			const float Fx = (float)x + 0.5f;
			float Dx = 0.0f;
			float Dy = 0.0f;
			bool Corner = false;

			if(Fx < Left && Fy < Top)
			{
				Dx = Left - Fx;
				Dy = Top - Fy;
				Corner = true;
			}
			else if(Fx > Right && Fy < Top)
			{
				Dx = Fx - Right;
				Dy = Top - Fy;
				Corner = true;
			}
			else if(Fx < Left && Fy > Bottom)
			{
				Dx = Left - Fx;
				Dy = Fy - Bottom;
				Corner = true;
			}
			else if(Fx > Right && Fy > Bottom)
			{
				Dx = Fx - Right;
				Dy = Fy - Bottom;
				Corner = true;
			}

			if(!Corner)
				continue;

			const float Dist2 = Dx * Dx + Dy * Dy;
			if(Dist2 <= (UseSoftEdge ? InnerR2 : OuterR2))
				continue;

			float Alpha = 0.0f;
			if(UseSoftEdge && Dist2 < OuterR2)
			{
				const float Dist = std::sqrt(Dist2);
				Alpha = std::clamp(R - Dist, 0.0f, 1.0f);
			}

			const size_t Index = (size_t)(y * Width + x) * 4;
			if(Alpha <= 0.0f)
			{
				Pixels[Index + 3] = 0;
			}
			else if(Alpha < 1.0f)
			{
				Pixels[Index + 3] = (uint8_t)std::round(Pixels[Index + 3] * Alpha);
			}
		}
	}
}

static void ApplyCircularFeatherMask(std::vector<uint8_t> &Pixels, int Width, int Height)
{
	if(Width <= 0 || Height <= 0)
		return;
	const size_t ExpectedSize = (size_t)Width * (size_t)Height * 4;
	if(Pixels.size() < ExpectedSize)
		return;

	const float Feather = std::clamp((float)std::min(Width, Height) / 64.0f, 1.0f, 6.0f);
	for(int y = 0; y < Height; ++y)
	{
		for(int x = 0; x < Width; ++x)
		{
			const float Alpha = SystemMediaControls::AlbumArtCircleMaskAlpha((float)x + 0.5f, (float)y + 0.5f, Width, Height, Feather);
			if(Alpha >= 1.0f)
				continue;

			const size_t Index = (size_t)(y * Width + x) * 4;
			if(Alpha <= 0.0f)
			{
				Pixels[Index + 3] = 0;
			}
			else
			{
				Pixels[Index + 3] = (uint8_t)std::round(Pixels[Index + 3] * Alpha);
			}
		}
	}
}

// 像素在后台线程已备好：这里直接把 CImageInfo 交给 LoadTextureRawMove，
// 主线程不再为上传复制一份 RGBA 缓冲。
static IGraphics::CTextureHandle LoadAlbumArtTexture(IGraphics *pGraphics, CImageInfo &Image, const char *pName)
{
	if(pGraphics == nullptr || Image.m_pData == nullptr)
		return {};
	return pGraphics->LoadTextureRawMove(Image, 0, pName);
}

static void ApplySharedAlbumArt(CSystemMediaControls::SShared *pShared, CSystemMediaControls::SState &State, IGraphics *pGraphics, const IClient *pClient)
{
	if(!pShared || !pGraphics)
		return;

	bool AlbumArtDirty = false;
	int AlbumArtWidth = 0;
	int AlbumArtHeight = 0;
	std::unique_ptr<CQmPreparedMediaArt> pAlbumArt;
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		if(pShared->m_AlbumArtDirty)
		{
			AlbumArtDirty = true;
			AlbumArtWidth = pShared->m_AlbumArtWidth;
			AlbumArtHeight = pShared->m_AlbumArtHeight;
			pAlbumArt = std::move(pShared->m_pAlbumArt);
			pShared->m_AlbumArtDirty = false;
		}
	}

	if(!AlbumArtDirty)
		return;
	CPerfTimer ApplyTimer;

	ClearAlbumArtLocal(State, pGraphics);

	if(pAlbumArt != nullptr)
	{
		State.m_AlbumArt = LoadAlbumArtTexture(pGraphics, pAlbumArt->m_Original, "smtc_album_art");
		State.m_AlbumArtCircular = LoadAlbumArtTexture(pGraphics, pAlbumArt->m_Circular, "smtc_album_art_circular");
	}
	if(State.m_AlbumArt.IsValid())
	{
		State.m_AlbumArtWidth = AlbumArtWidth;
		State.m_AlbumArtHeight = AlbumArtHeight;
	}

	char aExtra[128];
	str_format(aExtra, sizeof(aExtra), "width=%d height=%d valid=%d circular_valid=%d", AlbumArtWidth, AlbumArtHeight, State.m_AlbumArt.IsValid() ? 1 : 0, State.m_AlbumArtCircular.IsValid() ? 1 : 0);
	QmPerfLogStage("perf/system_media_controls", "album_art_apply", ApplyTimer.ElapsedMs(), true, pClient, nullptr, nullptr, aExtra);
}

#endif

CSystemMediaControls::CSystemMediaControls() = default;
CSystemMediaControls::~CSystemMediaControls() = default;

void CSystemMediaControls::SyncNeteaseHookConfiguration()
{
	const bool HookEnabled = g_Config.m_QmNeteaseHookEnable != 0;
	const bool ConfigurationChanged = !m_NeteaseHookConfigInitialized ||
					  HookEnabled != m_LastNeteaseHookEnabled ||
					  (HookEnabled && m_LastNeteaseHookHelperPath != g_Config.m_QmNeteaseHookHelperPath);
	if(!ConfigurationChanged)
		return;

	if(m_pNeteaseHook != nullptr)
	{
		if(SystemMediaControls::ShouldStopNeteaseHookForConfigurationChange(m_NeteaseHookConfigInitialized, m_LastNeteaseHookEnabled))
			m_pNeteaseHook->Stop();
		if(HookEnabled)
			m_pNeteaseHook->Start(g_Config.m_QmNeteaseHookHelperPath, g_Config.m_QmNeteaseHookTimeoutMs);
	}

	m_NeteaseHookConfigInitialized = true;
	m_LastNeteaseHookEnabled = HookEnabled;
	if(HookEnabled)
		m_LastNeteaseHookHelperPath.assign(g_Config.m_QmNeteaseHookHelperPath);
	else
		m_LastNeteaseHookHelperPath.clear();
	m_HasNeteaseSnapshot = false;
	m_NeteaseSnapshot = {};
	m_LastNeteaseHookReadFrame = 0;
	m_NeteaseHookReadFrameInitialized = false;
}

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
void CSystemMediaControls::ThreadMain()
{
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	}
	catch(const winrt::hresult_error &)
	{
		return;
	}

	{
		// Release WinRT objects before tearing down the apartment.
		GlobalSystemMediaTransportControlsSessionManager Manager{nullptr};
		GlobalSystemMediaTransportControlsSession Session{nullptr};
		SPlainState State{};
		bool HasMedia = false;
		std::string AlbumArtKey;
		SystemMediaControls::CTimelineGenerationTracker TimelineGenerationTracker;
		CQmPlayerVolume PlayerVolume;
		auto LastPropsUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(2);

		while(!m_StopThread)
		{
			try
			{
				if(!g_Config.m_QmSmtcEnable)
				{
					PlayerVolume.Reset();
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}

				if(!Manager)
				{
					try
					{
						const auto RequestOp = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
						if(!WaitForAsync(RequestOp, m_StopThread))
						{
							if(m_StopThread.load(std::memory_order_relaxed))
								break;
							Manager = nullptr;
						}
						else
						{
							// NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
							Manager = RequestOp.GetResults();
						}
					}
					catch(const winrt::hresult_error &)
					{
						Manager = nullptr;
					}
				}

				if(!Manager)
				{
					if(HasMedia)
					{
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}

				Session = Manager.GetCurrentSession();
				if(!Session)
				{
					PlayerVolume.Reset();
					if(HasMedia)
					{
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}

				const auto PlaybackInfo = Session.GetPlaybackInfo();
				if(!PlaybackInfo)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				const auto Controls = PlaybackInfo.Controls();
				if(!Controls)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				State.m_CanPlay = Controls.IsPlayEnabled();
				State.m_CanPause = Controls.IsPauseEnabled();
				State.m_CanPrev = Controls.IsPreviousEnabled();
				State.m_CanNext = Controls.IsNextEnabled();
				const auto PlaybackStatus = PlaybackInfo.PlaybackStatus();
				switch(PlaybackStatus)
				{
				case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
					State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Playing;
					break;
				case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
					State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Paused;
					break;
				case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
				case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
					State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Stopped;
					break;
				default:
					State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Unknown;
					break;
				}
				State.m_Playing = State.m_PlaybackState == CSystemMediaControls::EPlaybackState::Playing;
				const auto PlaybackRate = PlaybackInfo.PlaybackRate();
				State.m_PlaybackRate = PlaybackRate ? std::max(0.0, PlaybackRate.Value()) : 1.0;
				const std::string SourceAppId = winrt::to_string(Session.SourceAppUserModelId());
				const bool SourceChanged = SystemMediaControls::MediaSourceChanged(State.m_aSourceAppId, SourceAppId.c_str());
				if(SourceChanged)
				{
					// 当前会话切换时，旧播放器的标题/封面不能和新来源
					// 组合发布；同时让下一次循环立即读取新会话属性。
					ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
					TimelineGenerationTracker.Reset();
				}
				str_copy(State.m_aSourceAppId, SourceAppId.c_str(), sizeof(State.m_aSourceAppId));

				auto Volume = PlayerVolume.Refresh(SourceAppId);
				std::optional<QmMediaVolume::SRequest> VolumeRequest;
				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					VolumeRequest = m_pShared->m_PendingVolume.Take(Volume.m_Generation);
				}
				if(VolumeRequest && Volume.m_Available)
				{
					PlayerVolume.SetVolume(VolumeRequest->m_Generation, VolumeRequest->m_Level);
					Volume = PlayerVolume.Refresh(SourceAppId);
				}
				State.m_CanSetVolume = Volume.m_Available;
				State.m_Volume = Volume.m_Level;
				State.m_VolumeGeneration = Volume.m_Generation;

				const auto Timeline = Session.GetTimelineProperties();
				if(!Timeline)
				{
					if(HasMedia)
						ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				const int64_t TimelineReadTick = time_get_impl();
				const auto TimelineObservedUtc = winrt::clock::now();
				SystemMediaControls::STimelineProperties TimelineProperties;
				TimelineProperties.m_Start100ns = Timeline.StartTime().count();
				TimelineProperties.m_End100ns = Timeline.EndTime().count();
				TimelineProperties.m_Position100ns = Timeline.Position().count();
				TimelineProperties.m_LastUpdatedUtc100ns = Timeline.LastUpdatedTime().time_since_epoch().count();
				const SystemMediaControls::STimelineSnapshot TimelineSnapshot = SystemMediaControls::NormalizeTimelineProperties(
					TimelineProperties,
					TimelineObservedUtc.time_since_epoch().count(),
					TimelineReadTick,
					time_freq());
				State.m_PositionMs = TimelineSnapshot.m_PositionMs;
				State.m_DurationMs = TimelineSnapshot.m_DurationMs;
				State.m_PositionUpdatedTick = TimelineSnapshot.m_PositionUpdatedTick;
				State.m_TimelineGeneration = TimelineGenerationTracker.Update(TimelineProperties);
				HasMedia = true;

				const auto Now = std::chrono::steady_clock::now();
				if(SourceChanged || Now - LastPropsUpdate >= std::chrono::seconds(1))
				{
					LastPropsUpdate = Now;
					try
					{
						const auto MediaPropsOp = Session.TryGetMediaPropertiesAsync();
						if(!WaitForAsync(MediaPropsOp, m_StopThread))
						{
							if(m_StopThread.load(std::memory_order_relaxed))
								break;
							ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
						}
						else
						{
							const auto MediaProps = MediaPropsOp.GetResults();
							if(!MediaProps)
							{
								ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
							}
							else
							{
								const std::string Title = winrt::to_string(MediaProps.Title());
								std::string Artist = winrt::to_string(MediaProps.Artist());
								std::string Album = winrt::to_string(MediaProps.AlbumTitle());
								ApplyAppleMusicMetadataFix(State.m_aSourceAppId, Artist, Album);

								if(!Title.empty())
								{
									str_copy(State.m_aTitle, Title.c_str(), sizeof(State.m_aTitle));
								}
								else
								{
									State.m_aTitle[0] = '\0';
								}

								if(!Artist.empty())
								{
									str_copy(State.m_aArtist, Artist.c_str(), sizeof(State.m_aArtist));
								}
								else
								{
									State.m_aArtist[0] = '\0';
								}

								if(!Album.empty())
								{
									str_copy(State.m_aAlbum, Album.c_str(), sizeof(State.m_aAlbum));
								}
								else
								{
									State.m_aAlbum[0] = '\0';
								}

								const bool HasText = !Title.empty() || !Artist.empty() || !Album.empty();
								if(HasText)
								{
									std::string NewKey = Title;
									NewKey.push_back('\n');
									NewKey.append(Artist);
									NewKey.push_back('\n');
									NewKey.append(Album);
									if(NewKey != AlbumArtKey)
									{
										AlbumArtKey = NewKey;
										const auto Thumbnail = MediaProps.Thumbnail();
										if(Thumbnail)
											UpdateAlbumArtData(m_pShared.get(), Thumbnail, m_StopThread);
										else
											ClearSharedAlbumArt(m_pShared.get());
									}
								}
								else
								{
									ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
								}
							}
						}
					}
					catch(const winrt::hresult_error &)
					{
						ClearMediaDetails(State, AlbumArtKey, m_pShared.get());
					}
				}

				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					m_pShared->m_State = State;
					m_pShared->m_HasMedia = HasMedia;
				}

				std::deque<SMediaCommand> Commands;
				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					Commands.swap(m_pShared->m_Commands);
				}
				if(Session)
				{
					for(const auto &Command : Commands)
					{
						try
						{
							const auto CurrentSession = Manager.GetCurrentSession();
							if(!g_Config.m_QmSmtcEnable || !CurrentSession || Command.m_SourceAppId != SourceAppId || winrt::to_string(CurrentSession.SourceAppUserModelId()) != SourceAppId)
								continue;
							switch(Command.m_Type)
							{
							case ECommand::Prev:
								Session.TrySkipPreviousAsync();
								break;
							case ECommand::PlayPause:
								Session.TryTogglePlayPauseAsync();
								break;
							case ECommand::Next:
								Session.TrySkipNextAsync();
								break;
							}
						}
						catch(const winrt::hresult_error &)
						{
							continue;
						}
					}
				}
			}

			catch(const winrt::hresult_error &)
			{
				PlayerVolume.Reset();
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}
			catch(...)
			{
				PlayerVolume.Reset();
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}

	winrt::uninit_apartment();
}
#endif

#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
// Linux/BSD 的媒体集成走 MPRIS：会话总线上每个播放器都提供
// org.mpris.MediaPlayer2.Player，属性语义与 SMTC 基本一一对应。
struct CSystemMediaControls::SMpris
{
	CSystemMediaControls::SState m_State{};
	bool m_HasMedia = false;
};

static constexpr int MPRIS_POLL_INTERVAL_MS = 500;
static constexpr const char *MPRIS_NAME_PREFIX = "org.mpris.MediaPlayer2.";
static constexpr const char *MPRIS_OBJECT_PATH = "/org/mpris/MediaPlayer2";
static constexpr const char *MPRIS_PLAYER_INTERFACE = "org.mpris.MediaPlayer2.Player";
static constexpr const char *MPRIS_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
static constexpr const char *DBUS_SERVICE_NAME = "org.freedesktop.DBus";
static constexpr const char *DBUS_OBJECT_PATH = "/org/freedesktop/DBus";
static constexpr const char *DBUS_INTERFACE_NAME = "org.freedesktop.DBus";

static thread_local const std::atomic_bool *s_pMprisStop = nullptr;
static thread_local const SystemMediaControls::CMprisPollBudget *s_pMprisBudget = nullptr;
static thread_local int s_MprisReservedMs = 0;
static int64_t MprisNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool MprisRequestAllowed() { return s_pMprisStop != nullptr && s_pMprisBudget != nullptr && s_pMprisBudget->Remaining(MprisNowMs(), s_pMprisStop->load()) > s_MprisReservedMs; }

static DBusMessage *MprisSendBlocking(DBusConnection *pConnection, DBusMessage *pRequest)
{
	if(pRequest == nullptr)
		return nullptr;
	if(!MprisRequestAllowed())
	{
		dbus_message_unref(pRequest);
		return nullptr;
	}
	DBusPendingCall *pPending = nullptr;
	// 单次请求最多 250ms，且不能超出整轮预算；超时服务不会吃掉全部发现时间。
	const int Timeout = SystemMediaControls::MprisRequestTimeout(s_pMprisBudget->Remaining(MprisNowMs(), s_pMprisStop->load()), s_MprisReservedMs);
	const SystemMediaControls::CMprisPollBudget RequestBudget(MprisNowMs(), Timeout);
	const bool Sent = dbus_connection_send_with_reply(pConnection, pRequest, &pPending, Timeout) != FALSE;
	dbus_message_unref(pRequest);
	if(!Sent || pPending == nullptr)
	{
		if(pPending != nullptr)
			dbus_pending_call_unref(pPending);
		return nullptr;
	}
	const bool Completed = SystemMediaControls::MprisWaitForReply(RequestBudget, [] { return s_pMprisStop->load(); }, [] { return MprisNowMs(); }, [pPending] { return dbus_pending_call_get_completed(pPending) != FALSE; }, [pConnection](int Slice) { return dbus_connection_read_write_dispatch(pConnection, Slice) != FALSE; });
	DBusMessage *pReply = nullptr;
	if(Completed)
		pReply = dbus_pending_call_steal_reply(pPending);
	else
		dbus_pending_call_cancel(pPending);
	dbus_pending_call_unref(pPending);
	if(pReply != nullptr && dbus_message_get_type(pReply) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
	{
		// 播放器已经退出或属性不存在时返回的是错误消息，这里统一当成"没有数据"。
		dbus_message_unref(pReply);
		return nullptr;
	}
	return pReply;
}

static DBusMessage *MprisPropertiesGetAll(DBusConnection *pConnection, const char *pDestination, const char *pInterface)
{
	DBusMessage *pRequest = dbus_message_new_method_call(pDestination, MPRIS_OBJECT_PATH, MPRIS_PROPERTIES_INTERFACE, "GetAll");
	if(pRequest == nullptr)
		return nullptr;
	if(!dbus_message_append_args(pRequest, DBUS_TYPE_STRING, &pInterface, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(pRequest);
		return nullptr;
	}
	return MprisSendBlocking(pConnection, pRequest);
}

static DBusMessage *MprisPropertiesGet(DBusConnection *pConnection, const char *pDestination, const char *pInterface, const char *pProperty)
{
	DBusMessage *pRequest = dbus_message_new_method_call(pDestination, MPRIS_OBJECT_PATH, MPRIS_PROPERTIES_INTERFACE, "Get");
	if(pRequest == nullptr)
		return nullptr;
	if(!dbus_message_append_args(pRequest, DBUS_TYPE_STRING, &pInterface, DBUS_TYPE_STRING, &pProperty, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(pRequest);
		return nullptr;
	}
	return MprisSendBlocking(pConnection, pRequest);
}

// GetAll 的返回值就是 a{sv}，取出字典本体供后面按属性名查找。
static bool MprisPropertyDict(DBusMessage *pReply, DBusMessageIter *pDict)
{
	DBusMessageIter Iterator;
	if(pReply == nullptr || !dbus_message_iter_init(pReply, &Iterator) || dbus_message_iter_get_arg_type(&Iterator) != DBUS_TYPE_ARRAY)
		return false;
	dbus_message_iter_recurse(&Iterator, pDict);
	return true;
}

// 在 a{sv} 里按名字取一个变体，找到后 pValue 指向变体内部的值。
static bool MprisFindEntry(const DBusMessageIter *pDict, const char *pName, DBusMessageIter *pValue)
{
	DBusMessageIter Iterator = *pDict;
	while(dbus_message_iter_get_arg_type(&Iterator) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter Entry;
		dbus_message_iter_recurse(&Iterator, &Entry);
		if(dbus_message_iter_get_arg_type(&Entry) == DBUS_TYPE_STRING)
		{
			const char *pKey = nullptr;
			dbus_message_iter_get_basic(&Entry, &pKey);
			if(pKey != nullptr && str_comp(pKey, pName) == 0 && dbus_message_iter_next(&Entry) && dbus_message_iter_get_arg_type(&Entry) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&Entry, pValue);
				return true;
			}
		}
		dbus_message_iter_next(&Iterator);
	}
	return false;
}

static bool MprisGetString(const DBusMessageIter *pDict, const char *pName, std::string &Value)
{
	DBusMessageIter ValueIterator;
	if(!MprisFindEntry(pDict, pName, &ValueIterator) || dbus_message_iter_get_arg_type(&ValueIterator) != DBUS_TYPE_STRING)
		return false;
	const char *pText = nullptr;
	dbus_message_iter_get_basic(&ValueIterator, &pText);
	if(pText == nullptr)
		return false;
	Value = pText;
	return true;
}

static bool MprisGetTrackId(const DBusMessageIter *pDict, std::string &Value)
{
	DBusMessageIter Iterator;
	if(!MprisFindEntry(pDict, "mpris:trackid", &Iterator) || dbus_message_iter_get_arg_type(&Iterator) != DBUS_TYPE_OBJECT_PATH)
		return false;
	const char *pPath = nullptr;
	dbus_message_iter_get_basic(&Iterator, &pPath);
	if(pPath == nullptr)
		return false;
	Value = pPath;
	return true;
}

// MPRIS 的 xesam:artist 是字符串数组，这里只取第一个，避免拼接出奇怪的署名。
static bool MprisGetStringArrayFirst(const DBusMessageIter *pDict, const char *pName, std::string &Value)
{
	DBusMessageIter ValueIterator;
	if(!MprisFindEntry(pDict, pName, &ValueIterator) || dbus_message_iter_get_arg_type(&ValueIterator) != DBUS_TYPE_ARRAY)
		return false;
	DBusMessageIter ArrayIterator;
	dbus_message_iter_recurse(&ValueIterator, &ArrayIterator);
	if(dbus_message_iter_get_arg_type(&ArrayIterator) != DBUS_TYPE_STRING)
		return false;
	const char *pText = nullptr;
	dbus_message_iter_get_basic(&ArrayIterator, &pText);
	if(pText == nullptr)
		return false;
	Value = pText;
	return true;
}

static bool MprisGetBool(const DBusMessageIter *pDict, const char *pName, bool &Value)
{
	DBusMessageIter ValueIterator;
	if(!MprisFindEntry(pDict, pName, &ValueIterator) || dbus_message_iter_get_arg_type(&ValueIterator) != DBUS_TYPE_BOOLEAN)
		return false;
	dbus_bool_t Raw = FALSE;
	dbus_message_iter_get_basic(&ValueIterator, &Raw);
	Value = Raw != FALSE;
	return true;
}

static bool MprisGetDouble(const DBusMessageIter *pDict, const char *pName, double &Value)
{
	DBusMessageIter ValueIterator;
	if(!MprisFindEntry(pDict, pName, &ValueIterator) || dbus_message_iter_get_arg_type(&ValueIterator) != DBUS_TYPE_DOUBLE)
		return false;
	dbus_message_iter_get_basic(&ValueIterator, &Value);
	return true;
}

static bool MprisGetInt64(const DBusMessageIter *pDict, const char *pName, int64_t &Value)
{
	DBusMessageIter ValueIterator;
	if(!MprisFindEntry(pDict, pName, &ValueIterator) || dbus_message_iter_get_arg_type(&ValueIterator) != DBUS_TYPE_INT64)
		return false;
	dbus_int64_t Raw = 0;
	dbus_message_iter_get_basic(&ValueIterator, &Raw);
	Value = static_cast<int64_t>(Raw);
	return true;
}

static bool MprisListPlayers(DBusConnection *pConnection, std::vector<std::string> &Names)
{
	Names.clear();
	DBusMessage *pRequest = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "ListNames");
	DBusMessage *pReply = MprisSendBlocking(pConnection, pRequest);
	if(pReply == nullptr)
		return false;

	DBusMessageIter Iterator;
	if(dbus_message_iter_init(pReply, &Iterator) && dbus_message_iter_get_arg_type(&Iterator) == DBUS_TYPE_ARRAY)
	{
		DBusMessageIter ArrayIterator;
		dbus_message_iter_recurse(&Iterator, &ArrayIterator);
		while(dbus_message_iter_get_arg_type(&ArrayIterator) == DBUS_TYPE_STRING)
		{
			const char *pName = nullptr;
			dbus_message_iter_get_basic(&ArrayIterator, &pName);
			// 前缀本身（不带播放器名）不是播放器，一并排除。
			if(pName != nullptr && str_startswith(pName, MPRIS_NAME_PREFIX) != nullptr && pName[str_length(MPRIS_NAME_PREFIX)] != '\0')
				Names.emplace_back(pName);
			dbus_message_iter_next(&ArrayIterator);
		}
	}
	dbus_message_unref(pReply);
	return true;
}

// Position 不在 GetAll 的结果里（部分播放器刻意省略），需要单独读一次。
static std::string MprisGetOwner(DBusConnection *pConnection, const char *pName)
{
	DBusMessage *pRequest = dbus_message_new_method_call(DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE_NAME, "GetNameOwner");
	if(pRequest == nullptr)
		return {};
	if(!dbus_message_append_args(pRequest, DBUS_TYPE_STRING, &pName, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(pRequest);
		return {};
	}
	DBusMessage *pReply = MprisSendBlocking(pConnection, pRequest);
	if(pReply == nullptr)
		return {};
	const char *pOwner = nullptr;
	std::string Owner;
	if(dbus_message_get_args(pReply, nullptr, DBUS_TYPE_STRING, &pOwner, DBUS_TYPE_INVALID) && pOwner != nullptr)
		Owner = pOwner;
	dbus_message_unref(pReply);
	return Owner;
}

static bool MprisGetPosition(DBusConnection *pConnection, const char *pDestination, int64_t &PositionUs)
{
	DBusMessage *pReply = MprisPropertiesGet(pConnection, pDestination, MPRIS_PLAYER_INTERFACE, "Position");
	if(pReply == nullptr)
		return false;

	bool Success = false;
	DBusMessageIter Iterator;
	if(dbus_message_iter_init(pReply, &Iterator) && dbus_message_iter_get_arg_type(&Iterator) == DBUS_TYPE_VARIANT)
	{
		DBusMessageIter Variant;
		dbus_message_iter_recurse(&Iterator, &Variant);
		if(dbus_message_iter_get_arg_type(&Variant) == DBUS_TYPE_INT64)
		{
			dbus_int64_t Raw = 0;
			dbus_message_iter_get_basic(&Variant, &Raw);
			PositionUs = static_cast<int64_t>(Raw);
			Success = true;
		}
	}
	dbus_message_unref(pReply);
	return Success;
}

static bool MprisSetVolume(DBusConnection *pConnection, const char *pDestination, double Level)
{
	DBusMessage *pRequest = dbus_message_new_method_call(pDestination, MPRIS_OBJECT_PATH, MPRIS_PROPERTIES_INTERFACE, "Set");
	if(pRequest == nullptr)
		return false;

	const char *pInterface = MPRIS_PLAYER_INTERFACE;
	const char *pProperty = "Volume";
	DBusMessageIter Iterator;
	dbus_message_iter_init_append(pRequest, &Iterator);
	dbus_message_iter_append_basic(&Iterator, DBUS_TYPE_STRING, &pInterface);
	dbus_message_iter_append_basic(&Iterator, DBUS_TYPE_STRING, &pProperty);
	DBusMessageIter Variant;
	dbus_message_iter_open_container(&Iterator, DBUS_TYPE_VARIANT, "d", &Variant);
	dbus_message_iter_append_basic(&Variant, DBUS_TYPE_DOUBLE, &Level);
	dbus_message_iter_close_container(&Iterator, &Variant);

	// 异步发送由短片 dispatch 推进，不在退出路径执行可能阻塞的 flush。
	const bool Sent = dbus_connection_send(pConnection, pRequest, nullptr) != FALSE;
	dbus_message_unref(pRequest);

	return Sent;
}

static void MprisInvokeCommand(DBusConnection *pConnection, const char *pDestination, ECommand Command)
{
	const char *pMember = nullptr;
	switch(Command)
	{
	case ECommand::Prev: pMember = "Previous"; break;
	case ECommand::PlayPause: pMember = "PlayPause"; break;
	case ECommand::Next: pMember = "Next"; break;
	}
	DBusMessage *pRequest = dbus_message_new_method_call(pDestination, MPRIS_OBJECT_PATH, MPRIS_PLAYER_INTERFACE, pMember);
	if(pRequest == nullptr)
		return;
	dbus_connection_send(pConnection, pRequest, nullptr);
	dbus_message_unref(pRequest);
}

static void ResetMprisSharedState(CSystemMediaControls::SShared *pShared)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_State = SPlainState{};
	pShared->m_HasMedia = false;
	pShared->m_PendingVolume.Reset();
	pShared->m_Commands.clear();
}

void CSystemMediaControls::ThreadMain()
{
	// libdbus 要求多线程程序在首次使用前初始化线程支持。
	if(!dbus_threads_init_default())
		return;

	SPlainState State{};
	bool HasMedia = false;
	std::string ActiveName;
	SystemMediaControls::CMprisSessionIdentity SessionIdentity;
	SystemMediaControls::CMprisTrackIdentity TrackIdentity;
	SystemMediaControls::CMprisCandidateScheduler CandidateScheduler;
	s_pMprisStop = &m_StopThread;
	SystemMediaControls::CTimelineGenerationTracker TimelineGenerationTracker;
	std::vector<std::string> Players;

	while(!m_StopThread)
	{
		DBusError Error;
		dbus_error_init(&Error);
		DBusConnection *pConnection = dbus_bus_get_private(DBUS_BUS_SESSION, &Error);
		dbus_error_free(&Error);
		if(pConnection == nullptr)
		{
			// 没有会话总线（例如纯 SSH 会话）时保持空状态，稍后重试。
			if(HasMedia)
				ResetMprisSharedState(m_pShared.get());
			HasMedia = false;
			State = SPlainState{};
			std::this_thread::sleep_for(std::chrono::milliseconds(MPRIS_POLL_INTERVAL_MS));
			continue;
		}
		dbus_connection_set_exit_on_disconnect(pConnection, FALSE);

		while(!m_StopThread)
		{
			if(!g_Config.m_QmSmtcEnable)
			{
				if(HasMedia)
					ResetMprisSharedState(m_pShared.get());
				HasMedia = false;
				State = SPlainState{};
				ActiveName.clear();
				TimelineGenerationTracker.Reset();
				SessionIdentity.Clear();
				TrackIdentity.Clear();
				if(!dbus_connection_read_write_dispatch(pConnection, MPRIS_POLL_INTERVAL_MS))
					break;
				continue;
			}

			const SystemMediaControls::CMprisPollBudget PollBudget(MprisNowMs(), 1000);
			s_pMprisBudget = &PollBudget;
			s_MprisReservedMs = 200;
			MprisListPlayers(pConnection, Players);

			// 一次 GetAll 同时拿到播放状态和全部属性，顺便选出目标播放器。
			DBusMessage *pSelected = nullptr;
			std::string SelectedName;
			std::string SelectedOwner;
			DBusMessageIter SelectedProperties;
			int BestScore = -1;
			CandidateScheduler.Visit(Players, ActiveName, PollBudget, [] { return MprisNowMs(); }, [&] { return m_StopThread.load(); }, [&](const std::string &Name) {
				if(!MprisRequestAllowed())
					return false;
				const std::string Owner = MprisGetOwner(pConnection, Name.c_str());
				if(Owner.empty())
					return true;
				DBusMessage *pReply = MprisPropertiesGetAll(pConnection, Owner.c_str(), MPRIS_PLAYER_INTERFACE);
				DBusMessageIter Properties;
				if(!MprisPropertyDict(pReply, &Properties))
				{
					if(pReply != nullptr)
						dbus_message_unref(pReply);
					return true;
				}
				std::string PlaybackStatus;
				MprisGetString(&Properties, "PlaybackStatus", PlaybackStatus);
				// 选源优先级：正在播放 > 暂停 > 停止/未知。
				// 只认 "Playing" 的话，同时存在多个会话时会选中一个已停止、元数据
				// 全空的播放器（Electron 应用残留的 MPRIS 名字就是这样），
				// 于是能力位全为假、控件全部变灰。同分时保留先出现的那个，
				// 而先出现的已被换成了上一次的活动播放器。
				const int Score = PlaybackStatus == "Playing" ? 2 : (PlaybackStatus == "Paused" ? 1 : 0);
				if(pSelected == nullptr || Score > BestScore)
				{
					if(pSelected != nullptr)
						dbus_message_unref(pSelected);
					pSelected = pReply;
					SelectedName = Name;
					SelectedOwner = Owner;
					SelectedProperties = Properties;
					BestScore = Score;
					if(Score == 2)
						return false;
				}
				else
				{
					dbus_message_unref(pReply);
				}
				return true; });
			s_MprisReservedMs = 0;

			if(m_StopThread.load())
			{
				if(pSelected != nullptr)
					dbus_message_unref(pSelected);
				break;
			}
			if(pSelected == nullptr)
			{
				if(HasMedia)
					ResetMprisSharedState(m_pShared.get());
				HasMedia = false;
				State = SPlainState{};
				ActiveName.clear();
				TimelineGenerationTracker.Reset();
				SessionIdentity.Clear();
				TrackIdentity.Clear();
				if(!dbus_connection_read_write_dispatch(pConnection, MPRIS_POLL_INTERVAL_MS))
					break;
				continue;
			}

			const bool SourceChanged = SessionIdentity.Update(SelectedName, SelectedOwner);
			const SPlainState PreviousState = State;
			// 属性是完整的本轮快照：缺失标题、艺术家、专辑和能力必须清空旧值。
			State = SPlainState{};
			if(SourceChanged)
			{
				// 换播放器时旧标题不能和新来源混在一起，世代号也一起翻新。
				State = SPlainState{};
				ActiveName = SelectedName;
				TimelineGenerationTracker.Reset();
			}
			str_copy(State.m_aSourceAppId, SelectedName.c_str() + str_length(MPRIS_NAME_PREFIX), sizeof(State.m_aSourceAppId));

			DBusMessageIter &Properties = SelectedProperties;

			std::string PlaybackStatus;
			MprisGetString(&Properties, "PlaybackStatus", PlaybackStatus);
			if(PlaybackStatus == "Playing")
			{
				State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Playing;
				State.m_Playing = true;
			}
			else if(PlaybackStatus == "Paused")
			{
				State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Paused;
				State.m_Playing = false;
			}
			else if(PlaybackStatus == "Stopped")
			{
				State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Stopped;
				State.m_Playing = false;
			}
			else
			{
				State.m_PlaybackState = CSystemMediaControls::EPlaybackState::Unknown;
				State.m_Playing = false;
			}

			SystemMediaControls::SMprisPropertiesSnapshot Snapshot;
			MprisGetBool(&Properties, "CanControl", Snapshot.m_CanControl);
			MprisGetBool(&Properties, "CanPlay", Snapshot.m_CanPlay);
			MprisGetBool(&Properties, "CanPause", Snapshot.m_CanPause);
			MprisGetBool(&Properties, "CanGoPrevious", Snapshot.m_CanPrev);
			MprisGetBool(&Properties, "CanGoNext", Snapshot.m_CanNext);
			Snapshot.ApplyControlAvailability();
			State.m_CanPlay = Snapshot.m_CanPlay;
			State.m_CanPause = Snapshot.m_CanPause;
			State.m_CanPrev = Snapshot.m_CanPrev;
			State.m_CanNext = Snapshot.m_CanNext;
			double PlaybackRate = 1.0;
			State.m_PlaybackRate = MprisGetDouble(&Properties, "Rate", PlaybackRate) && std::isfinite(PlaybackRate) && PlaybackRate > 0.0 ? PlaybackRate : 1.0;

			double Volume = 0.0;
			if(MprisGetDouble(&Properties, "Volume", Volume) && std::isfinite(Volume))
			{
				// 规范里 Volume 只在 CanControl 为真时可写，没有单独的 CanSetVolume。
				State.m_CanSetVolume = Snapshot.m_CanControl;
				State.m_Volume = static_cast<float>(std::clamp(Volume, 0.0, 1.0));
			}
			else
			{
				State.m_CanSetVolume = false;
				State.m_Volume = 0.0f;
			}
			State.m_VolumeGeneration = SessionIdentity.Generation();

			std::optional<QmMediaVolume::SRequest> VolumeRequest;
			{
				std::scoped_lock Lock(m_pShared->m_Mutex);
				VolumeRequest = m_pShared->m_PendingVolume.Take(SessionIdentity.Generation());
			}
			if(VolumeRequest.has_value() && State.m_CanSetVolume)
			{
				MprisSetVolume(pConnection, SelectedOwner.c_str(), VolumeRequest->m_Level);
				State.m_Volume = VolumeRequest->m_Level;
			}

			int64_t Duration100ns = 0;
			DBusMessageIter Metadata;
			if(MprisFindEntry(&Properties, "Metadata", &Metadata) && dbus_message_iter_get_arg_type(&Metadata) == DBUS_TYPE_ARRAY)
			{
				DBusMessageIter MetadataDict;
				dbus_message_iter_recurse(&Metadata, &MetadataDict);
				MprisGetString(&MetadataDict, "xesam:title", Snapshot.m_Title);
				MprisGetStringArrayFirst(&MetadataDict, "xesam:artist", Snapshot.m_Artist);
				MprisGetString(&MetadataDict, "xesam:album", Snapshot.m_Album);
				MprisGetTrackId(&MetadataDict, Snapshot.m_TrackId);

				int64_t LengthUs = 0;
				if(MprisGetInt64(&MetadataDict, "mpris:length", LengthUs) && LengthUs > 0)
					Duration100ns = SystemMediaControls::MprisMicrosecondsTo100ns(LengthUs);
			}

			str_copy(State.m_aTitle, Snapshot.m_Title.c_str(), sizeof(State.m_aTitle));
			str_copy(State.m_aArtist, Snapshot.m_Artist.c_str(), sizeof(State.m_aArtist));
			str_copy(State.m_aAlbum, Snapshot.m_Album.c_str(), sizeof(State.m_aAlbum));
			if(SourceChanged)
				TrackIdentity.Clear();
			const bool TrackChanged = TrackIdentity.Update(Snapshot);
			if(TrackChanged)
				TimelineGenerationTracker.Reset();
			int64_t PositionUs = 0;
			const bool HasPosition = MprisGetPosition(pConnection, SelectedOwner.c_str(), PositionUs);

			// Position 是查询当刻的实时值，因此不做 LastUpdatedTime 折算。
			SystemMediaControls::STimelineProperties TimelineProperties;
			TimelineProperties.m_Start100ns = 0;
			TimelineProperties.m_End100ns = Duration100ns;
			TimelineProperties.m_Position100ns = SystemMediaControls::MprisMicrosecondsTo100ns(PositionUs);
			TimelineProperties.m_LastUpdatedUtc100ns = 0;
			const SystemMediaControls::STimelineSnapshot TimelineSnapshot = SystemMediaControls::NormalizeTimelineProperties(
				TimelineProperties, 0, time_get_impl(), time_freq());
			State.m_DurationMs = TimelineSnapshot.m_DurationMs;
			const SystemMediaControls::SMprisPosition Position = SystemMediaControls::MprisResolvePosition(SourceChanged || TrackChanged, HasPosition,
				{PreviousState.m_PositionMs, PreviousState.m_PositionUpdatedTick, PreviousState.m_TimelineGeneration},
				{TimelineSnapshot.m_PositionMs, TimelineSnapshot.m_PositionUpdatedTick, HasPosition ? TimelineGenerationTracker.Update(TimelineProperties) : 0});
			State.m_PositionMs = Position.m_PositionMs;
			State.m_PositionUpdatedTick = Position.m_UpdatedTick;
			State.m_TimelineGeneration = Position.m_Generation;

			HasMedia = true;
			{
				std::scoped_lock Lock(m_pShared->m_Mutex);
				m_pShared->m_State = State;
				m_pShared->m_HasMedia = true;
			}

			std::deque<SMediaCommand> Commands;
			{
				std::scoped_lock Lock(m_pShared->m_Mutex);
				Commands.swap(m_pShared->m_Commands);
			}
			for(const SMediaCommand &Command : Commands)
			{
				if(!g_Config.m_QmSmtcEnable || !SessionIdentity.Accepts(Command.m_Generation))
					continue;
				if(m_StopThread.load())
					break;
				MprisInvokeCommand(pConnection, SelectedOwner.c_str(), Command.m_Type);
			}

			dbus_message_unref(pSelected);

			// 带超时的读取代替 sleep：既不空转，又能及时清掉连接队列里的信号。
			if(!dbus_connection_read_write_dispatch(pConnection, MPRIS_POLL_INTERVAL_MS))
				break;
		}

		if(HasMedia)
			ResetMprisSharedState(m_pShared.get());
		HasMedia = false;
		State = SPlainState{};
		ActiveName.clear();
		TimelineGenerationTracker.Reset();
		SessionIdentity.Clear();
		TrackIdentity.Clear();
		dbus_connection_close(pConnection);
		dbus_connection_unref(pConnection);
	}
	s_pMprisBudget = nullptr;
	s_MprisReservedMs = 0;
	s_pMprisStop = nullptr;
}
#endif

void CSystemMediaControls::OnInit()
{
	m_pNeteaseHook = std::make_unique<CQmNeteaseHookProvider>();
	m_LastNeteaseHookReadFrame = 0;
	m_NeteaseHookReadFrameInitialized = false;
	SyncNeteaseHookConfiguration();
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	m_pShared = std::make_unique<SShared>();
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	m_pWinrt = std::make_unique<SWinrt>();
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	m_pMpris = std::make_unique<SMpris>();
#endif
	m_StopThread = false;
	m_Thread = std::thread(&CSystemMediaControls::ThreadMain, this);
#endif
}

void CSystemMediaControls::OnShutdown()
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	m_StopThread = true;
	if(m_Thread.joinable())
	{
		m_Thread.join();
	}
	m_pShared.reset();
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(m_pWinrt)
	{
		ClearState(m_pWinrt->m_State, Graphics());
		m_pWinrt.reset();
	}
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	if(m_pMpris)
	{
		ClearState(m_pMpris->m_State, Graphics());
		m_pMpris.reset();
	}
#endif
#endif
	if(m_pNeteaseHook)
	{
		m_pNeteaseHook->Stop();
		m_pNeteaseHook.reset();
	}
	m_NeteaseHookConfigInitialized = false;
	m_LastNeteaseHookEnabled = false;
	m_LastNeteaseHookHelperPath.clear();
	m_HasNeteaseSnapshot = false;
	m_NeteaseSnapshot = {};
	m_LastNeteaseHookReadFrame = 0;
	m_NeteaseHookReadFrameInitialized = false;
}

void CSystemMediaControls::OnUpdate()
{
	SyncNeteaseHookConfiguration();
	// v5 是网易云私有补充数据。只在短帧窗口到期时读取，避免主线程每帧
	// 重试共享内存、复制快照和计算校验和。
	if(!m_LastNeteaseHookEnabled)
	{
		if(m_HasNeteaseSnapshot)
		{
			m_HasNeteaseSnapshot = false;
			m_NeteaseSnapshot = {};
		}
		if(m_NeteaseHookReadFrameInitialized)
		{
			m_LastNeteaseHookReadFrame = 0;
			m_NeteaseHookReadFrameInitialized = false;
		}
	}
	else if(m_pNeteaseHook)
	{
		const uint64_t CurrentFrame = Client() != nullptr ? Client()->PerfFrame() : 0;
		if(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(CurrentFrame, m_LastNeteaseHookReadFrame, m_NeteaseHookReadFrameInitialized))
		{
			m_LastNeteaseHookReadFrame = CurrentFrame;
			m_NeteaseHookReadFrameInitialized = true;
			QmNeteaseHook::SSnapshotV5 Snapshot{};
			if(m_pNeteaseHook->ReadV5(&Snapshot, g_Config.m_QmNeteaseHookTimeoutMs))
			{
				m_NeteaseSnapshot = Snapshot;
				m_HasNeteaseSnapshot = true;
			}
			else
			{
				m_NeteaseSnapshot = {};
				m_HasNeteaseSnapshot = false;
			}
		}
	}
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	SState *pMainState = MainState();
	if(pMainState == nullptr)
		return;

	if(!g_Config.m_QmSmtcEnable)
	{
		if(MainHasMedia())
		{
			ClearState(*pMainState, Graphics());
			SetMainHasMedia(false);
		}
		return;
	}

	if(!m_pShared)
		return;

	SPlainState SharedState{};
	bool HasMedia = false;
	{
		std::scoped_lock Lock(m_pShared->m_Mutex);
		SharedState = m_pShared->m_State;
		HasMedia = m_pShared->m_HasMedia;
	}

	if(!HasMedia)
	{
		if(MainHasMedia())
			ClearState(*pMainState, Graphics());
		SetMainHasMedia(false);
	}
	else
	{
		SetMainHasMedia(true);
		ApplySharedState(SharedState, *pMainState);
	}

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	ApplySharedAlbumArt(m_pShared.get(), *pMainState, Graphics(), Client());
#endif
#endif
}

bool CSystemMediaControls::GetStateSnapshot(SState &State) const
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	if(!g_Config.m_QmSmtcEnable)
	{
		State = SState{};
		return false;
	}

	if(const SState *pMainState = MainState(); pMainState != nullptr && MainHasMedia())
	{
		State = *pMainState;
		return true;
	}
#endif
	State = SState{};
	return false;
}

#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
CSystemMediaControls::SState *CSystemMediaControls::MainState()
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(m_pWinrt)
		return &m_pWinrt->m_State;
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	if(m_pMpris)
		return &m_pMpris->m_State;
#endif
	return nullptr;
}

const CSystemMediaControls::SState *CSystemMediaControls::MainState() const
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(m_pWinrt)
		return &m_pWinrt->m_State;
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	if(m_pMpris)
		return &m_pMpris->m_State;
#endif
	return nullptr;
}

bool CSystemMediaControls::MainHasMedia() const
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(m_pWinrt)
		return m_pWinrt->m_HasMedia;
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	if(m_pMpris)
		return m_pMpris->m_HasMedia;
#endif
	return false;
}

void CSystemMediaControls::SetMainHasMedia(bool HasMedia)
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(m_pWinrt)
		m_pWinrt->m_HasMedia = HasMedia;
#endif
#if SYSTEM_MEDIA_CONTROLS_MPRIS_ENABLED
	if(m_pMpris)
		m_pMpris->m_HasMedia = HasMedia;
#endif
}

const CSystemMediaControls::SState *CSystemMediaControls::ActiveState() const
{
	if(!g_Config.m_QmSmtcEnable || !m_pShared || !MainHasMedia())
		return nullptr;
	return MainState();
}
#endif

bool CSystemMediaControls::GetNeteaseSnapshot(QmNeteaseHook::SSnapshotV5 &Snapshot) const
{
	if(!m_HasNeteaseSnapshot)
	{
		Snapshot = {};
		return false;
	}
	Snapshot = m_NeteaseSnapshot;
	return true;
}

void CSystemMediaControls::Previous()
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	const SState *pMainState = ActiveState();
	if(pMainState == nullptr)
		return;
	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back({ECommand::Prev, pMainState->m_aSourceAppId, pMainState->m_VolumeGeneration});
#endif
}

void CSystemMediaControls::PlayPause()
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	const SState *pMainState = ActiveState();
	if(pMainState == nullptr)
		return;
	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back({ECommand::PlayPause, pMainState->m_aSourceAppId, pMainState->m_VolumeGeneration});
#endif
}

void CSystemMediaControls::Next()
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	const SState *pMainState = ActiveState();
	if(pMainState == nullptr)
		return;
	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back({ECommand::Next, pMainState->m_aSourceAppId, pMainState->m_VolumeGeneration});
#endif
}

void CSystemMediaControls::SetVolume(uint64_t Generation, float Volume)
{
#if SYSTEM_MEDIA_CONTROLS_BACKEND_ENABLED
	if(!g_Config.m_QmSmtcEnable || !m_pShared)
		return;
	std::scoped_lock Lock(m_pShared->m_Mutex);
	if(m_pShared->m_HasMedia && m_pShared->m_State.m_CanSetVolume && m_pShared->m_State.m_VolumeGeneration == Generation)
		m_pShared->m_PendingVolume.Set(Generation, Volume);
#endif
}
