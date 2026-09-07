/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DISPATCH_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_DISPATCH_LOGIC_H

#include "qm_render_slots.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

enum class EQmUpdateSlot
{
	UPDATE,
	COUNT,
};

// 只规定 Qm 内部输入顺序：槽位数值越小越先处理，同槽位 priority 越大越先处理。
// 不改变官方组件的输入顺序。
enum class EQmInputSlot
{
	MODAL,
	UI,
	GAMEPLAY,
	COUNT,
};

enum class EQmDispatchRegistration
{
	REGISTERED,
	FROZEN,
	INVALID_SLOT,
	INVALID_ID,
	DUPLICATE_ID,
	CAPACITY_EXCEEDED,
};

enum class EQmDispatchResult
{
	COMPLETED,
	CONSUMED,
	NOT_FROZEN,
	INVALID_SLOT,
};

// 注册表只拥有调度描述，不保存 enabled、输入、回调或业务状态。
// 单线程初始化后冻结；使用期间不得替换或销毁注册表，也不得在回调内重新赋值。
template<typename TSlot, std::size_t Capacity = 32>
class CQmDispatchRegistry
{
	static constexpr std::size_t SLOT_COUNT = static_cast<std::size_t>(TSlot::COUNT);
	static_assert(Capacity > 0, "Dispatch capacity must be positive");
	static_assert(SLOT_COUNT > 0, "Dispatch needs at least one slot");

public:
	static constexpr std::size_t MAX_ID_LENGTH = 63;

	class CEntry
	{
		friend class CQmDispatchRegistry;
		std::array<char, MAX_ID_LENGTH + 1> m_aId{};

	public:
		TSlot m_Slot{};
		int m_Priority = 0;
		// runtime 自己定义的窄分支索引；不是业务状态，也不要求全局唯一。
		std::size_t m_UserIndex = 0;

		std::string_view Id() const { return m_aId.data(); }
	};

private:
	std::array<CEntry, Capacity> m_aEntries{};
	std::array<std::size_t, SLOT_COUNT + 1> m_aOffsets{};
	std::size_t m_Size = 0;
	bool m_Frozen = false;

	static constexpr bool ValidSlot(TSlot Slot)
	{
		return static_cast<std::size_t>(Slot) < SLOT_COUNT;
	}

public:
	// ID 按值复制，调用方的临时字符串可在注册返回后释放。每个注册表内 ID 唯一。
	EQmDispatchRegistration Register(TSlot Slot, int Priority, std::string_view Id, std::size_t UserIndex = 0)
	{
		if(m_Frozen)
			return EQmDispatchRegistration::FROZEN;
		if(!ValidSlot(Slot))
			return EQmDispatchRegistration::INVALID_SLOT;
		if(Id.empty() || Id.size() > MAX_ID_LENGTH || Id.find('\0') != std::string_view::npos)
			return EQmDispatchRegistration::INVALID_ID;
		for(std::size_t i = 0; i < m_Size; ++i)
		{
			if(m_aEntries[i].Id() == Id)
				return EQmDispatchRegistration::DUPLICATE_ID;
		}
		if(m_Size == Capacity)
			return EQmDispatchRegistration::CAPACITY_EXCEEDED;
		CEntry &Entry = m_aEntries[m_Size++];
		Entry.m_Slot = Slot;
		Entry.m_Priority = Priority;
		Entry.m_UserIndex = UserIndex;
		std::copy(Id.begin(), Id.end(), Entry.m_aId.begin());
		return EQmDispatchRegistration::REGISTERED;
	}

	// 冻结幂等；插入排序仅在初始化运行，排序与分发均不申请堆内存。
	void Freeze()
	{
		if(m_Frozen)
			return;
		const auto Before = [](const CEntry &Left, const CEntry &Right) {
			if(Left.m_Slot != Right.m_Slot)
				return Left.m_Slot < Right.m_Slot;
			if(Left.m_Priority != Right.m_Priority)
			{
				if constexpr(std::is_same_v<TSlot, EQmInputSlot>)
					return Left.m_Priority > Right.m_Priority;
				return Left.m_Priority < Right.m_Priority;
			}
			return Left.Id() < Right.Id();
		};
		for(std::size_t i = 1; i < m_Size; ++i)
		{
			const CEntry Entry = m_aEntries[i];
			std::size_t Position = i;
			while(Position > 0 && Before(Entry, m_aEntries[Position - 1]))
			{
				m_aEntries[Position] = m_aEntries[Position - 1];
				--Position;
			}
			m_aEntries[Position] = Entry;
		}
		std::size_t Position = 0;
		for(std::size_t Slot = 0; Slot < SLOT_COUNT; ++Slot)
		{
			m_aOffsets[Slot] = Position;
			while(Position < m_Size && static_cast<std::size_t>(m_aEntries[Position].m_Slot) == Slot)
				++Position;
		}
		m_aOffsets[SLOT_COUNT] = m_Size;
		m_Frozen = true;
	}

	bool Frozen() const { return m_Frozen; }
	std::size_t Size() const { return m_Size; }

	// enabled 只读取 owner 的状态；输入构造、计时、logic、render 和任务启动全部放进 Run。
	// 这里只遍历目标槽位的连续区间；关闭项不会进入 Run。生命周期清理由 owner 单独负责。
	template<typename TEnabled, typename TRun>
	EQmDispatchResult Dispatch(TSlot Slot, TEnabled &&Enabled, TRun &&Run) const
	{
		if(!ValidSlot(Slot))
			return EQmDispatchResult::INVALID_SLOT;
		if(!m_Frozen)
			return EQmDispatchResult::NOT_FROZEN;
		const std::size_t Index = static_cast<std::size_t>(Slot);
		for(std::size_t i = m_aOffsets[Index]; i < m_aOffsets[Index + 1]; ++i)
		{
			const CEntry &Entry = m_aEntries[i];
			if(Enabled(Entry))
				Run(Entry);
		}
		return EQmDispatchResult::COMPLETED;
	}

	// 用于输入：按槽位、priority、ID 遍历，消费后连后续 enabled 也不再调用。
	// Run 在门控后才构造各 feature 输入；原始官方事件可由 lambda 借用，不在此存储。
	template<typename TEnabled, typename TRun>
	EQmDispatchResult DispatchUntilConsumed(TEnabled &&Enabled, TRun &&Run) const
	{
		if(!m_Frozen)
			return EQmDispatchResult::NOT_FROZEN;
		for(std::size_t i = 0; i < m_Size; ++i)
		{
			const CEntry &Entry = m_aEntries[i];
			if(Enabled(Entry) && Run(Entry))
				return EQmDispatchResult::CONSUMED;
		}
		return EQmDispatchResult::COMPLETED;
	}
};

using CQmRenderDispatch = CQmDispatchRegistry<EQmRenderSlot>;
using CQmUpdateDispatch = CQmDispatchRegistry<EQmUpdateSlot>;
using CQmInputDispatch = CQmDispatchRegistry<EQmInputSlot>;

#endif
