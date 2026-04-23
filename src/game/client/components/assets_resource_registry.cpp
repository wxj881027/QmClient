#include "assets_resource_registry.h"

#include <cstring>

static constexpr SAssetResourceCategory s_aAssetCategories[] = {
	{"entities", EAssetResourceKind::DIRECTORY, "assets/entities", true, "entities", "entity", EAssetPreviewKind::PNG_TEXTURE, false},
	{"game", EAssetResourceKind::DIRECTORY, "assets/game", true, "game", nullptr, EAssetPreviewKind::PNG_TEXTURE, false},
	{"hud", EAssetResourceKind::DIRECTORY, "assets/hud", true, "hud", nullptr, EAssetPreviewKind::PNG_TEXTURE, false},
	{"emoticons", EAssetResourceKind::DIRECTORY, "assets/emoticons", true, "emoticons", "emoticon", EAssetPreviewKind::PNG_TEXTURE, false},
	{"particles", EAssetResourceKind::DIRECTORY, "assets/particles", true, "particles", "particle", EAssetPreviewKind::PNG_TEXTURE, false},
	{"gui_cursor", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/gui_cursor", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"arrow", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/arrow", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"strong_weak", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/strong_weak", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"entity_bg", EAssetResourceKind::MAP_FILE, "maps", false, nullptr, nullptr, EAssetPreviewKind::MAP_PREVIEW_OR_ICON, true},
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
