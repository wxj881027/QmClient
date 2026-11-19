#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_IDENTITY_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_IDENTITY_FILE_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <memory>
#include <string_view>

namespace qm_identity_file
{
	inline constexpr const char *CLIENT_ID_FILE = "qmclient/playtime_client_id.txt";
	inline constexpr const char *MACHINE_ID_FILE = "qmclient/voice_machine_id.txt";

	enum class EFile
	{
		CLIENT_ID,
		MACHINE_ID,
	};

	inline const char *Filename(EFile File)
	{
		switch(File)
		{
		case EFile::CLIENT_ID: return CLIENT_ID_FILE;
		case EFile::MACHINE_ID: return MACHINE_ID_FILE;
		}
		return nullptr;
	}

	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual IOHANDLE Open(IStorage &Storage, const char *pPath) { return Storage.OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// 拒收或异常时不得先删除原文件。
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
		{
			return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
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

	// 同步保存内部已校验的身份值，保留原有单行文本格式；失败不改内存身份。
	inline bool Save(IStorage *pStorage, EFile FileKind, std::string_view Value, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			const char *pTarget = Filename(FileKind);
			if(!pStorage || !pTarget || Value.empty() || Value.size() > 64 || Value.find_first_of(std::string_view("\0\r\n", 3)) != std::string_view::npos)
				return false;
			char aTemporary[IO_MAX_PATH_LENGTH];
			IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), pTarget);
			char aRoot[IO_MAX_PATH_LENGTH] = "";
			pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			if(!aRoot[0] || str_length(aRoot) + str_length(aTemporary) + 1 >= IO_MAX_PATH_LENGTH ||
				pStorage->FileExists(aTemporary, IStorage::TYPE_SAVE) || pStorage->FolderExists(aTemporary, IStorage::TYPE_SAVE))
				return false;
			if(!pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE) && !pStorage->FolderExists("qmclient", IStorage::TYPE_SAVE))
				return false;
			char aContent[65];
			mem_copy(aContent, Value.data(), Value.size());
			aContent[Value.size()] = '\n';
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			STemporaryFile Candidate{*pStorage, aTemporary};
			std::unique_ptr<void, SFileCloser> File(Operations.Open(*pStorage, aTemporary), {Operations});
			if(!File)
				return false;
			Candidate.m_Owned = true;
			const unsigned Size = static_cast<unsigned>(Value.size() + 1);
			const bool Written = Operations.Write(File.get(), aContent, Size) == Size;
			const bool Synced = Operations.Sync(File.get()) == 0;
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Written || !Synced || !Clean || !Closed || !Operations.Publish(*pStorage, aTemporary, pTarget))
				return false;
			Candidate.m_Owned = false;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
