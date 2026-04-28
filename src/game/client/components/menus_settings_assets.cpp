#include "menus.h"
#include "assets_resource_registry.h"
#include "assets_preview_scale.h"
#include "background.h"

#include <algorithm>

#include <base/lock.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
bool AssetsPerfDebugEnabled()
{
	return g_Config.m_QmPerfDebug != 0;
}

double AssetsPerfDebugThresholdMs()
{
	return g_Config.m_QmPerfDebugThresholdMs > 0 ? g_Config.m_QmPerfDebugThresholdMs : 1.0;
}

void LogAssetsPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
{
	if(!AssetsPerfDebugEnabled())
		return;
	if(DurationMs < AssetsPerfDebugThresholdMs())
		return;

	if(pExtra != nullptr && pExtra[0] != '\0')
		dbg_msg("perf/assets", "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		dbg_msg("perf/assets", "stage=%s duration_ms=%.3f", pStage, DurationMs);
}
}

typedef std::function<void()> TMenuAssetScanLoadedFunc;

class CImageDecodeJob : public IJob
{
public:
	struct SResult
	{
		CImageInfo m_Image;
		bool m_Success = false;
	};

private:
	std::vector<uint8_t> m_vFileData;
	std::string m_Name;
	mutable CLock m_Lock;
	SResult m_Result;
	mutable bool m_Completed = false;

	void Run() override REQUIRES(!m_Lock)
	{
		if(m_vFileData.empty())
		{
			const CLockScope Lock(m_Lock);
			m_Completed = true;
			return;
		}

		CImageInfo Image;
		bool Success = false;

		constexpr uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		constexpr uint8_t WEBP_RIFF[] = {0x52, 0x49, 0x46, 0x46};
		constexpr uint8_t WEBP_WEBP[] = {0x57, 0x45, 0x42, 0x50};

		const bool IsPng = m_vFileData.size() >= 8 && 
			memcmp(m_vFileData.data(), PNG_SIGNATURE, 8) == 0;
		const bool IsWebp = m_vFileData.size() >= 12 && 
			memcmp(m_vFileData.data(), WEBP_RIFF, 4) == 0 &&
			memcmp(m_vFileData.data() + 8, WEBP_WEBP, 4) == 0;

		if(IsWebp)
		{
			Success = CImageLoader::LoadWebP(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image);
		}
		else if(IsPng)
		{
			Success = CImageLoader::LoadPng(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image);
		}
		else
		{
			if(CImageLoader::LoadWebP(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image))
				Success = true;
			else if(CImageLoader::LoadPng(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image))
				Success = true;
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = Success;
			m_Completed = true;
		}

		m_vFileData.clear();
		m_vFileData.shrink_to_fit();
	}

public:
	CImageDecodeJob(std::vector<uint8_t> &&vFileData, const char *pName) :
		m_vFileData(std::move(vFileData)),
		m_Name(pName)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	SResult GetResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

// 全异步图片加载 Job：在后台线程完成文件读取和解码
class CFullAsyncImageLoadJob : public IJob
{
public:
	struct SResult
	{
		CImageInfo m_Image;
		bool m_Success = false;
		bool m_Resized = false;
		int m_SourceWidth = 0;
		int m_SourceHeight = 0;
	};

private:
	std::vector<std::string> m_vPossiblePaths; // 可能的文件路径列表，按优先级排序
	IStorage *m_pStorage;
	int m_StorageType; // 存储类型
	int m_MaxTextureSize;
	std::string m_Name;
	mutable CLock m_Lock;
	SResult m_Result;
	mutable bool m_Completed = false;

	// 在后台线程读取文件
	bool TryLoadFile(const char *pFilename, std::vector<uint8_t> &vBuffer)
	{
		IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, m_StorageType);
		if(!File)
			return false;

		io_seek(File, 0, IOSEEK_END);
		const int64_t Size = io_tell(File);
		io_seek(File, 0, IOSEEK_START);

		if(Size <= 0 || Size > 10 * 1024 * 1024)
		{
			io_close(File);
			return false;
		}

		vBuffer.resize(Size);
		const size_t Read = io_read(File, vBuffer.data(), Size);
		io_close(File);

		return Read == (size_t)Size;
	}

	void Run() override REQUIRES(!m_Lock)
	{
		// 1. 尝试按优先级读取文件
		std::vector<uint8_t> vFileData;
		for(const std::string &Path : m_vPossiblePaths)
		{
			if(TryLoadFile(Path.c_str(), vFileData))
				break;
		}

		if(vFileData.empty())
		{
			const CLockScope Lock(m_Lock);
			m_Completed = true;
			return;
		}

		// 2. 解码图片
		CImageInfo Image;
		bool Success = false;

		constexpr uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		constexpr uint8_t WEBP_RIFF[] = {0x52, 0x49, 0x46, 0x46};
		constexpr uint8_t WEBP_WEBP[] = {0x57, 0x45, 0x42, 0x50};

		const bool IsPng = vFileData.size() >= 8 &&
			memcmp(vFileData.data(), PNG_SIGNATURE, 8) == 0;
		const bool IsWebp = vFileData.size() >= 12 &&
			memcmp(vFileData.data(), WEBP_RIFF, 4) == 0 &&
			memcmp(vFileData.data() + 8, WEBP_WEBP, 4) == 0;

		if(IsWebp)
		{
			Success = CImageLoader::LoadWebP(vFileData.data(), vFileData.size(), m_Name.c_str(), Image);
		}
		else if(IsPng)
		{
			Success = CImageLoader::LoadPng(vFileData.data(), vFileData.size(), m_Name.c_str(), Image);
		}
		else
		{
			if(CImageLoader::LoadWebP(vFileData.data(), vFileData.size(), m_Name.c_str(), Image))
				Success = true;
			else if(CImageLoader::LoadPng(vFileData.data(), vFileData.size(), m_Name.c_str(), Image))
				Success = true;
		}

		bool Resized = false;
		int SourceWidth = 0;
		int SourceHeight = 0;
		if(Success && Image.m_pData != nullptr && m_MaxTextureSize > 0)
		{
			SourceWidth = Image.m_Width;
			SourceHeight = Image.m_Height;
			const SPreviewTargetSize TargetSize = ComputePreviewTargetSize(Image.m_Width, Image.m_Height, m_MaxTextureSize);
			if(TargetSize.m_Resized)
			{
				ResizeImage(Image, TargetSize.m_Width, TargetSize.m_Height);
				Resized = true;
			}
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = Success;
			m_Result.m_Resized = Resized;
			m_Result.m_SourceWidth = SourceWidth;
			m_Result.m_SourceHeight = SourceHeight;
			m_Completed = true;
		}
	}

public:
	CFullAsyncImageLoadJob(std::vector<std::string> &&vPossiblePaths, IStorage *pStorage, const char *pName, int StorageType = IStorage::TYPE_ALL, int MaxTextureSize = 0) :
		m_vPossiblePaths(std::move(vPossiblePaths)),
		m_pStorage(pStorage),
		m_StorageType(StorageType),
		m_MaxTextureSize(MaxTextureSize),
		m_Name(pName)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	SResult GetResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

static std::string JsonEscape(const std::string &Str)
{
	std::string Result;
	Result.reserve(Str.size());
	for(char c : Str)
	{
		switch(c)
		{
		case '"': Result += "\\\""; break;
		case '\\': Result += "\\\\"; break;
		case '\n': Result += "\\n"; break;
		case '\r': Result += "\\r"; break;
		case '\t': Result += "\\t"; break;
		default: Result += c; break;
		}
	}
	return Result;
}

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

// ============================================================================
// ASYNC ASSET LIST LOADING
// ============================================================================
//
// Problem: Synchronous Storage()->ListDirectory() calls block the render thread
// causing UI stutters when switching asset tabs.
//
// Solution: Use background jobs to scan directories and populate asset lists.
// The render thread polls for completion and displays loading indicators.

class CAssetListLoadJob : public IJob
{
public:
	enum EAssetType
	{
		ASSET_TYPE_ENTITIES = 0,
		ASSET_TYPE_GAME,
		ASSET_TYPE_EMOTICONS,
		ASSET_TYPE_PARTICLES,
		ASSET_TYPE_HUD,
		ASSET_TYPE_GUI_CURSOR,
		ASSET_TYPE_ARROW,
		ASSET_TYPE_STRONG_WEAK,
		ASSET_TYPE_ENTITY_BG,
		ASSET_TYPE_EXTRAS,
	};

	struct SAssetEntry
	{
		char m_aName[IO_MAX_PATH_LENGTH];
		bool m_IsDir;
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

	struct SScanContext
	{
		IStorage *m_pStorage;
		std::vector<SAssetEntry> *m_pEntries;
		EAssetResourceKind m_Kind;
		bool m_SkipReservedDefault;
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

private:
	EAssetType m_Type;
	IStorage *m_pStorage;
	mutable CLock m_Lock;
	std::vector<SAssetEntry> m_vEntries;
	mutable bool m_Completed = false;

	static int ScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		const auto *pContext = static_cast<const SScanContext *>(pUser);
		auto *pEntries = pContext->m_pEntries;

		if(IsDir)
		{
			if(pContext->m_Kind != EAssetResourceKind::DIRECTORY)
				return 0;
			if(pName[0] == '.')
				return 0;
			if(pContext->m_SkipReservedDefault && str_comp(pName, "default") == 0)
				return 0;

			SAssetEntry Entry;
			str_copy(Entry.m_aName, pName);
			Entry.m_IsDir = true;
			Entry.m_Source = pContext->m_Source;
			pEntries->push_back(Entry);
		}
		else
		{
			const char *pExt = nullptr;
			if(pContext->m_Kind == EAssetResourceKind::MAP_FILE)
				pExt = ".map";
			else
				pExt = ".png";

			if(str_endswith(pName, pExt))
			{
				char aName[IO_MAX_PATH_LENGTH];
				str_truncate(aName, sizeof(aName), pName, str_length(pName) - str_length(pExt));
				if(pContext->m_SkipReservedDefault && str_comp(aName, "default") == 0)
					return 0;

				SAssetEntry Entry;
				str_copy(Entry.m_aName, aName);
				Entry.m_IsDir = false;
				Entry.m_Source = pContext->m_Source;
				pEntries->push_back(Entry);
			}
		}
		return 0;
	}

	struct SRecursiveMapScanContext
	{
		SScanContext *m_pScanContext;
		char m_aBasePath[IO_MAX_PATH_LENGTH];
		char m_aRelativePath[IO_MAX_PATH_LENGTH];
		char m_aNamePrefix[IO_MAX_PATH_LENGTH];
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

	static int RecursiveMapScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		auto *pContext = static_cast<SRecursiveMapScanContext *>(pUser);
		if(!str_comp(pInfo->m_pName, ".") || !str_comp(pInfo->m_pName, ".."))
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pContext->m_aRelativePath[0] != '\0')
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pContext->m_aRelativePath, pInfo->m_pName);
		else
			str_copy(aRelativePath, pInfo->m_pName);

		if(IsDir)
		{
			if(pInfo->m_pName[0] == '.')
				return 0;

			SRecursiveMapScanContext NextContext = *pContext;
			str_copy(NextContext.m_aRelativePath, aRelativePath, sizeof(NextContext.m_aRelativePath));

			char aFullPath[IO_MAX_PATH_LENGTH];
			str_format(aFullPath, sizeof(aFullPath), "%s/%s", pContext->m_aBasePath, aRelativePath);
			pContext->m_pScanContext->m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, aFullPath, RecursiveMapScanCallback, &NextContext);
			return 0;
		}

		if(!str_endswith(pInfo->m_pName, ".map"))
			return 0;

		char aName[IO_MAX_PATH_LENGTH];
		if(pContext->m_aNamePrefix[0] != '\0')
		{
			char aWithoutExt[IO_MAX_PATH_LENGTH];
			str_truncate(aWithoutExt, sizeof(aWithoutExt), aRelativePath, str_length(aRelativePath) - 4);
			str_format(aName, sizeof(aName), "%s/%s", pContext->m_aNamePrefix, aWithoutExt);
		}
		else
		{
			str_truncate(aName, sizeof(aName), aRelativePath, str_length(aRelativePath) - 4);
		}
		if(pContext->m_pScanContext->m_SkipReservedDefault && str_comp(aName, "default") == 0)
			return 0;

		SAssetEntry Entry;
		str_copy(Entry.m_aName, aName);
		Entry.m_IsDir = false;
		Entry.m_Source = pContext->m_Source;
		pContext->m_pScanContext->m_pEntries->push_back(Entry);
		return 0;
	}

	void Run() override REQUIRES(!m_Lock)
	{
		std::vector<SAssetEntry> vEntries;
		SScanContext ScanContext{m_pStorage, &vEntries, EAssetResourceKind::DIRECTORY, true, EEntityBgHierarchyEntrySource::LOCAL};

		if(m_Type == ASSET_TYPE_EXTRAS)
		{
			m_pStorage->ListDirectory(IStorage::TYPE_ALL, "assets/extras", ScanCallback, &ScanContext);
		}
		else
		{
			const SAssetResourceCategory *pCategory = nullptr;
			switch(m_Type)
			{
			case ASSET_TYPE_ENTITIES:
				pCategory = FindAssetResourceCategory("entities");
				break;
			case ASSET_TYPE_GAME:
				pCategory = FindAssetResourceCategory("game");
				break;
			case ASSET_TYPE_EMOTICONS:
				pCategory = FindAssetResourceCategory("emoticons");
				break;
			case ASSET_TYPE_PARTICLES:
				pCategory = FindAssetResourceCategory("particles");
				break;
			case ASSET_TYPE_HUD:
				pCategory = FindAssetResourceCategory("hud");
				break;
			case ASSET_TYPE_GUI_CURSOR:
				pCategory = FindAssetResourceCategory("gui_cursor");
				break;
			case ASSET_TYPE_ARROW:
				pCategory = FindAssetResourceCategory("arrow");
				break;
			case ASSET_TYPE_STRONG_WEAK:
				pCategory = FindAssetResourceCategory("strong_weak");
				break;
			case ASSET_TYPE_ENTITY_BG:
				pCategory = FindAssetResourceCategory("entity_bg");
				break;
			case ASSET_TYPE_EXTRAS:
				break;
			}

			dbg_assert(pCategory != nullptr, "asset list category must exist");
			ScanContext.m_Kind = pCategory->m_Kind;
			ScanContext.m_SkipReservedDefault = pCategory->m_Kind != EAssetResourceKind::MAP_FILE;
			if(m_Type == ASSET_TYPE_ENTITY_BG)
			{
				ScanContext.m_Kind = EAssetResourceKind::MAP_FILE;
				ScanContext.m_SkipReservedDefault = false;

				SRecursiveMapScanContext AssetsContext;
				AssetsContext.m_pScanContext = &ScanContext;
				str_copy(AssetsContext.m_aBasePath, pCategory->m_pInstallFolder, sizeof(AssetsContext.m_aBasePath));
				AssetsContext.m_aRelativePath[0] = '\0';
				str_copy(AssetsContext.m_aNamePrefix, "entity_bg", sizeof(AssetsContext.m_aNamePrefix));
				AssetsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &AssetsContext);

				SRecursiveMapScanContext MapsContext;
				MapsContext.m_pScanContext = &ScanContext;
				str_copy(MapsContext.m_aBasePath, "maps", sizeof(MapsContext.m_aBasePath));
				MapsContext.m_aRelativePath[0] = '\0';
				MapsContext.m_aNamePrefix[0] = '\0';
				MapsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, "maps", RecursiveMapScanCallback, &MapsContext);
			}
			else if(pCategory->m_Kind == EAssetResourceKind::MAP_FILE)
			{
				SRecursiveMapScanContext RecursiveContext;
				RecursiveContext.m_pScanContext = &ScanContext;
				str_copy(RecursiveContext.m_aBasePath, pCategory->m_pInstallFolder, sizeof(RecursiveContext.m_aBasePath));
				RecursiveContext.m_aRelativePath[0] = '\0';
				RecursiveContext.m_aNamePrefix[0] = '\0';
				RecursiveContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &RecursiveContext);
			}
			else
			{
				m_pStorage->ListDirectory(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, ScanCallback, &ScanContext);
			}
		}

		if(m_Type == ASSET_TYPE_ENTITY_BG)
		{
			std::unordered_set<std::string> vSeenNames;
			const auto DuplicateIt = std::remove_if(vEntries.begin(), vEntries.end(), [&](const SAssetEntry &Entry) {
				return !vSeenNames.insert(Entry.m_aName).second;
			});
			vEntries.erase(DuplicateIt, vEntries.end());
		}

		// Sort entries by name
		std::sort(vEntries.begin(), vEntries.end(),
			[](const SAssetEntry &Left, const SAssetEntry &Right) {
				return str_comp(Left.m_aName, Right.m_aName) < 0;
			});

		{
			const CLockScope Lock(m_Lock);
			m_vEntries = std::move(vEntries);
			m_Completed = true;
		}
	}

public:
	CAssetListLoadJob(EAssetType Type, IStorage *pStorage) :
		m_Type(Type),
		m_pStorage(pStorage)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	std::vector<SAssetEntry> GetEntries() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_vEntries;
	}

	EAssetType GetType() const { return m_Type; }
};

static bool LoadFileToBuffer(IStorage *pStorage, const char *pFilename, int StorageType, std::vector<uint8_t> &vBuffer)
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
	if(!File)
		return false;

	io_seek(File, 0, IOSEEK_END);
	const int64_t Size = io_tell(File);
	io_seek(File, 0, IOSEEK_START);

	if(Size <= 0 || Size > 10 * 1024 * 1024)
	{
		io_close(File);
		return false;
	}

	vBuffer.resize(Size);
	const size_t Read = io_read(File, vBuffer.data(), Size);
	io_close(File);

	return Read == (size_t)Size;
}

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

static void StartEntitiesDecode(CMenus::SCustomEntities *pEntitiesItem, IStorage *pStorage, IEngine *pEngine)
{
	if(pEntitiesItem->m_pDecodeJob || pEntitiesItem->m_RenderTexture.IsValid())
		return;

	// 构建可能的文件路径列表（按优先级排序）
	std::vector<std::string> vPossiblePaths;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			vPossiblePaths.emplace_back(aPath);
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			vPossiblePaths.emplace_back(aPath);
		}
		str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
		vPossiblePaths.emplace_back(aPath);
	}

	// 创建全异步 Job，在后台线程完成文件读取和解码
	pEntitiesItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pEntitiesItem->m_aName, IStorage::TYPE_ALL, LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	pEngine->AddJob(pEntitiesItem->m_pDecodeJob);
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void StartAssetDecode(TName *pAssetItem, const char *pAssetName, IStorage *pStorage, IEngine *pEngine)
{
	if(pAssetItem->m_pDecodeJob || pAssetItem->m_RenderTexture.IsValid())
		return;

	// 构建可能的文件路径列表（按优先级排序）
	std::vector<std::string> vPossiblePaths;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		if(str_comp(pAssetName, "gui_cursor") == 0 || str_comp(pAssetName, "arrow") == 0 || str_comp(pAssetName, "strong_weak") == 0)
		{
			const auto aCandidates = BuildNamedSingleFileAssetCandidates(pAssetName, pAssetItem->m_aName);
			for(const std::string &Candidate : aCandidates)
			{
				if(!Candidate.empty())
					vPossiblePaths.emplace_back(Candidate);
			}
		}
		else
		{
			str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
			vPossiblePaths.emplace_back(aPath);
		}
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		vPossiblePaths.emplace_back(aPath);
		str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
		vPossiblePaths.emplace_back(aPath);
	}

	// 创建全异步 Job，在后台线程完成文件读取和解码
	pAssetItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pAssetItem->m_aName, IStorage::TYPE_ALL, LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	pEngine->AddJob(pAssetItem->m_pDecodeJob);
}

static bool IsEntityBgSelectionMatched(const char *pItemName)
{
	if(str_comp(pItemName, "default") == 0)
		return IsDefaultBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);

	char aSelectedAsset[IO_MAX_PATH_LENGTH];
	if(!TryGetBackgroundEntitiesAssetName(g_Config.m_ClBackgroundEntities, aSelectedAsset, sizeof(aSelectedAsset)))
		return false;
	return str_comp(aSelectedAsset, pItemName) == 0;
}

static void ApplyEntityBgSelection(CGameClient *pGameClient, const char *pItemName)
{
	BuildBackgroundEntitiesValueFromAsset(pItemName, g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
	pGameClient->m_Background.LoadBackground();
}

static bool IsEntityBgManagedAssetName(const char *pAssetName)
{
	return pAssetName != nullptr && str_startswith(pAssetName, "entity_bg/");
}

static void CollectEntityBgPreviewPaths(IStorage *pStorage, const char *pAssetName, std::vector<std::string> &vOutPaths)
{
	vOutPaths.clear();
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return;

	auto AddCandidate = [&vOutPaths](const char *pPath) {
		if(pPath == nullptr || pPath[0] == '\0')
			return;
		if(std::find(vOutPaths.begin(), vOutPaths.end(), pPath) == vOutPaths.end())
			vOutPaths.emplace_back(pPath);
	};

	char aManagedPath[IO_MAX_PATH_LENGTH];
	char aMapPath[IO_MAX_PATH_LENGTH];
	str_format(aManagedPath, sizeof(aManagedPath), "assets/%s.png", pAssetName);
	str_format(aMapPath, sizeof(aMapPath), "maps/%s.png", pAssetName);

	if(IsEntityBgManagedAssetName(pAssetName))
	{
		const bool ManagedExists = pStorage != nullptr && pStorage->FileExists(aManagedPath, IStorage::TYPE_ALL);
		const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, IStorage::TYPE_ALL);
		if(ManagedExists || !MapExists)
			AddCandidate(aManagedPath);
		if(MapExists || !ManagedExists)
			AddCandidate(aMapPath);
	}
	else
	{
		AddCandidate(aMapPath);
	}
}

static void ResolveEntityBgLocalMapPath(IStorage *pStorage, const char *pAssetName, int StorageType, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return;

	char aManagedPath[IO_MAX_PATH_LENGTH];
	char aMapPath[IO_MAX_PATH_LENGTH];
	str_format(aManagedPath, sizeof(aManagedPath), "assets/%s.map", pAssetName);
	str_format(aMapPath, sizeof(aMapPath), "maps/%s.map", pAssetName);

	if(IsEntityBgManagedAssetName(pAssetName))
	{
		const bool ManagedExists = pStorage != nullptr && pStorage->FileExists(aManagedPath, StorageType);
		const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, StorageType);
		if(ManagedExists || !MapExists)
			str_copy(pOut, aManagedPath, OutSize);
		else
			str_copy(pOut, aMapPath, OutSize);
	}
	else
	{
		str_copy(pOut, aMapPath, OutSize);
	}
}

static void StartEntityBgDecode(CMenus::SCustomEntityBg *pAssetItem, IStorage *pStorage, IEngine *pEngine)
{
	if(pAssetItem->m_pDecodeJob || pAssetItem->m_RenderTexture.IsValid())
		return;

	std::vector<std::string> vPossiblePaths;
	CollectEntityBgPreviewPaths(pStorage, pAssetItem->m_aName, vPossiblePaths);
	if(vPossiblePaths.empty())
		return;

	pAssetItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pAssetItem->m_aName, IStorage::TYPE_ALL, LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	pEngine->AddJob(pAssetItem->m_pDecodeJob);
}

static bool IsEntityBgConfigSelected(const char *pAssetName)
{
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return false;
	return IsEntityBgSelectionMatched(pAssetName);
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vGameList, pUser);
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, pUser);
}

static std::vector<CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<CMenus::SCustomGuiCursor *> gs_vpSearchGuiCursorList;
static std::vector<CMenus::SCustomArrow *> gs_vpSearchArrowList;
static std::vector<CMenus::SCustomStrongWeak *> gs_vpSearchStrongWeakList;
static std::vector<CMenus::SCustomEntityBg *> gs_vpSearchEntityBgList;
static std::vector<CMenus::SCustomExtras *> gs_vpSearchExtrasList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true, // ASSETS_TAB_ENTITIES
	true, // ASSETS_TAB_GAME
	true, // ASSETS_TAB_EMOTICONS
	true, // ASSETS_TAB_PARTICLES
	true, // ASSETS_TAB_HUD
	true, // ASSETS_TAB_GUI_CURSOR
	true, // ASSETS_TAB_ARROW
	true, // ASSETS_TAB_STRONG_WEAK
	true, // ASSETS_TAB_ENTITY_BG
	true, // ASSETS_TAB_EXTRAS
};

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

namespace
{
constexpr const char *WORKSHOP_ASSETS_URL = "https://www.ddrace.cn/data/assets.json";

const char *AssetResourceCategoryIdByTab(int Tab)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		return "entities";
	if(Tab == ASSETS_TAB_GAME)
		return "game";
	if(Tab == ASSETS_TAB_EMOTICONS)
		return "emoticons";
	if(Tab == ASSETS_TAB_PARTICLES)
		return "particles";
	if(Tab == ASSETS_TAB_HUD)
		return "hud";
	if(Tab == ASSETS_TAB_GUI_CURSOR)
		return "gui_cursor";
	if(Tab == ASSETS_TAB_ARROW)
		return "arrow";
	if(Tab == ASSETS_TAB_STRONG_WEAK)
		return "strong_weak";
	if(Tab == ASSETS_TAB_ENTITY_BG)
		return "entity_bg";
	if(Tab == ASSETS_TAB_EXTRAS)
		return "extras";
	return nullptr;
}

const SAssetResourceCategory *AssetResourceCategoryByTab(int Tab)
{
	const char *pCategoryId = AssetResourceCategoryIdByTab(Tab);
	if(pCategoryId == nullptr)
		return nullptr;

	const SAssetResourceCategory *pCategory = FindAssetResourceCategory(pCategoryId);
	dbg_assert(pCategory != nullptr, "asset category must exist");
	return pCategory;
}

struct SWorkshopHudAsset
{
	std::string m_Id;
	std::string m_Name;
	std::string m_Author;
	std::string m_LocalName;
	std::string m_ImageUrl;
	std::string m_ThumbUrl;
	std::string m_ThumbCachePath;
	std::string m_InstallPath;
	IGraphics::CTextureHandle m_ThumbTexture;
	std::shared_ptr<CHttpRequest> m_pThumbTask;
	std::shared_ptr<CHttpRequest> m_pDownloadTask;
	bool m_DownloadFailed = false;
	bool m_Installed = false;
	CImageInfo m_ThumbImage;
	size_t m_ThumbBytes = 0;
	bool m_ThumbResized = false;
	std::shared_ptr<CFullAsyncImageLoadJob> m_pDecodeJob;

	SWorkshopHudAsset() = default;

	SWorkshopHudAsset(const SWorkshopHudAsset &Other) :
		m_Id(Other.m_Id),
		m_Name(Other.m_Name),
		m_Author(Other.m_Author),
		m_LocalName(Other.m_LocalName),
		m_ImageUrl(Other.m_ImageUrl),
		m_ThumbUrl(Other.m_ThumbUrl),
		m_ThumbCachePath(Other.m_ThumbCachePath),
		m_InstallPath(Other.m_InstallPath),
		m_ThumbTexture(Other.m_ThumbTexture),
		m_pThumbTask(Other.m_pThumbTask),
		m_pDownloadTask(Other.m_pDownloadTask),
		m_DownloadFailed(Other.m_DownloadFailed),
		m_Installed(Other.m_Installed),
		m_ThumbBytes(Other.m_ThumbBytes),
		m_ThumbResized(Other.m_ThumbResized),
		m_pDecodeJob(Other.m_pDecodeJob)
	{
		if(Other.m_ThumbImage.m_pData != nullptr)
			m_ThumbImage = Other.m_ThumbImage.DeepCopy();
	}

	SWorkshopHudAsset &operator=(const SWorkshopHudAsset &Other)
	{
		if(this == &Other)
			return *this;

		m_Id = Other.m_Id;
		m_Name = Other.m_Name;
		m_Author = Other.m_Author;
		m_LocalName = Other.m_LocalName;
		m_ImageUrl = Other.m_ImageUrl;
		m_ThumbUrl = Other.m_ThumbUrl;
		m_ThumbCachePath = Other.m_ThumbCachePath;
		m_InstallPath = Other.m_InstallPath;
		m_ThumbTexture = Other.m_ThumbTexture;
		m_pThumbTask = Other.m_pThumbTask;
		m_pDownloadTask = Other.m_pDownloadTask;
		m_DownloadFailed = Other.m_DownloadFailed;
		m_Installed = Other.m_Installed;
		m_ThumbImage.Free();
		if(Other.m_ThumbImage.m_pData != nullptr)
			m_ThumbImage = Other.m_ThumbImage.DeepCopy();
		m_ThumbBytes = Other.m_ThumbBytes;
		m_ThumbResized = Other.m_ThumbResized;
		m_pDecodeJob = Other.m_pDecodeJob;
		return *this;
	}

	SWorkshopHudAsset(SWorkshopHudAsset &&Other) = default;
	SWorkshopHudAsset &operator=(SWorkshopHudAsset &&Other) = default;

	~SWorkshopHudAsset()
	{
		m_ThumbImage.Free();
	}
};

struct SWorkshopHudState
{
	std::vector<SWorkshopHudAsset> m_vAssets;
	std::unordered_map<std::string, std::string> m_vEntityBgPreviewExtByName;
	std::deque<std::string> m_vReadyThumbQueue;
	std::unordered_set<std::string> m_vReadyThumbQueued;
	std::shared_ptr<CHttpRequest> m_pListTask;
	std::shared_ptr<CHttpRequest> m_pEntityBgPreviewTask;
	bool m_Requested = false;
	bool m_EntityBgPreviewRequested = false;
	bool m_LoadFailed = false;
	std::string m_EntityBgPreviewBaseUrl;
	char m_aError[128] = "";
	double m_CacheTime = 0.0;
	double m_LastRefreshTime = 0.0;
};

struct SDeleteDirectoryEntry
{
	char m_aName[IO_MAX_PATH_LENGTH];
	bool m_IsDir = false;
};

struct SDeleteDirectoryScanUser
{
	std::vector<SDeleteDirectoryEntry> *m_pEntries;
};

static SWorkshopHudState gs_aWorkshopStates[NUMBER_OF_ASSETS_TABS];

static SWorkshopHudState *WorkshopStateByTab(int Tab)
{
	const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(Tab);
	if(pCategory == nullptr || !pCategory->m_WorkshopEnabled)
		return nullptr;
	return &gs_aWorkshopStates[Tab];
}

} // namespace

namespace
{

static bool SearchFilterMatches(const char *pFilter, const char *pName, const char *pAuthor = nullptr)
{
	if(pFilter == nullptr || pFilter[0] == '\0')
		return true;
	if(pName != nullptr && str_utf8_find_nocase(pName, pFilter))
		return true;
	if(pAuthor != nullptr && pAuthor[0] != '\0' && str_utf8_find_nocase(pAuthor, pFilter))
		return true;
	return false;
}

static const char *NormalizeWorkshopAuthorName(const char *pAuthor)
{
	if(pAuthor == nullptr || pAuthor[0] == '\0')
		return "";
	if(str_comp(pAuthor, "资源来源于网络") == 0)
		return Localize("Internet (From online source)");
	return pAuthor;
}

static const SWorkshopHudAsset *FindWorkshopAssetByLocalName(const SWorkshopHudState *pState, const char *pLocalName)
{
	if(pState == nullptr || pLocalName == nullptr || pLocalName[0] == '\0')
		return nullptr;

	for(const SWorkshopHudAsset &Asset : pState->m_vAssets)
	{
		if(str_comp(Asset.m_LocalName.c_str(), pLocalName) == 0)
			return &Asset;
	}
	return nullptr;
}

static const char *FindWorkshopAuthorByLocalName(const SWorkshopHudState *pState, const char *pLocalName)
{
	if(const SWorkshopHudAsset *pAsset = FindWorkshopAssetByLocalName(pState, pLocalName))
		return pAsset->m_Author.c_str();
	return nullptr;
}

static void NormalizeEntityBgWorkshopAsset(SWorkshopHudAsset &Asset, IStorage *pStorage)
{
	if(Asset.m_InstallPath.empty())
		return;

	const std::string NormalizedInstallPath = NormalizeEntityBgWorkshopInstallPath(Asset.m_InstallPath);
	if(!NormalizedInstallPath.empty() && NormalizedInstallPath != Asset.m_InstallPath)
	{
		if(pStorage != nullptr &&
			pStorage->FileExists(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE) &&
			!pStorage->FileExists(NormalizedInstallPath.c_str(), IStorage::TYPE_SAVE))
		{
			pStorage->RenameFile(Asset.m_InstallPath.c_str(), NormalizedInstallPath.c_str(), IStorage::TYPE_SAVE);
		}
		Asset.m_InstallPath = NormalizedInstallPath;
	}

	const std::string LocalName = RebuildEntityBgWorkshopLocalName(Asset.m_InstallPath);
	if(!LocalName.empty())
		Asset.m_LocalName = LocalName;
}

static bool IsEntityBgWorkshopFolderOrChild(const char *pPath)
{
	return pPath != nullptr && (IsEntityBgWorkshopFolderPath(pPath) || str_startswith(pPath, "entity_bg/"));
}

static bool UsesCombinedAssetList(const SAssetResourceCategory *pCategory)
{
	return pCategory != nullptr && pCategory->m_WorkshopEnabled;
}

static void StartBackgroundDecode(SWorkshopHudAsset &Asset, IStorage *pStorage, IEngine *pEngine)
{
	if(Asset.m_pDecodeJob || Asset.m_ThumbTexture.IsValid() || Asset.m_ThumbImage.m_pData != nullptr)
		return;

	// 构建可能的文件路径列表（按优先级排序）
	std::vector<std::string> vPossiblePaths;

	if(Asset.m_Installed && !Asset.m_InstallPath.empty())
	{
		vPossiblePaths.emplace_back(Asset.m_InstallPath);
	}
	if(!Asset.m_ThumbCachePath.empty())
	{
		vPossiblePaths.emplace_back(Asset.m_ThumbCachePath);
	}

	if(!vPossiblePaths.empty())
	{
		// 创建全异步 Job，在后台线程完成文件读取和解码
		Asset.m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, Asset.m_Name.c_str(), IStorage::TYPE_SAVE);
		pEngine->AddJob(Asset.m_pDecodeJob);
	}
}

static void ResetWorkshopThumbReadyState(SWorkshopHudAsset &Asset)
{
	Asset.m_ThumbImage.Free();
	Asset.m_ThumbBytes = 0;
	Asset.m_ThumbResized = false;
}

static SWorkshopHudAsset *FindWorkshopAssetById(SWorkshopHudState &State, const std::string &Id)
{
	for(auto &Asset : State.m_vAssets)
	{
		if(Asset.m_Id == Id)
			return &Asset;
	}
	return nullptr;
}

static void ClearWorkshopReadyThumbQueue(SWorkshopHudState &State)
{
	State.m_vReadyThumbQueue.clear();
	State.m_vReadyThumbQueued.clear();
}

static void PruneWorkshopReadyThumbQueue(SWorkshopHudState &State)
{
	std::deque<std::string> vQueue;
	std::unordered_set<std::string> vQueued;
	for(const std::string &Id : State.m_vReadyThumbQueue)
	{
		SWorkshopHudAsset *pAsset = FindWorkshopAssetById(State, Id);
		if(pAsset == nullptr || pAsset->m_ThumbTexture.IsValid() || pAsset->m_ThumbImage.m_pData == nullptr)
			continue;
		if(vQueued.insert(Id).second)
			vQueue.push_back(Id);
	}
	State.m_vReadyThumbQueue = std::move(vQueue);
	State.m_vReadyThumbQueued = std::move(vQueued);
}

static void ResetWorkshopAssetRuntimeState(SWorkshopHudAsset &Asset, IGraphics *pGraphics, bool AbortTasks)
{
	if(AbortTasks && Asset.m_pThumbTask)
		Asset.m_pThumbTask->Abort();
	if(AbortTasks && Asset.m_pDownloadTask)
		Asset.m_pDownloadTask->Abort();
	Asset.m_pThumbTask.reset();
	Asset.m_pDownloadTask.reset();
	Asset.m_pDecodeJob.reset();
	ResetWorkshopThumbReadyState(Asset);
	pGraphics->UnloadTexture(&Asset.m_ThumbTexture);
}

static void QueueWorkshopReadyThumb(SWorkshopHudState &State, SWorkshopHudAsset &Asset, int CurTab)
{
	if(State.m_vReadyThumbQueued.insert(Asset.m_Id).second)
	{
		State.m_vReadyThumbQueue.push_back(Asset.m_Id);
		char aExtra[160];
		str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s queue_size=%d bytes=%u",
			CurTab, Asset.m_Name.c_str(), (int)State.m_vReadyThumbQueue.size(), (unsigned)Asset.m_ThumbBytes);
		LogAssetsPerfStage("assets_workshop_thumb_upload_queue_push", 0.0, true, aExtra);
	}
}

int CollectDeleteDirectoryEntries(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	if(pName[0] == '.')
		return 0;

	auto *pScanUser = static_cast<SDeleteDirectoryScanUser *>(pUser);
	SDeleteDirectoryEntry Entry;
	str_copy(Entry.m_aName, pName);
	Entry.m_IsDir = IsDir != 0;
	pScanUser->m_pEntries->push_back(Entry);
	return 0;
}

bool RemoveFolderTree(IStorage *pStorage, const char *pFolderPath)
{
	std::vector<SDeleteDirectoryEntry> vEntries;
	SDeleteDirectoryScanUser User;
	User.m_pEntries = &vEntries;
	pStorage->ListDirectory(IStorage::TYPE_SAVE, pFolderPath, CollectDeleteDirectoryEntries, &User);

	bool RemovedAnything = false;
	for(const SDeleteDirectoryEntry &Entry : vEntries)
	{
		char aChildPath[IO_MAX_PATH_LENGTH];
		str_format(aChildPath, sizeof(aChildPath), "%s/%s", pFolderPath, Entry.m_aName);
		if(Entry.m_IsDir)
		{
			RemovedAnything |= RemoveFolderTree(pStorage, aChildPath);
		}
		else
		{
			RemovedAnything |= pStorage->RemoveFile(aChildPath, IStorage::TYPE_SAVE);
		}
	}

	RemovedAnything |= pStorage->RemoveFolder(pFolderPath, IStorage::TYPE_SAVE);
	return RemovedAnything;
}

void GuessUrlExtension(const char *pUrl, char *pOutExt, int OutExtSize)
{
	str_copy(pOutExt, "png", OutExtSize);
	if(!pUrl || pUrl[0] == '\0')
		return;

	char aUrl[IO_MAX_PATH_LENGTH];
	str_copy(aUrl, pUrl, sizeof(aUrl));
	if(const char *pQuery = str_find(aUrl, "?"))
		aUrl[pQuery - aUrl] = '\0';

	const char *pSlash = str_rchr(aUrl, '/');
	const char *pDot = str_rchr(aUrl, '.');
	if(!pDot || (pSlash && pDot < pSlash))
		return;

	char aExt[16];
	str_copy(aExt, pDot + 1, sizeof(aExt));
	if(aExt[0] == '\0' || str_length(aExt) > 8)
		return;

	for(char &c : aExt)
	{
		if(c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
		if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
			return;
	}
	str_copy(pOutExt, aExt, OutExtSize);
}

void SanitizeFilenameInPlace(char *pFilename)
{
	str_sanitize_filename(pFilename);
	for(char *pChar = pFilename; *pChar != '\0'; ++pChar)
	{
		if(*pChar == ' ')
			*pChar = '_';
	}
}

std::string BuildSafeFilename(const char *pName, const char *pFallbackName, const char *pExt)
{
	char aName[128];
	if(pName && pName[0] != '\0')
		str_copy(aName, pName, sizeof(aName));
	else
		str_copy(aName, pFallbackName, sizeof(aName));
	SanitizeFilenameInPlace(aName);
	if(aName[0] == '\0')
		str_copy(aName, pFallbackName, sizeof(aName));

	char aDotExt[20];
	str_format(aDotExt, sizeof(aDotExt), ".%s", pExt);

	char aFilename[160];
	if(str_endswith_nocase(aName, aDotExt))
		str_copy(aFilename, aName, sizeof(aFilename));
	else
		str_format(aFilename, sizeof(aFilename), "%s%s", aName, aDotExt);
	return aFilename;
}

std::string UniqueWorkshopAssetBaseName(const SAssetResourceCategory &Category, std::string_view PreferredBaseName, const char *pAssetId, const std::unordered_set<std::string> &vUsedLocalNames)
{
	std::string BaseName(PreferredBaseName);
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return BaseName;

	const bool ReservedName = IsReservedNamedSingleFileAssetName(Category, BaseName);
	const bool DuplicateName = vUsedLocalNames.find(BaseName) != vUsedLocalNames.end();
	if(!ReservedName && !DuplicateName)
		return BaseName;

	char aSafeId[80];
	str_copy(aSafeId, pAssetId != nullptr && pAssetId[0] != '\0' ? pAssetId : "workshop", sizeof(aSafeId));
	SanitizeFilenameInPlace(aSafeId);
	if(aSafeId[0] == '\0')
		str_copy(aSafeId, "workshop", sizeof(aSafeId));

	std::string Candidate = BaseName + "_" + aSafeId;
	int Suffix = 2;
	while(IsReservedNamedSingleFileAssetName(Category, Candidate) || vUsedLocalNames.find(Candidate) != vUsedLocalNames.end())
	{
		Candidate = BaseName + "_" + aSafeId + "_" + std::to_string(Suffix);
		++Suffix;
	}

	return Candidate;
}

template<typename TName>
void EnsureDefaultAssetVisible(const SAssetResourceCategory &Category, std::vector<TName> &vAssetList)
{
	(void)Category;

	const auto HasDefault = std::any_of(vAssetList.begin(), vAssetList.end(), [](const TName &AssetItem) {
		return IsProtectedDefaultAsset(AssetItem.m_aName);
	});
	if(!HasDefault)
	{
		TName DefaultItem;
		str_copy(DefaultItem.m_aName, "default");
		vAssetList.push_back(DefaultItem);
	}

	std::sort(vAssetList.begin(), vAssetList.end(), [](const TName &LeftItem, const TName &RightItem) {
		return AssetResourceNameLess(LeftItem.m_aName, RightItem.m_aName);
	});
}

static const char *AssetCardDisplayName(const CMenus::SCustomItem *pItem)
{
	if(pItem == nullptr)
		return "";
	const char *pDisplayName = pItem->m_aDisplayName[0] != '\0' ? pItem->m_aDisplayName : pItem->m_aName;
	if(str_comp(pDisplayName, "entity_bg (Workshop)") == 0)
		return Localize("entity_bg (Workshop)");
	return pDisplayName;
}

struct SNamedSingleFileNameScanUser
{
	std::vector<std::string> *m_pNames;
};

int CollectNamedSingleFileAssetNamesCallback(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	if(IsDir || pName[0] == '.')
		return 0;
	if(!str_endswith(pName, ".png"))
		return 0;

	auto *pScanUser = static_cast<SNamedSingleFileNameScanUser *>(pUser);
	char aName[IO_MAX_PATH_LENGTH];
	str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
	pScanUser->m_pNames->emplace_back(aName);
	return 0;
}

bool WriteSmallTextFile(IStorage *pStorage, const char *pPath, std::string_view Text)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	const size_t Written = io_write(File, Text.data(), Text.size());
	io_close(File);
	return Written == Text.size();
}

bool CopyStorageFile(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, int TargetStorageType)
{
	std::vector<uint8_t> vBuffer;
	if(!LoadFileToBuffer(pStorage, pSourcePath, SourceStorageType, vBuffer))
		return false;

	IOHANDLE File = pStorage->OpenFile(pTargetPath, IOFLAG_WRITE, TargetStorageType);
	if(!File)
		return false;

	const size_t Written = io_write(File, vBuffer.data(), vBuffer.size());
	io_close(File);
	return Written == vBuffer.size();
}

void CollectNamedSingleFileAssetNames(IStorage *pStorage, const SAssetResourceCategory &Category, std::vector<std::string> &vAssetNames)
{
	vAssetNames.clear();
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return;

	SNamedSingleFileNameScanUser ScanUser{&vAssetNames};
	pStorage->ListDirectory(IStorage::TYPE_ALL, Category.m_pInstallFolder, CollectNamedSingleFileAssetNamesCallback, &ScanUser);
	::EnsureDefaultAssetVisible(vAssetNames);
}

std::string NextReservedNamedSingleFileMigrationName(const SAssetResourceCategory &Category, std::string_view OldName, const std::vector<std::string> &vExistingNames)
{
	const auto NameExists = [&Category, &vExistingNames](std::string_view Candidate) {
		if(IsReservedNamedSingleFileAssetName(Category, Candidate))
			return true;
		return std::find(vExistingNames.begin(), vExistingNames.end(), Candidate) != vExistingNames.end();
	};

	std::string Candidate = std::string(OldName) + "_local";
	if(!NameExists(Candidate))
		return Candidate;

	for(int Suffix = 2;; ++Suffix)
	{
		Candidate = std::string(OldName) + "_local_" + std::to_string(Suffix);
		if(!NameExists(Candidate))
			return Candidate;
	}
}

void UpdateNamedSingleFileConfigSelectionAfterMigration(const SAssetResourceCategory &Category, const char *pOldName, const char *pNewName)
{
	if(str_comp(Category.m_pId, "gui_cursor") == 0)
	{
		if(str_comp(g_Config.m_ClAssetGuiCursor, pOldName) == 0)
			str_copy(g_Config.m_ClAssetGuiCursor, pNewName, sizeof(g_Config.m_ClAssetGuiCursor));
	}
	else if(str_comp(Category.m_pId, "arrow") == 0)
	{
		if(str_comp(g_Config.m_ClAssetArrow, pOldName) == 0)
			str_copy(g_Config.m_ClAssetArrow, pNewName, sizeof(g_Config.m_ClAssetArrow));
	}
	else if(str_comp(Category.m_pId, "strong_weak") == 0)
	{
		if(str_comp(g_Config.m_ClAssetStrongWeak, pOldName) == 0)
			str_copy(g_Config.m_ClAssetStrongWeak, pNewName, sizeof(g_Config.m_ClAssetStrongWeak));
	}
}

void MigrateReservedNamedSingleFileAssets(IStorage *pStorage, const SAssetResourceCategory &Category)
{
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return;

	std::vector<std::string> vExistingNames;
	CollectNamedSingleFileAssetNames(pStorage, Category, vExistingNames);

	std::vector<std::string> vSaveNames;
	SNamedSingleFileNameScanUser ScanUser{&vSaveNames};
	pStorage->ListDirectory(IStorage::TYPE_SAVE, Category.m_pInstallFolder, CollectNamedSingleFileAssetNamesCallback, &ScanUser);

	for(const std::string &Name : vSaveNames)
	{
		if(IsProtectedDefaultAsset(Name) || !IsReservedNamedSingleFileAssetName(Category, Name))
			continue;

		const std::string NewName = NextReservedNamedSingleFileMigrationName(Category, Name, vExistingNames);
		char aOldPath[IO_MAX_PATH_LENGTH];
		char aNewPath[IO_MAX_PATH_LENGTH];
		str_format(aOldPath, sizeof(aOldPath), "%s/%s.png", Category.m_pInstallFolder, Name.c_str());
		str_format(aNewPath, sizeof(aNewPath), "%s/%s.png", Category.m_pInstallFolder, NewName.c_str());
		if(pStorage->FileExists(aOldPath, IStorage::TYPE_SAVE) &&
			!pStorage->FileExists(aNewPath, IStorage::TYPE_SAVE) &&
			pStorage->RenameFile(aOldPath, aNewPath, IStorage::TYPE_SAVE))
		{
			vExistingNames.emplace_back(NewName);
			UpdateNamedSingleFileConfigSelectionAfterMigration(Category, Name.c_str(), NewName.c_str());
		}
	}
}

void TryImportLegacySingleFileAsset(IStorage *pStorage, const SAssetResourceCategory &Category)
{
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return;

	char aMarkerPath[IO_MAX_PATH_LENGTH];
	str_format(aMarkerPath, sizeof(aMarkerPath), "%s/.legacy_imported", Category.m_pInstallFolder);
	if(pStorage->FileExists(aMarkerPath, IStorage::TYPE_SAVE))
		return;

	const char *pLegacySourcePath = LegacySingleFileAssetSourcePath(Category);
	if(pLegacySourcePath == nullptr || !pStorage->FileExists(pLegacySourcePath, IStorage::TYPE_SAVE))
		return;


	std::vector<std::string> vExistingNames;
	CollectNamedSingleFileAssetNames(pStorage, Category, vExistingNames);
	const std::string ImportedAssetName = NextLegacyAssetName(vExistingNames);

	pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(Category.m_pInstallFolder, IStorage::TYPE_SAVE);

	char aTargetPath[IO_MAX_PATH_LENGTH];
	str_format(aTargetPath, sizeof(aTargetPath), "%s/%s.png", Category.m_pInstallFolder, ImportedAssetName.c_str());
	if(!pStorage->FileExists(aTargetPath, IStorage::TYPE_SAVE) &&
		!CopyStorageFile(pStorage, pLegacySourcePath, IStorage::TYPE_SAVE, aTargetPath, IStorage::TYPE_SAVE))
	{
		return;
	}

	WriteSmallTextFile(pStorage, aMarkerPath, ImportedAssetName);
}

std::string NormalizeWorkshopAssetUrl(const char *pUrl)
{
	if(!pUrl || pUrl[0] == '\0')
		return {};

	std::string Url = pUrl;
	const size_t TransformPos = Url.find("!/");
	if(TransformPos != std::string::npos)
		Url.resize(TransformPos);
	return Url;
}

static constexpr const char *ENTITY_BG_PREVIEW_MAP_URL = "https://www.ddrace.cn/data/entity-bg-preview-map.json";

bool ParseEntityBgPreviewMap(const json_value *pRoot, std::unordered_map<std::string, std::string> &vPreviewExtByName, std::string &BaseUrl, char *pErr, int ErrSize)
{
	vPreviewExtByName.clear();
	BaseUrl.clear();

	if(!pRoot || pRoot->type != json_object)
	{
		str_copy(pErr, "Invalid entity bg preview response", ErrSize);
		return false;
	}

	const json_value *pBaseUrl = json_object_get(pRoot, "baseUrl");
	const json_value *pPreviewMap = json_object_get(pRoot, "previewMap");
	if(pBaseUrl == &json_value_none || pBaseUrl->type != json_string || pPreviewMap == &json_value_none || pPreviewMap->type != json_object)
	{
		str_copy(pErr, "Entity bg preview data is missing", ErrSize);
		return false;
	}

	BaseUrl = NormalizeWorkshopAssetUrl(json_string_get(pBaseUrl));
	if(BaseUrl.empty())
	{
		str_copy(pErr, "Entity bg preview base url is empty", ErrSize);
		return false;
	}

	for(unsigned i = 0; i < pPreviewMap->u.object.length; ++i)
	{
		const auto &Entry = pPreviewMap->u.object.values[i];
		if(Entry.name == nullptr || Entry.value == nullptr || Entry.value->type != json_string)
			continue;
		const char *pExt = json_string_get(Entry.value);
		if(pExt == nullptr || pExt[0] == '\0')
			continue;
		vPreviewExtByName[Entry.name] = pExt;
	}

	str_copy(pErr, "", ErrSize);
	return !vPreviewExtByName.empty();
}

bool BuildEntityBgPreviewThumbUrl(const SWorkshopHudState &WorkshopState, const char *pAssetName, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return false;
	pOut[0] = '\0';
	if(pAssetName == nullptr || pAssetName[0] == '\0' || WorkshopState.m_EntityBgPreviewBaseUrl.empty())
		return false;

	const auto It = WorkshopState.m_vEntityBgPreviewExtByName.find(pAssetName);
	if(It == WorkshopState.m_vEntityBgPreviewExtByName.end())
		return false;

	char aEscapedName[IO_MAX_PATH_LENGTH * 3];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), pAssetName);
	str_format(pOut, OutSize, "%s/%s.%s", WorkshopState.m_EntityBgPreviewBaseUrl.c_str(), aEscapedName, It->second.c_str());
	return true;
}

void ApplyEntityBgPreviewThumbUrls(SWorkshopHudState &WorkshopState)
{
	char aThumbUrl[IO_MAX_PATH_LENGTH * 2];
	for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
	{
		if(BuildEntityBgPreviewThumbUrl(WorkshopState, Asset.m_Name.c_str(), aThumbUrl, sizeof(aThumbUrl)))
			Asset.m_ThumbUrl = aThumbUrl;
		else
			Asset.m_ThumbUrl.clear();
	}
}

bool WorkshopCategoryMatches(const char *pCategoryValue, const SAssetResourceCategory &Category)
{
	if(!pCategoryValue || pCategoryValue[0] == '\0')
		return false;

	if((Category.m_pWorkshopCategory && Category.m_pWorkshopCategory[0] != '\0' && str_comp(pCategoryValue, Category.m_pWorkshopCategory) == 0) ||
		(Category.m_pWorkshopCategoryAlt && Category.m_pWorkshopCategoryAlt[0] != '\0' && str_comp(pCategoryValue, Category.m_pWorkshopCategoryAlt) == 0))
		return true;

	for(const char *pAlias : Category.m_vWorkshopCategoryAliases)
	{
		if(pAlias != nullptr && pAlias[0] != '\0' && str_comp(pCategoryValue, pAlias) == 0)
			return true;
	}

	return false;
}

bool ParseWorkshopAssets(const json_value *pRoot, const SAssetResourceCategory &Category, std::vector<SWorkshopHudAsset> &vOut, char *pErr, int ErrSize)
{
	vOut.clear();
	std::unordered_set<std::string> vUsedLocalNames;
	if(!pRoot || pRoot->type != json_object)
	{
		str_copy(pErr, "Invalid workshop response", ErrSize);
		return false;
	}

	const json_value *pAssets = json_object_get(pRoot, "assets");
	bool LegacyApi = false;
	if(pAssets == &json_value_none)
	{
		const json_value *pCode = json_object_get(pRoot, "code");
		if(pCode != &json_value_none && (pCode->type != json_integer || pCode->u.integer != 0))
		{
			const json_value *pMessage = json_object_get(pRoot, "message");
			if(pMessage != &json_value_none && pMessage->type == json_string)
				str_copy(pErr, json_string_get(pMessage), ErrSize);
			else
				str_copy(pErr, "Workshop api returned error", ErrSize);
			return false;
		}
		pAssets = json_object_get(pRoot, "data");
		LegacyApi = true;
	}

	if(pAssets == &json_value_none || pAssets->type != json_array)
	{
		str_copy(pErr, "Workshop asset list is missing", ErrSize);
		return false;
	}

	vOut.reserve(pAssets->u.array.length);
	for(unsigned i = 0; i < pAssets->u.array.length; ++i)
	{
		const json_value &Entry = (*pAssets)[i];
		if(Entry.type != json_object)
			continue;

		const json_value *pCategory = json_object_get(&Entry, "category");
		if(pCategory == &json_value_none || pCategory->type != json_string)
			continue;
		const char *pCategoryValue = json_string_get(pCategory);
		if(!WorkshopCategoryMatches(pCategoryValue, Category))
			continue;

		const json_value *pImage = json_object_get(&Entry, LegacyApi ? "image" : "image_url");
		if(pImage == &json_value_none)
			pImage = json_object_get(&Entry, "image");
		if(pImage == &json_value_none)
			pImage = json_object_get(&Entry, "image_url");
		if(pImage == &json_value_none || pImage->type != json_string)
			continue;

		const json_value *pId = json_object_get(&Entry, "id");
		const json_value *pName = json_object_get(&Entry, "name");
		const json_value *pAuthor = json_object_get(&Entry, "author");

		SWorkshopHudAsset Asset;
		Asset.m_Id = pId != &json_value_none && pId->type == json_string ? json_string_get(pId) : std::to_string(i);
		Asset.m_Name = pName != &json_value_none && pName->type == json_string ? json_string_get(pName) : Asset.m_Id;
		Asset.m_Author = pAuthor != &json_value_none && pAuthor->type == json_string ? NormalizeWorkshopAuthorName(json_string_get(pAuthor)) : "";
		Asset.m_ImageUrl = NormalizeWorkshopAssetUrl(json_string_get(pImage));
		if(Asset.m_ImageUrl.empty())
			continue;
		Asset.m_ThumbUrl = str_comp(Category.m_pId, "entity_bg") == 0 ? "" : Asset.m_ImageUrl;

		char aExt[16];
		GuessUrlExtension(Asset.m_ImageUrl.c_str(), aExt, sizeof(aExt));

		std::string SafeInstallName = BuildSafeFilename(Asset.m_Name.c_str(), Asset.m_Id.c_str(), aExt);
		const size_t DotPos = SafeInstallName.find_last_of('.');
		const std::string PreferredLocalBaseName = DotPos == std::string::npos ? SafeInstallName : SafeInstallName.substr(0, DotPos);
		const std::string LocalBaseName = UniqueWorkshopAssetBaseName(Category, PreferredLocalBaseName, Asset.m_Id.c_str(), vUsedLocalNames);
		if(LocalBaseName != PreferredLocalBaseName)
			SafeInstallName = LocalBaseName + "." + aExt;
		if(str_comp(Category.m_pId, "entity_bg") == 0)
			Asset.m_LocalName = "entity_bg/" + LocalBaseName;
		else
			Asset.m_LocalName = LocalBaseName;
		vUsedLocalNames.insert(Asset.m_LocalName);
		char aInstallPath[IO_MAX_PATH_LENGTH];
		str_format(aInstallPath, sizeof(aInstallPath), "%s/%s", Category.m_pInstallFolder, SafeInstallName.c_str());
		Asset.m_InstallPath = aInstallPath;
		if(str_comp(Category.m_pId, "entity_bg") == 0)
			NormalizeEntityBgWorkshopAsset(Asset, nullptr);

		char aSafeId[80];
		str_copy(aSafeId, Asset.m_Id.c_str(), sizeof(aSafeId));
		SanitizeFilenameInPlace(aSafeId);
		if(aSafeId[0] == '\0')
			str_copy(aSafeId, "asset", sizeof(aSafeId));
		char aThumbPath[IO_MAX_PATH_LENGTH];
		// Always use webp for thumbnail cache to save space
		str_format(aThumbPath, sizeof(aThumbPath), "qmclient/workshop/thumbs/%s.webp", aSafeId);
		Asset.m_ThumbCachePath = aThumbPath;

		vOut.push_back(std::move(Asset));
	}

	std::sort(vOut.begin(), vOut.end(), [](const SWorkshopHudAsset &Left, const SWorkshopHudAsset &Right) { return str_comp(Left.m_Name.c_str(), Right.m_Name.c_str()) < 0; });
	str_copy(pErr, "", ErrSize);
	return true;
}

void ResetWorkshopState(SWorkshopHudState &WorkshopState, IGraphics *pGraphics, bool AbortTasks)
{
	if(AbortTasks && WorkshopState.m_pListTask)
	{
		WorkshopState.m_pListTask->Abort();
	}
	if(AbortTasks && WorkshopState.m_pEntityBgPreviewTask)
	{
		WorkshopState.m_pEntityBgPreviewTask->Abort();
	}
	WorkshopState.m_pListTask.reset();
	WorkshopState.m_pEntityBgPreviewTask.reset();

	for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
	{
		ResetWorkshopAssetRuntimeState(Asset, pGraphics, AbortTasks);
	}

	WorkshopState.m_vAssets.clear();
	WorkshopState.m_vEntityBgPreviewExtByName.clear();
	WorkshopState.m_EntityBgPreviewBaseUrl.clear();
	ClearWorkshopReadyThumbQueue(WorkshopState);
	WorkshopState.m_Requested = false;
	WorkshopState.m_EntityBgPreviewRequested = false;
	WorkshopState.m_LoadFailed = false;
	// Keep m_CacheTime and m_LastRefreshTime for cache reuse
	str_copy(WorkshopState.m_aError, "");
}

// Serialize Workshop assets to JSON file for local cache
static bool SaveWorkshopCache(SWorkshopHudState &WorkshopState, IStorage *pStorage, const char *pCachePath)
{
	if(WorkshopState.m_vAssets.empty())
		return false;

	char aFullPath[IO_MAX_PATH_LENGTH];
	str_format(aFullPath, sizeof(aFullPath), "cache/%s", pCachePath);

	IOHANDLE File = pStorage->OpenFile(aFullPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	std::string JsonStr = "[";
	for(size_t i = 0; i < WorkshopState.m_vAssets.size(); ++i)
	{
		const SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[i];
		if(i > 0) JsonStr += ",";
		JsonStr += "{";
		JsonStr += "\"id\":\"" + JsonEscape(Asset.m_Id) + "\",";
		JsonStr += "\"name\":\"" + JsonEscape(Asset.m_Name) + "\",";
		JsonStr += "\"author\":\"" + JsonEscape(Asset.m_Author) + "\",";
		JsonStr += "\"image_url\":\"" + JsonEscape(Asset.m_ImageUrl) + "\",";
		JsonStr += "\"thumb_url\":\"" + JsonEscape(Asset.m_ThumbUrl) + "\",";
		JsonStr += "\"thumb_cache\":\"" + JsonEscape(Asset.m_ThumbCachePath) + "\",";
		JsonStr += "\"install_path\":\"" + JsonEscape(Asset.m_InstallPath) + "\"";
		JsonStr += "}";
	}
	JsonStr += "]";

	io_write(File, JsonStr.c_str(), JsonStr.size());
	io_close(File);
	return true;
}

// Load Workshop assets from local cache
static bool LoadWorkshopCache(SWorkshopHudState &WorkshopState, IStorage *pStorage, const char *pCachePath)
{
	char aFullPath[IO_MAX_PATH_LENGTH];
	str_format(aFullPath, sizeof(aFullPath), "cache/%s", pCachePath);

	IOHANDLE File = pStorage->OpenFile(aFullPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aBuffer[1024 * 512]; // 512KB buffer
	long FileSize = io_length(File);
	if(FileSize <= 0 || FileSize >= (long)sizeof(aBuffer))
	{
		io_close(File);
		return false;
	}

	io_read(File, aBuffer, FileSize);
	aBuffer[FileSize] = '\0';
	io_close(File);

	json_settings JsonSettings = {};
	char JsonError[1024];
	json_value *pJson = json_parse_ex(&JsonSettings, aBuffer, FileSize, JsonError);
	if(!pJson)
		return false;

	WorkshopState.m_vAssets.clear();
	ClearWorkshopReadyThumbQueue(WorkshopState);
	for(unsigned i = 0; i < pJson->u.array.length; ++i)
	{
		json_value *pItem = pJson->u.array.values[i];
		if(pItem->type != json_object)
			continue;

		SWorkshopHudAsset Asset;
		for(unsigned j = 0; j < pItem->u.object.length; ++j)
		{
			const char *pKey = pItem->u.object.values[j].name;
			json_value *pVal = pItem->u.object.values[j].value;
			if(str_comp(pKey, "id") == 0 && pVal->type == json_string)
				Asset.m_Id = pVal->u.string.ptr;
			else if(str_comp(pKey, "name") == 0 && pVal->type == json_string)
				Asset.m_Name = pVal->u.string.ptr;
			else if(str_comp(pKey, "author") == 0 && pVal->type == json_string)
				Asset.m_Author = NormalizeWorkshopAuthorName(pVal->u.string.ptr);
			else if(str_comp(pKey, "image_url") == 0 && pVal->type == json_string)
				Asset.m_ImageUrl = NormalizeWorkshopAssetUrl(pVal->u.string.ptr);
			else if(str_comp(pKey, "thumb_url") == 0 && pVal->type == json_string)
				Asset.m_ThumbUrl = NormalizeWorkshopAssetUrl(pVal->u.string.ptr);
			else if(str_comp(pKey, "thumb_cache") == 0 && pVal->type == json_string)
				Asset.m_ThumbCachePath = pVal->u.string.ptr;
			else if(str_comp(pKey, "install_path") == 0 && pVal->type == json_string)
				Asset.m_InstallPath = pVal->u.string.ptr;
		}
		if(Asset.m_ImageUrl.empty())
			continue;
		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			NormalizeEntityBgWorkshopAsset(Asset, pStorage);
		if(Asset.m_ThumbUrl.empty() && !str_endswith_nocase(Asset.m_ImageUrl.c_str(), ".map"))
			Asset.m_ThumbUrl = Asset.m_ImageUrl;
		Asset.m_Installed = pStorage->FileExists(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
		WorkshopState.m_vAssets.push_back(std::move(Asset));
	}

	json_value_free(pJson);
	return !WorkshopState.m_vAssets.empty();
}

// Get cache filename for a workshop tab
static const char *GetWorkshopCacheFilename(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_HUD: return "workshop_hud.json";
	case ASSETS_TAB_ENTITIES: return "workshop_entities.json";
	case ASSETS_TAB_GAME: return "workshop_game.json";
	case ASSETS_TAB_EMOTICONS: return "workshop_emoticons.json";
	case ASSETS_TAB_PARTICLES: return "workshop_particles.json";
	case ASSETS_TAB_GUI_CURSOR: return "workshop_gui_cursor.json";
	case ASSETS_TAB_ARROW: return "workshop_arrow.json";
	case ASSETS_TAB_STRONG_WEAK: return "workshop_strong_weak.json";
	case ASSETS_TAB_ENTITY_BG: return "workshop_entity_bg.json";
	case ASSETS_TAB_EXTRAS: return "workshop_extras.json";
	default: return nullptr;
	}
}

bool DeleteLocalAssetByTab(IStorage *pStorage, int CurTab, const char *pAssetName)
{
	if(IsProtectedDefaultAsset(pAssetName))
		return false;

	const char *pSubFolder = nullptr;
	switch(CurTab)
	{
	case ASSETS_TAB_ENTITIES: pSubFolder = "entities"; break;
	case ASSETS_TAB_GAME: pSubFolder = "game"; break;
	case ASSETS_TAB_EMOTICONS: pSubFolder = "emoticons"; break;
	case ASSETS_TAB_PARTICLES: pSubFolder = "particles"; break;
	case ASSETS_TAB_HUD: pSubFolder = "hud"; break;
	case ASSETS_TAB_GUI_CURSOR: pSubFolder = "gui_cursor"; break;
	case ASSETS_TAB_ARROW: pSubFolder = "arrow"; break;
	case ASSETS_TAB_STRONG_WEAK: pSubFolder = "strong_weak"; break;
	case ASSETS_TAB_ENTITY_BG:
	{
		char aMapPath[IO_MAX_PATH_LENGTH];
		ResolveEntityBgLocalMapPath(pStorage, pAssetName, IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
		return pStorage->RemoveFile(aMapPath, IStorage::TYPE_SAVE);
	}
	case ASSETS_TAB_EXTRAS: pSubFolder = "extras"; break;
	default: return false;
	}

	char aSingleFilePath[IO_MAX_PATH_LENGTH];
	str_format(aSingleFilePath, sizeof(aSingleFilePath), "assets/%s/%s.png", pSubFolder, pAssetName);
	bool Removed = pStorage->RemoveFile(aSingleFilePath, IStorage::TYPE_SAVE);

	char aFolderPath[IO_MAX_PATH_LENGTH];
	str_format(aFolderPath, sizeof(aFolderPath), "assets/%s/%s", pSubFolder, pAssetName);
	if(pStorage->FolderExists(aFolderPath, IStorage::TYPE_SAVE))
		Removed |= RemoveFolderTree(pStorage, aFolderPath);

	return Removed;
}

bool CanDeleteLocalAssetByTab(IStorage *pStorage, int CurTab, const char *pAssetName)
{
	if(IsProtectedDefaultAsset(pAssetName))
		return false;

	switch(CurTab)
	{
	case ASSETS_TAB_ENTITY_BG:
	{
		char aMapPath[IO_MAX_PATH_LENGTH];
		ResolveEntityBgLocalMapPath(pStorage, pAssetName, IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
		return pStorage->FileExists(aMapPath, IStorage::TYPE_SAVE);
	}
	case ASSETS_TAB_ENTITIES:
	case ASSETS_TAB_GAME:
	case ASSETS_TAB_EMOTICONS:
	case ASSETS_TAB_PARTICLES:
	case ASSETS_TAB_HUD:
	case ASSETS_TAB_GUI_CURSOR:
	case ASSETS_TAB_ARROW:
	case ASSETS_TAB_STRONG_WEAK:
	case ASSETS_TAB_EXTRAS:
		return true;
	default:
		return false;
	}
}
} // namespace

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_GUI_CURSOR)
		return gs_vpSearchGuiCursorList[Index];
	else if(CurTab == ASSETS_TAB_ARROW)
		return gs_vpSearchArrowList[Index];
	else if(CurTab == ASSETS_TAB_STRONG_WEAK)
		return gs_vpSearchStrongWeakList[Index];
	else if(CurTab == ASSETS_TAB_ENTITY_BG)
		return gs_vpSearchEntityBgList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

static CMenus::SCustomItem *GetCustomItemMutable(int CurTab, size_t Index)
{
	return const_cast<CMenus::SCustomItem *>(GetCustomItem(CurTab, Index));
}

static void ResetCustomItemPreviewState(CMenus::SCustomItem &Item)
{
	Item.m_pDecodeJob.reset();
	Item.m_PreviewImage.Free();
	Item.m_PreviewState = CMenus::SCustomItem::PREVIEW_STATE_UNLOADED;
	Item.m_PreviewEpoch = 0;
	Item.m_PreviewBytes = 0;
	Item.m_PreviewResized = false;
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
		ResetCustomItemPreviewState(Asset);
	}
	vList.clear();
}

void CMenus::SyncEntityBgInstalledWorkshopSources()
{
	SWorkshopHudState *pWorkshopState = WorkshopStateByTab(ASSETS_TAB_ENTITY_BG);
	if(pWorkshopState == nullptr)
		return;

	for(auto NameIt = m_vEntityBgSourceNames.begin(); NameIt != m_vEntityBgSourceNames.end();)
	{
		const auto SourceIt = m_vEntityBgSourceKinds.find(*NameIt);
		if(SourceIt != m_vEntityBgSourceKinds.end() && SourceIt->second == EEntityBgHierarchyEntrySource::WORKSHOP)
		{
			char aMapPath[IO_MAX_PATH_LENGTH];
			ResolveEntityBgLocalMapPath(Storage(), NameIt->c_str(), IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
			if(aMapPath[0] == '\0' || !Storage()->FileExists(aMapPath, IStorage::TYPE_SAVE))
			{
				m_vEntityBgSourceKinds.erase(SourceIt);
				NameIt = m_vEntityBgSourceNames.erase(NameIt);
				continue;
			}
			m_vEntityBgSourceKinds[*NameIt] = EEntityBgHierarchyEntrySource::LOCAL;
		}
		++NameIt;
	}

	for(const SWorkshopHudAsset &Asset : pWorkshopState->m_vAssets)
	{
		if(!Asset.m_Installed || Asset.m_LocalName.empty())
			continue;

		if(std::find(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), Asset.m_LocalName) == m_vEntityBgSourceNames.end())
			m_vEntityBgSourceNames.push_back(Asset.m_LocalName);
		m_vEntityBgSourceKinds[Asset.m_LocalName] = EEntityBgHierarchyEntrySource::WORKSHOP;
	}
}

void CMenus::RefreshEntityBgHierarchyView()
{
	++m_aCustomPreviewEpoch[ASSETS_TAB_ENTITY_BG];
	m_aaCustomPreviewDecodeQueue[ASSETS_TAB_ENTITY_BG].clear();
	m_aaCustomPreviewReadyQueue[ASSETS_TAB_ENTITY_BG].clear();
	m_aaCustomPreviewReadyQueued[ASSETS_TAB_ENTITY_BG].clear();

	ClearAssetList(m_vEntityBgList, Graphics());
	gs_vpSearchEntityBgList.clear();

	::EnsureDefaultAssetVisible(m_vEntityBgSourceNames);
	m_vEntityBgSourceKinds["default"] = EEntityBgHierarchyEntrySource::LOCAL;
	SyncEntityBgInstalledWorkshopSources();

	if(!m_ShowWorkshopAssets && IsEntityBgWorkshopFolderOrChild(m_aEntityBgCurrentFolder))
		m_aEntityBgCurrentFolder[0] = '\0';

	std::vector<SEntityBgHierarchyEntry> vEntries;
	const SWorkshopHudState *pEntityBgWorkshopState = WorkshopStateByTab(ASSETS_TAB_ENTITY_BG);
	if(s_aFilterInputs[ASSETS_TAB_ENTITY_BG].IsEmpty())
	{
		const bool ForceShowWorkshopFolder = pEntityBgWorkshopState != nullptr && !pEntityBgWorkshopState->m_vAssets.empty();
		vEntries = BuildEntityBgHierarchyEntries(m_vEntityBgSourceNames, m_aEntityBgCurrentFolder, m_ShowWorkshopAssets, &m_vEntityBgSourceKinds, ForceShowWorkshopFolder);
	}
	else
	{
		const char *pFilter = s_aFilterInputs[ASSETS_TAB_ENTITY_BG].GetString();
		for(const std::string &AssetName : m_vEntityBgSourceNames)
		{
			const auto SourceIt = m_vEntityBgSourceKinds.find(AssetName);
			const EEntityBgHierarchyEntrySource Source = SourceIt != m_vEntityBgSourceKinds.end() ? SourceIt->second : EEntityBgHierarchyEntrySource::LOCAL;
			if(!m_ShowWorkshopAssets && Source == EEntityBgHierarchyEntrySource::WORKSHOP)
				continue;

			SEntityBgHierarchyEntry Entry;
			str_copy(Entry.m_aName, AssetName.c_str(), sizeof(Entry.m_aName));
			const char *pDisplayName = AssetName.c_str();
			if(const char *pSlash = str_rchr(pDisplayName, '/'))
				pDisplayName = pSlash + 1;
			str_copy(Entry.m_aDisplayName, pDisplayName, sizeof(Entry.m_aDisplayName));
			Entry.m_IsDirectory = false;
			Entry.m_Source = Source;

			const char *pAuthor = FindWorkshopAuthorByLocalName(pEntityBgWorkshopState, Entry.m_aName);
			if(!SearchFilterMatches(pFilter, Entry.m_aDisplayName, pAuthor) &&
				!SearchFilterMatches(pFilter, Entry.m_aName, pAuthor))
				continue;

			vEntries.push_back(Entry);
		}

		std::stable_sort(vEntries.begin(), vEntries.end(), [](const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right) {
			return AssetResourceNameLess(Left.m_aDisplayName, Right.m_aDisplayName);
		});
	}

	m_vEntityBgList.reserve(vEntries.size());
	for(const SEntityBgHierarchyEntry &Entry : vEntries)
	{
		SCustomEntityBg Item;
		str_copy(Item.m_aName, Entry.m_aName, sizeof(Item.m_aName));
		str_copy(Item.m_aDisplayName, Entry.m_aDisplayName, sizeof(Item.m_aDisplayName));
		Item.m_IsDirectory = Entry.m_IsDirectory;
		m_vEntityBgList.push_back(Item);
	}

	dbg_assert(s_CurCustomTab == ASSETS_TAB_ENTITY_BG, "entity bg hierarchy refresh is only valid for entity bg tab");
	gs_aInitCustomList[s_CurCustomTab] = true;
}

void CMenus::ClearCustomItems(int CurTab)
{
	// Reset async loading state first
	m_aAssetLoadStates[CurTab] = ASSET_LOAD_STATE_UNLOADED;
	++m_aCustomPreviewEpoch[CurTab];
	m_aaCustomPreviewDecodeQueue[CurTab].clear();
	m_aaCustomPreviewReadyQueue[CurTab].clear();
	m_aaCustomPreviewReadyQueued[CurTab].clear();
	if(m_apAssetLoadJobs[CurTab])
	{
		// Note: We don't wait for the job to complete here as it could cause a stall.
		// The job will complete in the background and be ignored on next frame.
		m_apAssetLoadJobs[CurTab].reset();
	}

	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			ResetCustomItemPreviewState(Entity);
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();
		gs_vpSearchEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());
		gs_vpSearchGamesList.clear();

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());
		gs_vpSearchEmoticonsList.clear();

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());
		gs_vpSearchParticlesList.clear();

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());
		gs_vpSearchHudList.clear();

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_GUI_CURSOR)
	{
		ClearAssetList(m_vGuiCursorList, Graphics());
		gs_vpSearchGuiCursorList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
	}
	else if(CurTab == ASSETS_TAB_ARROW)
	{
		ClearAssetList(m_vArrowList, Graphics());
		gs_vpSearchArrowList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
	}
	else if(CurTab == ASSETS_TAB_STRONG_WEAK)
	{
		ClearAssetList(m_vStrongWeakList, Graphics());
		gs_vpSearchStrongWeakList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
	}
	else if(CurTab == ASSETS_TAB_ENTITY_BG)
	{
		ClearAssetList(m_vEntityBgList, Graphics());
		m_vEntityBgSourceNames.clear();
		m_vEntityBgSourceKinds.clear();
		m_aEntityBgCurrentFolder[0] = '\0';
		gs_vpSearchEntityBgList.clear();

		GameClient()->m_Background.LoadBackground();
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());
		gs_vpSearchExtrasList.clear();

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else
	{
		dbg_assert_failed("Invalid CurTab: %d", CurTab);
	}
	gs_aInitCustomList[CurTab] = true;
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, FS_LISTDIR_CALLBACK pfnCallback, IStorage *pStorage, TCaller Caller)
{
	if(vAssetList.empty())
	{
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	int ListSize = vAssetList.size();
	const char *pFilter = s_aFilterInputs[s_CurCustomTab].GetString();
	const SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab);
	for(int i = 0; i < ListSize; ++i)
	{
		TName *pAsset = &vAssetList[i];

		if constexpr(std::is_same_v<TName, CMenus::SCustomEntityBg>)
		{
			if(pAsset->m_IsDirectory)
			{
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aDisplayName, pFilter))
					continue;
				vpSearchList.push_back(pAsset);
				continue;
			}
		}

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty())
		{
			const char *pAuthor = FindWorkshopAuthorByLocalName(pWorkshopState, pAsset->m_aName);
			const char *pDisplayName = pAsset->m_aDisplayName[0] != '\0' ? pAsset->m_aDisplayName : pAsset->m_aName;
			if(!SearchFilterMatches(pFilter, pDisplayName, pAuthor) && !SearchFilterMatches(pFilter, pAsset->m_aName, pAuthor))
				continue;
		}

		vpSearchList.push_back(pAsset);
	}
	return vAssetList.size();
}

void CMenus::RenderSettingsCustom(CUIRect MainView)
{
	if(m_AssetsEditorState.m_Open)
	{
		RenderAssetsEditorScreen(MainView);
		return;
	}

	CUIRect TabBar, CustomList, QuickSearch, DirectoryButton, ReloadButton, WorkshopHudView;
	static bool s_AssetsTransitionInitialized = false;
	static int s_PrevAssetsTab = ASSETS_TAB_ENTITIES;
	static float s_AssetsTransitionDirection = 0.0f;
	static bool s_EntityGamePreview = true;
	const uint64_t AssetsTabSwitchNode = UiAnimNodeKey("settings_assets_tab_switch");

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / NUMBER_OF_ASSETS_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	static const char *s_apAssetsTabNames[NUMBER_OF_ASSETS_TABS] = {};
	static char s_aAssetsLanguageFile[IO_MAX_PATH_LENGTH] = {};
	if(str_comp(s_aAssetsLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		str_copy(s_aAssetsLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aAssetsLanguageFile));
		s_apAssetsTabNames[ASSETS_TAB_ENTITIES] = Localize("Entities");
		s_apAssetsTabNames[ASSETS_TAB_GAME] = Localize("Game");
		s_apAssetsTabNames[ASSETS_TAB_EMOTICONS] = Localize("Emoticons");
		s_apAssetsTabNames[ASSETS_TAB_PARTICLES] = Localize("Particles");
		s_apAssetsTabNames[ASSETS_TAB_HUD] = Localize("HUD");
		s_apAssetsTabNames[ASSETS_TAB_GUI_CURSOR] = Localize("Mouse");
		s_apAssetsTabNames[ASSETS_TAB_ARROW] = Localize("Direction Keys");
		s_apAssetsTabNames[ASSETS_TAB_STRONG_WEAK] = Localize("Strong Weak Hook");
		s_apAssetsTabNames[ASSETS_TAB_ENTITY_BG] = Localize("Entity Background Image");
		s_apAssetsTabNames[ASSETS_TAB_EXTRAS] = Localize("Other");
	}

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurCustomTab = Tab;
		}
	}

	if(!s_AssetsTransitionInitialized)
	{
		s_PrevAssetsTab = s_CurCustomTab;
		s_AssetsTransitionInitialized = true;
	}
	else if(s_CurCustomTab != s_PrevAssetsTab)
	{
		s_AssetsTransitionDirection = s_CurCustomTab > s_PrevAssetsTab ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(AssetsTabSwitchNode, 0.18f);
		s_PrevAssetsTab = s_CurCustomTab;
	}

	const float TransitionStrength = ReadUiSwitchAnimation(AssetsTabSwitchNode);
	const bool TransitionActive = TransitionStrength > 0.0f && s_AssetsTransitionDirection != 0.0f;
	const CUIRect ContentClip = MainView;
	const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(MainView, TransitionStrength, s_AssetsTransitionDirection, false, 0.08f, 24.0f, 120.0f);
	}

	// ============================================================================
	// ASYNC ASSET LIST LOADING
	// ============================================================================
	// Start async loading if list is empty and not already loading
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_UNLOADED)
	{
		for(const SAssetResourceCategory &Category : GetAssetResourceCategories())
		{
			if(Category.m_Kind == EAssetResourceKind::NAMED_SINGLE_FILE)
			{
				TryImportLegacySingleFileAsset(Storage(), Category);
				MigrateReservedNamedSingleFileAssets(Storage(), Category);
			}
		}

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);

		const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		switch(s_CurCustomTab)
		{
		case ASSETS_TAB_ENTITIES:
			dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
			break;
		case ASSETS_TAB_GAME:
			dbg_assert(pCurrentCategory != nullptr, "game category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
			break;
		case ASSETS_TAB_EMOTICONS:
			dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
			break;
		case ASSETS_TAB_PARTICLES:
			dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
			break;
		case ASSETS_TAB_HUD:
			dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
			break;
		case ASSETS_TAB_GUI_CURSOR:
			dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
			break;
		case ASSETS_TAB_ARROW:
			dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
			break;
		case ASSETS_TAB_STRONG_WEAK:
			dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
			break;
		case ASSETS_TAB_ENTITY_BG:
			dbg_assert(pCurrentCategory != nullptr, "entity bg category must exist");
			RefreshEntityBgHierarchyView();
			break;
		case ASSETS_TAB_EXTRAS:
			dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
			break;
		}

		// Start async loading job
		m_apAssetLoadJobs[s_CurCustomTab] = std::make_shared<CAssetListLoadJob>(
			static_cast<CAssetListLoadJob::EAssetType>(s_CurCustomTab), Storage());
		Engine()->AddJob(m_apAssetLoadJobs[s_CurCustomTab]);
		m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_LOADING;
	}

	// Check if async loading completed
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING &&
		m_apAssetLoadJobs[s_CurCustomTab] &&
		std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[s_CurCustomTab])->IsCompleted())
	{
		CPerfTimer CompletedTimer;
		auto pJob = std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[s_CurCustomTab]);
		std::vector<CAssetListLoadJob::SAssetEntry> vEntries = pJob->GetEntries();
		{
			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "tab=%d entries=%d", s_CurCustomTab, (int)vEntries.size());
			LogAssetsPerfStage("assets_load_job_complete", CompletedTimer.ElapsedMs(), true, aExtra);
		}

		// The list merge below can reallocate backing storage. Drop queued preview pointers
		// for the current tab before mutating the vectors.
		++m_aCustomPreviewEpoch[s_CurCustomTab];
		m_aaCustomPreviewDecodeQueue[s_CurCustomTab].clear();
		m_aaCustomPreviewReadyQueue[s_CurCustomTab].clear();
		m_aaCustomPreviewReadyQueued[s_CurCustomTab].clear();

		const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);

		// Populate the appropriate list
		CPerfTimer MergeTimer;
		switch(s_CurCustomTab)
		{
		case ASSETS_TAB_ENTITIES:
			for(const auto &Entry : vEntries)
			{
				SCustomEntities EntitiesItem;
				str_copy(EntitiesItem.m_aName, Entry.m_aName);
				m_vEntitiesList.push_back(EntitiesItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vEntitiesList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_GAME:
			for(const auto &Entry : vEntries)
			{
				SCustomGame GameItem;
				str_copy(GameItem.m_aName, Entry.m_aName);
				m_vGameList.push_back(GameItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "game category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vGameList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_EMOTICONS:
			for(const auto &Entry : vEntries)
			{
				SCustomEmoticon EmoticonItem;
				str_copy(EmoticonItem.m_aName, Entry.m_aName);
				m_vEmoticonList.push_back(EmoticonItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vEmoticonList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_PARTICLES:
			for(const auto &Entry : vEntries)
			{
				SCustomParticle ParticleItem;
				str_copy(ParticleItem.m_aName, Entry.m_aName);
				m_vParticlesList.push_back(ParticleItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vParticlesList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_HUD:
			for(const auto &Entry : vEntries)
			{
				SCustomHud HudItem;
				str_copy(HudItem.m_aName, Entry.m_aName);
				m_vHudList.push_back(HudItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vHudList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_GUI_CURSOR:
			for(const auto &Entry : vEntries)
			{
				SCustomGuiCursor Item;
				str_copy(Item.m_aName, Entry.m_aName);
				m_vGuiCursorList.push_back(Item);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vGuiCursorList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_ARROW:
			for(const auto &Entry : vEntries)
			{
				SCustomArrow Item;
				str_copy(Item.m_aName, Entry.m_aName);
				m_vArrowList.push_back(Item);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vArrowList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_STRONG_WEAK:
			for(const auto &Entry : vEntries)
			{
				SCustomStrongWeak Item;
				str_copy(Item.m_aName, Entry.m_aName);
				m_vStrongWeakList.push_back(Item);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vStrongWeakList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_ENTITY_BG:
			m_vEntityBgSourceNames.clear();
			m_vEntityBgSourceKinds.clear();
			m_vEntityBgSourceNames.reserve(vEntries.size());
			m_vEntityBgSourceKinds.reserve(vEntries.size());
			for(const auto &Entry : vEntries)
			{
				m_vEntityBgSourceNames.emplace_back(Entry.m_aName);
				m_vEntityBgSourceKinds[Entry.m_aName] = Entry.m_Source;
			}
			{
				CPerfTimer SortTimer;
				RefreshEntityBgHierarchyView();
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vEntityBgList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		case ASSETS_TAB_EXTRAS:
			for(const auto &Entry : vEntries)
			{
				SCustomExtras ExtrasItem;
				str_copy(ExtrasItem.m_aName, Entry.m_aName);
				m_vExtrasList.push_back(ExtrasItem);
			}
			{
				CPerfTimer SortTimer;
				dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
				char aExtra[128];
				str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d", s_CurCustomTab, (int)m_vExtrasList.size());
				LogAssetsPerfStage("assets_sort_list", SortTimer.ElapsedMs(), false, aExtra);
			}
			break;
		}
		{
			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "tab=%d entries=%d", s_CurCustomTab, (int)vEntries.size());
			LogAssetsPerfStage("assets_merge_results", MergeTimer.ElapsedMs(), true, aExtra);
		}

		m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_LOADED;
		gs_aInitCustomList[s_CurCustomTab] = true;
		m_apAssetLoadJobs[s_CurCustomTab].reset();
	}

	// Mark for search list rebuild if size changed
	switch(s_CurCustomTab)
	{
	case ASSETS_TAB_ENTITIES:
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_GAME:
		if(m_vGameList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_EMOTICONS:
		if(m_vEmoticonList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_PARTICLES:
		if(m_vParticlesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_HUD:
		if(m_vHudList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_GUI_CURSOR:
		if(m_vGuiCursorList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_ARROW:
		if(m_vArrowList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_STRONG_WEAK:
		if(m_vStrongWeakList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_ENTITY_BG:
		if(m_vEntityBgList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_EXTRAS:
		if(m_vExtrasList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);
	if(UsesCombinedAssetList(AssetResourceCategoryByTab(s_CurCustomTab)))
	{
		WorkshopHudView = CustomList;
	}

	// Show loading indicator while async loading is in progress
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING)
	{
		// Only show loading if we haven't loaded any items yet (excluding default)
		bool ShowLoading = false;
		switch(s_CurCustomTab)
		{
		case ASSETS_TAB_ENTITIES: ShowLoading = m_vEntitiesList.size() <= 1; break;
		case ASSETS_TAB_GAME: ShowLoading = m_vGameList.size() <= 1; break;
		case ASSETS_TAB_EMOTICONS: ShowLoading = m_vEmoticonList.size() <= 1; break;
		case ASSETS_TAB_PARTICLES: ShowLoading = m_vParticlesList.size() <= 1; break;
		case ASSETS_TAB_HUD: ShowLoading = m_vHudList.size() <= 1; break;
		case ASSETS_TAB_GUI_CURSOR: ShowLoading = m_vGuiCursorList.size() <= 1; break;
		case ASSETS_TAB_ARROW: ShowLoading = m_vArrowList.size() <= 1; break;
		case ASSETS_TAB_STRONG_WEAK: ShowLoading = m_vStrongWeakList.size() <= 1; break;
		case ASSETS_TAB_ENTITY_BG: ShowLoading = m_vEntityBgList.size() <= 1; break;
		case ASSETS_TAB_EXTRAS: ShowLoading = m_vExtrasList.size() <= 1; break;
		}

		if(ShowLoading)
		{
			// Draw loading spinner in the center of the list area
			const float SpinnerSize = 40.0f;
			CUIRect SpinnerRect;
			SpinnerRect.w = SpinnerSize;
			SpinnerRect.h = SpinnerSize;
			SpinnerRect.x = CustomList.x + (CustomList.w - SpinnerSize) / 2.0f;
			SpinnerRect.y = CustomList.y + (CustomList.h - SpinnerSize) / 2.0f - 20.0f;

			// Use a rotating icon as spinner
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			// Calculate rotation angle based on time
			const float Time = Client()->LocalTime();
			const float Rotation = Time * 360.0f * 2.0f; // 2 rotations per second

			// Render spinner with rotation
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.8f);

			// Draw multiple segments to simulate rotation
			const int NumSegments = 8;
			const float SegmentAngle = 360.0f / NumSegments;
			for(int i = 0; i < NumSegments; i++)
			{
				float Alpha = 0.1f + 0.9f * ((i + (int)(Rotation / SegmentAngle)) % NumSegments) / (float)(NumSegments - 1);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);

				float Angle = (i * SegmentAngle + Rotation) * (3.14159f / 180.0f);
				float CenterX = SpinnerRect.x + SpinnerRect.w / 2.0f;
				float CenterY = SpinnerRect.y + SpinnerRect.h / 2.0f;
				float Radius = SpinnerRect.w / 3.0f;

				IGraphics::CQuadItem Quad(
					CenterX + cosf(Angle) * Radius - 3.0f,
					CenterY + sinf(Angle) * Radius - 3.0f,
					6.0f, 6.0f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}
			Graphics()->QuadsEnd();

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

			// Loading text
			CUIRect LoadingTextRect;
			CustomList.HSplitTop(SpinnerRect.y - CustomList.y + SpinnerSize + 10.0f, nullptr, &LoadingTextRect);
			LoadingTextRect.h = 20.0f;
			Ui()->DoLabel(&LoadingTextRect, Localize("Loading assets..."), 14.0f, TEXTALIGN_MC);
		}
	}

	if(gs_aInitCustomList[s_CurCustomTab])
	{
		CPerfTimer SearchTimer;
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			gs_vpSearchEntitiesList.clear();
			ListSize = m_vEntitiesList.size();
			for(int i = 0; i < ListSize; ++i)
			{
				SCustomEntities *pEntity = &m_vEntitiesList[i];

				// filter quick search
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pEntity->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
					continue;

				gs_vpSearchEntitiesList.push_back(pEntity);
			}
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
		{
			ListSize = InitSearchList(gs_vpSearchGuiCursorList, m_vGuiCursorList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ListSize = InitSearchList(gs_vpSearchArrowList, m_vArrowList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
		{
			ListSize = InitSearchList(gs_vpSearchStrongWeakList, m_vStrongWeakList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
		{
			ListSize = InitSearchList(gs_vpSearchEntityBgList, m_vEntityBgList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
		char aExtra[160];
		str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d filter_len=%d",
			s_CurCustomTab, ListSize, (int)str_length(s_aFilterInputs[s_CurCustomTab].GetString()));
		LogAssetsPerfStage("assets_search_rebuild", SearchTimer.ElapsedMs(), true, aExtra);
	}

	int OldSelected = -1;
	float Margin = 10;
	float TextureWidth = 150;
	float TextureHeight = 150;
	const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);
	SMenuAssetScanUser LazyLoadUser;
	LazyLoadUser.m_pUser = this;
	constexpr int MaxPreviewUploadsPerFrame = 2;
	constexpr size_t MaxPreviewUploadBytesPerFrame = 4 * 1024 * 1024;
	constexpr int MaxPreviewDecodeStartsPerFrame = 6;
	constexpr int MaxPreviewDecodeFinalizesPerFrame = 2;
	constexpr double MaxPreviewDecodeFinalizeMsPerFrame = 95.0;
	constexpr int PreviewPrefetchRows = 2;
	int UploadedPreviewsThisFrame = 0;
	int ResizedPreviewsThisFrame = 0;
	int PreviewDecodeStartsThisFrame = 0;
	int PreviewDecodeFinalizesThisFrame = 0;
	size_t SearchListSize = 0;
	auto &vDecodeQueue = m_aaCustomPreviewDecodeQueue[s_CurCustomTab];
	auto &vReadyQueue = m_aaCustomPreviewReadyQueue[s_CurCustomTab];
	auto &vReadyQueued = m_aaCustomPreviewReadyQueued[s_CurCustomTab];
	const unsigned PreviewEpoch = m_aCustomPreviewEpoch[s_CurCustomTab];

	auto QueueReadyPreview = [&](SCustomItem *pItem) {
		if(vReadyQueued.insert(pItem).second)
		{
			vReadyQueue.push_back(pItem);
			char aExtra[160];
			str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s queue_size=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, (int)vReadyQueue.size(), (unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStage("assets_preview_upload_queue_push", 0.0, true, aExtra);
		}
	};

	auto StartPreviewDecode = [&](size_t Index) {
		if(m_aAssetLoadStates[s_CurCustomTab] != ASSET_LOAD_STATE_LOADED)
			return;
		SCustomItem *pItem = GetCustomItemMutable(s_CurCustomTab, Index);
		if(pItem == nullptr || PreviewDecodeStartsThisFrame >= MaxPreviewDecodeStartsPerFrame)
			return;
		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<SCustomEntityBg *>(pItem)->m_IsDirectory)
			return;
		if(pItem->m_RenderTexture.IsValid() || pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADING ||
			pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_READY || pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADED)
			return;
		pItem->m_PreviewImage.Free();
		pItem->m_PreviewEpoch = PreviewEpoch;
		pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_LOADING;
		pItem->m_PreviewBytes = 0;
		pItem->m_PreviewResized = false;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			SCustomEntities *pEntity = gs_vpSearchEntitiesList[Index];
			StartEntitiesDecode(pEntity, Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			SCustomGame *pGame = gs_vpSearchGamesList[Index];
			StartAssetDecode(pGame, "game", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			SCustomEmoticon *pEmoticon = gs_vpSearchEmoticonsList[Index];
			StartAssetDecode(pEmoticon, "emoticons", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			SCustomParticle *pParticle = gs_vpSearchParticlesList[Index];
			StartAssetDecode(pParticle, "particles", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			SCustomHud *pHud = gs_vpSearchHudList[Index];
			StartAssetDecode(pHud, "hud", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
		{
			SCustomGuiCursor *pGuiCursor = gs_vpSearchGuiCursorList[Index];
			StartAssetDecode(pGuiCursor, "gui_cursor", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			SCustomArrow *pArrow = gs_vpSearchArrowList[Index];
			StartAssetDecode(pArrow, "arrow", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
		{
			SCustomStrongWeak *pStrongWeak = gs_vpSearchStrongWeakList[Index];
			StartAssetDecode(pStrongWeak, "strong_weak", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
		{
			SCustomEntityBg *pEntityBg = gs_vpSearchEntityBgList[Index];
			StartEntityBgDecode(pEntityBg, Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			SCustomExtras *pExtras = gs_vpSearchExtrasList[Index];
			StartAssetDecode(pExtras, "extras", Storage(), Engine());
		}
		if(pItem->m_pDecodeJob)
		{
			vDecodeQueue.push_back(pItem);
			++PreviewDecodeStartsThisFrame;
		}
		else
		{
			pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_FAILED;
		}
	};

	auto SchedulePreviewRange = [&](int FirstIndex, int LastIndex, int ItemsPerRow) {
		if(SearchListSize == 0 || FirstIndex < 0 || LastIndex < 0)
			return;
		const int PrefetchItems = maximum(1, ItemsPerRow) * PreviewPrefetchRows;
		const int FirstRelevant = maximum(0, FirstIndex - PrefetchItems);
		const int LastRelevant = minimum((int)SearchListSize - 1, LastIndex + PrefetchItems);
		for(int Index = FirstRelevant; Index <= LastRelevant && PreviewDecodeStartsThisFrame < MaxPreviewDecodeStartsPerFrame; ++Index)
			StartPreviewDecode((size_t)Index);
	};

	auto RenderCardBadge = [&](const CUIRect &Rect, const char *pLabel, const ColorRGBA &FillColor, float FontSize) {
		CUIRect BadgeRect = Rect;
		BadgeRect.Draw(FillColor, IGraphics::CORNER_ALL, minimum(BadgeRect.h / 2.0f, 6.0f));
		Ui()->DoLabel(&BadgeRect, pLabel, FontSize, TEXTALIGN_MC);
	};

	auto RenderEntityBgFallback = [&](const CUIRect &Rect) {
		CUIRect FallbackRect = Rect;
		FallbackRect.Margin(6.0f, &FallbackRect);
		FallbackRect.Draw(ColorRGBA(0.14f, 0.16f, 0.18f, 0.9f), IGraphics::CORNER_ALL, 8.0f);

		CUIRect LabelRect = FallbackRect;
		LabelRect.Margin(8.0f, &LabelRect);
		Ui()->DoLabel(&LabelRect, Localize("Map Preview"), 11.0f, TEXTALIGN_MC);
	};

	struct SAssetCardHeaderLayout
	{
		CUIRect m_TextureRect;
		CUIRect m_TitleRect;
		CUIRect m_AuthorRect;
		CUIRect m_ActionButtonRect;
		CUIRect m_StatusTagRect;
		CUIRect m_LocalOnlyBadgeRect;
		bool m_HasActionButton = false;
		bool m_HasStatusTag = false;
		bool m_HasAuthorRow = false;
		bool m_HasLocalOnlyBadge = false;
	};

	constexpr float AssetCardFooterSpacing = 8.0f;
	constexpr float AssetCardHeaderHeightSingleLine = 24.0f;
	constexpr float AssetCardHeaderHeightWithAuthor = 34.0f;
	constexpr float AssetCardHeaderMargin = 3.0f;
	constexpr float AssetCardHeaderControlMargin = 2.0f;
	constexpr float AssetCardHeaderControlHeight = AssetCardHeaderHeightSingleLine - AssetCardHeaderMargin * 2.0f - AssetCardHeaderControlMargin * 2.0f;
	constexpr float AssetCardTextReserveSingleLine = 20.0f;
	constexpr float AssetCardTextReserveWithAuthor = 34.0f;

	auto LayoutAssetCardHeader = [&](const CUIRect &CardRect, bool HasActionButton, const char *pStatusLabel, bool ShowLocalOnlyBadge, bool ShowAuthorRow) {
		SAssetCardHeaderLayout Layout;
		CUIRect HeaderRect;
		CUIRect BodyRect = CardRect;
		const float AssetCardHeaderHeight = ShowAuthorRow ? AssetCardHeaderHeightWithAuthor : AssetCardHeaderHeightSingleLine;
		BodyRect.HSplitTop(AssetCardHeaderHeight, &HeaderRect, &Layout.m_TextureRect);
		Layout.m_TextureRect.HSplitTop(8.0f, nullptr, &Layout.m_TextureRect);

		CUIRect TitleRect = HeaderRect;
		TitleRect.Margin(AssetCardHeaderMargin, &TitleRect);

		if(HasActionButton)
		{
			Layout.m_HasActionButton = true;
			TitleRect.VSplitRight(24.0f, &TitleRect, &Layout.m_ActionButtonRect);
		}

		if(Layout.m_HasActionButton)
		{
			Layout.m_ActionButtonRect.Margin(AssetCardHeaderControlMargin, &Layout.m_ActionButtonRect);
			const float ControlHeight = minimum(AssetCardHeaderControlHeight, Layout.m_ActionButtonRect.h);
			if(Layout.m_ActionButtonRect.h > ControlHeight)
			{
				const float VerticalInset = (Layout.m_ActionButtonRect.h - ControlHeight) / 2.0f;
				Layout.m_ActionButtonRect.y += VerticalInset;
				Layout.m_ActionButtonRect.h = ControlHeight;
			}
		}

		const float BadgeGap = 4.0f;
		const float TagPadding = 6.0f;
		const float TagMinWidth = 0.0f;
		const float LocalOnlyBadgeMinWidth = 0.0f;
		const float TitleMinWidth = 52.0f;
		const float TagFontSize = 7.5f;
		const float BadgeFontSize = 7.5f;
		const float DownloadedTagBaseWidth = TextRender()->TextWidth(TagFontSize, Localize("Downloaded"), -1, -1.0f) + TagPadding * 2.0f;
		const float UnifiedStatusTagWidth = DownloadedTagBaseWidth;
		const float HeaderControlHeight = minimum(AssetCardHeaderControlHeight, TitleRect.h);
		auto ReserveTrailingRect = [&](CUIRect &AvailableRect, float DesiredWidth, float MinWidth, CUIRect &OutRect, bool &OutVisible) {
			if(AvailableRect.w <= 0.0f)
				return;

			const float AvailableWidth = AvailableRect.w;
			const float ReservedTitleWidth = minimum(TitleMinWidth, AvailableWidth);
			const float MaxWidth = maximum(0.0f, AvailableWidth - ReservedTitleWidth);
			if(MaxWidth <= 0.0f)
				return;

			const float Width = minimum(maximum(DesiredWidth, minimum(MinWidth, MaxWidth)), MaxWidth);
			AvailableRect.VSplitRight(Width, &AvailableRect, &OutRect);
			OutVisible = true;

			if(AvailableRect.w > BadgeGap)
			{
				const float GapWidth = minimum(BadgeGap, AvailableRect.w);
				AvailableRect.VSplitRight(GapWidth, &AvailableRect, nullptr);
			}
		};

		if(pStatusLabel != nullptr && pStatusLabel[0] != '\0')
		{
			const float TagHeight = HeaderControlHeight;
			const float DesiredTagWidth = UnifiedStatusTagWidth;
			ReserveTrailingRect(TitleRect, DesiredTagWidth, TagMinWidth, Layout.m_StatusTagRect, Layout.m_HasStatusTag);
			if(Layout.m_HasStatusTag && TagHeight > 0.0f && Layout.m_StatusTagRect.h > TagHeight)
			{
				const float VerticalInset = (Layout.m_StatusTagRect.h - TagHeight) / 2.0f;
				Layout.m_StatusTagRect.y += VerticalInset;
				Layout.m_StatusTagRect.h = TagHeight;
			}
		}

		if(ShowLocalOnlyBadge)
		{
			const char *pBadgeLabel = Localize("Local-only");
			const float BadgePadding = 6.0f;
			const float BadgeHeight = HeaderControlHeight;
			const float DesiredBadgeWidth = TextRender()->TextWidth(BadgeFontSize, pBadgeLabel, -1, -1.0f) + BadgePadding * 2.0f;
			ReserveTrailingRect(TitleRect, DesiredBadgeWidth, LocalOnlyBadgeMinWidth, Layout.m_LocalOnlyBadgeRect, Layout.m_HasLocalOnlyBadge);
			if(Layout.m_HasLocalOnlyBadge && BadgeHeight > 0.0f && Layout.m_LocalOnlyBadgeRect.h > BadgeHeight)
			{
				const float VerticalInset = (Layout.m_LocalOnlyBadgeRect.h - BadgeHeight) / 2.0f;
				Layout.m_LocalOnlyBadgeRect.y += VerticalInset;
				Layout.m_LocalOnlyBadgeRect.h = BadgeHeight;
			}
		}

		Layout.m_HasAuthorRow = ShowAuthorRow;
		if(Layout.m_HasAuthorRow)
			TitleRect.HSplitTop(11.0f, &Layout.m_TitleRect, &Layout.m_AuthorRect);
		else
			Layout.m_TitleRect = TitleRect;
		return Layout;
	};

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 75;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
	{
		SearchListSize = gs_vpSearchGuiCursorList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		SearchListSize = gs_vpSearchArrowList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
	{
		SearchListSize = gs_vpSearchStrongWeakList.size();
		TextureHeight = 50;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
	{
		SearchListSize = gs_vpSearchEntityBgList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}

	const float AssetCardTextReserve = s_CurCustomTab == ASSETS_TAB_ENTITY_BG ? AssetCardTextReserveSingleLine : AssetCardTextReserveWithAuthor;

	{
		CPerfTimer ScanTimer;
		CPerfTimer DecodeFinalizeTimer;
		int ReadyCount = 0;
		int DroppedStale = 0;
		int DeferredCompleted = 0;
		const size_t PendingCount = vDecodeQueue.size();
		for(size_t i = 0; i < PendingCount; ++i)
		{
			SCustomItem *pItem = vDecodeQueue.front();
			vDecodeQueue.pop_front();
			if(pItem == nullptr)
				continue;
			if(pItem->m_PreviewEpoch != PreviewEpoch)
			{
				ResetCustomItemPreviewState(*pItem);
				++DroppedStale;
				char aDropExtra[128];
				str_format(aDropExtra, sizeof(aDropExtra), "tab=%d asset=%s", s_CurCustomTab, pItem->m_aName);
				LogAssetsPerfStage("assets_preview_decode_drop_stale", 0.0, true, aDropExtra);
				continue;
			}
			if(!pItem->m_pDecodeJob)
			{
				if(pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADING)
					pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_UNLOADED;
				continue;
			}

			auto pDecodeJob = std::static_pointer_cast<CFullAsyncImageLoadJob>(pItem->m_pDecodeJob);
			if(!pDecodeJob->IsCompleted())
			{
				vDecodeQueue.push_back(pItem);
				continue;
			}

			if(PreviewDecodeFinalizesThisFrame >= MaxPreviewDecodeFinalizesPerFrame ||
				(PreviewDecodeFinalizesThisFrame > 0 && DecodeFinalizeTimer.ElapsedMs() >= MaxPreviewDecodeFinalizeMsPerFrame))
			{
				vDecodeQueue.push_back(pItem);
				++DeferredCompleted;
				continue;
			}

			CPerfTimer DecodeFinalizeBatchTimer;
			CPerfTimer FinalizeTimer;
			CFullAsyncImageLoadJob::SResult Result;
			{
				CPerfTimer GetResultTimer;
				Result = pDecodeJob->GetResult();
				LogAssetsPerfStage("assets_finalize_get_result", GetResultTimer.ElapsedMs(), false, pItem->m_aName);
			}
			pItem->m_pDecodeJob.reset();
			if(!Result.m_Success || !Result.m_Image.m_pData)
			{
				Result.m_Image.Free();
				pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_FAILED;
				continue;
			}

			const bool ResizedPreview = Result.m_Resized;
			{
				CPerfTimer TargetSizeTimer;
				char aTargetSizeExtra[160];
				str_format(aTargetSizeExtra, sizeof(aTargetSizeExtra), "asset=%s src=%dx%d dst=%dx%d resized=%d",
					pItem->m_aName, Result.m_SourceWidth > 0 ? Result.m_SourceWidth : (int)Result.m_Image.m_Width,
					Result.m_SourceHeight > 0 ? Result.m_SourceHeight : (int)Result.m_Image.m_Height,
					(int)Result.m_Image.m_Width, (int)Result.m_Image.m_Height, ResizedPreview ? 1 : 0);
				LogAssetsPerfStage("assets_finalize_target_size_calc", TargetSizeTimer.ElapsedMs(), false, aTargetSizeExtra);
			}

			{
				CPerfTimer PostProcessTimer;
				pItem->m_PreviewImage = std::move(Result.m_Image);
				pItem->m_PreviewBytes = pItem->m_PreviewImage.DataSize();
				pItem->m_PreviewResized = ResizedPreview;
				char aPostProcessExtra[160];
				str_format(aPostProcessExtra, sizeof(aPostProcessExtra), "asset=%s bytes=%u resized=%d",
					pItem->m_aName, (unsigned)pItem->m_PreviewBytes, ResizedPreview ? 1 : 0);
				LogAssetsPerfStage("assets_finalize_postprocess_pixels", PostProcessTimer.ElapsedMs(), false, aPostProcessExtra);
			}
			pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_READY;
			QueueReadyPreview(pItem);
			++ReadyCount;
			++PreviewDecodeFinalizesThisFrame;
			char aFinalizeTotalExtra[192];
			str_format(aFinalizeTotalExtra, sizeof(aFinalizeTotalExtra), "tab=%d asset=%s resized=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, ResizedPreview ? 1 : 0, (unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStage("assets_finalize_total", FinalizeTimer.ElapsedMs(), false, aFinalizeTotalExtra);
			char aReadyExtra[160];
			str_format(aReadyExtra, sizeof(aReadyExtra), "tab=%d asset=%s w=%u h=%u bytes=%u resized=%d",
				s_CurCustomTab, pItem->m_aName, (unsigned)pItem->m_PreviewImage.m_Width,
				(unsigned)pItem->m_PreviewImage.m_Height, (unsigned)pItem->m_PreviewBytes, ResizedPreview ? 1 : 0);
			LogAssetsPerfStage("assets_preview_decode_ready", 0.0, true, aReadyExtra);
			char aFinalizeExtra[192];
			str_format(aFinalizeExtra, sizeof(aFinalizeExtra), "tab=%d asset=%s finalized=%d deferred=%d resized=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, PreviewDecodeFinalizesThisFrame, DeferredCompleted, ResizedPreview ? 1 : 0,
				(unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStage("assets_preview_decode_finalize_batch", DecodeFinalizeBatchTimer.ElapsedMs(), false, aFinalizeExtra);
		}
		char aFinalizeTotalExtra[160];
		str_format(aFinalizeTotalExtra, sizeof(aFinalizeTotalExtra), "tab=%d finalized=%d deferred=%d pending_after=%d budget_ms=%.1f used_ms=%.3f",
			s_CurCustomTab, PreviewDecodeFinalizesThisFrame, DeferredCompleted, (int)vDecodeQueue.size(),
			MaxPreviewDecodeFinalizeMsPerFrame, DecodeFinalizeTimer.ElapsedMs());
		LogAssetsPerfStage("assets_preview_decode_finalize_total", DecodeFinalizeTimer.ElapsedMs(), false, aFinalizeTotalExtra);

		size_t UploadedBytesThisFrame = 0;
		while(!vReadyQueue.empty() && UploadedPreviewsThisFrame < MaxPreviewUploadsPerFrame)
		{
			SCustomItem *pItem = vReadyQueue.front();
			if(pItem == nullptr)
			{
				vReadyQueue.pop_front();
				continue;
			}

			const size_t ItemBytes = pItem->m_PreviewBytes;
			if(UploadedPreviewsThisFrame > 0 && UploadedBytesThisFrame + ItemBytes > MaxPreviewUploadBytesPerFrame)
				break;

			vReadyQueue.pop_front();
			vReadyQueued.erase(pItem);
			if(pItem->m_PreviewEpoch != PreviewEpoch || pItem->m_PreviewState != SCustomItem::PREVIEW_STATE_READY || !pItem->m_PreviewImage.m_pData)
			{
				ResetCustomItemPreviewState(*pItem);
				continue;
			}

			CPerfTimer UploadBatchTimer;
			pItem->m_RenderTexture = Graphics()->LoadTextureRawMove(pItem->m_PreviewImage, 0, pItem->m_aName);
			char aFinalizeUploadExtra[160];
			str_format(aFinalizeUploadExtra, sizeof(aFinalizeUploadExtra), "tab=%d asset=%s bytes=%u",
				s_CurCustomTab, pItem->m_aName, (unsigned)ItemBytes);
			LogAssetsPerfStage("assets_finalize_load_texture_raw_move", UploadBatchTimer.ElapsedMs(), false, aFinalizeUploadExtra);
			pItem->m_PreviewBytes = 0;
			pItem->m_PreviewState = pItem->m_RenderTexture.IsValid() ? SCustomItem::PREVIEW_STATE_LOADED : SCustomItem::PREVIEW_STATE_FAILED;
			UploadedBytesThisFrame += ItemBytes;
			++UploadedPreviewsThisFrame;
			if(pItem->m_PreviewResized)
				++ResizedPreviewsThisFrame;
			char aUploadExtra[192];
			str_format(aUploadExtra, sizeof(aUploadExtra), "tab=%d asset=%s uploads_this_frame=%d bytes=%u bytes_used=%u queue_remaining=%d resized=%d",
				s_CurCustomTab, pItem->m_aName, UploadedPreviewsThisFrame, (unsigned)ItemBytes,
				(unsigned)UploadedBytesThisFrame, (int)vReadyQueue.size(), pItem->m_PreviewResized ? 1 : 0);
			LogAssetsPerfStage("assets_preview_gpu_upload_batch", 0.0, true, aUploadExtra);
			pItem->m_PreviewResized = false;
		}
		char aDrainExtra[192];
		str_format(aDrainExtra, sizeof(aDrainExtra), "tab=%d processed=%d bytes_budget=%u queue_remaining=%d bytes_used=%u",
			s_CurCustomTab, UploadedPreviewsThisFrame, (unsigned)MaxPreviewUploadBytesPerFrame, (int)vReadyQueue.size(),
			(unsigned)UploadedBytesThisFrame);
		LogAssetsPerfStage("assets_preview_upload_queue_drain", 0.0, true, aDrainExtra);

		char aExtra[192];
		str_format(aExtra, sizeof(aExtra), "tab=%d search_list=%d decode_pending=%d ready_queue=%d ready=%d dropped_stale=%d uploads=%d resized=%d finalized=%d deferred=%d finalize_budget_ms=%.1f",
			s_CurCustomTab, (int)SearchListSize, (int)vDecodeQueue.size(), (int)vReadyQueue.size(),
			ReadyCount, DroppedStale, UploadedPreviewsThisFrame, ResizedPreviewsThisFrame, PreviewDecodeFinalizesThisFrame, DeferredCompleted,
			MaxPreviewDecodeFinalizeMsPerFrame);
		LogAssetsPerfStage("assets_preview_gpu_upload_scan", ScanTimer.ElapsedMs(), false, aExtra);
	}

	if(!UsesCombinedAssetList(pCurrentCategory))
	{
		static CListBox s_ListBox;
		const int LocalColumns = maximum(1, (int)(CustomList.w / (Margin + TextureWidth)));
		s_ListBox.DoStart(TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin, SearchListSize, LocalColumns, 1, OldSelected, &CustomList, false);
		static std::vector<CButtonContainer> s_vLocalDeleteButtons;
		s_vLocalDeleteButtons.resize(SearchListSize);
		static char s_aPendingDeleteName[IO_MAX_PATH_LENGTH] = "";
		static CUi::SConfirmPopupContext s_DeleteConfirmPopup;
		bool DeleteLocalRequested = false;
		char aDeleteLocalName[IO_MAX_PATH_LENGTH] = "";
		int FirstVisibleIndex = -1;
		int LastVisibleIndex = -1;
		for(size_t i = 0; i < SearchListSize; ++i)
		{
			const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
			if(pItem == nullptr)
				continue;

			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetsEntities) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetGame) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetEmoticons) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetParticles) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetHud) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetGuiCursor) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetArrow) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetStrongWeak) == 0)
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			{
				if(IsEntityBgConfigSelected(pItem->m_aName))
					OldSelected = i;
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				if(str_comp(pItem->m_aName, g_Config.m_ClAssetExtras) == 0)
					OldSelected = i;
			}

			const CListboxItem Item = s_ListBox.DoNextItem(pItem, OldSelected >= 0 && (size_t)OldSelected == i);
			CUIRect ItemRect = Item.m_Rect;
			ItemRect.Margin(Margin / 2, &ItemRect);
			if(!Item.m_Visible)
				continue;
			if(FirstVisibleIndex < 0)
				FirstVisibleIndex = (int)i;
			LastVisibleIndex = (int)i;

			const CUIRect CardRect = ItemRect;
			const bool HasDeleteButton = CanDeleteLocalAssetByTab(Storage(), s_CurCustomTab, pItem->m_aName);
			const bool ShowLocalOnlyBadge = pCurrentCategory != nullptr && pCurrentCategory->m_LocalOnlyBadge && !pCurrentCategory->m_WorkshopEnabled;
			const SAssetCardHeaderLayout HeaderLayout = LayoutAssetCardHeader(CardRect, HasDeleteButton, nullptr, ShowLocalOnlyBadge, false);
			CUIRect TitleRect = HeaderLayout.m_TitleRect;
			SLabelProperties TitleProps;
			TitleProps.m_MaxWidth = static_cast<int>(TitleRect.w);
			Ui()->DoLabel(&TitleRect, AssetCardDisplayName(pItem), 9.5f, TEXTALIGN_ML, TitleProps);
			if(HeaderLayout.m_HasLocalOnlyBadge)
				RenderCardBadge(HeaderLayout.m_LocalOnlyBadgeRect, Localize("Local-only"), ColorRGBA(0.46f, 0.41f, 0.20f, 0.88f), 7.5f);
			if(HeaderLayout.m_HasAuthorRow)
			{
				SLabelProperties AuthorProps;
				AuthorProps.m_MaxWidth = static_cast<int>(HeaderLayout.m_AuthorRect.w);
				Ui()->DoLabel(&HeaderLayout.m_AuthorRect, "--", 7.0f, TEXTALIGN_ML, AuthorProps);
			}
			if(pItem->m_RenderTexture.IsValid())
			{
				Graphics()->WrapClamp();
				Graphics()->TextureSet(pItem->m_RenderTexture);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				IGraphics::CQuadItem QuadItem(HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2, HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
			else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			{
				RenderEntityBgFallback(HeaderLayout.m_TextureRect);
			}

			if(HasDeleteButton)
			{
				if(Ui()->DoButton_FontIcon(&s_vLocalDeleteButtons[i], FONT_ICON_TRASH, 0, &HeaderLayout.m_ActionButtonRect, IGraphics::CORNER_ALL))
				{
					DeleteLocalRequested = true;
					str_copy(aDeleteLocalName, pItem->m_aName, sizeof(aDeleteLocalName));
				}
			}
		}
		if(FirstVisibleIndex >= 0)
		{
			SchedulePreviewRange(FirstVisibleIndex, LastVisibleIndex, LocalColumns);
			char aVisibleExtra[160];
			str_format(aVisibleExtra, sizeof(aVisibleExtra), "tab=%d first=%d last=%d starts=%d",
				s_CurCustomTab, FirstVisibleIndex, LastVisibleIndex, PreviewDecodeStartsThisFrame);
			LogAssetsPerfStage("assets_preview_decode_start_visible", 0.0, PreviewDecodeStartsThisFrame > 0, aVisibleExtra);
		}

		const int NewSelected = s_ListBox.DoEnd();
		auto ResetSelectedAssetToDefault = [&](const char *pDeletedName) {
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES && str_comp(g_Config.m_ClAssetsEntities, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetsEntities, "default");
				GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME && str_comp(g_Config.m_ClAssetGame, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetGame, "default");
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS && str_comp(g_Config.m_ClAssetEmoticons, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetEmoticons, "default");
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES && str_comp(g_Config.m_ClAssetParticles, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetParticles, "default");
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD && str_comp(g_Config.m_ClAssetHud, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetHud, "default");
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR && str_comp(g_Config.m_ClAssetGuiCursor, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetGuiCursor, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW && str_comp(g_Config.m_ClAssetArrow, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetArrow, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
			}
			else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK && str_comp(g_Config.m_ClAssetStrongWeak, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetStrongWeak, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && IsEntityBgConfigSelected(pDeletedName))
			{
				g_Config.m_ClBackgroundEntities[0] = '\0';
				GameClient()->m_Background.LoadBackground();
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS && str_comp(g_Config.m_ClAssetExtras, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetExtras, "default");
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
		};

		if(DeleteLocalRequested)
		{
			str_copy(s_aPendingDeleteName, aDeleteLocalName, sizeof(s_aPendingDeleteName));
			s_DeleteConfirmPopup.Reset();
			s_DeleteConfirmPopup.YesNoButtons();
			str_copy(s_DeleteConfirmPopup.m_aMessage, Localize("Are you sure you want to delete this asset?"));
			Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_DeleteConfirmPopup);
		}

		if(s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			if(DeleteLocalAssetByTab(Storage(), s_CurCustomTab, s_aPendingDeleteName))
			{
				ResetSelectedAssetToDefault(s_aPendingDeleteName);
				ClearCustomItems(s_CurCustomTab);
			}
			else
			{
				dbg_msg("assets", "failed to delete local asset '%s' in tab %d", s_aPendingDeleteName, s_CurCustomTab);
			}
			s_DeleteConfirmPopup.Reset();
			s_aPendingDeleteName[0] = '\0';
		}
		else if(s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
		{
			s_DeleteConfirmPopup.Reset();
			s_aPendingDeleteName[0] = '\0';
		}

		if(!DeleteLocalRequested && s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && s_ListBox.WasItemSelected() && NewSelected >= 0 && OldSelected != NewSelected)
		{
			const SCustomItem *pSelectedItem = GetCustomItem(s_CurCustomTab, NewSelected);
			if(pSelectedItem->m_aName[0] != '\0')
			{
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
				{
					const auto *pEntityBgItem = static_cast<const SCustomEntityBg *>(pSelectedItem);
					if(pEntityBgItem->m_IsDirectory)
					{
						if(str_comp(pSelectedItem->m_aName, "..") == 0)
						{
							char aParentFolder[IO_MAX_PATH_LENGTH];
							BuildEntityBgParentFolder(m_aEntityBgCurrentFolder, aParentFolder, sizeof(aParentFolder));
							str_copy(m_aEntityBgCurrentFolder, aParentFolder, sizeof(m_aEntityBgCurrentFolder));
						}
						else
						{
							str_copy(m_aEntityBgCurrentFolder, pSelectedItem->m_aName, sizeof(m_aEntityBgCurrentFolder));
						}
						RefreshEntityBgHierarchyView();
						return;
					}
				}

				if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
				{
					str_copy(g_Config.m_ClAssetsEntities, pSelectedItem->m_aName);
					GameClient()->m_MapImages.ChangeEntitiesPath(pSelectedItem->m_aName);
				}
				else if(s_CurCustomTab == ASSETS_TAB_GAME)
				{
					str_copy(g_Config.m_ClAssetGame, pSelectedItem->m_aName);
					GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				{
					str_copy(g_Config.m_ClAssetEmoticons, pSelectedItem->m_aName);
					GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
				}
				else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				{
					str_copy(g_Config.m_ClAssetParticles, pSelectedItem->m_aName);
					GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
				}
				else if(s_CurCustomTab == ASSETS_TAB_HUD)
				{
					str_copy(g_Config.m_ClAssetHud, pSelectedItem->m_aName);
					GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
				}
				else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
				{
					str_copy(g_Config.m_ClAssetGuiCursor, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
				}
				else if(s_CurCustomTab == ASSETS_TAB_ARROW)
				{
					str_copy(g_Config.m_ClAssetArrow, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
				}
				else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
				{
					str_copy(g_Config.m_ClAssetStrongWeak, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
				}
				else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
				{
					ApplyEntityBgSelection(GameClient(), pSelectedItem->m_aName);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
				{
					str_copy(g_Config.m_ClAssetExtras, pSelectedItem->m_aName);
					GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
				}
			}
		}
	}

	if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory) && WorkshopHudView.h > 0.0f)
	{
		SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab);
		dbg_assert(pWorkshopState != nullptr, "workshop asset state must exist");
		SWorkshopHudState &WorkshopState = *pWorkshopState;
		const char *pInstallFolder = pCategory->m_pInstallFolder;

		auto IsLocalAssetSelected = [&](const char *pName) {
			switch(s_CurCustomTab)
			{
			case ASSETS_TAB_HUD: return str_comp(pName, g_Config.m_ClAssetHud) == 0;
			case ASSETS_TAB_GAME: return str_comp(pName, g_Config.m_ClAssetGame) == 0;
			case ASSETS_TAB_EMOTICONS: return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
			case ASSETS_TAB_PARTICLES: return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
			case ASSETS_TAB_GUI_CURSOR: return str_comp(pName, g_Config.m_ClAssetGuiCursor) == 0;
			case ASSETS_TAB_ARROW: return str_comp(pName, g_Config.m_ClAssetArrow) == 0;
			case ASSETS_TAB_STRONG_WEAK: return str_comp(pName, g_Config.m_ClAssetStrongWeak) == 0;
			case ASSETS_TAB_ENTITY_BG: return IsEntityBgConfigSelected(pName);
			case ASSETS_TAB_EXTRAS: return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
			case ASSETS_TAB_ENTITIES: return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
			default: return false;
			}
		};

		auto ApplyLocalAssetSelection = [&](const char *pName) {
			switch(s_CurCustomTab)
			{
			case ASSETS_TAB_HUD:
				str_copy(g_Config.m_ClAssetHud, pName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
				break;
			case ASSETS_TAB_GAME:
				str_copy(g_Config.m_ClAssetGame, pName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				break;
			case ASSETS_TAB_EMOTICONS:
				str_copy(g_Config.m_ClAssetEmoticons, pName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
				break;
			case ASSETS_TAB_PARTICLES:
				str_copy(g_Config.m_ClAssetParticles, pName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
				break;
			case ASSETS_TAB_GUI_CURSOR:
				str_copy(g_Config.m_ClAssetGuiCursor, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
				break;
			case ASSETS_TAB_ARROW:
				str_copy(g_Config.m_ClAssetArrow, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
				break;
			case ASSETS_TAB_STRONG_WEAK:
				str_copy(g_Config.m_ClAssetStrongWeak, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
				break;
			case ASSETS_TAB_ENTITY_BG:
				ApplyEntityBgSelection(GameClient(), pName);
				break;
			case ASSETS_TAB_EXTRAS:
				str_copy(g_Config.m_ClAssetExtras, pName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
				break;
			case ASSETS_TAB_ENTITIES:
				str_copy(g_Config.m_ClAssetsEntities, pName);
				GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
				break;
			default:
				break;
			}
		};

		auto StartWorkshopListTask = [&]() {
			auto pTask = HttpGet(WORKSHOP_ASSETS_URL);
			pTask->Timeout(CTimeout{10000, 20000, 200, 10});
			pTask->LogProgress(HTTPLOG::FAILURE);
			pTask->FailOnErrorStatus(false);
			WorkshopState.m_pListTask = std::move(pTask);
			WorkshopState.m_LoadFailed = false;
			str_copy(WorkshopState.m_aError, "");
			Http()->Run(WorkshopState.m_pListTask);
		};

		auto StartEntityBgPreviewTask = [&]() {
			auto pTask = HttpGet(ENTITY_BG_PREVIEW_MAP_URL);
			pTask->Timeout(CTimeout{10000, 20000, 200, 10});
			pTask->LogProgress(HTTPLOG::FAILURE);
			pTask->FailOnErrorStatus(false);
			WorkshopState.m_pEntityBgPreviewTask = std::move(pTask);
			Http()->Run(WorkshopState.m_pEntityBgPreviewTask);
		};

		// Load from local cache first for instant display
		if(!WorkshopState.m_Requested && WorkshopState.m_vAssets.empty())
		{
			const char *pCacheFile = GetWorkshopCacheFilename(s_CurCustomTab);
			if(pCacheFile && LoadWorkshopCache(WorkshopState, Storage(), pCacheFile))
			{
				WorkshopState.m_CacheTime = Client()->LocalTime();
				WorkshopState.m_Requested = true; // Mark as loaded from cache
				gs_aInitCustomList[s_CurCustomTab] = true;
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					RefreshEntityBgHierarchyView();
				// Don't set m_LastRefreshTime here, so we won't auto-refresh
			}
		}

		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !WorkshopState.m_EntityBgPreviewRequested && !WorkshopState.m_pEntityBgPreviewTask)
		{
			StartEntityBgPreviewTask();
		}

		// Only start HTTP request if no cache exists (first time or after refresh)
		if(pCategory->m_WorkshopEnabled && !WorkshopState.m_Requested && !WorkshopState.m_pListTask && WorkshopState.m_vAssets.empty())
		{
			StartWorkshopListTask();
		}

		if(WorkshopState.m_pListTask && WorkshopState.m_pListTask->Done())
		{
			bool Parsed = false;
			char aError[sizeof(WorkshopState.m_aError)] = "";
			std::vector<SWorkshopHudAsset> vParsedAssets;
			const EHttpState ListTaskState = WorkshopState.m_pListTask->State();
			if(ListTaskState == EHttpState::DONE)
			{
				if(WorkshopState.m_pListTask->StatusCode() == 200)
				{
					json_value *pJson = WorkshopState.m_pListTask->ResultJson();
					if(pJson)
					{
						Parsed = ParseWorkshopAssets(pJson, *pCategory, vParsedAssets, aError, sizeof(aError));
						json_value_free(pJson);
					}
					else
					{
						str_copy(aError, "Workshop json parse failed", sizeof(aError));
					}
				}
				else
				{
					str_format(aError, sizeof(aError), "Workshop request failed (%d)", WorkshopState.m_pListTask->StatusCode());
				}
			}
			else if(ListTaskState == EHttpState::ABORTED)
			{
				str_copy(aError, "Workshop request aborted", sizeof(aError));
			}
			else
			{
				str_copy(aError, "Workshop request failed", sizeof(aError));
			}

			WorkshopState.m_pListTask.reset();
			
			if(Parsed)
			{
				// Incremental update: merge new data with existing data
				// Build a map of existing assets by ID for quick lookup
				std::unordered_map<std::string, size_t> ExistingAssetIndexMap;
				for(size_t i = 0; i < WorkshopState.m_vAssets.size(); ++i)
				{
					ExistingAssetIndexMap[WorkshopState.m_vAssets[i].m_Id] = i;
				}
				
				// Track which existing assets are still present in new data
				std::unordered_set<std::string> NewAssetIds;
				
				for(SWorkshopHudAsset &NewAsset : vParsedAssets)
				{
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
						NormalizeEntityBgWorkshopAsset(NewAsset, Storage());
					NewAsset.m_Installed = Storage()->FileExists(NewAsset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
					NewAssetIds.insert(NewAsset.m_Id);
					
					auto It = ExistingAssetIndexMap.find(NewAsset.m_Id);
					if(It != ExistingAssetIndexMap.end())
					{
						// Update existing asset: preserve texture and tasks
						SWorkshopHudAsset &ExistingAsset = WorkshopState.m_vAssets[It->second];
						NewAsset.m_ThumbTexture = ExistingAsset.m_ThumbTexture;
						ExistingAsset.m_ThumbTexture = IGraphics::CTextureHandle();
						NewAsset.m_ThumbImage = std::move(ExistingAsset.m_ThumbImage);
						NewAsset.m_ThumbBytes = ExistingAsset.m_ThumbBytes;
						NewAsset.m_ThumbResized = ExistingAsset.m_ThumbResized;
						ExistingAsset.m_ThumbBytes = 0;
						ExistingAsset.m_ThumbResized = false;
						NewAsset.m_pDecodeJob = std::move(ExistingAsset.m_pDecodeJob);
						NewAsset.m_pThumbTask = std::move(ExistingAsset.m_pThumbTask);
						NewAsset.m_pDownloadTask = std::move(ExistingAsset.m_pDownloadTask);
						// Replace the existing asset with updated data
						ExistingAsset = std::move(NewAsset);
					}
					else
					{
						// New asset: add to list
						WorkshopState.m_vAssets.push_back(std::move(NewAsset));
					}
				}
				
				// Remove assets that are no longer in the remote list
				for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
				{
					if(NewAssetIds.find(Asset.m_Id) == NewAssetIds.end())
						ResetWorkshopAssetRuntimeState(Asset, Graphics(), true);
				}
				WorkshopState.m_vAssets.erase(
					std::remove_if(WorkshopState.m_vAssets.begin(), WorkshopState.m_vAssets.end(),
						[&NewAssetIds](const SWorkshopHudAsset &Asset) {
							return NewAssetIds.find(Asset.m_Id) == NewAssetIds.end();
						}),
					WorkshopState.m_vAssets.end()
				);

				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					ApplyEntityBgPreviewThumbUrls(WorkshopState);
				PruneWorkshopReadyThumbQueue(WorkshopState);
				
				WorkshopState.m_Requested = true;
				WorkshopState.m_LastRefreshTime = Client()->LocalTime();
				gs_aInitCustomList[s_CurCustomTab] = true;
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					RefreshEntityBgHierarchyView();
				
				// Save to local cache
				const char *pCacheFile = GetWorkshopCacheFilename(s_CurCustomTab);
				if(pCacheFile)
					SaveWorkshopCache(WorkshopState, Storage(), pCacheFile);
			}
			else
			{
				// Parsing failed, keep existing data
				WorkshopState.m_Requested = true;
				if(WorkshopState.m_vAssets.empty())
				{
					WorkshopState.m_LoadFailed = true;
					str_copy(WorkshopState.m_aError, aError);
				}
			}
		}

		bool RefreshLocalList = false;
		
		constexpr int MaxWorkshopThumbDecodeFinalizesPerFrame = 1;
		constexpr int MaxWorkshopThumbUploadsPerFrame = 1;
		int WorkshopGpuUploadsThisFrame = 0;
		int WorkshopThumbFinalizesThisFrame = 0;
		int DeferredWorkshopThumbs = 0;
		size_t WorkshopThumbUploadedBytesThisFrame = 0;
		
		for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
		{
			if(Asset.m_pDecodeJob && Asset.m_pDecodeJob->IsCompleted())
			{
				if(Asset.m_ThumbTexture.IsValid())
				{
					Asset.m_pDecodeJob.reset();
				}
				else if(WorkshopThumbFinalizesThisFrame < MaxWorkshopThumbDecodeFinalizesPerFrame)
				{
					CPerfTimer DecodeFinalizeBatchTimer;
					bool ResizedPreview = false;
					CFullAsyncImageLoadJob::SResult Result = Asset.m_pDecodeJob->GetResult();
					Asset.m_pDecodeJob.reset();
					if(Result.m_Success && Result.m_Image.m_pData)
					{
						const SPreviewTargetSize TargetSize = ComputePreviewTargetSize(Result.m_Image.m_Width, Result.m_Image.m_Height, WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
						if(TargetSize.m_Resized)
						{
							ResizeImage(Result.m_Image, TargetSize.m_Width, TargetSize.m_Height);
							ResizedPreview = true;
						}
						ResetWorkshopThumbReadyState(Asset);
						Asset.m_ThumbImage = std::move(Result.m_Image);
						Asset.m_ThumbBytes = Asset.m_ThumbImage.DataSize();
						Asset.m_ThumbResized = ResizedPreview;
						QueueWorkshopReadyThumb(WorkshopState, Asset, s_CurCustomTab);
						++WorkshopThumbFinalizesThisFrame;
						char aDecodeExtra[160];
						str_format(aDecodeExtra, sizeof(aDecodeExtra), "tab=%d asset=%s resized=%d finalized=%d w=%u h=%u",
							s_CurCustomTab, Asset.m_Name.c_str(), ResizedPreview ? 1 : 0, WorkshopThumbFinalizesThisFrame,
							(unsigned)Asset.m_ThumbImage.m_Width, (unsigned)Asset.m_ThumbImage.m_Height);
						LogAssetsPerfStage("assets_workshop_thumb_decode_finalize_batch", DecodeFinalizeBatchTimer.ElapsedMs(), false, aDecodeExtra);
					}
					else
					{
						Result.m_Image.Free();
					}
				}
				else
				{
					++DeferredWorkshopThumbs;
				}
			}

			if(Asset.m_pThumbTask && Asset.m_pThumbTask->Done())
			{
				const bool ThumbOk = Asset.m_pThumbTask->State() == EHttpState::DONE && Asset.m_pThumbTask->StatusCode() == 200;
				Asset.m_pThumbTask.reset();
				if(ThumbOk && !Asset.m_ThumbTexture.IsValid() && !Asset.m_pDecodeJob)
				{
					StartBackgroundDecode(Asset, Storage(), Engine());
				}
			}

			if(Asset.m_pDownloadTask && Asset.m_pDownloadTask->Done())
			{
				const bool DownloadOk = Asset.m_pDownloadTask->State() == EHttpState::DONE && Asset.m_pDownloadTask->StatusCode() == 200;
				Asset.m_pDownloadTask.reset();
				Asset.m_DownloadFailed = !DownloadOk;
				if(DownloadOk)
				{
					Asset.m_Installed = true;
					RefreshLocalList = true;
				}
			}
		}
		while(!WorkshopState.m_vReadyThumbQueue.empty() && WorkshopGpuUploadsThisFrame < MaxWorkshopThumbUploadsPerFrame)
		{
			const std::string ReadyAssetId = WorkshopState.m_vReadyThumbQueue.front();
			WorkshopState.m_vReadyThumbQueue.pop_front();
			WorkshopState.m_vReadyThumbQueued.erase(ReadyAssetId);

			SWorkshopHudAsset *pAsset = FindWorkshopAssetById(WorkshopState, ReadyAssetId);
			if(pAsset == nullptr)
				continue;
			if(pAsset->m_ThumbTexture.IsValid())
			{
				ResetWorkshopThumbReadyState(*pAsset);
				continue;
			}
			if(pAsset->m_ThumbImage.m_pData == nullptr)
				continue;

			CPerfTimer UploadBatchTimer;
			const size_t AssetBytes = pAsset->m_ThumbBytes;
			pAsset->m_ThumbTexture = Graphics()->LoadTextureRawMove(pAsset->m_ThumbImage, 0, pAsset->m_Name.c_str());
			WorkshopThumbUploadedBytesThisFrame += AssetBytes;
			++WorkshopGpuUploadsThisFrame;
			char aExtra[192];
			str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s uploads_this_frame=%d bytes=%u bytes_used=%u queue_remaining=%d resized=%d",
				s_CurCustomTab, pAsset->m_Name.c_str(), WorkshopGpuUploadsThisFrame, (unsigned)AssetBytes,
				(unsigned)WorkshopThumbUploadedBytesThisFrame, (int)WorkshopState.m_vReadyThumbQueue.size(), pAsset->m_ThumbResized ? 1 : 0);
			LogAssetsPerfStage("assets_workshop_thumb_upload_batch", UploadBatchTimer.ElapsedMs(), false, aExtra);
			ResetWorkshopThumbReadyState(*pAsset);
		}
		char aWorkshopFinalizeExtra[160];
		str_format(aWorkshopFinalizeExtra, sizeof(aWorkshopFinalizeExtra), "tab=%d finalized=%d deferred=%d ready_queue=%d",
			s_CurCustomTab, WorkshopThumbFinalizesThisFrame, DeferredWorkshopThumbs, (int)WorkshopState.m_vReadyThumbQueue.size());
		LogAssetsPerfStage("assets_workshop_thumb_decode_finalize_total", 0.0, WorkshopThumbFinalizesThisFrame > 0 || DeferredWorkshopThumbs > 0 || !WorkshopState.m_vReadyThumbQueue.empty(), aWorkshopFinalizeExtra);
		if(DeferredWorkshopThumbs > 0)
		{
			char aDeferredExtra[128];
			str_format(aDeferredExtra, sizeof(aDeferredExtra), "tab=%d deferred=%d uploads=%d ready_queue=%d",
				s_CurCustomTab, DeferredWorkshopThumbs, WorkshopGpuUploadsThisFrame, (int)WorkshopState.m_vReadyThumbQueue.size());
			LogAssetsPerfStage("assets_workshop_thumb_decode_deferred", 0.0, true, aDeferredExtra);
		}
		if(RefreshLocalList)
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			{
				for(const SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
				{
					if(!Asset.m_Installed || Asset.m_LocalName.empty())
						continue;
					if(std::find(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), Asset.m_LocalName) == m_vEntityBgSourceNames.end())
						m_vEntityBgSourceNames.push_back(Asset.m_LocalName);
					if(m_vEntityBgSourceKinds.find(Asset.m_LocalName) == m_vEntityBgSourceKinds.end())
						m_vEntityBgSourceKinds.emplace(Asset.m_LocalName, EEntityBgHierarchyEntrySource::WORKSHOP);
				}
				RefreshEntityBgHierarchyView();
			}
			else
			{
				ClearCustomItems(s_CurCustomTab);
			}
		}

		WorkshopHudView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 8.0f);
		WorkshopHudView.Margin(8.0f, &WorkshopHudView);

		CUIRect WorkshopListArea = WorkshopHudView;
		WorkshopListArea.HSplitTop(4.0f, nullptr, &WorkshopListArea);
		const size_t LocalAssetTotalCount = SearchListSize;
		std::vector<size_t> vVisibleLocalAssetIndices;
		vVisibleLocalAssetIndices.reserve(LocalAssetTotalCount);
		for(size_t LocalIndex = 0; LocalIndex < LocalAssetTotalCount; ++LocalIndex)
		{
			if(GetCustomItem(s_CurCustomTab, LocalIndex) != nullptr)
				vVisibleLocalAssetIndices.push_back(LocalIndex);
		}
		const size_t LocalAssetCount = vVisibleLocalAssetIndices.size();

			const size_t AssetCount = WorkshopState.m_vAssets.size();
			static std::array<std::vector<CButtonContainer>, NUMBER_OF_ASSETS_TABS> s_avWorkshopActionButtons;
			auto &vWorkshopActionButtons = s_avWorkshopActionButtons[s_CurCustomTab];
			vWorkshopActionButtons.resize(AssetCount);

		std::vector<size_t> vVisibleDownloadableAssetIndices;
		vVisibleDownloadableAssetIndices.reserve(AssetCount);
		auto ShouldShowEntityBgWorkshopAssetsInCurrentFolder = [&]() {
			return m_ShowWorkshopAssets && IsEntityBgWorkshopFolderOrChild(m_aEntityBgCurrentFolder);
		};
		auto WorkshopAssetMatchesCurrentEntityBgFolder = [&](const SWorkshopHudAsset &Asset) {
			if(!ShouldShowEntityBgWorkshopAssetsInCurrentFolder())
				return false;
			if(IsEntityBgWorkshopFolderPath(m_aEntityBgCurrentFolder))
				return true;
			const int CurrentFolderLength = str_length(m_aEntityBgCurrentFolder);
			return str_startswith(Asset.m_LocalName.c_str(), m_aEntityBgCurrentFolder) && Asset.m_LocalName[CurrentFolderLength] == '/';
		};
		for(size_t AssetIndex = 0; AssetIndex < AssetCount; ++AssetIndex)
		{
			const SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[AssetIndex];
			if(Asset.m_Installed)
				continue;
			if(!m_ShowWorkshopAssets)
				continue;
			if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !WorkshopAssetMatchesCurrentEntityBgFolder(Asset))
				continue;
			if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() &&
				!SearchFilterMatches(s_aFilterInputs[s_CurCustomTab].GetString(), Asset.m_Name.c_str(), Asset.m_Author.c_str()))
				continue;
			vVisibleDownloadableAssetIndices.push_back(AssetIndex);
		}

		const size_t CombinedCount = LocalAssetCount + vVisibleDownloadableAssetIndices.size();
		if(CombinedCount == 0)
		{
			if(WorkshopState.m_pListTask)
			{
				Ui()->DoLabel(&WorkshopListArea, Localize("Loading…"), 12.0f, TEXTALIGN_MC);
			}
			else if(WorkshopState.m_LoadFailed)
			{
				char aText[192];
				str_format(aText, sizeof(aText), "%s: %s", Localize("Failed to load"), WorkshopState.m_aError[0] != '\0' ? WorkshopState.m_aError : "unknown");
				SLabelProperties LabelProps;
				LabelProps.m_MaxWidth = static_cast<int>(WorkshopListArea.w);
				Ui()->DoLabel(&WorkshopListArea, aText, 11.0f, TEXTALIGN_MC, LabelProps);
			}
			else
			{
				Ui()->DoLabel(&WorkshopListArea, Localize("No assets"), 12.0f, TEXTALIGN_MC);
			}
		}
		else
		{
			const int Columns = std::max(1, static_cast<int>(WorkshopListArea.w / (Margin + TextureWidth)));
			static CListBox s_WorkshopAssetsListBox;
			s_WorkshopAssetsListBox.DoStart(TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin, CombinedCount, Columns, 1, -1, &WorkshopListArea, false);

			static std::vector<CButtonContainer> s_vWorkshopLocalDeleteButtons;
			s_vWorkshopLocalDeleteButtons.resize(LocalAssetTotalCount);

			static char s_aWorkshopPendingDeleteName[IO_MAX_PATH_LENGTH] = "";
			static CUi::SConfirmPopupContext s_WorkshopDeleteConfirmPopup;
			static size_t s_PendingDownloadAssetIndex = SIZE_MAX;
			static CUi::SConfirmPopupContext s_WorkshopDownloadConfirmPopup;

			constexpr int MaxThumbStartsPerFrame = 16;
			int ThumbStartsThisFrame = 0;
			int OldCombinedSelected = -1;
			bool DeleteLocalRequested = false;
			char aDeleteLocalName[IO_MAX_PATH_LENGTH] = "";
			bool DownloadRequested = false;
			size_t RequestedDownloadAssetIndex = SIZE_MAX;
			float RequestedDownloadPopupX = 0.0f;
			float RequestedDownloadPopupY = 0.0f;
			bool WorkshopActionTriggered = false;
			CPerfTimer WorkshopCardsTimer;
			int FirstVisibleLocalIndex = -1;
			int LastVisibleLocalIndex = -1;
			auto RenderAssetStatusTag = [&](const CUIRect &TagRect, bool Downloaded) {
				CUIRect StatusRect = TagRect;
				const ColorRGBA TagColor = Downloaded ? ColorRGBA(0.18f, 0.62f, 0.32f, 0.88f) : ColorRGBA(0.52f, 0.52f, 0.58f, 0.82f);
				StatusRect.Draw(TagColor, IGraphics::CORNER_ALL, minimum(StatusRect.h / 2.0f, 6.0f));
				SLabelProperties StatusLabelProps;
				StatusLabelProps.m_MaxWidth = static_cast<int>(StatusRect.w - 2.0f);
				Ui()->DoLabel(&StatusRect, Localize(Downloaded ? "Downloaded" : "Not downloaded"), 7.5f, TEXTALIGN_MC, StatusLabelProps);
			};

			for(size_t ListIndex = 0; ListIndex < CombinedCount; ++ListIndex)
			{
				if(ListIndex < LocalAssetCount)
				{
					const size_t LocalIndex = vVisibleLocalAssetIndices[ListIndex];
					const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, LocalIndex);
					if(pItem == nullptr)
						continue;

					const bool Selected = IsLocalAssetSelected(pItem->m_aName);
					if(Selected)
						OldCombinedSelected = static_cast<int>(ListIndex);

					const CListboxItem Item = s_WorkshopAssetsListBox.DoNextItem(pItem, Selected);
					CUIRect ItemRect = Item.m_Rect;
					ItemRect.Margin(Margin / 2, &ItemRect);
					if(!Item.m_Visible)
						continue;
					if(FirstVisibleLocalIndex < 0)
						FirstVisibleLocalIndex = (int)LocalIndex;
					LastVisibleLocalIndex = (int)LocalIndex;

					const CUIRect CardRect = ItemRect;
					const bool IsEntityBgDirectory = s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<const SCustomEntityBg *>(pItem)->m_IsDirectory;
					const bool HasDeleteButton = !IsEntityBgDirectory && !IsProtectedDefaultAsset(pItem->m_aName);
					const bool ShowLocalOnlyBadge = pCategory->m_LocalOnlyBadge && !pCategory->m_WorkshopEnabled;
					const SAssetCardHeaderLayout HeaderLayout = LayoutAssetCardHeader(CardRect, HasDeleteButton, IsEntityBgDirectory ? nullptr : Localize("Downloaded"), ShowLocalOnlyBadge, false);
					CUIRect TitleRect = HeaderLayout.m_TitleRect;
					SLabelProperties TitleProps;
					TitleProps.m_MaxWidth = static_cast<int>(TitleRect.w);
					const char *pTitle = AssetCardDisplayName(pItem);
					Ui()->DoLabel(&TitleRect, pTitle, 9.0f, TEXTALIGN_ML, TitleProps);
					if(HeaderLayout.m_HasAuthorRow && ShouldShowEntityBgDirectorySubtitle(IsEntityBgDirectory))
					{
						SLabelProperties AuthorProps;
						AuthorProps.m_MaxWidth = static_cast<int>(HeaderLayout.m_AuthorRect.w);
						Ui()->DoLabel(&HeaderLayout.m_AuthorRect, "--", 7.0f, TEXTALIGN_ML, AuthorProps);
					}
					if(HeaderLayout.m_HasStatusTag && !IsEntityBgDirectory)
						RenderAssetStatusTag(HeaderLayout.m_StatusTagRect, true);
					if(HeaderLayout.m_HasLocalOnlyBadge)
						RenderCardBadge(HeaderLayout.m_LocalOnlyBadgeRect, Localize("Local-only"), ColorRGBA(0.46f, 0.41f, 0.20f, 0.88f), 7.5f);
					if(IsEntityBgDirectory)
					{
						CUIRect IconRect = HeaderLayout.m_TextureRect;
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						Ui()->DoLabel(&IconRect, str_comp(pItem->m_aName, "..") == 0 ? FONT_ICON_FOLDER_OPEN : FONT_ICON_FOLDER, 36.0f, TEXTALIGN_MC);
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					}
					else if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview)
					{
						const auto *pEntitiesItem = static_cast<const SCustomEntities *>(pItem);
						IGraphics::CTextureHandle Tex;
						for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT && !Tex.IsValid(); m++)
							Tex = pEntitiesItem->m_aImages[m].m_Texture;
						if(!Tex.IsValid())
							Tex = pItem->m_RenderTexture;
						if(!Tex.IsValid())
						{
							for(const auto &Asset : WorkshopState.m_vAssets)
							{
								if(Asset.m_Name == pItem->m_aName && Asset.m_ThumbTexture.IsValid())
								{
									Tex = Asset.m_ThumbTexture;
									break;
								}
							}
						}

						if(Tex.IsValid())
						{
							static const int COLS = 7, ROWS = 7;
							static const unsigned char aLayout[ROWS][COLS] = {
								{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
								{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
								{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
								{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
								{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
								{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
								{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
							};

							float TileSize = TextureWidth / (float)COLS;
							float OffX = HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2.0f;
							float OffY = HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - ROWS * TileSize) / 2.0f;

							const float kInset = 1.5f / 1024.0f;
							const float kTile = 1.0f / 16.0f;

							Graphics()->WrapClamp();
							Graphics()->TextureSet(Tex);
							Graphics()->QuadsBegin();
							Graphics()->SetColor(1, 1, 1, 1);
							for(int r = 0; r < ROWS; r++)
							{
								for(int c = 0; c < COLS; c++)
								{
									unsigned char Tile = aLayout[r][c];
									if(Tile == 0)
										continue;
									int Tx = Tile % 16;
									int Ty = Tile / 16;
									float U0 = Tx * kTile + kInset;
									float V0 = Ty * kTile + kInset;
									float U1 = U0 + kTile - kInset * 2;
									float V1 = V0 + kTile - kInset * 2;
									Graphics()->QuadsSetSubset(U0, V0, U1, V1);
									IGraphics::CQuadItem Q(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
									Graphics()->QuadsDrawTL(&Q, 1);
								}
							}
							Graphics()->QuadsEnd();
							Graphics()->WrapNormal();
						}
					}
					else
					{
						IGraphics::CTextureHandle Tex = pItem->m_RenderTexture;
						if(!Tex.IsValid())
						{
							for(const auto &Asset : WorkshopState.m_vAssets)
							{
								if(Asset.m_LocalName == pItem->m_aName && Asset.m_ThumbTexture.IsValid())
								{
									Tex = Asset.m_ThumbTexture;
									break;
								}
							}
						}
						if(Tex.IsValid())
						{
							Graphics()->WrapClamp();
							Graphics()->TextureSet(Tex);
							Graphics()->QuadsBegin();
							Graphics()->SetColor(1, 1, 1, 1);
							IGraphics::CQuadItem QuadItem(HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2, HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
							Graphics()->QuadsDrawTL(&QuadItem, 1);
							Graphics()->QuadsEnd();
							Graphics()->WrapNormal();
						}
					}

					if(HasDeleteButton)
					{
						if(Ui()->DoButton_FontIcon(&s_vWorkshopLocalDeleteButtons[LocalIndex], FONT_ICON_TRASH, 0, &HeaderLayout.m_ActionButtonRect, IGraphics::CORNER_ALL))
						{
							DeleteLocalRequested = true;
							str_copy(aDeleteLocalName, pItem->m_aName, sizeof(aDeleteLocalName));
						}
					}
				}
				else
				{
					const size_t DownloadableIndex = ListIndex - LocalAssetCount;
					const size_t AssetIndex = vVisibleDownloadableAssetIndices[DownloadableIndex];
					SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[AssetIndex];

					const CListboxItem Item = s_WorkshopAssetsListBox.DoNextItem(&Asset, false);
					CUIRect ItemRect = Item.m_Rect;
					ItemRect.Margin(Margin / 2, &ItemRect);
					if(!Item.m_Visible)
						continue;

					if(!Asset.m_ThumbTexture.IsValid() && !Asset.m_pThumbTask && !Asset.m_pDecodeJob && ThumbStartsThisFrame < MaxThumbStartsPerFrame)
					{
						if(Asset.m_Installed)
						{
							CPerfTimer ThumbStartTimer;
							StartBackgroundDecode(Asset, Storage(), Engine());
							++ThumbStartsThisFrame;
							char aExtra[160];
							str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s started=%d source=installed",
								s_CurCustomTab, Asset.m_Name.c_str(), ThumbStartsThisFrame);
							LogAssetsPerfStage("assets_workshop_thumb_start_installed", ThumbStartTimer.ElapsedMs(), false, aExtra);
						}
						else
						{
							CPerfTimer ThumbStartTimer;
							Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
							Storage()->CreateFolder("qmclient/workshop", IStorage::TYPE_SAVE);
							Storage()->CreateFolder("qmclient/workshop/thumbs", IStorage::TYPE_SAVE);
							const bool RequiresEntityBgPreviewUrl = s_CurCustomTab == ASSETS_TAB_ENTITY_BG;
							const char *pThumbSourceUrl = Asset.m_ThumbUrl.c_str();
							if(!RequiresEntityBgPreviewUrl && pThumbSourceUrl[0] == '\0')
								pThumbSourceUrl = Asset.m_ImageUrl.c_str();
							if(pThumbSourceUrl[0] == '\0')
								continue;
							char aWebpUrl[IO_MAX_PATH_LENGTH];
							str_format(aWebpUrl, sizeof(aWebpUrl), "%s!/format/webp", pThumbSourceUrl);
							auto pThumbTask = HttpGetFile(aWebpUrl, Storage(), Asset.m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
							pThumbTask->Timeout(CTimeout{8000, 20000, 100, 10});
							pThumbTask->LogProgress(HTTPLOG::FAILURE);
							pThumbTask->FailOnErrorStatus(false);
							pThumbTask->SkipByFileTime(false);
							Asset.m_pThumbTask = std::move(pThumbTask);
							Http()->Run(Asset.m_pThumbTask);
							++ThumbStartsThisFrame;
							char aExtra[160];
							str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s started=%d source=remote",
								s_CurCustomTab, Asset.m_Name.c_str(), ThumbStartsThisFrame);
							LogAssetsPerfStage("assets_workshop_thumb_start_remote", ThumbStartTimer.ElapsedMs(), false, aExtra);
						}
					}

					const CUIRect CardRect = ItemRect;
					const bool Downloading = Asset.m_pDownloadTask && !Asset.m_pDownloadTask->Done();
					const bool ShowAuthorRow = ShouldShowAssetCardAuthorRow(!Asset.m_Author.empty(), false);
					const SAssetCardHeaderLayout HeaderLayout = LayoutAssetCardHeader(CardRect, true, Localize(Asset.m_Installed ? "Downloaded" : "Not downloaded"), false, ShowAuthorRow);
					CUIRect TitleRect = HeaderLayout.m_TitleRect;
					SLabelProperties TitleProps;
					TitleProps.m_MaxWidth = static_cast<int>(TitleRect.w);
					Ui()->DoLabel(&TitleRect, Asset.m_Name.c_str(), 9.0f, TEXTALIGN_ML, TitleProps);
					if(HeaderLayout.m_HasAuthorRow)
					{
						SLabelProperties AuthorProps;
						AuthorProps.m_MaxWidth = static_cast<int>(HeaderLayout.m_AuthorRect.w);
						Ui()->DoLabel(&HeaderLayout.m_AuthorRect, Asset.m_Author.empty() ? "--" : Asset.m_Author.c_str(), 7.0f, TEXTALIGN_ML, AuthorProps);
					}
					if(HeaderLayout.m_HasStatusTag)
						RenderAssetStatusTag(HeaderLayout.m_StatusTagRect, Asset.m_Installed);

					if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview && Asset.m_ThumbTexture.IsValid())
					{
						static const int COLS = 7, ROWS = 7;
						static const unsigned char aLayout[ROWS][COLS] = {
							{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
							{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
							{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
							{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
							{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
							{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
							{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
						};

						float TileSize = TextureWidth / (float)COLS;
						float OffX = HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2.0f;
						float OffY = HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - ROWS * TileSize) / 2.0f;

						const float kInset = 1.5f / 1024.0f;
						const float kTile = 1.0f / 16.0f;

						Graphics()->WrapClamp();
						Graphics()->TextureSet(Asset.m_ThumbTexture);
						Graphics()->QuadsBegin();
						Graphics()->SetColor(1, 1, 1, 1);
						for(int r = 0; r < ROWS; r++)
						{
							for(int c = 0; c < COLS; c++)
							{
								unsigned char Tile = aLayout[r][c];
								if(Tile == 0)
									continue;
								int Tx = Tile % 16;
								int Ty = Tile / 16;
								float U0 = Tx * kTile + kInset;
								float V0 = Ty * kTile + kInset;
								float U1 = U0 + kTile - kInset * 2;
								float V1 = V0 + kTile - kInset * 2;
								Graphics()->QuadsSetSubset(U0, V0, U1, V1);
								IGraphics::CQuadItem Q(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
								Graphics()->QuadsDrawTL(&Q, 1);
							}
						}
						Graphics()->QuadsEnd();
						Graphics()->WrapNormal();
					}
					else if(Asset.m_ThumbTexture.IsValid())
					{
						Graphics()->WrapClamp();
						Graphics()->TextureSet(Asset.m_ThumbTexture);
						Graphics()->QuadsBegin();
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
						IGraphics::CQuadItem QuadItem(HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2, HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
						Graphics()->WrapNormal();
					}
					else
					{
						CUIRect LoadingRect = {HeaderLayout.m_TextureRect.x + (HeaderLayout.m_TextureRect.w - TextureWidth) / 2, HeaderLayout.m_TextureRect.y + (HeaderLayout.m_TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight};
						LoadingRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.10f), IGraphics::CORNER_ALL, 6.0f);
						Ui()->DoLabel(&LoadingRect, Localize("Loading…"), 10.0f, TEXTALIGN_MC);
					}
					const char *pActionIcon = Downloading ? FONT_ICON_ARROW_ROTATE_RIGHT : FONT_ICON_CIRCLE_CHEVRON_DOWN;
					if(Ui()->DoButton_FontIcon(&vWorkshopActionButtons[AssetIndex], pActionIcon, 0, &HeaderLayout.m_ActionButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Downloading))
					{
						DownloadRequested = true;
						RequestedDownloadAssetIndex = AssetIndex;
						RequestedDownloadPopupX = HeaderLayout.m_ActionButtonRect.x + HeaderLayout.m_ActionButtonRect.w;
						RequestedDownloadPopupY = HeaderLayout.m_ActionButtonRect.y + HeaderLayout.m_ActionButtonRect.h + 4.0f;
						WorkshopActionTriggered = true;
					}

					if(Asset.m_DownloadFailed)
					{
						CUIRect ErrorRect = CardRect;
						ErrorRect.HSplitBottom(14.0f, nullptr, &ErrorRect);
						Ui()->DoLabel(&ErrorRect, Localize("Download failed"), 9.0f, TEXTALIGN_MC);
					}
				}
			}
			if(FirstVisibleLocalIndex >= 0)
			{
				SchedulePreviewRange(FirstVisibleLocalIndex, LastVisibleLocalIndex, Columns);
				char aVisibleExtra[160];
				str_format(aVisibleExtra, sizeof(aVisibleExtra), "tab=%d first=%d last=%d starts=%d",
					s_CurCustomTab, FirstVisibleLocalIndex, LastVisibleLocalIndex, PreviewDecodeStartsThisFrame);
				LogAssetsPerfStage("assets_preview_decode_start_visible", 0.0, PreviewDecodeStartsThisFrame > 0, aVisibleExtra);
			}
			char aExtra[160];
			str_format(aExtra, sizeof(aExtra), "tab=%d combined=%d local_visible=%d remote_visible=%d thumb_starts=%d",
				s_CurCustomTab, (int)CombinedCount, (int)LocalAssetCount, (int)vVisibleDownloadableAssetIndices.size(), ThumbStartsThisFrame);
			LogAssetsPerfStage("assets_preview_draw_workshop_cards", WorkshopCardsTimer.ElapsedMs(), false, aExtra);

			const int NewCombinedSelected = s_WorkshopAssetsListBox.DoEnd();
			if(DeleteLocalRequested)
			{
				str_copy(s_aWorkshopPendingDeleteName, aDeleteLocalName, sizeof(s_aWorkshopPendingDeleteName));
				s_WorkshopDeleteConfirmPopup.Reset();
				s_WorkshopDeleteConfirmPopup.YesNoButtons();
				str_copy(s_WorkshopDeleteConfirmPopup.m_aMessage, Localize("Are you sure you want to delete this asset?"));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_WorkshopDeleteConfirmPopup);
			}
			if(DownloadRequested && RequestedDownloadAssetIndex < WorkshopState.m_vAssets.size())
			{
				s_PendingDownloadAssetIndex = RequestedDownloadAssetIndex;
				s_WorkshopDownloadConfirmPopup.Reset();
				s_WorkshopDownloadConfirmPopup.YesNoButtons();
				str_copy(s_WorkshopDownloadConfirmPopup.m_aMessage, Localize("Download this asset?"));
				Ui()->ShowPopupConfirm(RequestedDownloadPopupX, RequestedDownloadPopupY, &s_WorkshopDownloadConfirmPopup);
			}

			if(s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
			{
				if(DeleteLocalAssetByTab(Storage(), s_CurCustomTab, s_aWorkshopPendingDeleteName))
				{
					if(IsLocalAssetSelected(s_aWorkshopPendingDeleteName))
						ApplyLocalAssetSelection("default");
					for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
					{
						if(str_comp(Asset.m_LocalName.c_str(), s_aWorkshopPendingDeleteName) == 0)
						{
							Asset.m_Installed = false;
							break;
						}
					}
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					{
						const auto EntryIt = std::find(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), std::string(s_aWorkshopPendingDeleteName));
						if(EntryIt != m_vEntityBgSourceNames.end())
							m_vEntityBgSourceNames.erase(EntryIt);
						const auto SourceIt = m_vEntityBgSourceKinds.find(s_aWorkshopPendingDeleteName);
						if(SourceIt != m_vEntityBgSourceKinds.end() && SourceIt->second == EEntityBgHierarchyEntrySource::WORKSHOP)
							m_vEntityBgSourceKinds.erase(SourceIt);
						RefreshEntityBgHierarchyView();
					}
					else
					{
						ClearCustomItems(s_CurCustomTab);
					}
				}
				else
				{
					dbg_msg("assets", "failed to delete local asset '%s' in tab %d", s_aWorkshopPendingDeleteName, s_CurCustomTab);
				}
				s_WorkshopDeleteConfirmPopup.Reset();
				s_aWorkshopPendingDeleteName[0] = '\0';
			}
			else if(s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
			{
				s_WorkshopDeleteConfirmPopup.Reset();
				s_aWorkshopPendingDeleteName[0] = '\0';
			}

			if(s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED && s_PendingDownloadAssetIndex < WorkshopState.m_vAssets.size())
			{
				SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[s_PendingDownloadAssetIndex];
				Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
				Storage()->CreateFolder(pInstallFolder, IStorage::TYPE_SAVE);

				auto pDownloadTask = HttpGetFile(Asset.m_ImageUrl.c_str(), Storage(), Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
				pDownloadTask->Timeout(CTimeout{10000, 30000, 100, 10});
				pDownloadTask->LogProgress(HTTPLOG::FAILURE);
				pDownloadTask->FailOnErrorStatus(false);
				pDownloadTask->SkipByFileTime(false);
				Asset.m_pDownloadTask = std::move(pDownloadTask);
				Asset.m_DownloadFailed = false;
				Http()->Run(Asset.m_pDownloadTask);
				WorkshopActionTriggered = true;
				s_WorkshopDownloadConfirmPopup.Reset();
				s_PendingDownloadAssetIndex = SIZE_MAX;
			}
			else if(s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
			{
				s_WorkshopDownloadConfirmPopup.Reset();
				s_PendingDownloadAssetIndex = SIZE_MAX;
			}

			if(!DeleteLocalRequested && s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && !WorkshopActionTriggered && s_WorkshopAssetsListBox.WasItemSelected() && NewCombinedSelected >= 0 && NewCombinedSelected != OldCombinedSelected && static_cast<size_t>(NewCombinedSelected) < LocalAssetCount)
			{
				const size_t LocalIndex = vVisibleLocalAssetIndices[static_cast<size_t>(NewCombinedSelected)];
				const SCustomItem *pNewItem = GetCustomItem(s_CurCustomTab, LocalIndex);
				if(pNewItem && pNewItem->m_aName[0] != '\0')
				{
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<const SCustomEntityBg *>(pNewItem)->m_IsDirectory)
					{
						if(str_comp(pNewItem->m_aName, "..") == 0)
						{
							char aParentFolder[IO_MAX_PATH_LENGTH];
							BuildEntityBgParentFolder(m_aEntityBgCurrentFolder, aParentFolder, sizeof(aParentFolder));
							str_copy(m_aEntityBgCurrentFolder, aParentFolder, sizeof(m_aEntityBgCurrentFolder));
						}
						else
						{
							str_copy(m_aEntityBgCurrentFolder, pNewItem->m_aName, sizeof(m_aEntityBgCurrentFolder));
						}
						RefreshEntityBgHierarchyView();
					}
					else
					{
						ApplyLocalAssetSelection(pNewItem->m_aName);
					}
				}
			}
		}

		if(WorkshopState.m_pEntityBgPreviewTask && WorkshopState.m_pEntityBgPreviewTask->Done())
		{
			char aPreviewError[sizeof(WorkshopState.m_aError)] = "";
			const EHttpState PreviewTaskState = WorkshopState.m_pEntityBgPreviewTask->State();
			if(PreviewTaskState == EHttpState::DONE && WorkshopState.m_pEntityBgPreviewTask->StatusCode() == 200)
			{
				if(json_value *pJson = WorkshopState.m_pEntityBgPreviewTask->ResultJson())
				{
					if(ParseEntityBgPreviewMap(pJson, WorkshopState.m_vEntityBgPreviewExtByName, WorkshopState.m_EntityBgPreviewBaseUrl, aPreviewError, sizeof(aPreviewError)))
						ApplyEntityBgPreviewThumbUrls(WorkshopState);
					json_value_free(pJson);
				}
				else
				{
					str_copy(aPreviewError, "Entity bg preview json parse failed", sizeof(aPreviewError));
				}
			}
			else if(PreviewTaskState != EHttpState::DONE)
			{
				str_copy(aPreviewError, "Entity bg preview request failed", sizeof(aPreviewError));
			}
			else
			{
				str_format(aPreviewError, sizeof(aPreviewError), "Entity bg preview request failed (%d)", WorkshopState.m_pEntityBgPreviewTask->StatusCode());
			}

			WorkshopState.m_pEntityBgPreviewTask.reset();
			WorkshopState.m_EntityBgPreviewRequested = true;
			if(aPreviewError[0] != '\0' && WorkshopState.m_vEntityBgPreviewExtByName.empty())
				dbg_msg("assets", "%s", aPreviewError);
		}
	}

	// Quick search - 底部按钮栏布局
	MainView.HSplitBottom(ms_ButtonHeight, &MainView, &QuickSearch);
	CUIRect AssetsEditorButton;
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}

	// 从右往左切分按钮
	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);
	auto ComputeToolbarButtonWidth = [&](const char *pLabel) {
		constexpr float MinButtonWidth = 90.0f;
		constexpr float HorizontalPadding = 18.0f;
		return maximum(MinButtonWidth, TextRender()->TextWidth(10.0f, Localize(pLabel), -1, -1.0f) + HorizontalPadding);
	};
	const bool SupportsWorkshopSync = pCurrentCategory != nullptr && pCurrentCategory->m_WorkshopEnabled;
	const bool SupportsAssetsEditor = s_CurCustomTab == ASSETS_TAB_ENTITIES || s_CurCustomTab == ASSETS_TAB_GAME ||
		s_CurCustomTab == ASSETS_TAB_EMOTICONS || s_CurCustomTab == ASSETS_TAB_PARTICLES ||
		s_CurCustomTab == ASSETS_TAB_HUD || s_CurCustomTab == ASSETS_TAB_GUI_CURSOR ||
		s_CurCustomTab == ASSETS_TAB_ARROW || s_CurCustomTab == ASSETS_TAB_STRONG_WEAK ||
		s_CurCustomTab == ASSETS_TAB_EXTRAS;
	const bool ShowEntityPreviewToggle = s_CurCustomTab == ASSETS_TAB_ENTITIES;
	const float AssetsDirButtonWidth = ComputeToolbarButtonWidth("Assets directory");
	const float AssetsEditorButtonWidth = ComputeToolbarButtonWidth("Assets editor");
	const float EntityPreviewButtonWidth = ComputeToolbarButtonWidth("Entity Preview");
	const float ShowWorkshopAssetsButtonWidth = ComputeToolbarButtonWidth("Show Workshop Assets");
	const float WorkshopSyncButtonWidth = ComputeToolbarButtonWidth("Sync Workshop Assets");
	float ToolbarWidth = 25.0f + 10.0f + AssetsDirButtonWidth;
	if(ShowEntityPreviewToggle)
		ToolbarWidth += 10.0f + EntityPreviewButtonWidth;
	if(SupportsAssetsEditor)
		ToolbarWidth += 10.0f + AssetsEditorButtonWidth;
	if(SupportsWorkshopSync)
		ToolbarWidth += 10.0f + WorkshopSyncButtonWidth + 10.0f + ShowWorkshopAssetsButtonWidth;
	DirectoryButton.VSplitRight(ToolbarWidth, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &ReloadButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	CUIRect ShowWorkshopAssetsButton;
	CUIRect WorkshopSyncButton;
	if(SupportsWorkshopSync)
	{
		DirectoryButton.VSplitRight(ShowWorkshopAssetsButtonWidth, &DirectoryButton, &ShowWorkshopAssetsButton);
		DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
		DirectoryButton.VSplitRight(WorkshopSyncButtonWidth, &DirectoryButton, &WorkshopSyncButton);
		DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	}
	static CButtonContainer s_AssetsEditorButton;
	if(SupportsAssetsEditor)
	{
		DirectoryButton.VSplitRight(AssetsEditorButtonWidth, &DirectoryButton, &AssetsEditorButton);
		DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
		if(DoButton_Menu(&s_AssetsEditorButton, Localize("Assets editor"), 0, &AssetsEditorButton))
		{
			int AssetsEditorType = ASSETS_EDITOR_TYPE_GAME;
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
				AssetsEditorType = ASSETS_EDITOR_TYPE_ENTITIES;
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				AssetsEditorType = ASSETS_EDITOR_TYPE_EMOTICONS;
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				AssetsEditorType = ASSETS_EDITOR_TYPE_PARTICLES;
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
				AssetsEditorType = ASSETS_EDITOR_TYPE_HUD;
			else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
				AssetsEditorType = ASSETS_EDITOR_TYPE_GUI_CURSOR;
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
				AssetsEditorType = ASSETS_EDITOR_TYPE_ARROW;
			else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
				AssetsEditorType = ASSETS_EDITOR_TYPE_STRONG_WEAK;
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
				AssetsEditorType = ASSETS_EDITOR_TYPE_EXTRAS;
			AssetsEditorOpen(AssetsEditorType);
			return;
		}
	}

	CUIRect AssetsDirButton;
	DirectoryButton.VSplitRight(AssetsDirButtonWidth, &DirectoryButton, &AssetsDirButton);

	// Entity Preview 按钮（仅实体层标签页显示）
	if(ShowEntityPreviewToggle)
	{
		CUIRect ToggleRect;
		DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
		DirectoryButton.VSplitRight(EntityPreviewButtonWidth, &DirectoryButton, &ToggleRect);
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoButton_Menu(&s_EntityPreviewToggleId, Localize("Entity Preview"), s_EntityGamePreview, &ToggleRect))
			s_EntityGamePreview = !s_EntityGamePreview;
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &ToggleRect, Localize("Toggle between game scene preview and raw texture"));
	}

	static CButtonContainer s_AssetsDirId;
	static CButtonContainer s_ShowWorkshopAssetsId;
	static CButtonContainer s_WorkshopSyncId;
	if(SupportsWorkshopSync && DoButton_Menu(&s_ShowWorkshopAssetsId, Localize("Show Workshop Assets"), m_ShowWorkshopAssets, &ShowWorkshopAssetsButton))
	{
		m_ShowWorkshopAssets = !m_ShowWorkshopAssets;
		gs_aInitCustomList[s_CurCustomTab] = true;
		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			RefreshEntityBgHierarchyView();
	}
	if(SupportsWorkshopSync)
		GameClient()->m_Tooltips.DoToolTip(&s_ShowWorkshopAssetsId, &ShowWorkshopAssetsButton, Localize("Show Workshop Assets"));

	if(SupportsWorkshopSync && DoButton_Menu(&s_WorkshopSyncId, Localize("Sync Workshop Assets"), 0, &WorkshopSyncButton))
	{
		if(SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab))
			ResetWorkshopState(*pWorkshopState, Graphics(), true);
	}
	if(SupportsWorkshopSync)
		GameClient()->m_Tooltips.DoToolTip(&s_WorkshopSyncId, &WorkshopSyncButton, Localize("Sync Workshop Assets"));

	if(DoButton_Menu(&s_AssetsDirId, Localize("Assets directory"), 0, &AssetsDirButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		if(pCategory != nullptr)
			str_copy(aBufFull, pCategory->m_pInstallFolder);
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &AssetsDirButton, Localize("Open the directory to add custom assets"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_AssetsReloadBtnId;
	if(DoButton_Menu(&s_AssetsReloadBtnId, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
		if(SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab))
			ResetWorkshopState(*pWorkshopState, Graphics(), true);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(TransitionActive && TransitionAlpha > 0.0f)
	{
		ContentClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha), IGraphics::CORNER_NONE, 0.0f);
	}
	if(TransitionActive)
	{
		Ui()->ClipDisable();
	}
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}
