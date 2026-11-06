#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_HIERARCHY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_HIERARCHY_H

#include <game/client/components/assets_resource_registry.h>

#include <algorithm>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

template<typename TItem>
struct SSettingsAssetHierarchyCandidate
{
	std::vector<std::string> m_vSourceNames;
	std::unordered_map<std::string, EEntityBgHierarchyEntrySource> m_vSourceKinds;
	std::vector<TItem> m_vItems;
	char m_aCurrentFolder[IO_MAX_PATH_LENGTH] = "";
};

inline bool SettingsAssetSearchFilterMatches(const char *pFilter, const char *pName, const char *pAuthor)
{
	return pFilter == nullptr || pFilter[0] == '\0' ||
	       (pName != nullptr && str_utf8_find_nocase(pName, pFilter)) ||
	       (pAuthor != nullptr && pAuthor[0] != '\0' && str_utf8_find_nocase(pAuthor, pFilter));
}

// 来源同步与作者查询只写候选；全部分配和排序完成前不动已发布列表。
template<typename TItem, typename TSyncSources, typename TPopulateAuthor>
std::optional<SSettingsAssetHierarchyCandidate<TItem>> TryPrepareSettingsAssetHierarchy(
	const std::vector<std::string> &vSourceNames,
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> &vSourceKinds,
	const char *pCurrentFolder, bool ShowWorkshop, bool ForceShowWorkshopFolder, const char *pFilter,
	TSyncSources &&SyncSources, TPopulateAuthor &&PopulateAuthor)
try
{
	static_assert(std::is_nothrow_move_constructible_v<TItem>);
	SSettingsAssetHierarchyCandidate<TItem> Candidate;
	Candidate.m_vSourceNames = vSourceNames;
	Candidate.m_vSourceKinds = vSourceKinds;
	str_copy(Candidate.m_aCurrentFolder, pCurrentFolder != nullptr ? pCurrentFolder : "");
	EnsureDefaultAssetVisible(Candidate.m_vSourceNames);
	Candidate.m_vSourceKinds["default"] = EEntityBgHierarchyEntrySource::LOCAL;
	std::forward<TSyncSources>(SyncSources)(Candidate.m_vSourceNames, Candidate.m_vSourceKinds);
	if(!ShowWorkshop && IsEntityBgWorkshopFolderOrChildPath(Candidate.m_aCurrentFolder))
		Candidate.m_aCurrentFolder[0] = '\0';

	std::vector<SEntityBgHierarchyEntry> vEntries;
	if(pFilter == nullptr || pFilter[0] == '\0')
	{
		vEntries = BuildEntityBgHierarchyEntries(Candidate.m_vSourceNames, Candidate.m_aCurrentFolder, ShowWorkshop, &Candidate.m_vSourceKinds, ForceShowWorkshopFolder);
	}
	else
	{
		for(const std::string &AssetName : Candidate.m_vSourceNames)
		{
			const auto SourceIt = Candidate.m_vSourceKinds.find(AssetName);
			const EEntityBgHierarchyEntrySource Source = SourceIt != Candidate.m_vSourceKinds.end() ? SourceIt->second : EEntityBgHierarchyEntrySource::LOCAL;
			if(!ShowWorkshop && Source == EEntityBgHierarchyEntrySource::WORKSHOP)
				continue;

			SEntityBgHierarchyEntry Entry;
			str_copy(Entry.m_aName, AssetName.c_str());
			const char *pDisplayName = AssetName.c_str();
			if(const char *pSlash = str_rchr(pDisplayName, '/'))
				pDisplayName = pSlash + 1;
			str_copy(Entry.m_aDisplayName, pDisplayName);
			Entry.m_Source = Source;
			TItem PreviewItem;
			str_copy(PreviewItem.m_aName, Entry.m_aName);
			PopulateAuthor(PreviewItem);
			if(!SettingsAssetSearchFilterMatches(pFilter, Entry.m_aDisplayName, PreviewItem.m_aAuthor) &&
				!SettingsAssetSearchFilterMatches(pFilter, Entry.m_aName, PreviewItem.m_aAuthor))
				continue;
			vEntries.push_back(Entry);
		}
		std::stable_sort(vEntries.begin(), vEntries.end(), [](const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right) {
			return AssetResourceNameLess(Left.m_aDisplayName, Right.m_aDisplayName);
		});
	}

	Candidate.m_vItems.reserve(vEntries.size());
	for(const SEntityBgHierarchyEntry &Entry : vEntries)
	{
		TItem Item;
		str_copy(Item.m_aName, Entry.m_aName);
		str_copy(Item.m_aDisplayName, Entry.m_aDisplayName);
		Item.m_IsDirectory = Entry.m_IsDirectory;
		if(!Item.m_IsDirectory)
			PopulateAuthor(Item);
		Candidate.m_vItems.push_back(std::move(Item));
	}
	return Candidate;
}
catch(const std::bad_alloc &)
{
	return std::nullopt;
}
catch(const std::length_error &)
{
	return std::nullopt;
}

// 提交只搬已有预览 owner 并交换容器；候选接回旧条目，供调用方卸载消失的 GPU 资源。
template<typename TItem>
void CommitSettingsAssetHierarchy(SSettingsAssetHierarchyCandidate<TItem> &Candidate,
	std::vector<std::string> &vSourceNames,
	std::unordered_map<std::string, EEntityBgHierarchyEntrySource> &vSourceKinds,
	std::vector<TItem> &vItems) noexcept
{
	for(TItem &Item : Candidate.m_vItems)
	{
		for(TItem &Previous : vItems)
		{
			if(Previous.m_IsDirectory != Item.m_IsDirectory || str_comp(Previous.m_aName, Item.m_aName) != 0)
				continue;
			Item.m_RenderTexture = Previous.m_RenderTexture;
			Previous.m_RenderTexture.Invalidate();
			Item.m_pDecodeJob = std::move(Previous.m_pDecodeJob);
			Item.m_PreviewImage = std::move(Previous.m_PreviewImage);
			Item.m_MemoryLease = std::move(Previous.m_MemoryLease);
			Item.m_PreviewState = Previous.m_PreviewState;
			Item.m_PreviewEpoch = Previous.m_PreviewEpoch;
			Item.m_PreviewListIndex = Previous.m_PreviewListIndex;
			Item.m_PreviewBytes = Previous.m_PreviewBytes;
			Item.m_PreviewRequestedTextureSize = Previous.m_PreviewRequestedTextureSize;
			Item.m_PreviewResidentBytes = Previous.m_PreviewResidentBytes;
			Item.m_PreviewResized = Previous.m_PreviewResized;
			Item.m_PreviewHighPriority = Previous.m_PreviewHighPriority;
			break;
		}
	}
	vSourceNames.swap(Candidate.m_vSourceNames);
	vSourceKinds.swap(Candidate.m_vSourceKinds);
	vItems.swap(Candidate.m_vItems);
}

#endif
