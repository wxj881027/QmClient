#include "save_job.h"

#include <base/log.h>
#include <base/system.h>

#include <game/localization.h>

#include <utility>

bool CDataFileWriterFinishJob::IOperations::Replace(IStorage &Storage, const char *pTemporary, const char *pDestination, char *pBackup, unsigned BackupSize)
{
	return IStorage::ReplaceFileSafely(&Storage, pTemporary, pDestination, pBackup, BackupSize);
}

void CDataFileWriterFinishJob::Run()
{
	// 先建立无需分配的失败结果，翻译、日志或发布抛异常也不能被消费为成功。
	str_copy(m_aErrorMessage, "Saving failed: Could not finish saving the map.");
	char aBackupFilename[2 * IO_MAX_PATH_LENGTH]{};
	try
	{
		if(!m_Writer.Finish())
		{
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not write map file '%s'.", "Editor"), m_aRealFilename);
			log_error("editor/save", "%s", m_aErrorMessage);
			return;
		}

		if(m_pOperations->Replace(*m_pStorage, m_aTempFilename, m_aRealFilename, aBackupFilename, sizeof(aBackupFilename)))
		{
			// 文件发布已经完成，日志异常不改变本次成功结果。
			m_aErrorMessage[0] = '\0';
			log_trace("editor/save", Localize("saving '%s' done", "Editor"), m_aRealFilename);
			return;
		}

		if(aBackupFilename[0] != '\0')
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not replace map file '%s'; the previous version was restored or kept at '%s'.", "Editor"), m_aRealFilename, aBackupFilename);
		else
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), Localize("Saving failed: Could not move temporary map file '%s' to '%s'.", "Editor"), m_aTempFilename, m_aRealFilename);
		log_error("editor/save", "%s", m_aErrorMessage);
	}
	catch(...)
	{
		// 主线程按已有错误结果恢复未保存状态；已成功发布则保留成功结果。
		if(m_aErrorMessage[0] != '\0' && aBackupFilename[0] != '\0')
			str_format(m_aErrorMessage, sizeof(m_aErrorMessage), "Saving failed: Could not finish saving '%s'. The previous file may be at '%s'.", m_aRealFilename, aBackupFilename);
	}
}

CDataFileWriterFinishJob::CDataFileWriterFinishJob(IStorage *pStorage, CEditorMap *pMap, const char *pRealFilename, const char *pTempFilename, CDataFileWriter &&Writer, IOperations *pOperations) :
	m_pOperations(pOperations ? pOperations : &m_DefaultOperations),
	m_pStorage(pStorage),
	m_pMap(pMap),
	m_Writer(std::move(Writer))
{
	str_copy(m_aRealFilename, pRealFilename);
	str_copy(m_aTempFilename, pTempFilename);
	m_aErrorMessage[0] = '\0';
}
