#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_SAVE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_SAVE_JOB_H

#include <base/system.h>

#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// 快照只读；后台仅负责文件操作，不访问组件和正在变化的统计缓存。
class CQmStatisticsSaveJob : public IJob
{
public:
	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual void CreateFolder(IStorage &Storage) { (void)Storage.CreateFolder("qmclient", IStorage::TYPE_SAVE); }
		virtual IOHANDLE Open(IStorage &Storage, const char *pPath) { return Storage.OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		// 无论结果如何均消费句柄；关闭边界不抛异常。
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
		{
			// 同目录候选直接替换；禁止先移走或删除旧目标。
			return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
		}
		virtual void Remove(IStorage &Storage, const char *pPath) { (void)Storage.RemoveFile(pPath, IStorage::TYPE_SAVE); }
	};

private:
	IStorage *m_pStorage;
	std::string m_Filename;
	std::string m_Content;
	std::mutex m_Mutex;
	bool m_Attempted = false;
	bool m_Success = false;
	CFileOperations m_DefaultOperations;
	std::shared_ptr<CFileOperations> m_pOperations;

	void Run() override { Finish(); }

public:
	CQmStatisticsSaveJob(IStorage *pStorage, std::string Filename, std::string Content, std::shared_ptr<CFileOperations> pOperations = {}) :
		m_pStorage(pStorage),
		m_Filename(std::move(Filename)),
		m_Content(std::move(Content)),
		m_pOperations(std::move(pOperations)) {}

	// 手动保存和退出可接管排队中的任务；同一快照最多写入一次。
	bool Finish() noexcept
	try
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Attempted)
			return m_Success;
		m_Attempted = true;
		if(!m_pStorage || m_Content.size() > std::numeric_limits<unsigned>::max())
			return false;
		auto &Operations = m_pOperations ? *m_pOperations : m_DefaultOperations;
		Operations.CreateFolder(*m_pStorage);
		char aTempFilename[IO_MAX_PATH_LENGTH];
		IStorage::FormatTmpPath(aTempFilename, sizeof(aTempFilename), m_Filename.c_str());
		struct STemporary
		{
			IStorage &m_Storage;
			CFileOperations &m_Operations;
			const char *m_pPath;
			bool m_Owned = false;
			~STemporary()
			{
				if(m_Owned)
				{
					try
					{
						m_Operations.Remove(m_Storage, m_pPath);
					}
					catch(...)
					{
					}
				}
			}
		} Candidate{*m_pStorage, Operations, aTempFilename};
		const auto Close = [&Operations](void *pFile) noexcept { (void)Operations.Close(pFile); };
		std::unique_ptr<void, decltype(Close)> File(Operations.Open(*m_pStorage, aTempFilename), Close);
		if(!File)
			return false;
		Candidate.m_Owned = true;
		const bool Written = Operations.Write(File.get(), m_Content.data(), static_cast<unsigned>(m_Content.size())) == m_Content.size();
		const bool Synced = Operations.Sync(File.get()) == 0;
		const bool Clean = Operations.Error(File.get()) == 0;
		const bool Closed = Operations.Close(File.release()) == 0;
		m_Success = Written && Synced && Clean && Closed && Operations.Publish(*m_pStorage, aTempFilename, m_Filename.c_str());
		Candidate.m_Owned = !m_Success;
		return m_Success;
	}
	catch(...)
	{
		return false;
	}
};

#endif
