#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_IMAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_IMAGE_H

#include <base/hash.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>

#include <cstdlib>
#include <utility>

namespace qm_community_icons
{
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
	ELoadResult LoadImage(TStorage &Storage, const char *pPath, int StorageType, CImageInfo &Original, CImageInfo &Grayscale, SHA256_DIGEST &Sha256) noexcept
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
			if(!CImageLoader::LoadPng(File.m_pData, FileSize, pPath, Decoded.m_Image))
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
