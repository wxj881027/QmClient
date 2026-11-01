#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_STORAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_STORAGE_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/tclient/map_history.h>

#include <limits>
#include <memory>
#include <string>

namespace qm_map_history_storage
{
	inline constexpr const char *FILE_PATH = "qmclient/map_history.json";

	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual std::string Serialize(const QmMapHistory::CMapHistory &History) { return History.ToJson(); }
		virtual IOHANDLE Open(IStorage &Storage, const char *pPath) { return Storage.OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		// 无论返回值如何，Close均消费句柄且不抛异常。
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// false或异常表示尚未发布；不能通过删除旧目标重试。
		virtual bool Publish(IStorage &Storage, const char *pTemporary)
		{
			return Storage.RenameFile(pTemporary, FILE_PATH, IStorage::TYPE_SAVE, false);
		}
	};

	struct SFileCloser
	{
		CFileOperations &m_Operations;
		void operator()(void *pFile) const noexcept { m_Operations.Close(pFile); }
	};

	struct STemporaryFile
	{
		IStorage &m_Storage;
		const char *m_pPath;
		bool m_Owned = false;
		STemporaryFile(IStorage &Storage, const char *pPath) :
			m_Storage(Storage), m_pPath(pPath) {}
		STemporaryFile(const STemporaryFile &) = delete;
		STemporaryFile &operator=(const STemporaryFile &) = delete;
		~STemporaryFile()
		{
			if(m_Owned)
			{
				try
				{
					m_Storage.RemoveFile(m_pPath, IStorage::TYPE_SAVE);
				}
				catch(...)
				{
				}
			}
		}
	};

	inline bool Save(IStorage *pStorage, const QmMapHistory::CMapHistory &History, bool &Dirty, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage)
				return false;
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			// 扩容失败时尚未创建候选，更不能改动已保存的历史。
			const std::string Json = Operations.Serialize(History);
			if(Json.size() > std::numeric_limits<unsigned>::max())
				return false;
			char aTemporary[IO_MAX_PATH_LENGTH];
			IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), FILE_PATH);
			char aRoot[IO_MAX_PATH_LENGTH] = "";
			pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			if(!aRoot[0] || str_length(aRoot) + str_length(aTemporary) + 1 >= IO_MAX_PATH_LENGTH ||
				pStorage->FileExists(aTemporary, IStorage::TYPE_SAVE) || pStorage->FolderExists(aTemporary, IStorage::TYPE_SAVE))
				return false;
			if(!pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE) && !pStorage->FolderExists("qmclient", IStorage::TYPE_SAVE))
				return false;
			STemporaryFile Candidate{*pStorage, aTemporary};
			// 确认不存在后接管创建尝试，打开过程失败也清理本次候选。
			Candidate.m_Owned = true;
			std::unique_ptr<void, SFileCloser> File(Operations.Open(*pStorage, aTemporary), {Operations});
			if(!File)
				return false;
			const bool Written = Operations.Write(File.get(), Json.data(), static_cast<unsigned>(Json.size())) == Json.size();
			const bool Synced = Operations.Sync(File.get()) == 0;
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Written || !Synced || !Clean || !Closed || !Operations.Publish(*pStorage, aTemporary))
				return false;
			Candidate.m_Owned = false;
			Dirty = false;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
