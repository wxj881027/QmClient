#include "skin_source_job.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <cstdlib>

void SQmSkinSourceData::ReadLastModified(IStorage *pStorage, const char *pPath, int StorageType)
{
	time_t Created = 0, Modified = 0;
	m_LastModified.reset();
	if(StorageType == IStorage::TYPE_ALL)
	{
		for(int Type = 0; Type < pStorage->NumPaths(); ++Type)
		{
			if(pStorage->RetrieveTimes(pPath, Type, &Created, &Modified))
			{
				m_LastModified = Modified;
				return;
			}
		}
		return;
	}
	if(pStorage->RetrieveTimes(pPath, StorageType, &Created, &Modified))
		m_LastModified = Modified;
}

CQmSkinDownloadJob::CQmSkinDownloadJob(IStorage *pStorage, const char *pName, TPrepare Prepare, std::shared_ptr<IHttpRequest> pResponse) :
	CQmSkinSourceJob(pName),
	m_pStorage(pStorage),
	m_Prepare(Prepare),
	m_pResponse(std::move(pResponse))
{
	if(m_pResponse && m_pResponse->State() == EHttpState::DONE && m_pResponse->StatusCode() == 200)
	{
		unsigned char *pResult = nullptr;
		m_pResponse->Result(&pResult, &m_ResponseSize);
		m_pResponseData = pResult;
	}
}

void CQmSkinDownloadJob::Run()
{
	if(State() == STATE_ABORTED)
		return;

	// 完整预处理成功且未取消后才发布，失败与取消的临时像素由 RAII 回收。
	SQmSkinSourceData Data;
	bool Decoded = false;
	if(m_pResponse)
	{
		if(m_pResponseData == nullptr || m_ResponseSize == 0)
			return;
		Decoded = CImageLoader::LoadPng(m_pResponseData, m_ResponseSize, m_aName, Data.m_Info);
	}
	else
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", m_aName);
		Data.ReadLastModified(m_pStorage, aPath, IStorage::TYPE_SAVE);
		void *pPngData = nullptr;
		unsigned PngSize = 0;
		if(!m_pStorage->ReadFile(aPath, IStorage::TYPE_SAVE, &pPngData, &PngSize))
			return;
		if(State() != STATE_ABORTED)
			Decoded = CImageLoader::LoadPng(pPngData, PngSize, aPath, Data.m_Info);
		free(pPngData);
	}
	if(State() == STATE_ABORTED)
		return;
	if(!Decoded || !m_Prepare(m_aName, Data))
	{
		log_error("skins", "Failed to load skin '%s'", m_aName);
		return;
	}
	if(State() == STATE_ABORTED)
		return;
	m_Data = std::move(Data);
}
