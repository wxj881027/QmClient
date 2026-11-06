#ifndef GAME_EDITOR_MAPITEMS_SAVE_JOB_H
#define GAME_EDITOR_MAPITEMS_SAVE_JOB_H

#include <engine/shared/datafile.h>
#include <engine/shared/jobs.h>

class CEditorMap;

class CDataFileWriterFinishJob : public IJob
{
public:
	class IOperations
	{
	public:
		virtual ~IOperations() = default;
		virtual bool Replace(IStorage &Storage, const char *pTemporary, const char *pDestination, char *pBackup, unsigned BackupSize);
	};

private:
	IOperations m_DefaultOperations;
	IOperations *const m_pOperations;
	IStorage *m_pStorage;
	CEditorMap *m_pMap;
	char m_aRealFilename[IO_MAX_PATH_LENGTH];
	char m_aTempFilename[IO_MAX_PATH_LENGTH];
	char m_aErrorMessage[2 * IO_MAX_PATH_LENGTH + 128];
	CDataFileWriter m_Writer;

protected:
	void Run() override;

public:
	// 注入的发布操作只借用，须比作业存活更久。
	CDataFileWriterFinishJob(IStorage *pStorage, CEditorMap *pMap, const char *pRealFilename, const char *pTempFilename, CDataFileWriter &&Writer, IOperations *pOperations = nullptr);
	CEditorMap *Map() const { return m_pMap; }
	const char *RealFilename() const { return m_aRealFilename; }
	const char *ErrorMessage() const { return m_aErrorMessage; }
};

#endif
