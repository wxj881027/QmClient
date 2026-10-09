#include "image_loader.h"

#include "image_manipulation.h"

#include <base/log.h>
#include <base/system.h>

#include <png.h>
#if defined(CONF_WEBP)
#include <webp/decode.h>
#include <webp/encode.h>
#endif

#include <array>
#include <csetjmp>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>

bool CByteBufferReader::Read(void *pData, size_t Size)
{
	if(m_Error)
		return false;

	if(m_ReadOffset + Size <= m_Size)
	{
		mem_copy(pData, &m_pData[m_ReadOffset], Size);
		m_ReadOffset += Size;
		return true;
	}
	else
	{
		m_Error = true;
		return false;
	}
}

void CByteBufferWriter::Write(const void *pData, size_t Size)
{
	if(!Size)
		return;

	const size_t WriteOffset = m_vBuffer.size();
	m_vBuffer.resize(WriteOffset + Size);
	mem_copy(&m_vBuffer[WriteOffset], pData, Size);
}

namespace
{

	class CUserErrorStruct
	{
	public:
		CByteBufferReader *m_pReader;
		const char *m_pContextName;
		std::jmp_buf m_JmpBuf;
	};

} // namespace

[[noreturn]] static void PngErrorCallback(png_structp pPngStruct, png_const_charp pErrorMessage)
{
	CUserErrorStruct *pUserStruct = static_cast<CUserErrorStruct *>(png_get_error_ptr(pPngStruct));
	log_error("png", "error for file \"%s\": %s", pUserStruct->m_pContextName, pErrorMessage);
	std::longjmp(pUserStruct->m_JmpBuf, 1);
}

static void PngWarningCallback(png_structp pPngStruct, png_const_charp pWarningMessage)
{
	CUserErrorStruct *pUserStruct = static_cast<CUserErrorStruct *>(png_get_error_ptr(pPngStruct));
	log_warn("png", "warning for file \"%s\": %s", pUserStruct->m_pContextName, pWarningMessage);
}

static void PngReadDataCallback(png_structp pPngStruct, png_bytep pOutBytes, png_size_t ByteCountToRead)
{
	CByteBufferReader *pReader = static_cast<CByteBufferReader *>(png_get_io_ptr(pPngStruct));
	if(!pReader->Read(pOutBytes, ByteCountToRead))
	{
		png_error(pPngStruct, "Could not read all bytes, file was too small");
	}
}

static CImageInfo::EImageFormat ImageFormatFromChannelCount(int ColorChannelCount)
{
	switch(ColorChannelCount)
	{
	case 1:
		return CImageInfo::FORMAT_R;
	case 2:
		return CImageInfo::FORMAT_RA;
	case 3:
		return CImageInfo::FORMAT_RGB;
	case 4:
		return CImageInfo::FORMAT_RGBA;
	default:
		dbg_assert_failed("ColorChannelCount invalid");
	}
}

static bool ImageDataSize(size_t Width, size_t Height, CImageInfo::EImageFormat Format, size_t &DataSize)
{
	const size_t PixelSize = CImageInfo::PixelSize(Format);
	if(Width == 0 || Height == 0 || Width > std::numeric_limits<size_t>::max() / Height)
	{
		DataSize = 0;
		return false;
	}
	const size_t PixelCount = Width * Height;
	if(PixelCount > std::numeric_limits<size_t>::max() / PixelSize)
	{
		DataSize = 0;
		return false;
	}
	DataSize = PixelCount * PixelSize;
	return true;
}

static int PngliteIncompatibility(png_structp pPngStruct, png_infop pPngInfo)
{
	int Result = 0;

	const int ColorType = png_get_color_type(pPngStruct, pPngInfo);
	switch(ColorType)
	{
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGB_ALPHA:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		break;
	default:
		log_debug("png", "color type %d unsupported by pnglite", ColorType);
		Result |= CImageLoader::PNGLITE_COLOR_TYPE;
	}

	const int BitDepth = png_get_bit_depth(pPngStruct, pPngInfo);
	switch(BitDepth)
	{
	case 8:
	case 16:
		break;
	default:
		log_debug("png", "bit depth %d unsupported by pnglite", BitDepth);
		Result |= CImageLoader::PNGLITE_BIT_DEPTH;
	}

	const int InterlaceType = png_get_interlace_type(pPngStruct, pPngInfo);
	if(InterlaceType != PNG_INTERLACE_NONE)
	{
		log_debug("png", "interlace type %d unsupported by pnglite", InterlaceType);
		Result |= CImageLoader::PNGLITE_INTERLACE_TYPE;
	}

	if(png_get_compression_type(pPngStruct, pPngInfo) != PNG_COMPRESSION_TYPE_BASE)
	{
		log_debug("png", "non-default compression type unsupported by pnglite");
		Result |= CImageLoader::PNGLITE_COMPRESSION_TYPE;
	}

	if(png_get_filter_type(pPngStruct, pPngInfo) != PNG_FILTER_TYPE_BASE)
	{
		log_debug("png", "non-default filter type unsupported by pnglite");
		Result |= CImageLoader::PNGLITE_FILTER_TYPE;
	}

	return Result;
}

bool CImageLoader::LoadPng(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, int &PngliteIncompatible, size_t MaxDecodedBytes)
{
	// libpng 错误会 longjmp，清理状态放在堆上，避免读取被跳转失效的局部指针。
	struct SReadState
	{
		CUserErrorStruct m_Error;
		png_structp m_pPng = nullptr;
		png_infop m_pInfo = nullptr;
		png_bytepp m_pRows = nullptr;
		~SReadState()
		{
			delete[] m_pRows;
			if(m_pPng != nullptr)
				png_destroy_read_struct(&m_pPng, &m_pInfo, nullptr);
		}
	};
	std::unique_ptr<SReadState> pState(new(std::nothrow) SReadState{});
	if(pState == nullptr)
		return false;
	pState->m_Error = {&Reader, pContextName, {}};
	pState->m_pPng = png_create_read_struct(PNG_LIBPNG_VER_STRING, &pState->m_Error, PngErrorCallback, PngWarningCallback);
	if(pState->m_pPng == nullptr)
		return false;
	if(setjmp(pState->m_Error.m_JmpBuf))
	{
		Image.Free();
		return false;
	}
	pState->m_pInfo = png_create_info_struct(pState->m_pPng);
	if(pState->m_pInfo == nullptr)
		return false;

	png_byte aSignature[8];
	if(!Reader.Read(aSignature, sizeof(aSignature)) || png_sig_cmp(aSignature, 0, sizeof(aSignature)) != 0)
	{
		log_error("png", "file is not a valid PNG file (signature mismatch).");
		return false;
	}

	png_set_read_fn(pState->m_pPng, (png_bytep)&Reader, PngReadDataCallback);
	png_set_sig_bytes(pState->m_pPng, sizeof(aSignature));

#if defined(PNG_SET_USER_LIMITS_SUPPORTED)
	if(MaxDecodedBytes != std::numeric_limits<size_t>::max())
	{
		png_set_chunk_malloc_max(pState->m_pPng, 1024 * 1024);
		png_set_chunk_cache_max(pState->m_pPng, 16);
	}
#endif
	png_read_info(pState->m_pPng, pState->m_pInfo);

	if(Reader.Error())
	{
		// error already logged
		return false;
	}

	const png_uint_32 PngWidth = png_get_image_width(pState->m_pPng, pState->m_pInfo);
	const png_uint_32 PngHeight = png_get_image_height(pState->m_pPng, pState->m_pInfo);
	const png_byte BitDepth = png_get_bit_depth(pState->m_pPng, pState->m_pInfo);
	const int ColorType = png_get_color_type(pState->m_pPng, pState->m_pInfo);

	if(PngWidth == 0 || PngHeight == 0 || PngWidth > (png_uint_32)std::numeric_limits<int>::max() || PngHeight > (png_uint_32)std::numeric_limits<int>::max())
	{
		log_error("png", "image has invalid dimensions. width=%u height=%u", (unsigned)PngWidth, (unsigned)PngHeight);
		return false;
	}
	const int Width = (int)PngWidth;
	const int Height = (int)PngHeight;
	// 按 RGBA 的最坏情况限制像素，后续格式转换也不能突破调用方预算。
	size_t RgbaDataSize = 0;
	if(!ImageDataSize(Width, Height, CImageInfo::FORMAT_RGBA, RgbaDataSize) || RgbaDataSize > MaxDecodedBytes)
	{
		log_error("png", "image exceeds decoded byte budget. width=%d height=%d", Width, Height);
		return false;
	}

	if(BitDepth == 16)
	{
		png_set_strip_16(pState->m_pPng);
	}
	else if(BitDepth > 8 || BitDepth == 0)
	{
		log_error("png", "bit depth %d not supported.", BitDepth);
		return false;
	}

	if(ColorType == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_palette_to_rgb(pState->m_pPng);
	}

	if(ColorType == PNG_COLOR_TYPE_GRAY && BitDepth < 8)
	{
		png_set_expand_gray_1_2_4_to_8(pState->m_pPng);
	}

	if(png_get_valid(pState->m_pPng, pState->m_pInfo, PNG_INFO_tRNS))
	{
		png_set_tRNS_to_alpha(pState->m_pPng);
	}

	png_read_update_info(pState->m_pPng, pState->m_pInfo);

	const int ColorChannelCount = png_get_channels(pState->m_pPng, pState->m_pInfo);
	const size_t BytesInRow = png_get_rowbytes(pState->m_pPng, pState->m_pInfo);
	const CImageInfo::EImageFormat ImageFormat = ImageFormatFromChannelCount(ColorChannelCount);
	size_t ExpectedDataSize = 0;
	if(BytesInRow == 0 || ColorChannelCount <= 0 || (size_t)Width > std::numeric_limits<size_t>::max() / CImageInfo::PixelSize(ImageFormat) ||
		(size_t)BytesInRow != (size_t)Width * CImageInfo::PixelSize(ImageFormat) ||
		!ImageDataSize(Width, Height, ImageFormat, ExpectedDataSize))
	{
		log_error("png", "image dimensions are too large. width=%d height=%d rowbytes=%" PRIzu " channels=%d", Width, Height, BytesInRow, ColorChannelCount);
		return false;
	}

	Image.m_pData = static_cast<uint8_t *>(malloc(ExpectedDataSize));
	if(Image.m_pData == nullptr)
	{
		log_error("png", "failed to allocate image data. width=%d height=%d bytes=%" PRIzu, Width, Height, ExpectedDataSize);
		return false;
	}
	pState->m_pRows = new(std::nothrow) png_bytep[Height];
	if(pState->m_pRows == nullptr)
	{
		Image.Free();
		log_error("png", "failed to allocate row pointers. height=%d", Height);
		return false;
	}
	for(int y = 0; y < Height; ++y)
		pState->m_pRows[y] = &Image.m_pData[(size_t)y * BytesInRow];

	png_read_image(pState->m_pPng, pState->m_pRows);
	if(Reader.Error())
		Image.Free();
	else
	{
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = ImageFormat;
		PngliteIncompatible = PngliteIncompatibility(pState->m_pPng, pState->m_pInfo);
	}
	return !Reader.Error();
}

bool CImageLoader::LoadPng(IOHANDLE File, const char *pFilename, CImageInfo &Image, int &PngliteIncompatible)
{
	if(!File)
	{
		log_error("png", "failed to open file for reading. filename='%s'", pFilename);
		return false;
	}

	void *pFileData;
	unsigned FileDataSize;
	const bool ReadSuccess = io_read_all(File, &pFileData, &FileDataSize);
	io_close(File);
	if(!ReadSuccess)
	{
		log_error("png", "failed to read file. filename='%s'", pFilename);
		return false;
	}

	CByteBufferReader ImageReader(static_cast<const uint8_t *>(pFileData), FileDataSize);

	const bool LoadResult = CImageLoader::LoadPng(ImageReader, pFilename, Image, PngliteIncompatible);
	free(pFileData);
	if(!LoadResult)
	{
		log_error("png", "failed to load image from file. filename='%s'", pFilename);
		return false;
	}

	if(Image.m_Format != CImageInfo::FORMAT_RGB && Image.m_Format != CImageInfo::FORMAT_RGBA)
	{
		log_error("png", "image has unsupported format. filename='%s' format='%s'", pFilename, Image.FormatName());
		Image.Free();
		return false;
	}

	return true;
}

static void PngWriteDataCallback(png_structp pPngStruct, png_bytep pOutBytes, png_size_t ByteCountToWrite)
{
	CByteBufferWriter *pWriter = static_cast<CByteBufferWriter *>(png_get_io_ptr(pPngStruct));
	pWriter->Write(pOutBytes, ByteCountToWrite);
}

static void PngOutputFlushCallback(png_structp pPngStruct)
{
	// no need to flush memory buffer
}

static int PngColorTypeFromFormat(CImageInfo::EImageFormat Format)
{
	switch(Format)
	{
	case CImageInfo::FORMAT_R:
		return PNG_COLOR_TYPE_GRAY;
	case CImageInfo::FORMAT_RA:
		return PNG_COLOR_TYPE_GRAY_ALPHA;
	case CImageInfo::FORMAT_RGB:
		return PNG_COLOR_TYPE_RGB;
	case CImageInfo::FORMAT_RGBA:
		return PNG_COLOR_TYPE_RGBA;
	default:
		dbg_assert_failed("Format invalid");
	}
}

bool CImageLoader::ReadPngComment(IOHANDLE File, const char *pFilename, std::string &Comment)
{
	Comment.clear();
	if(File == nullptr)
		return false;

	CUserErrorStruct UserErrorStruct = {nullptr, pFilename, {}};
	png_structp pPngStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, &UserErrorStruct, PngErrorCallback, PngWarningCallback);
	if(pPngStruct == nullptr)
	{
		io_close(File);
		return false;
	}
	png_infop pPngInfo = png_create_info_struct(pPngStruct);
	if(pPngInfo == nullptr)
	{
		png_destroy_read_struct(&pPngStruct, nullptr, nullptr);
		io_close(File);
		return false;
	}
	if(setjmp(UserErrorStruct.m_JmpBuf))
	{
		png_destroy_read_struct(&pPngStruct, &pPngInfo, nullptr);
		io_close(File);
		return false;
	}

	// 元数据来自可移动的截图文件，限制辅助 chunk 的分配与数量。
#if defined(PNG_SET_USER_LIMITS_SUPPORTED)
	png_set_chunk_malloc_max(pPngStruct, 4096);
	png_set_chunk_cache_max(pPngStruct, 16);
#endif
	struct SMetadataInput
	{
		IOHANDLE m_File;
		size_t m_Remaining;
	} Input{File, 64 * 1024};
	png_set_read_fn(pPngStruct, &Input, [](png_structp pPng, png_bytep pBytes, png_size_t Count) {
		auto *pInput = static_cast<SMetadataInput *>(png_get_io_ptr(pPng));
		if(Count > pInput->m_Remaining || io_read(pInput->m_File, pBytes, Count) != Count)
			png_error(pPng, "PNG metadata exceeds read budget or is truncated");
		pInput->m_Remaining -= Count;
	});
	png_read_info(pPngStruct, pPngInfo);
	png_textp pText = nullptr;
	const int Count = png_get_text(pPngStruct, pPngInfo, &pText, nullptr);
	for(int Index = 0; Index < Count; ++Index)
	{
		if(pText[Index].key == nullptr || str_comp(pText[Index].key, "Comment") != 0 || pText[Index].text == nullptr)
			continue;
		const size_t Length = pText[Index].compression == PNG_ITXT_COMPRESSION_NONE || pText[Index].compression == PNG_ITXT_COMPRESSION_zTXt ? pText[Index].itxt_length : pText[Index].text_length;
		if(Length <= 4096)
			Comment.assign(pText[Index].text, Length);
		break;
	}
	png_destroy_read_struct(&pPngStruct, &pPngInfo, nullptr);
	io_close(File);
	return true;
}

bool CImageLoader::SavePng(CByteBufferWriter &Writer, const CImageInfo &Image, const char *pComment)
{
	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0 ||
		Image.m_Width > (size_t)std::numeric_limits<int>::max() || Image.m_Height > (size_t)std::numeric_limits<int>::max() ||
		Image.m_Format < CImageInfo::FORMAT_RGB || Image.m_Format > CImageInfo::FORMAT_RA)
		return false;
	size_t DataSize = 0;
	if(!ImageDataSize((int)Image.m_Width, (int)Image.m_Height, Image.m_Format, DataSize))
		return false;
	// 清理状态置于堆上，libpng longjmp 后不会读取值已改变的自动局部变量。
	struct SWriteState
	{
		CUserErrorStruct m_Error{nullptr, "PNG encode", {}};
		png_structp m_pPng = nullptr;
		png_infop m_pInfo = nullptr;
		png_bytepp m_pRows = nullptr;
		~SWriteState()
		{
			delete[] m_pRows;
			if(m_pPng != nullptr)
				png_destroy_write_struct(&m_pPng, &m_pInfo);
		}
	};
	auto pState = std::make_unique<SWriteState>();
	pState->m_pPng = png_create_write_struct(PNG_LIBPNG_VER_STRING, &pState->m_Error, PngErrorCallback, PngWarningCallback);
	if(pState->m_pPng == nullptr)
		return false;
	if(setjmp(pState->m_Error.m_JmpBuf))
		return false;
	pState->m_pInfo = png_create_info_struct(pState->m_pPng);
	if(pState->m_pInfo == nullptr)
		return false;
	png_set_write_fn(pState->m_pPng, &Writer, PngWriteDataCallback, PngOutputFlushCallback);
	png_set_IHDR(pState->m_pPng, pState->m_pInfo, Image.m_Width, Image.m_Height, 8, PngColorTypeFromFormat(Image.m_Format), PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	if(pComment != nullptr && pComment[0] != '\0')
	{
		png_text Text = {};
		Text.compression = PNG_ITXT_COMPRESSION_NONE;
		Text.key = const_cast<char *>("Comment");
		Text.text = const_cast<char *>(pComment);
		Text.itxt_length = str_length(pComment);
		Text.lang = const_cast<char *>("");
		Text.lang_key = const_cast<char *>("");
		png_set_text(pState->m_pPng, pState->m_pInfo, &Text, 1);
	}
	png_write_info(pState->m_pPng, pState->m_pInfo);
	pState->m_pRows = new png_bytep[Image.m_Height];
	for(size_t Y = 0; Y < Image.m_Height; ++Y)
		pState->m_pRows[Y] = Image.m_pData + Y * Image.m_Width * CImageInfo::PixelSize(Image.m_Format);
	png_write_image(pState->m_pPng, pState->m_pRows);
	png_write_end(pState->m_pPng, pState->m_pInfo);
	return true;
}

bool CImageLoader::SavePng(IOHANDLE File, const char *pFilename, const CImageInfo &Image, const char *pComment)
{
	if(!File)
	{
		log_error("png", "failed to open file for writing. filename='%s'", pFilename);
		return false;
	}

	CByteBufferWriter Writer;
	if(!CImageLoader::SavePng(Writer, Image, pComment))
	{
		// error already logged
		io_close(File);
		return false;
	}

	const bool WriteSuccess = io_write(File, Writer.Data(), Writer.Size()) == Writer.Size();
	if(!WriteSuccess)
	{
		log_error("png", "failed to write PNG data to file. filename='%s'", pFilename);
	}
	io_close(File);
	return WriteSuccess;
}

bool CImageLoader::SaveWebP(CByteBufferWriter &Writer, const CImageInfo &Image)
{
#if defined(CONF_WEBP)
	CImageInfo RgbaImage;
	const CImageInfo *pEncodeImage = &Image;
	if(Image.m_Format != CImageInfo::FORMAT_RGBA)
	{
		RgbaImage = Image.DeepCopy();
		ConvertToRgba(RgbaImage);
		pEncodeImage = &RgbaImage;
	}

	uint8_t *pOutput = nullptr;
	const size_t EncodedSize = WebPEncodeLosslessRGBA(
		pEncodeImage->m_pData,
		(int)pEncodeImage->m_Width,
		(int)pEncodeImage->m_Height,
		(int)(pEncodeImage->m_Width * 4),
		&pOutput);
	if(RgbaImage.m_pData != nullptr)
		RgbaImage.Free();
	if(EncodedSize == 0 || pOutput == nullptr)
	{
		log_error("webp", "failed to encode WebP image");
		return false;
	}

	Writer.Write(pOutput, EncodedSize);
	WebPFree(pOutput);
	return true;
#else
	(void)Writer;
	(void)Image;
	log_error("webp", "cannot save WebP: client was built without libwebp support");
	return false;
#endif
}

bool CImageLoader::SaveWebP(IOHANDLE File, const char *pFilename, const CImageInfo &Image)
{
	if(!File)
	{
		log_error("webp", "failed to open file for writing. filename='%s'", pFilename);
		return false;
	}

	CByteBufferWriter Writer;
	if(!CImageLoader::SaveWebP(Writer, Image))
	{
		io_close(File);
		return false;
	}

	const bool WriteSuccess = io_write(File, Writer.Data(), Writer.Size()) == Writer.Size();
	if(!WriteSuccess)
	{
		log_error("webp", "failed to write WebP data to file. filename='%s'", pFilename);
	}
	io_close(File);
	return WriteSuccess;
}

bool CImageLoader::LoadWebP(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes)
{
#if defined(CONF_WEBP)
	// Read all data from reader
	const size_t DataSize = Reader.Size();
	if(DataSize == 0)
	{
		log_error("webp", "empty data for '%s'", pContextName);
		return false;
	}

	std::vector<uint8_t> vData(DataSize);
	if(!Reader.Read(vData.data(), DataSize))
	{
		log_error("webp", "failed to read data for '%s'", pContextName);
		return false;
	}

	// Use libwebp to decode WebP
	int Width = 0, Height = 0;

	// First check if it's a valid WebP file and get dimensions
	if(!WebPGetInfo(vData.data(), vData.size(), &Width, &Height))
	{
		log_error("webp", "invalid WebP data for '%s'", pContextName);
		return false;
	}

	if(Width == 0 || Height == 0)
	{
		log_error("webp", "invalid image dimensions for '%s': %dx%d", pContextName, Width, Height);
		return false;
	}

	// 先验证预算，再直接解码到最终缓冲，避免同时持有两份完整 RGBA。
	size_t DataSizeOut = 0;
	if(Width < 0 || Height < 0 || Width > std::numeric_limits<int>::max() / 4 ||
		!ImageDataSize(Width, Height, CImageInfo::FORMAT_RGBA, DataSizeOut) || DataSizeOut > MaxDecodedBytes)
	{
		log_error("webp", "image exceeds decoded byte budget for '%s': %dx%d", pContextName, Width, Height);
		return false;
	}
	uint8_t *pDestData = (uint8_t *)malloc(DataSizeOut);
	if(!pDestData)
	{
		log_error("webp", "failed to allocate output buffer for '%s'", pContextName);
		return false;
	}
	if(!WebPDecodeRGBAInto(vData.data(), vData.size(), pDestData, DataSizeOut, Width * 4))
	{
		free(pDestData);
		log_error("webp", "failed to decode WebP for '%s'", pContextName);
		return false;
	}

	// Fill image info
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = pDestData;

	return true;
#else
	(void)Image;
	(void)MaxDecodedBytes;

	const size_t DataSize = Reader.Size();
	if(DataSize < 12)
	{
		return false;
	}

	std::array<uint8_t, 12> aHeader;
	if(!Reader.Read(aHeader.data(), aHeader.size()))
	{
		return false;
	}

	static constexpr uint8_t WEBP_RIFF[] = {'R', 'I', 'F', 'F'};
	static constexpr uint8_t WEBP_WEBP[] = {'W', 'E', 'B', 'P'};
	const bool IsWebP = mem_comp(aHeader.data(), WEBP_RIFF, std::size(WEBP_RIFF)) == 0 &&
			    mem_comp(aHeader.data() + 8, WEBP_WEBP, std::size(WEBP_WEBP)) == 0;
	if(IsWebP)
	{
		log_error("webp", "cannot load '%s': client was built without libwebp support", pContextName);
	}
	return false;
#endif
}

bool CImageLoader::LoadWebP(IOHANDLE File, const char *pFilename, CImageInfo &Image)
{
#if defined(CONF_WEBP)
	if(!File)
	{
		log_error("webp", "failed to open file for reading. filename='%s'", pFilename);
		return false;
	}

	void *pFileData;
	unsigned FileDataSize;
	const bool ReadSuccess = io_read_all(File, &pFileData, &FileDataSize);
	io_close(File);
	if(!ReadSuccess)
	{
		log_error("webp", "failed to read file. filename='%s'", pFilename);
		return false;
	}

	CByteBufferReader ImageReader(static_cast<const uint8_t *>(pFileData), FileDataSize);

	const bool LoadResult = CImageLoader::LoadWebP(ImageReader, pFilename, Image);
	free(pFileData);
	if(!LoadResult)
	{
		log_error("webp", "failed to load image from file. filename='%s'", pFilename);
		return false;
	}

	return true;
#else
	(void)Image;

	if(!File)
	{
		log_error("webp", "failed to open file for reading. filename='%s'", pFilename);
		return false;
	}

	io_close(File);
	log_error("webp", "cannot load image from file. filename='%s', client was built without libwebp support", pFilename);
	return false;
#endif
}

bool CImageLoader::LoadPng(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes)
{
	CByteBufferReader Reader(static_cast<const uint8_t *>(pData), Size);
	int PngliteIncompatible = 0;
	return LoadPng(Reader, pContextName, Image, PngliteIncompatible, MaxDecodedBytes);
}

bool CImageLoader::LoadWebP(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes)
{
	CByteBufferReader Reader(static_cast<const uint8_t *>(pData), Size);
	return LoadWebP(Reader, pContextName, Image, MaxDecodedBytes);
}
