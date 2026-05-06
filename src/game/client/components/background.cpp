#include "background.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <engine/shared/config.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

#if defined(CONF_VIDEORECORDER)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace
{
constexpr int MAX_BACKGROUND_VIDEO_DIMENSION = 4096;
constexpr int64_t MAX_BACKGROUND_VIDEO_PIXELS = (int64_t)MAX_BACKGROUND_VIDEO_DIMENSION * (int64_t)MAX_BACKGROUND_VIDEO_DIMENSION;

bool TryMigrateLegacyEntityBgMapPath(IStorage *pStorage, const char *pManagedPath)
{
	if(pStorage == nullptr || pManagedPath == nullptr || !str_endswith_nocase(pManagedPath, ".map"))
		return false;
	if(pStorage->FileExists(pManagedPath, IStorage::TYPE_ALL))
		return true;

	char aLegacyPath[IO_MAX_PATH_LENGTH];
	str_copy(aLegacyPath, pManagedPath, sizeof(aLegacyPath));
	aLegacyPath[str_length(aLegacyPath) - 4] = '\0';

	for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
	{
		char aCandidatePath[IO_MAX_PATH_LENGTH];
		str_format(aCandidatePath, sizeof(aCandidatePath), "%s%s", aLegacyPath, pExtension);
		if(pStorage->FileExists(aCandidatePath, IStorage::TYPE_SAVE))
		{
			if(pStorage->RenameFile(aCandidatePath, pManagedPath, IStorage::TYPE_SAVE))
				return true;
		}
	}

	return pStorage->FileExists(pManagedPath, IStorage::TYPE_ALL);
}

void ResolveBackgroundEntitiesStoragePath(IStorage *pStorage, const char *pBackgroundEntities, const char *pDefaultExtension, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(pBackgroundEntities == nullptr || pBackgroundEntities[0] == '\0')
		return;

	const char *pExtension = FindBackgroundFileExtension(pBackgroundEntities);
	if(pExtension == nullptr)
		pExtension = pDefaultExtension;
	if(str_startswith_nocase(pBackgroundEntities, "entity_bg/"))
	{
		char aManagedPath[IO_MAX_PATH_LENGTH];
		char aMapPath[IO_MAX_PATH_LENGTH];
		str_format(aManagedPath, sizeof(aManagedPath), "assets/%s%s", pBackgroundEntities, str_endswith_nocase(pBackgroundEntities, pExtension) ? "" : pExtension);
		str_format(aMapPath, sizeof(aMapPath), "maps/%s%s", pBackgroundEntities, str_endswith_nocase(pBackgroundEntities, pExtension) ? "" : pExtension);

		const bool IsMapFile = str_comp_nocase(pExtension, ".map") == 0;
		const bool ManagedExists = pStorage != nullptr && (IsMapFile ? TryMigrateLegacyEntityBgMapPath(pStorage, aManagedPath) : pStorage->FileExists(aManagedPath, IStorage::TYPE_ALL));
		const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, IStorage::TYPE_ALL);
		if(ManagedExists || !MapExists)
			str_copy(pOut, aManagedPath, OutSize);
		else
			str_copy(pOut, aMapPath, OutSize);
	}
	else
	{
		str_format(pOut, OutSize, "maps/%s%s", pBackgroundEntities, str_endswith_nocase(pBackgroundEntities, pExtension) ? "" : pExtension);
	}
}

bool MakeStoragePath(IStorage *pStorage, const char *pPath, char *pOut, int OutSize)
{
	if(pStorage == nullptr || pPath == nullptr || pPath[0] == '\0')
		return false;

	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL, pOut, OutSize);
	if(!File)
		return false;
	io_close(File);
	return pOut[0] != '\0';
}
}

CBackground::CBackground(ERenderType MapType, bool OnlineOnly) :
	CMapLayers(MapType, OnlineOnly)
{
	m_pLayers = new CLayers;
	m_pBackgroundLayers = m_pLayers;
	m_pImages = new CMapImages;
	m_pBackgroundImages = m_pImages;
	m_Loaded = false;
	m_ImageBackground = false;
	m_aMapName[0] = '\0';
	m_BackgroundTexture.Invalidate();
}

CBackground::~CBackground()
{
	ClearImageBackground(false);
	ClearVideoBackground(false);
	delete m_pBackgroundLayers;
	delete m_pBackgroundImages;
}

CBackgroundEngineMap *CBackground::CreateBGMap()
{
	return new CBackgroundEngineMap;
}

void CBackground::ClearImageBackground(bool UnloadTexture)
{
	if(UnloadTexture && m_BackgroundTexture.IsValid())
		Graphics()->UnloadTexture(&m_BackgroundTexture);
	m_BackgroundTexture.Invalidate();
	m_ImageBackground = false;
}

bool CBackground::LoadImageBackground(const char *pPath)
{
	ClearVideoBackground();
	ClearImageBackground();
	m_BackgroundTexture = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL);
	if(m_BackgroundTexture.IsNullTexture())
	{
		m_BackgroundTexture.Invalidate();
		return false;
	}
	m_ImageBackground = true;
	m_Loaded = true;
	return true;
}

void CBackground::ClearVideoBackground(bool UnloadTexture)
{
#if defined(CONF_VIDEORECORDER)
	if(m_pVideoPacket != nullptr)
		av_packet_free(&m_pVideoPacket);
	if(m_pVideoFrame != nullptr)
		av_frame_free(&m_pVideoFrame);
	if(m_pVideoRgbaFrame != nullptr)
		av_frame_free(&m_pVideoRgbaFrame);
	if(m_pVideoSwsContext != nullptr)
		sws_freeContext(m_pVideoSwsContext);
	if(m_pVideoCodecContext != nullptr)
		avcodec_free_context(&m_pVideoCodecContext);
	if(m_pVideoFormatContext != nullptr)
		avformat_close_input(&m_pVideoFormatContext);

	m_VideoStreamIndex = -1;
	m_VideoStartTime = 0;
	m_VideoDuration = 0.0;
	m_VideoLastFrameTime = -1.0;
	m_VideoFrameInterval = 1.0 / 30.0;
	m_VideoWidth = 0;
	m_VideoHeight = 0;
	m_vVideoFrameBuffer.clear();
#endif

	if(UnloadTexture && m_BackgroundTexture.IsValid())
		Graphics()->UnloadTexture(&m_BackgroundTexture);
	m_BackgroundTexture.Invalidate();
	m_VideoBackground = false;
}

bool CBackground::LoadVideoBackground(const char *pPath)
{
	ClearImageBackground();
	ClearVideoBackground();

#if defined(CONF_VIDEORECORDER)
	char aFullPath[IO_MAX_PATH_LENGTH];
	if(!MakeStoragePath(Storage(), pPath, aFullPath, sizeof(aFullPath)))
		return false;

	if(avformat_open_input(&m_pVideoFormatContext, aFullPath, nullptr, nullptr) < 0)
	{
		log_warn("background/video", "Could not open video background '%s'", pPath);
		ClearVideoBackground();
		return false;
	}
	if(avformat_find_stream_info(m_pVideoFormatContext, nullptr) < 0)
	{
		log_warn("background/video", "Could not read video stream info for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	for(unsigned StreamIndex = 0; StreamIndex < m_pVideoFormatContext->nb_streams; ++StreamIndex)
	{
		const AVStream *pStream = m_pVideoFormatContext->streams[StreamIndex];
		if(pStream != nullptr && pStream->codecpar != nullptr && pStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			m_VideoStreamIndex = (int)StreamIndex;
			break;
		}
	}
	if(m_VideoStreamIndex < 0)
	{
		log_warn("background/video", "No video stream found in '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	AVStream *pVideoStream = m_pVideoFormatContext->streams[m_VideoStreamIndex];
	const AVCodec *pCodec = avcodec_find_decoder(pVideoStream->codecpar->codec_id);
	if(pCodec == nullptr)
	{
		log_warn("background/video", "No decoder found for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	m_pVideoCodecContext = avcodec_alloc_context3(pCodec);
	if(m_pVideoCodecContext == nullptr || avcodec_parameters_to_context(m_pVideoCodecContext, pVideoStream->codecpar) < 0 || avcodec_open2(m_pVideoCodecContext, pCodec, nullptr) < 0)
	{
		log_warn("background/video", "Could not open decoder for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	m_VideoWidth = m_pVideoCodecContext->width;
	m_VideoHeight = m_pVideoCodecContext->height;
	if(m_VideoWidth <= 0 || m_VideoHeight <= 0 || m_VideoWidth > MAX_BACKGROUND_VIDEO_DIMENSION || m_VideoHeight > MAX_BACKGROUND_VIDEO_DIMENSION || (int64_t)m_VideoWidth * (int64_t)m_VideoHeight > MAX_BACKGROUND_VIDEO_PIXELS)
	{
		log_warn("background/video", "Unsupported video background size %dx%d for '%s'", m_VideoWidth, m_VideoHeight, pPath);
		ClearVideoBackground();
		return false;
	}

	m_pVideoFrame = av_frame_alloc();
	m_pVideoRgbaFrame = av_frame_alloc();
	m_pVideoPacket = av_packet_alloc();
	if(m_pVideoFrame == nullptr || m_pVideoRgbaFrame == nullptr || m_pVideoPacket == nullptr)
	{
		log_warn("background/video", "Could not allocate video frame buffers for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	m_vVideoFrameBuffer.resize((size_t)m_VideoWidth * (size_t)m_VideoHeight * 4);
	const int FillResult = av_image_fill_arrays(m_pVideoRgbaFrame->data, m_pVideoRgbaFrame->linesize, m_vVideoFrameBuffer.data(), AV_PIX_FMT_RGBA, m_VideoWidth, m_VideoHeight, 1);
	if(FillResult < 0)
	{
		log_warn("background/video", "Could not bind video frame buffer for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	m_pVideoSwsContext = sws_getContext(
		m_VideoWidth, m_VideoHeight, m_pVideoCodecContext->pix_fmt,
		m_VideoWidth, m_VideoHeight, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if(m_pVideoSwsContext == nullptr)
	{
		log_warn("background/video", "Could not create video scaler for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	AVRational FrameRate = av_guess_frame_rate(m_pVideoFormatContext, pVideoStream, nullptr);
	if(FrameRate.num > 0 && FrameRate.den > 0)
		m_VideoFrameInterval = (double)FrameRate.den / (double)FrameRate.num;
	else if(pVideoStream->avg_frame_rate.num > 0 && pVideoStream->avg_frame_rate.den > 0)
		m_VideoFrameInterval = (double)pVideoStream->avg_frame_rate.den / (double)pVideoStream->avg_frame_rate.num;
	m_VideoFrameInterval = std::clamp(m_VideoFrameInterval, 1.0 / 240.0, 1.0);

	if(m_pVideoFormatContext->duration > 0)
		m_VideoDuration = (double)m_pVideoFormatContext->duration / (double)AV_TIME_BASE;
	m_VideoStartTime = time_get();
	m_VideoLastFrameTime = -m_VideoFrameInterval;

	if(!DecodeNextVideoFrame() || !UploadVideoFrame())
	{
		log_warn("background/video", "Could not decode first frame of '%s'", pPath);
		ClearVideoBackground();
		return false;
	}

	m_VideoBackground = true;
	m_Loaded = true;
	return true;
#else
	(void)pPath;
	return false;
#endif
}

#if defined(CONF_VIDEORECORDER)
bool CBackground::DecodeNextVideoFrame()
{
	if(m_pVideoFormatContext == nullptr || m_pVideoCodecContext == nullptr || m_pVideoPacket == nullptr || m_pVideoFrame == nullptr || m_pVideoRgbaFrame == nullptr || m_pVideoSwsContext == nullptr)
		return false;

	while(true)
	{
		const int ReadResult = av_read_frame(m_pVideoFormatContext, m_pVideoPacket);
		if(ReadResult < 0)
		{
			avcodec_send_packet(m_pVideoCodecContext, nullptr);
		}
		else if(m_pVideoPacket->stream_index != m_VideoStreamIndex)
		{
			av_packet_unref(m_pVideoPacket);
			continue;
		}
		else
		{
			if(avcodec_send_packet(m_pVideoCodecContext, m_pVideoPacket) < 0)
			{
				av_packet_unref(m_pVideoPacket);
				return false;
			}
			av_packet_unref(m_pVideoPacket);
		}

		const int ReceiveResult = avcodec_receive_frame(m_pVideoCodecContext, m_pVideoFrame);
		if(ReceiveResult == AVERROR(EAGAIN))
		{
			if(ReadResult < 0)
				return false;
			continue;
		}
		if(ReceiveResult < 0)
			return false;

		sws_scale(m_pVideoSwsContext, m_pVideoFrame->data, m_pVideoFrame->linesize, 0, m_VideoHeight, m_pVideoRgbaFrame->data, m_pVideoRgbaFrame->linesize);
		av_frame_unref(m_pVideoFrame);
		return true;
	}
}

bool CBackground::RestartVideoBackground()
{
	if(m_pVideoFormatContext == nullptr || m_pVideoCodecContext == nullptr)
		return false;
	if(av_seek_frame(m_pVideoFormatContext, m_VideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0)
		return false;
	avcodec_flush_buffers(m_pVideoCodecContext);
	m_VideoStartTime = time_get();
	m_VideoLastFrameTime = -m_VideoFrameInterval;
	return true;
}

bool CBackground::UploadVideoFrame()
{
	if(m_VideoWidth <= 0 || m_VideoHeight <= 0 || m_vVideoFrameBuffer.empty())
		return false;

	if(m_BackgroundTexture.IsValid())
		return Graphics()->UpdateTexture(m_BackgroundTexture, 0, 0, (size_t)m_VideoWidth, (size_t)m_VideoHeight, m_vVideoFrameBuffer.data(), false);

	CImageInfo Image;
	Image.m_Width = (size_t)m_VideoWidth;
	Image.m_Height = (size_t)m_VideoHeight;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = m_vVideoFrameBuffer.data();
	m_BackgroundTexture = Graphics()->LoadTextureRaw(Image, 0, "background-video");
	return m_BackgroundTexture.IsValid();
}
#endif

bool CBackground::UpdateVideoBackground()
{
	if(!m_VideoBackground)
		return false;

#if defined(CONF_VIDEORECORDER)
	if(m_VideoStartTime == 0)
		m_VideoStartTime = time_get();

	const double PlaybackTime = (double)(time_get() - m_VideoStartTime) / (double)time_freq();
	const double TargetFrameTime = m_VideoDuration > 0.0 ? std::fmod(PlaybackTime, m_VideoDuration) : PlaybackTime;

	if(m_VideoDuration > 0.0 && PlaybackTime >= m_VideoDuration && TargetFrameTime + m_VideoFrameInterval < m_VideoLastFrameTime)
	{
		if(!RestartVideoBackground())
			return false;
	}

	bool Updated = false;
	int DecodedFrames = 0;
	while(TargetFrameTime + m_VideoFrameInterval * 0.5 >= m_VideoLastFrameTime + m_VideoFrameInterval && DecodedFrames < 4)
	{
		if(!DecodeNextVideoFrame())
		{
			if(!RestartVideoBackground() || !DecodeNextVideoFrame())
				return Updated;
		}
		m_VideoLastFrameTime += m_VideoFrameInterval;
		Updated = true;
		++DecodedFrames;
	}

	if(Updated)
		UploadVideoFrame();
	return true;
#else
	return false;
#endif
}

bool CBackground::RenderBackgroundTexture()
{
	if(!m_ImageBackground && !m_VideoBackground)
		return false;
	if(m_VideoBackground)
		UpdateVideoBackground();
	if(!m_BackgroundTexture.IsValid())
		return false;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);
	Graphics()->TextureSet(m_BackgroundTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem QuadItem(0.0f, 0.0f, ScreenWidth, ScreenHeight);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
	return true;
}

void CBackground::OnInit()
{
	m_pBackgroundMap = CreateBGMap();
	m_pMap = m_pBackgroundMap;

	m_pImages->OnInterfacesInit(GameClient());
	Kernel()->RegisterInterface(m_pBackgroundMap);
	if(!IsDefaultBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities) && !IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities))
		LoadBackground();
}

void CBackground::OnShutdown()
{
	ClearImageBackground();
	ClearVideoBackground();
}

void CBackground::LoadBackground()
{
	if(m_Loaded && !m_ImageBackground && !m_VideoBackground && m_pMap == m_pBackgroundMap)
		m_pMap->Unload();

	ClearImageBackground();
	ClearVideoBackground();
	m_Loaded = false;
	m_pMap = m_pBackgroundMap;
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;

	char aBackgroundEntities[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities, aBackgroundEntities, sizeof(aBackgroundEntities));

	str_copy(m_aMapName, aBackgroundEntities);
	const char *pBackgroundEntities = aBackgroundEntities;
	if(pBackgroundEntities[0] != '\0')
	{
		bool NeedImageLoading = false;

		char aBuf[IO_MAX_PATH_LENGTH];
		if(str_comp(pBackgroundEntities, CURRENT_MAP) == 0)
		{
			m_pMap = Kernel()->RequestInterface<IEngineMap>();
			if(m_pMap->IsLoaded())
			{
				m_pLayers = GameClient()->Layers();
				m_pImages = &GameClient()->m_MapImages;
				m_Loaded = true;
			}
		}
		else if(IsBackgroundImageExtension(pBackgroundEntities))
		{
			ResolveBackgroundEntitiesStoragePath(Storage(), pBackgroundEntities, ".png", aBuf, sizeof(aBuf));
			LoadImageBackground(aBuf);
		}
		else if(IsBackgroundVideoExtension(pBackgroundEntities))
		{
			ResolveBackgroundEntitiesStoragePath(Storage(), pBackgroundEntities, ".mp4", aBuf, sizeof(aBuf));
			LoadVideoBackground(aBuf);
		}
		else
		{
			ResolveBackgroundEntitiesStoragePath(Storage(), pBackgroundEntities, ".map", aBuf, sizeof(aBuf));
			if(m_pMap->Load(aBuf))
			{
				m_pLayers->Init(m_pMap, true);
				NeedImageLoading = true;
				m_Loaded = true;
			}
		}

		if(m_Loaded && !m_ImageBackground && !m_VideoBackground)
		{
			if(NeedImageLoading)
			{
				m_pImages->LoadBackground(m_pLayers, m_pMap);
			}
			CMapLayers::OnMapLoad();
		}
	}
}

void CBackground::OnMapLoad()
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities, aNormalized, sizeof(aNormalized));
	if(str_comp(aNormalized, CURRENT_MAP) == 0 || str_comp(aNormalized, m_aMapName))
	{
		LoadBackground();
	}
}

void CBackground::OnRender()
{
	if(!m_Loaded)
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClOverlayEntities != 100)
		return;

	if(RenderBackgroundTexture())
		return;

	CMapLayers::OnRender();
}

bool CBackground::RenderCustom(const vec2 &Center, float Zoom)
{
	if(!m_Loaded)
		return false;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	if(g_Config.m_ClOverlayEntities != 100)
		return false;

	if(RenderBackgroundTexture())
		return true;

	CMapLayers::RenderCustom(Center, Zoom);
	return true;
}
