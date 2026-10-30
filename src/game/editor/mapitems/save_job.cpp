#include "save_job.h"

#include <base/log.h>
#include <base/system.h>

#include <game/localization.h>

#include <utility>

void CDataFileWriterFinishJob::Run()
{
	if(!m_Writer.Finish())
	{
		str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not write map file '%s'.", "Editor"), m_aRealFilename);
		log_error("editor/save", "%s", m_aErrorMessage);
		return;
	}

	char aBackupFilename[2 * IO_MAX_PATH_LENGTH];
	if(IStorage::ReplaceFileSafely(m_pStorage, m_aTempFilename, m_aRealFilename, aBackupFilename, sizeof(aBackupFilename)))
	{
		log_trace("editor/save", Localize("saving '%s' done", "Editor"), m_aRealFilename);
		return;
	}

	if(aBackupFilename[0] != '\0')
		str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not replace map file '%s'; the previous version was restored or kept at '%s'.", "Editor"), m_aRealFilename, aBackupFilename);
	else
		str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not move temporary map file '%s' to '%s'.", "Editor"), m_aTempFilename, m_aRealFilename);
	log_error("editor/save", "%s", m_aErrorMessage);
}

CDataFileWriterFinishJob::CDataFileWriterFinishJob(IStorage *pStorage, CEditorMap *pMap, const char *pRealFilename, const char *pTempFilename, CDataFileWriter &&Writer) :
	m_pStorage(pStorage),
	m_pMap(pMap),
	m_Writer(std::move(Writer))
{
	str_copy(m_aRealFilename, pRealFilename);
	str_copy(m_aTempFilename, pTempFilename);
	m_aErrorMessage[0] = '\0';
}
