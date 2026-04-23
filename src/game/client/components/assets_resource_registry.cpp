#include "assets_resource_registry.h"

#include <algorithm>
#include <cstring>

namespace
{
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
}

static constexpr SAssetResourceCategory s_aAssetCategories[] = {
	{"entities", EAssetResourceKind::DIRECTORY, "assets/entities", true, "entities", "entity", s_apWorkshopEntitiesAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"game", EAssetResourceKind::DIRECTORY, "assets/game", true, "game", nullptr, s_apWorkshopGameAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"hud", EAssetResourceKind::DIRECTORY, "assets/hud", true, "hud", nullptr, s_apWorkshopHudAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"emoticons", EAssetResourceKind::DIRECTORY, "assets/emoticons", true, "emoticons", "emoticon", s_apWorkshopEmoticonsAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"particles", EAssetResourceKind::DIRECTORY, "assets/particles", true, "particles", "particle", s_apWorkshopParticlesAliases, EAssetPreviewKind::PNG_TEXTURE, false},
	{"gui_cursor", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/gui_cursor", false, nullptr, nullptr, {}, EAssetPreviewKind::PNG_TEXTURE, true},
	{"arrow", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/arrow", false, nullptr, nullptr, {}, EAssetPreviewKind::PNG_TEXTURE, true},
	{"strong_weak", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/strong_weak", false, nullptr, nullptr, {}, EAssetPreviewKind::PNG_TEXTURE, true},
	{"entity_bg", EAssetResourceKind::MAP_FILE, "maps", false, nullptr, nullptr, {}, EAssetPreviewKind::MAP_PREVIEW_OR_ICON, true},
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

const char *LegacySingleFileAssetSourcePath(const SAssetResourceCategory &Category)
{
	if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
		return nullptr;

	if(std::strcmp(Category.m_pId, "gui_cursor") == 0)
		return "DDNet/gui_cursor.png";
	if(std::strcmp(Category.m_pId, "arrow") == 0)
		return "DDNet/arrow.png";
	if(std::strcmp(Category.m_pId, "strong_weak") == 0)
		return "DDNet/strong_weak.png";
	return nullptr;
}

std::array<std::string, 3> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName)
{
	std::array<std::string, 3> aCandidates;
	aCandidates[0] = "assets/" + std::string(CategoryId) + "/" + std::string(ActiveName) + ".png";

	const std::string CategoryIdString(CategoryId);
	if(const SAssetResourceCategory *pCategory = FindAssetResourceCategory(CategoryIdString.c_str()))
	{
		if(const char *pLegacyPath = LegacySingleFileAssetSourcePath(*pCategory))
			aCandidates[1] = pLegacyPath;
	}

	if(CategoryId == "gui_cursor")
		aCandidates[2] = "gui_cursor.png";
	else if(CategoryId == "arrow")
		aCandidates[2] = "arrow.png";
	else if(CategoryId == "strong_weak")
		aCandidates[2] = "strong_weak.png";

	return aCandidates;
}
