// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_JSON_TRACKED_ALLOCATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_JSON_TRACKED_ALLOCATOR_H

#include <engine/external/json-parser/json.h>

#include <cstddef>
#include <memory>

namespace qm_json
{
	// json-parser 在内存预算或分配失败时，只回收得掉它还能追溯到的块：第二趟解析里
	// 已经取出、尚未挂进树的节点会留在堆上。一条 64KB 的密集数组被预算拒绝，漏掉的
	// 就是十几 MB 的半成品树。
	//
	// 这里给每次分配加一个双向链表头，解析结束后统一回收残余块：
	// - 解析失败：库释放得掉的部分走 Free 顺带摘链，剩下的由本对象析构释放；
	// - 解析成功：树会被 shared_ptr 长期持有，所以 deleter 也持有本对象的引用，
	//   保证树释放完之前链头一直有效，不会留下指向已销毁对象的 user_data。
	class CTrackedJsonAllocator
	{
	public:
		CTrackedJsonAllocator() = default;
		CTrackedJsonAllocator(const CTrackedJsonAllocator &) = delete;
		CTrackedJsonAllocator &operator=(const CTrackedJsonAllocator &) = delete;
		virtual ~CTrackedJsonAllocator();

		// 写入 json_settings 的 user_data/mem_alloc/mem_free；本对象必须活到树释放完。
		void Bind(json_settings *pSettings) noexcept;
		// 库是否报告过分配失败（预算耗尽或真 OOM），调用方据此区分失败原因。
		bool Failed() const { return m_Failed; }

	protected:
		virtual void *Allocate(size_t Size, int Zero) noexcept = 0;

	private:
		struct SBlock
		{
			SBlock *m_pNext;
			SBlock *m_pPrev;
		};
		static void *Alloc(size_t Size, int Zero, void *pUser) noexcept;
		static void Free(void *pMemory, void *pUser) noexcept;
		void ReleaseRemaining() noexcept;

		SBlock *m_pHead = nullptr;
		bool m_Failed = false;
	};

	// 与 CTrackedJsonAllocator 绑定的 json 释放器：把 allocator 的引用一起带进树的
	// 所有者，保证 free 时链头仍然有效。
	struct STrackedJsonDeleter
	{
		std::shared_ptr<CTrackedJsonAllocator> m_pAllocator;
		json_settings m_Settings{};

		void operator()(json_value *pValue) const;
	};
} // namespace qm_json

#endif
