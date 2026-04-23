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
