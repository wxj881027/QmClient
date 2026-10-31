#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPLANCOMMIT_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPLANCOMMIT_H

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

// 仅借用菜单已有进度字段，视图不能比字段所属对象活得更久。
struct SSettingsTextPlanProgressView
{
	size_t &m_TextCursor;
	size_t &m_CollectionCursor;
	std::vector<std::pair<int, int>> &m_vGlyphs;
	size_t &m_GlyphCursor;
	size_t &m_GlyphPlanCursor;
	bool &m_CollectionDirty;
	bool &m_CollectionComplete;
	bool &m_VisiblePlanReady;
	std::array<int, 3> &m_VisibleContext;

	void RestartConsumption() const noexcept
	{
		m_TextCursor = 0;
		m_vGlyphs.clear();
		m_GlyphCursor = 0;
		m_GlyphPlanCursor = static_cast<size_t>(-1);
	}
	// 已准备好新单元时Dirty为false；替换/失效后须重新准备时为true。
	void RestartCollection(bool Dirty) const noexcept
	{
		RestartConsumption();
		m_CollectionCursor = 0;
		m_CollectionDirty = Dirty;
		m_CollectionComplete = false;
		m_VisiblePlanReady = false;
	}
	void ReplaceVisiblePlan(const std::array<int, 3> &Context) const noexcept
	{
		RestartCollection(true);
		// 空可见页也是已准备结果，不能每帧重建并重置后续收集进度。
		m_VisibleContext = Context;
		m_VisiblePlanReady = true;
	}
	bool VisiblePlanMatches(const std::array<int, 3> &Context) const noexcept
	{
		return m_VisiblePlanReady && m_VisibleContext == Context;
	}
};

inline int SettingsTextPlanPendingUnitCount(size_t UnitCount, size_t Cursor, bool Dirty) noexcept
{
	const size_t Remaining = UnitCount > Cursor ? UnitCount - Cursor : 0;
	// dirty还需重建，即使旧单元为空/已经走完，也不能报告无待办。
	const size_t Pending = Dirty ? std::max(size_t{1}, Remaining) : Remaining;
	return static_cast<int>(std::min(Pending, static_cast<size_t>(std::numeric_limits<int>::max())));
}

// 单元先收集到候选，文本和两套索引全部准备成功才共同发布。
// 失败保留值；reserve可能改变容量和引用，调用方不能跨此调用保存容器迭代器。
template<typename TItem, typename FCollect, typename FKeys>
bool TryCollectSettingsTextPlan(std::vector<TItem> &vPlan, std::unordered_set<std::string> &Descriptors, std::unordered_set<std::string> &Keys, FCollect &&Collect, FKeys &&MakeKeys) noexcept
{
	static_assert(std::is_nothrow_move_constructible_v<TItem> && std::is_nothrow_destructible_v<TItem>);
	static_assert(noexcept(std::declval<std::hash<std::string>>()(std::declval<const std::string &>())));
	std::vector<const std::string *> vAddedDescriptors, vAddedKeys;
	try
	{
		std::vector<TItem> vCandidates;
		std::forward<FCollect>(Collect)(vCandidates);
		const size_t Count = vCandidates.size();
		if(Count == 0)
			return true;
		if(Count > vPlan.max_size() - vPlan.size() || Count > Descriptors.max_size() - Descriptors.size() || Count > Keys.max_size() - Keys.size())
			throw std::length_error("settings text plan capacity");
		vAddedDescriptors.reserve(Count);
		vAddedKeys.reserve(Count);
		// 预留最坏新增数量；回滚只借用节点值地址，rehash也不会使其失效。
		Descriptors.reserve(Descriptors.size() + Count);
		Keys.reserve(Keys.size() + Count);
		const size_t Required = vPlan.size() + Count;
		if(Required > vPlan.capacity())
		{
			const size_t Capacity = vPlan.capacity();
			vPlan.reserve(std::max(Required, Capacity > vPlan.max_size() / 2 ? vPlan.max_size() : Capacity * 2));
		}
		for(const TItem &Item : vCandidates)
		{
			auto ItemKeys = MakeKeys(Item);
			if(!ItemKeys)
				continue;
			auto Descriptor = Descriptors.insert(std::move(ItemKeys->first));
			if(Descriptor.second)
				vAddedDescriptors.push_back(&*Descriptor.first);
			auto Key = Keys.insert(std::move(ItemKeys->second));
			if(Key.second)
				vAddedKeys.push_back(&*Key.first);
		}
		// 此后只进行预留容量内的无抛移动；游标由调用方在成功后推进。
		for(TItem &Item : vCandidates)
			vPlan.push_back(std::move(Item));
		return true;
	}
	catch(...)
	{
		// 只撤销本批新节点，重复命中已有索引时不能误删旧状态。
		for(const std::string *pKey : vAddedKeys)
			Keys.erase(*pKey);
		for(const std::string *pDescriptor : vAddedDescriptors)
			Descriptors.erase(*pDescriptor);
		return false;
	}
}

#endif
