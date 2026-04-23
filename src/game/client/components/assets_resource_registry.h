#ifndef GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H

#include <span>
#include <string>
#include <string_view>

enum class EAssetResourceKind
{
	DIRECTORY,
	NAMED_SINGLE_FILE,
	MAP_FILE,
};

enum class EAssetPreviewKind
{
	PNG_TEXTURE,
	MAP_PREVIEW_OR_ICON,
};

struct SAssetResourceCategory
{
	const char *m_pId;
	EAssetResourceKind m_Kind;
	const char *m_pInstallFolder;
	bool m_WorkshopEnabled;
	const char *m_pWorkshopCategory;
	const char *m_pWorkshopCategoryAlt;
	EAssetPreviewKind m_PreviewKind;
	bool m_LocalOnlyBadge;
};

const SAssetResourceCategory *FindAssetResourceCategory(const char *pId);
bool AssetResourceNeedsLegacyImport(std::string_view CurrentName);
std::string NextLegacyAssetName(std::span<const std::string> ExistingNames);
bool IsProtectedDefaultAsset(std::string_view AssetName);

#endif
