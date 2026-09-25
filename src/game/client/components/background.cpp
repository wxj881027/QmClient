#include "background.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>

#if defined(CONF_FAMILY_WINDOWS)
#if !defined(WINVER) || WINVER < 0x0601
#undef WINVER
#define WINVER 0x0601
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06010000
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#endif
#pragma push_macro("NOGDI")
#undef NOGDI
#pragma push_macro("NOBITMAP")
#undef NOBITMAP
#define IStorage IWICStorage
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#undef IStorage
#pragma pop_macro("NOBITMAP")
#pragma pop_macro("NOGDI")
#if defined(ERROR)
#undef ERROR
#endif
#endif

#if defined(CONF_VIDEORECORDER)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}
#endif

namespace
{
	constexpr int MAX_BACKGROUND_VIDEO_DIMENSION = 4096;
	constexpr int64_t MAX_BACKGROUND_VIDEO_PIXELS = (int64_t)MAX_BACKGROUND_VIDEO_DIMENSION * (int64_t)MAX_BACKGROUND_VIDEO_DIMENSION;
	constexpr double MAX_BACKGROUND_VIDEO_FPS = 60.0;
	constexpr int MAX_BACKGROUND_VIDEO_CATCH_UP_DECODE_STEPS_PER_UPDATE = 4;
#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
	constexpr size_t MAX_BACKGROUND_VIDEO_QUEUED_FRAMES = 3;
#endif
	constexpr int MAX_BACKGROUND_IMAGE_DIMENSION = 4096;

	double EffectiveBackgroundVideoFrameInterval(double VideoFrameInterval)
	{
		const int BackgroundVideoFps = std::clamp(g_Config.m_ClBackgroundVideoFps, 1, static_cast<int>(MAX_BACKGROUND_VIDEO_FPS));
		return std::max(VideoFrameInterval, 1.0 / static_cast<double>(BackgroundVideoFps));
	}

	bool IsPngFileData(const uint8_t *pData, unsigned FileSize)
	{
		static constexpr uint8_t s_aPngSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		return FileSize >= std::size(s_aPngSignature) && mem_comp(pData, s_aPngSignature, std::size(s_aPngSignature)) == 0;
	}

	bool IsWebPFileData(const uint8_t *pData, unsigned FileSize)
	{
		static constexpr uint8_t s_aRiffSignature[] = {'R', 'I', 'F', 'F'};
		static constexpr uint8_t s_aWebPSignature[] = {'W', 'E', 'B', 'P'};
		return FileSize >= 12 &&
		       mem_comp(pData, s_aRiffSignature, std::size(s_aRiffSignature)) == 0 &&
		       mem_comp(pData + 8, s_aWebPSignature, std::size(s_aWebPSignature)) == 0;
	}

	bool IsJpegFileData(const uint8_t *pData, unsigned DataSize)
	{
		return DataSize >= 3 && pData[0] == 0xff && pData[1] == 0xd8 && pData[2] == 0xff;
	}

	bool IsBmpFileData(const uint8_t *pData, unsigned DataSize)
	{
		return DataSize >= 2 && pData[0] == 'B' && pData[1] == 'M';
	}

	bool IsTgaFileData(const uint8_t *pData, unsigned DataSize)
	{
		if(DataSize < 18)
			return false;

		const uint8_t ImageType = pData[2];
		const int Width = pData[12] | (pData[13] << 8);
		const int Height = pData[14] | (pData[15] << 8);
		const uint8_t PixelDepth = pData[16];
		const bool SupportedType = ImageType == 2 || ImageType == 3 || ImageType == 10 || ImageType == 11;
		const bool SupportedDepth = PixelDepth == 8 || PixelDepth == 15 || PixelDepth == 16 || PixelDepth == 24 || PixelDepth == 32;
		return SupportedType && Width > 0 && Height > 0 && SupportedDepth;
	}

	bool IsTiffFileData(const uint8_t *pData, unsigned DataSize)
	{
		static constexpr uint8_t s_aLittleEndianSignature[] = {'I', 'I', 42, 0};
		static constexpr uint8_t s_aBigEndianSignature[] = {'M', 'M', 0, 42};
		return DataSize >= 4 &&
		       (mem_comp(pData, s_aLittleEndianSignature, std::size(s_aLittleEndianSignature)) == 0 ||
			       mem_comp(pData, s_aBigEndianSignature, std::size(s_aBigEndianSignature)) == 0);
	}

	bool IsGifFileData(const uint8_t *pData, unsigned DataSize)
	{
		static constexpr uint8_t s_aGif87Signature[] = {'G', 'I', 'F', '8', '7', 'a'};
		static constexpr uint8_t s_aGif89Signature[] = {'G', 'I', 'F', '8', '9', 'a'};
		return DataSize >= 6 &&
		       (mem_comp(pData, s_aGif87Signature, std::size(s_aGif87Signature)) == 0 ||
			       mem_comp(pData, s_aGif89Signature, std::size(s_aGif89Signature)) == 0);
	}

#if defined(CONF_VIDEORECORDER)
	const char *FfmpegErrorText(int Error, char *pBuffer, int BufferSize)
	{
		if(BufferSize <= 0)
			return "";
		if(Error >= 0)
		{
			str_format(pBuffer, BufferSize, "ok(%d)", Error);
			return pBuffer;
		}
		av_strerror(Error, pBuffer, BufferSize);
		return pBuffer;
	}

	AVCodecID FfmpegImageCodecIdFromData(const uint8_t *pData, unsigned DataSize)
	{
		if(IsJpegFileData(pData, DataSize))
			return AV_CODEC_ID_MJPEG;
		if(IsBmpFileData(pData, DataSize))
			return AV_CODEC_ID_BMP;
		if(IsTgaFileData(pData, DataSize))
			return AV_CODEC_ID_TARGA;
		if(IsTiffFileData(pData, DataSize))
			return AV_CODEC_ID_TIFF;
		if(IsGifFileData(pData, DataSize))
			return AV_CODEC_ID_GIF;
		return AV_CODEC_ID_NONE;
	}

	const AVInputFormat *FfmpegInputFormatFromName(const char *pName)
	{
		if(pName == nullptr)
			return nullptr;
		if(str_endswith_nocase(pName, ".mp4") || str_endswith_nocase(pName, ".mov"))
			return av_find_input_format("mov");
		if(str_endswith_nocase(pName, ".webm") || str_endswith_nocase(pName, ".mkv"))
			return av_find_input_format("matroska");
		return nullptr;
	}

	AVCodecID FfmpegImageCodecIdFromName(const char *pName)
	{
		if(pName == nullptr)
			return AV_CODEC_ID_NONE;
		if(str_endswith_nocase(pName, ".jpg") || str_endswith_nocase(pName, ".jpeg") || str_endswith_nocase(pName, ".jfif"))
			return AV_CODEC_ID_MJPEG;
		if(str_endswith_nocase(pName, ".bmp"))
			return AV_CODEC_ID_BMP;
		if(str_endswith_nocase(pName, ".tga"))
			return AV_CODEC_ID_TARGA;
		if(str_endswith_nocase(pName, ".tif") || str_endswith_nocase(pName, ".tiff"))
			return AV_CODEC_ID_TIFF;
		if(str_endswith_nocase(pName, ".gif"))
			return AV_CODEC_ID_GIF;
		return AV_CODEC_ID_NONE;
	}
#endif

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

#if defined(CONF_VIDEORECORDER)
	int ReadVideoFilePacket(void *pOpaque, uint8_t *pBuffer, int BufferSize)
	{
		if(pOpaque == nullptr || pBuffer == nullptr || BufferSize <= 0)
			return AVERROR(EINVAL);

		const unsigned ReadSize = io_read(static_cast<IOHANDLE>(pOpaque), pBuffer, BufferSize);
		return ReadSize > 0 ? (int)ReadSize : AVERROR_EOF;
	}

	int64_t SeekVideoFile(void *pOpaque, int64_t Offset, int Whence)
	{
		if(pOpaque == nullptr)
			return AVERROR(EINVAL);

		if(Whence == AVSEEK_SIZE)
		{
			IOHANDLE File = static_cast<IOHANDLE>(pOpaque);
			const int64_t CurrentPosition = io_tell(File);
			if(CurrentPosition < 0 || io_seek(File, 0, IOSEEK_END) != 0)
				return AVERROR(EIO);
			const int64_t Length = io_tell(File);
			if(io_seek(File, CurrentPosition, IOSEEK_START) != 0)
				return AVERROR(EIO);
			return Length >= 0 ? Length : AVERROR(EIO);
		}

		Whence &= ~AVSEEK_FORCE;
		ESeekOrigin Origin;
		switch(Whence)
		{
		case SEEK_SET:
			Origin = IOSEEK_START;
			break;
		case SEEK_CUR:
			Origin = IOSEEK_CUR;
			break;
		case SEEK_END:
			Origin = IOSEEK_END;
			break;
		default:
			return AVERROR(EINVAL);
		}
		if(io_seek(static_cast<IOHANDLE>(pOpaque), Offset, Origin) != 0)
			return AVERROR(EIO);
		return io_tell(static_cast<IOHANDLE>(pOpaque));
	}
#endif

	bool ResizeBackgroundImageIfNeeded(CImageInfo &Image)
	{
		if(Image.m_Width <= MAX_BACKGROUND_IMAGE_DIMENSION && Image.m_Height <= MAX_BACKGROUND_IMAGE_DIMENSION)
			return false;

		const double Scale = minimum(
			(double)MAX_BACKGROUND_IMAGE_DIMENSION / (double)Image.m_Width,
			(double)MAX_BACKGROUND_IMAGE_DIMENSION / (double)Image.m_Height);
		const int NewWidth = maximum(1, (int)std::floor((double)Image.m_Width * Scale));
		const int NewHeight = maximum(1, (int)std::floor((double)Image.m_Height * Scale));
		ResizeImage(Image, NewWidth, NewHeight);
		return true;
	}

#if defined(CONF_FAMILY_WINDOWS)
	void ReleaseWicObject(IUnknown *pObject)
	{
		if(pObject != nullptr)
			pObject->Release();
	}

	bool LoadBackgroundWicImageData(const void *pData, unsigned DataSize, const char *pContextName, CImageInfo &Image)
	{
		if(pData == nullptr || DataSize == 0)
			return false;

		IWICImagingFactory *pFactory = nullptr;
		IWICStream *pStream = nullptr;
		IWICBitmapDecoder *pDecoder = nullptr;
		IWICBitmapFrameDecode *pFrame = nullptr;
		IWICFormatConverter *pConverter = nullptr;
		uint8_t *pImageData = nullptr;
		UINT Width = 0;
		UINT Height = 0;
		HRESULT Result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		if(SUCCEEDED(Result))
			Result = pFactory->CreateStream(&pStream);
		if(SUCCEEDED(Result))
			Result = pStream->InitializeFromMemory(static_cast<BYTE *>(const_cast<void *>(pData)), DataSize);
		if(SUCCEEDED(Result))
			Result = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder);
		if(SUCCEEDED(Result))
			Result = pDecoder->GetFrame(0, &pFrame);
		if(SUCCEEDED(Result))
			Result = pFrame->GetSize(&Width, &Height);
		if(SUCCEEDED(Result))
			Result = pFactory->CreateFormatConverter(&pConverter);
		if(SUCCEEDED(Result))
			Result = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
		if(SUCCEEDED(Result))
		{
			const size_t PixelSize = CImageInfo::PixelSize(CImageInfo::FORMAT_RGBA);
			if(Width == 0 || Height == 0 || (size_t)Width > std::numeric_limits<size_t>::max() / (size_t)Height / PixelSize)
			{
				Result = E_INVALIDARG;
			}
			else
			{
				const size_t ImageSize = (size_t)Width * (size_t)Height * PixelSize;
				pImageData = static_cast<uint8_t *>(malloc(ImageSize));
				if(pImageData == nullptr)
					Result = E_OUTOFMEMORY;
				else
					Result = pConverter->CopyPixels(nullptr, Width * (UINT)PixelSize, (UINT)ImageSize, pImageData);
			}
		}

		ReleaseWicObject(pConverter);
		ReleaseWicObject(pFrame);
		ReleaseWicObject(pDecoder);
		ReleaseWicObject(pStream);
		ReleaseWicObject(pFactory);

		if(FAILED(Result))
		{
			log_warn("background/image", "Could not decode image with WIC '%s': error=0x%08x text='%s' data_size=%u width=%u height=%u",
				pContextName,
				(unsigned)Result,
				windows_format_system_message(Result).c_str(),
				DataSize,
				Width,
				Height);
			free(pImageData);
			return false;
		}

		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = pImageData;
		return true;
	}

	bool CopyMediaFoundationRgb32ToRgba(const uint8_t *pSourceData, ptrdiff_t SourceStride, int Width, int Height, std::vector<uint8_t> &vDest)
	{
		if(pSourceData == nullptr || Width <= 0 || Height <= 0 || SourceStride == 0)
			return false;
		if((size_t)Width > std::numeric_limits<size_t>::max() / (size_t)Height / 4)
			return false;
		const size_t RequiredSize = (size_t)Width * (size_t)Height * 4;
		if(vDest.size() < RequiredSize)
			return false;
		if(SourceStride > 0 && (size_t)SourceStride < (size_t)Width * 4)
			return false;
		if(SourceStride < 0 && (size_t)-SourceStride < (size_t)Width * 4)
			return false;

		for(int y = 0; y < Height; ++y)
		{
			const uint8_t *pSrc = pSourceData + (ptrdiff_t)y * SourceStride;
			uint32_t *pDst = reinterpret_cast<uint32_t *>(vDest.data() + (size_t)y * (size_t)Width * 4);
			for(int x = 0; x < Width; ++x)
			{
				const uint32_t Blue = pSrc[x * 4 + 0];
				const uint32_t Green = pSrc[x * 4 + 1];
				const uint32_t Red = pSrc[x * 4 + 2];
				pDst[x] = Red | (Green << 8) | (Blue << 16) | 0xff000000u;
			}
		}
		return true;
	}
#endif

#if defined(CONF_VIDEORECORDER)
	bool LoadBackgroundFfmpegImageData(const uint8_t *pData, unsigned DataSize, const char *pContextName, AVCodecID CodecId, CImageInfo &Image)
	{
		if(pData == nullptr || DataSize == 0 || DataSize > (unsigned)std::numeric_limits<int>::max() || CodecId == AV_CODEC_ID_NONE)
		{
			log_warn("background/image", "Invalid FFmpeg image input for '%s': data=%p size=%u codec=%d", pContextName, pData, DataSize, (int)CodecId);
			return false;
		}

		const AVCodec *pCodec = avcodec_find_decoder(CodecId);
		if(pCodec == nullptr)
		{
			log_warn("background/image", "No FFmpeg decoder for image '%s': codec=%d", pContextName, (int)CodecId);
			return false;
		}

		AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
		AVPacket *pPacket = av_packet_alloc();
		AVFrame *pFrame = av_frame_alloc();
		AVFrame *pRgbaFrame = av_frame_alloc();
		SwsContext *pSwsContext = nullptr;
		uint8_t *pImageData = nullptr;
		int CodecOpenResult = AVERROR(ENOMEM);
		int PacketAllocResult = AVERROR(ENOMEM);
		int SendPacketResult = AVERROR(EINVAL);
		int FirstReceiveResult = AVERROR(EINVAL);
		int FlushResult = 0;
		int FlushReceiveResult = AVERROR(EINVAL);
		int FillArrayResult = AVERROR(EINVAL);
		int ScaleResult = 0;
		bool Success = false;
		bool ReceivedFrame = false;

		if(pCodecContext != nullptr && pPacket != nullptr && pFrame != nullptr && pRgbaFrame != nullptr)
		{
			CodecOpenResult = avcodec_open2(pCodecContext, pCodec, nullptr);
			if(CodecOpenResult >= 0)
			{
				PacketAllocResult = av_new_packet(pPacket, (int)DataSize);
				if(PacketAllocResult >= 0)
				{
					mem_copy(pPacket->data, pData, DataSize);
					SendPacketResult = avcodec_send_packet(pCodecContext, pPacket);
					if(SendPacketResult >= 0)
					{
						FirstReceiveResult = avcodec_receive_frame(pCodecContext, pFrame);
						if(FirstReceiveResult == AVERROR(EAGAIN))
						{
							FlushResult = avcodec_send_packet(pCodecContext, nullptr);
							FlushReceiveResult = avcodec_receive_frame(pCodecContext, pFrame);
						}
						ReceivedFrame = FirstReceiveResult >= 0 || FlushReceiveResult >= 0;
					}
					av_packet_unref(pPacket);
				}
			}
			if(ReceivedFrame)
			{
				const int ImageWidth = pFrame->width;
				const int ImageHeight = pFrame->height;
				const AVPixelFormat SourceFormat = pFrame->format == AV_PIX_FMT_NONE ? pCodecContext->pix_fmt : (AVPixelFormat)pFrame->format;
				if(ImageWidth > 0 && ImageHeight > 0 && SourceFormat != AV_PIX_FMT_NONE)
				{
					const size_t PixelSize = CImageInfo::PixelSize(CImageInfo::FORMAT_RGBA);
					if((size_t)ImageWidth <= std::numeric_limits<size_t>::max() / (size_t)ImageHeight / PixelSize)
					{
						const size_t ImageSize = (size_t)ImageWidth * (size_t)ImageHeight * PixelSize;
						pImageData = static_cast<uint8_t *>(malloc(ImageSize));
						if(pImageData != nullptr &&
							(FillArrayResult = av_image_fill_arrays(pRgbaFrame->data, pRgbaFrame->linesize, pImageData, AV_PIX_FMT_RGBA, ImageWidth, ImageHeight, 1)) >= 0)
						{
							pSwsContext = sws_getContext(ImageWidth, ImageHeight, SourceFormat, ImageWidth, ImageHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
							if(pSwsContext != nullptr && (ScaleResult = sws_scale(pSwsContext, pFrame->data, pFrame->linesize, 0, ImageHeight, pRgbaFrame->data, pRgbaFrame->linesize)) > 0)
							{
								Image.m_Width = (size_t)ImageWidth;
								Image.m_Height = (size_t)ImageHeight;
								Image.m_Format = CImageInfo::FORMAT_RGBA;
								Image.m_pData = pImageData;
								pImageData = nullptr;
								Success = true;
							}
						}
					}
				}
			}
		}

		if(!Success)
		{
			char aCodecOpenError[AV_ERROR_MAX_STRING_SIZE];
			char aPacketAllocError[AV_ERROR_MAX_STRING_SIZE];
			char aSendPacketError[AV_ERROR_MAX_STRING_SIZE];
			char aFirstReceiveError[AV_ERROR_MAX_STRING_SIZE];
			char aFlushError[AV_ERROR_MAX_STRING_SIZE];
			char aFlushReceiveError[AV_ERROR_MAX_STRING_SIZE];
			char aFillArrayError[AV_ERROR_MAX_STRING_SIZE];
			log_warn("background/image", "Could not decode image '%s': codec=%d codec_name=%s data_size=%u alloc_ctx=%d alloc_packet=%d alloc_frame=%d alloc_rgba=%d open=%s packet=%s send=%s receive=%s flush=%s flush_receive=%s received=%d frame=%dx%d frame_format=%d fill=%s sws=%d scale=%d",
				pContextName,
				(int)CodecId,
				pCodec != nullptr && pCodec->name != nullptr ? pCodec->name : "(none)",
				DataSize,
				pCodecContext != nullptr,
				pPacket != nullptr,
				pFrame != nullptr,
				pRgbaFrame != nullptr,
				FfmpegErrorText(CodecOpenResult, aCodecOpenError, sizeof(aCodecOpenError)),
				FfmpegErrorText(PacketAllocResult, aPacketAllocError, sizeof(aPacketAllocError)),
				FfmpegErrorText(SendPacketResult, aSendPacketError, sizeof(aSendPacketError)),
				FfmpegErrorText(FirstReceiveResult, aFirstReceiveError, sizeof(aFirstReceiveError)),
				FfmpegErrorText(FlushResult, aFlushError, sizeof(aFlushError)),
				FfmpegErrorText(FlushReceiveResult, aFlushReceiveError, sizeof(aFlushReceiveError)),
				ReceivedFrame,
				pFrame != nullptr ? pFrame->width : 0,
				pFrame != nullptr ? pFrame->height : 0,
				pFrame != nullptr ? pFrame->format : AV_PIX_FMT_NONE,
				FfmpegErrorText(FillArrayResult, aFillArrayError, sizeof(aFillArrayError)),
				pSwsContext != nullptr,
				ScaleResult);
			free(pImageData);
		}
		if(pSwsContext != nullptr)
			sws_freeContext(pSwsContext);
		if(pRgbaFrame != nullptr)
			av_frame_free(&pRgbaFrame);
		if(pFrame != nullptr)
			av_frame_free(&pFrame);
		if(pPacket != nullptr)
			av_packet_free(&pPacket);
		if(pCodecContext != nullptr)
			avcodec_free_context(&pCodecContext);
		return Success;
	}
#endif
}

bool LoadBackgroundImageData(const void *pData, unsigned DataSize, const char *pContextName, CImageInfo &Image)
{
	if(pData == nullptr || DataSize == 0)
		return false;

	const uint8_t *pImageData = static_cast<const uint8_t *>(pData);
	if(IsPngFileData(pImageData, DataSize))
		return CImageLoader::LoadPng(pData, DataSize, pContextName, Image);
	if(IsWebPFileData(pImageData, DataSize))
		return CImageLoader::LoadWebP(pData, DataSize, pContextName, Image);
#if defined(CONF_FAMILY_WINDOWS)
	if(IsJpegFileData(pImageData, DataSize) || IsBmpFileData(pImageData, DataSize) || IsTiffFileData(pImageData, DataSize) || IsGifFileData(pImageData, DataSize))
		return LoadBackgroundWicImageData(pData, DataSize, pContextName, Image);
#endif
#if defined(CONF_VIDEORECORDER)
	AVCodecID CodecId = FfmpegImageCodecIdFromData(pImageData, DataSize);
	if(CodecId == AV_CODEC_ID_NONE)
		CodecId = FfmpegImageCodecIdFromName(pContextName);
	if(CodecId != AV_CODEC_ID_NONE)
		return LoadBackgroundFfmpegImageData(pImageData, DataSize, pContextName, CodecId, Image);
#endif
	if(CImageLoader::LoadPng(pData, DataSize, pContextName, Image) ||
		CImageLoader::LoadWebP(pData, DataSize, pContextName, Image))
		return true;
	return false;
}

bool LoadBackgroundImageFile(IStorage *pStorage, const char *pPath, CImageInfo &Image)
{
	if(pStorage == nullptr || pPath == nullptr || pPath[0] == '\0')
		return false;

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!pStorage->ReadFile(pPath, IStorage::TYPE_ALL, &pFileData, &FileSize))
		return false;

	const bool Loaded = LoadBackgroundImageData(pFileData, FileSize, pPath, Image);
	free(pFileData);
	return Loaded;
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
	CImageInfo Image;
	if(!LoadBackgroundImageFile(Storage(), pPath, Image))
		return false;

	ResizeBackgroundImageIfNeeded(Image);
	m_BackgroundTexture = Graphics()->LoadTextureRawMove(Image, IGraphics::TEXLOAD_NO_MIPMAPS, pPath);
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
	if(m_pVideoIoContext != nullptr)
		avio_context_free(&m_pVideoIoContext);
	if(m_VideoFile != nullptr)
		io_close(m_VideoFile);

	m_VideoStreamIndex = -1;
	m_VideoFile = nullptr;
	m_VideoStartTime = 0;
	m_VideoDuration = 0.0;
	m_VideoLastFrameTime = -1.0;
	m_VideoFrameInterval = 1.0 / 30.0;
	m_VideoLastUploadTime = 0;
	m_VideoWidth = 0;
	m_VideoHeight = 0;
	m_vVideoFrameBuffer.clear();
#endif
#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
	StopMediaFoundationVideoThread();
	if(m_pMfSourceReader != nullptr)
		m_pMfSourceReader->Release();
	m_pMfSourceReader = nullptr;
	m_vMfVideoQueuedFrames.clear();
	m_vMfVideoFreeFrameBuffers.clear();
	m_MfVideoLastUploadTime = 0;
	m_MfVideoBackground = false;
	if(m_MfStarted)
	{
		MFShutdown();
		m_MfStarted = false;
	}
#endif

	if(UnloadTexture && m_BackgroundTexture.IsValid())
		Graphics()->UnloadTexture(&m_BackgroundTexture);
	m_BackgroundTexture.Invalidate();
	m_VideoBackground = false;
}

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
bool CBackground::LoadMediaFoundationVideoBackground(const char *pPath)
{
	char aFullPath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = Storage()->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL, aFullPath, sizeof(aFullPath));
	if(File == nullptr)
		return false;
	const int64_t VideoFileSize = io_length(File);
	io_close(File);

	HRESULT Result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if(FAILED(Result))
	{
		log_warn("background/video-mf", "MFStartup failed for '%s': error=0x%08x text='%s'", pPath, (unsigned)Result, windows_format_system_message(Result).c_str());
		return false;
	}
	m_MfStarted = true;

	const std::wstring WidePath = windows_utf8_to_wide(aFullPath);
	IMFAttributes *pAttributes = nullptr;
	Result = MFCreateAttributes(&pAttributes, 1);
	if(SUCCEEDED(Result))
		Result = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	if(FAILED(Result))
	{
		ReleaseWicObject(pAttributes);
		log_warn("background/video-mf", "Could not create source reader attributes for '%s': error=0x%08x text='%s' full_path='%s'",
			pPath,
			(unsigned)Result,
			windows_format_system_message(Result).c_str(),
			aFullPath);
		return false;
	}
	Result = MFCreateSourceReaderFromURL(WidePath.c_str(), pAttributes, &m_pMfSourceReader);
	ReleaseWicObject(pAttributes);
	if(FAILED(Result))
	{
		log_warn("background/video-mf", "Could not create source reader for '%s': error=0x%08x text='%s' full_path='%s' file_size=%lld",
			pPath,
			(unsigned)Result,
			windows_format_system_message(Result).c_str(),
			aFullPath,
			(long long)VideoFileSize);
		return false;
	}

	IMFMediaType *pMediaType = nullptr;
	Result = MFCreateMediaType(&pMediaType);
	if(SUCCEEDED(Result))
		Result = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if(SUCCEEDED(Result))
		Result = pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if(SUCCEEDED(Result))
		Result = m_pMfSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pMediaType);
	ReleaseWicObject(pMediaType);
	if(FAILED(Result))
	{
		log_warn("background/video-mf", "Could not set RGB32 output type for '%s': error=0x%08x text='%s' full_path='%s'",
			pPath,
			(unsigned)Result,
			windows_format_system_message(Result).c_str(),
			aFullPath);
		return false;
	}

	IMFMediaType *pCurrentType = nullptr;
	Result = m_pMfSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
	UINT32 Width = 0;
	UINT32 Height = 0;
	UINT32 FrameRateNum = 0;
	UINT32 FrameRateDen = 0;
	if(SUCCEEDED(Result))
		Result = MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &Width, &Height);
	if(SUCCEEDED(Result))
		MFGetAttributeRatio(pCurrentType, MF_MT_FRAME_RATE, &FrameRateNum, &FrameRateDen);
	ReleaseWicObject(pCurrentType);
	if(FAILED(Result) || Width == 0 || Height == 0)
	{
		log_warn("background/video-mf", "Could not read video type for '%s': error=0x%08x text='%s' width=%u height=%u full_path='%s'",
			pPath,
			(unsigned)Result,
			windows_format_system_message(Result).c_str(),
			Width,
			Height,
			aFullPath);
		return false;
	}

	m_VideoWidth = (int)Width;
	m_VideoHeight = (int)Height;
	if(m_VideoWidth <= 0 || m_VideoHeight <= 0 || m_VideoWidth > MAX_BACKGROUND_VIDEO_DIMENSION || m_VideoHeight > MAX_BACKGROUND_VIDEO_DIMENSION || (int64_t)m_VideoWidth * (int64_t)m_VideoHeight > MAX_BACKGROUND_VIDEO_PIXELS)
	{
		log_warn("background/video-mf", "Unsupported video background size %dx%d for '%s'", m_VideoWidth, m_VideoHeight, pPath);
		return false;
	}

	if(FrameRateNum > 0 && FrameRateDen > 0)
		m_VideoFrameInterval = (double)FrameRateDen / (double)FrameRateNum;
	m_VideoFrameInterval = std::clamp(m_VideoFrameInterval, 1.0 / MAX_BACKGROUND_VIDEO_FPS, 1.0);

	PROPVARIANT Duration;
	PropVariantInit(&Duration);
	if(SUCCEEDED(m_pMfSourceReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &Duration)) && Duration.vt == VT_UI8)
		m_VideoDuration = (double)Duration.uhVal.QuadPart / 10000000.0;
	PropVariantClear(&Duration);

	m_vVideoFrameBuffer.resize((size_t)m_VideoWidth * (size_t)m_VideoHeight * 4);
	m_VideoStartTime = time_get();
	m_VideoLastFrameTime = -m_VideoFrameInterval;

	const bool DecodedFirstFrame = DecodeNextMediaFoundationVideoFrame(0.0, m_vVideoFrameBuffer);
	const bool UploadedFirstFrame = DecodedFirstFrame && UploadVideoFrame();
	if(!DecodedFirstFrame || !UploadedFirstFrame)
	{
		log_warn("background/video-mf", "Could not decode first frame of '%s': decoded=%d uploaded=%d width=%d height=%d duration=%.3f frame_interval=%.6f full_path='%s'",
			pPath,
			DecodedFirstFrame,
			UploadedFirstFrame,
			m_VideoWidth,
			m_VideoHeight,
			m_VideoDuration,
			m_VideoFrameInterval,
			aFullPath);
		return false;
	}

	{
		const std::lock_guard<std::mutex> Lock(m_MfVideoFrameMutex);
		m_vMfVideoQueuedFrames.clear();
		m_vMfVideoFreeFrameBuffers.clear();
		m_MfVideoLastUploadTime = time_get();
	}
	m_MfVideoThreadStop = false;
	m_MfVideoThreadRunning = true;
	m_MfVideoThread = std::thread(&CBackground::MediaFoundationVideoThreadFunc, this);

	m_MfVideoBackground = true;
	m_VideoBackground = true;
	m_Loaded = true;
	log_info("background/video-mf", "Loaded video background '%s': full_path='%s' size=%dx%d duration=%.3f frame_interval=%.6f file_size=%lld",
		pPath,
		aFullPath,
		m_VideoWidth,
		m_VideoHeight,
		m_VideoDuration,
		m_VideoFrameInterval,
		(long long)VideoFileSize);
	return true;
}
#endif

bool CBackground::LoadVideoBackground(const char *pPath)
{
	ClearImageBackground();
	ClearVideoBackground();

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
	if(LoadMediaFoundationVideoBackground(pPath))
		return true;
	ClearVideoBackground();
#endif

#if defined(CONF_VIDEORECORDER)
	char aFullPath[IO_MAX_PATH_LENGTH];
	m_VideoFile = Storage()->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL, aFullPath, sizeof(aFullPath));
	if(m_VideoFile == nullptr)
	{
		log_warn("background/video", "Could not open storage file for video background '%s'", pPath);
		return false;
	}
	const int64_t VideoFileSize = io_length(m_VideoFile);

	static constexpr int VIDEO_IO_BUFFER_SIZE = 32 * 1024;
	unsigned char *pIoBuffer = static_cast<unsigned char *>(av_malloc(VIDEO_IO_BUFFER_SIZE));
	if(pIoBuffer == nullptr)
	{
		log_warn("background/video", "Could not allocate video IO buffer for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}
	m_pVideoIoContext = avio_alloc_context(pIoBuffer, VIDEO_IO_BUFFER_SIZE, 0, m_VideoFile, ReadVideoFilePacket, nullptr, SeekVideoFile);
	if(m_pVideoIoContext == nullptr)
	{
		av_free(pIoBuffer);
		log_warn("background/video", "Could not allocate video IO context for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}
	m_pVideoIoContext->seekable = AVIO_SEEKABLE_NORMAL;

	m_pVideoFormatContext = avformat_alloc_context();
	if(m_pVideoFormatContext == nullptr)
	{
		log_warn("background/video", "Could not allocate video format context for '%s'", pPath);
		ClearVideoBackground();
		return false;
	}
	m_pVideoFormatContext->pb = m_pVideoIoContext;
	m_pVideoFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

	const AVInputFormat *pInputFormat = FfmpegInputFormatFromName(pPath);
	const int OpenResult = avformat_open_input(&m_pVideoFormatContext, nullptr, const_cast<AVInputFormat *>(pInputFormat), nullptr);
	if(OpenResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		log_warn("background/video", "Could not open video background '%s': error=%d text='%s' full_path='%s' file_size=%lld input_format=%s io_seekable=%d io_error=%d",
			pPath,
			OpenResult,
			FfmpegErrorText(OpenResult, aError, sizeof(aError)),
			aFullPath,
			(long long)VideoFileSize,
			pInputFormat != nullptr && pInputFormat->name != nullptr ? pInputFormat->name : "(auto)",
			m_pVideoIoContext != nullptr ? m_pVideoIoContext->seekable : 0,
			m_pVideoIoContext != nullptr ? m_pVideoIoContext->error : 0);
		ClearVideoBackground();
		return false;
	}
	const int StreamInfoResult = avformat_find_stream_info(m_pVideoFormatContext, nullptr);
	if(StreamInfoResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		log_warn("background/video", "Could not read video stream info for '%s': error=%d text='%s' full_path='%s' file_size=%lld input_format=%s streams=%u io_error=%d",
			pPath,
			StreamInfoResult,
			FfmpegErrorText(StreamInfoResult, aError, sizeof(aError)),
			aFullPath,
			(long long)VideoFileSize,
			m_pVideoFormatContext != nullptr && m_pVideoFormatContext->iformat != nullptr && m_pVideoFormatContext->iformat->name != nullptr ? m_pVideoFormatContext->iformat->name : "(none)",
			m_pVideoFormatContext != nullptr ? m_pVideoFormatContext->nb_streams : 0,
			m_pVideoIoContext != nullptr ? m_pVideoIoContext->error : 0);
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
		log_warn("background/video", "No video stream found in '%s': full_path='%s' file_size=%lld streams=%u input_format=%s duration=%lld",
			pPath,
			aFullPath,
			(long long)VideoFileSize,
			m_pVideoFormatContext != nullptr ? m_pVideoFormatContext->nb_streams : 0,
			m_pVideoFormatContext != nullptr && m_pVideoFormatContext->iformat != nullptr && m_pVideoFormatContext->iformat->name != nullptr ? m_pVideoFormatContext->iformat->name : "(none)",
			m_pVideoFormatContext != nullptr ? (long long)m_pVideoFormatContext->duration : 0LL);
		ClearVideoBackground();
		return false;
	}

	AVStream *pVideoStream = m_pVideoFormatContext->streams[m_VideoStreamIndex];
	const AVCodec *pCodec = avcodec_find_decoder(pVideoStream->codecpar->codec_id);
	if(pCodec == nullptr)
	{
		log_warn("background/video", "No decoder found for '%s': stream=%d codec=%d width=%d height=%d",
			pPath,
			m_VideoStreamIndex,
			pVideoStream->codecpar != nullptr ? (int)pVideoStream->codecpar->codec_id : -1,
			pVideoStream->codecpar != nullptr ? pVideoStream->codecpar->width : 0,
			pVideoStream->codecpar != nullptr ? pVideoStream->codecpar->height : 0);
		ClearVideoBackground();
		return false;
	}

	m_pVideoCodecContext = avcodec_alloc_context3(pCodec);
	const int ParamsResult = m_pVideoCodecContext != nullptr ? avcodec_parameters_to_context(m_pVideoCodecContext, pVideoStream->codecpar) : AVERROR(ENOMEM);
	const int CodecOpenResult = ParamsResult >= 0 ? avcodec_open2(m_pVideoCodecContext, pCodec, nullptr) : AVERROR(EINVAL);
	if(m_pVideoCodecContext == nullptr || ParamsResult < 0 || CodecOpenResult < 0)
	{
		char aParamsError[AV_ERROR_MAX_STRING_SIZE];
		char aCodecOpenError[AV_ERROR_MAX_STRING_SIZE];
		log_warn("background/video", "Could not open decoder for '%s': stream=%d codec=%d codec_name=%s params=%s open=%s",
			pPath,
			m_VideoStreamIndex,
			pVideoStream->codecpar != nullptr ? (int)pVideoStream->codecpar->codec_id : -1,
			pCodec->name != nullptr ? pCodec->name : "(none)",
			FfmpegErrorText(ParamsResult, aParamsError, sizeof(aParamsError)),
			FfmpegErrorText(CodecOpenResult, aCodecOpenError, sizeof(aCodecOpenError)));
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
		char aFillError[AV_ERROR_MAX_STRING_SIZE];
		log_warn("background/video", "Could not bind video frame buffer for '%s': fill=%s width=%d height=%d buffer_size=%zu",
			pPath,
			FfmpegErrorText(FillResult, aFillError, sizeof(aFillError)),
			m_VideoWidth,
			m_VideoHeight,
			m_vVideoFrameBuffer.size());
		ClearVideoBackground();
		return false;
	}

	m_pVideoSwsContext = sws_getContext(
		m_VideoWidth, m_VideoHeight, m_pVideoCodecContext->pix_fmt,
		m_VideoWidth, m_VideoHeight, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if(m_pVideoSwsContext == nullptr)
	{
		log_warn("background/video", "Could not create video scaler for '%s': width=%d height=%d pix_fmt=%d",
			pPath,
			m_VideoWidth,
			m_VideoHeight,
			m_pVideoCodecContext != nullptr ? m_pVideoCodecContext->pix_fmt : AV_PIX_FMT_NONE);
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

	const bool DecodedFirstFrame = DecodeNextVideoFrame();
	const bool UploadedFirstFrame = DecodedFirstFrame && UploadVideoFrame();
	if(UploadedFirstFrame)
		m_VideoLastUploadTime = time_get();
	if(!DecodedFirstFrame || !UploadedFirstFrame)
	{
		log_warn("background/video", "Could not decode first frame of '%s': decoded=%d uploaded=%d stream=%d codec=%d codec_name=%s width=%d height=%d pix_fmt=%d duration=%.3f frame_interval=%.6f",
			pPath,
			DecodedFirstFrame,
			UploadedFirstFrame,
			m_VideoStreamIndex,
			pVideoStream->codecpar != nullptr ? (int)pVideoStream->codecpar->codec_id : -1,
			pCodec->name != nullptr ? pCodec->name : "(none)",
			m_VideoWidth,
			m_VideoHeight,
			m_pVideoCodecContext != nullptr ? m_pVideoCodecContext->pix_fmt : AV_PIX_FMT_NONE,
			m_VideoDuration,
			m_VideoFrameInterval);
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

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
bool CBackground::DecodeNextMediaFoundationVideoFrame(double TargetFrameTime, std::vector<uint8_t> &vFrameBuffer)
{
	if(m_pMfSourceReader == nullptr || m_VideoWidth <= 0 || m_VideoHeight <= 0 || vFrameBuffer.empty())
	{
		log_warn("background/video-mf", "Cannot decode video frame: reader=%d width=%d height=%d buffer=%zu",
			m_pMfSourceReader != nullptr,
			m_VideoWidth,
			m_VideoHeight,
			vFrameBuffer.size());
		return false;
	}

	for(int Attempts = 0; Attempts < 16; ++Attempts)
	{
		DWORD StreamIndex = 0;
		DWORD StreamFlags = 0;
		LONGLONG Timestamp = 0;
		IMFSample *pSample = nullptr;
		HRESULT Result = m_pMfSourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &StreamIndex, &StreamFlags, &Timestamp, &pSample);
		if(FAILED(Result))
		{
			log_warn("background/video-mf", "Could not read video sample: error=0x%08x text='%s' attempts=%d flags=0x%08lx stream=%lu",
				(unsigned)Result,
				windows_format_system_message(Result).c_str(),
				Attempts + 1,
				StreamFlags,
				(unsigned long)StreamIndex);
			ReleaseWicObject(pSample);
			return false;
		}
		if((StreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
		{
			ReleaseWicObject(pSample);
			return false;
		}
		if(pSample == nullptr)
			continue;

		const bool HasTimestamp = Timestamp >= 0;
		const double SampleTime = HasTimestamp ? (double)Timestamp / 10000000.0 : TargetFrameTime;
		if(HasTimestamp && SampleTime < TargetFrameTime && Attempts < 15)
		{
			ReleaseWicObject(pSample);
			continue;
		}

		IMFMediaBuffer *pBuffer = nullptr;
		Result = pSample->GetBufferByIndex(0, &pBuffer);
		if(FAILED(Result))
			Result = pSample->ConvertToContiguousBuffer(&pBuffer);
		if(FAILED(Result))
		{
			log_warn("background/video-mf", "Could not get sample buffer: error=0x%08x text='%s' attempts=%d flags=0x%08lx timestamp=%lld",
				(unsigned)Result,
				windows_format_system_message(Result).c_str(),
				Attempts + 1,
				StreamFlags,
				(long long)Timestamp);
			ReleaseWicObject(pSample);
			return false;
		}

		DWORD MaxLength = 0;
		DWORD CurrentLength = 0;
		IMF2DBuffer *pBuffer2D = nullptr;
		Result = pBuffer->QueryInterface(IID_PPV_ARGS(&pBuffer2D));
		if(SUCCEEDED(Result))
		{
			BYTE *pSourceData = nullptr;
			LONG SourceStride = 0;
			Result = pBuffer2D->Lock2D(&pSourceData, &SourceStride);
			if(SUCCEEDED(Result))
			{
				if(!CopyMediaFoundationRgb32ToRgba(pSourceData, SourceStride, m_VideoWidth, m_VideoHeight, vFrameBuffer))
					Result = MF_E_BUFFERTOOSMALL;
				pBuffer2D->Unlock2D();
			}
		}
		else
		{
			BYTE *pSourceData = nullptr;
			Result = pBuffer->Lock(&pSourceData, &MaxLength, &CurrentLength);
			if(SUCCEEDED(Result))
			{
				const size_t RequiredSize = (size_t)m_VideoWidth * (size_t)m_VideoHeight * 4;
				if(CurrentLength >= RequiredSize)
				{
					if(!CopyMediaFoundationRgb32ToRgba(pSourceData, (ptrdiff_t)m_VideoWidth * 4, m_VideoWidth, m_VideoHeight, vFrameBuffer))
						Result = MF_E_BUFFERTOOSMALL;
				}
				else
				{
					Result = MF_E_BUFFERTOOSMALL;
				}
				pBuffer->Unlock();
			}
		}

		ReleaseWicObject(pBuffer2D);
		ReleaseWicObject(pBuffer);
		ReleaseWicObject(pSample);
		if(FAILED(Result))
		{
			log_warn("background/video-mf", "Could not copy video sample: error=0x%08x text='%s' current_length=%lu max_length=%lu required=%zu size=%dx%d",
				(unsigned)Result,
				windows_format_system_message(Result).c_str(),
				(unsigned long)CurrentLength,
				(unsigned long)MaxLength,
				(size_t)m_VideoWidth * (size_t)m_VideoHeight * 4,
				m_VideoWidth,
				m_VideoHeight);
			return false;
		}
		m_VideoLastFrameTime = SampleTime;
		return true;
	}

	log_warn("background/video-mf", "Could not read a video sample after retrying: width=%d height=%d", m_VideoWidth, m_VideoHeight);
	return false;
}

bool CBackground::RestartMediaFoundationVideoBackground()
{
	if(m_pMfSourceReader == nullptr)
		return false;

	PROPVARIANT Position;
	PropVariantInit(&Position);
	Position.vt = VT_I8;
	Position.hVal.QuadPart = 0;
	const HRESULT Result = m_pMfSourceReader->SetCurrentPosition(GUID_NULL, Position);
	PropVariantClear(&Position);
	if(FAILED(Result))
	{
		log_warn("background/video-mf", "Could not restart video background: error=0x%08x text='%s'", (unsigned)Result, windows_format_system_message(Result).c_str());
		return false;
	}
	m_VideoStartTime = time_get();
	m_VideoLastFrameTime = -m_VideoFrameInterval;
	return true;
}

void CBackground::StopMediaFoundationVideoThread()
{
	m_MfVideoThreadStop = true;
	if(m_MfVideoThread.joinable())
		m_MfVideoThread.join();
	m_MfVideoThreadRunning = false;
	m_MfVideoThreadStop = false;
}

void CBackground::MediaFoundationVideoThreadFunc()
{
	const size_t FrameBufferSize = (size_t)m_VideoWidth * (size_t)m_VideoHeight * 4;
	double TargetFrameTime = 0.0;
	int64_t LastProducedTime = 0;
	while(!m_MfVideoThreadStop)
	{
		const double FrameInterval = EffectiveBackgroundVideoFrameInterval(m_VideoFrameInterval);
		if(LastProducedTime != 0)
		{
			const double TimeSinceProduced = (double)(time_get() - LastProducedTime) / (double)time_freq();
			if(TimeSinceProduced + FrameInterval * 0.25 < FrameInterval)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
		}

		std::vector<uint8_t> vThreadFrameBuffer;
		{
			const std::lock_guard<std::mutex> Lock(m_MfVideoFrameMutex);
			if(!m_vMfVideoFreeFrameBuffers.empty())
			{
				vThreadFrameBuffer = std::move(m_vMfVideoFreeFrameBuffers.back());
				m_vMfVideoFreeFrameBuffers.pop_back();
			}
		}
		if(vThreadFrameBuffer.size() != FrameBufferSize)
			vThreadFrameBuffer.resize(FrameBufferSize);

		if(!DecodeNextMediaFoundationVideoFrame(TargetFrameTime, vThreadFrameBuffer))
		{
			TargetFrameTime = 0.0;
			if(!RestartMediaFoundationVideoBackground() || !DecodeNextMediaFoundationVideoFrame(TargetFrameTime, vThreadFrameBuffer))
				break;
		}
		TargetFrameTime = m_VideoLastFrameTime + FrameInterval;
		LastProducedTime = time_get();

		{
			const std::lock_guard<std::mutex> Lock(m_MfVideoFrameMutex);
			while(m_vMfVideoQueuedFrames.size() >= MAX_BACKGROUND_VIDEO_QUEUED_FRAMES)
			{
				m_vMfVideoFreeFrameBuffers.push_back(std::move(m_vMfVideoQueuedFrames.front().m_vData));
				m_vMfVideoQueuedFrames.pop_front();
			}
			m_vMfVideoQueuedFrames.push_back({std::move(vThreadFrameBuffer)});
		}

		std::this_thread::yield();
	}
	m_MfVideoThreadRunning = false;
}
#endif

#if defined(CONF_VIDEORECORDER)
bool CBackground::DecodeNextVideoFrame(bool StoreRgbaFrame)
{
	if(m_pVideoFormatContext == nullptr || m_pVideoCodecContext == nullptr || m_pVideoPacket == nullptr || m_pVideoFrame == nullptr || m_pVideoRgbaFrame == nullptr || m_pVideoSwsContext == nullptr)
	{
		log_warn("background/video", "Cannot decode video frame: format=%d codec=%d packet=%d frame=%d rgba=%d sws=%d",
			m_pVideoFormatContext != nullptr,
			m_pVideoCodecContext != nullptr,
			m_pVideoPacket != nullptr,
			m_pVideoFrame != nullptr,
			m_pVideoRgbaFrame != nullptr,
			m_pVideoSwsContext != nullptr);
		return false;
	}

	int ReadAttempts = 0;
	while(true)
	{
		const int ReadResult = av_read_frame(m_pVideoFormatContext, m_pVideoPacket);
		++ReadAttempts;
		if(ReadResult < 0)
		{
			const int FlushResult = avcodec_send_packet(m_pVideoCodecContext, nullptr);
			if(FlushResult < 0)
			{
				char aReadError[AV_ERROR_MAX_STRING_SIZE];
				char aFlushError[AV_ERROR_MAX_STRING_SIZE];
				log_warn("background/video", "Could not flush video decoder: read=%s flush=%s attempts=%d stream=%d",
					FfmpegErrorText(ReadResult, aReadError, sizeof(aReadError)),
					FfmpegErrorText(FlushResult, aFlushError, sizeof(aFlushError)),
					ReadAttempts,
					m_VideoStreamIndex);
				return false;
			}
		}
		else if(m_pVideoPacket->stream_index != m_VideoStreamIndex)
		{
			av_packet_unref(m_pVideoPacket);
			continue;
		}
		else
		{
			const int SendResult = avcodec_send_packet(m_pVideoCodecContext, m_pVideoPacket);
			if(SendResult < 0)
			{
				char aSendError[AV_ERROR_MAX_STRING_SIZE];
				log_warn("background/video", "Could not send video packet: send=%s attempts=%d packet_stream=%d target_stream=%d packet_size=%d",
					FfmpegErrorText(SendResult, aSendError, sizeof(aSendError)),
					ReadAttempts,
					m_pVideoPacket->stream_index,
					m_VideoStreamIndex,
					m_pVideoPacket->size);
				av_packet_unref(m_pVideoPacket);
				return false;
			}
			av_packet_unref(m_pVideoPacket);
		}

		const int ReceiveResult = avcodec_receive_frame(m_pVideoCodecContext, m_pVideoFrame);
		if(ReceiveResult == AVERROR(EAGAIN))
		{
			if(ReadResult < 0)
			{
				char aReadError[AV_ERROR_MAX_STRING_SIZE];
				log_warn("background/video", "Video decoder needs more input after EOF: read=%s attempts=%d stream=%d",
					FfmpegErrorText(ReadResult, aReadError, sizeof(aReadError)),
					ReadAttempts,
					m_VideoStreamIndex);
				return false;
			}
			continue;
		}
		if(ReceiveResult < 0)
		{
			char aReceiveError[AV_ERROR_MAX_STRING_SIZE];
			log_warn("background/video", "Could not receive video frame: receive=%s attempts=%d stream=%d width=%d height=%d pix_fmt=%d",
				FfmpegErrorText(ReceiveResult, aReceiveError, sizeof(aReceiveError)),
				ReadAttempts,
				m_VideoStreamIndex,
				m_VideoWidth,
				m_VideoHeight,
				m_pVideoCodecContext != nullptr ? m_pVideoCodecContext->pix_fmt : AV_PIX_FMT_NONE);
			return false;
		}

		if(!StoreRgbaFrame)
		{
			av_frame_unref(m_pVideoFrame);
			return true;
		}

		const int ScaleResult = sws_scale(m_pVideoSwsContext, m_pVideoFrame->data, m_pVideoFrame->linesize, 0, m_VideoHeight, m_pVideoRgbaFrame->data, m_pVideoRgbaFrame->linesize);
		if(ScaleResult <= 0)
		{
			log_warn("background/video", "Could not scale video frame: scale=%d attempts=%d frame=%dx%d frame_format=%d target=%dx%d",
				ScaleResult,
				ReadAttempts,
				m_pVideoFrame->width,
				m_pVideoFrame->height,
				m_pVideoFrame->format,
				m_VideoWidth,
				m_VideoHeight);
			av_frame_unref(m_pVideoFrame);
			return false;
		}
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
	m_VideoLastUploadTime = 0;
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

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_VIDEORECORDER)
	if(m_MfVideoBackground)
	{
		const int64_t Now = time_get();
		const double FrameInterval = EffectiveBackgroundVideoFrameInterval(m_VideoFrameInterval);
		if(m_MfVideoLastUploadTime != 0)
		{
			const double TimeSinceUpload = (double)(Now - m_MfVideoLastUploadTime) / (double)time_freq();
			if(TimeSinceUpload + FrameInterval * 0.25 < FrameInterval)
				return true;
		}

		bool HasFrame = false;
		{
			const std::lock_guard<std::mutex> Lock(m_MfVideoFrameMutex);
			while(m_vMfVideoQueuedFrames.size() > 1)
			{
				m_vMfVideoFreeFrameBuffers.push_back(std::move(m_vMfVideoQueuedFrames.front().m_vData));
				m_vMfVideoQueuedFrames.pop_front();
			}
			if(!m_vMfVideoQueuedFrames.empty() && m_vMfVideoQueuedFrames.front().m_vData.size() == m_vVideoFrameBuffer.size())
			{
				m_vMfVideoFreeFrameBuffers.push_back(std::move(m_vVideoFrameBuffer));
				m_vVideoFrameBuffer = std::move(m_vMfVideoQueuedFrames.front().m_vData);
				m_vMfVideoQueuedFrames.pop_front();
				HasFrame = true;
			}
		}
		if(HasFrame)
		{
			UploadVideoFrame();
			m_MfVideoLastUploadTime = Now;
		}
		return m_MfVideoThreadRunning || HasFrame;
	}
#endif

#if defined(CONF_VIDEORECORDER)
	if(m_VideoStartTime == 0)
		m_VideoStartTime = time_get();

	const int64_t Now = time_get();
	const double FrameInterval = EffectiveBackgroundVideoFrameInterval(m_VideoFrameInterval);
	if(m_VideoLastUploadTime != 0)
	{
		const double TimeSinceUpload = (double)(Now - m_VideoLastUploadTime) / (double)time_freq();
		if(TimeSinceUpload + FrameInterval * 0.25 < FrameInterval)
			return true;
	}

	const double PlaybackTime = (double)(Now - m_VideoStartTime) / (double)time_freq();
	const double TargetFrameTime = m_VideoDuration > 0.0 ? std::fmod(PlaybackTime, m_VideoDuration) : PlaybackTime;

	if(m_VideoDuration > 0.0 && PlaybackTime >= m_VideoDuration && TargetFrameTime + m_VideoFrameInterval < m_VideoLastFrameTime)
	{
		if(!RestartVideoBackground())
			return false;
	}

	bool Updated = false;
	const int ScheduledDecodeSteps = maximum(1, static_cast<int>(std::ceil(FrameInterval / m_VideoFrameInterval)) + 1);
	const int MaxDecodedFrames = maximum(ScheduledDecodeSteps, MAX_BACKGROUND_VIDEO_CATCH_UP_DECODE_STEPS_PER_UPDATE);
	const int PendingFrames = maximum(0, static_cast<int>(std::floor((TargetFrameTime + m_VideoFrameInterval * 0.5 - m_VideoLastFrameTime) / m_VideoFrameInterval)));
	const int FramesToDecode = minimum(PendingFrames, MaxDecodedFrames);
	for(int DecodedFrames = 0; DecodedFrames < FramesToDecode; ++DecodedFrames)
	{
		const bool StoreRgbaFrame = DecodedFrames + 1 == FramesToDecode;
		if(!DecodeNextVideoFrame(StoreRgbaFrame))
		{
			if(!RestartVideoBackground() || !DecodeNextVideoFrame(StoreRgbaFrame))
				return Updated;
		}
		m_VideoLastFrameTime += m_VideoFrameInterval;
		Updated = StoreRgbaFrame;
	}

	if(Updated)
	{
		UploadVideoFrame();
		m_VideoLastUploadTime = Now;
	}
	return true;
#else
	return false;
#endif
}

bool CBackground::RenderBackgroundTexture()
{
	if(!InterfacesInitialized())
		return false;
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

void CBackground::RenderBackgroundColor()
{
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClBackgroundEntitiesColor));
	Graphics()->DrawRect(ScreenX0, ScreenY0, ScreenX1 - ScreenX0, ScreenY1 - ScreenY0, BackgroundColor, IGraphics::CORNER_NONE, 0.0f);
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
	if(str_comp(g_Config.m_ClBackgroundEntities, aBackgroundEntities) != 0)
		str_copy(g_Config.m_ClBackgroundEntities, aBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));

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
			if(m_pMap->Load(aBuf, IStorage::TYPE_ALL))
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
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClOverlayEntities != 100)
		return;

	// 实体层纯色背景必须在此阶段显式绘制，不能依赖帧首清屏状态。
	RenderBackgroundColor();
	if(!m_Loaded)
		return;

	if(RenderBackgroundTexture())
		return;

	CMapLayers::OnRender();
}

bool CBackground::RenderCustom(const vec2 &Center, float Zoom)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	if(g_Config.m_ClOverlayEntities != 100)
		return false;

	RenderBackgroundColor();
	if(!m_Loaded)
		return true;

	if(RenderBackgroundTexture())
		return true;

	CMapLayers::RenderCustom(Center, Zoom);
	return true;
}
