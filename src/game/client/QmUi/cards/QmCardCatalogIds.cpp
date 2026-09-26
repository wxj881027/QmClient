// 卡片目录 stableId 清单与查询：纯数据，不依赖 UI/菜单，
// 便于 testrunner 等目标在不链接 game-client 的情况下校验 HasCardModule。
#include <base/system.h>

#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <algorithm>
#include <vector>

namespace qm_card_catalog
{
	namespace
	{
		// 三个分类的卡片清单（stableId 与 QmCardRegistry 的默认 Placement 表同源）。
		// 页面从这里取"该有哪些卡"，卡片实现则分派到对应的卡片模块文件。
		const std::vector<const char *> s_vVisualCards = {
			"qm:chat_bubble",
			"qm:focus_mode",
			"qm:camera_view",
			"qm:skin_appearance",
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
			"qm:mini_features",
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

	bool HasCardModule(const char *pStableId)
	{
		return ContainsStableId(s_vVisualCards, pStableId) || ContainsStableId(s_vFunctionCards, pStableId) || ContainsStableId(s_vHudCards, pStableId);
	}

	uint64_t MeasureContentRevision()
	{
		uint64_t Revision = FoldRevision(0, (uint64_t)s_vVisualCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vFunctionCards.size());
		Revision = FoldRevision(Revision, (uint64_t)s_vHudCards.size());
		return Revision;
	}
} // namespace qm_card_catalog
