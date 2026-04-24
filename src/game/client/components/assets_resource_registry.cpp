#include "assets_resource_registry.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr std::string_view ENTITY_BG_WORKSHOP_PREFIX = "entity_bg/";
constexpr std::string_view ENTITY_BG_INSTALL_PREFIX = "assets/entity_bg/";
constexpr std::string_view ENTITY_BG_MAP_SUFFIX = ".map";
constexpr const char *ENTITY_BG_WORKSHOP_FOLDER_LABEL = "entity_bg (Workshop)";

constexpr const char *s_apWorkshopHudAliases[] = {
	"HUD",
	"界面（HUD）",
};

constexpr const char *s_apWorkshopEntitiesAliases[] = {
	"实体层",
	"实体（Entities）",
	"实体（ENTITIES）",
};

constexpr const char *s_apWorkshopGameAliases[] = {
	"游戏",
	"游戏（Game）",
	"游戏（GAME）",
};

constexpr const char *s_apWorkshopEmoticonsAliases[] = {
	"表情",
	"表情（Emoticons）",
	"表情（EMOTICONS）",
};

constexpr const char *s_apWorkshopParticlesAliases[] = {
	"粒子",
	"粒子（Particles）",
	"粒子（PARTICLES）",
};

constexpr const char *s_apWorkshopMouseAliases[] = {
	"Mouse",
	"鼠标",
};

constexpr const char *s_apWorkshopArrowAliases[] = {
	"Arrow",
	"方向键",
};

constexpr const char *s_apWorkshopStrongWeakAliases[] = {
	"Strong Weak Hook",
	"强弱钩",
};

constexpr const char *s_apWorkshopEntityBgAliases[] = {
	"Entity Background Image",
	"实体层背景图",
};

constexpr const char *s_apWorkshopExtrasAliases[] = {
	"Extras",
	"其他",
};
}

static EEntityBgHierarchyEntrySource ResolveEntityBgAssetSource(const std::string &AssetName, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources);

static constexpr SAssetResourceCategory s_aAssetCategories[] = {
	{"entities", EAssetResourceKind::DIRECTORY, "assets/entities", true, "entities", "entity", s_apWorkshopEntitiesAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"game", EAssetResourceKind::DIRECTORY, "assets/game", true, "game", nullptr, s_apWorkshopGameAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"hud", EAssetResourceKind::DIRECTORY, "assets/hud", true, "hud", nullptr, s_apWorkshopHudAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"emoticons", EAssetResourceKind::DIRECTORY, "assets/emoticons", true, "emoticons", "emoticon", s_apWorkshopEmoticonsAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"particles", EAssetResourceKind::DIRECTORY, "assets/particles", true, "particles", "particle", s_apWorkshopParticlesAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"gui_cursor", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/gui_cursor", true, "gui_cursor", "mouse", s_apWorkshopMouseAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"arrow", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/arrow", true, "arrow", nullptr, s_apWorkshopArrowAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"strong_weak", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/strong_weak", true, "strong_weak", nullptr, s_apWorkshopStrongWeakAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"entity_bg", EAssetResourceKind::MAP_FILE, "assets/entity_bg", true, "entity_bg", nullptr, s_apWorkshopEntityBgAliases, EAssetPreviewKind::MAP_PREVIEW_OR_ICON, false},
	{"extras", EAssetResourceKind::DIRECTORY, "assets/extras", true, "extras", nullptr, s_apWorkshopExtrasAliases, EAssetPreviewKind::PNG_TEXTURE, false},
};

const SAssetResourceCategory *FindAssetResourceCategory(const char *pId)
{
	if(pId == nullptr)
		return nullptr;

	for(const auto &Category : s_aAssetCategories)
	{
		if(std::strcmp(Category.m_pId, pId) == 0)
			return &Category;
	}

	return nullptr;
}

std::span<const SAssetResourceCategory> GetAssetResourceCategories()
{
	return s_aAssetCategories;
}

bool HasEntityBgWorkshopFolder(const std::vector<std::string> &vAssetNames, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources)
{
	return std::any_of(vAssetNames.begin(), vAssetNames.end(), [pAssetSources](const std::string &AssetName) {
		return ResolveEntityBgAssetSource(AssetName, pAssetSources) == EEntityBgHierarchyEntrySource::WORKSHOP;
	});
}

bool IsProtectedDefaultAsset(std::string_view AssetName)
{
	return AssetName == "default";
}

bool AssetResourceNeedsLegacyImport(std::string_view CurrentName)
{
	return !IsProtectedDefaultAsset(CurrentName);
}

std::string NextLegacyAssetName(std::span<const std::string> ExistingNames)
{
	const auto HasName = [ExistingNames](std::string_view Name) {
		return std::any_of(ExistingNames.begin(), ExistingNames.end(), [Name](const std::string &ExistingName) {
			return ExistingName == Name;
		});
	};

	if(!HasName("legacy"))
		return "legacy";

	for(int Suffix = 2;; ++Suffix)
	{
		std::string Candidate = "legacy_" + std::to_string(Suffix);
		if(!HasName(Candidate))
			return Candidate;
	}
}

bool AssetResourceNameLess(std::string_view LeftName, std::string_view RightName)
{
	const bool LeftIsDefault = IsProtectedDefaultAsset(LeftName);
	const bool RightIsDefault = IsProtectedDefaultAsset(RightName);
	if(LeftIsDefault != RightIsDefault)
		return LeftIsDefault;

	return LeftName < RightName;
}

void EnsureDefaultAssetVisible(std::vector<std::string> &vAssetNames)
{
	const auto HasDefault = std::any_of(vAssetNames.begin(), vAssetNames.end(), [](const std::string &Name) {
		return IsProtectedDefaultAsset(Name);
	});
	if(!HasDefault)
		vAssetNames.emplace_back("default");

	std::sort(vAssetNames.begin(), vAssetNames.end(), [](const std::string &LeftName, const std::string &RightName) {
		return AssetResourceNameLess(LeftName, RightName);
	});
}

std::string RebuildEntityBgWorkshopLocalName(std::string_view InstallPath)
{
	std::string NormalizedPath(InstallPath);
	std::replace(NormalizedPath.begin(), NormalizedPath.end(), '\\', '/');

	std::string_view PathView(NormalizedPath);
	if(!PathView.starts_with(ENTITY_BG_INSTALL_PREFIX) || !PathView.ends_with(ENTITY_BG_MAP_SUFFIX))
		return "";

	PathView.remove_prefix(ENTITY_BG_INSTALL_PREFIX.size());
	PathView.remove_suffix(ENTITY_BG_MAP_SUFFIX.size());
	if(PathView.empty())
		return "";

	return std::string(ENTITY_BG_WORKSHOP_PREFIX) + std::string(PathView);
}

const char *LegacySingleFileAssetSourcePath(const SAssetResourceCategory &Category)
{
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return nullptr;

	if(std::strcmp(Category.m_pId, "gui_cursor") == 0)
		return "gui_cursor.png";
	if(std::strcmp(Category.m_pId, "arrow") == 0)
		return "arrow.png";
	if(std::strcmp(Category.m_pId, "strong_weak") == 0)
		return "strong_weak.png";
	return nullptr;
}

const char *BuiltinSingleFileAssetFilename(const SAssetResourceCategory &Category)
{
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return nullptr;

	if(std::strcmp(Category.m_pId, "gui_cursor") == 0)
		return "gui_cursor.png";
	if(std::strcmp(Category.m_pId, "arrow") == 0)
		return "arrow.png";
	if(std::strcmp(Category.m_pId, "strong_weak") == 0)
		return "strong_weak.png";
	return nullptr;
}

std::array<std::string, 3> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName)
{
	std::array<std::string, 3> aCandidates;
	aCandidates[0] = "assets/" + std::string(CategoryId) + "/" + std::string(ActiveName) + ".png";

	const std::string CategoryIdString(CategoryId);
	if(const SAssetResourceCategory *pCategory = FindAssetResourceCategory(CategoryIdString.c_str()))
	{
		if(const char *pBuiltinFilename = BuiltinSingleFileAssetFilename(*pCategory))
			aCandidates[1] = pBuiltinFilename;
	}

	return aCandidates;
}

bool IsEntityBgWorkshopFolderPath(const char *pPath)
{
	return pPath != nullptr && str_comp(pPath, "entity_bg") == 0;
}

bool EntityBgHierarchyEntryLess(const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right)
{
	const bool LeftIsParent = str_comp(Left.m_aName, "..") == 0;
	const bool RightIsParent = str_comp(Right.m_aName, "..") == 0;
	if(LeftIsParent != RightIsParent)
		return LeftIsParent;

	if(Left.m_IsDirectory != Right.m_IsDirectory)
		return Left.m_IsDirectory;

	const bool LeftIsDefault = IsProtectedDefaultAsset(Left.m_aName);
	const bool RightIsDefault = IsProtectedDefaultAsset(Right.m_aName);
	if(LeftIsDefault != RightIsDefault)
		return LeftIsDefault;

	const int DisplayComp = str_comp_nocase(Left.m_aDisplayName, Right.m_aDisplayName);
	if(DisplayComp != 0)
		return DisplayComp < 0;

	return str_comp_nocase(Left.m_aName, Right.m_aName) < 0;
}

static EEntityBgHierarchyEntrySource ResolveEntityBgAssetSource(const std::string &AssetName, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources)
{
	if(pAssetSources != nullptr)
	{
		const auto It = pAssetSources->find(AssetName);
		if(It != pAssetSources->end())
			return It->second;
	}

	return AssetName.starts_with(ENTITY_BG_WORKSHOP_PREFIX) ? EEntityBgHierarchyEntrySource::WORKSHOP : EEntityBgHierarchyEntrySource::LOCAL;
}

static std::string_view EntityBgHierarchyPath(std::string_view AssetName)
{
	if(AssetName.starts_with(ENTITY_BG_WORKSHOP_PREFIX))
		return AssetName.substr(ENTITY_BG_WORKSHOP_PREFIX.size());
	return AssetName;
}

static bool IsEntityBgWorkshopFolderOrChildPath(const char *pPath)
{
	return pPath != nullptr && (IsEntityBgWorkshopFolderPath(pPath) || str_startswith(pPath, "entity_bg/"));
}

static std::string_view EntityBgLogicalFolder(std::string_view CurrentFolder)
{
	if(CurrentFolder == "entity_bg")
		return "";
	if(CurrentFolder.starts_with(ENTITY_BG_WORKSHOP_PREFIX))
		return CurrentFolder.substr(ENTITY_BG_WORKSHOP_PREFIX.size());
	return CurrentFolder;
}

std::vector<SEntityBgHierarchyEntry> BuildEntityBgHierarchyEntries(const std::vector<std::string> &vAssetNames, const char *pCurrentFolder, bool ShowWorkshopFolder, const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> *pAssetSources)
{
	std::vector<SEntityBgHierarchyEntry> vEntries;
	std::unordered_set<std::string> vSeenNames;

	char aFolder[IO_MAX_PATH_LENGTH];
	str_copy(aFolder, pCurrentFolder != nullptr ? pCurrentFolder : "", sizeof(aFolder));
	const bool IsWorkshopFolder = IsEntityBgWorkshopFolderOrChildPath(aFolder);
	const std::string_view LogicalFolder = EntityBgLogicalFolder(aFolder);
	const int FolderLength = str_length(aFolder);

	auto AddEntry = [&](const char *pName, const char *pDisplayName, bool IsDirectory, EEntityBgHierarchyEntrySource Source) {
		if(pName == nullptr || pName[0] == '\0')
			return;
		if(!vSeenNames.insert(pName).second)
			return;

		SEntityBgHierarchyEntry Entry;
		str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));
		str_copy(Entry.m_aDisplayName, pDisplayName != nullptr && pDisplayName[0] != '\0' ? pDisplayName : pName, sizeof(Entry.m_aDisplayName));
		Entry.m_IsDirectory = IsDirectory;
		Entry.m_Source = Source;
		vEntries.push_back(Entry);
	};

	if(aFolder[0] != '\0')
		AddEntry("..", "..", true, EEntityBgHierarchyEntrySource::NAVIGATION);

	for(const std::string &AssetName : vAssetNames)
	{
		if(AssetName.empty())
			continue;

		const EEntityBgHierarchyEntrySource Source = ResolveEntityBgAssetSource(AssetName, pAssetSources);
		const bool IsWorkshopAsset = Source == EEntityBgHierarchyEntrySource::WORKSHOP;
		const std::string_view HierarchyName = EntityBgHierarchyPath(AssetName);

		if(aFolder[0] == '\0')
		{
			if(IsWorkshopAsset)
				continue;

			const size_t SlashPos = HierarchyName.find('/');
			if(SlashPos == std::string_view::npos)
			{
				AddEntry(AssetName.c_str(), std::string(HierarchyName).c_str(), false, Source);
			}
			else
			{
				const std::string FolderDisplayName(HierarchyName.substr(0, SlashPos));
				const std::string FolderPath(HierarchyName.substr(0, SlashPos));
				AddEntry(FolderPath.c_str(), FolderDisplayName.c_str(), true, Source);
			}
			continue;
		}

		if(IsWorkshopFolder)
		{
			if(!IsWorkshopAsset)
				continue;
		}
		else
		{
			if(IsWorkshopAsset)
				continue;
		}

		const std::string_view MatchFolder = IsWorkshopFolder ? LogicalFolder : std::string_view(aFolder);
		if(HierarchyName == MatchFolder)
			continue;
		if(!MatchFolder.empty())
		{
			if(HierarchyName.size() <= MatchFolder.size() || HierarchyName.substr(0, MatchFolder.size()) != MatchFolder || HierarchyName[MatchFolder.size()] != '/')
				continue;
		}

		const std::string_view Remainder = MatchFolder.empty() ? HierarchyName : HierarchyName.substr(MatchFolder.size() + 1);
		if(Remainder.empty())
			continue;

		const size_t SlashPos = Remainder.find('/');
		if(SlashPos == std::string_view::npos)
		{
			AddEntry(AssetName.c_str(), std::string(Remainder).c_str(), false, Source);
		}
		else
		{
			const std::string ChildDisplayName(Remainder.substr(0, SlashPos));
			const std::string ChildLogicalPath = MatchFolder.empty() ? std::string(Remainder.substr(0, SlashPos)) :
				std::string(MatchFolder) + "/" + std::string(Remainder.substr(0, SlashPos));
			const std::string ChildPath = IsWorkshopFolder ? std::string(ENTITY_BG_WORKSHOP_PREFIX) + ChildLogicalPath : ChildLogicalPath;
			AddEntry(ChildPath.c_str(), ChildDisplayName.c_str(), true, Source);
		}
	}

	if(aFolder[0] == '\0' && ShowWorkshopFolder && HasEntityBgWorkshopFolder(vAssetNames, pAssetSources))
		AddEntry("entity_bg", ENTITY_BG_WORKSHOP_FOLDER_LABEL, true, EEntityBgHierarchyEntrySource::WORKSHOP);

	std::stable_sort(vEntries.begin(), vEntries.end(), EntityBgHierarchyEntryLess);
	return vEntries;
}

bool ShouldShowEntityBgDirectorySubtitle(bool IsDirectory)
{
	return !IsDirectory;
}

void BuildEntityBgParentFolder(const char *pCurrentFolder, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;

	str_copy(pOut, pCurrentFolder != nullptr ? pCurrentFolder : "", OutSize);
	if(pOut[0] == '\0')
		return;

	if(str_comp(pOut, "entity_bg") == 0 || fs_parent_dir(pOut) != 0)
		pOut[0] = '\0';
}

bool ShouldShowAssetCardAuthorRow(bool HasAuthorText, bool IsDirectory)
{
	return HasAuthorText && !IsDirectory;
}
