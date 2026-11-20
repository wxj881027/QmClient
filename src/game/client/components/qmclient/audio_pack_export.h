#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_EXPORT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_EXPORT_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace qm_audio_pack
{
	class CExportFileOperations
	{
	public:
		virtual ~CExportFileOperations() = default;
		virtual bool ReadAll(IOHANDLE File, void **ppData, unsigned *pSize) { return io_read_all(File, ppData, pSize); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Flush(IOHANDLE File) { return io_flush(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// false或异常表示尚未发布；禁止删除旧目标后重试重命名。
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
		{
			return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
		}
	};

	inline std::string ExportTemporaryPath(const char *pTarget)
	{
		return std::string(pTarget) + ".qm-export." + std::to_string(pid()) + ".tmp";
	}

	inline bool ValidExportStoragePath(const char *pPath)
	{
		// 与存储写入的相对路径限制一致，先校验，避免目录创建先越出保存根。
		return pPath && pPath[0] && pPath[0] != '/' && pPath[0] != '\\' &&
		       !str_find(pPath, "../") && !str_find(pPath, "..\\")
#if defined(CONF_FAMILY_WINDOWS)
		       && pPath[1] != ':'
#endif
			;
	}

	inline bool EnsureExportDirectories(IStorage &Storage, const char *pPath)
	{
		std::string Directory;
		for(const char *pCursor = pPath; *pCursor; ++pCursor)
		{
			if(*pCursor == '/' && !Directory.empty() && !Storage.FolderExists(Directory.c_str(), IStorage::TYPE_SAVE) &&
				!Storage.CreateFolder(Directory.c_str(), IStorage::TYPE_SAVE))
				return false;
			Directory.push_back(*pCursor);
		}
		return true;
	}

	struct CExportFileCloser
	{
		CExportFileOperations *m_pOperations;
		void operator()(void *pFile) const noexcept { m_pOperations->Close(pFile); }
	};

	struct CExportReadBuffer
	{
		void *m_pData = nullptr;
		~CExportReadBuffer() { free(m_pData); }
	};

	struct CExportTemporaryFile
	{
		IStorage &m_Storage;
		const std::string &m_Path;
		bool m_Owned = false;
		~CExportTemporaryFile()
		{
			if(m_Owned)
			{
				try
				{
					m_Storage.RemoveFile(m_Path.c_str(), IStorage::TYPE_SAVE);
				}
				catch(...)
				{
				}
			}
		}
	};

	inline bool ExportFile(IStorage *pStorage, const char *pSource, int SourceType, const char *pTarget, CExportFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage || !pSource || !pSource[0] || !ValidExportStoragePath(pTarget))
				return false;
			const std::string Temporary = ExportTemporaryPath(pTarget);
			char aRoot[IO_MAX_PATH_LENGTH] = "";
			pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			if(!aRoot[0] || str_length(aRoot) + Temporary.size() + 1 >= IO_MAX_PATH_LENGTH ||
				pStorage->FileExists(Temporary.c_str(), IStorage::TYPE_SAVE) || pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE))
				return false;
			if(!pStorage->FileExists(pSource, SourceType) || !EnsureExportDirectories(*pStorage, pTarget))
				return false;

			CExportFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			std::unique_ptr<void, CExportFileCloser> Source(pStorage->OpenFile(pSource, IOFLAG_READ, SourceType), {&Operations});
			if(!Source)
				return false;
			CExportReadBuffer Buffer;
			unsigned Size = 0;
			const bool Read = Operations.ReadAll(Source.get(), &Buffer.m_pData, &Size);
			const bool SourceClosed = Operations.Close(Source.release()) == 0;
			if(!Read || !Buffer.m_pData || !SourceClosed)
				return false;

			CExportTemporaryFile Candidate{*pStorage, Temporary};
			std::unique_ptr<void, CExportFileCloser> Destination(pStorage->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE), {&Operations});
			if(!Destination)
				return false;
			Candidate.m_Owned = true;
			const bool Written = Operations.Write(Destination.get(), Buffer.m_pData, Size) == Size;
			const bool Flushed = Operations.Flush(Destination.get()) == 0;
			const bool Clean = Operations.Error(Destination.get()) == 0;
			const bool DestinationClosed = Operations.Close(Destination.release()) == 0;
			if(!Written || !Flushed || !Clean || !DestinationClosed || !Operations.Publish(*pStorage, Temporary.c_str(), pTarget))
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
