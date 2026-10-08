// 卡片目录 stableId 清单与查询：纯数据，不依赖 UI/菜单，
// 便于 testrunner 等目标在不链接 game-client 的情况下校验 HasCardModule。
#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <algorithm>
#include <vector>

namespace qm_card_catalog
{
	namespace
	{
		// 分类页的卡片清单（stableId 与 QmCardRegistry 的默认 Placement 表同源）。
		// 页面从这里取"该有哪些卡"，卡片实现则分派到对应的卡片模块文件。
		const std::vector<const char *> s_vVisualCards = {
			"qm:appearance_preset",
			"qm:tooltip",
			"qm:chat_bubble",
			"qm:focus_mode",
			"qm:camera_view",
			"qm:skin_transition",
			"qm:weapon_animation",
			"qm:streamer",
			"qm:entity_overlay",
			"qm:collision_hitbox",
			"qm:water_hammer",
		};

		const std::vector<const char *> s_vFunctionCards = {
			"qm:gores_actor",
			"qm:gores",
			"qm:key_binds",
			"qm:emoticons",
			"qm:better_scoreboard",
			"qm:mini_features",
			"qm:ime",
			"qm:jump_hint",
			"qm:weapon_trajectory",
			"qm:friend_notify",
			"qm:block_words",
			"qm:translate",
			"qm:translate_ui",
			"qm:qiafen",
			"qm:pie_menu",
			"qm:map_upload",
			"qm:favorite_maps",
			"qm:hj_assist",
			"qm:solo_split",
			"qm:steam",
		};

		const std::vector<const char *> s_vHudCards = {
			"qm:dummy_miniview",
			"qm:coords",
			"qm:player_stats",
			"qm:debug_graph",
			"qm:debug_mode",
			"qm:input_overlay",
			"qm:hud_notifications",
			"qm:voice",
			"qm:dynamic_island",
			"qm:system_media_controls",
			"qm:lyrics",
			"qm:background_3d",
			"qm:bind_status_hud",
			"qm:gores_drown_board",
		};

		const std::vector<const char *> s_vBindCards = {
			"qm:bind_editor",
		};

		const std::vector<const char *> s_vNameplateCards = {
			"deck:appearance-name-plate-settings",
			"deck:appearance-name-plate-text",
			"deck:appearance-name-plate-hook-strength",
			"deck:appearance-name-plate-key-presses",
		};

		const std::vector<const char *> s_vTeeCards = {
			"deck:tee-identity",
			"deck:tee-skin-list",
			"deck:tee-skin-options",
			"deck:tee-skin-queue",
			"qm:skin_appearance",
			"deck:tee-glow",
		};

		const std::vector<const char *> s_vTitleCards = {
			"deck:qmclient-contributors-title",
			"deck:qmclient-contributors-title-display",
		};

		const std::vector<const char *> s_vGeneralCards = {
			"deck:general-game",
			"deck:general-language",
			"deck:general-client",
			"deck:general-recording",
			"deck:tclient-info-files",
		};

		bool ContainsStableId(const std::vector<const char *> &vStableIds, const char *pStableId)
		{
			if(pStableId == nullptr)
				return false;
			return std::any_of(vStableIds.begin(), vStableIds.end(), [pStableId](const char *pCandidate) { return str_comp(pCandidate, pStableId) == 0; });
		}

		uint64_t FoldRevision(uint64_t Hash, const uint64_t Revision)
		{
			return Hash * 1099511628211ULL ^ Revision;
		}
	} // namespace

	const std::vector<const char *> &VisualCardStableIds()
	{
		return s_vVisualCards;
	}

	const std::vector<const char *> &FunctionCardStableIds()
	{
		return s_vFunctionCards;
	}

	const std::vector<const char *> &HudCardStableIds()
	{
		return s_vHudCards;
	}

	const std::vector<const char *> &BindCardStableIds()
	{
		return s_vBindCards;
	}

	const std::vector<const char *> &NameplateCardStableIds()
	{
		return s_vNameplateCards;
	}

	uint64_t NameplateMeasureContentRevision()
	{
		return (static_cast<uint64_t>(g_Config.m_ClNamePlatesClan != 0) << 0) |
		       (static_cast<uint64_t>(g_Config.m_ClNamePlatesIds != 0) << 1) |
		       (static_cast<uint64_t>(g_Config.m_ClNamePlatesIdsSeparateLine != 0) << 2) |
		       (static_cast<uint64_t>(g_Config.m_ClNamePlatesStrong != 0) << 3) |
		       (static_cast<uint64_t>(g_Config.m_ClShowDirection > 0) << 4) |
		       (static_cast<uint64_t>(g_Config.m_QmNameplateEffectAutoLod != 0) << 5) |
		       (static_cast<uint64_t>(g_Config.m_QmNameplateAdvanced != 0) << 6);
	}

	const std::vector<const char *> &TeeCardStableIds()
	{
		return s_vTeeCards;
	}

	bool HasCardModule(const char *pStableId)
	{
		return ContainsStableId(s_vVisualCards, pStableId) || ContainsStableId(s_vFunctionCards, pStableId) || ContainsStableId(s_vHudCards, pStableId) || ContainsStableId(s_vBindCards, pStableId) || ContainsStableId(s_vNameplateCards, pStableId) || ContainsStableId(s_vTeeCards, pStableId) || ContainsStableId(s_vTitleCards, pStableId) || ContainsStableId(s_vGeneralCards, pStableId);
	}

	const std::vector<const char *> &GeneralCardStableIds()
	{
		return s_vGeneralCards;
	}

	uint64_t GeneralMeasureContentRevision()
	{
		return (static_cast<uint64_t>(g_Config.m_ClAutoDemoRecord != 0) << 0) |
		       (static_cast<uint64_t>(g_Config.m_ClAutoScreenshot != 0) << 1) |
		       (static_cast<uint64_t>(g_Config.m_ClAutoStatboardScreenshot != 0) << 2) |
		       (static_cast<uint64_t>(g_Config.m_ClAutoCSV != 0) << 3) |
		       (static_cast<uint64_t>(g_Config.m_ClDyncam != 0 || g_Config.m_ClMouseFollowfactor > 0) << 4);
	}

	const std::vector<const char *> &TitleCardStableIds()
	{
		return s_vTitleCards;
	}

	uint64_t MeasureContentRevision(size_t LanguageCount, size_t ThemeCount)
	{
		uint64_t Revision = FoldRevision(0, (uint64_t)s_vVisualCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vFunctionCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vHudCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vBindCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vNameplateCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vTeeCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vTitleCards.size());
		Revision = FoldRevision(Revision, NameplateMeasureContentRevision());
		Revision = FoldRevision(Revision, static_cast<uint64_t>(s_vGeneralCards.size()));
		Revision = FoldRevision(Revision, GeneralMeasureContentRevision());
		Revision = FoldRevision(Revision, static_cast<uint64_t>(LanguageCount));
		Revision = FoldRevision(Revision, static_cast<uint64_t>(ThemeCount));
		return Revision;
	}
} // namespace qm_card_catalog
