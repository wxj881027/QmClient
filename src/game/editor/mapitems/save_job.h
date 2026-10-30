#ifndef GAME_EDITOR_MAPITEMS_SAVE_JOB_H
#define GAME_EDITOR_MAPITEMS_SAVE_JOB_H

#include <engine/shared/datafile.h>
#include <engine/shared/jobs.h>

class CEditorMap;

class CDataFileWriterFinishJob : public IJob
{
	IStorage *m_pStorage;
	CEditorMap *m_pMap;
	char m_aRealFilename[IO_MAX_PATH_LENGTH];
	char m_aTempFilename[IO_MAX_PATH_LENGTH];
	char m_aErrorMessage[2 * IO_MAX_PATH_LENGTH + 128];
	CDataFileWriter m_Writer;

protected:
	void Run() override;

public:
	CDataFileWriterFinishJob(IStorage *pStorage, CEditorMap *pMap, const char *pRealFilename, const char *pTempFilename, CDataFileWriter &&Writer);
	CEditorMap *Map() const { return m_pMap; }
	const char *RealFilename() const { return m_aRealFilename; }
	const char *ErrorMessage() const { return m_aErrorMessage; }
};

#endif
