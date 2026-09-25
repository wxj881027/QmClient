#ifndef GAME_CLIENT_QMUI_CARDS_QMMAPUPLOADSEARCH_H
#define GAME_CLIENT_QMUI_CARDS_QMMAPUPLOADSEARCH_H

#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/qm_map_upload.h>

#include <algorithm>
#include <deque>
#include <vector>

namespace qm_map_upload
{
	struct SMapFile
	{
		char m_aFilename[IO_MAX_PATH_LENGTH] = "";
		char m_aPath[IO_MAX_PATH_LENGTH] = "";
		bool m_IsDirectory = false;
		int m_StorageType = IStorage::TYPE_ALL;
	};

	inline bool MatchesMapName(const SMapFile &File, const char *pQuery)
	{
		return !File.m_IsDirectory && str_utf8_find_nocase(File.m_aFilename, pQuery) != nullptr;
	}

	class CSearchIndex
	{
		std::deque<SMapFile> m_PendingFolders;
		std::vector<SMapFile> m_vMaps;

		struct SScanContext
		{
			CSearchIndex *m_pIndex;
			const char *m_pFolder;
		};

		static int Scan(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
		{
			const auto &Context = *static_cast<SScanContext *>(pUser);
			if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0 ||
				(!IsDir && !IsMapFilename(pInfo->m_pName)))
				return 0;
			SMapFile File;
			if(str_length(Context.m_pFolder) + str_length(pInfo->m_pName) + 2 > (int)sizeof(File.m_aPath))
				return 0;
			str_copy(File.m_aFilename, pInfo->m_pName);
			str_format(File.m_aPath, sizeof(File.m_aPath), "%s/%s", Context.m_pFolder, pInfo->m_pName);
			File.m_IsDirectory = IsDir != 0;
			File.m_StorageType = StorageType;
			if(File.m_IsDirectory)
				Context.m_pIndex->m_PendingFolders.push_back(File);
			else
				Context.m_pIndex->m_vMaps.push_back(File);
			return 0;
		}

	public:
		void Reset(int NumStoragePaths)
		{
			m_PendingFolders.clear();
			m_vMaps.clear();
			// 分别枚举存储位置，避免 TYPE_ALL 合并同名目录而漏掉其中的地图。
			for(int StorageType = 0; StorageType < NumStoragePaths; ++StorageType)
			{
				for(const char *pRoot : {"maps", "downloadedmaps"})
				{
					SMapFile Folder;
					str_copy(Folder.m_aPath, pRoot);
					Folder.m_StorageType = StorageType;
					m_PendingFolders.push_back(Folder);
				}
			}
		}

		bool Busy() const { return !m_PendingFolders.empty(); }

		bool ScanNext(IStorage *pStorage)
		{
			if(!Busy())
				return false;
			// 每帧只扫描一个目录；改变搜索词时只筛选缓存，不重复访问磁盘。
			const SMapFile Folder = m_PendingFolders.front();
			m_PendingFolders.pop_front();
			const size_t PreviousSize = m_vMaps.size();
			SScanContext Context{this, Folder.m_aPath};
			pStorage->ListDirectoryInfo(Folder.m_StorageType, Folder.m_aPath, Scan, &Context);
			return m_vMaps.size() != PreviousSize;
		}

		std::vector<SMapFile> Find(const char *pQuery) const
		{
			std::vector<SMapFile> vMatches;
			for(const auto &File : m_vMaps)
			{
				if(MatchesMapName(File, pQuery))
					vMatches.push_back(File);
			}
			std::stable_sort(vMatches.begin(), vMatches.end(), [](const SMapFile &Left, const SMapFile &Right) {
				const int NameOrder = str_comp_filenames(Left.m_aFilename, Right.m_aFilename);
				if(NameOrder != 0)
					return NameOrder < 0;
				const int PathOrder = str_comp_filenames(Left.m_aPath, Right.m_aPath);
				return PathOrder != 0 ? PathOrder < 0 : Left.m_StorageType < Right.m_StorageType;
			});
			return vMatches;
		}
	};
}

#endif // GAME_CLIENT_QMUI_CARDS_QMMAPUPLOADSEARCH_H
