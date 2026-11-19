#include "qm_storage_layout.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/storage.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace
{
	constexpr const char *DIRECTORIES[] = {
		qm_storage::ROOT_DIR,
		qm_storage::CONFIG_DIR,
		qm_storage::CONFIG_CREDENTIALS_DIR,
		qm_storage::CONFIG_DRAFTS_DIR,
		qm_storage::CONFIG_PACKAGES_DIR,
		qm_storage::CACHE_DIR,
		qm_storage::CACHE_NETWORK_DIR,
		qm_storage::CACHE_REMOTE_DIR,
		qm_storage::CACHE_WORKSHOP_DIR,
		qm_storage::CACHE_WORKSHOP_CATALOG_DIR,
		qm_storage::CACHE_WORKSHOP_THUMBS_DIR,
		qm_storage::CACHE_FONTS_DIR,
		qm_storage::CACHE_FONT_PREVIEWS_DIR,
		qm_storage::CACHE_MAPS_DIR,
		qm_storage::CACHE_MAP_COVERS_DIR,
		qm_storage::CACHE_MAP_PREVIEWS_DIR,
		qm_storage::CACHE_SETTINGS_DIR,
		qm_storage::CACHE_SOCIAL_DIR,
		qm_storage::STATE_DIR,
		qm_storage::STATE_MAPS_DIR,
		qm_storage::STATE_WORKSHOP_DIR,
		qm_storage::STATE_LEGACY_DIR,
		qm_storage::CONTENT_DIR,
		qm_storage::CONTENT_RANK1_DIR,
		qm_storage::CONTENT_RANK1_DEMOS_DIR,
		qm_storage::CONTENT_RANK1_GHOSTS_DIR,
		qm_storage::CONTENT_CHAT_LOG_DIR,
		qm_storage::CONTENT_LYRICS_DIR,
		qm_storage::FONT_DIR,
		qm_storage::FONT_STORE_DIR,
		qm_storage::RUNTIME_DIR,
		qm_storage::RUNTIME_EDITOR_DIR,
		qm_storage::RUNTIME_UPDATES_DIR,
	};

	struct SPathMove
	{
		const char *m_pFrom;
		const char *m_pTo;
	};

	constexpr SPathMove FILE_MOVES[] = {
		{"qmclient/qmclient_profiles.cfg", qm_storage::PROFILES_FILE},
		{"qmclient/qmclient_chatbinds.cfg", qm_storage::CHATBINDS_FILE},
		{"qmclient/qmclient_warlist.cfg", qm_storage::WARLIST_FILE},
		{"qmclient/developer_token.txt", qm_storage::DEVELOPER_TOKEN_FILE},
		{"qmclient/title_token.txt", qm_storage::TITLE_TOKEN_FILE},
		{"qmclient/playtime_client_id.txt", qm_storage::PLAYTIME_CLIENT_ID_FILE},
		{"qmclient/voice_machine_id.txt", qm_storage::MACHINE_ID_FILE},
		{"qmclient/news_draft.md", qm_storage::NEWS_DRAFT_FILE},
		{"qmclient/sponsors_draft.md", qm_storage::SPONSORS_DRAFT_FILE},
		{"qmclient/map_categories.json", qm_storage::MAP_CATEGORIES_FILE},
		{"qmclient/map_history.json", qm_storage::MAP_HISTORY_FILE},
		{"qmclient/map_notes.json", qm_storage::MAP_NOTES_FILE},
		{"qmclient/axiom_map_difficulty.json", qm_storage::MAP_DIFFICULTY_CACHE_FILE},
		{"qmclient/settings_section_cache_metadata.cfg", qm_storage::SETTINGS_METADATA_FILE},
		{"qmclient/glyph_prewarm.txt", qm_storage::GLYPH_PREWARM_FILE},
		{"qmclient/statistics.json", qm_storage::STATISTICS_FILE},
		{"qmclient/score_cache.sqlite3", qm_storage::SCORE_CACHE_FILE},
		{"qmclient/score_cache.sqlite3-wal", "qmclient/state/score_cache.sqlite3-wal"},
		{"qmclient/score_cache.sqlite3-shm", "qmclient/state/score_cache.sqlite3-shm"},
		{"qmclient/save_usage.sqlite3", qm_storage::SAVE_USAGE_FILE},
		{"qmclient/save_usage.sqlite3-wal", "qmclient/state/legacy/save_usage.sqlite3-wal"},
		{"qmclient/save_usage.sqlite3-shm", "qmclient/state/legacy/save_usage.sqlite3-shm"},
		{"qmclient/markdown_broadcast.json", qm_storage::BROADCAST_CACHE_FILE},
		{"qmclient/news_cache.json", qm_storage::NEWS_CACHE_FILE},
		{"qmclient/sponsors_cache.json", qm_storage::SPONSORS_CACHE_FILE},
		{"qmclient/public_word_filter.txt", qm_storage::WORD_FILTER_CACHE_FILE},
		{"qmclient/workshop/local_asset_authors.json", qm_storage::WORKSHOP_AUTHOR_CACHE_FILE},
		{"qmclient/lifecycle_pending.marker", qm_storage::LIFECYCLE_MARKER_FILE},
		{"qmclient/crash_reporter_started_at.marker", qm_storage::CRASH_REPORTER_MARKER_FILE},
		{"qmclient/graphics_recovery.marker", qm_storage::GRAPHICS_RECOVERY_MARKER_FILE},
		{"qmclient/config_save.lock", qm_storage::CONFIG_SAVE_LOCK_FILE},
		{"qmclient/editor_collab_snapshot.map", qm_storage::EDITOR_COLLAB_SNAPSHOT_FILE},
		{"qmclient/editor_collab_incoming.map", qm_storage::EDITOR_COLLAB_INCOMING_FILE},
		{"custom-communities-ddnet-info.json", qm_storage::CUSTOM_COMMUNITIES_CACHE_FILE},
		{"qm_friend_last_seen.json", qm_storage::FRIEND_LAST_SEEN_CACHE_FILE},
		{"qm_local_map_tags.txt", qm_storage::LOCAL_MAP_TAGS_FILE},
	};

	constexpr const char *SERVERLIST_CACHE_SOURCES[] = {
		"cache/qm-serverlist.json",
		"qmclient/cache/qm-serverlist.json",
	};

	constexpr SPathMove DIRECTORY_MOVES[] = {
		{"qmclient/config_packages", qm_storage::CONFIG_PACKAGES_DIR},
		{"qmclient/fontcache", qm_storage::CACHE_FONT_PREVIEWS_DIR},
		{"qmclient/workshop/thumbs", qm_storage::CACHE_WORKSHOP_THUMBS_DIR},
		{"qmclient/mapcovers", qm_storage::CACHE_MAP_COVERS_DIR},
		{"qmclient/rank1", qm_storage::CONTENT_RANK1_DIR},
		{"qmclient/chat_log", qm_storage::CONTENT_CHAT_LOG_DIR},
		{"qmclient/lyrics", qm_storage::CONTENT_LYRICS_DIR},
		{"fonts/downloaded_fonts", "qmclient/state/legacy/old_downloaded_fonts"},
		{"map_preview_cache", qm_storage::CACHE_MAP_PREVIEWS_DIR},
	};

	constexpr const char *WORKSHOP_CATALOGS[] = {
		"workshop_hud.json",
		"workshop_entities.json",
		"workshop_game.json",
		"workshop_emoticons.json",
		"workshop_particles.json",
		"workshop_gui_cursor.json",
		"workshop_arrow.json",
		"workshop_strong_weak.json",
		"workshop_entity_bg.json",
		"workshop_extras.json",
	};

	bool EnsureFolderPath(IStorage *pStorage, const char *pPath)
	{
		if(pStorage == nullptr || pPath == nullptr || pPath[0] == '\0')
			return false;
		if(pStorage->FolderExists(pPath, IStorage::TYPE_SAVE))
			return true;

		char aPath[IO_MAX_PATH_LENGTH];
		str_copy(aPath, pPath, sizeof(aPath));
		for(char *pCursor = aPath; *pCursor != '\0'; ++pCursor)
		{
			if(*pCursor != '/')
				continue;
			*pCursor = '\0';
			const bool Created = pStorage->CreateFolder(aPath, IStorage::TYPE_SAVE) || pStorage->FolderExists(aPath, IStorage::TYPE_SAVE);
			*pCursor = '/';
			if(!Created)
				return false;
		}
		return pStorage->CreateFolder(aPath, IStorage::TYPE_SAVE) || pStorage->FolderExists(aPath, IStorage::TYPE_SAVE);
	}

	bool EnsureParentFolder(IStorage *pStorage, const char *pPath)
	{
		if(pStorage == nullptr || pPath == nullptr)
			return false;
		const char *pSlash = str_rchr(pPath, '/');
		if(pSlash == nullptr)
			return true;
		char aFolder[IO_MAX_PATH_LENGTH];
		const int Length = static_cast<int>(pSlash - pPath);
		if(Length <= 0 || Length >= static_cast<int>(sizeof(aFolder)))
			return false;
		str_copy(aFolder, pPath, Length + 1);
		return EnsureFolderPath(pStorage, aFolder);
	}

	bool FilesEqual(IStorage *pStorage, const char *pLeft, const char *pRight)
	{
		SHA256_DIGEST LeftDigest;
		SHA256_DIGEST RightDigest;
		return pStorage->CalculateHashes(pLeft, IStorage::TYPE_SAVE, &LeftDigest) &&
		       pStorage->CalculateHashes(pRight, IStorage::TYPE_SAVE, &RightDigest) &&
		       LeftDigest == RightDigest;
	}

	bool CopyFile(IStorage *pStorage, const char *pFrom, const char *pTo)
	{
		char aTemp[IO_MAX_PATH_LENGTH];
		if(str_length(pTo) + str_length(".migrating") >= sizeof(aTemp))
			return false;
		str_format(aTemp, sizeof(aTemp), "%s.migrating", pTo);
		pStorage->RemoveFile(aTemp, IStorage::TYPE_SAVE);

		IOHANDLE Source = pStorage->OpenFile(pFrom, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(Source == nullptr)
			return false;
		IOHANDLE Destination = pStorage->OpenFile(aTemp, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(Destination == nullptr)
		{
			io_close(Source);
			return false;
		}

		bool Success = true;
		unsigned char aBuffer[64 * 1024];
		while(true)
		{
			const int Read = io_read(Source, aBuffer, sizeof(aBuffer));
			if(Read < 0 || (Read > 0 && io_write(Destination, aBuffer, Read) != static_cast<unsigned>(Read)))
			{
				Success = false;
				break;
			}
			if(Read == 0)
				break;
		}
		Success &= io_close(Source) == 0;
		Success &= io_close(Destination) == 0;
		Success &= FilesEqual(pStorage, pFrom, aTemp);
		if(!Success || !pStorage->RenameFile(aTemp, pTo, IStorage::TYPE_SAVE, false))
		{
			pStorage->RemoveFile(aTemp, IStorage::TYPE_SAVE);
			return false;
		}
		if(!pStorage->RemoveFile(pFrom, IStorage::TYPE_SAVE))
		{
			// 源文件仍被占用时回滚目标，避免下次启动遇到内容不同的双份文件。
			pStorage->RemoveFile(pTo, IStorage::TYPE_SAVE);
			return false;
		}
		return true;
	}

	bool MoveFile(IStorage *pStorage, const char *pFrom, const char *pTo)
	{
		if(!pStorage->FileExists(pFrom, IStorage::TYPE_SAVE))
			return true;
		if(!EnsureParentFolder(pStorage, pTo))
		{
			log_error("storage_layout", "Could not create the target folder for '%s'", pTo);
			return false;
		}
		if(pStorage->FileExists(pTo, IStorage::TYPE_SAVE))
		{
			if(!FilesEqual(pStorage, pFrom, pTo))
			{
				log_error("storage_layout", "Migration conflict: both '%s' and '%s' exist with different contents", pFrom, pTo);
				return false;
			}
			return pStorage->RemoveFile(pFrom, IStorage::TYPE_SAVE);
		}
		if(pStorage->RenameFile(pFrom, pTo, IStorage::TYPE_SAVE, false))
			return true;
		if(CopyFile(pStorage, pFrom, pTo))
			return true;
		log_error("storage_layout", "Could not migrate '%s' to '%s'", pFrom, pTo);
		return false;
	}

	bool MoveCacheFileToLegacy(IStorage *pStorage, const char *pFrom, const char *pTarget)
	{
		const char *pLeaf = str_rchr(pTarget, '/');
		pLeaf = pLeaf == nullptr ? pTarget : pLeaf + 1;
		for(int Index = 0; Index < 100; ++Index)
		{
			char aLegacy[IO_MAX_PATH_LENGTH];
			if(Index == 0)
				str_format(aLegacy, sizeof(aLegacy), "qmclient/state/legacy/cache/%s", pLeaf);
			else
				str_format(aLegacy, sizeof(aLegacy), "qmclient/state/legacy/cache/%s.%d", pLeaf, Index);
			if(pStorage->FileExists(aLegacy, IStorage::TYPE_SAVE))
				continue;
			if(MoveFile(pStorage, pFrom, aLegacy))
				return true;
			log_error("storage_layout", "Could not preserve superseded cache file '%s'", pFrom);
			return false;
		}
		log_error("storage_layout", "No free legacy name for superseded cache file '%s'", pFrom);
		return false;
	}

	// 旧版本曾同时把缓存写到两个位置。缓存可以重建，但两份内容可能不同；
	// 因此按修改时间选最新的一份作为当前缓存，其余副本移入 state/legacy。
	bool MigrateNewestCacheFile(IStorage *pStorage, const char *const *apSources, size_t NumSources, const char *pTarget)
	{
		bool Success = true;
		std::vector<const char *> vRemaining;
		const bool TargetExists = pStorage->FileExists(pTarget, IStorage::TYPE_SAVE);
		for(size_t Index = 0; Index < NumSources; ++Index)
		{
			const char *pSource = apSources[Index];
			if(!pStorage->FileExists(pSource, IStorage::TYPE_SAVE))
				continue;
			if(TargetExists && FilesEqual(pStorage, pSource, pTarget))
			{
				Success &= pStorage->RemoveFile(pSource, IStorage::TYPE_SAVE);
				continue;
			}
			vRemaining.push_back(pSource);
		}
		if(vRemaining.empty())
			return Success;

		time_t TargetModified = 0;
		if(TargetExists)
		{
			time_t Created = 0;
			if(!pStorage->RetrieveTimes(pTarget, IStorage::TYPE_SAVE, &Created, &TargetModified))
				TargetModified = 0;
		}

		const char *pWinner = nullptr;
		time_t WinnerModified = TargetModified;
		for(const char *pSource : vRemaining)
		{
			time_t Created = 0;
			time_t Modified = 0;
			pStorage->RetrieveTimes(pSource, IStorage::TYPE_SAVE, &Created, &Modified);
			if((!TargetExists && pWinner == nullptr) || Modified > WinnerModified)
			{
				pWinner = pSource;
				WinnerModified = Modified;
			}
		}

		if(pWinner != nullptr)
		{
			char aDemotedTarget[IO_MAX_PATH_LENGTH];
			bool TargetDemoted = false;
			if(TargetExists)
			{
				for(int Index = 0; Index < 100; ++Index)
				{
					if(Index == 0)
						str_format(aDemotedTarget, sizeof(aDemotedTarget), "%s.migration-old", pTarget);
					else
						str_format(aDemotedTarget, sizeof(aDemotedTarget), "%s.migration-old.%d", pTarget, Index);
					if(!pStorage->FileExists(aDemotedTarget, IStorage::TYPE_SAVE))
						break;
				}
				if(!MoveFile(pStorage, pTarget, aDemotedTarget))
					return false;
				TargetDemoted = true;
			}
			if(!MoveFile(pStorage, pWinner, pTarget))
			{
				if(TargetDemoted)
					MoveFile(pStorage, aDemotedTarget, pTarget);
				return false;
			}
			if(TargetDemoted)
				Success &= MoveCacheFileToLegacy(pStorage, aDemotedTarget, pTarget);
		}

		for(const char *pSource : vRemaining)
		{
			if(pSource == pWinner)
				continue;
			Success &= MoveCacheFileToLegacy(pStorage, pSource, pTarget);
		}
		return Success;
	}

	bool PathIsLink(IStorage *pStorage, const char *pPath)
	{
		char aAbsolute[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aAbsolute, sizeof(aAbsolute));
#if defined(CONF_FAMILY_WINDOWS)
		const std::wstring Wide = windows_utf8_to_wide(aAbsolute);
		const DWORD Attributes = GetFileAttributesW(Wide.c_str());
		return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
		struct stat Stat;
		return lstat(aAbsolute, &Stat) == 0 && S_ISLNK(Stat.st_mode);
#endif
	}

	struct STreeEntry
	{
		std::string m_Name;
		bool m_IsDirectory = false;
	};

	int CollectTreeEntry(const CFsFileInfo *pInfo, int IsDir, int, void *pUser)
	{
		auto *pEntries = static_cast<std::vector<STreeEntry> *>(pUser);
		if(pInfo->m_pName != nullptr && pInfo->m_pName[0] != '\0' &&
			str_comp(pInfo->m_pName, ".") != 0 && str_comp(pInfo->m_pName, "..") != 0)
			pEntries->push_back({pInfo->m_pName, IsDir != 0});
		return 0;
	}

	bool MoveDirectory(IStorage *pStorage, const char *pFrom, const char *pTo)
	{
		if(!pStorage->FolderExists(pFrom, IStorage::TYPE_SAVE))
			return true;
		if(PathIsLink(pStorage, pFrom) || PathIsLink(pStorage, pTo))
		{
			log_error("storage_layout", "Refusing to migrate linked directory '%s'", pFrom);
			return false;
		}
		if(!EnsureFolderPath(pStorage, pTo))
		{
			log_error("storage_layout", "Could not create migration directory '%s'", pTo);
			return false;
		}

		std::vector<STreeEntry> vEntries;
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pFrom, CollectTreeEntry, &vEntries);
		bool Success = true;
		for(const STreeEntry &Entry : vEntries)
		{
			char aSource[IO_MAX_PATH_LENGTH];
			char aTarget[IO_MAX_PATH_LENGTH];
			str_format(aSource, sizeof(aSource), "%s/%s", pFrom, Entry.m_Name.c_str());
			str_format(aTarget, sizeof(aTarget), "%s/%s", pTo, Entry.m_Name.c_str());
			Success &= Entry.m_IsDirectory ? MoveDirectory(pStorage, aSource, aTarget) : MoveFile(pStorage, aSource, aTarget);
		}
		if(Success && pStorage->FolderExists(pFrom, IStorage::TYPE_SAVE) && !pStorage->RemoveFolder(pFrom, IStorage::TYPE_SAVE))
		{
			log_error("storage_layout", "Migrated directory '%s' is not empty after migration", pFrom);
			Success = false;
		}
		return Success;
	}

	bool MoveDirectoryPreservingConflictsImpl(IStorage *pStorage, const char *pFrom, const char *pTo, const char *pLegacyRoot, const std::string &Relative)
	{
		if(!pStorage->FolderExists(pFrom, IStorage::TYPE_SAVE))
			return true;
		if(PathIsLink(pStorage, pFrom) || PathIsLink(pStorage, pTo))
		{
			log_error("storage_layout", "Refusing to migrate linked directory '%s'", pFrom);
			return false;
		}
		if(!EnsureFolderPath(pStorage, pTo))
		{
			log_error("storage_layout", "Could not create migration directory '%s'", pTo);
			return false;
		}

		std::vector<STreeEntry> vEntries;
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pFrom, CollectTreeEntry, &vEntries);
		bool Success = true;
		for(const STreeEntry &Entry : vEntries)
		{
			char aSource[IO_MAX_PATH_LENGTH];
			char aTarget[IO_MAX_PATH_LENGTH];
			str_format(aSource, sizeof(aSource), "%s/%s", pFrom, Entry.m_Name.c_str());
			str_format(aTarget, sizeof(aTarget), "%s/%s", pTo, Entry.m_Name.c_str());
			const std::string ChildRelative = Relative.empty() ? Entry.m_Name : Relative + "/" + Entry.m_Name;
			if(Entry.m_IsDirectory)
			{
				Success &= MoveDirectoryPreservingConflictsImpl(pStorage, aSource, aTarget, pLegacyRoot, ChildRelative);
				continue;
			}
			if(pStorage->FileExists(aTarget, IStorage::TYPE_SAVE) && !FilesEqual(pStorage, aSource, aTarget))
			{
				char aLegacy[IO_MAX_PATH_LENGTH];
				char aLegacyFolder[IO_MAX_PATH_LENGTH];
				const char *pSlash = str_rchr(ChildRelative.c_str(), '/');
				if(pSlash != nullptr)
				{
					str_format(aLegacyFolder, sizeof(aLegacyFolder), "%s/%.*s", pLegacyRoot, static_cast<int>(pSlash - ChildRelative.c_str()), ChildRelative.c_str());
					if(!EnsureFolderPath(pStorage, aLegacyFolder))
					{
						Success = false;
						continue;
					}
				}
				bool FoundName = false;
				for(int Index = 0; Index < 100; ++Index)
				{
					if(Index == 0)
						str_format(aLegacy, sizeof(aLegacy), "%s/%s", pLegacyRoot, ChildRelative.c_str());
					else
						str_format(aLegacy, sizeof(aLegacy), "%s/%s.%d", pLegacyRoot, ChildRelative.c_str(), Index);
					if(!pStorage->FileExists(aLegacy, IStorage::TYPE_SAVE))
					{
						FoundName = true;
						break;
					}
				}
				if(!FoundName || !MoveFile(pStorage, aSource, aLegacy))
				{
					log_error("storage_layout", "Could not preserve conflicting font '%s'", aSource);
					Success = false;
				}
				continue;
			}
			Success &= MoveFile(pStorage, aSource, aTarget);
		}
		if(Success && pStorage->FolderExists(pFrom, IStorage::TYPE_SAVE) && !pStorage->RemoveFolder(pFrom, IStorage::TYPE_SAVE))
		{
			log_error("storage_layout", "Migrated directory '%s' is not empty after migration", pFrom);
			Success = false;
		}
		return Success;
	}

	bool MoveDirectoryPreservingConflicts(IStorage *pStorage, const char *pFrom, const char *pTo, const char *pLegacyRoot)
	{
		return MoveDirectoryPreservingConflictsImpl(pStorage, pFrom, pTo, pLegacyRoot, {});
	}

	bool RemoveStaleFiles(IStorage *pStorage, const char *pDirectory, const char *pPrefix, const char *pSuffix)
	{
		struct SFilter
		{
			std::vector<std::string> m_vNames;
			const char *m_pPrefix;
			const char *m_pSuffix;
		};
		SFilter Filter{{}, pPrefix, pSuffix};
		pStorage->ListDirectory(
			IStorage::TYPE_SAVE, pDirectory,
			[](const char *pName, int IsDir, int, void *pUser) {
				auto *pFilterData = static_cast<SFilter *>(pUser);
				if(IsDir == 0 && str_startswith(pName, pFilterData->m_pPrefix) != nullptr &&
					str_endswith(pName, pFilterData->m_pSuffix) != nullptr)
					pFilterData->m_vNames.emplace_back(pName);
				return 0;
			},
			&Filter);
		bool Success = true;
		for(const std::string &Name : Filter.m_vNames)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s", pDirectory, Name.c_str());
			Success &= pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
		}
		return Success;
	}

	bool MigrateWorkshopCatalogs(IStorage *pStorage)
	{
		bool Success = true;
		for(const char *pName : WORKSHOP_CATALOGS)
		{
			char aOld[IO_MAX_PATH_LENGTH];
			char aOlder[IO_MAX_PATH_LENGTH];
			char aTarget[IO_MAX_PATH_LENGTH];
			str_format(aTarget, sizeof(aTarget), "%s/%s", qm_storage::CACHE_WORKSHOP_CATALOG_DIR, pName);
			str_format(aOld, sizeof(aOld), "qmclient/cache/%s", pName);
			str_format(aOlder, sizeof(aOlder), "cache/%s", pName);
			const char *apSources[] = {aOld, aOlder};
			Success &= MigrateNewestCacheFile(pStorage, apSources, 2, aTarget);
		}
		return Success;
	}
}

namespace qm_storage
{
	bool EnsureFolder(IStorage *pStorage, const char *pPath)
	{
		return EnsureFolderPath(pStorage, pPath);
	}

	bool EnsureStorageLayout(IStorage *pStorage)
	{
		if(pStorage == nullptr)
			return false;
		bool Success = true;
		for(const char *pDirectory : DIRECTORIES)
			Success &= pStorage->CreateFolder(pDirectory, IStorage::TYPE_SAVE) || pStorage->FolderExists(pDirectory, IStorage::TYPE_SAVE);
		return Success;
	}

	bool StorageLayoutMigrationPending(IStorage *pStorage)
	{
		return pStorage != nullptr && !pStorage->FileExists(LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE);
	}

	bool MigrateStorageLayout(IStorage *pStorage)
	{
		if(pStorage == nullptr || !EnsureStorageLayout(pStorage))
			return false;

		bool Success = true;
		for(const SPathMove &Move : FILE_MOVES)
			Success &= MoveFile(pStorage, Move.m_pFrom, Move.m_pTo);
		Success &= MigrateNewestCacheFile(pStorage, SERVERLIST_CACHE_SOURCES, std::size(SERVERLIST_CACHE_SOURCES), SERVERLIST_CACHE_FILE);
		for(const SPathMove &Move : DIRECTORY_MOVES)
			Success &= MoveDirectory(pStorage, Move.m_pFrom, Move.m_pTo);
		// 实时目录优先于历史归档；同时存在时不能让旧归档覆盖较新的用户字体。
		Success &= MoveDirectoryPreservingConflicts(pStorage, "qmclient/fonts", qm_storage::FONT_DIR, "qmclient/state/legacy/font_conflicts");
		Success &= MoveDirectoryPreservingConflicts(pStorage, "qmclient/content/fonts", qm_storage::FONT_STORE_DIR, "qmclient/state/legacy/content_font_conflicts");
		// 6bf2 曾把 qmclient/fonts 暂存到 legacy；最后回迁历史归档，冲突文件保留在 legacy。
		Success &= MoveDirectoryPreservingConflicts(pStorage, "qmclient/state/legacy/old_fonts", qm_storage::FONT_DIR, "qmclient/state/legacy/font_conflicts");
		Success &= MoveDirectoryPreservingConflicts(pStorage, "qmclient/state/legacy/old_content_fonts", qm_storage::FONT_STORE_DIR, "qmclient/state/legacy/content_font_conflicts");
		Success &= MigrateWorkshopCatalogs(pStorage);

		// 这些文件是本项目旧版原子写的残留，只删除明确前缀的临时文件。
		Success &= RemoveStaleFiles(pStorage, "cache", "qm-serverlist.json.", ".tmp");
		Success &= RemoveStaleFiles(pStorage, "qmclient/cache", "qm-serverlist.json.", ".tmp");
		Success &= RemoveStaleFiles(pStorage, "qmclient", ".save_probe_", "");
		Success &= RemoveStaleFiles(pStorage, "", "custom-communities-ddnet-info.json.", ".tmp");
		Success &= RemoveStaleFiles(pStorage, "", "qm_friend_last_seen.json.", ".tmp");
		// 强退或崩溃会留下 HTTP 原子写临时文件；本轮启动时进程尚未发起新请求，可安全清理。
		Success &= RemoveStaleFiles(pStorage, "", "ddnet-info.json.", ".tmp");

		// 只移除已搬空的旧容器，未知文件仍保留，函数失败时不写完成标记。
		for(const char *pDirectory : {"qmclient/workshop", "qmclient/mapcovers"})
		{
			if(pStorage->FolderExists(pDirectory, IStorage::TYPE_SAVE))
				pStorage->RemoveFolder(pDirectory, IStorage::TYPE_SAVE);
		}

		if(!Success)
		{
			log_error("storage_layout", "Storage layout migration is incomplete; conflicting files were preserved");
			return false;
		}

		if(!pStorage->FileExists(LAYOUT_MIGRATION_MARKER_FILE, IStorage::TYPE_SAVE))
		{
			IOHANDLE Marker = pStorage->OpenFile(LAYOUT_MIGRATION_MARKER_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(Marker == nullptr)
				return false;
			static constexpr const char *VERSION = "1\n";
			const bool Written = io_write(Marker, VERSION, str_length(VERSION)) == str_length(VERSION);
			const bool Closed = io_close(Marker) == 0;
			if(!Written || !Closed)
				return false;
		}
		log_info("storage_layout", "QmClient storage layout is ready at '%s'", ROOT_DIR);
		return true;
	}
}
