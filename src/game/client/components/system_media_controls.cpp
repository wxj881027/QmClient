// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "system_media_controls.h"

#include "system_media_controls_timeline.h"

#include <base/time.h>

// SyncNeteaseHookConfiguration 在非 WINRT 平台也会编译，需无条件提供 g_Config
#include <engine/shared/config.h>

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>

#include <game/client/components/qmclient/perf_logging.h>

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
#include <mutex>
#include <string>
#include <vector>
#endif

#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
using namespace winrt::Windows::Media::Control;

struct CSystemMediaControls::SWinrt
{
	CSystemMediaControls::SState m_State{};
	bool m_HasMedia = false;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SPlainState
{
	bool m_CanPlay = false;
	bool m_CanPause = false;
	bool m_CanPrev = false;
	bool m_CanNext = false;
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

struct CSystemMediaControls::SShared
{
	std::mutex m_Mutex;
	SPlainState m_State{};
	bool m_HasMedia = false;
	std::deque<ECommand> m_Commands;
	std::vector<uint8_t> m_AlbumArtRgba;
	std::vector<uint8_t> m_AlbumArtCircularRgba;
	int m_AlbumArtWidth = 0;
	int m_AlbumArtHeight = 0;
	bool m_AlbumArtDirty = false;
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

static void ClearAlbumArtLocal(CSystemMediaControls::SWinrt *pWinrt, IGraphics *pGraphics)
{
	if(pGraphics && pWinrt->m_State.m_AlbumArt.IsValid())
	{
		pGraphics->UnloadTexture(&pWinrt->m_State.m_AlbumArt);
	}
	if(pGraphics && pWinrt->m_State.m_AlbumArtCircular.IsValid())
	{
		pGraphics->UnloadTexture(&pWinrt->m_State.m_AlbumArtCircular);
	}
	pWinrt->m_State.m_AlbumArt.Invalidate();
	pWinrt->m_State.m_AlbumArtCircular.Invalidate();
	pWinrt->m_State.m_AlbumArtWidth = 0;
	pWinrt->m_State.m_AlbumArtHeight = 0;
}

static void ClearState(CSystemMediaControls::SWinrt *pWinrt, IGraphics *pGraphics)
{
	ClearAlbumArtLocal(pWinrt, pGraphics);
	pWinrt->m_State = CSystemMediaControls::SState{};
	pWinrt->m_HasMedia = false;
}

static void ClearSharedAlbumArt(CSystemMediaControls::SShared *pShared)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_AlbumArtRgba.clear();
	pShared->m_AlbumArtCircularRgba.clear();
	pShared->m_AlbumArtWidth = 0;
	pShared->m_AlbumArtHeight = 0;
	pShared->m_AlbumArtDirty = true;
}

static void SetSharedAlbumArt(CSystemMediaControls::SShared *pShared, std::vector<uint8_t> &&Pixels, std::vector<uint8_t> &&CircularPixels, int Width, int Height)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_AlbumArtRgba = std::move(Pixels);
	pShared->m_AlbumArtCircularRgba = std::move(CircularPixels);
	pShared->m_AlbumArtWidth = Width;
	pShared->m_AlbumArtHeight = Height;
	pShared->m_AlbumArtDirty = true;
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
		SetSharedAlbumArt(pShared, std::move(Copy), std::move(CircularCopy), (int)DecodeSize.m_Width, (int)DecodeSize.m_Height);
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

static IGraphics::CTextureHandle LoadAlbumArtTexture(IGraphics *pGraphics, const std::vector<uint8_t> &Pixels, int Width, int Height, const char *pName)
{
	if(pGraphics == nullptr || Width <= 0 || Height <= 0)
		return {};
	const size_t ExpectedSize = (size_t)Width * (size_t)Height * 4;
	if(Pixels.size() < ExpectedSize)
		return {};

	CImageInfo Image;
	Image.m_Width = (size_t)Width;
	Image.m_Height = (size_t)Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = static_cast<uint8_t *>(malloc(ExpectedSize));
	if(!Image.m_pData)
		return {};

	mem_copy(Image.m_pData, Pixels.data(), ExpectedSize);
	return pGraphics->LoadTextureRawMove(Image, 0, pName);
}

static void ApplySharedAlbumArt(CSystemMediaControls::SShared *pShared, CSystemMediaControls::SWinrt *pWinrt, IGraphics *pGraphics, const IClient *pClient)
{
	if(!pShared || !pWinrt || !pGraphics)
		return;

	bool AlbumArtDirty = false;
	int AlbumArtWidth = 0;
	int AlbumArtHeight = 0;
	std::vector<uint8_t> AlbumArtPixels;
	std::vector<uint8_t> AlbumArtCircularPixels;
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		if(pShared->m_AlbumArtDirty)
		{
			AlbumArtDirty = true;
			AlbumArtWidth = pShared->m_AlbumArtWidth;
			AlbumArtHeight = pShared->m_AlbumArtHeight;
			AlbumArtPixels = std::move(pShared->m_AlbumArtRgba);
			AlbumArtCircularPixels = std::move(pShared->m_AlbumArtCircularRgba);
			pShared->m_AlbumArtRgba.clear();
			pShared->m_AlbumArtCircularRgba.clear();
			pShared->m_AlbumArtDirty = false;
		}
	}

	if(!AlbumArtDirty)
		return;
	CPerfTimer ApplyTimer;

	ClearAlbumArtLocal(pWinrt, pGraphics);

	pWinrt->m_State.m_AlbumArt = LoadAlbumArtTexture(pGraphics, AlbumArtPixels, AlbumArtWidth, AlbumArtHeight, "smtc_album_art");
	pWinrt->m_State.m_AlbumArtCircular = LoadAlbumArtTexture(pGraphics, AlbumArtCircularPixels, AlbumArtWidth, AlbumArtHeight, "smtc_album_art_circular");
	if(pWinrt->m_State.m_AlbumArt.IsValid())
	{
		pWinrt->m_State.m_AlbumArtWidth = AlbumArtWidth;
		pWinrt->m_State.m_AlbumArtHeight = AlbumArtHeight;
	}

	char aExtra[128];
	str_format(aExtra, sizeof(aExtra), "width=%d height=%d valid=%d circular_valid=%d", AlbumArtWidth, AlbumArtHeight, pWinrt->m_State.m_AlbumArt.IsValid() ? 1 : 0, pWinrt->m_State.m_AlbumArtCircular.IsValid() ? 1 : 0);
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
		auto LastPropsUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(2);

		while(!m_StopThread)
		{
			try
			{
				if(!g_Config.m_QmSmtcEnable)
				{
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

				std::deque<ECommand> Commands;
				{
					std::scoped_lock Lock(m_pShared->m_Mutex);
					Commands.swap(m_pShared->m_Commands);
				}
				if(Session)
				{
					for(const auto Command : Commands)
					{
						try
						{
							switch(Command)
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
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}
			catch(...)
			{
				ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}

	winrt::uninit_apartment();
}
#endif

void CSystemMediaControls::OnInit()
{
	m_pNeteaseHook = std::make_unique<CQmNeteaseHookProvider>();
	m_LastNeteaseHookReadFrame = 0;
	m_NeteaseHookReadFrameInitialized = false;
	SyncNeteaseHookConfiguration();
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	m_pWinrt = std::make_unique<SWinrt>();
	m_pShared = std::make_unique<SShared>();
	m_StopThread = false;
	m_Thread = std::thread(&CSystemMediaControls::ThreadMain, this);
#endif
}

void CSystemMediaControls::OnShutdown()
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	m_StopThread = true;
	if(m_Thread.joinable())
	{
		m_Thread.join();
	}
	m_pShared.reset();
	if(m_pWinrt)
	{
		ClearState(m_pWinrt.get(), Graphics());
		m_pWinrt.reset();
	}
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
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(!m_pWinrt)
		return;

	if(!g_Config.m_QmSmtcEnable)
	{
		if(m_pWinrt->m_HasMedia)
			ClearState(m_pWinrt.get(), Graphics());
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
		if(m_pWinrt->m_HasMedia)
			ClearState(m_pWinrt.get(), Graphics());
		m_pWinrt->m_HasMedia = false;
	}
	else
	{
		m_pWinrt->m_HasMedia = true;
		m_pWinrt->m_State.m_CanPlay = SharedState.m_CanPlay;
		m_pWinrt->m_State.m_CanPause = SharedState.m_CanPause;
		m_pWinrt->m_State.m_CanPrev = SharedState.m_CanPrev;
		m_pWinrt->m_State.m_CanNext = SharedState.m_CanNext;
		m_pWinrt->m_State.m_PlaybackState = SharedState.m_PlaybackState;
		m_pWinrt->m_State.m_Playing = SharedState.m_Playing;
		str_copy(m_pWinrt->m_State.m_aSourceAppId, SharedState.m_aSourceAppId, sizeof(m_pWinrt->m_State.m_aSourceAppId));
		str_copy(m_pWinrt->m_State.m_aTitle, SharedState.m_aTitle, sizeof(m_pWinrt->m_State.m_aTitle));
		str_copy(m_pWinrt->m_State.m_aArtist, SharedState.m_aArtist, sizeof(m_pWinrt->m_State.m_aArtist));
		str_copy(m_pWinrt->m_State.m_aAlbum, SharedState.m_aAlbum, sizeof(m_pWinrt->m_State.m_aAlbum));
		m_pWinrt->m_State.m_PositionMs = SharedState.m_PositionMs;
		m_pWinrt->m_State.m_DurationMs = SharedState.m_DurationMs;
		m_pWinrt->m_State.m_PositionUpdatedTick = SharedState.m_PositionUpdatedTick;
		m_pWinrt->m_State.m_TimelineGeneration = SharedState.m_TimelineGeneration;
		m_pWinrt->m_State.m_PlaybackRate = SharedState.m_PlaybackRate;
	}

	ApplySharedAlbumArt(m_pShared.get(), m_pWinrt.get(), Graphics(), Client());

#endif
}

bool CSystemMediaControls::GetStateSnapshot(SState &State) const
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(!g_Config.m_QmSmtcEnable)
	{
		State = SState{};
		return false;
	}

	if(m_pWinrt && m_pWinrt->m_HasMedia)
	{
		State = m_pWinrt->m_State;
		return true;
	}
#endif
	State = SState{};
	return false;
}

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
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(!g_Config.m_QmSmtcEnable)
		return;

	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Prev);
#endif
}

void CSystemMediaControls::PlayPause()
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(!g_Config.m_QmSmtcEnable)
		return;

	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::PlayPause);
#endif
}

void CSystemMediaControls::Next()
{
#if SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED
	if(!g_Config.m_QmSmtcEnable)
		return;

	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Next);
#endif
}
