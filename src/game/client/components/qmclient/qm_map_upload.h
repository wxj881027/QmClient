#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MAP_UPLOAD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MAP_UPLOAD_H

#include <base/system.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace QmMapUpload
{
	constexpr size_t MAX_MAP_SIZE = 64 * 1024 * 1024;
	constexpr size_t MAX_RESPONSE_SIZE = 16 * 1024;

	inline bool IsMapFilename(const char *pFilename)
	{
		if(!pFilename)
			return false;
		const int Length = str_length(pFilename);
		return Length >= 4 && str_comp_nocase(pFilename + Length - 4, ".map") == 0;
	}

	bool ValidateFilename(const char *pFilename);
	bool BuildMultipart(const char *pFilename, const char *pPlayerName, const unsigned char *pData, size_t Size, const char *pBoundary, std::string &Body);

	struct SMapFile
	{
		char m_aFilename[IO_MAX_PATH_LENGTH] = "";
		char m_aPath[IO_MAX_PATH_LENGTH] = "";
		bool m_IsDirectory = false;
		int m_StorageType = IStorage::TYPE_ALL;
	};

	inline bool MatchesMapName(const SMapFile &File, const char *pQuery)
	{
		return !File.m_IsDirectory && str_utf8_find_nocase(File.m_aFilename, pQuery ? pQuery : "") != nullptr;
	}

	// 增量建立 maps/downloadedmaps 索引。每帧扫描一个目录，查询只过滤缓存。
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
			const SScanContext &Context = *static_cast<SScanContext *>(pUser);
			if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0 || (!IsDir && !IsMapFilename(pInfo->m_pName)))
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
			for(int StorageType = 0; StorageType < NumStoragePaths; ++StorageType)
				for(const char *pRoot : {"maps", "downloadedmaps"})
				{
					SMapFile Folder;
					str_copy(Folder.m_aPath, pRoot);
					Folder.m_StorageType = StorageType;
					m_PendingFolders.push_back(Folder);
				}
		}
		bool Busy() const { return !m_PendingFolders.empty(); }
		bool ScanNext(IStorage *pStorage)
		{
			if(!pStorage || !Busy())
				return false;
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
			for(const SMapFile &File : m_vMaps)
				if(MatchesMapName(File, pQuery))
					vMatches.push_back(File);
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

	struct SResponse
	{
		bool m_Valid = false;
		bool m_Success = false;
		std::string m_Message;
	};

	SResponse ParseResponse(int StatusCode, const char *pData, size_t Length);

	enum class EStatus
	{
		IDLE,
		UPLOADING,
		SUCCESS,
		CANCELLED,
		INVALID_FILE,
		TOO_LARGE,
		READ_FAILED,
		NETWORK_ERROR,
		SERVER_ERROR,
		INVALID_RESPONSE,
		MISSING_PLAYER,
		INVALID_ENDPOINT,
	};

	// 后台准备文件，主线程只负责发布请求和收取结果；endpoint 由本地调用方提供。
	class CUpload
	{
		class CPrepareJob;
		std::shared_ptr<CPrepareJob> m_pPrepareJob;
		IHttp *m_pHttp = nullptr;
		std::shared_ptr<IHttpRequest> m_pRequest;
		EStatus m_Status = EStatus::IDLE;
		std::string m_Detail;
		int m_StatusCode = 0;

	public:
		CUpload() = default;
		CUpload(const CUpload &) = delete;
		CUpload &operator=(const CUpload &) = delete;
		~CUpload();

		bool Busy() const { return m_pPrepareJob != nullptr || m_pRequest != nullptr; }
		EStatus Status() const { return m_Status; }
		const std::string &Detail() const { return m_Detail; }
		int StatusCode() const { return m_StatusCode; }
		void Reset();
		void Start(IStorage *pStorage, IHttp *pHttp, IEngine *pEngine, const char *pEndpoint, const char *pPath, int StorageType, const char *pPlayerName);
		void Poll();
		void Cancel();
	};
}

#endif
