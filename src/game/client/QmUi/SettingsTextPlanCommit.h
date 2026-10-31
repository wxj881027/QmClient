#ifndef GAME_CLIENT_QMUI_SETTINGSTEXTPLANCOMMIT_H
#define GAME_CLIENT_QMUI_SETTINGSTEXTPLANCOMMIT_H

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

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
