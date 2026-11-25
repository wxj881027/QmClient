// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_json_tracked_allocator.h"

#include <cstdlib>

qm_json::CTrackedJsonAllocator::~CTrackedJsonAllocator()
{
	ReleaseRemaining();
}

void qm_json::CTrackedJsonAllocator::Bind(json_settings *pSettings) noexcept
{
	pSettings->user_data = this;
	pSettings->mem_alloc = Alloc;
	pSettings->mem_free = Free;
}

void *qm_json::CTrackedJsonAllocator::Alloc(size_t Size, int Zero, void *pUser) noexcept
{
	auto *pThis = static_cast<CTrackedJsonAllocator *>(pUser);
	// 头部和用户区一次分配：多要一个链表节，返回的指针整体后移。
	void *pMemory = pThis->Allocate(Size + sizeof(SBlock), Zero);
	pThis->m_Failed |= pMemory == nullptr;
	if(pMemory == nullptr)
		return nullptr;
	auto *pBlock = static_cast<SBlock *>(pMemory);
	pBlock->m_pPrev = nullptr;
	pBlock->m_pNext = pThis->m_pHead;
	if(pThis->m_pHead != nullptr)
		pThis->m_pHead->m_pPrev = pBlock;
	pThis->m_pHead = pBlock;
	return pBlock + 1;
}

void qm_json::CTrackedJsonAllocator::Free(void *pMemory, void *pUser) noexcept
{
	if(pMemory == nullptr)
		return;
	auto *pThis = static_cast<CTrackedJsonAllocator *>(pUser);
	auto *pBlock = static_cast<SBlock *>(pMemory) - 1;
	if(pBlock->m_pPrev != nullptr)
		pBlock->m_pPrev->m_pNext = pBlock->m_pNext;
	else
		pThis->m_pHead = pBlock->m_pNext;
	if(pBlock->m_pNext != nullptr)
		pBlock->m_pNext->m_pPrev = pBlock->m_pPrev;
	std::free(pBlock);
}

void qm_json::CTrackedJsonAllocator::ReleaseRemaining() noexcept
{
	while(m_pHead != nullptr)
	{
		SBlock *pBlock = m_pHead;
		m_pHead = pBlock->m_pNext;
		std::free(pBlock);
	}
}

void qm_json::STrackedJsonDeleter::operator()(json_value *pValue) const
{
	if(pValue == nullptr)
		return;
	// m_pAllocator 让 m_Settings.user_data 指向的对象活到树释放完。
	json_settings Settings = m_Settings;
	json_value_free_ex(&Settings, pValue);
}
