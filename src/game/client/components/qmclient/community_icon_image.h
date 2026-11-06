#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_IMAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_IMAGE_H

#include "community_icon_limits.h"

#include <base/hash.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>

namespace qm_community_icons
{
	// 从同一打开的文件检查长度并读取，禁止先ReadFile后才拒绝大缓冲。
	class CBoundedFileReader
	{
		IStorage &m_Storage;
		std::size_t m_MaxBytes;
		bool m_TooLarge = false;

	public:
		explicit CBoundedFileReader(IStorage &Storage, std::size_t MaxBytes = MAX_FILE_BYTES) :
			m_Storage(Storage), m_MaxBytes(MaxBytes) {}
		bool TooLarge() const { return m_TooLarge; }
		bool ReadFile(const char *pPath, int StorageType, void **ppData, unsigned *pSize)
		{
			*ppData = nullptr;
			*pSize = 0;
			m_TooLarge = false;
			struct SFile
			{
				IOHANDLE m_Handle;
				~SFile()
				{
					if(m_Handle)
						io_close(m_Handle);
				}
			} File{m_Storage.OpenFile(pPath, IOFLAG_READ, StorageType)};
			if(!File.m_Handle)
				return false;
			const int64_t Length = io_length(File.m_Handle);
			if(Length < 0)
				return false;
			if(static_cast<uint64_t>(Length) > m_MaxBytes || static_cast<uint64_t>(Length) > std::numeric_limits<unsigned>::max())
			{
				m_TooLarge = true;
				return false;
			}
			const unsigned Size = static_cast<unsigned>(Length);
			std::unique_ptr<uint8_t, decltype(&std::free)> pData(static_cast<uint8_t *>(std::malloc(Size ? Size : 1)), &std::free);
			if(!pData || io_read(File.m_Handle, pData.get(), Size) != Size)
				return false;
			uint8_t Extra;
			if(io_read(File.m_Handle, &Extra, 1) != 0 || io_error(File.m_Handle))
				return false;
			*ppData = pData.release();
			*pSize = Size;
			return true;
		}
	};

	enum class ELoadResult
	{
		LOADED,
		READ_FAILED,
		DECODE_FAILED,
		FORMAT_UNSUPPORTED,
		COPY_FAILED,
		PROCESSING_FAILED,
	};

	// 仅借用存储；解码与摘要消费同一份快照，完整准备后再交接输出。
	template<typename TStorage>
	ELoadResult LoadImage(TStorage &Storage, const char *pPath, int StorageType, CImageInfo &Original, CImageInfo &Grayscale, SHA256_DIGEST &Sha256, std::size_t MaxDecodedBytes = MAX_DECODED_RGBA_BYTES) noexcept
	{
		struct SFile
		{
			void *m_pData = nullptr;
			~SFile() { Free(); }
			void Free()
			{
				std::free(m_pData);
				m_pData = nullptr;
			}
		} File;
		struct SImage
		{
			CImageInfo m_Image;
			~SImage() { m_Image.Free(); }
		} Decoded, Grey;
		try
		{
			unsigned FileSize = 0;
			if(!Storage.ReadFile(pPath, StorageType, &File.m_pData, &FileSize))
				return ELoadResult::READ_FAILED;
			if(!CImageLoader::LoadPng(File.m_pData, FileSize, pPath, Decoded.m_Image, MaxDecodedBytes))
				return ELoadResult::DECODE_FAILED;
			if(Decoded.m_Image.m_Format != CImageInfo::FORMAT_RGBA)
				return ELoadResult::FORMAT_UNSUPPORTED;
			const auto Digest = sha256(File.m_pData, FileSize);
			File.Free();
			Grey.m_Image = Decoded.m_Image.DeepCopy();
			if(!Grey.m_Image.m_pData)
				return ELoadResult::COPY_FAILED;
			ConvertToGrayscale(Grey.m_Image);
			Original = std::move(Decoded.m_Image);
			Grayscale = std::move(Grey.m_Image);
			Sha256 = Digest;
			return ELoadResult::LOADED;
		}
		catch(...)
		{
			return ELoadResult::PROCESSING_FAILED;
		}
	}
}

#endif
