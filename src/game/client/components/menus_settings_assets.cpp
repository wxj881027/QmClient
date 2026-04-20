#include "menus.h"

#include <algorithm>

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

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_set>

using namespace FontIcons;
using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

class CMenus::CAssetDecodeJob : public IJob
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
	std::mutex m_Mutex;
	SResult m_Result;
	bool m_Completed = false;

	void Run() override
	{
		if(m_vFileData.empty())
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
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
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = Success;
			m_Completed = true;
		}

		m_vFileData.clear();
		m_vFileData.shrink_to_fit();
	}

public:
	CAssetDecodeJob(std::vector<uint8_t> &&vFileData, const char *pName) :
		m_vFileData(std::move(vFileData)),
		m_Name(pName)
	{
	}

	bool IsCompleted() const
	{
		std::lock_guard<std::mutex> Lock(const_cast<std::mutex &>(m_Mutex));
		return m_Completed;
	}

	SResult GetResult()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

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
	std::mutex m_Mutex;
	SResult m_Result;
	bool m_Completed = false;

	void Run() override
	{
		if(m_vFileData.empty())
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
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
			std::lock_guard<std::mutex> Lock(m_Mutex);
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

	bool IsCompleted() const
	{
		std::lock_guard<std::mutex> Lock(const_cast<std::mutex &>(m_Mutex));
		return m_Completed;
	}

	SResult GetResult()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
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

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	NUMBER_OF_ASSETS_TABS = 6,
};

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

	std::vector<uint8_t> vFileData;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			if(LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData))
				break;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			if(LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData))
				break;
		}
		if(vFileData.empty())
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
			LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData);
		}
	}

	if(!vFileData.empty())
	{
		pEntitiesItem->m_pDecodeJob = std::make_shared<CMenus::CAssetDecodeJob>(std::move(vFileData), pEntitiesItem->m_aName);
		pEngine->AddJob(pEntitiesItem->m_pDecodeJob);
	}
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

	std::vector<uint8_t> vFileData;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData);
		if(vFileData.empty())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			LoadFileToBuffer(pStorage, aPath, IStorage::TYPE_ALL, vFileData);
		}
	}

	if(!vFileData.empty())
	{
		pAssetItem->m_pDecodeJob = std::make_shared<CMenus::CAssetDecodeJob>(std::move(vFileData), pAssetItem->m_aName);
		pEngine->AddJob(pAssetItem->m_pDecodeJob);
	}
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
static std::vector<CMenus::SCustomExtras *> gs_vpSearchExtrasList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true, // ASSETS_TAB_ENTITIES
	true, // ASSETS_TAB_GAME
	true, // ASSETS_TAB_EMOTICONS
	true, // ASSETS_TAB_PARTICLES
	true, // ASSETS_TAB_HUD
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
constexpr const char *WORKSHOP_HUD_ASSETS_URL = WORKSHOP_ASSETS_URL;
constexpr const char *WORKSHOP_HUD_CATEGORY = "HUD";
constexpr const char *WORKSHOP_ENTITIES_ASSETS_URL = WORKSHOP_ASSETS_URL;
constexpr const char *WORKSHOP_ENTITIES_CATEGORY = "实体层";
constexpr const char *WORKSHOP_ENTITIES_CATEGORY_ALT = "实体（ENTITIES）";
constexpr const char *WORKSHOP_GAME_ASSETS_URL = WORKSHOP_ASSETS_URL;
constexpr const char *WORKSHOP_GAME_CATEGORY = "游戏";
constexpr const char *WORKSHOP_GAME_CATEGORY_ALT = "游戏（GAME）";
constexpr const char *WORKSHOP_EMOTICONS_ASSETS_URL = WORKSHOP_ASSETS_URL;
constexpr const char *WORKSHOP_EMOTICONS_CATEGORY = "表情";
constexpr const char *WORKSHOP_EMOTICONS_CATEGORY_ALT = "表情（EMOTICONS）";
constexpr const char *WORKSHOP_PARTICLES_ASSETS_URL = WORKSHOP_ASSETS_URL;
constexpr const char *WORKSHOP_PARTICLES_CATEGORY = "粒子";
constexpr const char *WORKSHOP_PARTICLES_CATEGORY_ALT = "粒子（PARTICLES）";

struct SWorkshopHudAsset
{
	std::string m_Id;
	std::string m_Name;
	std::string m_Author;
	std::string m_LocalName;
	std::string m_ImageUrl;
	std::string m_ThumbCachePath;
	std::string m_InstallPath;
	IGraphics::CTextureHandle m_ThumbTexture;
	std::shared_ptr<CHttpRequest> m_pThumbTask;
	std::shared_ptr<CHttpRequest> m_pDownloadTask;
	bool m_DownloadFailed = false;
	bool m_Installed = false;
	
	std::shared_ptr<CImageDecodeJob> m_pDecodeJob;
};

struct SWorkshopHudState
{
	std::vector<SWorkshopHudAsset> m_vAssets;
	std::shared_ptr<CHttpRequest> m_pListTask;
	bool m_Requested = false;
	bool m_LoadFailed = false;
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

static SWorkshopHudState gs_WorkshopHudState;
static SWorkshopHudState gs_WorkshopEntitiesState;
static SWorkshopHudState gs_WorkshopGameState;
static SWorkshopHudState gs_WorkshopEmoticonsState;
static SWorkshopHudState gs_WorkshopParticlesState;

} // namespace

namespace
{

static void StartBackgroundDecode(SWorkshopHudAsset &Asset, IStorage *pStorage, IEngine *pEngine)
{
	if(Asset.m_pDecodeJob || Asset.m_ThumbTexture.IsValid())
		return;

	std::vector<uint8_t> vFileData;

	if(Asset.m_Installed && !Asset.m_InstallPath.empty())
	{
		LoadFileToBuffer(pStorage, Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE, vFileData);
	}

	if(vFileData.empty() && !Asset.m_ThumbCachePath.empty())
	{
		LoadFileToBuffer(pStorage, Asset.m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE, vFileData);
	}

	if(!vFileData.empty())
	{
		Asset.m_pDecodeJob = std::make_shared<CImageDecodeJob>(std::move(vFileData), Asset.m_Name.c_str());
		pEngine->AddJob(Asset.m_pDecodeJob);
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

bool WorkshopCategoryMatches(const char *pCategoryValue, const char *pCategoryFilter, const char *pCategoryFilterAlt, const char *pInstallFolder)
{
	if(!pCategoryValue || pCategoryValue[0] == '\0')
		return false;

	if((pCategoryFilter && pCategoryFilter[0] != '\0' && str_comp(pCategoryValue, pCategoryFilter) == 0) ||
		(pCategoryFilterAlt && pCategoryFilterAlt[0] != '\0' && str_comp(pCategoryValue, pCategoryFilterAlt) == 0))
	{
		return true;
	}

	if(!pInstallFolder || pInstallFolder[0] == '\0')
		return false;

	if(str_comp(pInstallFolder, "hud") == 0)
		return str_comp(pCategoryValue, "HUD") == 0 || str_comp(pCategoryValue, "界面（HUD）") == 0;
	if(str_comp(pInstallFolder, "entities") == 0)
		return str_comp(pCategoryValue, "实体层") == 0 ||
			str_comp(pCategoryValue, "实体（Entities）") == 0 ||
			str_comp(pCategoryValue, "实体（ENTITIES）") == 0;
	if(str_comp(pInstallFolder, "game") == 0)
		return str_comp(pCategoryValue, "游戏") == 0 ||
			str_comp(pCategoryValue, "游戏（Game）") == 0 ||
			str_comp(pCategoryValue, "游戏（GAME）") == 0;
	if(str_comp(pInstallFolder, "emoticons") == 0)
		return str_comp(pCategoryValue, "表情") == 0 ||
			str_comp(pCategoryValue, "表情（Emoticons）") == 0 ||
			str_comp(pCategoryValue, "表情（EMOTICONS）") == 0;
	if(str_comp(pInstallFolder, "particles") == 0)
		return str_comp(pCategoryValue, "粒子") == 0 ||
			str_comp(pCategoryValue, "粒子（Particles）") == 0 ||
			str_comp(pCategoryValue, "粒子（PARTICLES）") == 0;

	return false;
}

bool ParseWorkshopAssets(const json_value *pRoot, const char *pCategoryFilter, const char *pCategoryFilterAlt, const char *pInstallFolder, std::vector<SWorkshopHudAsset> &vOut, char *pErr, int ErrSize)
{
	vOut.clear();
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
		if(!WorkshopCategoryMatches(pCategoryValue, pCategoryFilter, pCategoryFilterAlt, pInstallFolder))
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
		Asset.m_Author = pAuthor != &json_value_none && pAuthor->type == json_string ? json_string_get(pAuthor) : "";
		Asset.m_ImageUrl = NormalizeWorkshopAssetUrl(json_string_get(pImage));
		if(Asset.m_ImageUrl.empty())
			continue;

		char aExt[16];
		GuessUrlExtension(Asset.m_ImageUrl.c_str(), aExt, sizeof(aExt));

		const std::string SafeInstallName = BuildSafeFilename(Asset.m_Name.c_str(), Asset.m_Id.c_str(), aExt);
		const size_t DotPos = SafeInstallName.find_last_of('.');
		Asset.m_LocalName = DotPos == std::string::npos ? SafeInstallName : SafeInstallName.substr(0, DotPos);
		char aInstallPath[IO_MAX_PATH_LENGTH];
		str_format(aInstallPath, sizeof(aInstallPath), "assets/%s/%s", pInstallFolder, SafeInstallName.c_str());
		Asset.m_InstallPath = aInstallPath;

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
	WorkshopState.m_pListTask.reset();

	for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
	{
		if(AbortTasks && Asset.m_pThumbTask)
			Asset.m_pThumbTask->Abort();
		if(AbortTasks && Asset.m_pDownloadTask)
			Asset.m_pDownloadTask->Abort();
		Asset.m_pThumbTask.reset();
		Asset.m_pDownloadTask.reset();
		pGraphics->UnloadTexture(&Asset.m_ThumbTexture);
	}

	WorkshopState.m_vAssets.clear();
	WorkshopState.m_Requested = false;
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
				Asset.m_Author = pVal->u.string.ptr;
			else if(str_comp(pKey, "image_url") == 0 && pVal->type == json_string)
				Asset.m_ImageUrl = pVal->u.string.ptr;
			else if(str_comp(pKey, "thumb_cache") == 0 && pVal->type == json_string)
				Asset.m_ThumbCachePath = pVal->u.string.ptr;
			else if(str_comp(pKey, "install_path") == 0 && pVal->type == json_string)
				Asset.m_InstallPath = pVal->u.string.ptr;
		}
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
	default: return nullptr;
	}
}

bool DeleteLocalAssetByTab(IStorage *pStorage, int CurTab, const char *pAssetName)
{
	const char *pSubFolder = nullptr;
	switch(CurTab)
	{
	case ASSETS_TAB_ENTITIES: pSubFolder = "entities"; break;
	case ASSETS_TAB_GAME: pSubFolder = "game"; break;
	case ASSETS_TAB_EMOTICONS: pSubFolder = "emoticons"; break;
	case ASSETS_TAB_PARTICLES: pSubFolder = "particles"; break;
	case ASSETS_TAB_HUD: pSubFolder = "hud"; break;
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
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

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
	for(int i = 0; i < ListSize; ++i)
	{
		TName *pAsset = &vAssetList[i];

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
			continue;

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
		s_apAssetsTabNames[ASSETS_TAB_EXTRAS] = Localize("Extras");
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

	auto LoadStartTime = time_get_nanoseconds();
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = [&]() {
		if(time_get_nanoseconds() - LoadStartTime > 500ms)
			RenderLoading(Localize("Loading assets"), "", 0);
	};
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		InitAssetList(m_vGameList, "assets/game", GameScan, Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		InitAssetList(m_vEmoticonList, "assets/emoticons", EmoticonsScan, Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		InitAssetList(m_vParticlesList, "assets/particles", ParticlesScan, Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		InitAssetList(m_vHudList, "assets/hud", HudScan, Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		InitAssetList(m_vExtrasList, "assets/extras", ExtrasScan, Storage(), &User);
	}
	else
	{
		dbg_assert_failed("Invalid s_CurCustomTab: %d", s_CurCustomTab);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);
	if(s_CurCustomTab == ASSETS_TAB_HUD || s_CurCustomTab == ASSETS_TAB_ENTITIES || s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_EMOTICONS || s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		WorkshopHudView = CustomList;
	}
	if(gs_aInitCustomList[s_CurCustomTab])
	{
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
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	float Margin = 10;
	float TextureWidth = 150;
	float TextureHeight = 150;
	SMenuAssetScanUser LazyLoadUser;
	LazyLoadUser.m_pUser = this;
	constexpr int MaxGpuUploadsPerFrame = 30;
	int GpuUploadsThisFrame = 0;

	auto UploadCompletedDecodeJob = [&](SCustomItem *pItem) {
		if(!pItem->m_pDecodeJob || !pItem->m_pDecodeJob->IsCompleted())
			return;
		if(GpuUploadsThisFrame >= MaxGpuUploadsPerFrame)
			return;
		if(pItem->m_RenderTexture.IsValid())
		{
			pItem->m_pDecodeJob.reset();
			return;
		}
		CMenus::CAssetDecodeJob::SResult Result = pItem->m_pDecodeJob->GetResult();
		pItem->m_pDecodeJob.reset();
		if(Result.m_Success && Result.m_Image.m_pData)
		{
			constexpr int MaxPreviewSize = 512;
			if(Result.m_Image.m_Width > MaxPreviewSize || Result.m_Image.m_Height > MaxPreviewSize)
			{
				int NewWidth = Result.m_Image.m_Width;
				int NewHeight = Result.m_Image.m_Height;
				if(NewWidth > NewHeight)
				{
					NewHeight = (int)((float)NewHeight * MaxPreviewSize / NewWidth);
					NewWidth = MaxPreviewSize;
				}
				else
				{
					NewWidth = (int)((float)NewWidth * MaxPreviewSize / NewHeight);
					NewHeight = MaxPreviewSize;
				}
				ResizeImage(Result.m_Image, NewWidth, NewHeight);
			}
			pItem->m_RenderTexture = Graphics()->LoadTextureRaw(Result.m_Image, 0, pItem->m_aName);
			Result.m_Image.Free();
			++GpuUploadsThisFrame;
		}
	};

	auto StartPreviewDecode = [&](size_t Index) {
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			SCustomEntities *pEntity = gs_vpSearchEntitiesList[Index];
			if(pEntity->m_RenderTexture.IsValid() || pEntity->m_pDecodeJob)
				return;
			StartEntitiesDecode(pEntity, Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			SCustomGame *pGame = gs_vpSearchGamesList[Index];
			if(pGame->m_RenderTexture.IsValid() || pGame->m_pDecodeJob)
				return;
			StartAssetDecode(pGame, "game", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			SCustomEmoticon *pEmoticon = gs_vpSearchEmoticonsList[Index];
			if(pEmoticon->m_RenderTexture.IsValid() || pEmoticon->m_pDecodeJob)
				return;
			StartAssetDecode(pEmoticon, "emoticons", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			SCustomParticle *pParticle = gs_vpSearchParticlesList[Index];
			if(pParticle->m_RenderTexture.IsValid() || pParticle->m_pDecodeJob)
				return;
			StartAssetDecode(pParticle, "particles", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			SCustomHud *pHud = gs_vpSearchHudList[Index];
			if(pHud->m_RenderTexture.IsValid() || pHud->m_pDecodeJob)
				return;
			StartAssetDecode(pHud, "hud", Storage(), Engine());
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			SCustomExtras *pExtras = gs_vpSearchExtrasList[Index];
			if(pExtras->m_RenderTexture.IsValid() || pExtras->m_pDecodeJob)
				return;
			StartAssetDecode(pExtras, "extras", Storage(), Engine());
		}
	};

	auto ProcessCompletedDecodeJobs = [&]() {
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			for(auto *pEntity : gs_vpSearchEntitiesList)
				UploadCompletedDecodeJob(pEntity);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			for(auto *pGame : gs_vpSearchGamesList)
				UploadCompletedDecodeJob(pGame);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			for(auto *pEmoticon : gs_vpSearchEmoticonsList)
				UploadCompletedDecodeJob(pEmoticon);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			for(auto *pParticle : gs_vpSearchParticlesList)
				UploadCompletedDecodeJob(pParticle);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			for(auto *pHud : gs_vpSearchHudList)
				UploadCompletedDecodeJob(pHud);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			for(auto *pExtras : gs_vpSearchExtrasList)
				UploadCompletedDecodeJob(pExtras);
		}
	};

	size_t SearchListSize = 0;

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
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}

	for(size_t i = 0; i < SearchListSize; ++i)
	{
		StartPreviewDecode(i);
	}
	ProcessCompletedDecodeJobs();

	if(s_CurCustomTab != ASSETS_TAB_HUD && s_CurCustomTab != ASSETS_TAB_ENTITIES && s_CurCustomTab != ASSETS_TAB_GAME && s_CurCustomTab != ASSETS_TAB_EMOTICONS && s_CurCustomTab != ASSETS_TAB_PARTICLES)
	{
		static CListBox s_ListBox;
		s_ListBox.DoStart(TextureHeight + 15.0f + 10.0f + Margin, SearchListSize, CustomList.w / (Margin + TextureWidth), 1, OldSelected, &CustomList, false);
		static std::vector<CButtonContainer> s_vLocalDeleteButtons;
		s_vLocalDeleteButtons.resize(SearchListSize);
		static char s_aPendingDeleteName[50] = "";
		static CUi::SConfirmPopupContext s_DeleteConfirmPopup;
		bool DeleteLocalRequested = false;
		char aDeleteLocalName[50] = "";
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

			const CUIRect CardRect = ItemRect;

			CUIRect TextureRect;
			ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
			TextureRect.HSplitTop(10, nullptr, &TextureRect);
			Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);
			if(pItem->m_RenderTexture.IsValid())
			{
				Graphics()->WrapClamp();
				Graphics()->TextureSet(pItem->m_RenderTexture);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}

			if(str_comp(pItem->m_aName, "default") != 0)
			{
				CUIRect DeleteButton = CardRect;
				DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
				DeleteButton.VSplitRight(24.0f, nullptr, &DeleteButton);
				DeleteButton.Margin(2.0f, &DeleteButton);
				if(Ui()->DoButton_FontIcon(&s_vLocalDeleteButtons[i], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				{
					DeleteLocalRequested = true;
					str_copy(aDeleteLocalName, pItem->m_aName, sizeof(aDeleteLocalName));
				}
			}
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

		if(!DeleteLocalRequested && s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && NewSelected >= 0 && OldSelected != NewSelected)
		{
			if(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName[0] != '\0')
			{
				if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
				{
					str_copy(g_Config.m_ClAssetsEntities, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->m_MapImages.ChangeEntitiesPath(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				}
				else if(s_CurCustomTab == ASSETS_TAB_GAME)
				{
					str_copy(g_Config.m_ClAssetGame, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				{
					str_copy(g_Config.m_ClAssetEmoticons, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
				}
				else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				{
					str_copy(g_Config.m_ClAssetParticles, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
				}
				else if(s_CurCustomTab == ASSETS_TAB_HUD)
				{
					str_copy(g_Config.m_ClAssetHud, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
				{
					str_copy(g_Config.m_ClAssetExtras, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
					GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
				}
			}
		}
	}

	if((s_CurCustomTab == ASSETS_TAB_HUD || s_CurCustomTab == ASSETS_TAB_ENTITIES || s_CurCustomTab == ASSETS_TAB_GAME || s_CurCustomTab == ASSETS_TAB_EMOTICONS || s_CurCustomTab == ASSETS_TAB_PARTICLES) && WorkshopHudView.h > 0.0f)
	{
		SWorkshopHudState &WorkshopState = s_CurCustomTab == ASSETS_TAB_HUD ? gs_WorkshopHudState :
			(s_CurCustomTab == ASSETS_TAB_ENTITIES ? gs_WorkshopEntitiesState :
				(s_CurCustomTab == ASSETS_TAB_GAME ? gs_WorkshopGameState :
					(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? gs_WorkshopEmoticonsState : gs_WorkshopParticlesState)));
		const char *pWorkshopAssetsUrl = s_CurCustomTab == ASSETS_TAB_HUD ? WORKSHOP_HUD_ASSETS_URL :
			(s_CurCustomTab == ASSETS_TAB_ENTITIES ? WORKSHOP_ENTITIES_ASSETS_URL :
				(s_CurCustomTab == ASSETS_TAB_GAME ? WORKSHOP_GAME_ASSETS_URL :
					(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? WORKSHOP_EMOTICONS_ASSETS_URL : WORKSHOP_PARTICLES_ASSETS_URL)));
		const char *pWorkshopCategory = s_CurCustomTab == ASSETS_TAB_HUD ? WORKSHOP_HUD_CATEGORY :
			(s_CurCustomTab == ASSETS_TAB_ENTITIES ? WORKSHOP_ENTITIES_CATEGORY :
				(s_CurCustomTab == ASSETS_TAB_GAME ? WORKSHOP_GAME_CATEGORY :
					(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? WORKSHOP_EMOTICONS_CATEGORY : WORKSHOP_PARTICLES_CATEGORY)));
		const char *pWorkshopCategoryAlt = s_CurCustomTab == ASSETS_TAB_HUD ? nullptr :
			(s_CurCustomTab == ASSETS_TAB_ENTITIES ? WORKSHOP_ENTITIES_CATEGORY_ALT :
				(s_CurCustomTab == ASSETS_TAB_GAME ? WORKSHOP_GAME_CATEGORY_ALT :
					(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? WORKSHOP_EMOTICONS_CATEGORY_ALT : WORKSHOP_PARTICLES_CATEGORY_ALT)));
		const char *pInstallFolder = s_CurCustomTab == ASSETS_TAB_HUD ? "hud" :
			(s_CurCustomTab == ASSETS_TAB_ENTITIES ? "entities" :
				(s_CurCustomTab == ASSETS_TAB_GAME ? "game" :
					(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? "emoticons" : "particles")));

		auto IsLocalAssetSelected = [&](const char *pName) {
			if(s_CurCustomTab == ASSETS_TAB_HUD)
				return str_comp(pName, g_Config.m_ClAssetHud) == 0;
			if(s_CurCustomTab == ASSETS_TAB_GAME)
				return str_comp(pName, g_Config.m_ClAssetGame) == 0;
			if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
			if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
			return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
		};

		auto ApplyLocalAssetSelection = [&](const char *pName) {
			if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, pName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, pName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, pName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, pName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else
			{
				str_copy(g_Config.m_ClAssetsEntities, pName);
				GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
			}
		};

		auto StartWorkshopListTask = [&]() {
			auto pTask = HttpGet(pWorkshopAssetsUrl);
			pTask->Timeout(CTimeout{10000, 20000, 200, 10});
			pTask->LogProgress(HTTPLOG::FAILURE);
			pTask->FailOnErrorStatus(false);
			WorkshopState.m_pListTask = std::move(pTask);
			WorkshopState.m_LoadFailed = false;
			str_copy(WorkshopState.m_aError, "");
			Http()->Run(WorkshopState.m_pListTask);
		};

		// Load from local cache first for instant display
		if(!WorkshopState.m_Requested && WorkshopState.m_vAssets.empty())
		{
			const char *pCacheFile = GetWorkshopCacheFilename(s_CurCustomTab);
			if(pCacheFile && LoadWorkshopCache(WorkshopState, Storage(), pCacheFile))
			{
				WorkshopState.m_CacheTime = Client()->LocalTime();
				WorkshopState.m_Requested = true; // Mark as loaded from cache
				// Don't set m_LastRefreshTime here, so we won't auto-refresh
			}
		}

		// Only start HTTP request if no cache exists (first time or after refresh)
		if(!WorkshopState.m_Requested && !WorkshopState.m_pListTask && WorkshopState.m_vAssets.empty())
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
						Parsed = ParseWorkshopAssets(pJson, pWorkshopCategory, pWorkshopCategoryAlt, pInstallFolder, vParsedAssets, aError, sizeof(aError));
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
					NewAsset.m_Installed = Storage()->FileExists(NewAsset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
					NewAssetIds.insert(NewAsset.m_Id);
					
					auto It = ExistingAssetIndexMap.find(NewAsset.m_Id);
					if(It != ExistingAssetIndexMap.end())
					{
						// Update existing asset: preserve texture and tasks
						SWorkshopHudAsset &ExistingAsset = WorkshopState.m_vAssets[It->second];
						NewAsset.m_ThumbTexture = ExistingAsset.m_ThumbTexture;
						ExistingAsset.m_ThumbTexture = IGraphics::CTextureHandle();
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
				WorkshopState.m_vAssets.erase(
					std::remove_if(WorkshopState.m_vAssets.begin(), WorkshopState.m_vAssets.end(),
						[&NewAssetIds](const SWorkshopHudAsset &Asset) {
							return NewAssetIds.find(Asset.m_Id) == NewAssetIds.end();
						}),
					WorkshopState.m_vAssets.end()
				);
				
				WorkshopState.m_Requested = true;
				WorkshopState.m_LastRefreshTime = Client()->LocalTime();
				
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
		
		constexpr int MaxGpuUploadsPerFrame = 30;
		int GpuUploadsThisFrame = 0;
		
		for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
		{
			if(Asset.m_pDecodeJob && Asset.m_pDecodeJob->IsCompleted())
			{
				if(!Asset.m_ThumbTexture.IsValid() && GpuUploadsThisFrame < MaxGpuUploadsPerFrame)
				{
					CImageDecodeJob::SResult Result = Asset.m_pDecodeJob->GetResult();
					if(Result.m_Success && Result.m_Image.m_pData)
					{
						constexpr int MaxPreviewSize = 512;
						if(Result.m_Image.m_Width > MaxPreviewSize || Result.m_Image.m_Height > MaxPreviewSize)
						{
							int NewWidth = Result.m_Image.m_Width;
							int NewHeight = Result.m_Image.m_Height;
							if(NewWidth > NewHeight)
							{
								NewHeight = (int)((float)NewHeight * MaxPreviewSize / NewWidth);
								NewWidth = MaxPreviewSize;
							}
							else
							{
								NewWidth = (int)((float)NewWidth * MaxPreviewSize / NewHeight);
								NewHeight = MaxPreviewSize;
							}
							ResizeImage(Result.m_Image, NewWidth, NewHeight);
						}
						Asset.m_ThumbTexture = Graphics()->LoadTextureRaw(Result.m_Image, 0, Asset.m_Name.c_str());
						Result.m_Image.Free();
						++GpuUploadsThisFrame;
					}
				}
				Asset.m_pDecodeJob.reset();
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
		if(RefreshLocalList)
			ClearCustomItems(s_CurCustomTab);

		WorkshopHudView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 8.0f);
		WorkshopHudView.Margin(8.0f, &WorkshopHudView);

		CUIRect WorkshopHeader, WorkshopListArea;
		WorkshopHudView.HSplitTop(20.0f, &WorkshopHeader, &WorkshopListArea);

		CUIRect WorkshopRefreshButton;
		WorkshopHeader.VSplitRight(76.0f, &WorkshopHeader, &WorkshopRefreshButton);
		static CButtonContainer s_WorkshopRefreshButton;
		if(DoButton_Menu(&s_WorkshopRefreshButton, "刷新", 0, &WorkshopRefreshButton))
		{
			// Incremental refresh: don't clear data, just fetch updates
			if(!WorkshopState.m_pListTask)
			{
				WorkshopState.m_Requested = false; // Allow new request
				StartWorkshopListTask();
			}
		}

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
			static std::vector<CButtonContainer> s_vWorkshopHudActionButtons;
			static std::vector<CButtonContainer> s_vWorkshopEntitiesActionButtons;
			static std::vector<CButtonContainer> s_vWorkshopGameActionButtons;
			static std::vector<CButtonContainer> s_vWorkshopEmoticonsActionButtons;
			static std::vector<CButtonContainer> s_vWorkshopParticlesActionButtons;
			auto &vWorkshopActionButtons = s_CurCustomTab == ASSETS_TAB_HUD ? s_vWorkshopHudActionButtons :
				(s_CurCustomTab == ASSETS_TAB_ENTITIES ? s_vWorkshopEntitiesActionButtons :
					(s_CurCustomTab == ASSETS_TAB_GAME ? s_vWorkshopGameActionButtons :
						(s_CurCustomTab == ASSETS_TAB_EMOTICONS ? s_vWorkshopEmoticonsActionButtons : s_vWorkshopParticlesActionButtons)));
			vWorkshopActionButtons.resize(AssetCount);

		std::vector<size_t> vVisibleDownloadableAssetIndices;
		vVisibleDownloadableAssetIndices.reserve(AssetCount);
		for(size_t AssetIndex = 0; AssetIndex < AssetCount; ++AssetIndex)
		{
			const SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[AssetIndex];
			if(Asset.m_Installed)
				continue;
			if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(Asset.m_Name.c_str(), s_aFilterInputs[s_CurCustomTab].GetString()))
				continue;
			vVisibleDownloadableAssetIndices.push_back(AssetIndex);
		}

		const size_t CombinedCount = LocalAssetCount + vVisibleDownloadableAssetIndices.size();
		if(CombinedCount == 0)
		{
			if(WorkshopState.m_pListTask)
			{
				Ui()->DoLabel(&WorkshopListArea, Localize("Loading..."), 12.0f, TEXTALIGN_MC);
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
			s_WorkshopAssetsListBox.DoStart(TextureHeight + 15.0f + 10.0f + Margin, CombinedCount, Columns, 1, -1, &WorkshopListArea, false);

			static std::vector<CButtonContainer> s_vWorkshopLocalDeleteButtons;
			s_vWorkshopLocalDeleteButtons.resize(LocalAssetTotalCount);

			static char s_aWorkshopPendingDeleteName[50] = "";
			static CUi::SConfirmPopupContext s_WorkshopDeleteConfirmPopup;
			static size_t s_PendingDownloadAssetIndex = SIZE_MAX;
			static CUi::SConfirmPopupContext s_WorkshopDownloadConfirmPopup;

			constexpr int MaxThumbStartsPerFrame = 2; // Keep low to avoid frame hitches
			int ThumbStartsThisFrame = 0;
			int OldCombinedSelected = -1;
			bool DeleteLocalRequested = false;
			char aDeleteLocalName[50] = "";
			bool WorkshopActionTriggered = false;

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

					const CUIRect CardRect = ItemRect;

					CUIRect TextureRect;
					ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
					TextureRect.HSplitTop(10, nullptr, &TextureRect);
					Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);
					if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview)
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
							float OffX = TextureRect.x + (TextureRect.w - TextureWidth) / 2.0f;
							float OffY = TextureRect.y + (TextureRect.h - ROWS * TileSize) / 2.0f;

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
								if(Asset.m_Name == pItem->m_aName && Asset.m_ThumbTexture.IsValid())
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
							IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
							Graphics()->QuadsDrawTL(&QuadItem, 1);
							Graphics()->QuadsEnd();
							Graphics()->WrapNormal();
						}
					}

					if(str_comp(pItem->m_aName, "default") != 0)
					{
						CUIRect DeleteButton = CardRect;
						DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
						DeleteButton.VSplitRight(24.0f, nullptr, &DeleteButton);
						DeleteButton.Margin(2.0f, &DeleteButton);
						if(Ui()->DoButton_FontIcon(&s_vWorkshopLocalDeleteButtons[LocalIndex], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
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
							StartBackgroundDecode(Asset, Storage(), Engine());
							++ThumbStartsThisFrame;
						}
						else
						{
							Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
							Storage()->CreateFolder("qmclient/workshop", IStorage::TYPE_SAVE);
							Storage()->CreateFolder("qmclient/workshop/thumbs", IStorage::TYPE_SAVE);
							char aWebpUrl[IO_MAX_PATH_LENGTH];
							str_format(aWebpUrl, sizeof(aWebpUrl), "%s!/format/webp", Asset.m_ImageUrl.c_str());
							auto pThumbTask = HttpGetFile(aWebpUrl, Storage(), Asset.m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
							pThumbTask->Timeout(CTimeout{8000, 20000, 100, 10});
							pThumbTask->LogProgress(HTTPLOG::FAILURE);
							pThumbTask->FailOnErrorStatus(false);
							pThumbTask->SkipByFileTime(false);
							Asset.m_pThumbTask = std::move(pThumbTask);
							Http()->Run(Asset.m_pThumbTask);
							++ThumbStartsThisFrame;
						}
					}

					const CUIRect CardRect = ItemRect;
					CUIRect TextureRect;
					ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
					TextureRect.HSplitTop(10, nullptr, &TextureRect);
					Ui()->DoLabel(&ItemRect, Asset.m_Name.c_str(), ItemRect.h - 2, TEXTALIGN_MC);

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
						float OffX = TextureRect.x + (TextureRect.w - TextureWidth) / 2.0f;
						float OffY = TextureRect.y + (TextureRect.h - ROWS * TileSize) / 2.0f;

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
						IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
						Graphics()->WrapNormal();
					}
					else
					{
						CUIRect LoadingRect = {TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight};
						LoadingRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.10f), IGraphics::CORNER_ALL, 6.0f);
						Ui()->DoLabel(&LoadingRect, Localize("Loading..."), 10.0f, TEXTALIGN_MC);
					}

					const bool Downloading = Asset.m_pDownloadTask && !Asset.m_pDownloadTask->Done();
					CUIRect DownloadButton = CardRect;
					DownloadButton.HSplitTop(20.0f, &DownloadButton, nullptr);
					DownloadButton.VSplitRight(24.0f, nullptr, &DownloadButton);
					DownloadButton.Margin(2.0f, &DownloadButton);
					const char *pActionIcon = Downloading ? FONT_ICON_ARROW_ROTATE_RIGHT : FONT_ICON_CIRCLE_CHEVRON_DOWN;
					if(Ui()->DoButton_FontIcon(&vWorkshopActionButtons[AssetIndex], pActionIcon, 0, &DownloadButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Downloading))
					{
						s_PendingDownloadAssetIndex = AssetIndex;
						s_WorkshopDownloadConfirmPopup.Reset();
						s_WorkshopDownloadConfirmPopup.YesNoButtons();
						str_copy(s_WorkshopDownloadConfirmPopup.m_aMessage, Localize("Download this asset?"));
						Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_WorkshopDownloadConfirmPopup);
					}

					if(Asset.m_DownloadFailed)
					{
						CUIRect ErrorRect = CardRect;
						ErrorRect.HSplitBottom(14.0f, nullptr, &ErrorRect);
						Ui()->DoLabel(&ErrorRect, Localize("Download failed"), 9.0f, TEXTALIGN_MC);
					}
				}
			}

			const int NewCombinedSelected = s_WorkshopAssetsListBox.DoEnd();
			if(DeleteLocalRequested)
			{
				str_copy(s_aWorkshopPendingDeleteName, aDeleteLocalName, sizeof(s_aWorkshopPendingDeleteName));
				s_WorkshopDeleteConfirmPopup.Reset();
				s_WorkshopDeleteConfirmPopup.YesNoButtons();
				str_copy(s_WorkshopDeleteConfirmPopup.m_aMessage, Localize("Are you sure you want to delete this asset?"));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_WorkshopDeleteConfirmPopup);
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
					ClearCustomItems(s_CurCustomTab);
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
				char aInstallFolderPath[64];
				str_format(aInstallFolderPath, sizeof(aInstallFolderPath), "assets/%s", pInstallFolder);
				Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
				Storage()->CreateFolder(aInstallFolderPath, IStorage::TYPE_SAVE);

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

			if(!DeleteLocalRequested && s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && !WorkshopActionTriggered && NewCombinedSelected >= 0 && NewCombinedSelected != OldCombinedSelected && static_cast<size_t>(NewCombinedSelected) < LocalAssetCount)
			{
				const size_t LocalIndex = vVisibleLocalAssetIndices[static_cast<size_t>(NewCombinedSelected)];
				const SCustomItem *pNewItem = GetCustomItem(s_CurCustomTab, LocalIndex);
				if(pNewItem && pNewItem->m_aName[0] != '\0')
					ApplyLocalAssetSelection(pNewItem->m_aName);
			}
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
	DirectoryButton.VSplitRight(175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &ReloadButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	DirectoryButton.VSplitRight(110.0f, &DirectoryButton, &AssetsEditorButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	static CButtonContainer s_AssetsEditorButton;
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
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			AssetsEditorType = ASSETS_EDITOR_TYPE_EXTRAS;
		AssetsEditorOpen(AssetsEditorType);
		return;
	}

	CUIRect AssetsDirButton;
	DirectoryButton.VSplitRight(100.0f, &DirectoryButton, &AssetsDirButton);

	// Entity Preview 按钮（仅实体层标签页显示）
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		CUIRect ToggleRect;
		DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
		DirectoryButton.VSplitRight(100.0f, &DirectoryButton, &ToggleRect);
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoButton_Menu(&s_EntityPreviewToggleId, Localize("Entity Preview"), s_EntityGamePreview, &ToggleRect))
			s_EntityGamePreview = !s_EntityGamePreview;
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &ToggleRect, Localize("Toggle between game scene preview and raw texture"));
	}

	static CButtonContainer s_AssetsDirId;
	if(DoButton_Menu(&s_AssetsDirId, Localize("Assets directory"), 0, &AssetsDirButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
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
		if(s_CurCustomTab == ASSETS_TAB_HUD)
			ResetWorkshopState(gs_WorkshopHudState, Graphics(), true);
		else if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			ResetWorkshopState(gs_WorkshopEntitiesState, Graphics(), true);
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			ResetWorkshopState(gs_WorkshopGameState, Graphics(), true);
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			ResetWorkshopState(gs_WorkshopEmoticonsState, Graphics(), true);
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			ResetWorkshopState(gs_WorkshopParticlesState, Graphics(), true);
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
