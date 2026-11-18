#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_SAVE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_SAVE_JOB_H

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/shared/qm_storage_layout.h>

#include <mutex>
#include <string>
#include <utility>

// 快照只读；后台仅负责文件操作，不访问组件和正在变化的统计缓存。
class CQmStatisticsSaveJob : public IJob
{
	IStorage *m_pStorage;
	std::string m_Filename;
	std::string m_Content;
	std::mutex m_Mutex;
	bool m_Attempted = false;
	bool m_Success = false;

	void Run() override { Finish(); }

public:
	CQmStatisticsSaveJob(IStorage *pStorage, std::string Filename, std::string Content) :
		m_pStorage(pStorage),
		m_Filename(std::move(Filename)),
		m_Content(std::move(Content)) {}

	// 手动保存和退出可接管排队中的任务；同一快照最多写入一次。
	bool Finish()
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Attempted)
			return m_Success;
		m_Attempted = true;
		if(!m_pStorage)
			return false;
		const char *pSlash = str_rchr(m_Filename.c_str(), '/');
		if(pSlash != nullptr)
			qm_storage::EnsureFolder(m_pStorage, std::string(m_Filename.c_str(), pSlash - m_Filename.c_str()).c_str());
		char aTempFilename[IO_MAX_PATH_LENGTH];
		IStorage::FormatTmpPath(aTempFilename, sizeof(aTempFilename), m_Filename.c_str());
		IOHANDLE File = m_pStorage->OpenFile(aTempFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		bool Written = io_write(File, m_Content.data(), m_Content.size()) == m_Content.size();
		if(io_sync(File) != 0)
			Written = false;
		if(io_close(File) != 0)
			Written = false;
		if(Written)
		{
			char aBackupFilename[2 * IO_MAX_PATH_LENGTH];
			m_Success = IStorage::ReplaceFileSafely(m_pStorage, aTempFilename, m_Filename.c_str(), aBackupFilename, sizeof(aBackupFilename));
		}
		if(!m_Success)
			m_pStorage->RemoveFile(aTempFilename, IStorage::TYPE_SAVE);
		return m_Success;
	}
};

#endif
