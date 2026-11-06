#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_JOB_H

#include <base/lock.h>

#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/background.h>
#include <game/client/components/menus.h>

#include <algorithm>
#include <cstring>
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

	struct SScanLimits
	{
		size_t m_MaxDepth = 32;
		size_t m_MaxDirectoryCalls = 8192;
		size_t m_MaxNonDotVisits = 131072;
		size_t m_MaxEntries = 65536;
	};

	struct SScanContext
	{
		IStorage *m_pStorage;
		std::vector<SAssetEntry> *m_pEntries;
		EAssetResourceKind m_Kind;
		bool m_SkipReservedDefault;
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
		const SScanLimits *m_pLimits;
		size_t m_DirectoryCalls = 0;
		size_t m_NonDotVisits = 0;
		size_t m_EmittedEntries = 0;
		bool m_Failed = false;
	};

private:
	EAssetType m_Type;
	IStorage *m_pStorage;
	SScanLimits m_Limits;
	mutable CLock m_Lock;
	std::vector<SAssetEntry> m_vEntries;
	mutable bool m_Completed = false;
	int m_Generation = 0;

	static bool FailScan(SScanContext *pContext)
	{
		pContext->m_Failed = true;
		return false;
	}

	static bool CountScanWork(SScanContext *pContext, size_t &Count, size_t Limit)
	{
		if(pContext->m_Failed)
			return false;
		if(Count >= Limit)
			return FailScan(pContext);
		++Count;
		return true;
	}

	template<size_t N>
	static bool JoinScanPath(SScanContext *pContext, char (&aOut)[N], const char *pParent, const char *pName)
	{
		const size_t ParentLength = std::strlen(pParent);
		const size_t NameLength = std::strlen(pName);
		const size_t Separator = ParentLength != 0 ? 1 : 0;
		if(ParentLength >= N || Separator > N - 1 - ParentLength || NameLength > N - 1 - ParentLength - Separator)
			return FailScan(pContext);
		if(Separator)
			str_format(aOut, sizeof(aOut), "%s/%s", pParent, pName);
		else
			str_copy(aOut, pName);
		return true;
	}

	static bool AddScanEntry(SScanContext *pContext, const SAssetEntry &Entry)
	{
		if(!CountScanWork(pContext, pContext->m_EmittedEntries, pContext->m_pLimits->m_MaxEntries))
			return false;
		pContext->m_pEntries->push_back(Entry);
		return true;
	}

	static bool ScanDirectory(SScanContext *pContext, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser)
	{
		if(!CountScanWork(pContext, pContext->m_DirectoryCalls, pContext->m_pLimits->m_MaxDirectoryCalls))
			return false;
		// Checked 保留跨根名称遮蔽，并将停止/错误传播到后续根；不能退回 void 扫描。
		const auto Result = pContext->m_pStorage->ListDirectoryChecked(IStorage::TYPE_ALL, pPath, Callback, pUser);
		if(Result != EFsListDirectoryResult::SUCCESS && Result != EFsListDirectoryResult::NOT_FOUND)
			return FailScan(pContext);
		return !pContext->m_Failed;
	}

	static int ScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		auto *pContext = static_cast<SScanContext *>(pUser);
		if(pContext->m_Failed)
			return 1;
		if(!str_comp(pName, ".") || !str_comp(pName, ".."))
			return 0;
		if(!CountScanWork(pContext, pContext->m_NonDotVisits, pContext->m_pLimits->m_MaxNonDotVisits))
			return 1;

		if(IsDir)
		{
			if(pContext->m_Kind != EAssetResourceKind::DIRECTORY)
				return 0;
			if(pName[0] == '.')
				return 0;
			if(pContext->m_SkipReservedDefault && str_comp(pName, "default") == 0)
				return 0;

			SAssetEntry Entry;
			if(std::strlen(pName) >= sizeof(Entry.m_aName))
			{
				FailScan(pContext);
				return 1;
			}
			str_copy(Entry.m_aName, pName);
			Entry.m_IsDir = true;
			Entry.m_Source = pContext->m_Source;
			return AddScanEntry(pContext, Entry) ? 0 : 1;
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
				if(std::strlen(pName) >= sizeof(aName))
				{
					FailScan(pContext);
					return 1;
				}
				str_truncate(aName, sizeof(aName), pName, str_length(pName) - str_length(pExt));
				if(pContext->m_SkipReservedDefault && str_comp(aName, "default") == 0)
					return 0;

				SAssetEntry Entry;
				str_copy(Entry.m_aName, aName);
				Entry.m_IsDir = false;
				Entry.m_Source = pContext->m_Source;
				return AddScanEntry(pContext, Entry) ? 0 : 1;
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
		size_t m_Depth = 0;
	};

	static int RecursiveMapScanCallback(const char *pName, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		auto *pContext = static_cast<SRecursiveMapScanContext *>(pUser);
		auto *pScan = pContext->m_pScanContext;
		if(pScan->m_Failed)
			return 1;
		if(!str_comp(pName, ".") || !str_comp(pName, ".."))
			return 0;
		if(!CountScanWork(pScan, pScan->m_NonDotVisits, pScan->m_pLimits->m_MaxNonDotVisits))
			return 1;
		if(IsDir && pName[0] == '.')
			return 0;
		const char *pExtension = nullptr;
		if(!IsDir)
		{
			pExtension = str_endswith_nocase(pName, ".map") ? ".map" : FindBackgroundFileExtension(pName);
			if(pExtension == nullptr)
				return 0;
		}

		char aRelativePath[IO_MAX_PATH_LENGTH];
		char aFullPath[IO_MAX_PATH_LENGTH];
		if(!JoinScanPath(pScan, aRelativePath, pContext->m_aRelativePath, pName) ||
			!JoinScanPath(pScan, aFullPath, pContext->m_aBasePath, aRelativePath))
			return 1;

		if(IsDir)
		{
			if(pContext->m_Depth >= pScan->m_pLimits->m_MaxDepth)
			{
				FailScan(pScan);
				return 1;
			}

			SRecursiveMapScanContext NextContext = *pContext;
			str_copy(NextContext.m_aRelativePath, aRelativePath, sizeof(NextContext.m_aRelativePath));
			NextContext.m_Depth = pContext->m_Depth + 1;
			return ScanDirectory(pScan, aFullPath, RecursiveMapScanCallback, &NextContext) ? 0 : 1;
		}

		char aName[IO_MAX_PATH_LENGTH];
		if(pContext->m_aNamePrefix[0] != '\0')
		{
			if(str_comp_nocase(pExtension, ".map") == 0)
			{
				char aWithoutExt[IO_MAX_PATH_LENGTH];
				str_truncate(aWithoutExt, sizeof(aWithoutExt), aRelativePath, str_length(aRelativePath) - str_length(pExtension));
				if(!JoinScanPath(pScan, aName, pContext->m_aNamePrefix, aWithoutExt))
					return 1;
			}
			else
			{
				if(!JoinScanPath(pScan, aName, pContext->m_aNamePrefix, aRelativePath))
					return 1;
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
		return AddScanEntry(pScan, Entry) ? 0 : 1;
	}

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		std::vector<SAssetEntry> vEntries;
		SScanContext ScanContext{m_pStorage, &vEntries, EAssetResourceKind::DIRECTORY, true, EEntityBgHierarchyEntrySource::LOCAL, &m_Limits};

		if(m_Type == ASSET_TYPE_EXTRAS)
		{
			if(!ScanDirectory(&ScanContext, "assets/extras", ScanCallback, &ScanContext))
				return;
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
				if(!JoinScanPath(&ScanContext, AssetsContext.m_aBasePath, "", pCategory->m_pInstallFolder))
					return;
				AssetsContext.m_aRelativePath[0] = '\0';
				str_copy(AssetsContext.m_aNamePrefix, "entity_bg", sizeof(AssetsContext.m_aNamePrefix));
				AssetsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				if(!ScanDirectory(&ScanContext, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &AssetsContext))
					return;

				SRecursiveMapScanContext MapsContext;
				MapsContext.m_pScanContext = &ScanContext;
				str_copy(MapsContext.m_aBasePath, "maps", sizeof(MapsContext.m_aBasePath));
				MapsContext.m_aRelativePath[0] = '\0';
				MapsContext.m_aNamePrefix[0] = '\0';
				MapsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				if(!ScanDirectory(&ScanContext, "maps", RecursiveMapScanCallback, &MapsContext))
					return;
			}
			else if(pCategory->m_Kind == EAssetResourceKind::MAP_FILE)
			{
				SRecursiveMapScanContext RecursiveContext;
				RecursiveContext.m_pScanContext = &ScanContext;
				if(!JoinScanPath(&ScanContext, RecursiveContext.m_aBasePath, "", pCategory->m_pInstallFolder))
					return;
				RecursiveContext.m_aRelativePath[0] = '\0';
				RecursiveContext.m_aNamePrefix[0] = '\0';
				RecursiveContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				if(!ScanDirectory(&ScanContext, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &RecursiveContext))
					return;
			}
			else
			{
				if(!ScanDirectory(&ScanContext, pCategory->m_pInstallFolder, ScanCallback, &ScanContext))
					return;
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
		CAssetListLoadJob(Type, pStorage, SScanLimits{})
	{
	}

	CAssetListLoadJob(EAssetType Type, IStorage *pStorage, const SScanLimits &Limits) :
		m_Type(Type),
		m_pStorage(pStorage),
		m_Limits(Limits)
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
