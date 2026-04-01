#include "system_media_controls.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <base/system.h>

#include <engine/image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#endif

#if defined(CONF_FAMILY_WINDOWS)
using namespace winrt::Windows::Media::Control;

struct CSystemMediaControls::SWinrt
{
	CSystemMediaControls::SState m_State{};
	bool m_HasMedia = false;
};

struct SPlainState
{
	bool m_CanPlay = false;
	bool m_CanPause = false;
	bool m_CanPrev = false;
	bool m_CanNext = false;
	bool m_Playing = false;
	char m_aTitle[128] = {};
	char m_aArtist[128] = {};
	char m_aAlbum[128] = {};
	int64_t m_PositionMs = 0;
	int64_t m_DurationMs = 0;
};

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
	pWinrt->m_State.m_AlbumArt.Invalidate();
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
	pShared->m_AlbumArtWidth = 0;
	pShared->m_AlbumArtHeight = 0;
	pShared->m_AlbumArtDirty = true;
}

static void SetSharedAlbumArt(CSystemMediaControls::SShared *pShared, std::vector<uint8_t> &&Pixels, int Width, int Height)
{
	std::scoped_lock Lock(pShared->m_Mutex);
	pShared->m_AlbumArtRgba = std::move(Pixels);
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
		const uint32_t Width = Decoder.PixelWidth();
		const uint32_t Height = Decoder.PixelHeight();
		if(Width == 0 || Height == 0)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		const auto PixelDataOp = Decoder.GetPixelDataAsync(
			winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Rgba8,
			winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied,
			winrt::Windows::Graphics::Imaging::BitmapTransform(),
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
		const size_t ExpectedSize = (size_t)Width * (size_t)Height * 4;
		if(Pixels.size() < ExpectedSize)
		{
			ClearSharedAlbumArt(pShared);
			return;
		}

		std::vector<uint8_t> Copy(Pixels.begin(), Pixels.begin() + ExpectedSize);
		SetSharedAlbumArt(pShared, std::move(Copy), (int)Width, (int)Height);
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
				Pixels[Index + 0] = 0;
				Pixels[Index + 1] = 0;
				Pixels[Index + 2] = 0;
				Pixels[Index + 3] = 0;
			}
			else if(Alpha < 1.0f)
			{
				Pixels[Index + 0] = (uint8_t)std::round(Pixels[Index + 0] * Alpha);
				Pixels[Index + 1] = (uint8_t)std::round(Pixels[Index + 1] * Alpha);
				Pixels[Index + 2] = (uint8_t)std::round(Pixels[Index + 2] * Alpha);
				Pixels[Index + 3] = (uint8_t)std::round(Pixels[Index + 3] * Alpha);
			}
		}
	}
}

static void ApplySharedAlbumArt(CSystemMediaControls::SShared *pShared, CSystemMediaControls::SWinrt *pWinrt, IGraphics *pGraphics)
{
	if(!pShared || !pWinrt || !pGraphics)
		return;

	bool AlbumArtDirty = false;
	int AlbumArtWidth = 0;
	int AlbumArtHeight = 0;
	std::vector<uint8_t> AlbumArtPixels;
	{
		std::scoped_lock Lock(pShared->m_Mutex);
		if(pShared->m_AlbumArtDirty)
		{
			AlbumArtDirty = true;
			AlbumArtWidth = pShared->m_AlbumArtWidth;
			AlbumArtHeight = pShared->m_AlbumArtHeight;
			AlbumArtPixels = std::move(pShared->m_AlbumArtRgba);
			pShared->m_AlbumArtRgba.clear();
			pShared->m_AlbumArtDirty = false;
		}
	}

	if(!AlbumArtDirty)
		return;

	ClearAlbumArtLocal(pWinrt, pGraphics);

	const size_t ExpectedSize = (size_t)AlbumArtWidth * (size_t)AlbumArtHeight * 4;
	if(AlbumArtWidth > 0 && AlbumArtHeight > 0 && AlbumArtPixels.size() >= ExpectedSize)
	{
		const float RoundingRatio = 2.0f / 14.0f;
		const float Radius = (float)std::min(AlbumArtWidth, AlbumArtHeight) * RoundingRatio;
		ApplyRoundedMask(AlbumArtPixels, AlbumArtWidth, AlbumArtHeight, Radius);

		CImageInfo Image;
		Image.m_Width = (size_t)AlbumArtWidth;
		Image.m_Height = (size_t)AlbumArtHeight;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(ExpectedSize));
		if(Image.m_pData)
		{
			mem_copy(Image.m_pData, AlbumArtPixels.data(), ExpectedSize);
			pWinrt->m_State.m_AlbumArt = pGraphics->LoadTextureRawMove(Image, 0, "smtc_album_art");
			if(pWinrt->m_State.m_AlbumArt.IsValid())
			{
				pWinrt->m_State.m_AlbumArtWidth = AlbumArtWidth;
				pWinrt->m_State.m_AlbumArtHeight = AlbumArtHeight;
			}
		}
	}
}
#endif

CSystemMediaControls::CSystemMediaControls() = default;
CSystemMediaControls::~CSystemMediaControls() = default;

#if defined(CONF_FAMILY_WINDOWS)
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

	GlobalSystemMediaTransportControlsSessionManager Manager{nullptr};
	GlobalSystemMediaTransportControlsSession Session{nullptr};
	SPlainState State{};
	bool HasMedia = false;
	std::string AlbumArtKey;
	auto LastPropsUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(2);

	while(!m_StopThread)
	{
		try
		{
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
			State.m_Playing = PlaybackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

			const auto Timeline = Session.GetTimelineProperties();
			if(!Timeline)
			{
				if(HasMedia)
					ResetSharedState(m_pShared.get(), State, HasMedia, AlbumArtKey);
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				continue;
			}
			const int64_t Start100ns = Timeline.StartTime().count();
			const int64_t End100ns = Timeline.EndTime().count();
			const int64_t Position100ns = Timeline.Position().count();
			const int64_t Duration100ns = End100ns - Start100ns;
			const int64_t PositionRel100ns = Position100ns - Start100ns;
			State.m_DurationMs = Duration100ns > 0 ? Duration100ns / 10000 : 0;
			State.m_PositionMs = PositionRel100ns > 0 ? PositionRel100ns / 10000 : 0;
			HasMedia = true;

			const auto Now = std::chrono::steady_clock::now();
			if(Now - LastPropsUpdate >= std::chrono::seconds(1))
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
							const std::string Artist = winrt::to_string(MediaProps.Artist());
							const std::string Album = winrt::to_string(MediaProps.AlbumTitle());

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
								const std::string NewKey = Title + "\n" + Artist + "\n" + Album;
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

	winrt::uninit_apartment();
}
#endif

void CSystemMediaControls::OnInit()
{
#if defined(CONF_FAMILY_WINDOWS)
	m_pWinrt = std::make_unique<SWinrt>();
	m_pShared = std::make_unique<SShared>();
	m_StopThread = false;
	m_Thread = std::thread(&CSystemMediaControls::ThreadMain, this);
#endif
}

void CSystemMediaControls::OnShutdown()
{
#if defined(CONF_FAMILY_WINDOWS)
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
}

void CSystemMediaControls::OnUpdate()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!m_pWinrt)
		return;

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
		m_pWinrt->m_State.m_Playing = SharedState.m_Playing;
		str_copy(m_pWinrt->m_State.m_aTitle, SharedState.m_aTitle, sizeof(m_pWinrt->m_State.m_aTitle));
		str_copy(m_pWinrt->m_State.m_aArtist, SharedState.m_aArtist, sizeof(m_pWinrt->m_State.m_aArtist));
		str_copy(m_pWinrt->m_State.m_aAlbum, SharedState.m_aAlbum, sizeof(m_pWinrt->m_State.m_aAlbum));
		m_pWinrt->m_State.m_PositionMs = SharedState.m_PositionMs;
		m_pWinrt->m_State.m_DurationMs = SharedState.m_DurationMs;
	}

	ApplySharedAlbumArt(m_pShared.get(), m_pWinrt.get(), Graphics());
#endif
}

bool CSystemMediaControls::GetStateSnapshot(SState &State) const
{
#if defined(CONF_FAMILY_WINDOWS)
	if(m_pWinrt && m_pWinrt->m_HasMedia)
	{
		State = m_pWinrt->m_State;
		return true;
	}
#endif
	State = SState{};
	return false;
}

void CSystemMediaControls::Previous()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Prev);
#endif
}

void CSystemMediaControls::PlayPause()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::PlayPause);
#endif
}

void CSystemMediaControls::Next()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!m_pShared)
		return;

	std::scoped_lock Lock(m_pShared->m_Mutex);
	m_pShared->m_Commands.push_back(ECommand::Next);
#endif
}
