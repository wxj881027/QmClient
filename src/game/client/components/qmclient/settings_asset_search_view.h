#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_SEARCH_VIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_SEARCH_VIEW_H

#include <game/client/components/qmclient/settings_asset_queue.h>
#include <game/client/components/settings_resource_jobs.h>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

// View只借用Owned；调用方须在替换或移动条目前清除旧借用，不能按data地址判断代际。
// Keep只读取条目，不得修改View或Owned；同owner的准备失败保留已发布视图。
template<typename TItem, typename TViewAllocator, typename TOwnedAllocator, typename TPredicate>
bool TryBuildSettingsAssetSearchView(std::vector<TItem *, TViewAllocator> &View, std::vector<TItem, TOwnedAllocator> &Owned, TPredicate &&Keep)
{
	try
	{
		std::vector<TItem *, TViewAllocator> Candidate(View.get_allocator());
		Candidate.reserve(Owned.size());
		for(TItem &Item : Owned)
		{
			if(Keep(std::as_const(Item)))
				Candidate.push_back(&Item);
		}
		View.swap(Candidate);
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

// 收尾只向当前owner认领；筛选为空或搜索候选延期都不能隐藏仍存活的资源。
template<typename TItem, typename TAllocator>
TItem *FindSettingsAssetPreviewOwner(std::vector<TItem, TAllocator> &Owned, int ExpectedTab, const SSettingsAssetPreviewHandle &Handle) noexcept
{
	if(Handle.m_Tab != ExpectedTab)
		return nullptr;
	const auto Matches = [&Handle](const TItem &Item) noexcept {
		return Item.m_PreviewEpoch == Handle.m_Epoch && SettingsAssetPreviewHandleReferencesAsset(Handle, Item.m_aName);
	};
	if(Handle.m_Index < Owned.size() && Matches(Owned[Handle.m_Index]))
		return &Owned[Handle.m_Index];
	for(TItem &Item : Owned)
	{
		if(Matches(Item))
			return &Item;
	}
	return nullptr;
}

// READY像素仍由Item拥有；完整跟踪记录准备成功后才发布新的认领字段。
template<typename TItem, typename TQueue, typename TSet>
bool TryQueueSettingsAssetReadyPreview(TItem &Item, int Tab, unsigned Epoch, size_t ViewIndex, bool HighPriority, TQueue &Queue, TSet &Queued, bool PromoteExisting = true)
{
	if(Item.m_PreviewState != TItem::PREVIEW_STATE_READY || Item.m_PreviewImage.m_pData == nullptr)
		return false;
	const size_t Index = Item.m_PreviewEpoch == Epoch ? Item.m_PreviewListIndex : ViewIndex;
	return SettingsAssetQueueTryStartKeyed(Queue, Queued, HighPriority, [&] { return SSettingsAssetPreviewHandle{Tab, Epoch, Index, Item.m_aName}; }, [](const auto &Handle) { return SettingsAssetPreviewHandleKey(Handle); }, [](const auto &Left, const auto &Right) { return SettingsAssetPreviewHandleMatches(Left, Right.m_Tab, Right.m_Epoch, Right.m_Index, Right.m_Name.c_str()); }, [&Item, Epoch, Index, HighPriority]() noexcept {
			Item.m_PreviewEpoch = Epoch;
			Item.m_PreviewListIndex = Index;
			Item.m_PreviewHighPriority = HighPriority;
			return true; }, PromoteExisting);
}

#endif
