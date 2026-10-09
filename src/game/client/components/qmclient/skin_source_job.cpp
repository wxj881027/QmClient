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

CQmSkinDownloadJob::CQmSkinDownloadJob(IStorage *pStorage, IHttp *pHttp, const char *pName, const char *pBaseUrl, TPrepare Prepare, bool UseCache, TCreateRequest CreateRequest) :
	CQmSkinSourceJob(pName),
	m_pStorage(pStorage),
	m_pHttp(pHttp),
	m_BaseUrl(pBaseUrl),
	m_Prepare(Prepare),
	m_UseCache(UseCache),
	m_CreateRequest(CreateRequest)
{
}

bool CQmSkinDownloadJob::Abort()
{
	if(!CQmSkinSourceJob::Abort())
		return false;
	const CLockScope LockScope(m_Lock);
	if(m_pGetRequest)
		m_pGetRequest->Abort();
	return true;
}

void CQmSkinDownloadJob::StartUpdate()
{
	char aEscapedName[256];
	EscapeUrl(aEscapedName, m_aName);
	char aUrl[IO_MAX_PATH_LENGTH], aPath[IO_MAX_PATH_LENGTH];
	str_format(aUrl, sizeof(aUrl), "%s%s.png", m_BaseUrl.c_str(), aEscapedName);
	str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", m_aName);
	Download(aUrl, aPath, true, false);
}

bool CQmSkinDownloadJob::DownloadReady()
{
	const CLockScope LockScope(m_Lock);
	return m_pGetRequest != nullptr && m_pGetRequest->Done();
}

std::shared_ptr<IHttpRequest> CQmSkinDownloadJob::Download(const char *pUrl, const char *pPath, bool SkipByFileTime, bool Wait)
{
	std::shared_ptr<IHttpRequest> pGet = m_CreateRequest(pUrl);
	pGet->WriteToFileAndMemory(m_pStorage, pPath, IStorage::TYPE_SAVE);
	pGet->Timeout(CTimeout{10000, 0, 8192, 10});
	pGet->MaxResponseSize(10 * 1024 * 1024);
	pGet->ValidateBeforeOverwrite(true);
	pGet->SkipByFileTime(SkipByFileTime);
	pGet->LogProgress(HTTPLOG::NONE);
	pGet->FailOnErrorStatus(false);
	{
		const CLockScope LockScope(m_Lock);
		// 取消可能发生在构造请求之前，不能在取消之后再提交新的下载。
		if(State() == STATE_ABORTED)
			return nullptr;
		m_pGetRequest = pGet;
		m_pHttp->Run(pGet);
	}
	if(!Wait)
		return pGet;
	pGet->Wait();
	{
		const CLockScope LockScope(m_Lock);
		m_pGetRequest.reset();
	}
	return pGet;
}

void CQmSkinDownloadJob::Run()
{
	if(State() == STATE_ABORTED)
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", m_aName);
	if(m_UseCache)
	{
		m_Data.ReadLastModified(m_pStorage, aPath, IStorage::TYPE_SAVE);
		void *pPngData = nullptr;
		unsigned PngSize = 0;
		if(m_pStorage->ReadFile(aPath, IStorage::TYPE_SAVE, &pPngData, &PngSize))
		{
			const bool Decoded = CImageLoader::LoadPng(pPngData, PngSize, aPath, m_Data.m_Info);
			free(pPngData);
			if(State() == STATE_ABORTED)
				return;
			if(Decoded && m_Prepare(m_aName, m_Data))
			{
				m_UsedCachedSkin = true;
				return;
			}
			m_Data = {};
		}
	}

	char aEscapedName[256];
	EscapeUrl(aEscapedName, m_aName);
	char aUrl[IO_MAX_PATH_LENGTH];
	str_format(aUrl, sizeof(aUrl), "%s%s.png", m_BaseUrl.c_str(), aEscapedName);
	std::shared_ptr<IHttpRequest> pGet;
	if(m_UseCache)
		pGet = Download(aUrl, aPath, true);
	else
	{
		// HTTP 在独立线程完成后才占用解码 worker，慢网络不会堵住新的缓存任务。
		const CLockScope LockScope(m_Lock);
		if(m_pGetRequest == nullptr || !m_pGetRequest->Done())
			return;
		pGet = std::move(m_pGetRequest);
	}
	if(pGet == nullptr || State() == STATE_ABORTED)
	{
		if(pGet != nullptr)
			pGet->OnValidation(false);
		return;
	}
	if(pGet->State() != EHttpState::DONE || pGet->StatusCode() >= 400)
	{
		m_NotFound = pGet->State() == EHttpState::DONE && pGet->StatusCode() == 404;
		pGet->OnValidation(false);
		return;
	}
	if(pGet->StatusCode() == 304)
	{
		if(!m_UseCache)
		{
			m_NotModified = true;
			pGet->OnValidation(true);
			return;
		}
		// 缓存缺失或损坏时，304 不能提供像素，必须无条件重新下载。
		pGet->OnValidation(false);
		pGet = Download(aUrl, aPath, false);
		if(pGet == nullptr || State() == STATE_ABORTED)
		{
			if(pGet != nullptr)
				pGet->OnValidation(false);
			return;
		}
		if(pGet->State() != EHttpState::DONE || pGet->StatusCode() >= 400 || pGet->StatusCode() == 304)
		{
			m_NotFound = pGet->State() == EHttpState::DONE && pGet->StatusCode() == 404;
			pGet->OnValidation(false);
			return;
		}
	}
	unsigned char *pResult = nullptr;
	size_t ResultSize = 0;
	pGet->Result(&pResult, &ResultSize);
	const bool Success = CImageLoader::LoadPng(pResult, ResultSize, aUrl, m_Data.m_Info) &&
			     State() != STATE_ABORTED && m_Prepare(m_aName, m_Data);
	if(!Success)
	{
		m_Data = {};
		log_error("skins", "Failed to load downloaded skin '%s'", m_aName);
	}
	pGet->OnValidation(Success && State() != STATE_ABORTED);
	if(Success)
		m_Data.ReadLastModified(m_pStorage, aPath, IStorage::TYPE_SAVE);
}
