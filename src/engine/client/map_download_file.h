#ifndef ENGINE_CLIENT_MAP_DOWNLOAD_FILE_H
#define ENGINE_CLIENT_MAP_DOWNLOAD_FILE_H

#include <base/system.h>

#include <engine/storage.h>

#include <utility>

class CMapDownloadFileOperations
{
public:
	virtual ~CMapDownloadFileOperations() = default;
	virtual IOHANDLE Open(IStorage &Storage, const char *pFilename) { return Storage.OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE); }
	virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
	virtual int Error(IOHANDLE File) { return io_error(File); }
	// 即使返回失败，关闭操作也已消费句柄；注入的操作须遵循相同合同。
	virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
};

class CMapDownloadFile
{
	CMapDownloadFileOperations m_DefaultOperations;
	CMapDownloadFileOperations *const m_pOperations;
	IOHANDLE m_File = nullptr;
	bool m_Failed = false;

public:
	// 注入的文件操作只借用，必须比本对象存活更久。
	explicit CMapDownloadFile(CMapDownloadFileOperations *pOperations = nullptr) :
		m_pOperations(pOperations ? pOperations : &m_DefaultOperations) {}
	CMapDownloadFile(const CMapDownloadFile &) = delete;
	CMapDownloadFile &operator=(const CMapDownloadFile &) = delete;
	~CMapDownloadFile() { Close(); }
	explicit operator bool() const { return m_File != nullptr; }

	bool Open(IStorage &Storage, const char *pFilename)
	{
		if(m_File)
			return false;
		try
		{
			m_File = m_pOperations->Open(Storage, pFilename);
			if(m_File)
				m_Failed = false;
			return m_File != nullptr;
		}
		catch(...)
		{
			return false;
		}
	}

	bool Write(const void *pData, unsigned Size)
	{
		if(!m_File || m_Failed)
			return false;
		if(!pData || Size == 0)
		{
			m_Failed = true;
			return false;
		}
		try
		{
			m_Failed = m_pOperations->Write(m_File, pData, Size) != Size || m_pOperations->Error(m_File) != 0;
			return !m_Failed;
		}
		catch(...)
		{
			m_Failed = true;
			return false;
		}
	}

	bool Close() noexcept
	{
		IOHANDLE File = std::exchange(m_File, nullptr);
		if(!File)
			return true;
		bool Failed = std::exchange(m_Failed, false);
		try
		{
			Failed = m_pOperations->Error(File) != 0 || Failed;
		}
		catch(...)
		{
			Failed = true;
		}
		const bool Closed = m_pOperations->Close(File) == 0;
		return !Failed && Closed;
	}
};

namespace map_download
{
	inline bool CanAppend(int Amount, int Size, int Total)
	{
		return Amount >= 0 && Total >= Amount && Size > 0 && Size <= Total - Amount;
	}

	inline bool Publish(IStorage &Storage, const char *pCandidate, const char *pTarget)
	{
		try
		{
			return Storage.RenameFile(pCandidate, pTarget, IStorage::TYPE_SAVE, false);
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
