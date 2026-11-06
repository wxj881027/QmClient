#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_JOB_H

#include <base/lock.h>

#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/background.h>
#include <game/client/components/menus.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

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

	using SAssetEntry = CMenus::SSettingsAssetMergeEntry;

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
	int m_Generation = 0;

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

		const char *pExtension = str_endswith_nocase(pInfo->m_pName, ".map") ? ".map" : FindBackgroundFileExtension(pInfo->m_pName);
		if(pExtension == nullptr)
			return 0;

		char aName[IO_MAX_PATH_LENGTH];
		if(pContext->m_aNamePrefix[0] != '\0')
		{
			if(str_comp_nocase(pExtension, ".map") == 0)
			{
				char aWithoutExt[IO_MAX_PATH_LENGTH];
				str_truncate(aWithoutExt, sizeof(aWithoutExt), aRelativePath, str_length(aRelativePath) - str_length(pExtension));
				str_format(aName, sizeof(aName), "%s/%s", pContext->m_aNamePrefix, aWithoutExt);
			}
			else
			{
				str_format(aName, sizeof(aName), "%s/%s", pContext->m_aNamePrefix, aRelativePath);
			}
		}
		else
		{
			if(str_comp_nocase(pExtension, ".map") == 0)
				str_truncate(aName, sizeof(aName), aRelativePath, str_length(aRelativePath) - str_length(pExtension));
			else
				str_copy(aName, aRelativePath);
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

protected:
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

		// 按名字排序后一次发布完整候选。
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

	void SetGeneration(int Generation)
	{
		m_Generation = Generation;
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed || Finished();
	}

	bool Succeeded() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	std::vector<SAssetEntry> TakeEntries() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		if(!m_Completed)
			return {};
		return std::move(m_vEntries);
	}

	EAssetType GetType() const { return m_Type; }
	int Generation() const { return m_Generation; }
};

#endif
