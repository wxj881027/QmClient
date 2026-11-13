#include "image_loader.h"

#include "image_manipulation.h"

#include <base/log.h>
#include <base/system.h>

#include <png.h>
#if !defined(PNG_USER_MEM_SUPPORTED)
#error "PNG decoding requires libpng user memory callbacks for resource failure classification."
#endif
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

	if(Size <= m_Size - m_ReadOffset)
	{
		if(Size != 0)
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

bool CByteBufferWriter::Write(const void *pData, size_t Size) noexcept
{
	if(!Size)
		return true;
	const size_t WriteOffset = m_vBuffer.size();
	if(!pData || Size > m_vBuffer.max_size() - WriteOffset)
		return false;
	try
	{
		m_vBuffer.resize(WriteOffset + Size);
	}
	catch(...)
	{
		return false;
	}
	mem_copy(&m_vBuffer[WriteOffset], pData, Size);
	return true;
}

namespace
{
	template<typename TEncode>
	bool SaveImageFile(IOHANDLE File, const char *pFilename, const char *pFormat, CImageFileOperations &Operations, TEncode &&Encode)
	{
		if(File == nullptr)
		{
			log_error(pFormat, "failed to open file for writing. filename='%s'", pFilename);
			return false;
		}
		// 编码异常也关闭句柄；显式关闭先交出所有权，关闭失败不能再关一次。
		const auto Close = [&Operations](void *pFile) { Operations.Close(pFile); };
		std::unique_ptr<void, decltype(Close)> Owned(File, Close);
		CByteBufferWriter Writer;
		if(!Encode(Writer))
			return false;
		const bool Written = Writer.Size() <= std::numeric_limits<unsigned>::max() &&
				     Operations.Write(Owned.get(), Writer.Data(), static_cast<unsigned>(Writer.Size())) == Writer.Size();
		const bool Closed = Operations.Close(Owned.release()) == 0;
		if(!Written)
			log_error(pFormat, "failed to write image data to file. filename='%s'", pFilename);
		if(!Closed)
			log_error(pFormat, "failed to close image file. filename='%s'", pFilename);
		return Written && Closed;
	}

	class CUserErrorStruct
	{
	public:
		CByteBufferReader *m_pReader;
		const char *m_pContextName;
		std::jmp_buf m_JmpBuf;
	};

	struct SPngReadState
	{
		CUserErrorStruct m_Error;
		CPngReadAllocator &m_Allocator;
		png_structp m_pPng = nullptr;
		png_infop m_pInfo = nullptr;
		png_bytepp m_pRows = nullptr;
		bool m_AllocationFailed = false;

		explicit SPngReadState(CPngReadAllocator &Allocator) noexcept :
			m_Allocator(Allocator) {}
		~SPngReadState()
		{
			m_Allocator.Deallocate(CPngReadAllocator::EKind::ROWS, m_pRows);
			if(m_pPng != nullptr)
				png_destroy_read_struct(&m_pPng, &m_pInfo, nullptr);
		}
	};

	struct SPngReadStateDeleter
	{
		CPngReadAllocator &m_Allocator;
		void operator()(SPngReadState *pState) const noexcept
		{
			if(pState)
			{
				pState->~SPngReadState();
				m_Allocator.Deallocate(CPngReadAllocator::EKind::STATE, pState);
			}
		}
	};

	png_voidp AllocatePngRead(png_structp pPng, png_alloc_size_t Size) noexcept
	{
		auto &State = *static_cast<SPngReadState *>(png_get_mem_ptr(pPng));
		void *pData = State.m_Allocator.Allocate(CPngReadAllocator::EKind::LIBPNG, Size);
		if(pData == nullptr)
			State.m_AllocationFailed = true;
		return pData;
	}

	void DeallocatePngRead(png_structp pPng, png_voidp pData) noexcept
	{
		auto &State = *static_cast<SPngReadState *>(png_get_mem_ptr(pPng));
		State.m_Allocator.Deallocate(CPngReadAllocator::EKind::LIBPNG, pData);
	}

} // namespace

void *CPngReadAllocator::Allocate(EKind Kind, size_t Size) noexcept
{
	switch(Kind)
	{
	case EKind::STATE: return ::operator new(Size, std::nothrow);
	case EKind::ROWS: return ::operator new[](Size, std::nothrow);
	case EKind::PIXELS:
	case EKind::LIBPNG: return malloc(Size);
	}
	return nullptr;
}

void CPngReadAllocator::Deallocate(EKind Kind, void *pData) noexcept
{
	switch(Kind)
	{
	case EKind::STATE: ::operator delete(pData); break;
	case EKind::ROWS: ::operator delete[](pData); break;
	case EKind::PIXELS:
	case EKind::LIBPNG: free(pData); break;
	}
}

unsigned CImageFileOperations::Write(IOHANDLE File, const void *pData, unsigned Size) noexcept
{
	return io_write(File, pData, Size);
}

int CImageFileOperations::Close(IOHANDLE File) noexcept
{
	return io_close(File);
}

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
	return LoadPngChecked(Reader, pContextName, Image, PngliteIncompatible, MaxDecodedBytes) == EPngLoadResult::SUCCESS;
}

CImageLoader::EPngLoadResult CImageLoader::LoadPngChecked(CByteBufferReader &Reader, const char *pContextName, CImageInfo &Image, int &PngliteIncompatible, size_t MaxDecodedBytes, CPngReadAllocator *pAllocator)
{
	CPngReadAllocator DefaultAllocator;
	CPngReadAllocator &Allocator = pAllocator != nullptr ? *pAllocator : DefaultAllocator;
	void *pStateMemory = Allocator.Allocate(CPngReadAllocator::EKind::STATE, sizeof(SPngReadState));
	if(pStateMemory == nullptr)
		return EPngLoadResult::RESOURCE_ERROR;
	// owner 在 setjmp 前建立；回跳后只读取堆上的错误标记和已登记指针。
	const std::unique_ptr<SPngReadState, SPngReadStateDeleter> pState(new(pStateMemory) SPngReadState(Allocator), SPngReadStateDeleter{Allocator});
	pState->m_Error = {&Reader, pContextName, {}};
	if(setjmp(pState->m_Error.m_JmpBuf))
	{
		Image.Free();
		return pState->m_AllocationFailed ? EPngLoadResult::RESOURCE_ERROR : EPngLoadResult::INVALID;
	}
	pState->m_pPng = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, &pState->m_Error, PngErrorCallback, PngWarningCallback, pState.get(), AllocatePngRead, DeallocatePngRead);
	if(pState->m_pPng == nullptr)
		return pState->m_AllocationFailed ? EPngLoadResult::RESOURCE_ERROR : EPngLoadResult::INVALID;
	pState->m_pInfo = png_create_info_struct(pState->m_pPng);
	if(pState->m_pInfo == nullptr)
		return pState->m_AllocationFailed ? EPngLoadResult::RESOURCE_ERROR : EPngLoadResult::INVALID;

	png_byte aSignature[8];
	if(!Reader.Read(aSignature, sizeof(aSignature)) || png_sig_cmp(aSignature, 0, sizeof(aSignature)) != 0)
	{
		log_error("png", "file is not a valid PNG file (signature mismatch).");
		return EPngLoadResult::INVALID;
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
		return EPngLoadResult::INVALID;
	}

	const png_uint_32 PngWidth = png_get_image_width(pState->m_pPng, pState->m_pInfo);
	const png_uint_32 PngHeight = png_get_image_height(pState->m_pPng, pState->m_pInfo);
	const png_byte BitDepth = png_get_bit_depth(pState->m_pPng, pState->m_pInfo);
	const int ColorType = png_get_color_type(pState->m_pPng, pState->m_pInfo);

	if(PngWidth == 0 || PngHeight == 0 || PngWidth > (png_uint_32)std::numeric_limits<int>::max() || PngHeight > (png_uint_32)std::numeric_limits<int>::max())
	{
		log_error("png", "image has invalid dimensions. width=%u height=%u", (unsigned)PngWidth, (unsigned)PngHeight);
		return EPngLoadResult::INVALID;
	}
	const int Width = (int)PngWidth;
	const int Height = (int)PngHeight;
	// 按 RGBA 的最坏情况限制像素，后续格式转换也不能突破调用方预算。
	size_t RgbaDataSize = 0;
	if(!ImageDataSize(Width, Height, CImageInfo::FORMAT_RGBA, RgbaDataSize) || RgbaDataSize > MaxDecodedBytes)
	{
		log_error("png", "image exceeds decoded byte budget. width=%d height=%d", Width, Height);
		return EPngLoadResult::INVALID;
	}

	if(BitDepth == 16)
	{
		png_set_strip_16(pState->m_pPng);
	}
	else if(BitDepth > 8 || BitDepth == 0)
	{
		log_error("png", "bit depth %d not supported.", BitDepth);
		return EPngLoadResult::INVALID;
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
		return EPngLoadResult::INVALID;
	}

	Image.m_pData = static_cast<uint8_t *>(Allocator.Allocate(CPngReadAllocator::EKind::PIXELS, ExpectedDataSize));
	if(Image.m_pData == nullptr)
	{
		log_error("png", "failed to allocate image data. width=%d height=%d bytes=%" PRIzu, Width, Height, ExpectedDataSize);
		return EPngLoadResult::RESOURCE_ERROR;
	}
	pState->m_pRows = static_cast<png_bytepp>(Allocator.Allocate(CPngReadAllocator::EKind::ROWS, static_cast<size_t>(Height) * sizeof(png_bytep)));
	if(pState->m_pRows == nullptr)
	{
		Image.Free();
		log_error("png", "failed to allocate row pointers. height=%d", Height);
		return EPngLoadResult::RESOURCE_ERROR;
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
	// libpng 可跳过非关键元数据的分配失败；正文仍成功时保留旧 bool 成功语义。
	return Reader.Error() ? EPngLoadResult::INVALID : EPngLoadResult::SUCCESS;
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

static void PngWriteDataCallback(png_structp pPngStruct, png_bytep pOutBytes, png_size_t ByteCountToWrite) noexcept
{
	CByteBufferWriter *pWriter = static_cast<CByteBufferWriter *>(png_get_io_ptr(pPngStruct));
	// Write已完成C++展开并返回，之后才进入libpng自己的错误跳转。
	if(!pWriter->Write(pOutBytes, ByteCountToWrite))
		png_error(pPngStruct, "Could not append encoded PNG bytes");
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
	// scope在setjmp之前建立，成功标志只在全部png调用结束后设置。
	CByteBufferWriter::CWriteScope Output(Writer);
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
	Output.Commit();
	return true;
}

bool CImageLoader::SavePng(IOHANDLE File, const char *pFilename, const CImageInfo &Image, const char *pComment)
{
	CImageFileOperations Operations;
	return SavePng(File, pFilename, Image, pComment, Operations);
}

bool CImageLoader::SavePng(IOHANDLE File, const char *pFilename, const CImageInfo &Image, const char *pComment, CImageFileOperations &Operations)
{
	return SaveImageFile(File, pFilename, "png", Operations, [&](CByteBufferWriter &Writer) { return SavePng(Writer, Image, pComment); });
}

bool CImageLoader::SaveWebP(CByteBufferWriter &Writer, const CImageInfo &Image)
{
#if defined(CONF_WEBP)
	if(!Image.m_pData || Image.m_Width == 0 || Image.m_Height == 0 ||
		Image.m_Width > WEBP_MAX_DIMENSION || Image.m_Height > WEBP_MAX_DIMENSION ||
		Image.m_Format < CImageInfo::FORMAT_RGB || Image.m_Format > CImageInfo::FORMAT_RA)
		return false;
	size_t DataSize;
	if(!ImageDataSize(Image.m_Width, Image.m_Height, Image.m_Format, DataSize))
		return false;
	CByteBufferWriter::CWriteScope Output(Writer);
	struct SRgbaOwner
	{
		CImageInfo m_Image;
		~SRgbaOwner() { m_Image.Free(); }
	} Rgba;
	const CImageInfo *pEncodeImage = &Image;
	if(Image.m_Format != CImageInfo::FORMAT_RGBA)
	{
		// ConvertToRgbaAlloc的布尔值表示是否原本RGBA，实际转换成功以目标指针确认。
		ConvertToRgbaAlloc(Rgba.m_Image.m_pData, Image);
		if(!Rgba.m_Image.m_pData)
			return false;
		Rgba.m_Image.m_Width = Image.m_Width;
		Rgba.m_Image.m_Height = Image.m_Height;
		Rgba.m_Image.m_Format = CImageInfo::FORMAT_RGBA;
		pEncodeImage = &Rgba.m_Image;
	}

	uint8_t *pOutput = nullptr;
	const size_t EncodedSize = WebPEncodeLosslessRGBA(
		pEncodeImage->m_pData,
		(int)pEncodeImage->m_Width,
		(int)pEncodeImage->m_Height,
		(int)(pEncodeImage->m_Width * 4),
		&pOutput);
	std::unique_ptr<uint8_t, decltype(&WebPFree)> Encoded(pOutput, WebPFree);
	Rgba.m_Image.Free();
	if(EncodedSize == 0 || !Encoded)
	{
		log_error("webp", "failed to encode WebP image");
		return false;
	}

	if(!Writer.Write(Encoded.get(), EncodedSize))
		return false;
	Output.Commit();
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
	CImageFileOperations Operations;
	return SaveWebP(File, pFilename, Image, Operations);
}

bool CImageLoader::SaveWebP(IOHANDLE File, const char *pFilename, const CImageInfo &Image, CImageFileOperations &Operations)
{
	return SaveImageFile(File, pFilename, "webp", Operations, [&](CByteBufferWriter &Writer) { return SaveWebP(Writer, Image); });
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
	return LoadPngChecked(pData, Size, pContextName, Image, MaxDecodedBytes) == EPngLoadResult::SUCCESS;
}

CImageLoader::EPngLoadResult CImageLoader::LoadPngChecked(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes, CPngReadAllocator *pAllocator)
{
	CByteBufferReader Reader(static_cast<const uint8_t *>(pData), Size);
	int PngliteIncompatible = 0;
	return LoadPngChecked(Reader, pContextName, Image, PngliteIncompatible, MaxDecodedBytes, pAllocator);
}

bool CImageLoader::LoadWebP(const void *pData, size_t Size, const char *pContextName, CImageInfo &Image, size_t MaxDecodedBytes)
{
	CByteBufferReader Reader(static_cast<const uint8_t *>(pData), Size);
	return LoadWebP(Reader, pContextName, Image, MaxDecodedBytes);
}
