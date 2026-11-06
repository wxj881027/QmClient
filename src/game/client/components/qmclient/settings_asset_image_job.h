#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_IMAGE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_IMAGE_JOB_H

#include "bounded_file_reader.h"
#include "settings_asset_memory.h"

#include <base/lock.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/assets_preview_scale.h>

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 后台只准备文件与像素，主线程通过独立完成发布接收结果。
class CFullAsyncImageLoadJob : public IJob
{
public:
	using FDecode = bool (*)(const void *, unsigned, const char *, CImageInfo &);
	using FResize = void (*)(CImageInfo &, int, int);
	struct SResult
	{
		CImageInfo m_Image;
		CSettingsAssetMemoryLease m_MemoryLease;
		bool m_Success = false;
		bool m_Resized = false;
		int m_SourceWidth = 0;
		int m_SourceHeight = 0;
		SResult() = default;
		SResult(SResult &&) = default;
		SResult &operator=(SResult &&) = default;
		~SResult() { m_Image.Free(); }
	};

private:
	std::vector<std::string> m_vPossiblePaths;
	IStorage *m_pStorage;
	int m_StorageType;
	int m_MaxTextureSize;
	size_t m_MaxDecodedBytes;
	std::string m_Name;
	FDecode m_pDecode;
	FResize m_pResize;
	CSettingsAssetMemoryLease m_WorkingMemory;
	mutable CLock m_Lock;
	SResult m_Result;
	bool m_Completed = false;

	static bool DecodeImage(const void *pData, unsigned Size, const char *pName, CImageInfo &Image, size_t MaxDecodedBytes)
	{
		constexpr uint8_t PngSignature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		constexpr uint8_t WebpRiff[] = {0x52, 0x49, 0x46, 0x46};
		constexpr uint8_t WebpWebp[] = {0x57, 0x45, 0x42, 0x50};
		const auto *pBytes = static_cast<const uint8_t *>(pData);
		if(Size >= 12 && memcmp(pBytes, WebpRiff, 4) == 0 && memcmp(pBytes + 8, WebpWebp, 4) == 0)
			return CImageLoader::LoadWebP(pData, Size, pName, Image, MaxDecodedBytes);
		if(Size >= 8 && memcmp(pBytes, PngSignature, 8) == 0)
			return CImageLoader::LoadPng(pData, Size, pName, Image, MaxDecodedBytes);
		if(CImageLoader::LoadWebP(pData, Size, pName, Image, MaxDecodedBytes))
			return true;
		Image.Free();
		return CImageLoader::LoadPng(pData, Size, pName, Image, MaxDecodedBytes);
	}

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		CSettingsAssetMemoryLease WorkingMemory(std::move(m_WorkingMemory));
		if(!WorkingMemory.HasReservation())
			return;
		SResult Result;
		{
			CQmCheckedFileData FileData;
			for(const auto &Path : m_vPossiblePaths)
			{
				if(FileData.Read(*m_pStorage, Path.c_str(), m_StorageType, LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE))
					break;
			}
			if(FileData.Data() != nullptr)
			{
				auto &Image = Result.m_Image;
				// 四参解码器保留为外部测试接缝；生产默认路径在实际解码分配前传入预算。
				Result.m_Success = m_pDecode != nullptr ? m_pDecode(FileData.Data(), FileData.Size(), m_Name.c_str(), Image) :
									  DecodeImage(FileData.Data(), FileData.Size(), m_Name.c_str(), Image, m_MaxDecodedBytes);
				Result.m_Success = Result.m_Success && Image.m_pData != nullptr && Image.m_Width > 0 && Image.m_Height > 0 &&
						   Image.m_Format >= CImageInfo::FORMAT_RGB && Image.m_Format <= CImageInfo::FORMAT_RA &&
						   Image.m_Width <= std::numeric_limits<int>::max() && Image.m_Height <= std::numeric_limits<int>::max() &&
						   Image.m_Width <= m_MaxDecodedBytes / 4u / Image.m_Height;
				if(Result.m_Success && m_MaxTextureSize > 0)
				{
					Result.m_SourceWidth = (int)Image.m_Width;
					Result.m_SourceHeight = (int)Image.m_Height;
					const auto Target = ComputePreviewTargetSize(Result.m_SourceWidth, Result.m_SourceHeight, m_MaxTextureSize);
					if(Target.m_Resized)
					{
						m_pResize(Image, Target.m_Width, Target.m_Height);
						Result.m_Success = Image.m_pData != nullptr && Image.m_Width == (size_t)Target.m_Width && Image.m_Height == (size_t)Target.m_Height;
						Result.m_Resized = Result.m_Success;
					}
				}
			}
		}
		// 编码缓冲及 codec/缩放临时已销毁，此后只保留最终图与原生上传并存上界。
		uint64_t ReadyBytes = 0;
		Result.m_Success = Result.m_Success && SettingsAssetMemoryReadyBytes(Result.m_Image, ReadyBytes) && WorkingMemory.Shrink(ReadyBytes);
		if(!Result.m_Success)
		{
			Result.m_Image.Free();
			WorkingMemory.Reset();
		}
		else
			Result.m_MemoryLease = std::move(WorkingMemory);
		const CLockScope Lock(m_Lock);
		m_Result = std::move(Result);
		m_Completed = true;
	}

public:
	CFullAsyncImageLoadJob(std::vector<std::string> &&vPossiblePaths, IStorage *pStorage, const char *pName,
		int StorageType = IStorage::TYPE_ALL, int MaxTextureSize = 0, FDecode pDecode = nullptr,
		FResize pResize = static_cast<FResize>(ResizeImage), size_t MaxDecodedBytes = ASSET_PREVIEW_MAX_DECODED_BYTES,
		CSettingsAssetMemoryBudget *pBudget = nullptr) :
		m_vPossiblePaths(std::move(vPossiblePaths)),
		m_pStorage(pStorage),
		m_StorageType(StorageType),
		m_MaxTextureSize(MaxTextureSize),
		m_MaxDecodedBytes(MaxDecodedBytes),
		m_Name(pName != nullptr ? pName : ""),
		m_pDecode(pDecode),
		m_pResize(pResize)
	{
		uint64_t WorkingBytes = 0;
		if(pStorage == nullptr || pName == nullptr || m_vPossiblePaths.empty() || (MaxTextureSize > 0 && pResize == nullptr) ||
			!SettingsAssetMemoryComputeWorkingBytes(LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE, MaxDecodedBytes, MaxTextureSize, WorkingBytes))
			throw std::invalid_argument("invalid settings asset image input");
		if(!(pBudget != nullptr ? *pBudget : SettingsAssetMemoryDefaultBudget()).TryAcquire(WorkingBytes, m_WorkingMemory))
			throw std::bad_alloc();
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed || Finished();
	}

	SResult GetResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		// 私有完整发布独立于池终态；执行异常前尚未发布时不读候选。
		if(!m_Completed)
			return {};
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

// 准备和接收成功后才向页面交出等待对象，拒收不留下永远排队的任务。
inline std::shared_ptr<CFullAsyncImageLoadJob> QmTryQueueSettingsAssetImage(std::vector<std::string> &&vPaths,
	IStorage *pStorage, IEngine &Engine, const char *pName, int StorageType, int MaxTextureSize, size_t MaxDecodedBytes = ASSET_PREVIEW_MAX_DECODED_BYTES,
	CSettingsAssetMemoryBudget *pBudget = nullptr, ESettingsAssetImageAdmission *pAdmission = nullptr)
{
	if(pAdmission != nullptr)
		*pAdmission = ESettingsAssetImageAdmission::DEFERRED;
	uint64_t WorkingBytes = 0;
	if(pStorage == nullptr || pName == nullptr || vPaths.empty() ||
		!SettingsAssetMemoryComputeWorkingBytes(LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE, MaxDecodedBytes, MaxTextureSize, WorkingBytes))
	{
		if(pAdmission != nullptr)
			*pAdmission = ESettingsAssetImageAdmission::FAILED;
		return nullptr;
	}
	try
	{
		auto pJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPaths), pStorage, pName, StorageType, MaxTextureSize,
			nullptr, static_cast<CFullAsyncImageLoadJob::FResize>(ResizeImage), MaxDecodedBytes, pBudget);
		if(Engine.TryAddJob(pJob))
		{
			if(pAdmission != nullptr)
				*pAdmission = ESettingsAssetImageAdmission::ACCEPTED;
			return pJob;
		}
	}
	catch(const std::bad_alloc &)
	{
		// 准备失败保留可重试状态，不发布尚未接收的候选。
	}
	catch(const std::length_error &)
	{
		// 准备和明确拒收均可重试，不向消费者发布解码失败。
	}
	return nullptr;
}

#endif
