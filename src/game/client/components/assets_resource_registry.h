// 请抬头享受阳光｜日子很好 我很我---------致咩子
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
// 客户端自带的空白材质（虚拟条目，不对应文件）：选中它表示该类资源故意留空，
// 用户不必自己造透明图；显式选择时不再参与 qm_blank_asset_fallback 回退。
void EnsureBlankAssetVisible(std::vector<std::string> &vAssetNames);

inline constexpr const char *QM_BLANK_ASSET_NAME = "blank";

inline bool IsBlankAssetName(std::string_view AssetName)
{
	return AssetName == QM_BLANK_ASSET_NAME;
}

// 内置虚拟条目（default / blank）：不可删除、不可重命名，也不对应本地素材文件。
inline bool IsProtectedAssetName(std::string_view AssetName)
{
	return IsProtectedDefaultAsset(AssetName) || IsBlankAssetName(AssetName);
}

// 空白材质对「整张图片替换」的类别有意义：图集类、单文件类图片素材，以及编辑器实体层
// 贴图（entities 选中 blank 时按内置实体图的尺寸造全透明层）。entity_bg 是地图文件，不支持。
inline bool AssetResourceSupportsBlank(const SAssetResourceCategory &Category)
{
	return Category.m_Kind != EAssetResourceKind::MAP_FILE;
}
bool IsEntityBgWorkshopFolderPath(const char *pPath);
bool HasEntityBgWorkshopFolder(const std::vector<std::string> &vAssetNames, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr);
bool IsEntityBgWorkshopRootEntry(const SEntityBgHierarchyEntry &Entry);
EEntityBgHierarchyEntrySource MergeEntityBgHierarchyEntrySource(EEntityBgHierarchyEntrySource Existing, EEntityBgHierarchyEntrySource Incoming);
std::string RebuildEntityBgWorkshopLocalName(std::string_view InstallPath);
std::string NormalizeEntityBgWorkshopInstallPath(std::string_view InstallPath);
bool EntityBgHierarchyEntryLess(const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right);
std::vector<SEntityBgHierarchyEntry> BuildEntityBgHierarchyEntries(const std::vector<std::string> &vAssetNames, const char *pCurrentFolder, bool ShowWorkshopFolder = true, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr, bool ForceShowWorkshopFolder = false);
bool ShouldShowEntityBgDirectorySubtitle(bool IsDirectory);
void BuildEntityBgParentFolder(const char *pCurrentFolder, char *pOut, int OutSize);
bool ShouldShowAssetCardAuthorRow(bool HasAuthorText, bool IsDirectory);
const char *LegacySingleFileAssetSourcePath(const SAssetResourceCategory &Category);
const char *BuiltinSingleFileAssetFilename(const SAssetResourceCategory &Category);
bool IsReservedNamedSingleFileAssetName(const SAssetResourceCategory &Category, std::string_view AssetName);
std::array<std::string, 4> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName);

#endif
