#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_MERGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_LIST_MERGE_H

#include <base/system.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// 既有列表须已按资源名排序；准备回调只填写新条目，不发布外部资源。
template<typename TItem, typename TAllocator, typename TPrepare>
bool TryMergeSettingsAssetEntries(std::vector<TItem, TAllocator> &vList, const std::vector<CMenus::SSettingsAssetMergeEntry> &vEntries, TPrepare &&Prepare)
{
	static_assert(std::is_base_of_v<CMenus::SCustomItem, TItem>);
	static_assert(std::is_nothrow_move_constructible_v<TItem>);
	static_assert(std::is_nothrow_move_assignable_v<TItem>);

	try
	{
		std::vector<TItem, TAllocator> vNewItems(vList.get_allocator());
		for(const CMenus::SSettingsAssetMergeEntry &Entry : vEntries)
		{
			const auto SameName = [&Entry](const TItem &Item) {
				return str_comp(Item.m_aName, Entry.m_aName) == 0;
			};
			if(std::any_of(vList.begin(), vList.end(), SameName) || std::any_of(vNewItems.begin(), vNewItems.end(), SameName))
				continue;
			if(vNewItems.empty())
				vNewItems.reserve(vEntries.size());
			TItem Item;
			str_copy(Item.m_aName, Entry.m_aName);
			Prepare(Item, Entry);
			vNewItems.push_back(std::move(Item));
		}
		if(vNewItems.empty())
			return true;

		std::stable_sort(vNewItems.begin(), vNewItems.end(), [](const TItem &Left, const TItem &Right) {
			return AssetResourceNameLess(Left.m_aName, Right.m_aName);
		});
		if(vNewItems.size() > vList.max_size() - vList.size())
			return false;
		std::vector<TItem, TAllocator> vMerged(vList.get_allocator());
		vMerged.reserve(vList.size() + vNewItems.size());

		// 所有可失败准备已完成；此后只转移像素和句柄，再整体交换列表。
		size_t OldIndex = 0;
		size_t NewIndex = 0;
		while(OldIndex < vList.size() && NewIndex < vNewItems.size())
		{
			if(AssetResourceNameLess(vNewItems[NewIndex].m_aName, vList[OldIndex].m_aName))
				vMerged.push_back(std::move(vNewItems[NewIndex++]));
			else
				vMerged.push_back(std::move(vList[OldIndex++]));
		}
		while(OldIndex < vList.size())
			vMerged.push_back(std::move(vList[OldIndex++]));
		while(NewIndex < vNewItems.size())
			vMerged.push_back(std::move(vNewItems[NewIndex++]));
		vList.swap(vMerged);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

// 发布回调须具备失败保留旧结果的事务边界，且不能重入修改原预算。
template<typename TPublish>
bool TryPublishSettingsAssetMergeBatch(const std::vector<CMenus::SSettingsAssetMergeEntry> &vEntries, size_t &Cursor, SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget, TPublish &&Publish)
{
	if(Cursor >= vEntries.size())
		return true;
	try
	{
		SSettingsResourceMergeBudget NextBudget = Budget;
		SSettingsWarmupFrameBudget NextFrameBudget = pFrameBudget ? *pFrameBudget : SSettingsWarmupFrameBudget{};
		SSettingsWarmupFrameBudget *pNextFrameBudget = pFrameBudget ? &NextFrameBudget : nullptr;
		const size_t MaxBatchSize = std::min(vEntries.size() - Cursor, static_cast<size_t>(std::max(0, Budget.m_MaxListEntries)));
		std::vector<CMenus::SSettingsAssetMergeEntry> vBatch;
		size_t NextCursor = Cursor;
		while(NextCursor < vEntries.size() && SettingsResourceConsumeMergeEntry(NextBudget, pNextFrameBudget))
		{
			if(vBatch.empty())
				vBatch.reserve(MaxBatchSize);
			vBatch.push_back(vEntries[NextCursor++]);
		}
		if(vBatch.empty())
		{
			// 预算阻断仍给出原有停顿原因，但未发布就不消耗计数。
			Budget.m_StopReason = NextBudget.m_StopReason;
			if(pFrameBudget)
				pFrameBudget->m_StopReason = NextFrameBudget.m_StopReason;
			return true;
		}
		if(!Publish(vBatch))
			return false;
		Cursor = NextCursor;
		Budget = NextBudget;
		if(pFrameBudget)
			*pFrameBudget = NextFrameBudget;
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

#endif
