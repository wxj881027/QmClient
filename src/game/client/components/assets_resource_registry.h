#ifndef GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H

#include <base/system.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

enum class EEntityBgHierarchyEntrySource
{
	NAVIGATION,
	LOCAL,
	WORKSHOP,
};

struct SEntityBgHierarchyEntry
{
	char m_aName[IO_MAX_PATH_LENGTH] = {0};
	char m_aDisplayName[IO_MAX_PATH_LENGTH] = {0};
	bool m_IsDirectory = false;
	EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
};

struct SAssetResourceCategory
{
	const char *m_pId;
	EAssetResourceKind m_Kind;
	const char *m_pInstallFolder;
	bool m_WorkshopEnabled;
	const char *m_pWorkshopCategory;
	const char *m_pWorkshopCategoryAlt;
	std::span<const char *const> m_vWorkshopCategoryAliases;
	EAssetPreviewKind m_PreviewKind;
	bool m_LocalOnlyBadge;
};

const SAssetResourceCategory *FindAssetResourceCategory(const char *pId);
std::span<const SAssetResourceCategory> GetAssetResourceCategories();
bool AssetResourceNeedsLegacyImport(std::string_view CurrentName);
std::string NextLegacyAssetName(std::span<const std::string> ExistingNames);
bool IsProtectedDefaultAsset(std::string_view AssetName);
bool AssetResourceNameLess(std::string_view LeftName, std::string_view RightName);
void EnsureDefaultAssetVisible(std::vector<std::string> &vAssetNames);
bool IsEntityBgWorkshopFolderPath(const char *pPath);
bool HasEntityBgWorkshopFolder(const std::vector<std::string> &vAssetNames, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr);
std::string RebuildEntityBgWorkshopLocalName(std::string_view InstallPath);
bool EntityBgHierarchyEntryLess(const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right);
std::vector<SEntityBgHierarchyEntry> BuildEntityBgHierarchyEntries(const std::vector<std::string> &vAssetNames, const char *pCurrentFolder, bool ShowWorkshopFolder = true, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr, bool ForceShowWorkshopFolder = false);
bool ShouldShowEntityBgDirectorySubtitle(bool IsDirectory);
void BuildEntityBgParentFolder(const char *pCurrentFolder, char *pOut, int OutSize);
bool ShouldShowAssetCardAuthorRow(bool HasAuthorText, bool IsDirectory);
const char *LegacySingleFileAssetSourcePath(const SAssetResourceCategory &Category);
const char *BuiltinSingleFileAssetFilename(const SAssetResourceCategory &Category);
std::array<std::string, 3> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName);

#endif
