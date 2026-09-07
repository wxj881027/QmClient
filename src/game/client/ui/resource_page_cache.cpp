/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "resource_page_cache.h"

#include <cstring>

namespace
{
SResourcePageState *FindPage(std::map<std::string, SResourcePageState, std::less<>> &Pages, const char *pId)
{
	if(!pId || pId[0] == '\0')
		return nullptr;
	const auto It = Pages.find(pId);
	return It == Pages.end() ? nullptr : &It->second;
}
}

bool CResourcePageCache::RegisterPage(const char *pId)
{
	if(!pId || pId[0] == '\0' || Find(pId))
		return false;
	m_Pages.emplace(pId, SResourcePageState{pId});
	return true;
}

bool CResourcePageCache::BeginLoad(const char *pId, uint32_t Generation)
{
	SResourcePageState *pPage = FindPage(m_Pages, pId);
	if(!pPage)
		return false;
	if(pPage->m_State == EResourcePageState::READY && pPage->m_Generation == Generation)
		return false;
	if(pPage->m_State == EResourcePageState::LOADING && pPage->m_Generation == Generation)
		return false;
	pPage->m_State = EResourcePageState::LOADING;
	pPage->m_Generation = Generation;
	pPage->m_RequestId = ++m_NextRequestId;
	pPage->m_Error.clear();
	return true;
}

bool CResourcePageCache::IsCurrentLoad(const char *pId, uint32_t Generation, uint64_t RequestId) const
{
	const SResourcePageState *pPage = Find(pId);
	return pPage && pPage->m_State == EResourcePageState::LOADING &&
		pPage->m_Generation == Generation && pPage->m_RequestId == RequestId;
}

bool CResourcePageCache::CompleteLoad(const char *pId, uint32_t Generation, uint64_t RequestId, bool Success, const char *pError)
{
	SResourcePageState *pPage = FindPage(m_Pages, pId);
	if(!IsCurrentLoad(pId, Generation, RequestId))
		return false;
	pPage->m_State = Success ? EResourcePageState::READY : EResourcePageState::FAILED;
	pPage->m_Error = Success || !pError ? "" : pError;
	return true;
}

bool CResourcePageCache::NeedsLoad(const char *pId, uint32_t Generation) const
{
	const SResourcePageState *pPage = Find(pId);
	return pPage == nullptr || pPage->m_Generation != Generation || (pPage->m_State != EResourcePageState::LOADING && pPage->m_State != EResourcePageState::READY);
}

void CResourcePageCache::Invalidate(uint32_t Generation)
{
	for(auto &[Id, Page] : m_Pages)
	{
		Page.m_State = EResourcePageState::UNLOADED;
		Page.m_Generation = Generation;
		Page.m_RequestId = 0;
		Page.m_Error.clear();
	}
}

void CResourcePageCache::InvalidatePage(const char *pId, uint32_t Generation)
{
	if(auto *pPage = FindPage(m_Pages, pId))
	{
		pPage->m_State = EResourcePageState::UNLOADED;
		pPage->m_Generation = Generation;
		pPage->m_RequestId = 0;
		pPage->m_Error.clear();
	}
}

void CResourcePageCache::UnregisterPage(const char *pId)
{
	if(pId)
		m_Pages.erase(pId);
}

void CResourcePageCache::Clear()
{
	m_Pages.clear();
}

const SResourcePageState *CResourcePageCache::Find(const char *pId) const
{
	if(!pId || pId[0] == '\0')
		return nullptr;
	const auto It = m_Pages.find(pId);
	return It == m_Pages.end() ? nullptr : &It->second;
}
