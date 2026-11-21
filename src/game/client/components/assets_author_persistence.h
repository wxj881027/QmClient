// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_ASSETS_AUTHOR_PERSISTENCE_H
#define GAME_CLIENT_COMPONENTS_ASSETS_AUTHOR_PERSISTENCE_H

#include "assets_resource_registry.h"
#include "background.h"

#include <engine/shared/qm_storage_layout.h>

#include <string>

inline constexpr const char *LOCAL_ASSET_AUTHOR_CACHE_FILENAME = qm_storage::WORKSHOP_AUTHOR_CACHE_FILE;

inline bool SupportsPersistedLocalAssetAuthorCategory(const char *pCategoryId)
{
	return FindAssetResourceCategory(pCategoryId) != nullptr;
}

inline std::string BuildPersistedLocalAssetAuthorKey(const char *pCategoryId, const char *pLocalName)
{
	if(pCategoryId == nullptr || pCategoryId[0] == '\0' || pLocalName == nullptr || pLocalName[0] == '\0')
		return {};
	return std::string(pCategoryId) + ":" + pLocalName;
}

struct SLocalAssetPrimarySourcePath
{
	std::string m_PrimaryPath;
	std::string m_FallbackPath;
};

inline SLocalAssetPrimarySourcePath LocalAssetPrimarySourcePaths(const SAssetResourceCategory &Category, const char *pLocalName)
{
	SLocalAssetPrimarySourcePath Paths;
	if(pLocalName == nullptr || pLocalName[0] == '\0')
		return Paths;

	if(Category.m_Kind == EAssetResourceKind::MAP_FILE)
	{
		const bool ManagedAsset = str_startswith(pLocalName, "entity_bg/");
		const bool HasExplicitMapExtension = str_endswith_nocase(pLocalName, ".map");
		const char *pImageExtension = IsBackgroundImageExtension(pLocalName) ? FindBackgroundFileExtension(pLocalName) : nullptr;
		const bool HasExplicitExtension = HasExplicitMapExtension || pImageExtension != nullptr;
		const std::string Extension = HasExplicitMapExtension ? ".map" : (pImageExtension != nullptr ? pImageExtension : ".map");
		if(ManagedAsset)
		{
			Paths.m_PrimaryPath = std::string("assets/") + pLocalName + (HasExplicitExtension ? "" : Extension);
			Paths.m_FallbackPath = std::string("maps/") + pLocalName + (HasExplicitExtension ? "" : Extension);
		}
		else
		{
			Paths.m_PrimaryPath = std::string("maps/") + pLocalName + (HasExplicitExtension ? "" : Extension);
		}
		return Paths;
	}

	Paths.m_PrimaryPath = std::string(Category.m_pInstallFolder) + "/" + pLocalName + ".png";
	if(Category.m_Kind == EAssetResourceKind::DIRECTORY)
		Paths.m_FallbackPath = std::string(Category.m_pInstallFolder) + "/" + pLocalName + "/" + Category.m_pId + ".png";
	return Paths;
}

inline std::string ResolveLocalAssetPrimarySourcePath(const SAssetResourceCategory &Category, const char *pLocalName, bool PrimaryExists, bool FallbackExists)
{
	const SLocalAssetPrimarySourcePath Paths = LocalAssetPrimarySourcePaths(Category, pLocalName);
	if(Paths.m_PrimaryPath.empty())
		return {};

	if(Category.m_Kind == EAssetResourceKind::MAP_FILE)
	{
		const bool ManagedAsset = pLocalName != nullptr && str_startswith(pLocalName, "entity_bg/");
		if(ManagedAsset)
			return PrimaryExists || !FallbackExists ? Paths.m_PrimaryPath : Paths.m_FallbackPath;
		return Paths.m_PrimaryPath;
	}

	if(PrimaryExists || Paths.m_FallbackPath.empty() || !FallbackExists)
		return Paths.m_PrimaryPath;
	return Paths.m_FallbackPath;
}

#endif
