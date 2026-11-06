/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "countryflags.h"

#include <base/log.h>

#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

CCountryFlags::CCountryFlagLoadJob::CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage, size_t FlagIndex) :
	m_Path(pPath),
	m_CountryCode(CountryCode),
	m_FlagIndex(FlagIndex),
	m_pStorage(pStorage)
{
}

void CCountryFlags::CCountryFlagLoadJob::Run()
{
	const ELoadStatus Status = Load(
		[&](void **ppData, unsigned *pSize) { return m_pStorage->ReadFile(m_Path.c_str(), IStorage::TYPE_ALL, ppData, pSize); },
		[&](const void *pData, unsigned Size, CImageInfo &Image) { return CImageLoader::LoadPng(pData, Size, m_Path.c_str(), Image); });
	// 诊断也可能失败；完成状态已经发布，不会因此留下永久等待的作业。
	switch(Status)
	{
	case ELoadStatus::SUCCESS:
		break;
	case ELoadStatus::READ_FAILED:
		log_error("countryflags", "Failed to read flag file '%s'", m_Path.c_str());
		break;
	case ELoadStatus::DECODE_FAILED:
		log_error("countryflags", "Failed to decode flag PNG '%s'", m_Path.c_str());
		break;
	case ELoadStatus::EXCEPTION:
		log_error("countryflags", "Exception while loading flag file '%s'", m_Path.c_str());
		break;
	}
}
