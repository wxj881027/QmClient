// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_ASSETS_RESOURCE_REGISTRY_H

#include <base/system.h>

#include <array>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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
std::string NextReservedNamedSingleFileMigrationName(const SAssetResourceCategory &Category, std::string_view OldName, std::span<const std::string> ExistingNames);
bool IsProtectedDefaultAsset(std::string_view AssetName);
bool AssetResourceNameLess(std::string_view LeftName, std::string_view RightName);
void EnsureDefaultAssetVisible(std::vector<std::string> &vAssetNames);
// 客户端自带的空白材质（虚拟条目，不对应文件）：选中它表示该类资源故意留空，
// 用户不必自己造透明图；开启 qm_blank_asset_fallback 时绘制默认资源，保留选择。
void EnsureBlankAssetVisible(std::vector<std::string> &vAssetNames);

inline constexpr const char *QM_BLANK_ASSET_NAME = "blank";

inline bool IsBlankAssetName(std::string_view AssetName)
{
	return AssetName == QM_BLANK_ASSET_NAME;
}

// 实体材质加载与预览共用解析后的路径和透明状态，虚拟 blank 不探测同名磁盘资源。
struct SEntitiesAssetSelection
{
	std::string m_Path;
	bool m_IsBlank;
};
SEntitiesAssetSelection ResolveEntitiesAssetSelection(std::string_view AssetName, bool BlankAssetFallback);

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
bool IsEntityBgWorkshopFolderOrChildPath(const char *pPath);
bool IsEntityBgParentFolderPath(const char *pPath);
bool EntityBgWorkshopAssetMatchesFolder(std::string_view AssetName, const char *pCurrentFolder);
bool HasEntityBgWorkshopFolder(const std::vector<std::string> &vAssetNames, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr);
bool IsEntityBgWorkshopRootEntry(const SEntityBgHierarchyEntry &Entry);
EEntityBgHierarchyEntrySource MergeEntityBgHierarchyEntrySource(EEntityBgHierarchyEntrySource Existing, EEntityBgHierarchyEntrySource Incoming);
std::string RebuildEntityBgWorkshopLocalName(std::string_view InstallPath);
std::string NormalizeEntityBgWorkshopInstallPath(std::string_view InstallPath);

struct SEntityBgInstallPathPreparation
{
	std::string m_Path;
	bool m_RenameFailed = false;
	std::string m_LocalName;
};

// 路径、本地名与返回对象均在改名前准备；提交后仅移动或交换已有字符串。
// 已有目标与没有旧文件时保留原规范化策略，改名失败不提供待发布的本地名。
template<typename TStorage>
SEntityBgInstallPathPreparation PrepareEntityBgWorkshopInstallPath(TStorage *pStorage, std::string_view InstallPath, int StorageType)
{
	std::string Current(InstallPath);
	SEntityBgInstallPathPreparation Result;
	Result.m_Path = NormalizeEntityBgWorkshopInstallPath(InstallPath);
	const bool NeedsNormalization = !Result.m_Path.empty() && Result.m_Path != Current;
	if(!NeedsNormalization)
		Result.m_Path = std::move(Current);
	Result.m_LocalName = RebuildEntityBgWorkshopLocalName(Result.m_Path);
	if(!NeedsNormalization)
		return Result;
	if(pStorage && pStorage->FileExists(Current.c_str(), StorageType) && !pStorage->FileExists(Result.m_Path.c_str(), StorageType) &&
		!pStorage->RenameFile(Current.c_str(), Result.m_Path.c_str(), StorageType))
	{
		Result.m_Path.swap(Current);
		Result.m_LocalName.clear();
		Result.m_RenameFailed = true;
	}
	return Result;
}

struct SNamedSingleFileMigrationResult
{
	std::string m_Name;
	bool m_PreparationFailed = false;
	bool m_Renamed = false;
};

// 改名前准备返回名、独立登记名和容量；后端拒绝不发布，提交异常保留可见。
template<typename TStorage>
SNamedSingleFileMigrationResult TryMigrateReservedNamedSingleFileAsset(TStorage *pStorage, const SAssetResourceCategory &Category, const char *pOldName, std::vector<std::string> &vExistingNames, int StorageType)
{
	SNamedSingleFileMigrationResult Result;
	char aOldPath[IO_MAX_PATH_LENGTH];
	char aNewPath[IO_MAX_PATH_LENGTH];
	str_format(aOldPath, sizeof(aOldPath), "%s/%s.png", Category.m_pInstallFolder, pOldName);
	if(!pStorage->FileExists(aOldPath, StorageType))
		return Result;
	std::string RegistrationName;
	try
	{
		Result.m_Name = NextReservedNamedSingleFileMigrationName(Category, pOldName, vExistingNames);
		RegistrationName = Result.m_Name;
		str_format(aNewPath, sizeof(aNewPath), "%s/%s.png", Category.m_pInstallFolder, Result.m_Name.c_str());
		vExistingNames.reserve(vExistingNames.size() + 1);
	}
	catch(const std::bad_alloc &)
	{
		Result.m_Name.clear();
		Result.m_PreparationFailed = true;
		return Result;
	}
	catch(const std::length_error &)
	{
		Result.m_Name.clear();
		Result.m_PreparationFailed = true;
		return Result;
	}
	if(pStorage->FileExists(aNewPath, StorageType) || !pStorage->RenameFile(aOldPath, aNewPath, StorageType))
		return Result;
	vExistingNames.push_back(std::move(RegistrationName));
	Result.m_Renamed = true;
	return Result;
}

bool EntityBgHierarchyEntryLess(const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right);
// LOCAL 目录以 / 开头、Workshop 目录以 //entity_bg 开头，parent 使用 / 作为内部导航 token。
// 扫描 leaf 保持相对资源名，因此不会与这些导航身份碰撞。
// token 仅用于 current folder/返回上级；真实文件名、磁盘路径和保存配置保持原资源名。
// 旧的无标记 LOCAL/Workshop current folder 仍可传入，返回的目录统一使用 token。
std::vector<SEntityBgHierarchyEntry> BuildEntityBgHierarchyEntries(const std::vector<std::string> &vAssetNames, const char *pCurrentFolder, bool ShowWorkshopFolder = true, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources = nullptr, bool ForceShowWorkshopFolder = false);
bool ShouldShowEntityBgDirectorySubtitle(bool IsDirectory);
void BuildEntityBgParentFolder(const char *pCurrentFolder, char *pOut, int OutSize);
bool ShouldShowAssetCardAuthorRow(bool HasAuthorText, bool IsDirectory);
const char *LegacySingleFileAssetSourcePath(const SAssetResourceCategory &Category);
const char *BuiltinSingleFileAssetFilename(const SAssetResourceCategory &Category);
bool IsReservedNamedSingleFileAssetName(const SAssetResourceCategory &Category, std::string_view AssetName);
std::array<std::string, 4> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName);

#endif
