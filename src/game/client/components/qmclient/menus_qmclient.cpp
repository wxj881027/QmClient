#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmIslandNotice.h>
#include <game/client/QmUi/QmIslandSurface.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/QmModuleTypes.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiDogfood.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h>
#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/input_overlay.h>
#include <game/client/components/qmclient/keyword_reply_rules.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_map_upload.h>
#include <game/client/components/qmclient/qm_markdown.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qm_sponsor_authors.h>
#include <game/client/components/qmclient/qm_title_color.h>
#include <game/client/components/qmclient/qm_title_render.h>
#include <game/client/components/qmclient/qm_title_style.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	NUMBER_OF_TCLIENT_TABS
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

[[maybe_unused]] static float s_Time = 0.0f;
[[maybe_unused]] static bool s_StartedTime = false;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
extern bool g_CommandBindCacheInitialized;

namespace
{
	// Visual Deck 需要完整的模块表，才能在切换单张卡片的折叠状态时保留其他 tab 的历史配置。
	const std::array<qm_module::SQmModuleEntry, qm_module::QmModuleCount> s_aQmModuleDefaults = {{{qm_module::EQmModuleId::Info, qm_module::EQmModuleColumn::Full, 0, "info"},
		{qm_module::EQmModuleId::ChatBubble, qm_module::EQmModuleColumn::Left, 0, "chat_bubble"},
		{qm_module::EQmModuleId::SkinAppearance, qm_module::EQmModuleColumn::Left, 1, "skin_appearance"},
		{qm_module::EQmModuleId::SkinTransition, qm_module::EQmModuleColumn::Left, 2, "skin_transition"},
		{qm_module::EQmModuleId::FocusMode, qm_module::EQmModuleColumn::Left, 3, "focus_mode"},
		{qm_module::EQmModuleId::GoresActor, qm_module::EQmModuleColumn::Left, 3, "gores_actor"},
		{qm_module::EQmModuleId::Gores, qm_module::EQmModuleColumn::Left, 4, "gores"},
		{qm_module::EQmModuleId::KeyBinds, qm_module::EQmModuleColumn::Left, 5, "key_binds"},
		{qm_module::EQmModuleId::BetterScoreboard, qm_module::EQmModuleColumn::Left, 6, "better_scoreboard"},
		{qm_module::EQmModuleId::MiniFeatures, qm_module::EQmModuleColumn::Left, 7, "mini_features"},
		{qm_module::EQmModuleId::JumpHint, qm_module::EQmModuleColumn::Left, 7, "jump_hint"},
		{qm_module::EQmModuleId::WeaponTrajectory, qm_module::EQmModuleColumn::Left, 8, "weapon_trajectory"},
		{qm_module::EQmModuleId::Coords, qm_module::EQmModuleColumn::Left, 9, "coords"},
		{qm_module::EQmModuleId::Streamer, qm_module::EQmModuleColumn::Left, 10, "streamer"},
		{qm_module::EQmModuleId::FriendNotify, qm_module::EQmModuleColumn::Left, 11, "friend_notify"},
		{qm_module::EQmModuleId::BlockWords, qm_module::EQmModuleColumn::Left, 12, "block_words"},
		{qm_module::EQmModuleId::Translate, qm_module::EQmModuleColumn::Left, 14, "translate"},
		{qm_module::EQmModuleId::TranslateUi, qm_module::EQmModuleColumn::Left, 15, "translate_ui"},
		{qm_module::EQmModuleId::QiaFen, qm_module::EQmModuleColumn::Left, 13, "qiafen"},
		{qm_module::EQmModuleId::PieMenu, qm_module::EQmModuleColumn::Left, 16, "pie_menu"},
		{qm_module::EQmModuleId::CameraView, qm_module::EQmModuleColumn::Right, 0, "camera_view"},
		{qm_module::EQmModuleId::WeaponAnimation, qm_module::EQmModuleColumn::Right, 1, "weapon_animation"},
		{qm_module::EQmModuleId::EntityOverlay, qm_module::EQmModuleColumn::Right, 2, "entity_overlay"},
		{qm_module::EQmModuleId::Laser, qm_module::EQmModuleColumn::Right, 3, "laser"},
		{qm_module::EQmModuleId::PlayerStats, qm_module::EQmModuleColumn::Right, 4, "player_stats"},
		{qm_module::EQmModuleId::CollisionHitbox, qm_module::EQmModuleColumn::Right, 5, "collision_hitbox"},
		{qm_module::EQmModuleId::FavoriteMaps, qm_module::EQmModuleColumn::Right, 6, "favorite_maps"},
		{qm_module::EQmModuleId::HJAssist, qm_module::EQmModuleColumn::Right, 7, "hj_assist"},
		{qm_module::EQmModuleId::DebugGraph, qm_module::EQmModuleColumn::Right, 9, "debug_graph"},
		{qm_module::EQmModuleId::InputOverlay, qm_module::EQmModuleColumn::Right, 10, "input_overlay"},
		{qm_module::EQmModuleId::HudNotifications, qm_module::EQmModuleColumn::Right, 11, "hud_notifications"},
		{qm_module::EQmModuleId::Voice, qm_module::EQmModuleColumn::Right, 12, "voice"},
		{qm_module::EQmModuleId::DummyMiniView, qm_module::EQmModuleColumn::Right, 13, "dummy_miniview"},
		{qm_module::EQmModuleId::DynamicIsland, qm_module::EQmModuleColumn::Right, 14, "dynamic_island"},
		{qm_module::EQmModuleId::SystemMediaControls, qm_module::EQmModuleColumn::Right, 15, "system_media_controls"},
		{qm_module::EQmModuleId::Lyrics, qm_module::EQmModuleColumn::Right, 16, "lyrics"},
		{qm_module::EQmModuleId::Background3D, qm_module::EQmModuleColumn::Right, 17, "background_3d"},
		{qm_module::EQmModuleId::DebugMode, qm_module::EQmModuleColumn::Right, 19, "debug_mode"},
		{qm_module::EQmModuleId::BindStatusHud, qm_module::EQmModuleColumn::Right, 20, "bind_status_hud"},
		{qm_module::EQmModuleId::SoloSplit, qm_module::EQmModuleColumn::Left, 17, "solo_split"},
		// 本地差异：远程把表情卡放在 Left/17、地图上传卡放在 Right/8。
		// 本地 Left/17 已被独有的 SoloSplit 占用，故表情卡追加到 Left/18；
		// Right/8 随速通计时器删除而空出，但地图上传卡仍保持在列尾，
		// 不改动既有卡片的既有顺序。
		{qm_module::EQmModuleId::Emoticons, qm_module::EQmModuleColumn::Left, 18, "emoticons"},
		{qm_module::EQmModuleId::MapUpload, qm_module::EQmModuleColumn::Right, 21, "map_upload"},
		{qm_module::EQmModuleId::Steam, qm_module::EQmModuleColumn::Right, 22, "steam"},
		{qm_module::EQmModuleId::WaterHammerHighlight, qm_module::EQmModuleColumn::Right, 4, "water_hammer"},
		{qm_module::EQmModuleId::GoresDrownBoard, qm_module::EQmModuleColumn::Right, 23, "gores_drown_board"},
		{qm_module::EQmModuleId::Ime, qm_module::EQmModuleColumn::Left, 19, "ime"}}};
}

using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;

struct SQmGlobalSearchResults
{
	std::vector<SQmGlobalSearchCard> m_vAllVisibleCards;
};

namespace
{
	void CollectGlobalSearchResults(const char *pSearch, bool Sixup, const qm_card_order::CModel &Model, SQmGlobalSearchResults &Out)
	{
		Out.m_vAllVisibleCards.clear();
		// 无搜索内容时结果为空：搜索页默认只显示搜索输入卡片，不展示全量卡片。
		if(pSearch == nullptr || pSearch[0] == '\0')
			return;
		std::vector<SQmGlobalSearchCard> vCards = qm_card_registry::SearchCards(pSearch, Model);
		Out.m_vAllVisibleCards.reserve(vCards.size());
		for(SQmGlobalSearchCard &Card : vCards)
		{
			const char *pTab = Card.m_Target.m_pTab;
			if(pTab != nullptr && str_comp(pTab, "global-search") == 0)
				continue;
			if(pTab != nullptr && ((Sixup && str_comp(pTab, "tee") == 0) || (!Sixup && str_comp(pTab, "tee7") == 0)))
				continue;
			Out.m_vAllVisibleCards.push_back(std::move(Card));
		}
	}

	bool PerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogQmPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/qmclient", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	[[maybe_unused]] void LogTClientPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!PerfDebugEnabled())
			return;
		QmPerfLogStage("perf/tclient", pStage, DurationMs, Force, nullptr, nullptr, nullptr, pExtra);
	}

	const char *QmSettingsTabName(int Tab)
	{
		switch(Tab)
		{
		case CMenus::QMCLIENT_SETTINGS_TAB_VISUAL: return "visuals";
		case CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION: return "functions";
		case CMenus::QMCLIENT_SETTINGS_TAB_HUD: return "hud";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONTRIBUTORS: return "contributors";
		case CMenus::QMCLIENT_SETTINGS_TAB_CONFIG: return "config";
		case CMenus::QMCLIENT_SETTINGS_TAB_BIND: return "bind";
		default: return "unknown";
		}
	}

	struct SSectionCullContext
	{
		float m_ViewportTop;
		float m_ViewportBottom;
		float m_PrefetchPadding;
	};

	bool IsSectionVisible(const CUIRect &SectionRect, const SSectionCullContext &Context)
	{
		return SectionRect.y + SectionRect.h >= Context.m_ViewportTop - Context.m_PrefetchPadding &&
		       SectionRect.y <= Context.m_ViewportBottom + Context.m_PrefetchPadding;
	}

	uint64_t HashBytesFnv1a64(uint64_t Hash, const void *pData, size_t DataSize)
	{
		const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
		for(size_t i = 0; i < DataSize; ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	template<typename T>
	uint64_t HashValueFnv1a64(uint64_t Hash, const T &Value)
	{
		return HashBytesFnv1a64(Hash, &Value, sizeof(Value));
	}

	uint64_t HashStringFnv1a64(uint64_t Hash, const char *pString)
	{
		return pString == nullptr ? Hash : HashBytesFnv1a64(Hash, pString, str_length(pString));
	}

}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRulePlain
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRuleInputRow
{
	char m_aTrigger[512] = "";
	char m_aReply[256] = "";
	int m_AutoRename = 0;
	int m_Regex = 0;
	CLineInput m_TriggerInput;
	CLineInput m_ReplyInput;

	SAutoReplyRuleInputRow()
	{
		m_TriggerInput.SetBuffer(m_aTrigger, sizeof(m_aTrigger));
		m_ReplyInput.SetBuffer(m_aReply, sizeof(m_aReply));
	}
};

static std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> s_vKeywordRuleRows;
static bool s_KeywordRuleRowsInited = false;
static CButtonContainer s_KeywordAddRuleButton;
static std::vector<CButtonContainer> s_vKeywordRemoveRuleButtons;
static uint64_t s_BlockWordsLayoutRevision = 1;
static char s_aBlockWordsLayoutConfigCache[sizeof(g_Config.m_QmBlockWordsList)] = {};
static uint64_t s_KeywordRulesLayoutRevision = 1;
static size_t s_KeywordRulesLayoutCount = 0;
static bool s_KeywordRulesLayoutHalfFilled = false;
static char s_aKeywordRulesConfigCache[sizeof(g_Config.m_QmKeywordReplyRules)] = {};
static uint64_t s_FavoriteMapsLayoutRevision = 1;
static size_t s_FavoriteMapsLayoutCount = std::numeric_limits<size_t>::max();

static void UpdateKeywordRulesLayoutState(size_t RuleCount, bool HalfFilled)
{
	if(s_KeywordRulesLayoutCount == RuleCount && s_KeywordRulesLayoutHalfFilled == HalfFilled)
		return;
	s_KeywordRulesLayoutCount = RuleCount;
	s_KeywordRulesLayoutHalfFilled = HalfFilled;
	++s_KeywordRulesLayoutRevision;
}

static char *ParseAutoReplyRulePrefixes(char *pLine, bool &OutAutoRename, bool &OutRegex, bool &OutHasExplicitRenameFlag, bool &OutHasExplicitRegexFlag)
{
	OutAutoRename = false;
	OutRegex = false;
	OutHasExplicitRenameFlag = false;
	OutHasExplicitRegexFlag = false;

	char *pTrimmedLine = (char *)str_utf8_skip_whitespaces(pLine);
	while(true)
	{
		const char *pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rename]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[r]");
		if(pAfterPrefix)
		{
			OutAutoRename = true;
			OutHasExplicitRenameFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[regex]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[re]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rx]");
		if(pAfterPrefix)
		{
			OutRegex = true;
			OutHasExplicitRegexFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		break;
	}

	return pTrimmedLine;
}

static bool CopyTrimmedString(const char *pSrc, char *pOut, size_t OutSize)
{
	pOut[0] = '\0';
	if(!pSrc)
		return false;

	char aBuf[1024];
	str_copy(aBuf, pSrc, sizeof(aBuf));
	char *pTrimmed = (char *)str_utf8_skip_whitespaces(aBuf);
	str_utf8_trim_right(pTrimmed);
	str_copy(pOut, pTrimmed, OutSize);
	return pOut[0] != '\0';
}

static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "", bool AutoRename = false, bool Regex = false)
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
}

static void ParseAutoReplyRules(const char *pRules, std::vector<SAutoReplyRulePlain> &vOutRules)
{
	vOutRules.clear();
	if(!pRules || pRules[0] == '\0')
		return;

	const char *pCursor = pRules;
	while(*pCursor)
	{
		char aLine[1024];
		int LineLen = 0;
		while(*pCursor && *pCursor != '\n' && *pCursor != '\r')
		{
			if(LineLen < (int)sizeof(aLine) - 1)
				aLine[LineLen++] = *pCursor;
			pCursor++;
		}
		aLine[LineLen] = '\0';

		while(*pCursor == '\n' || *pCursor == '\r')
			pCursor++;

		char *pLine = (char *)str_utf8_skip_whitespaces(aLine);
		str_utf8_trim_right(pLine);
		if(pLine[0] == '\0' || pLine[0] == '#')
			continue;

		bool AutoRename = false;
		bool RegexRule = false;
		bool HasExplicitRenameFlag = false;
		bool HasExplicitRegexFlag = false;
		char *pRuleText = ParseAutoReplyRulePrefixes(pLine, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag);
		(void)AutoRename;
		(void)RegexRule;
		(void)HasExplicitRenameFlag;
		(void)HasExplicitRegexFlag;

		const char *pArrowConst = str_find(pRuleText, "=>");
		if(!pArrowConst)
			continue;

		char *pArrow = pRuleText + (pArrowConst - pRuleText);
		*pArrow = '\0';
		pArrow += 2;

		char *pKeywords = (char *)str_utf8_skip_whitespaces(pRuleText);
		str_utf8_trim_right(pKeywords);
		char *pReply = (char *)str_utf8_skip_whitespaces(pArrow);
		str_utf8_trim_right(pReply);
		if(pKeywords[0] == '\0' || pReply[0] == '\0')
			continue;

		vOutRules.push_back({pKeywords, pReply, AutoRename, RegexRule});
	}
}

static size_t CountAutoReplyRules(const char *pRules)
{
	if(!pRules || pRules[0] == '\0')
		return 0;

	size_t Count = 0;
	const char *pCursor = pRules;
	while(*pCursor)
	{
		char aLine[1024];
		int LineLen = 0;
		while(*pCursor && *pCursor != '\n' && *pCursor != '\r')
		{
			if(LineLen < (int)sizeof(aLine) - 1)
				aLine[LineLen++] = *pCursor;
			pCursor++;
		}
		aLine[LineLen] = '\0';
		while(*pCursor == '\n' || *pCursor == '\r')
			pCursor++;

		char *pLine = (char *)str_utf8_skip_whitespaces(aLine);
		str_utf8_trim_right(pLine);
		if(pLine[0] == '\0' || pLine[0] == '#')
			continue;
		bool AutoRename = false;
		bool RegexRule = false;
		bool HasExplicitRenameFlag = false;
		bool HasExplicitRegexFlag = false;
		char *pRuleText = ParseAutoReplyRulePrefixes(pLine, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag);
		const char *pArrowConst = str_find(pRuleText, "=>");
		if(!pArrowConst)
			continue;
		char *pArrow = pRuleText + (pArrowConst - pRuleText);
		*pArrow = '\0';
		pArrow += 2;
		char *pKeywords = (char *)str_utf8_skip_whitespaces(pRuleText);
		str_utf8_trim_right(pKeywords);
		char *pReply = (char *)str_utf8_skip_whitespaces(pArrow);
		str_utf8_trim_right(pReply);
		if(pKeywords[0] != '\0' && pReply[0] != '\0')
			++Count;
	}
	return Count;
}

static bool AutoReplyRowsMatchRules(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, const std::vector<SAutoReplyRulePlain> &vRules)
{
	std::vector<SAutoReplyRulePlain> vCompleteRows;
	vCompleteRows.reserve(vRows.size());
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;
		vCompleteRows.push_back({aTrigger, aReply, pRow->m_AutoRename != 0, pRow->m_Regex != 0});
	}

	if(vCompleteRows.size() != vRules.size())
		return false;

	for(size_t i = 0; i < vCompleteRows.size(); ++i)
	{
		if(str_comp(vCompleteRows[i].m_Keywords.c_str(), vRules[i].m_Keywords.c_str()) != 0 ||
			str_comp(vCompleteRows[i].m_Reply.c_str(), vRules[i].m_Reply.c_str()) != 0 ||
			vCompleteRows[i].m_AutoRename != vRules[i].m_AutoRename ||
			vCompleteRows[i].m_Regex != vRules[i].m_Regex)
			return false;
	}
	return true;
}

static bool IsAutoReplyRuleRowHalfFilled(const SAutoReplyRuleInputRow &Row)
{
	char aTrigger[512];
	char aReply[256];
	const bool HasTrigger = CopyTrimmedString(Row.m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
	const bool HasReply = CopyTrimmedString(Row.m_ReplyInput.GetString(), aReply, sizeof(aReply));
	return HasTrigger != HasReply;
}

static void BuildAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, char *pOutRules, size_t OutRulesSize)
{
	pOutRules[0] = '\0';
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;

		if(pOutRules[0] != '\0')
			str_append(pOutRules, "\n", OutRulesSize);
		if(pRow->m_AutoRename != 0)
			str_append(pOutRules, "[rename] ", OutRulesSize);
		if(pRow->m_Regex != 0)
			str_append(pOutRules, "[regex] ", OutRulesSize);
		str_append(pOutRules, aTrigger, OutRulesSize);
		str_append(pOutRules, "=>", OutRulesSize);
		str_append(pOutRules, aReply, OutRulesSize);
	}
}

static float CalcQiaFenInputHeight(ITextRender *pTextRender, const char *pText, float Width, float TextFontSize, float LineSpacing, float MinHeight)
{
	const float VPadding = 2.0f;
	const float LineWidth = maximum(1.0f, Width - VPadding * 2.0f);
	const char *pMeasureText = (pText && pText[0] != '\0') ? pText : " ";
	const STextBoundingBox Box = pTextRender->TextBoundingBox(TextFontSize, pMeasureText, -1, LineWidth, LineSpacing);
	return maximum(MinHeight, Box.m_H + VPadding * 2.0f);
}

[[maybe_unused]] static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

[[maybe_unused]] static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = std::clamp(Tab, 0, NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1);

	const int PreviousTab = m_QmClientSettingsTab;
	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;

	g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
	m_QmClientSettingsTab = Tab;
	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	RenderSettings(MainView);
	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	m_QmClientSettingsTab = PreviousTab;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

CMenus::SSettingsQmScrollFrame CMenus::BeginSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, CUIRect *pView, float ContentHeight, const SQmSettingsCardStyle &CardStyle, float UiScale, float PreviousOffsetY, bool Enabled)
{
	SSettingsQmScrollFrame Frame;
	Frame.m_ViewRect = *pView;
	Frame.m_ClipRect = *pView;
	Frame.m_PreviousOffsetY = PreviousOffsetY;
	Frame.m_Enabled = Enabled;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, g_Config.m_UiSmoothScrollTime / 1000.0f);
	Frame.m_Style = ScrollPolicy.m_Style;
	(void)CardStyle;
	if(!Enabled)
		return Frame;

	const SQmScrollConfig ScrollConfig = QmSettingsScrollConfig(UiScale, g_Config.m_UiSmoothScrollTime / 1000.0f);

	SQmScrollContainerInput ScrollInput;
	ScrollInput.m_Hovered = Ui()->MouseHovered(pView);
	ScrollInput.m_MouseValid = true;
	ScrollInput.m_MouseX = Ui()->MouseX();
	ScrollInput.m_MouseY = Ui()->MouseY();
	ScrollInput.m_MouseDown = Ui()->MouseButton(0);
	ScrollInput.m_MousePressed = Ui()->MouseButtonClicked(0);

	const SQmScrollContainerFrame ProbeFrame = ScrollContainer.PreviewFrame(ScrollState, *pView, ContentHeight, Frame.m_Style);
	CUIRect WheelHotRect = ProbeFrame.m_ClipRect;
	if(ProbeFrame.m_ScrollbarVisible)
		WheelHotRect.w += Frame.m_Style.m_ScrollbarWidth;
	ScrollInput.m_Hovered = Ui()->MouseHovered(&WheelHotRect);
	ScrollInput.m_ModifierPressed = Input()->ModifierIsPressed();
	ScrollInput.m_AltPressed = Input()->AltIsPressed();

	if(ProbeFrame.m_ScrollbarVisible)
	{
		const void *pScrollbarId = &ScrollContainer;
		ScrollInput.m_ThumbHovered = Ui()->MouseHovered(&ProbeFrame.m_ScrollbarThumbRect);
		ScrollInput.m_TrackHovered = Ui()->MouseHovered(&ProbeFrame.m_ScrollbarTrackRect) && !ScrollInput.m_ThumbHovered;
		if(ScrollInput.m_ThumbHovered || ScrollInput.m_TrackHovered)
			Ui()->SetHotItem(pScrollbarId);
		if((Ui()->HotItem() == pScrollbarId || ScrollInput.m_ThumbHovered || ScrollInput.m_TrackHovered) && ScrollInput.m_MousePressed)
			Ui()->SetActiveItem(pScrollbarId);
		if(Ui()->CheckActiveItem(pScrollbarId))
		{
			ScrollInput.m_ThumbHovered = ScrollInput.m_ThumbHovered || ScrollContainer.ScrollbarDragActive(ScrollState);
			ScrollInput.m_TrackHovered = ScrollInput.m_TrackHovered && !ScrollContainer.ScrollbarDragActive(ScrollState);
			if(!ScrollInput.m_MouseDown)
				Ui()->SetActiveItem(nullptr);
		}
	}

	Frame.m_Frame = ScrollContainer.Update(ScrollState, *pView, ContentHeight, GameClient()->UiRuntimeV2()->FrameDt(), ScrollInput, Frame.m_Style, ScrollConfig);
	Frame.m_ClipRect = Frame.m_Frame.m_ClipRect;
	Frame.m_Offset.y = -Frame.m_Frame.m_Offset;
	*pView = Frame.m_ClipRect;
	Ui()->ClipEnable(&Frame.m_ClipRect);
	return Frame;
}

void CMenus::RenderQmSettingsSliderWithValueInput(const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix, bool PrewarmOnly, unsigned Flags)
{
	const int OriginalValue = *pValue;
	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(pId);
	ui_widget::SNumericFieldOptions Options;
	Options.m_Flags = Flags;
	Options.m_pSuffix = pSuffix;
	Options.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
	Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;

	const float UiScale = std::clamp(ControlColumn.h / ui_token::settings::ROW_HEIGHT, 0.78f, 1.0f);
	IUiContext InputCtx = SettingsUiContext("qmclient_slider_input", UiScale);
	if(PrewarmOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}
	ui_widget::NumericField(InputCtx, pState, pId, pValue, MinValue, MaxValue, ControlColumn, Options);
	if(PrewarmOnly || Ui()->RenderOnly())
		*pValue = OriginalValue;
}

// 被禅模式/Gores 等临时接管的配置项：设置页灰化显示并提示接管来源；未被接管返回 nullptr。
static const char *QmTemporaryOverrideTooltip(const char *pOwnerId)
{
	if(pOwnerId == nullptr)
		return nullptr;
	if(str_comp(pOwnerId, "qm_zen_mode") == 0)
		return Localize("Controlled by Zen mode");
	if(str_comp(pOwnerId, "qm_gores_mode") == 0)
		return Localize("Controlled by Gores mode");
	return Localize("Temporarily controlled by a mode toggle");
}

const char *CMenus::TemporaryOverrideTooltip(const int *pValue) const
{
	return QmTemporaryOverrideTooltip(ConfigManager() != nullptr ? ConfigManager()->SaveValueOverrideOwner(pValue) : nullptr);
}

bool CMenus::RenderQmFunctionCheckbox(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly, const char *pTooltip)
{
	const int OriginalValue = *pValue;
	const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
	SLabelProperties LabelProps;
	if(pOverrideTooltip != nullptr)
	{
		LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
		// 灰化行用 ProcessInput=false 绘制，不会自己占 hover；补一次只读的按钮逻辑
		// 让 HotItem 指向本行，CTooltips 才会激活提示（返回值丢弃，不写值）。
		if(!PrewarmOnly && !Ui()->RenderOnly())
		{
			Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(pId, pRect, pOverrideTooltip);
		}
	}
	// 被临时接管的项灰化并停止响应点击：接管期间用户改它会被接管逻辑覆盖。
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pId, pTextId, pText, *pValue, pRect, LabelProps, pOverrideTooltip == nullptr) != 0;
	if(Changed)
		*pValue ^= 1;
	if(pTooltip != nullptr && !PrewarmOnly && !Ui()->RenderOnly())
		GameClient()->m_Tooltips.DoToolTip(pId, pRect, pTooltip);
	if(PrewarmOnly || Ui()->RenderOnly())
		*pValue = OriginalValue;
	return Changed;
}

bool CMenus::RenderQmVisualCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
	SLabelProperties LabelProps;
	if(pOverrideTooltip != nullptr)
	{
		LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
		// 灰化行不占 hover，补一次只读的按钮逻辑让提示能激活（返回值丢弃，不写值）。
		if(!Ui()->RenderOnly())
		{
			Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(pId, &Row, pOverrideTooltip);
		}
	}
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pId, pTextId, pText, *pValue, &Row, LabelProps, pOverrideTooltip == nullptr) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmVisualLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign, const SLabelProperties &LabelProps)
{
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
}

void CMenus::RenderQmVisualStreamerContent(CUIRect &Content, float LineHeight, float LineSpacing)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerHideNames, "Replace non-friend names with ID", Localize("Replace non-friend names with ID"), &g_Config.m_QmStreamerHideNames);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerHideSkins, "Replace non-friend skins with default", Localize("Replace non-friend skins with default"), &g_Config.m_QmStreamerHideSkins);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmStreamerScoreboardDefaultFlags, "Use default flags on scoreboard", Localize("Use default flags on scoreboard"), &g_Config.m_QmStreamerScoreboardDefaultFlags);
}

void CMenus::RenderQmVisualTranslateUiContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing)
{
	NTranslateUiSettings::RenderTranslateUiModule(this, Content, LineHeight, BodySize, LineSpacing);
}

void CMenus::RenderQmVisualEntityOverlayContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderSlider = [&](const void *pInputId, int *pValue, const char *pTitle) {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		Ui()->DoLabel(&LabelColumn, pTitle, BodySize, TEXTALIGN_ML);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, 0, 100, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	static int s_QmEntityOverlayDeathAlphaInputId;
	static int s_QmEntityOverlayFreezeAlphaInputId;
	static int s_QmEntityOverlayUnfreezeAlphaInputId;
	static int s_QmEntityOverlayDeepFreezeAlphaInputId;
	static int s_QmEntityOverlayDeepUnfreezeAlphaInputId;
	static int s_QmEntityOverlayTeleAlphaInputId;
	static int s_QmEntityOverlayTeleCheckpointAlphaInputId;
	static int s_QmEntityOverlaySwitchAlphaInputId;
	static int s_ClOverlayEntitiesInputId;
	RenderSlider(&s_QmEntityOverlayDeathAlphaInputId, &g_Config.m_QmEntityOverlayDeathAlpha, Localize("Death opacity"));
	RenderSlider(&s_QmEntityOverlayFreezeAlphaInputId, &g_Config.m_QmEntityOverlayFreezeAlpha, Localize("Freeze opacity"));
	RenderSlider(&s_QmEntityOverlayUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayUnfreezeAlpha, Localize("Unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepFreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepFreezeAlpha, Localize("Deep freeze opacity"));
	RenderSlider(&s_QmEntityOverlayDeepUnfreezeAlphaInputId, &g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, Localize("Deep unfreeze opacity"));
	RenderSlider(&s_QmEntityOverlayTeleAlphaInputId, &g_Config.m_QmEntityOverlayTeleAlpha, Localize("Teleport opacity"));
	RenderSlider(&s_QmEntityOverlayTeleCheckpointAlphaInputId, &g_Config.m_QmEntityOverlayTeleCheckpointAlpha, Localize("CP opacity"));
	RenderSlider(&s_QmEntityOverlaySwitchAlphaInputId, &g_Config.m_QmEntityOverlaySwitchAlpha, Localize("Switch opacity"));
	RenderSlider(&s_ClOverlayEntitiesInputId, &g_Config.m_ClOverlayEntities, Localize("Tune layer opacity"));
}

void CMenus::RenderQmVisualCollisionHitboxContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pId, pTextId, pText, *pValue, &Row))
			*pValue ^= 1;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	int HitboxModeEnabled = g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox;
	{
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmHitboxMode, "Show hitbox mode", Localize("Show hitbox mode"), HitboxModeEnabled, &Row))
		{
			HitboxModeEnabled ^= 1;
			g_Config.m_QmHitboxMode = HitboxModeEnabled;
			g_Config.m_QmShowCollisionHitbox = 0;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(!HitboxModeEnabled)
		return;

	RenderCheckbox(&g_Config.m_QmHitboxShowMap, "Map danger border", Localize("Map danger border"), &g_Config.m_QmHitboxShowMap);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeCollision, "Tee collision (Tee to Tee)", Localize("Tee collision (Tee to Tee)"), &g_Config.m_QmHitboxShowTeeCollision);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeFreeze, "Tee freeze probe (Tee to Freeze)", Localize("Tee freeze probe (Tee to Freeze)"), &g_Config.m_QmHitboxShowTeeFreeze);
	RenderCheckbox(&g_Config.m_QmHitboxShowTeeDeath, "Tee death probe", Localize("Tee death probe"), &g_Config.m_QmHitboxShowTeeDeath);
	RenderCheckbox(&g_Config.m_QmHitboxShowPickups, "Pickup range", Localize("Pickup range"), &g_Config.m_QmHitboxShowPickups);
	RenderCheckbox(&g_Config.m_QmHitboxShowHammer, "Hammer interaction", Localize("Hammer interaction"), &g_Config.m_QmHitboxShowHammer);
	RenderCheckbox(&g_Config.m_QmHitboxShowProjectiles, "Projectile / explosion range", Localize("Projectile / explosion range"), &g_Config.m_QmHitboxShowProjectiles);
	RenderCheckbox(&g_Config.m_QmHitboxShowLasers, "Laser / shotgun interaction", Localize("Laser / shotgun interaction"), &g_Config.m_QmHitboxShowLasers);
	RenderCheckbox(&g_Config.m_QmHitboxShowFreezeLasers, "Freeze laser collision volume", Localize("Freeze laser collision volume"), &g_Config.m_QmHitboxShowFreezeLasers);
	RenderCheckbox(&g_Config.m_QmHitboxShowHook, "Hook interaction", Localize("Hook interaction"), &g_Config.m_QmHitboxShowHook);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	static std::vector<const char *> s_HitboxScopeDropDownNames;
	s_HitboxScopeDropDownNames = {Localize("Local only"), Localize("Local + Dummy"), Localize("All players")};
	static CUi::SDropDownState s_HitboxScopeDropDownState;
	static CScrollRegion s_HitboxScopeDropDownScrollRegion;
	s_HitboxScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HitboxScopeDropDownScrollRegion;
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "qmclient-hitbox-player-range", &LabelColumn, Localize("Player range"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	const int HitboxScope = std::clamp(g_Config.m_QmHitboxPlayerScope, 0, 2);
	const int HitboxScopeNew = DoSettingsDropDown(&ControlColumn, HitboxScope, s_HitboxScopeDropDownNames.data(), s_HitboxScopeDropDownNames.size(), s_HitboxScopeDropDownState);
	if(g_Config.m_QmHitboxPlayerScope != HitboxScopeNew)
		g_Config.m_QmHitboxPlayerScope = HitboxScopeNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_FreezeColorId;
	DoLine_ColorPicker(&s_FreezeColorId, CurrentSettingsContentMetrics(), &Content, Localize("Freeze border color"), &g_Config.m_QmHitboxColorFreeze, ColorRGBA(1.0f, 0.0f, 1.0f), false);
	static CButtonContainer s_TeeColorId;
	DoLine_ColorPicker(&s_TeeColorId, CurrentSettingsContentMetrics(), &Content, Localize("Tee hitbox color"), &g_Config.m_QmHitboxColorTee, ColorRGBA(0.0f, 1.0f, 1.0f), false);
	static CButtonContainer s_WeaponColorId;
	DoLine_ColorPicker(&s_WeaponColorId, CurrentSettingsContentMetrics(), &Content, Localize("Weapon range color"), &g_Config.m_QmHitboxColorWeapon, ColorRGBA(1.0f, 1.0f, 0.0f), false);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, "qmclient-hitbox-opacity", &LabelColumn, Localize("Opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmHitboxAlphaInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmHitboxAlphaInputId, ControlColumn, &g_Config.m_QmHitboxAlpha, 0, 100, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmVisualWeaponAnimationContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim, "Weapon switch animation", Localize("Weapon switch animation"), &g_Config.m_QmWeaponSwitchAnim);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, "Play a flip animation while reloading weapons", Localize("Play a flip animation while reloading weapons"), &g_Config.m_QmWeaponReloadAnim);
	Content.HSplitTop(ContentGap, nullptr, &Content);

	// 锤子旋转模式：锤子像其他武器一样跟随准星旋转。独立于开关动画选项，
	// 不受上方动画开关 early-return 影响（自视觉页字体区迁移至此武器动画卡片）。
	{
		CUIRect HammerRow, HammerLabel, HammerControl;
		Content.HSplitTop(LineHeight, &HammerRow, &Content);
		HammerRow.VSplitLeft(LabelWidth, &HammerLabel, &HammerControl);
		RenderQmVisualLabel("qmclient-hammer-mode", &HammerLabel, Localize("Hammer Mode"), BodySize);
		static std::vector<const char *> s_HammerModeDropDownNames;
		s_HammerModeDropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_HammerModeDropDownState;
		static CScrollRegion s_HammerModeDropDownScrollRegion;
		s_HammerModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HammerModeDropDownScrollRegion;
		const int HammerMode = std::clamp(g_Config.m_TcHammerRotatesWithCursor, 0, 2);
		const int NewHammerMode = DoSettingsDropDown(&HammerControl, HammerMode, s_HammerModeDropDownNames.data(), s_HammerModeDropDownNames.size(), s_HammerModeDropDownState);
		if(g_Config.m_TcHammerRotatesWithCursor != NewHammerMode)
			g_Config.m_TcHammerRotatesWithCursor = NewHammerMode;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	if(g_Config.m_QmWeaponReloadAnim)
	{
		static int s_QmWeaponReloadAnimProbabilityInputId;
		RenderValue("qmclient-weapon-reload-animation-probability", "Weapon reload animation probability", &s_QmWeaponReloadAnimProbabilityInputId, &g_Config.m_QmWeaponReloadAnimProbability, 0, 100, "%");
	}

	if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	static std::vector<const char *> s_WeaponSwitchAnimScopeDropDownNames;
	s_WeaponSwitchAnimScopeDropDownNames = {Localize("Self only"), Localize("Local"), Localize("All players")};
	static CUi::SDropDownState s_WeaponSwitchAnimScopeDropDownState;
	static CScrollRegion s_WeaponSwitchAnimScopeDropDownScrollRegion;
	s_WeaponSwitchAnimScopeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponSwitchAnimScopeDropDownScrollRegion;
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-weapon-switch-animation-range", &LabelColumn, Localize("Animation range"), BodySize);
	const int Scope = std::clamp(g_Config.m_QmWeaponSwitchAnimScope, 0, 2);
	const int NewScope = DoSettingsDropDown(&ControlColumn, Scope, s_WeaponSwitchAnimScopeDropDownNames.data(), s_WeaponSwitchAnimScopeDropDownNames.size(), s_WeaponSwitchAnimScopeDropDownState);
	if(g_Config.m_QmWeaponSwitchAnimScope != NewScope)
		g_Config.m_QmWeaponSwitchAnimScope = NewScope;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(!g_Config.m_QmWeaponSwitchAnim)
		return;

	static int s_QmWeaponSwitchAnimDurationInputId;
	static int s_QmWeaponSwitchAnimDistanceInputId;
	static int s_QmWeaponSwitchAnimRotationInputId;
	RenderValue("qmclient-weapon-switch-duration", "Weapon switch duration", &s_QmWeaponSwitchAnimDurationInputId, &g_Config.m_QmWeaponSwitchAnimDurationMs, 50, 2000, "ms");
	RenderValue("qmclient-weapon-switch-distance", "Weapon switch distance", &s_QmWeaponSwitchAnimDistanceInputId, &g_Config.m_QmWeaponSwitchAnimDistance, 0, 100);
	RenderValue("qmclient-weapon-switch-rotation", "Weapon switch rotation", &s_QmWeaponSwitchAnimRotationInputId, &g_Config.m_QmWeaponSwitchAnimRotation, 0, 1440, "deg");

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-weapon-switch-easing", &LabelColumn, Localize("Weapon switch easing"), BodySize);
	static std::vector<const char *> s_WeaponSwitchAnimEasingDropDownNames;
	s_WeaponSwitchAnimEasingDropDownNames = {Localize("Ease out cubic"), Localize("Elastic back"), Localize("Linear"), Localize("Ease in out quad")};
	static CUi::SDropDownState s_WeaponSwitchAnimEasingDropDownState;
	static CScrollRegion s_WeaponSwitchAnimEasingDropDownScrollRegion;
	s_WeaponSwitchAnimEasingDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponSwitchAnimEasingDropDownScrollRegion;
	const int Easing = std::clamp(g_Config.m_QmWeaponSwitchAnimEasing, 0, 3);
	const int NewEasing = DoSettingsDropDown(&ControlColumn, Easing, s_WeaponSwitchAnimEasingDropDownNames.data(), s_WeaponSwitchAnimEasingDropDownNames.size(), s_WeaponSwitchAnimEasingDropDownState);
	if(g_Config.m_QmWeaponSwitchAnimEasing != NewEasing)
		g_Config.m_QmWeaponSwitchAnimEasing = NewEasing;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmVisualChatBubbleContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubble, "qmclient-chat-bubble-enable", Localize("Show chat bubbles above players"), &g_Config.m_QmChatBubble);
	if(!g_Config.m_QmChatBubble)
		return;

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int Min, int Max, const char *pSuffix = "") {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmChatBubbleDurationInputId;
	static int s_QmChatBubbleAlphaInputId;
	static int s_QmChatBubbleFontSizeInputId;
	RenderValue("qmclient-chat-bubble-duration", "Duration", &s_QmChatBubbleDurationInputId, &g_Config.m_QmChatBubbleDuration, 1, 30, "s");
	RenderValue("qmclient-chat-bubble-opacity", "Bubble opacity", &s_QmChatBubbleAlphaInputId, &g_Config.m_QmChatBubbleAlpha, 0, 100, "%");
	RenderValue("qmclient-chat-bubble-font-size", "Font size", &s_QmChatBubbleFontSizeInputId, &g_Config.m_QmChatBubbleFontSize, 8, 32);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-chat-bubble-animation", &LabelColumn, Localize("Animation"), BodySize);
	static std::vector<const char *> s_ChatBubbleAnimDropDownNames;
	s_ChatBubbleAnimDropDownNames = {Localize("Dissolve"), Localize("Shrink"), Localize("Bounce")};
	static CUi::SDropDownState s_ChatBubbleAnimDropDownState;
	static CScrollRegion s_ChatBubbleAnimDropDownScrollRegion;
	s_ChatBubbleAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimDropDownScrollRegion;
	const int Animation = DoSettingsDropDown(&ControlColumn, g_Config.m_QmChatBubbleAnimation, s_ChatBubbleAnimDropDownNames.data(), s_ChatBubbleAnimDropDownNames.size(), s_ChatBubbleAnimDropDownState);
	if(g_Config.m_QmChatBubbleAnimation != Animation)
		g_Config.m_QmChatBubbleAnimation = Animation;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_ChatBubbleBgColorId;
	static CButtonContainer s_ChatBubbleTextColorId;
	DoLine_ColorPicker(&s_ChatBubbleBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("Background color"), &g_Config.m_QmChatBubbleBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f), false, nullptr, true);
	DoLine_ColorPicker(&s_ChatBubbleTextColorId, CurrentSettingsContentMetrics(), &Content, Localize("Text color"), &g_Config.m_QmChatBubbleTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
}

void CMenus::RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)
{
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	static CButtonContainer s_ReaderButtonFocusToggle, s_ClearButtonFocusToggle;
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmFocusMode, "qmclient-focus-mode-enable", Localize("Enable Zen mode"), &g_Config.m_QmFocusMode);
	CUIRect LeftColumn, RightColumn, Row;
	Content.VSplitMid(&LeftColumn, &RightColumn, ColumnGap);
	auto RenderSection = [&](CUIRect &Target, const char *pTextId, const char *pLabel) {
		Target.HSplitTop(SmallSize, &Row, &Target);
		TextRender()->TextColor(ColorRGBA(0.72f, 0.72f, 0.78f, 0.86f));
		RenderQmVisualLabel(pTextId, &Row, Localize(pLabel), SmallSize);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Target.HSplitTop(LineSpacing, nullptr, &Target);
	};
	auto RenderCheckbox = [&](CUIRect &Target, int *pConfig, const char *pTextId, const char *pLabel) {
		Target.HSplitTop(LineHeight, &Row, &Target);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pConfig, pTextId, Localize(pLabel), *pConfig, &Row))
			*pConfig ^= 1;
		Target.HSplitTop(LineSpacing, nullptr, &Target);
	};
	RenderSection(LeftColumn, "qmclient-focus-section-interface", "Interface");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideHud, "qmclient-focus-hide-hud", "Hide HUD");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideMapProgress, "qmclient-focus-hide-map-progress", "Hide map progress");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideInfoMessages, "qmclient-focus-hide-info-messages", "Hide kill/finish messages");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideScoreboard, "qmclient-focus-hide-scoreboard", "Hide scoreboard");
	RenderSection(LeftColumn, "qmclient-focus-section-players", "Players");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideNames, "qmclient-focus-hide-names", "Hide names");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideNameplates, "qmclient-focus-hide-nameplates", "Hide nameplates");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideDirectionIndicators, "qmclient-focus-hide-direction-indicators", "Hide direction indicators");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideGuideLines, "qmclient-focus-hide-guide-lines", "Hide guide lines");
	RenderSection(LeftColumn, "qmclient-focus-section-visuals", "Visuals");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideJumpEffects, "qmclient-focus-hide-jump-effects", "Hide jump effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideKillEffects, "qmclient-focus-hide-kill-effects", "Hide death/respawn effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideExplosionEffects, "qmclient-focus-hide-explosion-effects", "Hide explosion effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideFreezeEffects, "qmclient-focus-hide-freeze-effects", "Hide freeze effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideHammerEffects, "qmclient-focus-hide-hammer-effects", "Hide hammer effects");
	RenderCheckbox(LeftColumn, &g_Config.m_QmFocusModeHideMuzzleEffects, "qmclient-focus-hide-muzzle-effects", "Hide weapon muzzle flashes");
	RenderSection(RightColumn, "qmclient-focus-section-audio", "Audio");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteJumpSounds, "qmclient-focus-mute-jump-sounds", "Mute jump sounds");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteDeathSounds, "qmclient-focus-mute-death-sounds", "Mute death/respawn sounds");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeMuteHammerSounds, "qmclient-focus-mute-hammer-sounds", "Mute hammer sounds");
	RenderSection(RightColumn, "qmclient-focus-section-chat", "Chat");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideChat, "qmclient-focus-hide-chat", "Hide player messages");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideSystemInfoMessages, "qmclient-focus-hide-system-info-messages", "Hide join/version prompts");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideSystemMessages, "qmclient-focus-hide-system-messages", "Hide server prompt notifications");
	RenderCheckbox(RightColumn, &g_Config.m_QmFocusModeHideEcho, "qmclient-focus-hide-echo", "Hide Echo messages");
	Content.y = std::max(LeftColumn.y, RightColumn.y);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	CUIRect BindLabel, BindKey;
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	RenderQmVisualLabel("qmclient-focus-mode-key", &BindLabel, Localize("Zen mode key"), BodySize);
	CBindSlot FocusBind(KEY_UNKNOWN, KeyModifier::NONE);
	if(const auto FocusIt = g_CommandBindCache.find("toggle qm_focus_mode 0 1"); FocusIt != g_CommandBindCache.end())
		FocusBind = FocusIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonFocusToggle, &s_ClearButtonFocusToggle, &BindKey, FocusBind, false);
	if(Result.m_Bind != FocusBind)
	{
		if(FocusBind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(FocusBind.m_Key, "", false, FocusBind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "toggle qm_focus_mode 0 1", false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign(std::string("toggle qm_focus_mode 0 1"), Result.m_Bind);
		}
		else
			g_CommandBindCache.erase("toggle qm_focus_mode 0 1");
	}
}

void CMenus::RenderQmVisualCameraViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "", unsigned Flags = 0u) {
		CUIRect Row, LabelColumn, ControlColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly, Flags);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDrift, "Enable camera drift", Localize("Enable camera drift"), &g_Config.m_QmCameraDrift);
	if(g_Config.m_QmCameraDrift)
	{
		static int s_QmCameraDriftAmountInputId;
		static int s_QmCameraDriftSmoothnessInputId;
		RenderValue("qmclient-camera-drift-intensity", "Drift intensity", &s_QmCameraDriftAmountInputId, &g_Config.m_QmCameraDriftAmount, 0, 200);
		RenderValue("qmclient-camera-drift-smoothness", "Drift smoothness", &s_QmCameraDriftSmoothnessInputId, &g_Config.m_QmCameraDriftSmoothness, 0, 100, "%");
		RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDriftReverse, "Drift direction", Localize("Drift direction"), &g_Config.m_QmCameraDriftReverse);
	}
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDynamicFov, "Enable dynamic FOV", Localize("Enable dynamic FOV"), &g_Config.m_QmDynamicFov);
	if(g_Config.m_QmDynamicFov)
	{
		static int s_QmDynamicFovAmountInputId;
		static int s_QmDynamicFovSmoothnessInputId;
		RenderValue("qmclient-camera-dynamic-fov-intensity", "Dynamic FOV intensity", &s_QmDynamicFovAmountInputId, &g_Config.m_QmDynamicFovAmount, 0, 200);
		RenderValue("qmclient-camera-dynamic-fov-smoothness", "Dynamic FOV smoothness", &s_QmDynamicFovSmoothnessInputId, &g_Config.m_QmDynamicFovSmoothness, 0, 100, "%");
	}
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCinematicCamera, "Cinematic camera", Localize("Cinematic camera"), &g_Config.m_QmCinematicCamera);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, "Instant zoom reverse", Localize("Instant zoom reverse"), &g_Config.m_QmZoomInstantReverse);
	static int s_QmUiScaleInputId;
	RenderValue("qmclient-ui-scale", "UI scale", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
	const char *apAspectPresetNames[] = {Localize("Off"), "5:4", "4:3", "3:2", "16:9", "21:9", Localize("Custom")};
	static CUi::SDropDownState s_AspectPresetDropDownState;
	static CScrollRegion s_AspectPresetDropDownScrollRegion;
	s_AspectPresetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetDropDownScrollRegion;
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-camera-aspect-ratio-preset", &LabelColumn, Localize("Aspect ratio preset"), BodySize);
	const int CurrentPreset = std::clamp(g_Config.m_QmAspectPreset, 0, 6);
	const int NewPreset = DoSettingsDropDown(&ControlColumn, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetDropDownState);
	bool AspectChanged = NewPreset != CurrentPreset;
	if(AspectChanged)
	{
		g_Config.m_QmAspectPreset = NewPreset;
		switch(NewPreset)
		{
		case 1: g_Config.m_QmAspectRatio = 125; break;
		case 2: g_Config.m_QmAspectRatio = 133; break;
		case 3: g_Config.m_QmAspectRatio = 150; break;
		case 4: g_Config.m_QmAspectRatio = 178; break;
		case 5: g_Config.m_QmAspectRatio = 233; break;
		case 6:
			if(g_Config.m_QmAspectRatio < 100)
				g_Config.m_QmAspectRatio = 178;
			break;
		default: break;
		}
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmAspectPreset == 6)
	{
		static int s_QmAspectRatioInputId;
		const int OldAspectRatio = g_Config.m_QmAspectRatio;
		RenderValue("qmclient-camera-custom-aspect-ratio", "Custom aspect ratio", &s_QmAspectRatioInputId, &g_Config.m_QmAspectRatio, 100, 300);
		AspectChanged |= OldAspectRatio != g_Config.m_QmAspectRatio;
	}
	int EffectiveAspectValue = 0;
	switch(g_Config.m_QmAspectPreset)
	{
	case 1: EffectiveAspectValue = 125; break;
	case 2: EffectiveAspectValue = 133; break;
	case 3: EffectiveAspectValue = 150; break;
	case 4: EffectiveAspectValue = 178; break;
	case 5: EffectiveAspectValue = 233; break;
	case 6: EffectiveAspectValue = std::clamp(g_Config.m_QmAspectRatio, 100, 300); break;
	default: break;
	}
	Content.HSplitTop(BodySize, &Row, &Content);
	char aAspectInfo[128];
	if(EffectiveAspectValue > 0)
		str_format(aAspectInfo, sizeof(aAspectInfo), "%s %.2f:1", Localize("Current aspect ratio:"), EffectiveAspectValue / 100.0f);
	else
		str_copy(aAspectInfo, Localize("Current aspect ratio: Show default"), sizeof(aAspectInfo));
	Ui()->DoLabel(&Row, aAspectInfo, BodySize, TEXTALIGN_ML);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(AspectChanged && !PrewarmOnly)
		GameClient()->TClientComponent().QueueAspectApply();
}

void CMenus::FinishSettingsQmScrollContainer(CQmScrollState &ScrollState, CQmScrollContainer &ScrollContainer, SSettingsQmScrollFrame &Frame, const CUIRect &EndRect, float *pContentHeight, float *pPreviousOffsetY, bool TrackScrollActive)
{
	if(!Frame.m_Enabled)
		return;

	Ui()->ClipDisable();
	*pContentHeight = maximum(0.0f, std::ceil(EndRect.y + EndRect.h - (Frame.m_ClipRect.y + Frame.m_Offset.y)));
	Frame.m_Frame = ScrollContainer.PreviewFrame(ScrollState, Frame.m_ViewRect, *pContentHeight, Frame.m_Style);
	CUIRect WheelHotRect = Frame.m_Frame.m_ClipRect;
	if(Frame.m_Frame.m_ScrollbarVisible)
		WheelHotRect.w += Frame.m_Style.m_ScrollbarWidth;
	const void *pWheelOwnerId = &ScrollContainer;
	const bool WheelEligible = Frame.m_Frame.m_ScrollbarVisible && !Ui()->UnderlyingScrollBlocked() && Ui()->MouseHovered(&WheelHotRect);
	Ui()->RegisterWheelOwner(pWheelOwnerId, EUiWheelOwnerPriority::PAGE, WheelHotRect, WheelEligible);
	float WheelDelta = 0.0f;
	if(Ui()->TryConsumeWheel(pWheelOwnerId, &WheelDelta))
	{
		const SQmScrollConfig ScrollConfig = QmSettingsScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
		ScrollContainer.ScrollByWheel(ScrollState, WheelDelta, Frame.m_ViewRect.h, *pContentHeight, ScrollConfig);
		Frame.m_Frame = ScrollContainer.PreviewFrame(ScrollState, Frame.m_ViewRect, *pContentHeight, Frame.m_Style);
	}
	const float CurrentOffsetY = Frame.m_Frame.m_ScrollbarVisible ? Frame.m_Frame.m_Offset : 0.0f;
	if(TrackScrollActive)
	{
		m_SettingsScrollActive = m_SettingsScrollActive || absolute(CurrentOffsetY - Frame.m_PreviousOffsetY) > 0.01f;
		if(pPreviousOffsetY != nullptr)
			*pPreviousOffsetY = CurrentOffsetY;
	}
	if(Frame.m_Frame.m_ScrollbarVisible)
	{
		DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarTrackRect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), ColorRGBA(), Frame.m_Frame.m_ScrollbarTrackRect.w * 0.5f);
		DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarThumbRect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.34f), ColorRGBA(), Frame.m_Frame.m_ScrollbarThumbRect.w * 0.5f);
	}
}

bool CMenus::RenderQmHudCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, &Row) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

bool CMenus::HandleQmHudCheckboxInput(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	const CUIRect VisibleContent = Content;
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const CUIRect HitRect = Row.Intersection(VisibleContent);
	const bool Changed = HitRect.w > 0.0f && HitRect.h > 0.0f && Ui()->DoButtonLogic(pId, *pValue, &HitRect, BUTTONFLAG_LEFT) != 0;
	if(Changed)
		*pValue ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmHudLabel(const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign, const SLabelProperties &LabelProps)
{
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
}

void CMenus::RenderQmHudKeyBindRow(CUIRect &Content, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pLabel, const char *pCommand, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto CurrentBindIt = g_CommandBindCache.find(pCommand);
	if(CurrentBindIt != g_CommandBindCache.end())
		Bind = CurrentBindIt->second;

	CUIRect BindRow, BindLabel, BindKey;
	Content.HSplitTop(LineHeight, &BindRow, &Content);
	BindRow.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	Ui()->DoLabel(&BindLabel, pLabel, BodySize, TEXTALIGN_ML);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &BindKey, Bind, false);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign(std::string(pCommand), Result.m_Bind);
		}
		else
		{
			g_CommandBindCache.erase(pCommand);
		}
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionKeyBindsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	static CButtonContainer s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
		s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
		s_ReaderButton45Degrees, s_ClearButton45Degrees,
		s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
		s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
		s_ReaderButtonRightJump, s_ClearButtonRightJump,
		s_ReaderButtonWeaponTrajectory, s_ClearButtonWeaponTrajectory,
		s_ReaderButtonTimeoutDisconnect, s_ClearButtonTimeoutDisconnect;
	[[maybe_unused]] static CButtonContainer s_ReaderButtonDeepflyToggle, s_ClearButtonDeepflyToggle;

	RenderQmHudKeyBindRow(Content, s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
		Localize("HDF"), "+toggle cl_dummy_hammer 1 0", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
		Localize("DF"), "+fire; +toggle cl_dummy_hammer 1 0", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButton45Degrees, s_ClearButton45Degrees,
		Localize("45° Aim"), "echo You are using 45-degree aim;+toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
		Localize("Gap aim rescue"), "+toggle_restore inp_mousesens 1", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
		Localize("Left jump"), "+jump; +left", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonRightJump, s_ClearButtonRightJump,
		Localize("Right jump"), "+jump; +right", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonWeaponTrajectory, s_ClearButtonWeaponTrajectory,
		Localize("Weapon Trajectory"), "+showweapontrajectory", LineHeight, BodySize, LineSpacing, LabelWidth);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonTimeoutDisconnect, s_ClearButtonTimeoutDisconnect,
		Localize("Active disconnect"), "qm_timeout_disconnect", LineHeight, BodySize, LineSpacing, LabelWidth);
}

void CMenus::RenderQmFunctionGoresActorContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_gores_actor_text_inputs", BodySize / ui_token::font::BODY);
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_TcFreezeChatEnabled, "qmclient-gores-actor-enable", Localize("Auto chat in water"), &g_Config.m_TcFreezeChatEnabled, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!g_Config.m_TcFreezeChatEnabled)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_TcFreezeChatEmoticon, "qmclient-gores-actor-emoticon", Localize("Send emoticon in water"), &g_Config.m_TcFreezeChatEmoticon, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_TcFreezeChatEmoticon)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		DoSettingsScrollbarOption(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, "qmclient-gores-actor-emoticon-id", &g_Config.m_TcFreezeChatEmoticonId, &g_Config.m_TcFreezeChatEmoticonId, &Row, Localize("Emoticon ID"), 0, 15);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-gores-actor-chat-message", &LabelColumn, Localize("Chat message"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static CLineInput s_FreezeChatMessageQmClient(g_Config.m_TcFreezeChatMessage, sizeof(g_Config.m_TcFreezeChatMessage));
	s_FreezeChatMessageQmClient.SetEmptyText(Localize("Leave empty to disable"));
	ui_widget::InputField(TextInputCtx, &s_FreezeChatMessageQmClient, ControlColumn, Localize("Leave empty to disable"), BodySize);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoSettingsScrollbarOption(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, "qmclient-gores-actor-send-probability", &g_Config.m_TcFreezeChatChance, &g_Config.m_TcFreezeChatChance, &Row, Localize("Send probability"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionGoresContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	static CButtonContainer s_ReaderButtonGoresToggle, s_ClearButtonGoresToggle;
	static CButtonContainer s_AxiomPasswordToggleButton, s_AxiomDummyPasswordToggleButton;
	static bool s_ShowAxiomPassword = false;
	static bool s_ShowAxiomDummyPassword = false;
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmGores, "qmclient-gores-enable", "Enable Gores mode", &g_Config.m_QmGores);
	RenderCheckbox(&g_Config.m_QmAxiomAutoLogin, "qmclient-gores-axiom-auto-login", "Auto login Axiom server", &g_Config.m_QmAxiomAutoLogin);

	if(g_Config.m_QmAxiomAutoLogin)
	{
		IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_gores_text_inputs", BodySize / ui_token::font::BODY);
		auto RenderPassword = [&](const char *pTextId, const char *pText, CLineInput &Input, CButtonContainer &ToggleButton, bool &Visible) {
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			Input.SetHidden(!Visible);
			ui_widget::SInputFieldOptions Options;
			Options.m_FontSize = BodySize;
			Options.m_pTrailingActionId = &ToggleButton;
			Options.m_pTrailingActionIcon = Visible ? FONT_ICON_EYE_SLASH : FONT_ICON_EYE;
			Options.m_TrailingActionQmIcon = static_cast<int>(Visible ? EQmIcon::EYE_OFF : EQmIcon::EYE);
			Options.m_TrailingWidth = ControlColumn.h;
			if(ui_widget::InputField(TextInputCtx, &Input, ControlColumn, Options).m_TrailingAction)
				Visible = !Visible;
			Content.HSplitTop(LineSpacing, nullptr, &Content);
		};
		static CLineInput s_AxiomLoginPassword(g_Config.m_QmAxiomLoginPassword, sizeof(g_Config.m_QmAxiomLoginPassword));
		static CLineInput s_AxiomDummyLoginPassword(g_Config.m_QmAxiomDummyLoginPassword, sizeof(g_Config.m_QmAxiomDummyLoginPassword));
		RenderPassword("qmclient-gores-axiom-main-password", "Axiom main account password", s_AxiomLoginPassword, s_AxiomPasswordToggleButton, s_ShowAxiomPassword);
		RenderPassword("qmclient-gores-axiom-dummy-password", "Axiom dummy password", s_AxiomDummyLoginPassword, s_AxiomDummyPasswordToggleButton, s_ShowAxiomDummyPassword);
	}

	RenderCheckbox(&g_Config.m_QmGoresAutoEnable, "qmclient-gores-auto-enable", "Auto enable in Gores mode", &g_Config.m_QmGoresAutoEnable);
	if(g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable)
	{
		RenderCheckbox(&g_Config.m_QmGoresAutoWeaponSwitch, "qmclient-gores-auto-weapon-switch", "Auto weapon switch", &g_Config.m_QmGoresAutoWeaponSwitch);
		RenderCheckbox(&g_Config.m_QmGoresFastInput, "qmclient-gores-fast-input", "Auto-toggle fast input", &g_Config.m_QmGoresFastInput);
		RenderCheckbox(&g_Config.m_QmGoresFastInputOthers, "qmclient-gores-fast-input-others", "Auto-toggle fast input others", &g_Config.m_QmGoresFastInputOthers);
		RenderCheckbox(&g_Config.m_QmGoresDisableIfWeapons, "qmclient-gores-disable-if-weapons", "Disable after picking up other weapons", &g_Config.m_QmGoresDisableIfWeapons);
		RenderCheckbox(&g_Config.m_QmGoresDisableDummyHammer, "qmclient-gores-disable-dummy-hammer", "Temporarily disable dummy hammering", &g_Config.m_QmGoresDisableDummyHammer);
		RenderCheckbox(&g_Config.m_QmGoresHideGuides, "qmclient-gores-hide-guides", "Hide guide lines", &g_Config.m_QmGoresHideGuides);
		RenderCheckbox(&g_Config.m_QmGoresSuppressSwitchAnim, "qmclient-gores-suppress-switch-anim", "Skip switch animation when hammering", &g_Config.m_QmGoresSuppressSwitchAnim);
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	CUIRect BindLabel, BindKey;
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-gores-mode-key", &BindLabel, Localize("Gores mode key"), BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
	CBindSlot GoresBind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto GoresIt = g_CommandBindCache.find("toggle qm_gores 0 1");
	if(GoresIt != g_CommandBindCache.end())
		GoresBind = GoresIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonGoresToggle, &s_ClearButtonGoresToggle, &BindKey, GoresBind, false);
	if(Result.m_Bind == GoresBind)
		return;
	if(GoresBind.m_Key != KEY_UNKNOWN)
		GameClient()->m_Binds.Bind(GoresBind.m_Key, "", false, GoresBind.m_ModifierMask);
	if(Result.m_Bind.m_Key != KEY_UNKNOWN)
	{
		GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "toggle qm_gores 0 1", false, Result.m_Bind.m_ModifierMask);
		g_CommandBindCache.insert_or_assign("toggle qm_gores 0 1", Result.m_Bind);
	}
	else
	{
		g_CommandBindCache.erase("toggle qm_gores 0 1");
	}
	(void)PrewarmOnly;
}

void CMenus::RenderQmFunctionSoloSplitContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	static CButtonContainer s_ReaderButtonSoloSplit, s_ClearButtonSoloSplit;
	static CButtonContainer s_ReaderButtonSoloSplitEnter, s_ClearButtonSoloSplitEnter;
	static CButtonContainer s_ReaderButtonSoloSplitLeave, s_ClearButtonSoloSplitLeave;
	static const void *s_RestoreTeamInputId = &s_RestoreTeamInputId;
	// 自动锁队配置与 HJAssist 卡共用 qm_auto_team_lock；HJAssist 已用配置地址作控件 id，
	// 两卡可能同屏显示，这里改用独立静态地址避免 hot/active 项串扰。
	static const void *s_AutoTeamLockCheckboxId = &s_AutoTeamLockCheckboxId;
	CUIRect Row, BindLabel, BindKey, LabelColumn, ControlColumn;
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmSoloSplitLinkDummy, "qmclient-solo-split-link-dummy", "Auto-connect dummy", &g_Config.m_QmSoloSplitLinkDummy);
	RenderCheckbox(s_AutoTeamLockCheckboxId, "qmclient-solo-split-auto-team-lock", "Auto team lock", &g_Config.m_QmAutoTeamLock);
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-solo-split-restore-team", &LabelColumn, Localize("Team when leaving solo split"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	RenderQmSettingsSliderWithValueInput(s_RestoreTeamInputId, ControlColumn, &g_Config.m_QmSoloSplitRestoreTeam, 0, 63, "", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// 只保留键位行：按下触发 qm_solo_split（toggle 语义，非 toggle 命令）。
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-solo-split-key", &BindLabel, Localize("Solo split key"), BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
	CBindSlot SoloSplitBind(KEY_UNKNOWN, KeyModifier::NONE);
	const auto SoloSplitIt = g_CommandBindCache.find("qm_solo_split");
	if(SoloSplitIt != g_CommandBindCache.end())
		SoloSplitBind = SoloSplitIt->second;
	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButtonSoloSplit, &s_ClearButtonSoloSplit, &BindKey, SoloSplitBind, false);
	if(Result.m_Bind != SoloSplitBind)
	{
		if(SoloSplitBind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(SoloSplitBind.m_Key, "", false, SoloSplitBind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
		{
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "qm_solo_split", false, Result.m_Bind.m_ModifierMask);
			g_CommandBindCache.insert_or_assign("qm_solo_split", Result.m_Bind);
		}
		else
			g_CommandBindCache.erase("qm_solo_split");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	auto RenderBind = [&](const char *pCommand, const char *pTextId, const char *pText, CButtonContainer &Reader, CButtonContainer &Clear) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &BindLabel, &BindKey);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &BindLabel, pText, BodySize, TEXTALIGN_ML, {}, (int)BindLabel.w);
		CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
		const auto It = g_CommandBindCache.find(pCommand);
		if(It != g_CommandBindCache.end())
			Bind = It->second;
		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&Reader, &Clear, &BindKey, Bind, false);
		if(Result.m_Bind != Bind)
		{
			if(Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			{
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
				g_CommandBindCache.insert_or_assign(pCommand, Result.m_Bind);
			}
			else
				g_CommandBindCache.erase(pCommand);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderBind("qm_solo_split_enter", "qmclient-solo-split-enter-key", Localize("Solo split enter key"), s_ReaderButtonSoloSplitEnter, s_ClearButtonSoloSplitEnter);
	RenderBind("qm_solo_split_leave", "qmclient-solo-split-leave-key", Localize("Solo split leave key"), s_ReaderButtonSoloSplitLeave, s_ClearButtonSoloSplitLeave);
	(void)PrewarmOnly;
}

void CMenus::RenderQmFunctionJumpHintContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmJumpHint, "Position jump hint", Localize("Position jump hint"), &g_Config.m_QmJumpHint, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_QmJumpHintColorId;
	DoLine_ColorPicker(&s_QmJumpHintColorId, CurrentSettingsContentMetrics(), &Content, Localize("Text color"), &g_Config.m_QmJumpHintColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmJumpHintXInputId;
	static int s_QmJumpHintYInputId;
	static int s_QmJumpHintSizeInputId;
	RenderValue("qmclient-jump-hint-horizontal-position", "Horizontal position", &s_QmJumpHintXInputId, &g_Config.m_QmJumpHintX, 0, 100, "%");
	RenderValue("qmclient-jump-hint-vertical-position", "Vertical position", &s_QmJumpHintYInputId, &g_Config.m_QmJumpHintY, 0, 100, "%");
	RenderValue("qmclient-jump-hint-font-size", "Font size", &s_QmJumpHintSizeInputId, &g_Config.m_QmJumpHintSize, 1, 50);
}

void CMenus::RenderQmFunctionWeaponTrajectoryContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	CUIElement &DisplayModeLabel = SettingsTextElement(SETTINGS_QMCLIENT, m_QmClientSettingsTab, "qmclient-display-mode");
	DoSettingsLabelStreamed(DisplayModeLabel, &LabelColumn, Localize("Display mode"), BodySize, TEXTALIGN_ML);
	static std::vector<const char *> s_WeaponTrajectoryModeNames;
	s_WeaponTrajectoryModeNames = {Localize("Off"), Localize("Show on key"), Localize("Always show")};
	static CUi::SDropDownState s_WeaponTrajectoryModeDropDownState;
	static CScrollRegion s_WeaponTrajectoryModeDropDownScrollRegion;
	s_WeaponTrajectoryModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WeaponTrajectoryModeDropDownScrollRegion;
	const int WeaponTrajectoryModeNew = DoSettingsDropDown(&ControlColumn, std::clamp(g_Config.m_QmWeaponTrajectory, 0, 2), s_WeaponTrajectoryModeNames.data(), s_WeaponTrajectoryModeNames.size(), s_WeaponTrajectoryModeDropDownState);
	if(g_Config.m_QmWeaponTrajectory != WeaponTrajectoryModeNew)
		g_Config.m_QmWeaponTrajectory = WeaponTrajectoryModeNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmWeaponTrajectory == 0)
		return;

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryGun, "qmclient-weapon-trajectory-gun", Localize("Pistol guide line"), &g_Config.m_QmWeaponTrajectoryGun, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryNinja, "qmclient-weapon-trajectory-ninja", Localize("Predict ninja path"), &g_Config.m_QmWeaponTrajectoryNinja, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_WeaponTrajectoryColorId;
	DoLine_ColorPicker(&s_WeaponTrajectoryColorId, CurrentSettingsContentMetrics(), &Content, Localize("Guide line color"), &g_Config.m_QmWeaponTrajectoryColor, ColorRGBA(1.0f, 0.6f, 0.2f, 1.0f), false);
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmWeaponTrajectoryWidthInputId;
	static int s_QmWeaponTrajectoryAlphaInputId;
	RenderValue("qmclient-weapon-trajectory-line-width", "Line width", &s_QmWeaponTrajectoryWidthInputId, &g_Config.m_QmWeaponTrajectoryWidth, 1, 10);
	RenderValue("qmclient-weapon-trajectory-opacity", "Opacity", &s_QmWeaponTrajectoryAlphaInputId, &g_Config.m_QmWeaponTrajectoryAlpha, 0, 100, "%");
}

void CMenus::RenderQmFunctionFriendNotifyContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_friend_enter_text_inputs", BodySize / ui_token::font::BODY);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pTextId, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderText = [&](const char *pTextId, const char *pText, CLineInput *pInput, const char *pPlaceholder) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		pInput->SetEmptyText(Localize(pPlaceholder));
		ui_widget::InputField(TextInputCtx, pInput, ControlColumn, Localize(pPlaceholder), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	static int s_QmFriendAutoFollowDelayInputId;
	static int s_QmFriendOnlineRefreshSecondsInputId;
	RenderCheckbox(&g_Config.m_QmFriendOnlineNotify, "Notify when friends come online", "Notify when friends come online", &g_Config.m_QmFriendOnlineNotify);
	RenderCheckbox(&g_Config.m_QmFriendOnlineAutoRefresh, "Auto refresh server list", "Auto refresh server list", &g_Config.m_QmFriendOnlineAutoRefresh);
	RenderValue("qmclient-friend-auto-follow-delay", "Auto-follow delay", &s_QmFriendAutoFollowDelayInputId, &g_Config.m_QmFriendAutoFollowDelay, 0, 30, "s");
	if(g_Config.m_QmFriendOnlineAutoRefresh)
		RenderValue("qmclient-friend-notifications-refresh-interval", "Refresh interval", &s_QmFriendOnlineRefreshSecondsInputId, &g_Config.m_QmFriendOnlineRefreshSeconds, 5, 300, "s");
	RenderCheckbox(&g_Config.m_QmFriendEnterAutoGreet, "Auto greet friends entering map", "Auto greet friends entering map", &g_Config.m_QmFriendEnterAutoGreet);
	RenderCheckbox(&g_Config.m_QmFriendEnterBroadcast, "Large text announcement for friend joining", "Large text announcement for friend joining", &g_Config.m_QmFriendEnterBroadcast);
	if(g_Config.m_QmFriendEnterBroadcast)
	{
		static CLineInput s_FriendEnterBroadcastText(g_Config.m_QmFriendEnterBroadcastText, sizeof(g_Config.m_QmFriendEnterBroadcastText));
		RenderText("qmclient-friend-notifications-large-text-content", "Large text content", &s_FriendEnterBroadcastText, "Please use %s as friend name");
	}
	if(g_Config.m_QmFriendEnterAutoGreet)
	{
		static CLineInput s_FriendEnterGreetText(g_Config.m_QmFriendEnterGreetText, sizeof(g_Config.m_QmFriendEnterGreetText));
		RenderText("qmclient-friend-notifications-greeting-text", "Greeting text", &s_FriendEnterGreetText, "Leave empty to disable");
	}
}

bool CMenus::ToggleQmHudCountdownLocation(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, int *pValue)
{
	// 本地差异：远程此入口只做按钮逻辑、不绘制标签（其卡片模块也未另画标签），
	// 而本地既有实现带可见文案（"Follow Tee" / "Show in Dynamic Island"）。
	// 按控件 id 选择对应标签，保留本地界面文案，避免迁出后标签消失。
	if(pId == qm_card_catalog::SwitchCountdownFollowTeeId())
		return RenderQmHudCheckbox(Content, LineHeight, LineSpacing, pId, "qmclient-switch-countdown-follow-tee", Localize("Follow Tee"), pValue);
	return RenderQmHudCheckbox(Content, LineHeight, LineSpacing, pId, "qmclient-switch-countdown-media-island", Localize("Show in Dynamic Island"), pValue);
}

void CMenus::RenderQmFunctionEmoticonsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth)
{
	// 自 MiniFeatures 列表迁出（R3）：表情相关开关独立成 qm:emoticons 卡，
	// 避免同一个开关在两张卡里各出现一次。文案 id 沿用本地约定（英文源串作为 textId），
	// 不使用远程的 "qm-emoticons-*" 键，避免 12 语文案整体退回英文。
	static CButtonContainer s_ReaderButtonLaunchEmote;
	static CButtonContainer s_ClearButtonLaunchEmote;
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmShowOtherSuperEmotes, "Show other players' large emoticons", Localize("Show other players' large emoticons"), &g_Config.m_QmShowOtherSuperEmotes, &Row, false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmShowOtherLaunchEmotes, "Show other players' launched emoticons", Localize("Show other players' launched emoticons"), &g_Config.m_QmShowOtherLaunchEmotes, &Row, false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	RenderQmHudKeyBindRow(Content, s_ReaderButtonLaunchEmote, s_ClearButtonLaunchEmote,
		Localize("Launch emote key"), "toggle_emote_launcher", LineHeight, BodySize, LineSpacing, LabelWidth);
}

void CMenus::RenderQmFunctionMiniFeaturesContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	auto RenderCheckboxTipped = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, const char *pTooltip, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly, pTooltip);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmFootParticles, "Local particle effects", &g_Config.m_QmFootParticles);
	RenderCheckbox(&g_Config.m_QmClientMarkTrail, "Remote particle effects", &g_Config.m_QmClientMarkTrail);
	RenderCheckbox(&g_Config.m_QmClientShowBadge, "Show Qm badge", &g_Config.m_QmClientShowBadge);
	RenderCheckbox(&g_Config.m_QmAutoUpdate, "Automatic updates", &g_Config.m_QmAutoUpdate);
	RenderCheckbox(&g_Config.m_QmShowOutdatedVersionWarning, "Show outdated version warning", &g_Config.m_QmShowOutdatedVersionWarning);
	// 计分板相关的 5 项（更好的计分板/积分检查/死亡后显示/滚轮滚动/过滤器）只在 qm:better_scoreboard 卡里渲染一次，
	// 这里不再重复，避免同一条配置在两张卡里各出现一遍。
	RenderCheckbox(&g_Config.m_QmHideJoinServerInfo, "Hide server information on join", &g_Config.m_QmHideJoinServerInfo);
	RenderCheckboxTipped(&g_Config.m_QmShowTuneZoneColors, "Show tune zone colors", Localize("Color map tune zones by their tune zone number"), &g_Config.m_QmShowTuneZoneColors);
	// 回退语义在加载期读取：开关变化由 CGameClient::OnRender 的兜底轮询统一触发热重载，
	// 这里只负责渲染复选框，不在渲染遍里做加载副作用。
	RenderCheckboxTipped(&g_Config.m_QmBlankAssetFallback, "Blank asset auto fallback", Localize("Automatically fall back to the default asset when a custom asset sprite is fully transparent; turn off to keep blank sprites invisible (e.g. to hide effects)"), &g_Config.m_QmBlankAssetFallback);
	RenderCheckbox(&g_Config.m_QmMessageMerge, "Message merging", &g_Config.m_QmMessageMerge);
	RenderCheckbox(&g_Config.m_QmShortServerNames, "Short server names", &g_Config.m_QmShortServerNames);
	RenderCheckbox(&g_Config.m_QmRepeatEnabled, "Enable repeat", &g_Config.m_QmRepeatEnabled);
	RenderCheckbox(&g_Config.m_QmRandomEmoteOnHit, "Random emoticon", &g_Config.m_QmRandomEmoteOnHit);
	// 两个表情开关已迁入 qm:emoticons 卡（RenderQmFunctionEmoticonsContent），此处不再重复渲染。
	RenderCheckbox(&g_Config.m_QmComboPopup, "Combo", &g_Config.m_QmComboPopup);
	RenderCheckbox(&g_Config.m_QmSayNoPop, "Hide input emoticon", &g_Config.m_QmSayNoPop);
	// 关闭赞助提醒时不弹确认框，而是用同样式的灵动岛问一句，
	// 避免把「关掉一个提醒」变成需要连点两次的操作。
	Content.HSplitTop(LineHeight, &Row, &Content);
	const int SponsorNudgeBefore = g_Config.m_QmSponsorNudge;
	RenderQmFunctionCheckbox(&g_Config.m_QmSponsorNudge, "Sponsor reminder", Localize("Sponsor reminder"), &g_Config.m_QmSponsorNudge, &Row, PrewarmOnly);
	if(!PrewarmOnly && SponsorNudgeBefore != 0 && g_Config.m_QmSponsorNudge == 0)
		GameClient()->ShowSponsorNudgeFarewell();
	else if(!PrewarmOnly && SponsorNudgeBefore == 0 && g_Config.m_QmSponsorNudge != 0)
		GameClient()->HideSponsorNudgeFarewell();
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionImeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, &g_Config.m_QmImeAutoManage, "Auto manage IME while typing", Localize("Auto manage IME while typing"), &g_Config.m_QmImeAutoManage, PrewarmOnly);
	RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, &g_Config.m_QmNewIme, "New IME", Localize("New IME"), &g_Config.m_QmNewIme, PrewarmOnly);
	static CButtonContainer s_ImeBgColorId;
	DoLine_AlphaColorPicker(&s_ImeBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("IME background color"), &g_Config.m_QmImeBgColor, &g_Config.m_QmImeOpacity, 0x1C1C1E, 96);
}

void CMenus::RenderQmFunctionBlockWordsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmBlockWordsShowConsole, "Show blocked words in console", &g_Config.m_QmBlockWordsShowConsole);
	static CButtonContainer s_BlockWordsConsoleColorId;
	DoLine_ColorPicker(&s_BlockWordsConsoleColorId, CurrentSettingsContentMetrics(), &Content, Localize("Console color"), &g_Config.m_QmBlockWordsConsoleColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
	RenderCheckbox(&g_Config.m_QmBlockWordsEnabled, "Enable word filter list", &g_Config.m_QmBlockWordsEnabled);

	const int BlockWordsAction = g_Config.m_QmBlockWordsAction;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-action", &LabelColumn, Localize("Behavior"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	CUIRect ActionRow = ControlColumn;
	CUIRect ActionButton;
	static CButtonContainer s_BlockWordsActionReplace, s_BlockWordsActionHide;
	const float ActionWidth = ActionRow.w / 2.0f;
	ActionRow.VSplitLeft(ActionWidth, &ActionButton, &ActionRow);
	if(DoButtonLineSize_Menu(&s_BlockWordsActionReplace, Localize("Replace mode"), BlockWordsAction == 0, &ActionButton, LineHeight, false, 0, IGraphics::CORNER_L, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_QmBlockWordsAction = 0;
	if(DoButtonLineSize_Menu(&s_BlockWordsActionHide, Localize("Hide player messages"), BlockWordsAction == 1, &ActionRow, LineHeight, false, 0, IGraphics::CORNER_R, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_QmBlockWordsAction = 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(BlockWordsAction == 0)
	{
		RenderCheckbox(&g_Config.m_QmBlockWordsMultiReplace, "Use multi-char replacement based on word length", &g_Config.m_QmBlockWordsMultiReplace);

		static CLineInputBuffered<8> s_BlockWordsReplaceInput;
		static bool s_BlockWordsReplaceInited = false;
		if(!s_BlockWordsReplaceInited)
		{
			s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
			s_BlockWordsReplaceInited = true;
		}
		else if(!s_BlockWordsReplaceInput.IsActive() && str_comp(s_BlockWordsReplaceInput.GetString(), g_Config.m_QmBlockWordsReplacementChar) != 0)
		{
			s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
		}
		s_BlockWordsReplaceInput.SetEmptyText("*");
		IUiContext ReplacementInputCtx = SettingsUiContext("settings_qmclient_block_words_text_inputs", UiScale);
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-replacement-chars", &LabelColumn, Localize("Replacement chars"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		if(ui_widget::InputField(ReplacementInputCtx, &s_BlockWordsReplaceInput, ControlColumn, "*", BodySize))
		{
			char aReplacement[8];
			str_utf8_truncate(aReplacement, sizeof(aReplacement), s_BlockWordsReplaceInput.GetString(), 1);
			if(aReplacement[0] == '\0')
				str_copy(aReplacement, "*", sizeof(aReplacement));
			str_copy(g_Config.m_QmBlockWordsReplacementChar, aReplacement, sizeof(g_Config.m_QmBlockWordsReplacementChar));
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-match-mode", &LabelColumn, Localize("Mode"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		CUIRect ModeRow = ControlColumn;
		CUIRect ModeButton;
		static CButtonContainer s_BlockWordsModeRegex, s_BlockWordsModeFull, s_BlockWordsModeBoth;
		const float ModeWidth = ModeRow.w / 3.0f;
		ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
		if(DoButtonLineSize_Menu(&s_BlockWordsModeRegex, Localize("Regular expression"), g_Config.m_QmBlockWordsMode == 0, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_L, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 0;
		ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
		if(DoButtonLineSize_Menu(&s_BlockWordsModeFull, Localize("Literal"), g_Config.m_QmBlockWordsMode == 1, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_NONE, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 1;
		if(DoButtonLineSize_Menu(&s_BlockWordsModeBoth, Localize("Both"), g_Config.m_QmBlockWordsMode == 2, &ModeRow, LineHeight, false, 0, IGraphics::CORNER_R, ui_token::radius::BASE, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 2;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	static CLineInputBuffered<1024> s_BlockWordsInput;
	static bool s_BlockWordsInited = false;
	if(!s_BlockWordsInited)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
		s_BlockWordsInited = true;
	}
	else if(!s_BlockWordsInput.IsActive() && str_comp(s_BlockWordsInput.GetString(), g_Config.m_QmBlockWordsList) != 0)
	{
		s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
	}
	s_BlockWordsInput.SetEmptyText(Localize("Separate with commas"));
	const float InputLineSpacing = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const float InputHeight = CalcQiaFenInputHeight(TextRender(), s_BlockWordsInput.GetString(), Content.w - LabelWidth, BodySize, InputLineSpacing, LineHeight);
	Content.HSplitTop(InputHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-word-filter-label", &LabelColumn, Localize("Word Filter"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	IUiContext ListInputCtx = SettingsUiContext("qmclient_block_words_input", UiScale);
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_Mode = ui_widget::EInputFieldMode::MULTILINE;
	InputOptions.m_pPlaceholder = Localize("Separate with commas");
	InputOptions.m_FontSize = BodySize;
	InputOptions.m_LineSpacing = InputLineSpacing;
	InputOptions.m_TextAlign = TEXTALIGN_ML;
	if(ui_widget::InputField(ListInputCtx, &s_BlockWordsInput, ControlColumn, InputOptions).m_Changed)
	{
		str_copy(g_Config.m_QmBlockWordsList, s_BlockWordsInput.GetString(), sizeof(g_Config.m_QmBlockWordsList));
		str_copy(s_aBlockWordsLayoutConfigCache, g_Config.m_QmBlockWordsList, sizeof(s_aBlockWordsLayoutConfigCache));
		++s_BlockWordsLayoutRevision;
	}
}

void CMenus::RenderQmFunctionKeywordReplyContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_keyword_reply_text_inputs", UiScale);
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-auto-reply-cooldown", &LabelColumn, Localize("Auto reply cooldown"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmAutoReplyCooldownInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmAutoReplyCooldownInputId, ControlColumn, &g_Config.m_QmAutoReplyCooldown, 0, 30, "s", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmKeywordReplyEnabled, "Enable keyword reply", &g_Config.m_QmKeywordReplyEnabled);
	RenderCheckbox(&g_Config.m_QmKeywordReplyUseDummy, "Reply with dummy", &g_Config.m_QmKeywordReplyUseDummy);

	auto SyncRuleRowsFromConfig = [](std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, bool &Inited, const char *pConfigRules) {
		std::vector<SAutoReplyRulePlain> vParsedRules;
		ParseAutoReplyRules(pConfigRules, vParsedRules);
		const auto RebuildRows = [&]() {
			vRows.clear();
			for(const auto &Rule : vParsedRules)
				vRows.push_back(CreateAutoReplyRuleInputRow(Rule.m_Keywords.c_str(), Rule.m_Reply.c_str(), Rule.m_AutoRename, Rule.m_Regex));
		};
		bool HasActiveInput = false;
		for(const auto &pRule : vRows)
		{
			if(pRule->m_TriggerInput.IsActive() || pRule->m_ReplyInput.IsActive())
			{
				HasActiveInput = true;
				break;
			}
		}
		const bool RowsMatch = Inited && AutoReplyRowsMatchRules(vRows, vParsedRules);
		if(!Inited || (!HasActiveInput && !RowsMatch))
		{
			RebuildRows();
			Inited = true;
			const bool HalfFilled = std::any_of(vRows.begin(), vRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
			UpdateKeywordRulesLayoutState(vRows.size(), HalfFilled);
			return true;
		}
		return RowsMatch;
	};
	if(QmKeywordReplyRules::EditorConfigChanged(s_KeywordRuleRowsInited, s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules))
	{
		char aDecodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		QmKeywordReplyRules::DecodeFromConfig(g_Config.m_QmKeywordReplyRules, aDecodedRules, sizeof(aDecodedRules));
		if(SyncRuleRowsFromConfig(s_vKeywordRuleRows, s_KeywordRuleRowsInited, aDecodedRules))
			str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rules", &LabelColumn, Localize("Keyword rules"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	CUIRect AddRuleButtonRect;
	ControlColumn.VSplitRight(maximum(LineHeight, 24.0f * UiScale), &ControlColumn, &AddRuleButtonRect);
	AddRuleButtonRect.VMargin((AddRuleButtonRect.w - AddRuleButtonRect.h) * 0.5f, &AddRuleButtonRect);
	QmKeywordReplyRules::SEditorChanges Changes;
	if(!PrewarmOnly && DoButton_Menu_QmIcon(&s_KeywordAddRuleButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &AddRuleButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL))
	{
		auto pNewRule = CreateAutoReplyRuleInputRow();
		pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
		s_vKeywordRuleRows.push_back(std::move(pNewRule));
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), s_KeywordRulesLayoutHalfFilled);
		Changes.m_Added = true;
	}
	s_vKeywordRemoveRuleButtons.resize(s_vKeywordRuleRows.size());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	const char *pRenameLabel = Localize("Rename");
	const char *pRegexLabel = Localize("Regex");
	const float OptionSpacing = maximum(4.0f, 4.0f * UiScale);
	const float CheckboxControlWidth = maximum(LineHeight * 1.65f, 30.0f) + 8.0f;
	const float MaxOptionWidth = maximum(CheckboxControlWidth + BodySize, Content.w * 0.24f);
	const float RenameWidth = minimum(MaxOptionWidth, CheckboxControlWidth + TextRender()->TextWidth(BodySize, pRenameLabel) + 2.0f);
	const float RegexWidth = minimum(MaxOptionWidth, CheckboxControlWidth + TextRender()->TextWidth(BodySize, pRegexLabel) + 2.0f);
	auto RenderRuleOption = [this, PrewarmOnly, BodySize](const char *pTextId, const char *pText, int *pValue, const CUIRect &Rect) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_StopAtEnd = true;
		const bool ProcessInput = !PrewarmOnly && !Ui()->RenderOnly();
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pValue, pTextId, pText, *pValue, &Rect, Props, ProcessInput, BodySize) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};

	for(size_t i = 0; i < s_vKeywordRuleRows.size();)
	{
		auto &pRule = s_vKeywordRuleRows[i];
		pRule->m_TriggerInput.SetEmptyText("");
		pRule->m_ReplyInput.SetEmptyText("");
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect RenameColumn, RegexColumn, TriggerColumn, SendColumn, ReplyColumn, RemoveButtonRect;
		Row.VSplitLeft(RenameWidth, &RenameColumn, &ControlColumn);
		ControlColumn.VSplitLeft(OptionSpacing, nullptr, &ControlColumn);
		ControlColumn.VSplitLeft(RegexWidth, &RegexColumn, &ControlColumn);
		ControlColumn.VSplitLeft(OptionSpacing, nullptr, &ControlColumn);
		ControlColumn.VSplitRight(maximum(LineHeight, 24.0f * UiScale), &ControlColumn, &RemoveButtonRect);
		RemoveButtonRect.VMargin((RemoveButtonRect.w - RemoveButtonRect.h) * 0.5f, &RemoveButtonRect);
		const float SendWidth = minimum(ControlColumn.w, maximum(40.0f, 40.0f * UiScale));
		ControlColumn.VSplitLeft((ControlColumn.w - SendWidth) * 0.5f, &TriggerColumn, &ControlColumn);
		ControlColumn.VSplitLeft(SendWidth, &SendColumn, &ReplyColumn);
		Changes.m_Rename |= RenderRuleOption("Rename", pRenameLabel, &pRule->m_AutoRename, RenameColumn);
		Changes.m_Regex |= RenderRuleOption("Regex", pRegexLabel, &pRule->m_Regex, RegexColumn);
		Changes.m_TriggerText |= ui_widget::InputField(TextInputCtx, &pRule->m_TriggerInput, TriggerColumn, "", BodySize);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-send-label", &SendColumn, Localize("Send"), BodySize, TEXTALIGN_MC, {}, (int)SendColumn.w);
		Changes.m_ReplyText |= ui_widget::InputField(TextInputCtx, &pRule->m_ReplyInput, ReplyColumn, "", BodySize);
		const bool RemoveClicked = !PrewarmOnly && DoButton_Menu_QmIcon(&s_vKeywordRemoveRuleButtons[i], EQmIcon::MINUS, FONT_ICON_MINUS, 0, &RemoveButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		if(RemoveClicked)
		{
			s_vKeywordRuleRows.erase(s_vKeywordRuleRows.begin() + i);
			s_vKeywordRemoveRuleButtons.erase(s_vKeywordRemoveRuleButtons.begin() + i);
			const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
			UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
			Changes.m_Removed = true;
			continue;
		}
		++i;
	}

	if(Changes.ShouldCommit(Ui()->RenderOnly()))
	{
		char aEncodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		BuildAutoReplyRulesFromRows(s_vKeywordRuleRows, aEncodedRules, sizeof(aEncodedRules));
		QmKeywordReplyRules::EncodeForConfig(aEncodedRules, g_Config.m_QmKeywordReplyRules, sizeof(g_Config.m_QmKeywordReplyRules));
		str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
	if(Changes.Any())
	{
		const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
	}
	if(!s_KeywordRulesLayoutHalfFilled)
		return;
	Content.HSplitTop(LineHeight, &Row, &Content);
	TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rule-validation", &Row, Localize("Both sides of keyword rules must be filled"), SmallSize, TEXTALIGN_ML, {}, (int)Row.w);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionTranslateContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float CardLabelWidth, bool PrewarmOnly)
{
	// 标签宽度必须按当前卡片内容计算；沿用整页宽度会把端点和模型输入框压缩到不可读。
	const float LabelWidth = std::min(CardLabelWidth, std::clamp(Content.w * 0.30f, 150.0f, 260.0f));
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_translate_text_inputs", BodySize / ui_token::font::BODY);
	auto RenderCheckbox = [this, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float VMargin) {
		CUIRect CheckBoxRect;
		pRect->HSplitTop(VMargin, &CheckBoxRect, pRect);
		return RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &CheckBoxRect, PrewarmOnly);
	};
	auto RenderLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize, int TextAlign = TEXTALIGN_ML, const SLabelProperties &LabelProps = {}) {
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, pRect, pText, FontSize, TextAlign, LabelProps, (int)pRect->w);
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};
	CUIRect Row, LabelCol, ControlCol;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateAuto, "Auto translate received messages", Localize("Auto translate received messages"), &g_Config.m_QmTranslateAuto, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateAutoOutgoing, "Auto translate sent messages", Localize("Auto translate sent messages"), &g_Config.m_QmTranslateAutoOutgoing, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	const auto TranslateBackendDropDownNames = NTranslateUi::NamesWithCustom(NTranslateUi::BackendNames());
	if(!PrewarmOnly && !Ui()->RenderOnly())
		NTranslateUi::NormalizeBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend));
	static CUi::SDropDownState s_TranslateBackendDropDownState;
	static CScrollRegion s_TranslateBackendDropDownScrollRegion;
	s_TranslateBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TranslateBackendDropDownScrollRegion;

	const int BackendSelectedOld = NTranslateUi::BackendIndexForDisplay(g_Config.m_QmTranslateBackend);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	CUIElement &TranslationServiceLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-translation-service");
	DoSettingsLabelStreamed(TranslationServiceLabel, &LabelCol, Localize("Translation service"), BodySize, TEXTALIGN_ML);
	const int BackendSelectedNew = DoSettingsDropDown(&ControlCol, BackendSelectedOld, TranslateBackendDropDownNames.data(), TranslateBackendDropDownNames.size(), s_TranslateBackendDropDownState);
	if(!PrewarmOnly && !Ui()->RenderOnly())
		NTranslateUi::CommitBackend(g_Config.m_QmTranslateBackend, sizeof(g_Config.m_QmTranslateBackend), BackendSelectedOld, BackendSelectedNew);
	const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
	const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
	const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
	const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
	const bool IsMymemoryBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0;
	const bool IsDeeplBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// MyMemory 免注册说明
	if(IsMymemoryBackend)
	{
		Content.HSplitTop(SmallSize, &Row, &Content);
		Row.VMargin(LabelWidth, &Row);
		Ui()->DoLabel(&Row, Localize("MyMemory needs no registration (anonymous daily quota)"), SmallSize, TEXTALIGN_ML);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// DeepL 说明与 API Key 输入
	if(IsDeeplBackend)
	{
		Content.HSplitTop(SmallSize, &Row, &Content);
		Row.VMargin(LabelWidth, &Row);
		Ui()->DoLabel(&Row, Localize("DeepL API Free: 500,000 characters per month (register at deepl.com; free keys end with :fx)"), SmallSize, TEXTALIGN_ML);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-deepl-key", &LabelCol, Localize("API key"), BodySize);
		static CLineInput s_TranslateDeeplKey(g_Config.m_QmTranslateDeeplKey, sizeof(g_Config.m_QmTranslateDeeplKey));
		s_TranslateDeeplKey.SetHidden(true);
		ui_widget::InputField(TextInputCtx, &s_TranslateDeeplKey, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// FTAPI 自动翻译开关（仅在 FTAPI 后端时显示）
	if(IsFtapiBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderCheckbox(&g_Config.m_QmTranslateFtapiAutoEnable, "Enable FTAPI auto-translate (may overload the service)", Localize("Enable FTAPI auto-translate (may overload the service)"), &g_Config.m_QmTranslateFtapiAutoEnable, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// FTAPI 警告提示
		Content.HSplitTop(LineHeight * 0.8f, &Row, &Content);
		Row.VMargin(LabelWidth, &Row);
		RenderLabel("qmclient-translate-ftapi-warning", &Row, Localize("⚠️ FTAPI is a free service. Excessive use may cause service suspension."), BodySize * 0.8f);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	auto RenderLanguageDropDownWithCustomInput = [this, BodySize, PrewarmOnly, &TextInputCtx](const CUIRect &ControlColumn, const char *const *apNames, const char *const *apCodes, int Count, CUi::SDropDownState &DropDownState, char *pConfigValue, size_t ConfigValueSize, CLineInput &LineInput, const char *pEmptyText) {
		CUIRect DropRect, EditRect;
		ControlColumn.VSplitMid(&DropRect, &EditRect);
		DropRect.VMargin(1.0f, &DropRect);
		EditRect.VMargin(1.0f, &EditRect);

		auto FindIndex = [](const char *pValue, const char *const *apConfigCodes, int ConfigCodeCount) -> int {
			for(int i = 0; i < ConfigCodeCount; ++i)
			{
				if(str_comp(pValue, apConfigCodes[i]) == 0)
					return i;
			}
			return -1;
		};

		const int OldSel = FindIndex(pConfigValue, apCodes, Count);
		// 自定义语言代码不能在普通重绘时被第一项覆盖；显式展示“自定义”项。
		std::vector<const char *> vNames(apNames, apNames + Count);
		vNames.push_back(Localize("Custom…"));
		const int SelectedIndex = NTranslateUi::CustomSelectionIndex(OldSel, Count);
		const int NewSel = DoSettingsDropDown(&DropRect, SelectedIndex, vNames.data(), static_cast<int>(vNames.size()), DropDownState);
		if(!PrewarmOnly && !Ui()->RenderOnly())
			NTranslateUi::CommitSelection(pConfigValue, ConfigValueSize, apCodes, Count, SelectedIndex, NewSel);

		if(!LineInput.IsActive() && str_comp(LineInput.GetString(), pConfigValue) != 0)
			LineInput.Set(pConfigValue);
		LineInput.SetEmptyText(pEmptyText);
		const bool WasActive = LineInput.IsActive();
		const bool SubmitPressed = !PrewarmOnly && (Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
		const bool ClickedOutside = !PrewarmOnly && (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)) && !Ui()->MouseHovered(&EditRect);
		ui_widget::STextFieldOptions LanguageInputOptions;
		LanguageInputOptions.m_pPlaceholder = pEmptyText;
		LanguageInputOptions.m_FontSize = BodySize;
		LanguageInputOptions.m_Corners = IGraphics::CORNER_ALL;
		LanguageInputOptions.m_TextAlign = TEXTALIGN_MC;
		ui_widget::InputField(TextInputCtx, &LineInput, EditRect, LanguageInputOptions);
		if(WasActive && (SubmitPressed || ClickedOutside))
		{
			str_copy(pConfigValue, LineInput.GetString(), ConfigValueSize);
		}
	};
	auto RenderSliderWithNumberInput = [&RenderSliderWithValueInput](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix);
	};

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	CUIElement &TargetLanguageLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-target-language");
	DoSettingsLabelStreamed(TargetLanguageLabel, &LabelCol, Localize("Translate received messages to"), BodySize, TEXTALIGN_ML);

	// 下拉框 + 输入框组合
	{
		const auto &LangNames = NTranslateUi::LanguageNames();
		const auto &LangCodes = NTranslateUi::LanguageCodes();
		static CUi::SDropDownState s_TargetLangDropDown;

		static CLineInput s_TranslateTarget(g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget));
		RenderLanguageDropDownWithCustomInput(ControlCol, LangNames.data(), LangCodes.data(), LangCodes.size(), s_TargetLangDropDown, g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget), s_TranslateTarget, "zh");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	// Endpoint 配置 - 根据后端类型显示不同的端点输入
	if(IsTencentCloudBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-tencent-endpoint", &LabelCol, Localize("Endpoint"), BodySize);
		static CLineInput s_TranslateEndpoint(g_Config.m_QmTranslateTcEndpoint, sizeof(g_Config.m_QmTranslateTcEndpoint));
		s_TranslateEndpoint.SetEmptyText("https://tmt.tencentcloudapi.com/");
		ui_widget::InputField(TextInputCtx, &s_TranslateEndpoint, ControlCol, "https://tmt.tencentcloudapi.com/", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	else if(IsLibreTranslateBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-aliyun-endpoint", &LabelCol, Localize("Endpoint"), BodySize);
		static CLineInput s_TranslateEndpoint(g_Config.m_QmTranslateLibreEndpoint, sizeof(g_Config.m_QmTranslateLibreEndpoint));
		s_TranslateEndpoint.SetEmptyText("http://localhost:5000");
		ui_widget::InputField(TextInputCtx, &s_TranslateEndpoint, ControlCol, "http://localhost:5000", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	// LLM 后端的端点配置在 Provider 选择区域显示

	if(IsTencentCloudBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-region", &LabelCol, Localize("Region"), BodySize);
		static CLineInput s_TranslateRegion(g_Config.m_QmTranslateTcRegion, sizeof(g_Config.m_QmTranslateTcRegion));
		s_TranslateRegion.SetEmptyText("ap-guangzhou");
		ui_widget::InputField(TextInputCtx, &s_TranslateRegion, ControlCol, "ap-guangzhou", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-secret-id", &LabelCol, Localize("SecretId"), BodySize);
		static CLineInput s_TranslateSecretId(g_Config.m_QmTranslateTcSecretId, sizeof(g_Config.m_QmTranslateTcSecretId));
		s_TranslateSecretId.SetEmptyText(Localize("Tencent Cloud SecretId"));
		ui_widget::InputField(TextInputCtx, &s_TranslateSecretId, ControlCol, Localize("Tencent Cloud SecretId"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-secret-key", &LabelCol, Localize("SecretKey"), BodySize);
		static CLineInput s_TranslateSecretKey(g_Config.m_QmTranslateTcSecretKey, sizeof(g_Config.m_QmTranslateTcSecretKey));
		s_TranslateSecretKey.SetEmptyText(Localize("Tencent Cloud SecretKey"));
		s_TranslateSecretKey.SetHidden(true);
		ui_widget::InputField(TextInputCtx, &s_TranslateSecretKey, ControlCol, Localize("Tencent Cloud SecretKey"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	else if(IsLibreTranslateBackend)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-api-key", &LabelCol, Localize("API key"), BodySize);
		static CLineInput s_TranslateKey(g_Config.m_QmTranslateLibreKey, sizeof(g_Config.m_QmTranslateLibreKey));
		s_TranslateKey.SetHidden(true);
		ui_widget::InputField(TextInputCtx, &s_TranslateKey, ControlCol, "", BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	if(IsLlmBackend)
	{
		// LLM Provider 选择
		const std::array<const char *, 4> LlmProviderDropDownNames = {
			Localize("Zhipu AI"),
			Localize("DeepSeek"),
			Localize("OpenAI"),
			Localize("Custom")};
		static CUi::SDropDownState s_LlmProviderDropDownState;
		static CScrollRegion s_LlmProviderDropDownScrollRegion;
		s_LlmProviderDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_LlmProviderDropDownScrollRegion;

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		CUIElement &LlmProviderLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-llm-provider");
		DoSettingsLabelStreamed(LlmProviderLabel, &LabelCol, Localize("LLM provider"), BodySize, TEXTALIGN_ML);
		const int NewProvider = DoSettingsDropDown(&ControlCol, g_Config.m_QmTranslateLlmProvider, LlmProviderDropDownNames.data(), LlmProviderDropDownNames.size(), s_LlmProviderDropDownState);
		// 写回前校验范围，防止异常返回值（如越界防御收敛出的 -1）污染配置
		if(NewProvider != g_Config.m_QmTranslateLlmProvider && NewProvider >= 0 && NewProvider < static_cast<int>(LlmProviderDropDownNames.size()))
		{
			g_Config.m_QmTranslateLlmProvider = NewProvider;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 各 Provider 的 API Key 输入框（静态变量，分别绑定到不同配置）
		static CLineInput s_LlmApiKeyZhipu(g_Config.m_QmTranslateLlmKeyZhipu, sizeof(g_Config.m_QmTranslateLlmKeyZhipu));
		static CLineInput s_LlmApiKeyDeepseek(g_Config.m_QmTranslateLlmKeyDeepseek, sizeof(g_Config.m_QmTranslateLlmKeyDeepseek));
		static CLineInput s_LlmApiKeyOpenai(g_Config.m_QmTranslateLlmKeyOpenai, sizeof(g_Config.m_QmTranslateLlmKeyOpenai));
		static CLineInput s_LlmApiKeyCustom(g_Config.m_QmTranslateLlmKeyCustom, sizeof(g_Config.m_QmTranslateLlmKeyCustom));

		// 根据 Provider 显示对应的 API Key 输入框
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);

		CLineInput *pActiveKeyInput = nullptr;
		// 各分支都会赋值，默认分支覆盖其余取值，无需初始值。
		const char *pKeyLabel;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			pKeyLabel = Localize("Zhipu API key");
			s_LlmApiKeyZhipu.SetEmptyText("ZHIPU_API_KEY");
			s_LlmApiKeyZhipu.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyZhipu;
			break;
		case 1: // DeepSeek
			pKeyLabel = Localize("DeepSeek API key");
			s_LlmApiKeyDeepseek.SetEmptyText("DEEPSEEK_API_KEY");
			s_LlmApiKeyDeepseek.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyDeepseek;
			break;
		case 2: // OpenAI
			pKeyLabel = Localize("OpenAI API key");
			s_LlmApiKeyOpenai.SetEmptyText("OPENAI_API_KEY");
			s_LlmApiKeyOpenai.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyOpenai;
			break;
		case 3: // Custom
		default:
			pKeyLabel = Localize("Custom API key");
			s_LlmApiKeyCustom.SetEmptyText("API_KEY");
			s_LlmApiKeyCustom.SetHidden(true);
			pActiveKeyInput = &s_LlmApiKeyCustom;
			break;
		}

		Ui()->DoLabel(&LabelCol, pKeyLabel, BodySize, TEXTALIGN_ML);
		if(pActiveKeyInput)
			ui_widget::InputField(TextInputCtx, pActiveKeyInput, ControlCol, pActiveKeyInput->GetEmptyText(), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 各 Provider 的模型配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-model", &LabelCol, Localize("Model"), BodySize);

		static CLineInput s_LlmModelZhipu(g_Config.m_QmTranslateLlmModelZhipu, sizeof(g_Config.m_QmTranslateLlmModelZhipu));
		static CLineInput s_LlmModelDeepseek(g_Config.m_QmTranslateLlmModelDeepseek, sizeof(g_Config.m_QmTranslateLlmModelDeepseek));
		static CLineInput s_LlmModelOpenai(g_Config.m_QmTranslateLlmModelOpenai, sizeof(g_Config.m_QmTranslateLlmModelOpenai));
		static CLineInput s_LlmModelCustom(g_Config.m_QmTranslateLlmModelCustom, sizeof(g_Config.m_QmTranslateLlmModelCustom));

		CLineInput *pActiveModelInput = nullptr;
		char *pModelConfigValue = nullptr;
		size_t ModelConfigSize = 0;
		const char *pModelEmptyText = "model-name";
		// 非空表示该 Provider 提供预设模型列表（下拉 + 自定义输入组合）
		std::vector<const char *> vModelPresets;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			pModelEmptyText = "glm-4.7-flash";
			s_LlmModelZhipu.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelZhipu;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelZhipu;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelZhipu);
			vModelPresets = {"glm-4.7-flash", "glm-4.7-flashx", "glm-4.7", "glm-5.1"};
			break;
		case 1: // DeepSeek
			pModelEmptyText = "deepseek-flash";
			s_LlmModelDeepseek.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelDeepseek;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelDeepseek;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelDeepseek);
			vModelPresets = {"deepseek-flash", "deepseek-v4-pro"};
			break;
		case 2: // OpenAI
			pModelEmptyText = "gpt-5.6-luna";
			s_LlmModelOpenai.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelOpenai;
			pModelConfigValue = g_Config.m_QmTranslateLlmModelOpenai;
			ModelConfigSize = sizeof(g_Config.m_QmTranslateLlmModelOpenai);
			vModelPresets = {"gpt-5.6-luna", "gpt-5.6-terra", "gpt-5.6-sol"};
			break;
		case 3: // Custom
		default:
			s_LlmModelCustom.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelCustom;
			break;
		}
		if(pActiveModelInput && vModelPresets.empty())
		{
			// 自定义 Provider 无预设，保持纯文本输入
			ui_widget::InputField(TextInputCtx, pActiveModelInput, ControlCol, pActiveModelInput->GetEmptyText(), BodySize);
		}
		else if(pActiveModelInput)
		{
			// 预设下拉 + 自定义输入组合；末位「自定义」仅切换为手输，不覆盖当前值
			static CUi::SDropDownState s_LlmModelDropDownState;
			static CScrollRegion s_LlmModelDropDownScrollRegion;
			s_LlmModelDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_LlmModelDropDownScrollRegion;

			std::vector<const char *> vModelNames(vModelPresets);
			vModelNames.push_back(Localize("Custom…"));
			const int CustomIndex = static_cast<int>(vModelNames.size()) - 1;
			auto FindPresetIndex = [&vModelPresets, CustomIndex](const char *pValue) -> int {
				for(size_t i = 0; i < vModelPresets.size(); ++i)
				{
					if(str_comp(pValue, vModelPresets[i]) == 0)
						return static_cast<int>(i);
				}
				return CustomIndex;
			};

			CUIRect ModelDropRect, ModelEditRect;
			ControlCol.VSplitMid(&ModelDropRect, &ModelEditRect);
			ModelDropRect.VMargin(1.0f, &ModelDropRect);
			ModelEditRect.VMargin(1.0f, &ModelEditRect);

			const int ModelOldSel = FindPresetIndex(pModelConfigValue);
			const int ModelNewSel = DoSettingsDropDown(&ModelDropRect, ModelOldSel, vModelNames.data(), vModelNames.size(), s_LlmModelDropDownState);
			if(ModelNewSel >= 0 && ModelNewSel != ModelOldSel && ModelNewSel != CustomIndex)
				str_copy(pModelConfigValue, vModelPresets[ModelNewSel], ModelConfigSize);

			if(!pActiveModelInput->IsActive() && str_comp(pActiveModelInput->GetString(), pModelConfigValue) != 0)
				pActiveModelInput->Set(pModelConfigValue);
			pActiveModelInput->SetEmptyText(pModelEmptyText);
			const bool ModelWasActive = pActiveModelInput->IsActive();
			const bool ModelSubmitPressed = !PrewarmOnly && (Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
			const bool ModelClickedOutside = !PrewarmOnly && (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)) && !Ui()->MouseHovered(&ModelEditRect);
			ui_widget::STextFieldOptions ModelInputOptions;
			ModelInputOptions.m_pPlaceholder = pModelEmptyText;
			ModelInputOptions.m_FontSize = BodySize;
			ModelInputOptions.m_Corners = IGraphics::CORNER_ALL;
			ModelInputOptions.m_TextAlign = TEXTALIGN_MC;
			ui_widget::InputField(TextInputCtx, pActiveModelInput, ModelEditRect, ModelInputOptions);
			if(ModelWasActive && (ModelSubmitPressed || ModelClickedOutside))
				str_copy(pModelConfigValue, pActiveModelInput->GetString(), ModelConfigSize);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// 出站目标语言（常驻）
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	RenderLabel("qmclient-translate-send-target-language", &LabelCol, Localize("Translate outgoing messages to"), BodySize);

	// 下拉框 + 输入框组合
	{
		const auto &apOutTargetNames = NTranslateUi::LanguageNames();
		const auto &apOutTargetCodes = NTranslateUi::LanguageCodes();
		static CUi::SDropDownState s_OutTargetLangDropDown;

		static CLineInput s_TargetLang(g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget));
		RenderLanguageDropDownWithCustomInput(ControlCol, apOutTargetNames.data(), apOutTargetCodes.data(), apOutTargetCodes.size(), s_OutTargetLangDropDown, g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget), s_TargetLang, "en");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	// 【高级】选项折叠开关
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderCheckbox(&g_Config.m_QmTranslateShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmTranslateShowAdvanced, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_QmTranslateShowAdvanced)
	{
		// 本地语言检测阈值
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-minimum-match-chars", &LabelCol, Localize("Minimum match chars"), BodySize);
		{
			static int s_LocalDetectMinCharsSelectorId;
			RenderSliderWithNumberInput(&s_LocalDetectMinCharsSelectorId, ControlCol, &g_Config.m_QmTranslateLocalDetectMinChars, 1, 12);
		}
		Content.HSplitTop(LineSpacing * 0.5f, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-target-language-ratio", &LabelCol, Localize("Target language ratio"), BodySize);
		{
			static int s_LocalDetectRatioSelectorId;
			RenderSliderWithNumberInput(&s_LocalDetectRatioSelectorId, ControlCol, &g_Config.m_QmTranslateLocalDetectRatio, 50, 100);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// LLM 端点（高级；Custom Provider 必填，始终显示）
	if(IsLlmBackend && (g_Config.m_QmTranslateShowAdvanced || g_Config.m_QmTranslateLlmProvider == 3))
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-endpoint-optional", &LabelCol, Localize("Endpoint (optional)"), BodySize);

		static CLineInput s_LlmEndpointZhipu(g_Config.m_QmTranslateLlmEndpointZhipu, sizeof(g_Config.m_QmTranslateLlmEndpointZhipu));
		static CLineInput s_LlmEndpointDeepseek(g_Config.m_QmTranslateLlmEndpointDeepseek, sizeof(g_Config.m_QmTranslateLlmEndpointDeepseek));
		static CLineInput s_LlmEndpointOpenai(g_Config.m_QmTranslateLlmEndpointOpenai, sizeof(g_Config.m_QmTranslateLlmEndpointOpenai));
		static CLineInput s_LlmEndpointCustom(g_Config.m_QmTranslateLlmEndpointCustom, sizeof(g_Config.m_QmTranslateLlmEndpointCustom));

		CLineInput *pActiveEndpointInput = nullptr;
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			s_LlmEndpointZhipu.SetEmptyText("https://open.bigmodel.cn/api/paas/v4/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointZhipu;
			break;
		case 1: // DeepSeek
			s_LlmEndpointDeepseek.SetEmptyText("https://api.deepseek.com/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointDeepseek;
			break;
		case 2: // OpenAI
			s_LlmEndpointOpenai.SetEmptyText("https://api.openai.com/v1/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointOpenai;
			break;
		case 3: // Custom
		default:
			s_LlmEndpointCustom.SetEmptyText("https://api.example.com/v1/chat/completions");
			pActiveEndpointInput = &s_LlmEndpointCustom;
			break;
		}
		if(pActiveEndpointInput)
			ui_widget::InputField(TextInputCtx, pActiveEndpointInput, ControlCol, pActiveEndpointInput->GetEmptyText(), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	// LLM 并发、思考模式与提示词（高级）
	if(IsLlmBackend && g_Config.m_QmTranslateShowAdvanced)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-concurrency", &LabelCol, Localize("Concurrency (0 = auto)"), BodySize);
		static int s_LlmConcurrencySelectorId;
		RenderSliderWithNumberInput(&s_LlmConcurrencySelectorId, ControlCol, &g_Config.m_QmTranslateLlmConcurrency, 0, 20);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 显示当前有效并发数
		{
			const int EffectiveConcurrency = GetTranslateConcurrency();

			// 显示有效并发数
			char aBuf[64];
			if(g_Config.m_QmTranslateLlmConcurrency == 0)
			{
				str_format(aBuf, sizeof(aBuf), Localize("Auto concurrency: %d (smart default)"), EffectiveConcurrency);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), Localize("Manual concurrency: %d"), EffectiveConcurrency);
			}
			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, nullptr, &ControlCol);
			Ui()->DoLabel(&ControlCol, aBuf, SmallSize, TEXTALIGN_ML);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 智谱免费档并发限制提示
		if(g_Config.m_QmTranslateLlmProvider == 0)
		{
			Content.HSplitTop(SmallSize, &Row, &Content);
			Row.VMargin(LabelWidth, &Row);
			Ui()->DoLabel(&Row, Localize("Zhipu free models allow only 1 concurrent request"), SmallSize, TEXTALIGN_ML);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
		}

		// 思考模式开关
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderCheckbox(&g_Config.m_QmTranslateLlmEnableThinking, "Enable thinking mode (slower)", Localize("Enable thinking mode (slower)"), &g_Config.m_QmTranslateLlmEnableThinking, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 思考模式提示
		if(g_Config.m_QmTranslateLlmEnableThinking)
		{
			const char *pHint = nullptr;
			if(g_Config.m_QmTranslateLlmProvider == 2) // OpenAI
			{
				pHint = Localize("Thinking mode requires a reasoning model");
			}
			else if(g_Config.m_QmTranslateLlmProvider == 3) // Custom
			{
				pHint = Localize("Make sure the backend supports OpenAI-compatible thinking parameters");
			}

			if(pHint)
			{
				Content.HSplitTop(SmallSize, &Row, &Content);
				Row.VMargin(LabelWidth, &Row);
				Ui()->DoLabel(&Row, pHint, SmallSize, TEXTALIGN_ML);
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			}
		}

		// 自定义提示词配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-custom-prompt-template", &LabelCol, Localize("Custom prompt template"), BodySize);
		static CLineInput s_CustomPrompt(g_Config.m_QmTranslateSystemPrompt, sizeof(g_Config.m_QmTranslateSystemPrompt));
		s_CustomPrompt.SetEmptyText(Localize("Leave empty to use default prompt"));
		ui_widget::InputField(TextInputCtx, &s_CustomPrompt, ControlCol, Localize("Leave empty to use default prompt"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}

	if(g_Config.m_QmTranslateShowAdvanced)
	{
		// 原文语言由接收、发送翻译共用
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-send-source-language", &LabelCol, Localize("Original message language"), BodySize);
		if(!PrewarmOnly && !Ui()->RenderOnly())
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmTranslateSource, &Row, Localize("Original language for both received and outgoing messages. Use Auto to detect each message."));

		// 下拉框 + 输入框组合
		{
			const auto apSourceNames = NTranslateUi::SourceLanguageNames();
			const auto apSourceCodes = NTranslateUi::SourceLanguageCodes();
			static CUi::SDropDownState s_SourceLangDropDown;

			static CLineInput s_SourceLang(g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource));
			RenderLanguageDropDownWithCustomInput(ControlCol, apSourceNames.data(), apSourceCodes.data(), apSourceCodes.size(), s_SourceLangDropDown, g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource), s_SourceLang, "auto");
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 接收翻译方式
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-receive-method", &LabelCol, Localize("Receive translation method"), BodySize);
		{
			const std::array<const char *, 2> apIncomingModeNames = {
				Localize("Translate only when not in target language"),
				Localize("Always translate"),
			};
			static CUi::SDropDownState s_IncomingModeDropDown;
			const int OldIncomingMode = std::clamp(g_Config.m_QmTranslateAutoMode, 0, 1);
			const int NewIncomingMode = DoSettingsDropDown(&ControlCol, OldIncomingMode, apIncomingModeNames.data(), apIncomingModeNames.size(), s_IncomingModeDropDown);
			if(NewIncomingMode != OldIncomingMode)
				g_Config.m_QmTranslateAutoMode = NewIncomingMode;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 发送翻译方式
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-send-method", &LabelCol, Localize("Send translation method"), BodySize);
		{
			const std::array<const char *, 2> apOutgoingModeNames = {
				Localize("Translate only when needed"),
				Localize("Always translate"),
			};
			static CUi::SDropDownState s_OutgoingModeDropDown;
			const int OldMode = std::clamp(g_Config.m_QmTranslateAutoOutgoingMode, 0, 1);
			const int NewMode = DoSettingsDropDown(&ControlCol, OldMode, apOutgoingModeNames.data(), apOutgoingModeNames.size(), s_OutgoingModeDropDown);
			if(NewMode != OldMode)
				g_Config.m_QmTranslateAutoOutgoingMode = NewMode;
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}

void CMenus::RenderQmFunctionFavoriteMapsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	const auto &FavMaps = GameClient()->TClientComponent().GetFavoriteMaps();
	if(s_FavoriteMapsLayoutCount != FavMaps.size())
	{
		s_FavoriteMapsLayoutCount = FavMaps.size();
		++s_FavoriteMapsLayoutRevision;
	}

	auto MapCategoryKeyFromText = [](const char *pText) -> const char * {
		if(!pText || pText[0] == '\0')
			return nullptr;
		if(str_find_nocase(pText, "DDmaX"))
		{
			if(str_find_nocase(pText, "Easy"))
				return "DDmaX Easy";
			if(str_find_nocase(pText, "Next"))
				return "DDmaX Next";
			if(str_find_nocase(pText, "Pro"))
				return "DDmaX Pro";
			if(str_find_nocase(pText, "Nut"))
				return "DDmaX Nut";
			return "DDmaX";
		}
		if(str_find_nocase(pText, "Oldschool"))
			return "Oldschool";
		if(str_find_nocase(pText, "Novice"))
			return "Novice";
		if(str_find_nocase(pText, "Moderate"))
			return "Moderate";
		if(str_find_nocase(pText, "Brutal"))
			return "Brutal";
		if(str_find_nocase(pText, "Insane"))
			return "Insane";
		if(str_find_nocase(pText, "Dummy"))
			return "Dummy";
		if(str_find_nocase(pText, "Solo"))
			return "Solo";
		if(str_find_nocase(pText, "Race"))
			return "Race";
		if(str_find_nocase(pText, "Fun"))
			return "Fun";
		if(str_find_nocase(pText, "Event"))
			return "Event";
		return nullptr;
	};
	// 只做纯文本映射，不读成员，无需捕获 this。
	auto MapTypeDisplayName = [](const char *pType) -> const char * {
		if(!pType || pType[0] == '\0')
			return Localize("Unknown");
		if(str_comp_nocase(pType, "DDmaX Easy") == 0)
			return Localize("Classic easy");
		if(str_comp_nocase(pType, "DDmaX Next") == 0)
			return Localize("Classic next");
		if(str_comp_nocase(pType, "DDmaX Pro") == 0)
			return Localize("Classic pro");
		if(str_comp_nocase(pType, "DDmaX Nut") == 0)
			return Localize("Classic nut");
		if(str_comp_nocase(pType, "DDmaX") == 0)
			return Localize("Classic");
		if(str_comp_nocase(pType, "Novice") == 0)
			return Localize("Novice");
		if(str_comp_nocase(pType, "Moderate") == 0)
			return Localize("Moderate");
		if(str_comp_nocase(pType, "Brutal") == 0)
			return Localize("Brutal");
		if(str_comp_nocase(pType, "Insane") == 0)
			return Localize("Insane");
		if(str_comp_nocase(pType, "Dummy") == 0)
			return Localize("Dummy");
		if(str_comp_nocase(pType, "Solo") == 0)
			return Localize("Solo");
		if(str_comp_nocase(pType, "Oldschool") == 0)
			return Localize("Oldschool");
		if(str_comp_nocase(pType, "Race") == 0)
			return Localize("Race");
		if(str_comp_nocase(pType, "Fun") == 0)
			return Localize("Fun");
		if(str_comp_nocase(pType, "Event") == 0)
			return Localize("Event");
		return Localize("Unknown");
	};

	static std::unordered_map<std::string, std::string> s_MapCategories;
	static int s_MapCategoryScanIndex = 0;
	static int s_LastNumServers = -1;
	static float s_NextFullScan = 0.0f;
	IServerBrowser *pServerBrowser = ServerBrowser();
	const float Now = Client()->LocalTime();
	if(Ui()->RenderOnly())
	{
		// 文本预热只读取现有分类快照，不能扫描服务器或写入磁盘缓存。
	}
	else if(!pServerBrowser || FavMaps.empty())
	{
		s_MapCategories.clear();
		s_MapCategoryScanIndex = 0;
		s_LastNumServers = -1;
		s_NextFullScan = 0.0f;
	}
	else
	{
		const int NumServers = pServerBrowser->NumSortedServers();
		if(NumServers != s_LastNumServers)
		{
			s_LastNumServers = NumServers;
			s_MapCategoryScanIndex = 0;
			s_NextFullScan = 0.0f;
		}
		if(NumServers > 0 && (Now >= s_NextFullScan || s_MapCategoryScanIndex > 0))
		{
			if(s_MapCategoryScanIndex == 0)
			{
				s_MapCategories.clear();
				s_MapCategories.reserve((size_t)NumServers);
			}
			constexpr int ServersPerFrame = 64;
			int ProcessedServers = 0;
			while(s_MapCategoryScanIndex < NumServers && ProcessedServers < ServersPerFrame)
			{
				const CServerInfo *pInfo = pServerBrowser->SortedGet(s_MapCategoryScanIndex);
				++s_MapCategoryScanIndex;
				++ProcessedServers;
				if(!pInfo || pInfo->m_aMap[0] == '\0')
					continue;
				const char *pCategoryKey = MapCategoryKeyFromText(pInfo->m_aCommunityType);
				if(!pCategoryKey)
					pCategoryKey = MapCategoryKeyFromText(pInfo->m_aName);
				if(!pCategoryKey)
					continue;
				auto It = s_MapCategories.find(pInfo->m_aMap);
				if(It == s_MapCategories.end() || It->second != pCategoryKey)
				{
					s_MapCategories[pInfo->m_aMap] = pCategoryKey;
					GameClient()->TClientComponent().UpdateMapCategoryCache(pInfo->m_aMap, pCategoryKey);
				}
			}
			if(s_MapCategoryScanIndex >= NumServers)
			{
				s_MapCategoryScanIndex = 0;
				s_NextFullScan = Now + 2.0f;
			}
		}
		const NETADDR *pServerAddr = Client()->ServerAddress();
		const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
		if(pEntry && pEntry->m_Info.m_aMap[0] != '\0')
		{
			const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
			if(!pCategoryKey)
				pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
			if(pCategoryKey)
			{
				s_MapCategories[pEntry->m_Info.m_aMap] = pCategoryKey;
				GameClient()->TClientComponent().UpdateMapCategoryCache(pEntry->m_Info.m_aMap, pCategoryKey);
			}
		}
	}

	auto GetMapCategory = [&](const char *pMapName) -> const char * {
		if(!pMapName || pMapName[0] == '\0')
			return Localize("Unknown");
		const auto It = s_MapCategories.find(pMapName);
		if(It != s_MapCategories.end() && !It->second.empty())
			return MapTypeDisplayName(It->second.c_str());
		const char *pCurrentMap = Client()->GetCurrentMap();
		if(pServerBrowser && pCurrentMap && str_comp(pCurrentMap, pMapName) == 0)
		{
			const NETADDR *pServerAddr = Client()->ServerAddress();
			const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
			if(pEntry)
			{
				const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
				if(!pCategoryKey)
					pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
				if(pCategoryKey)
					return MapTypeDisplayName(pCategoryKey);
			}
		}
		const char *pCachedCategory = GameClient()->TClientComponent().GetCachedMapCategoryKey(pMapName);
		if(pCachedCategory)
			return MapTypeDisplayName(pCachedCategory);
		return Localize("Unknown");
	};

	static int s_CopiedMapIndex = -1;
	static float s_CopiedTime = 0.0f;
	if(s_CopiedMapIndex >= 0 && Client()->LocalTime() - s_CopiedTime > 1.5f)
		s_CopiedMapIndex = -1;
	CUIRect Row;
	if(FavMaps.empty())
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-favorite-maps-empty", &Row, Localize("No favorite maps yet"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		return;
	}

	static int s_aMapButtonIds[64];
	static CButtonContainer s_aMapRemoveButtons[64];
	IUiContext IconButtonCtx = SettingsUiContext("settings_qmclient_favorite_maps_icons", UiScale);
	std::string RemoveMapName;
	size_t MapIndex = 0;
	for(const std::string &MapName : FavMaps)
	{
		if(MapIndex >= std::size(s_aMapButtonIds))
			break;
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect RowLabel, RowRemove;
		Row.VSplitRight(LineHeight, &RowLabel, &RowRemove);
		RowRemove.HMargin(std::clamp(2.0f * UiScale, 1.0f, 2.0f), &RowRemove);
		if(ui_widget::IconButton(IconButtonCtx, &s_aMapRemoveButtons[MapIndex], EQmIcon::CLOSE, FONT_ICON_XMARK, RowRemove))
		{
			if(RemoveMapName.empty())
				RemoveMapName = MapName;
			s_CopiedMapIndex = -1;
		}
		if(!PrewarmOnly && Ui()->MouseInside(&RowLabel))
		{
			Ui()->SetHotItem(&s_aMapButtonIds[MapIndex]);
			if(Ui()->MouseButtonClicked(0))
			{
				Input()->SetClipboardText(MapName.c_str());
				s_CopiedMapIndex = (int)MapIndex;
				s_CopiedTime = Client()->LocalTime();
			}
		}
		if(s_CopiedMapIndex == (int)MapIndex)
		{
			TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-favorite-map-copied", &RowLabel, Localize("Copied"), BodySize, TEXTALIGN_ML, {}, (int)RowLabel.w);
		}
		else
		{
			char aLabel[256];
			str_format(aLabel, sizeof(aLabel), "%s (%s)", MapName.c_str(), GetMapCategory(MapName.c_str()));
			TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f);
			Ui()->DoLabel(&RowLabel, aLabel, BodySize, TEXTALIGN_ML);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(Ui()->HotItem() == &s_aMapButtonIds[MapIndex])
			GameClient()->m_Tooltips.DoToolTip(&s_aMapButtonIds[MapIndex], &RowLabel, Localize("Click to copy the map name"));
		if(Ui()->HotItem() == &s_aMapRemoveButtons[MapIndex])
			GameClient()->m_Tooltips.DoToolTip(&s_aMapRemoveButtons[MapIndex], &RowRemove, Localize("Remove from favorites"));
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		++MapIndex;
	}
	if(!RemoveMapName.empty())
	{
		GameClient()->TClientComponent().RemoveFavoriteMap(RemoveMapName.c_str());
		const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
		if(s_FavoriteMapsLayoutCount != FavoriteMapCount)
		{
			s_FavoriteMapsLayoutCount = FavoriteMapCount;
			++s_FavoriteMapsLayoutRevision;
		}
	}
}

void CMenus::RenderQmHudBindStatusContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 内置四项状态开关（自外观页 DDRace HUD 卡片迁移）
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusReset, "appearance-show-key-stuck-status", Localize("Show key stuck status"), &g_Config.m_ClShowhudKeyStatusReset);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusHammer, "appearance-show-hammer-status", Localize("Show hammer status"), &g_Config.m_ClShowhudKeyStatusHammer);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusControl, "appearance-show-dummy-control-status", Localize("Show dummy control status"), &g_Config.m_ClShowhudKeyStatusControl);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusSync, "appearance-show-dummy-copy-status", Localize("Show dummy copy status"), &g_Config.m_ClShowhudKeyStatusSync);
}

void CMenus::RenderQmHudDebugGraphContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	static CButtonContainer s_ReaderButtonDebugGraphToggle;
	static CButtonContainer s_ClearButtonDebugGraphToggle;
	RenderQmHudKeyBindRow(Content, s_ReaderButtonDebugGraphToggle, s_ClearButtonDebugGraphToggle, Localize("Global toggle key"), "toggle dbg_graphs 0 1", LineHeight, BodySize, LineSpacing, LabelWidth);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-debug-graph-panel-opacity", &LabelColumn, Localize("Panel opacity"), BodySize);
	static int s_QmMonitoringHudOpacityInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmMonitoringHudOpacityInputId, ControlColumn, &g_Config.m_QmMonitoringHudOpacity, 0, 100, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudDebugModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 调试模式总开关：任一性能开关开启即视为"调试模式"开启；点击时统一开启/关闭三个开关。
	const bool DebugModeEnabled = g_Config.m_QmPerfDebug != 0 || g_Config.m_QmPerfLogfile != 0 || g_Config.m_QmPerfStutterDiagnostics != 0;

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	static int s_QmPerfDebugModeSwitchId;
	if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_QmPerfDebugModeSwitchId, "Debug mode", Localize("Debug mode"), DebugModeEnabled, &Row))
	{
		const int NewValue = DebugModeEnabled ? 0 : 1;
		g_Config.m_QmPerfDebug = NewValue;
		g_Config.m_QmPerfLogfile = NewValue;
		g_Config.m_QmPerfStutterDiagnostics = NewValue;
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pText, Localize(pText), *pValue, &Row))
			*pValue ^= 1;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmPerfDebug, "Enable main thread and render stage performance debug logging", &g_Config.m_QmPerfDebug);
	RenderCheckbox(&g_Config.m_QmPerfLogfile, "Write performance debug logs to dedicated file", &g_Config.m_QmPerfLogfile);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, "qmclient-debug-mode-threshold", &LabelColumn, Localize("Performance debug log threshold (ms)"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmPerfDebugThresholdMsInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmPerfDebugThresholdMsInputId, ControlColumn, &g_Config.m_QmPerfDebugThresholdMs, 1, 1000, "ms", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderCheckbox(&g_Config.m_QmPerfStutterDiagnostics, "Enable client stutter diagnostics at startup", &g_Config.m_QmPerfStutterDiagnostics);
}

void CMenus::RenderQmHudInputOverlayContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmInputOverlay, "Show inputs", Localize("Show inputs"), &g_Config.m_QmInputOverlay);
	if(!g_Config.m_QmInputOverlay)
		return;

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmInputOverlayScaleInputId;
	static int s_QmInputOverlayMouseScaleInputId;
	static int s_QmInputOverlayOpacityInputId;
	static int s_QmInputOverlayPosXInputId;
	static int s_QmInputOverlayPosYInputId;
	RenderValue("qmclient-input-overlay-keyboard-size", "Keyboard size", &s_QmInputOverlayScaleInputId, &g_Config.m_QmInputOverlayScale, 1, 200);
	RenderValue("qmclient-input-overlay-mouse-size", "Mouse size", &s_QmInputOverlayMouseScaleInputId, &g_Config.m_QmInputOverlayMouseScale, 1, 200);
	RenderValue("qmclient-input-overlay-opacity", "Opacity", &s_QmInputOverlayOpacityInputId, &g_Config.m_QmInputOverlayOpacity, 0, 100);
	RenderValue("qmclient-input-overlay-horizontal-position", "Horizontal position", &s_QmInputOverlayPosXInputId, &g_Config.m_QmInputOverlayPosX, 0, 100);
	RenderValue("qmclient-input-overlay-vertical-position", "Vertical position", &s_QmInputOverlayPosYInputId, &g_Config.m_QmInputOverlayPosY, 0, 100);
}

void CMenus::RenderQmHudDummyMiniViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool Expanded, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDummyMiniView, "Enable dummy window", Localize("Enable dummy window"), &g_Config.m_QmDummyMiniView);
	if(!Expanded)
		return;
	Content.HSplitTop(LineHeight * 0.8f, nullptr, &Content);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmDummyMiniViewAuto, "Only show when the other Tee is not on screen", Localize("Only show when the other Tee is not on screen"), &g_Config.m_QmDummyMiniViewAuto);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "%", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmDummyMiniViewSizeInputId;
	static int s_QmDummyMiniViewZoomInputId;
	RenderValue("qmclient-dummy-window-size", "Dummy window size", &s_QmDummyMiniViewSizeInputId, &g_Config.m_QmDummyMiniViewSize, 50, 200);
	RenderValue("qmclient-dummy-window-zoom", "Dummy window zoom", &s_QmDummyMiniViewZoomInputId, &g_Config.m_QmDummyMiniViewZoom, 10, 300);
}

void CMenus::RenderQmHudDynamicIslandContent(CUIRect &Content, float LineHeight, float LineSpacing, bool OriginalStyle)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandUseOriginalStyle, "Use original style", Localize("Use original style"), &g_Config.m_QmHudIslandUseOriginalStyle);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandShowTeam, "Show team", Localize("Show team"), &g_Config.m_QmHudIslandShowTeam);

	if(OriginalStyle)
		return;

	static CButtonContainer s_DynamicIslandBgColorId;
	// 颜色弹窗里的 A(透明度) 是整块板的通透度（亚克力）：开高斯模糊时模糊照旧，
	// 板越透越看得见后面的画面；A=0 时整块板连外圈阴影一起消失。
	DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("Background color"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80);

	// 钩子倒计时是独立开关：钩住玩家时在开关环正上方画一个蓝色倒计时环。
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, "Enable hook countdown", Localize("Enable hook countdown"), &g_Config.m_QmHookCountdown);

	// 开关倒计时：总开关决定是否显示，两个位置开关决定显示在跟随 Tee 的圆环上还是灵动岛里，可同时勾选。
	const bool CountdownChanged = RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, "Enable switch countdown", Localize("Enable switch countdown"), &g_Config.m_QmSwitchCountdown);
	if(!g_Config.m_QmSwitchCountdown)
		return;

	int FollowTee = QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
	int MediaIsland = QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) ? 1 : 0;
	const int CurrentMode = std::clamp(g_Config.m_QmSwitchCountdownMode, static_cast<int>(EQmSwitchCountdownMode::FOLLOW_TEE), static_cast<int>(EQmSwitchCountdownMode::BOTH));
	bool LocationChanged = false;

	// 位置开关不绑定配置项，用固定地址当按钮 ID，避免取栈变量地址导致 ID 漂移。
	// 控件 id 的权威定义已随卡片目录迁至 QmUi/cards/QmCardCatalogHud.cpp（内部头提供取用入口），
	// 渲染路径与卡片路径必须共用同一组 id，否则同一次点击会被两条路径各处理一次。
	LocationChanged |= ToggleQmHudCountdownLocation(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownFollowTeeId(), &FollowTee);
	LocationChanged |= ToggleQmHudCountdownLocation(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownMediaIslandId(), &MediaIsland);

	if(CountdownChanged || LocationChanged)
	{
		// 两个位置都不勾时倒计时无处可显示，直接关掉总开关，避免界面与渲染结果互相打架。
		if(FollowTee == 0 && MediaIsland == 0)
		{
			g_Config.m_QmSwitchCountdown = 0;
			FollowTee = 0;
			MediaIsland = 1;
		}
		g_Config.m_QmSwitchCountdownMode = QmHudSwitchCountdownModeFromLocations(FollowTee != 0, MediaIsland != 0, CurrentMode);
	}
}

void CMenus::RenderQmHudLyricsContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	// 音乐 Hook 开关：同一时间只能启用一个，点开其中一个时自动关闭其余。
	// 遍历 QmMusicHookRegistry 而非硬编码 Netease/Soda，新增 Hook 只需在注册表登记即自动覆盖。
	size_t HookCount = 0;
	const SQmMusicHookEntry *apHooks = QmMusicHookRegistry(&HookCount);
	for(size_t i = 0; i < HookCount; ++i)
	{
		const SQmMusicHookEntry &Hook = apHooks[i];
		const bool Changed = RenderQmHudCheckbox(Content, LineHeight, LineSpacing, Hook.m_pEnableConfig, Hook.m_pSettingsTextId, Localize(Hook.m_pSettingsText), Hook.m_pEnableConfig);
		if(Changed && *Hook.m_pEnableConfig != 0)
		{
			// 互斥：打开一个 Hook 时自动关闭其余 Hook。
			for(size_t j = 0; j < HookCount; ++j)
			{
				if(j != i)
					*apHooks[j].m_pEnableConfig = 0;
			}
		}
	}
	// 兜底：配置被外部直接改成多个 Hook 同时开启时，保留第一个，关闭其余。
	int FirstEnabled = -1;
	for(size_t i = 0; i < HookCount; ++i)
	{
		if(*apHooks[i].m_pEnableConfig != 0)
		{
			if(FirstEnabled == -1)
				FirstEnabled = (int)i;
			else
				*apHooks[i].m_pEnableConfig = 0;
		}
	}
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, "Enable lyrics", Localize("Enable lyrics"), &g_Config.m_QmLyrics);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, "Show lyrics inside Dynamic Island", Localize("Show lyrics inside Dynamic Island"), &g_Config.m_QmLyricsInMediaIsland);
	if(g_Config.m_QmKugouHookEnable != 0)
	{
		CUIRect Row, SetupButton, RestoreButton;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitMid(&SetupButton, &RestoreButton, LineSpacing);
		static CButtonContainer s_KugouSetup, s_KugouRestore;
		const bool Setup = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouSetup, "qmclient-kugou-setup", Localize("Set up Kugou lyrics"), 0, &SetupButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
		const bool Restore = DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_KugouRestore, "qmclient-kugou-restore", Localize("Restore Kugou files"), 0, &RestoreButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE);
		// 预热和只读布局阶段只绘制按钮，安装文件操作由 helper 再次明确确认。
		if(!PrewarmOnly && !Ui()->RenderOnly() && (Setup || Restore))
			GameClient()->m_MusicLyricsIntegration.RunKugouSetup(Restore);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(g_Config.m_QmKugouHookEnable != 0 || g_Config.m_QmQQMusicHookEnable != 0)
	{
		CUIRect Row;
		Content.HSplitTop(LineHeight, &Row, &Content);
		char aStatus[512] = {};
		GameClient()->m_MusicLyricsIntegration.GetStatus(aStatus, sizeof(aStatus));
		RenderQmHudLabel("qmclient-music-hook-status", &Row, aStatus, ui_token::font::BODY);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(g_Config.m_QmSpotifyEnable != 0)
	{
		static CLineInput s_SpotifySpDc(g_Config.m_QmSpotifySpDc, sizeof(g_Config.m_QmSpotifySpDc));
		CUIRect Row, LabelColumn, InputColumn;
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(100.0f, &LabelColumn, &InputColumn);
		RenderQmHudLabel("qmclient-lyrics-spotify-sp-dc", &LabelColumn, "spotify_ck", ui_token::font::BODY);
		s_SpotifySpDc.SetHidden(true);
		IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_lyrics_spotify_text_inputs");
		ui_widget::InputField(TextInputCtx, &s_SpotifySpDc, InputColumn, Localize("Paste sp_dc from Spotify web cookies"), ui_token::font::BODY);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
}

void CMenus::RenderQmHudSystemMediaControlsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, "Enable system media control", Localize("Enable system media control"), &g_Config.m_QmSmtcEnable);
	if(!g_Config.m_QmSmtcEnable)
		return;

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcShowHud, "Show song info in top-left corner", Localize("Show song info in top-left corner"), &g_Config.m_QmSmtcShowHud);
	// Hook 开关与歌词开关已迁出为独立的 qm:lyrics 卡（内容函数仍是 RenderQmHudLyricsContent，
	// 由卡片目录的 Hud 分类模块负责调用）。此处不再渲染，否则歌词与来源开关会在两处各出现一次。
	CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
	Content.HSplitTop(LineHeight, &MediaButtons, &Content);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing * 2.0f) / 3.0f, &PrevButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing) / 2.0f, &PlayButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	NextButton = MediaButtons;

	static CButtonContainer s_SmtcPrev;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPrev, "qmclient-smtc-previous", Localize("Previous"), 0, &PrevButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.Previous();
	static CButtonContainer s_SmtcPlayPause;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPlayPause, "qmclient-smtc-play-pause", Localize("Play/Pause"), 0, &PlayButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.PlayPause();
	static CButtonContainer s_SmtcNext;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcNext, "qmclient-smtc-next", Localize("Next"), 0, &NextButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE))
		GameClient()->m_SystemMediaControls.Next();
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudNotificationsBasicContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsSystem, "Show important server prompts as notifications", Localize("Show important server prompts as notifications"), &g_Config.m_QmHudNotificationsSystem);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEcho, "Route Echo messages to notifications", Localize("Route Echo messages to notifications"), &g_Config.m_QmHudNotificationsEcho);
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmHudNotificationHoldInputId;
	static int s_QmHudNotificationTextSizeInputId;
	RenderValue("qmclient-notifications-hold-time", "Notification hold time", &s_QmHudNotificationHoldInputId, &g_Config.m_QmHudNotificationsHoldMs, 500, 10000, "ms");
	RenderValue("qmclient-notifications-text-size", "Notification text size", &s_QmHudNotificationTextSizeInputId, &g_Config.m_QmHudNotificationsTextSize, 1, 24);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmHudNotificationsShowAdvanced);
}

void CMenus::RenderQmHudNotificationsAdvancedContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsUseCategoryFilters, "Use notification category filters", Localize("Use notification category filters"), &g_Config.m_QmHudNotificationsUseCategoryFilters);
	if(g_Config.m_QmHudNotificationsUseCategoryFilters)
	{
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowPrompts, "Show important server prompts", Localize("Show important server prompts"), &g_Config.m_QmHudNotificationsShowPrompts);
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowUnknown, "Show unknown server messages", Localize("Show unknown server messages"), &g_Config.m_QmHudNotificationsShowUnknown);
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowBasicInfo, "Show basic server information", Localize("Show basic server information"), &g_Config.m_QmHudNotificationsShowBasicInfo);
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowHelpInfo, "Show server help and usage messages", Localize("Show server help and usage messages"), &g_Config.m_QmHudNotificationsShowHelpInfo);
	}
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsCompatSolo, "Detect compatible solo prompts from custom servers", Localize("Detect compatible solo prompts from custom servers"), &g_Config.m_QmHudNotificationsCompatSolo);

	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(Metrics.m_SmallSize, &Row, &Content);
	TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
	RenderQmHudLabel("qmclient-notifications-basic-info-note", &Row, Localize("Join, version, rules, and help messages stay in chat instead of popups"), Metrics.m_SmallSize);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	static CButtonContainer s_QmHudNotificationBgColorId;
	DoLine_ColorPicker(&s_QmHudNotificationBgColorId, Metrics, &Content, Localize("Notification background"), &g_Config.m_QmHudNotificationsBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), false, nullptr, true);
	static CButtonContainer s_QmHudNotificationTextColorId;
	DoLine_ColorPicker(&s_QmHudNotificationTextColorId, Metrics, &Content, Localize("System prompt text color"), &g_Config.m_QmHudNotificationsTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, true);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEchoInheritColor, "Echo follows the original chat color", Localize("Echo follows the original chat color"), &g_Config.m_QmHudNotificationsEchoInheritColor);
	static CButtonContainer s_QmHudNotificationEchoTextColorId;
	DoLine_ColorPicker(&s_QmHudNotificationEchoTextColorId, Metrics, &Content, Localize("Echo text color when not inheriting chat color"), &g_Config.m_QmHudNotificationsEchoTextColor, ColorRGBA(0.5f, 0.78f, 1.0f, 1.0f), false, nullptr, true);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-notifications-popup-animation", &LabelColumn, Localize("Popup animation"), BodySize);
	const char *apHudNotificationAnimDropDownNames[] = {Localize("Fade and slide"), Localize("Fade only"), Localize("No animation")};
	static CUi::SDropDownState s_HudNotificationAnimDropDownState;
	static CScrollRegion s_HudNotificationAnimDropDownScrollRegion;
	s_HudNotificationAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_HudNotificationAnimDropDownScrollRegion;
	const int AnimSelectedNew = DoSettingsDropDown(&ControlColumn, g_Config.m_QmHudNotificationsAnimType, apHudNotificationAnimDropDownNames, std::size(apHudNotificationAnimDropDownNames), s_HudNotificationAnimDropDownState);
	if(g_Config.m_QmHudNotificationsAnimType != AnimSelectedNew)
		g_Config.m_QmHudNotificationsAnimType = AnimSelectedNew;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmHudNotificationAnimInputId;
	static int s_QmHudNotificationMaxVisibleInputId;
	static int s_QmHudNotificationEdgeMarginInputId;
	RenderValue("qmclient-notifications-animation-duration", "Animation duration", &s_QmHudNotificationAnimInputId, &g_Config.m_QmHudNotificationsAnimMs, 0, 2000, "ms");
	RenderValue("qmclient-notifications-max-visible", "Max visible notifications", &s_QmHudNotificationMaxVisibleInputId, &g_Config.m_QmHudNotificationsMaxVisible, 1, 8);
	RenderValue("qmclient-notifications-edge-margin", "Edge margin", &s_QmHudNotificationEdgeMarginInputId, &g_Config.m_QmHudNotificationsEdgeMargin, 0, 32);
}

void CMenus::RenderQmHudPlayerStatsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsHud, "Show player stats HUD", Localize("Show player stats HUD"), &g_Config.m_QmPlayerStatsHud);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgress, "Map progress bar", Localize("Map progress bar"), &g_Config.m_QmPlayerStatsMapProgress);
	if(g_Config.m_QmPlayerStatsMapProgress)
	{
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressStyle, "Use embedded HUD progress bar", Localize("Use embedded HUD progress bar"), &g_Config.m_QmPlayerStatsMapProgressStyle);
		if(g_Config.m_QmPlayerStatsMapProgressStyle == 0)
		{
			static CButtonContainer s_MapProgressColorId;
			DoLine_ColorPicker(&s_MapProgressColorId, CurrentSettingsContentMetrics(), &Content, Localize("Progress bar color"), &g_Config.m_QmPlayerStatsMapProgressColor, ColorRGBA(36.0f / 255.0f, 199.0f / 255.0f, 100.0f / 255.0f, 1.0f), false, nullptr, true);
			CUIRect Row, LabelColumn, ControlColumn;
			auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
				Content.HSplitTop(LineHeight, &Row, &Content);
				Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
				RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
				RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			};
			static int s_QmPlayerStatsMapProgressWidthInputId;
			static int s_QmPlayerStatsMapProgressHeightInputId;
			static int s_QmPlayerStatsMapProgressPosXInputId;
			static int s_QmPlayerStatsMapProgressPosYInputId;
			RenderValue("qmclient-player-data-progress-bar-width", "Progress bar width", &s_QmPlayerStatsMapProgressWidthInputId, &g_Config.m_QmPlayerStatsMapProgressWidth, 10, 80);
			RenderValue("qmclient-player-data-progress-bar-height", "Progress bar height", &s_QmPlayerStatsMapProgressHeightInputId, &g_Config.m_QmPlayerStatsMapProgressHeight, 6, 30);
			RenderValue("qmclient-player-data-horizontal-position", "Horizontal position", &s_QmPlayerStatsMapProgressPosXInputId, &g_Config.m_QmPlayerStatsMapProgressPosX, 0, 100, "%");
			RenderValue("qmclient-player-data-vertical-position", "Vertical position", &s_QmPlayerStatsMapProgressPosYInputId, &g_Config.m_QmPlayerStatsMapProgressPosY, 0, 100, "%");
		}
		RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressDbgRoute, "Show dotted map route debug", Localize("Show dotted map route debug"), &g_Config.m_QmPlayerStatsMapProgressDbgRoute);
	}
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsResetOnJoin, "Reset stats when joining a server", Localize("Reset stats when joining a server"), &g_Config.m_QmPlayerStatsResetOnJoin);
}

void CMenus::RenderQmHudCoordsContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		const char *pOverrideTooltip = TemporaryOverrideTooltip(pValue);
		SLabelProperties LabelProps;
		if(pOverrideTooltip != nullptr)
		{
			LabelProps.SetColor(ui_token::color::TEXT_DISABLED);
			// 灰化行不占 hover，补一次只读的按钮逻辑让提示能激活（返回值丢弃，不写值）。
			if(!Ui()->RenderOnly())
			{
				Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_NONE);
				GameClient()->m_Tooltips.DoToolTip(pId, pRect, pOverrideTooltip);
			}
		}
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect, LabelProps, pOverrideTooltip == nullptr) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordsOwn, "Show own coordinates", Localize("Show own coordinates"), &g_Config.m_QmNameplateCoordsOwn, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoords, "Show other players' coordinates", Localize("Show other players' coordinates"), &g_Config.m_QmNameplateCoords, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordX, "Show X", Localize("Show X"), &g_Config.m_QmNameplateCoordX, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordY, "Show Y", Localize("Show Y"), &g_Config.m_QmNameplateCoordY, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHint, "X alignment hint with me", Localize("X alignment hint with me"), &g_Config.m_QmNameplateCoordXAlignHint, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmNameplateCoordXAlignHintStrict, "Strict mode", Localize("Strict mode"), &g_Config.m_QmNameplateCoordXAlignHintStrict, &Row, LineHeight);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	DoQmSettingsLabel("qmclient-show-coordinates-detection-time", &LabelCol, Localize("Detection time"), BodySize);
	static int s_CoordXAlignHintWindowSliderId;
	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(&s_CoordXAlignHintWindowSliderId);
	ui_widget::SNumericFieldOptions Options;
	Options.m_pSuffix = "ms";
	Options.m_FontSize = BodySize;
	Options.m_ValueStep = 100;
	IUiContext InputCtx;
	InputCtx.m_pUi = Ui();
	InputCtx.m_pAnim = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	InputCtx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
	InputCtx.m_ScopeHash = MakeUiScopeHash("qmclient_coord_x_align_hint_window");
	InputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	const int OriginalValue = g_Config.m_QmNameplateCoordXAlignHintWindowMs;
	ui_widget::NumericField(InputCtx, pState, &s_CoordXAlignHintWindowSliderId, &g_Config.m_QmNameplateCoordXAlignHintWindowMs, 100, 3000, ControlCol, Options);
	if(PrewarmOnly || Ui()->RenderOnly())
		g_Config.m_QmNameplateCoordXAlignHintWindowMs = OriginalValue;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CButtonContainer s_CoordXAlignHintColorId;
	DoLine_ColorPicker(&s_CoordXAlignHintColorId, Metrics, &Content, Localize("X alignment color"), &g_Config.m_QmNameplateCoordXAlignHintColor, ColorRGBA(1.0f, 0.82f, 0.2f, 1.0f), false, nullptr, false, false);
}

void CMenus::RenderQmHudVoiceContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float UiScale = Metrics.m_UiScale;
	// 下拉框在正式绘制阶段才提交配置。当前帧继续使用测量阶段的模式，
	// 避免先绘制新增行、下一帧才扩展卡片高度。
	const int NoiseSuppressModeForLayout = std::clamp(g_Config.m_QmVoiceNoiseSuppressEnable, 0, 2);
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};
	auto DoQmSettingsMenuButton = [this](CButtonContainer *pButton, const char *pTextId, const char *pText, const CUIRect *pRect) {
		return DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pButton, pTextId, pText, 0, pRect);
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};
	IUiContext QmClientVoiceTextInputCtx;
	QmClientVoiceTextInputCtx.m_pUi = Ui();
	QmClientVoiceTextInputCtx.m_pAnim = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	QmClientVoiceTextInputCtx.m_pTree = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
	QmClientVoiceTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_qmclient_voice_text_inputs");
	QmClientVoiceTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceEnable, "Enable voice", Localize("Enable voice"), &g_Config.m_QmVoiceEnable, &Row, LineHeight);
	if(g_Config.m_QmVoiceEnable)
		Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_QmVoiceEnable)
	{
		[[maybe_unused]] auto AddVoiceSectionLabel = [&](const char *pTitle, const char *pHint) {
			Content.HSplitTop(LineHeight * 0.78f, &Row, &Content);
			Ui()->DoLabel(&Row, pTitle, Metrics.m_HeadlineSize, TEXTALIGN_ML);
			if(pHint != nullptr && pHint[0] != '\0')
			{
				Content.HSplitTop(LineHeight * 0.68f, &Row, &Content);
				Ui()->DoLabel(&Row, pHint, Metrics.m_SmallSize, TEXTALIGN_ML);
			}
			Content.HSplitTop(LineSpacing * 0.75f, nullptr, &Content);
		};

		const bool NeedVoiceDiagnostics = g_Config.m_QmVoiceShowAdvanced && g_Config.m_QmVoiceShowConnectionStatus;
		VoiceUtils::SVoiceUiStatus VoiceUiStatus{};
		if(NeedVoiceDiagnostics)
			GameClient()->m_Voice.Voice().ExportUiStatus(VoiceUiStatus);
		auto LocalizeVoiceUiMicStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiMicStatus(Status);
			if(str_comp(pState, "muted") == 0)
				return Localize("Muted");
			if(str_comp(pState, "unavailable") == 0)
				return Localize("Not open, check input device or mic permission");
			if(str_comp(pState, "ready") == 0)
				return Localize("Opened");
			if(str_comp(pState, "waiting") == 0)
				return Localize("Waiting to open");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiOutputStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiOutputStatus(Status);
			if(str_comp(pState, "unavailable") == 0)
				return Localize("Not open, check output device");
			if(str_comp(pState, "ready") == 0)
				return Localize("Opened");
			if(str_comp(pState, "waiting") == 0)
				return Localize("Waiting to open");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiServerStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiServerStatus(Status);
			if(str_comp(pState, "local_test") == 0)
				str_copy(pBuf, Localize("Local test mode, no server needed"), BufSize);
			else if(str_comp(pState, "offline") == 0)
				str_copy(pBuf, Localize("Not connected to server"), BufSize);
			else if(str_comp(pState, "resolving") == 0)
				str_copy(pBuf, Localize("Parsing voice server address"), BufSize);
			else if(str_comp(pState, "socket_error") == 0)
				str_copy(pBuf, Localize("UDP socket not open"), BufSize);
			else if(str_comp(pState, "connected") == 0)
				str_format(pBuf, BufSize, "%s (%d ms)", Localize("Connected"), maximum(Status.m_PingMs, 0));
			else if(str_comp(pState, "connected_no_ping") == 0)
				str_copy(pBuf, Localize("Connected, waiting for first ping"), BufSize);
			else
				str_copy(pBuf, Localize("Unknown status"), BufSize);
		};
		auto LocalizeVoiceUiRoomStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiRoomStatus(Status);
			if(str_comp(pState, "local_test") == 0)
				str_copy(pBuf, Localize("Local test mode"), BufSize);
			else if(str_comp(pState, "offline") == 0)
				str_copy(pBuf, Localize("Not connected to server"), BufSize);
			else if(str_comp(pState, "matched") == 0)
				str_format(pBuf, BufSize, "%s (%d)", Localize("Matched with callable peer"), Status.m_ActivePeerCount);
			else if(str_comp(pState, "waiting_peer") == 0)
				str_copy(pBuf, Localize("No callable peer found"), BufSize);
			else
				str_copy(pBuf, Localize("Unknown status"), BufSize);
		};
		auto LocalizeVoiceUiTransportStatus = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pState = VoiceUtils::VoiceUiTransportStatus(Status);
			if(str_comp(pState, "tx_rx_active") == 0)
				return Localize("Sending and receiving");
			if(str_comp(pState, "tx_active") == 0)
				return Localize("Sending, waiting for peer echo");
			if(str_comp(pState, "rx_active") == 0)
				return Localize("Receiving");
			if(str_comp(pState, "idle_with_peer") == 0)
				return Localize("Connected, no one is speaking");
			if(str_comp(pState, "idle_no_peer") == 0)
				return Localize("No peer");
			return Localize("Not enabled");
		};
		auto LocalizeVoiceUiInputRouteStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiInputRouteStatus(Status);
			const char *pRequested = Status.m_aRequestedInputDevice[0] != '\0' ? Status.m_aRequestedInputDevice : Localize("Default");
			const char *pResolved = Status.m_aResolvedInputDevice[0] != '\0' ? Status.m_aResolvedInputDevice : Localize("System default");
			if(str_comp(pState, "using_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switched to"), pRequested);
			else if(str_comp(pState, "using_default") == 0)
				str_format(pBuf, BufSize, "%s (%s)", Localize("Use default input"), pResolved);
			else if(str_comp(pState, "switching_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switching"), pRequested);
			else if(str_comp(pState, "switching_default") == 0)
				str_copy(pBuf, Localize("Switching back to default input"), BufSize);
			else if(str_comp(pState, "permission_denied") == 0)
				str_copy(pBuf, Localize("Microphone permission denied by system"), BufSize);
			else if(str_comp(pState, "selected_failed") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switch failed"), pRequested);
			else if(str_comp(pState, "default_failed") == 0)
				str_copy(pBuf, Localize("Default input open failed"), BufSize);
			else if(str_comp(pState, "waiting") == 0)
				str_copy(pBuf, Localize("Waiting to open input device"), BufSize);
			else
				str_copy(pBuf, Localize("Not enabled"), BufSize);
		};
		auto LocalizeVoiceUiOutputRouteStatus = [&](const VoiceUtils::SVoiceUiStatus &Status, char *pBuf, size_t BufSize) {
			const char *pState = VoiceUtils::VoiceUiOutputRouteStatus(Status);
			const char *pRequested = Status.m_aRequestedOutputDevice[0] != '\0' ? Status.m_aRequestedOutputDevice : Localize("Default");
			const char *pResolved = Status.m_aResolvedOutputDevice[0] != '\0' ? Status.m_aResolvedOutputDevice : Localize("System default");
			if(str_comp(pState, "using_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switched to"), pRequested);
			else if(str_comp(pState, "using_default") == 0)
				str_format(pBuf, BufSize, "%s (%s)", Localize("Use default output"), pResolved);
			else if(str_comp(pState, "switching_selected") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switching"), pRequested);
			else if(str_comp(pState, "switching_default") == 0)
				str_copy(pBuf, Localize("Switching back to default output"), BufSize);
			else if(str_comp(pState, "selected_failed") == 0)
				str_format(pBuf, BufSize, "%s: %s", Localize("Switch failed"), pRequested);
			else if(str_comp(pState, "default_failed") == 0)
				str_copy(pBuf, Localize("Default output open failed"), BufSize);
			else if(str_comp(pState, "waiting") == 0)
				str_copy(pBuf, Localize("Waiting to open output device"), BufSize);
			else
				str_copy(pBuf, Localize("Not enabled"), BufSize);
		};
		auto LocalizeVoiceUiAudioIssue = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pIssue = VoiceUtils::VoiceUiAudioIssueKey(Status);
			if(str_comp(pIssue, "none") == 0)
				return Localize("No audio issues detected");
			if(str_comp(pIssue, "input_device_not_found") == 0)
				return Localize("Input device not found");
			if(str_comp(pIssue, "output_device_not_found") == 0)
				return Localize("Output device not found");
			if(str_comp(pIssue, "no_capture_devices") == 0)
				return Localize("No input device available");
			if(str_comp(pIssue, "no_output_devices") == 0)
				return Localize("No output device available");
			if(str_comp(pIssue, "open_capture_failed") == 0)
				return Localize("Input device open failed");
			if(str_comp(pIssue, "open_output_failed") == 0)
				return Localize("Output device open failed");
			if(str_comp(pIssue, "permission_denied") == 0)
				return Localize("Microphone permission denied by system");
			if(str_comp(pIssue, "backend_init_failed") == 0)
				return Localize("Audio backend init failed");
			return Localize("Unclassified audio issue");
		};
		auto RenderVoiceStatusRow = [&](const char *pTitle, const char *pValue) {
			Content.HSplitTop(LineHeight, &Row, &Content);
			CUIRect StatusLabel, StatusValue;
			Row.VSplitLeft(LabelWidth, &StatusLabel, &StatusValue);
			Ui()->DoLabel(&StatusLabel, pTitle, BodySize, TEXTALIGN_ML);
			Ui()->DoLabel(&StatusValue, pValue, Metrics.m_SmallSize, TEXTALIGN_ML);
			Content.HSplitTop(LineSpacing * 0.75f, nullptr, &Content);
		};
		auto LocalizeVoiceUiActionHint = [&](const VoiceUtils::SVoiceUiStatus &Status) {
			const char *pHint = VoiceUtils::VoiceUiActionHint(Status);
			if(str_comp(pHint, "select_input_device") == 0)
				return Localize("Try reselecting input device, confirm default mic or headset mic is online");
			if(str_comp(pHint, "select_output_device") == 0)
				return Localize("Try reselecting output device, confirm headphones/speakers are online");
			if(str_comp(pHint, "retry_input_open") == 0)
				return Localize("Input device open failed, try reconnecting headset/mic or reselecting input device");
			if(str_comp(pHint, "retry_output_open") == 0)
				return Localize("Output device open failed, try reconnecting speakers/headphones or reselecting output device");
			if(str_comp(pHint, "grant_mic_permission") == 0)
				return Localize("Allow mic permission in system settings, then reopen voice");
			if(str_comp(pHint, "check_audio_backend") == 0)
				return Localize("Audio backend init failed, try switching devices and check details");
			if(str_comp(pHint, "inspect_audio_log") == 0)
				return Localize("Audio init failed, check details below and logs");
			if(str_comp(pHint, "check_input") == 0)
				return Localize("Check input device, system default mic, and mic permission first");
			if(str_comp(pHint, "check_output") == 0)
				return Localize("Check output device, confirm headphones/speakers are still online");
			if(str_comp(pHint, "join_server") == 0)
				return Localize("Connect to server first to establish voice network link");
			if(str_comp(pHint, "check_server") == 0)
				return Localize("Check if voice server address is reachable");
			if(str_comp(pHint, "wait_connection") == 0)
				return Localize("Waiting for the voice WebSocket connection");
			if(str_comp(pHint, "retry_socket") == 0)
				return Localize("Try toggling voice or reconnecting to server");
			if(str_comp(pHint, "check_room") == 0)
				return Localize("Confirm both are on same server, same room, and support voice");
			if(str_comp(pHint, "wait_peer") == 0)
				return Localize("Sending locally, suggest the other party unmute or confirm they can receive");
			if(str_comp(pHint, "enable_voice") == 0)
				return Localize("Please enable voice first");
			return Localize("Status normal, check details below if still experiencing issues");
		};

		char aVoiceServerStatus[128]{};
		char aVoiceRoomStatus[128]{};
		char aVoiceTransportStatus[128]{};
		char aVoiceTransportDetail[160]{};
		char aVoiceInputRouteStatus[160]{};
		char aVoiceOutputRouteStatus[160]{};
		if(NeedVoiceDiagnostics)
		{
			LocalizeVoiceUiServerStatus(VoiceUiStatus, aVoiceServerStatus, sizeof(aVoiceServerStatus));
			LocalizeVoiceUiRoomStatus(VoiceUiStatus, aVoiceRoomStatus, sizeof(aVoiceRoomStatus));
			LocalizeVoiceUiInputRouteStatus(VoiceUiStatus, aVoiceInputRouteStatus, sizeof(aVoiceInputRouteStatus));
			LocalizeVoiceUiOutputRouteStatus(VoiceUiStatus, aVoiceOutputRouteStatus, sizeof(aVoiceOutputRouteStatus));
			str_copy(aVoiceTransportStatus, LocalizeVoiceUiTransportStatus(VoiceUiStatus), sizeof(aVoiceTransportStatus));
			if(VoiceUiStatus.m_TxAgeMs >= 0 || VoiceUiStatus.m_RxAgeMs >= 0)
			{
				str_format(aVoiceTransportDetail, sizeof(aVoiceTransportDetail), "%s: tx=%dms rx=%dms mic=%.0f%%",
					aVoiceTransportStatus,
					VoiceUiStatus.m_TxAgeMs,
					VoiceUiStatus.m_RxAgeMs,
					(double)std::clamp(VoiceUiStatus.m_MicLevel * 100.0f, 0.0f, 100.0f));
			}
			else
			{
				str_copy(aVoiceTransportDetail, aVoiceTransportStatus, sizeof(aVoiceTransportDetail));
			}
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		DoQmSettingsLabel("qmclient-voice-room-password", &LabelCol, Localize("Room password"), BodySize);
		static CLineInput s_VoiceToken(g_Config.m_QmVoiceToken, sizeof(g_Config.m_QmVoiceToken));
		if(!PrewarmOnly && !Ui()->RenderOnly())
		{
			s_VoiceToken.SetEmptyText(Localize("Leave empty to join public room"));
			s_VoiceToken.SetHidden(true);
		}
		ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceToken, ControlCol, Localize("Leave empty to join public room"), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceMicMute, "Mute microphone", Localize("Mute microphone"), &g_Config.m_QmVoiceMicMute, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		{
			CUIRect LabelColValue, ControlColValue;
			Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
			DoQmSettingsLabel("qmclient-voice-microphone-volume", &LabelColValue, Localize("Microphone volume"), BodySize);
			static int s_QmVoiceMicVolumeInputId;
			RenderSliderWithValueInput(&s_QmVoiceMicVolumeInputId, ControlColValue, &g_Config.m_QmVoiceMicVolume, 0, 300, "%");
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceVadEnable, "Auto unmute when speaking", Localize("Auto unmute when speaking"), &g_Config.m_QmVoiceVadEnable, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceShowAdvanced, "Advanced options", Localize("Advanced options"), &g_Config.m_QmVoiceShowAdvanced, &Row, LineHeight);
		if(g_Config.m_QmVoiceShowAdvanced)
			Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_QmVoiceShowAdvanced)
		{
			Content.HSplitTop(LineHeight, &Row, &Content);
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceShowConnectionStatus, "Show voice connection status", Localize("Show voice connection status"), &g_Config.m_QmVoiceShowConnectionStatus, &Row, LineHeight);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			if(g_Config.m_QmVoiceShowConnectionStatus)
			{
				AddVoiceSectionLabel(Localize("Current status"), Localize("Start here to quickly diagnose if the issue is with device, server, or room"));
				RenderVoiceStatusRow(Localize("Microphone"), LocalizeVoiceUiMicStatus(VoiceUiStatus));
				RenderVoiceStatusRow(Localize("Speaker"), LocalizeVoiceUiOutputStatus(VoiceUiStatus));
				RenderVoiceStatusRow(Localize("Input switch"), aVoiceInputRouteStatus);
				RenderVoiceStatusRow(Localize("Output switch"), aVoiceOutputRouteStatus);
				RenderVoiceStatusRow(Localize("Server"), aVoiceServerStatus);
				RenderVoiceStatusRow(Localize("Room"), aVoiceRoomStatus);
				RenderVoiceStatusRow(Localize("Send & Receive"), aVoiceTransportDetail);
				RenderVoiceStatusRow(Localize("Troubleshooting suggestions"), LocalizeVoiceUiActionHint(VoiceUiStatus));
				RenderVoiceStatusRow(Localize("Audio issue"), LocalizeVoiceUiAudioIssue(VoiceUiStatus));
				const char *pPrimaryError = VoiceUtils::VoiceUiPrimaryError(VoiceUiStatus);
				RenderVoiceStatusRow(Localize("Detailed reason"), pPrimaryError[0] != '\0' ? pPrimaryError : Localize("No audio issues detected"));
				Content.HSplitTop(LineSpacing * 0.5f, nullptr, &Content);
			}

			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
			DoQmSettingsLabel("qmclient-voice-server-ip", &LabelCol, Localize("Server URL"), BodySize);
			static CLineInput s_VoiceServer(g_Config.m_QmVoiceServer, sizeof(g_Config.m_QmVoiceServer));
			if(!PrewarmOnly && !Ui()->RenderOnly())
				s_VoiceServer.SetEmptyText("wss://qmclient.icu/ws/voice");
			ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceServer, ControlCol, "wss://qmclient.icu/ws/voice", BodySize);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
			DoQmSettingsLabel("qmclient-voice-input-device", &LabelCol, Localize("Input device"), BodySize);
			static std::vector<std::string> s_VoiceInputDeviceDisplayNames;
			static std::vector<std::string> s_VoiceInputDeviceConfigValues;
			static std::vector<const char *> s_VoiceInputDeviceDropDownNames;
			static std::vector<VoiceUtils::SVoiceDeviceDropdownEntry> s_VoiceInputDeviceEntries;
			static CUi::SDropDownState s_VoiceInputDeviceDropDownState;
			static CScrollRegion s_VoiceInputDeviceDropDownScrollRegion;
			static bool s_VoiceInputDevicesInitialized = false;
			s_VoiceInputDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceInputDeviceDropDownScrollRegion;
			auto RefreshVoiceInputDeviceList = [&]() {
				CPerfTimer StageTimer;
				s_VoiceInputDeviceDisplayNames.clear();
				s_VoiceInputDeviceConfigValues.clear();
				s_VoiceInputDeviceDropDownNames.clear();
				std::vector<std::string> vDetectedDeviceNames;
				const int NumInputs = SDL_GetNumAudioDevices(1);
				for(int i = 0; i < NumInputs; i++)
				{
					const char *pName = SDL_GetAudioDeviceName(i, 1);
					if(!pName || pName[0] == '\0')
						continue;
					vDetectedDeviceNames.emplace_back(pName);
				}

				VoiceUtils::BuildVoiceDeviceDropdownEntries(
					vDetectedDeviceNames,
					g_Config.m_QmVoiceInputDevice,
					Localize("Default"),
					Localize("Disconnected"),
					s_VoiceInputDeviceEntries);

				s_VoiceInputDeviceDisplayNames.reserve(s_VoiceInputDeviceEntries.size());
				s_VoiceInputDeviceConfigValues.reserve(s_VoiceInputDeviceEntries.size());
				s_VoiceInputDeviceDropDownNames.reserve(s_VoiceInputDeviceDisplayNames.size());
				for(const auto &Entry : s_VoiceInputDeviceEntries)
				{
					s_VoiceInputDeviceDisplayNames.push_back(Entry.m_DisplayName);
					s_VoiceInputDeviceConfigValues.push_back(Entry.m_ConfigValue);
					s_VoiceInputDeviceDropDownNames.push_back(s_VoiceInputDeviceDisplayNames.back().c_str());
				}

				char aVoiceExtra[96];
				str_format(aVoiceExtra, sizeof(aVoiceExtra), "devices=%d", (int)s_VoiceInputDeviceDisplayNames.size());
				LogQmPerfStage(Client(), "voice_device_enum", StageTimer.ElapsedMs(), false, aVoiceExtra);
			};
			const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
			if(!ReadOnly && !s_VoiceInputDevicesInitialized)
			{
				RefreshVoiceInputDeviceList();
				s_VoiceInputDevicesInitialized = true;
			}

			CUIRect VoiceInputDropDownRect;
			CUIRect VoiceInputRefreshButton;
			ControlCol.VSplitRight(maximum(68.0f, 68.0f * UiScale), &VoiceInputDropDownRect, &VoiceInputRefreshButton);

			static CButtonContainer s_VoiceInputRefreshButton;
			if(ReadOnly)
			{
				DoQmSettingsLabel("qmclient-voice-input-default", &VoiceInputDropDownRect, Localize("Default"), BodySize);
				DoQmSettingsMenuButton(&s_VoiceInputRefreshButton, "qmclient-voice-input-refresh", Localize("Refresh"), &VoiceInputRefreshButton);
			}
			else
			{
				const int VoiceInputSelectedOld = VoiceUtils::VoiceFindSelectedDeviceIndex(s_VoiceInputDeviceEntries, g_Config.m_QmVoiceInputDevice);
				const int VoiceInputSelectedNew = DoSettingsDropDown(&VoiceInputDropDownRect, VoiceInputSelectedOld, s_VoiceInputDeviceDropDownNames.data(), s_VoiceInputDeviceDropDownNames.size(), s_VoiceInputDeviceDropDownState);
				if(VoiceInputSelectedNew >= 0 && VoiceInputSelectedNew != VoiceInputSelectedOld && (size_t)VoiceInputSelectedNew < s_VoiceInputDeviceConfigValues.size())
					str_copy(g_Config.m_QmVoiceInputDevice, s_VoiceInputDeviceConfigValues[VoiceInputSelectedNew].c_str(), sizeof(g_Config.m_QmVoiceInputDevice));
				if(DoQmSettingsMenuButton(&s_VoiceInputRefreshButton, "qmclient-voice-input-refresh", Localize("Refresh"), &VoiceInputRefreshButton))
					RefreshVoiceInputDeviceList();
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
			DoQmSettingsLabel("qmclient-voice-output-device", &LabelCol, Localize("Output device"), BodySize);
			static std::vector<std::string> s_VoiceOutputDeviceDisplayNames;
			static std::vector<std::string> s_VoiceOutputDeviceConfigValues;
			static std::vector<const char *> s_VoiceOutputDeviceDropDownNames;
			static std::vector<VoiceUtils::SVoiceDeviceDropdownEntry> s_VoiceOutputDeviceEntries;
			static CUi::SDropDownState s_VoiceOutputDeviceDropDownState;
			static CScrollRegion s_VoiceOutputDeviceDropDownScrollRegion;
			static bool s_VoiceOutputDevicesInitialized = false;
			s_VoiceOutputDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceOutputDeviceDropDownScrollRegion;
			auto RefreshVoiceOutputDeviceList = [&]() {
				CPerfTimer StageTimer;
				s_VoiceOutputDeviceDisplayNames.clear();
				s_VoiceOutputDeviceConfigValues.clear();
				s_VoiceOutputDeviceDropDownNames.clear();
				std::vector<std::string> vDetectedDeviceNames;
				const int NumOutputs = SDL_GetNumAudioDevices(0);
				for(int i = 0; i < NumOutputs; i++)
				{
					const char *pName = SDL_GetAudioDeviceName(i, 0);
					if(!pName || pName[0] == '\0')
						continue;
					vDetectedDeviceNames.emplace_back(pName);
				}

				VoiceUtils::BuildVoiceDeviceDropdownEntries(
					vDetectedDeviceNames,
					g_Config.m_QmVoiceOutputDevice,
					Localize("Default"),
					Localize("Disconnected"),
					s_VoiceOutputDeviceEntries);

				s_VoiceOutputDeviceDisplayNames.reserve(s_VoiceOutputDeviceEntries.size());
				s_VoiceOutputDeviceConfigValues.reserve(s_VoiceOutputDeviceEntries.size());
				s_VoiceOutputDeviceDropDownNames.reserve(s_VoiceOutputDeviceDisplayNames.size());
				for(const auto &Entry : s_VoiceOutputDeviceEntries)
				{
					s_VoiceOutputDeviceDisplayNames.push_back(Entry.m_DisplayName);
					s_VoiceOutputDeviceConfigValues.push_back(Entry.m_ConfigValue);
					s_VoiceOutputDeviceDropDownNames.push_back(s_VoiceOutputDeviceDisplayNames.back().c_str());
				}

				char aVoiceExtra[96];
				str_format(aVoiceExtra, sizeof(aVoiceExtra), "devices=%d", (int)s_VoiceOutputDeviceDisplayNames.size());
				LogQmPerfStage(Client(), "voice_output_device_enum", StageTimer.ElapsedMs(), false, aVoiceExtra);
			};
			if(!ReadOnly && !s_VoiceOutputDevicesInitialized)
			{
				RefreshVoiceOutputDeviceList();
				s_VoiceOutputDevicesInitialized = true;
			}

			CUIRect VoiceOutputDropDownRect;
			CUIRect VoiceOutputRefreshButton;
			ControlCol.VSplitRight(maximum(68.0f, 68.0f * UiScale), &VoiceOutputDropDownRect, &VoiceOutputRefreshButton);

			static CButtonContainer s_VoiceOutputRefreshButton;
			if(ReadOnly)
			{
				DoQmSettingsLabel("qmclient-voice-output-default", &VoiceOutputDropDownRect, Localize("Default"), BodySize);
				DoQmSettingsMenuButton(&s_VoiceOutputRefreshButton, "qmclient-voice-output-refresh", Localize("Refresh"), &VoiceOutputRefreshButton);
			}
			else
			{
				const int VoiceOutputSelectedOld = VoiceUtils::VoiceFindSelectedDeviceIndex(s_VoiceOutputDeviceEntries, g_Config.m_QmVoiceOutputDevice);
				const int VoiceOutputSelectedNew = DoSettingsDropDown(&VoiceOutputDropDownRect, VoiceOutputSelectedOld, s_VoiceOutputDeviceDropDownNames.data(), s_VoiceOutputDeviceDropDownNames.size(), s_VoiceOutputDeviceDropDownState);
				if(VoiceOutputSelectedNew >= 0 && VoiceOutputSelectedNew != VoiceOutputSelectedOld && (size_t)VoiceOutputSelectedNew < s_VoiceOutputDeviceConfigValues.size())
					str_copy(g_Config.m_QmVoiceOutputDevice, s_VoiceOutputDeviceConfigValues[VoiceOutputSelectedNew].c_str(), sizeof(g_Config.m_QmVoiceOutputDevice));
				if(DoQmSettingsMenuButton(&s_VoiceOutputRefreshButton, "qmclient-voice-output-refresh", Localize("Refresh"), &VoiceOutputRefreshButton))
					RefreshVoiceOutputDeviceList();
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			{
				static std::vector<const char *> s_VoiceBitrateProfileDropDownNames;
				s_VoiceBitrateProfileDropDownNames = {
					Localize("Auto"),
					"24 kbps",
					"32 kbps",
					"48 kbps",
					"64 kbps",
				};
				static CUi::SDropDownState s_VoiceBitrateProfileDropDownState;
				static CScrollRegion s_VoiceBitrateProfileDropDownScrollRegion;
				s_VoiceBitrateProfileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceBitrateProfileDropDownScrollRegion;

				Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
				DoQmSettingsLabel("qmclient-voice-bitrate", &LabelCol, Localize("Voice bitrate"), BodySize);
				const int CurrentBitrateProfile = std::clamp(g_Config.m_QmVoiceBitrateProfile, 0, 4);
				const int NewBitrateProfile = DoSettingsDropDown(&ControlCol, CurrentBitrateProfile, s_VoiceBitrateProfileDropDownNames.data(), s_VoiceBitrateProfileDropDownNames.size(), s_VoiceBitrateProfileDropDownState);
				if(CurrentBitrateProfile != NewBitrateProfile)
					g_Config.m_QmVoiceBitrateProfile = NewBitrateProfile;
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			{
				static std::vector<const char *> s_VoiceNoiseSuppressModeDropDownNames;
				s_VoiceNoiseSuppressModeDropDownNames = {
					Localize("No noise reduction"),
					Localize("Simple noise reduction"),
#if defined(CONF_RNNOISE)
					Localize("RNNoise noise reduction"),
#else
					Localize("RNNoise noise reduction (unavailable in this build)"),
#endif
				};
				static CUi::SDropDownState s_VoiceNoiseSuppressModeDropDownState;
				static CScrollRegion s_VoiceNoiseSuppressModeDropDownScrollRegion;
				s_VoiceNoiseSuppressModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceNoiseSuppressModeDropDownScrollRegion;

				Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
				DoQmSettingsLabel("qmclient-voice-noise-reduction-mode", &LabelCol, Localize("Noise reduction mode"), BodySize);
				const int CurrentNoiseSuppressMode = std::clamp(g_Config.m_QmVoiceNoiseSuppressEnable, 0, 2);
				const int NewNoiseSuppressMode = DoSettingsDropDown(&ControlCol, CurrentNoiseSuppressMode, s_VoiceNoiseSuppressModeDropDownNames.data(), s_VoiceNoiseSuppressModeDropDownNames.size(), s_VoiceNoiseSuppressModeDropDownState);
				if(CurrentNoiseSuppressMode != NewNoiseSuppressMode)
					g_Config.m_QmVoiceNoiseSuppressEnable = NewNoiseSuppressMode;
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			if(NoiseSuppressModeForLayout != 0)
			{
#if !defined(CONF_RNNOISE)
				if(NoiseSuppressModeForLayout == 2)
				{
					Content.HSplitTop(LineHeight * 0.78f, &Row, &Content);
					DoQmSettingsLabel("qmclient-voice-rnnoise-fallback-warning", &Row, Localize("RNNoise not integrated in current build, will fallback to simple noise reduction"), Metrics.m_SmallSize);
					Content.HSplitTop(LineSpacing * 0.75f, nullptr, &Content);
				}
#endif
				Content.HSplitTop(LineHeight, &Row, &Content);
				{
					CUIRect LabelColValue, ControlColValue;
					Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
#if !defined(CONF_RNNOISE)
					const bool RnnoiseFallbackActive = NoiseSuppressModeForLayout == 2;
#endif
					const char *pNoiseSuppressStrengthLabel = NoiseSuppressModeForLayout == 2 ?
#if !defined(CONF_RNNOISE)
											  (RnnoiseFallbackActive ? Localize("Fallback simple noise reduction strength") : Localize("RNNoise noise reduction strength")) :
#else
											  Localize("RNNoise noise reduction strength") :
#endif
											  Localize("Simple noise reduction strength");
					Ui()->DoLabel(&LabelColValue, pNoiseSuppressStrengthLabel, BodySize, TEXTALIGN_ML);
					static int s_QmVoiceNoiseSuppressStrengthInputId;
					RenderSliderWithValueInput(&s_QmVoiceNoiseSuppressStrengthInputId, ControlColValue, &g_Config.m_QmVoiceNoiseSuppressStrength, 0, 100, "%");
				}
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			}

			Content.HSplitTop(LineHeight, &Row, &Content);
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceAgcEnable, "Auto gain control for mic (AGC, experimental)", Localize("Auto gain control for mic (AGC, experimental)"), &g_Config.m_QmVoiceAgcEnable, &Row, LineHeight);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			if(g_Config.m_QmVoiceVadEnable)
			{
				Content.HSplitTop(LineHeight, &Row, &Content);
				{
					CUIRect LabelColValue, ControlColValue;
					Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
					DoQmSettingsLabel("qmclient-voice-speech-trigger-threshold", &LabelColValue, Localize("Speech trigger threshold"), BodySize);
					static int s_QmVoiceVadThresholdInputId;
					RenderSliderWithValueInput(&s_QmVoiceVadThresholdInputId, ControlColValue, &g_Config.m_QmVoiceVadThreshold, 0, 100, "%");
				}
				Content.HSplitTop(LineSpacing, nullptr, &Content);

				Content.HSplitTop(LineHeight, &Row, &Content);
				{
					CUIRect LabelColValue, ControlColValue;
					Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
					DoQmSettingsLabel("qmclient-voice-activation-release-delay", &LabelColValue, Localize("Voice activation release delay"), BodySize);
					static int s_QmVoiceVadReleaseDelayMsInputId;
					RenderSliderWithValueInput(&s_QmVoiceVadReleaseDelayMsInputId, ControlColValue, &g_Config.m_QmVoiceVadReleaseDelayMs, 0, 1000, "ms");
				}
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			}

			Content.HSplitTop(LineSpacing * 1.15f, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			{
				CUIRect LabelColValue, ControlColValue;
				Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
				DoQmSettingsLabel("qmclient-voice-playback-volume", &LabelColValue, Localize("Playback volume"), BodySize);
				static int s_QmVoiceVolumeInputId;
				RenderSliderWithValueInput(&s_QmVoiceVolumeInputId, ControlColValue, &g_Config.m_QmVoiceVolume, 0, 400, "%");
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceStereo, "Enable stereo positioning", Localize("Enable stereo positioning"), &g_Config.m_QmVoiceStereo, &Row, LineHeight);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			if(g_Config.m_QmVoiceStereo)
			{
				Content.HSplitTop(LineHeight, &Row, &Content);
				{
					CUIRect LabelColValue, ControlColValue;
					Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
					DoQmSettingsLabel("qmclient-voice-left-right-channel-width", &LabelColValue, Localize("Left/right channel width"), BodySize);
					static int s_QmVoiceStereoWidthInputId;
					RenderSliderWithValueInput(&s_QmVoiceStereoWidthInputId, ControlColValue, &g_Config.m_QmVoiceStereoWidth, 0, 200, "%");
				}
				Content.HSplitTop(LineSpacing, nullptr, &Content);
			}

			Content.HSplitTop(LineHeight, &Row, &Content);
			{
				CUIRect LabelColValue, ControlColValue;
				Row.VSplitLeft(LabelWidth, &LabelColValue, &ControlColValue);
				DoQmSettingsLabel("qmclient-voice-distance-radius-tiles", &LabelColValue, Localize("Voice distance radius (tiles)"), BodySize);
				static int s_QmVoiceRadiusInputId;
				RenderSliderWithValueInput(&s_QmVoiceRadiusInputId, ControlColValue, &g_Config.m_QmVoiceRadius, 1, 400);
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			DoQmSettingsCheckboxAuto(&g_Config.m_QmVoiceGroupGlobal, "Full map listen in same room", Localize("Full map listen in same room"), &g_Config.m_QmVoiceGroupGlobal, &Row, LineHeight);
		}
	}
}

void CMenus::RenderQmHudBackground3DContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	// 单选项在正式绘制阶段提交，分支仍按本帧测量时的模式绘制。
	const int ColorModeForLayout = g_Config.m_Qm3DParticlesColorMode;
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};

	auto RenderIntOption = [&](const void *pId, const char *pLabel, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "", bool TrailingSpacing = true) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		Ui()->DoLabel(&LabelCol, pLabel, BodySize, TEXTALIGN_ML);
		RenderSliderWithValueInput(pId, ControlCol, pValue, MinValue, MaxValue, pSuffix);
		if(TrailingSpacing)
			Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticles, "Enable 3D background particles", Localize("Enable 3D background particles"), &g_Config.m_Qm3DParticles, &Row, LineHeight);
	if(g_Config.m_Qm3DParticles)
		Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_Qm3DParticles)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		DoQmSettingsLabel("qmclient-3d-background-particle-type", &LabelCol, Localize("Particle type"), BodySize);
		std::array<const char *, 9> apQm3DParticleTypeNames = {
			Localize("Cube"),
			Localize("Heart"),
			Localize("Sphere"),
			Localize("Pyramid"),
			Localize("Diamond"),
			Localize("Ring"),
			Localize("Star"),
			Localize("Crescent"),
			Localize("Mixed"),
		};
		const std::array<int, 9> aQm3DParticleTypeValues = {1, 2, 4, 5, 6, 7, 8, 9, 3};
		int TypeIndex = 0;
		for(size_t TypeValueIndex = 0; TypeValueIndex < aQm3DParticleTypeValues.size(); ++TypeValueIndex)
		{
			if(aQm3DParticleTypeValues[TypeValueIndex] == g_Config.m_Qm3DParticlesType)
			{
				TypeIndex = (int)TypeValueIndex;
				break;
			}
		}
		static CUi::SDropDownState s_Qm3DParticleTypeDropDownState;
		static CScrollRegion s_Qm3DParticleTypeDropDownScrollRegion;
		s_Qm3DParticleTypeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_Qm3DParticleTypeDropDownScrollRegion;
		const int NewTypeIndex = DoSettingsDropDown(&ControlCol, TypeIndex, apQm3DParticleTypeNames.data(), static_cast<int>(apQm3DParticleTypeNames.size()), s_Qm3DParticleTypeDropDownState);
		if(NewTypeIndex >= 0 && NewTypeIndex < static_cast<int>(aQm3DParticleTypeValues.size()) && NewTypeIndex != TypeIndex)
			g_Config.m_Qm3DParticlesType = aQm3DParticleTypeValues[NewTypeIndex];
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		static int s_Qm3DParticleCountInputId;
		RenderIntOption(&s_Qm3DParticleCountInputId, Localize("Particle count"), &g_Config.m_Qm3DParticlesCount, 1, 200);
		static int s_Qm3DParticleAlphaInputId;
		RenderIntOption(&s_Qm3DParticleAlphaInputId, Localize("Particle alpha"), &g_Config.m_Qm3DParticlesAlpha, 1, 100, "%");
		static int s_Qm3DParticleMinSizeInputId;
		RenderIntOption(&s_Qm3DParticleMinSizeInputId, Localize("Min size"), &g_Config.m_Qm3DParticlesSizeMin, 2, 64);
		if(!PrewarmOnly && !Ui()->RenderOnly() && g_Config.m_Qm3DParticlesSizeMax < g_Config.m_Qm3DParticlesSizeMin)
			g_Config.m_Qm3DParticlesSizeMax = g_Config.m_Qm3DParticlesSizeMin;
		static int s_Qm3DParticleMaxSizeInputId;
		RenderIntOption(&s_Qm3DParticleMaxSizeInputId, Localize("Max size"), &g_Config.m_Qm3DParticlesSizeMax, g_Config.m_Qm3DParticlesSizeMin, 64);
		static int s_Qm3DParticleSpeedInputId;
		RenderIntOption(&s_Qm3DParticleSpeedInputId, Localize("Particle speed"), &g_Config.m_Qm3DParticlesSpeed, 1, 500);
		static int s_Qm3DParticleDepthInputId;
		RenderIntOption(&s_Qm3DParticleDepthInputId, Localize("Particle depth"), &g_Config.m_Qm3DParticlesDepth, 10, 1000);
		static int s_Qm3DParticleViewMarginInputId;
		RenderIntOption(&s_Qm3DParticleViewMarginInputId, Localize("View margin"), &g_Config.m_Qm3DParticlesViewMargin, 0, 1000);
		static int s_Qm3DParticleFadeInInputId;
		RenderIntOption(&s_Qm3DParticleFadeInInputId, Localize("Fade in"), &g_Config.m_Qm3DParticlesFadeInMs, 1, 5000, "ms");
		static int s_Qm3DParticleFadeOutInputId;
		RenderIntOption(&s_Qm3DParticleFadeOutInputId, Localize("Fade out"), &g_Config.m_Qm3DParticlesFadeOutMs, 1, 5000, "ms");

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesCollide, "Particle collision", Localize("Particle collision"), &g_Config.m_Qm3DParticlesCollide, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		static int s_Qm3DParticlePushRadiusInputId;
		RenderIntOption(&s_Qm3DParticlePushRadiusInputId, Localize("Push radius"), &g_Config.m_Qm3DParticlesPushRadius, 0, 1000);
		static int s_Qm3DParticlePushStrengthInputId;
		RenderIntOption(&s_Qm3DParticlePushStrengthInputId, Localize("Push strength"), &g_Config.m_Qm3DParticlesPushStrength, 0, 2000);

		static std::vector<CButtonContainer> s_vQm3DParticleColorModeButtons = {{}, {}};
		int ColorMode = g_Config.m_Qm3DParticlesColorMode;
		if(DoSettingsLine_RadioMenu(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, Content, "qmclient-3d-particle-color-mode-label", Localize("Particle color"), s_vQm3DParticleColorModeButtons, {"qmclient-3d-particle-color-custom", "qmclient-3d-particle-color-random"}, {Localize("Custom"), Localize("Random")}, {1, 2}, ColorMode, Metrics))
			g_Config.m_Qm3DParticlesColorMode = ColorMode;
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(ColorModeForLayout == 1)
		{
			static CButtonContainer s_Qm3DParticleColorId;
			DoLine_ColorPicker(&s_Qm3DParticleColorId, Metrics, &Content, Localize("Particle color"), &g_Config.m_Qm3DParticlesColor, ColorRGBA(0.56f, 0.72f, 0.62f, 1.0f), false, nullptr, true);
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesGlow, "Particle glow", Localize("Particle glow"), &g_Config.m_Qm3DParticlesGlow, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesGlow)
		{
			static int s_Qm3DParticleGlowAlphaInputId;
			RenderIntOption(&s_Qm3DParticleGlowAlphaInputId, Localize("Glow alpha"), &g_Config.m_Qm3DParticlesGlowAlpha, 1, 100, "%");
			static int s_Qm3DParticleGlowOffsetInputId;
			RenderIntOption(&s_Qm3DParticleGlowOffsetInputId, Localize("Glow offset"), &g_Config.m_Qm3DParticlesGlowOffset, 1, 20);
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesTrail, "Particle trail", Localize("Particle trail"), &g_Config.m_Qm3DParticlesTrail, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesTrail)
		{
			static int s_Qm3DParticleTrailLengthInputId;
			RenderIntOption(&s_Qm3DParticleTrailLengthInputId, Localize("Trail length"), &g_Config.m_Qm3DParticlesTrailLength, 2, 6);
			static int s_Qm3DParticleTrailAlphaInputId;
			RenderIntOption(&s_Qm3DParticleTrailAlphaInputId, Localize("Trail alpha"), &g_Config.m_Qm3DParticlesTrailAlpha, 1, 100, "%");
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesPulse, "Particle pulse", Localize("Particle pulse"), &g_Config.m_Qm3DParticlesPulse, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesPulse)
		{
			static int s_Qm3DParticlePulseStrengthInputId;
			RenderIntOption(&s_Qm3DParticlePulseStrengthInputId, Localize("Pulse strength"), &g_Config.m_Qm3DParticlesPulseStrength, 0, 50, "%");
			static int s_Qm3DParticlePulseSpeedInputId;
			RenderIntOption(&s_Qm3DParticlePulseSpeedInputId, Localize("Pulse speed"), &g_Config.m_Qm3DParticlesPulseSpeed, 10, 300, "%");
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesTwinkle, "Particle twinkle", Localize("Particle twinkle"), &g_Config.m_Qm3DParticlesTwinkle, &Row, LineHeight);

		if(g_Config.m_Qm3DParticlesTwinkle)
		{
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			static int s_Qm3DParticleTwinkleStrengthInputId;
			RenderIntOption(&s_Qm3DParticleTwinkleStrengthInputId, Localize("Twinkle strength"), &g_Config.m_Qm3DParticlesTwinkleStrength, 0, 100, "%", false);
		}
	}
}

void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_hud", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ModuleStateIndex = [](EQmModuleId Id) { return std::clamp((int)Id, 0, (int)QmModuleCount - 1); };
	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	const bool DummyMiniViewExpanded = g_Config.m_QmDummyMiniView != 0;
	const bool DynamicIslandOriginalStyle = g_Config.m_QmHudIslandUseOriginalStyle != 0;
	auto EstimateContentHeight = [Metrics, LineHeight, LineSpacing, DummyMiniViewExpanded, DynamicIslandOriginalStyle](EQmModuleId Id, float ContentWidth) {
		const auto Rows = [LineHeight, LineSpacing](float Count) { return Count * (LineHeight + LineSpacing); };
		switch(Id)
		{
		case EQmModuleId::DummyMiniView: return ResolveQmHudDummyMiniViewHeight(Metrics, DummyMiniViewExpanded);
		case EQmModuleId::Coords: return ResolveQmHudCoordsHeight(Metrics);
		case EQmModuleId::PlayerStats: return ResolveQmHudPlayerStatsHeight(Metrics, g_Config.m_QmPlayerStatsMapProgress != 0, g_Config.m_QmPlayerStatsMapProgressStyle != 0);
		case EQmModuleId::DebugGraph: return Rows(2.0f);
		case EQmModuleId::DebugMode: return Rows(5.0f);
		case EQmModuleId::InputOverlay: return ResolveQmHudInputOverlayHeight(Metrics, g_Config.m_QmInputOverlay != 0);
		case EQmModuleId::HudNotifications: return ResolveQmHudNotificationsHeight(Metrics, g_Config.m_QmHudNotificationsShowAdvanced != 0, g_Config.m_QmHudNotificationsUseCategoryFilters != 0);
		case EQmModuleId::Voice: return ResolveQmHudVoiceHeight(Metrics, g_Config.m_QmVoiceEnable != 0, g_Config.m_QmVoiceShowAdvanced != 0, g_Config.m_QmVoiceShowConnectionStatus != 0, g_Config.m_QmVoiceNoiseSuppressEnable, g_Config.m_QmVoiceVadEnable != 0, g_Config.m_QmVoiceStereo != 0);
		case EQmModuleId::DynamicIsland: return ResolveQmHudDynamicIslandHeight(Metrics, DynamicIslandOriginalStyle, g_Config.m_QmSwitchCountdown != 0, ContentWidth);
		case EQmModuleId::SystemMediaControls: return g_Config.m_QmSmtcEnable ? Rows(7.0f) : Rows(1.0f);
		case EQmModuleId::Background3D: return ResolveQmHudBackground3DHeight(Metrics, ContentWidth, g_Config.m_Qm3DParticles != 0, g_Config.m_Qm3DParticlesColorMode == 1, g_Config.m_Qm3DParticlesGlow != 0, g_Config.m_Qm3DParticlesTrail != 0, g_Config.m_Qm3DParticlesPulse != 0, g_Config.m_Qm3DParticlesTwinkle != 0);
		case EQmModuleId::BindStatusHud: return Rows(4.0f); // 4 个内置状态开关
		default: return Rows(1.0f);
		}
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};

	const auto ConsumeQmHudRow = [LineHeight, LineSpacing](CUIRect &Content) {
		Content.HSplitTop(LineHeight, nullptr, &Content);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	const auto ConsumeQmHudHeight = [](CUIRect &Content, const float Height) {
		Content.HSplitTop(std::max(0.0f, Height), nullptr, &Content);
	};
	const auto BuildHudPreLayoutInput = [this, Metrics, LineHeight, LineSpacing, ReadOnly, ConsumeQmHudRow, ConsumeQmHudHeight](EQmModuleId Id) -> FSettingsCardPreLayoutInput {
		if(ReadOnly)
			return {};
		switch(Id)
		{
		case EQmModuleId::InputOverlay:
			return [this, Metrics](CUIRect Content) {
				return HandleQmHudCheckboxInput(Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmInputOverlay, &g_Config.m_QmInputOverlay);
			};
		case EQmModuleId::DynamicIsland:
			return [this, Metrics, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandUseOriginalStyle, &g_Config.m_QmHudIslandUseOriginalStyle);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudIslandShowTeam, &g_Config.m_QmHudIslandShowTeam) || Changed;
				if(!g_Config.m_QmHudIslandUseOriginalStyle)
				{
					const SSettingsColorRowLayout ColorLayout = ResolveSettingsColorRowLayout(Content, Metrics, false);
					Content.HSplitTop(ColorLayout.m_ConsumedHeight, nullptr, &Content);
				}
				return Changed;
			};
		case EQmModuleId::PlayerStats:
			return [this, LineHeight, LineSpacing, ConsumeQmHudRow](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsHud, &g_Config.m_QmPlayerStatsHud);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgress, &g_Config.m_QmPlayerStatsMapProgress) || Changed;
				if(g_Config.m_QmPlayerStatsMapProgress)
				{
					Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressStyle, &g_Config.m_QmPlayerStatsMapProgressStyle) || Changed;
					if(!g_Config.m_QmPlayerStatsMapProgressStyle)
					{
						ConsumeQmHudRow(Content);
						for(int Index = 0; Index < 4; ++Index)
							ConsumeQmHudRow(Content);
					}
					Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsMapProgressDbgRoute, &g_Config.m_QmPlayerStatsMapProgressDbgRoute) || Changed;
				}
				HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmPlayerStatsResetOnJoin, &g_Config.m_QmPlayerStatsResetOnJoin);
				return Changed;
			};
		case EQmModuleId::HudNotifications:
			return [this, LineHeight, LineSpacing, ConsumeQmHudRow](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsSystem, &g_Config.m_QmHudNotificationsSystem);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsEcho, &g_Config.m_QmHudNotificationsEcho) || Changed;
				ConsumeQmHudRow(Content);
				ConsumeQmHudRow(Content);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsShowAdvanced, &g_Config.m_QmHudNotificationsShowAdvanced) || Changed;
				if(g_Config.m_QmHudNotificationsShowAdvanced)
					Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmHudNotificationsUseCategoryFilters, &g_Config.m_QmHudNotificationsUseCategoryFilters) || Changed;
				return Changed;
			};
		case EQmModuleId::Voice:
			return [this, Metrics, LineHeight, LineSpacing, ConsumeQmHudRow, ConsumeQmHudHeight](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceEnable, &g_Config.m_QmVoiceEnable);
				if(!g_Config.m_QmVoiceEnable)
					return Changed;
				ConsumeQmHudRow(Content); // room password
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceMicMute, &g_Config.m_QmVoiceMicMute) || Changed;
				ConsumeQmHudRow(Content); // microphone volume
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceVadEnable, &g_Config.m_QmVoiceVadEnable) || Changed;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceShowAdvanced, &g_Config.m_QmVoiceShowAdvanced) || Changed;
				if(!g_Config.m_QmVoiceShowAdvanced)
					return Changed;

				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceShowConnectionStatus, &g_Config.m_QmVoiceShowConnectionStatus) || Changed;
				if(g_Config.m_QmVoiceShowConnectionStatus)
				{
					ConsumeQmHudHeight(Content, LineHeight * 1.46f + LineSpacing * 0.75f);
					ConsumeQmHudHeight(Content, 10.0f * (LineHeight + LineSpacing * 0.75f) + LineSpacing * 0.5f);
				}

				for(int Index = 0; Index < 5; ++Index)
					ConsumeQmHudRow(Content); // server, input/output device, bitrate and noise mode
				if(g_Config.m_QmVoiceNoiseSuppressEnable != 0)
				{
#if !defined(CONF_RNNOISE)
					if(g_Config.m_QmVoiceNoiseSuppressEnable == 2)
						ConsumeQmHudHeight(Content, LineHeight * 0.78f + LineSpacing * 0.75f);
#endif
					ConsumeQmHudRow(Content); // noise reduction strength
				}
				ConsumeQmHudRow(Content); // AGC
				if(g_Config.m_QmVoiceVadEnable)
				{
					ConsumeQmHudRow(Content);
					ConsumeQmHudRow(Content);
				}
				ConsumeQmHudHeight(Content, LineSpacing * 1.15f);
				ConsumeQmHudRow(Content); // playback volume
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmVoiceStereo, &g_Config.m_QmVoiceStereo) || Changed;
				if(g_Config.m_QmVoiceStereo)
					ConsumeQmHudRow(Content);
				return Changed;
			};
		case EQmModuleId::SystemMediaControls:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, &g_Config.m_QmSmtcEnable);
				return Changed;
			};
		case EQmModuleId::Background3D:
			return [this, Metrics, LineHeight, LineSpacing, ConsumeQmHudRow, ConsumeQmHudHeight](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticles, &g_Config.m_Qm3DParticles);
				if(!g_Config.m_Qm3DParticles)
					return Changed;
				ConsumeQmHudRow(Content); // particle type
				for(int Index = 0; Index < 9; ++Index)
					ConsumeQmHudRow(Content); // numeric particle options
				ConsumeQmHudRow(Content); // collision
				ConsumeQmHudRow(Content); // push radius
				ConsumeQmHudRow(Content); // push strength
				const SSettingsRadioRowLayout RadioLayout = ResolveSettingsRadioRowLayout(Content, 2, Metrics);
				ConsumeQmHudHeight(Content, RadioLayout.m_Height + LineSpacing);
				if(g_Config.m_Qm3DParticlesColorMode == 1)
					ConsumeQmHudRow(Content); // custom color
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesGlow, &g_Config.m_Qm3DParticlesGlow) || Changed;
				if(g_Config.m_Qm3DParticlesGlow)
				{
					ConsumeQmHudRow(Content);
					ConsumeQmHudRow(Content);
				}
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesTrail, &g_Config.m_Qm3DParticlesTrail) || Changed;
				if(g_Config.m_Qm3DParticlesTrail)
				{
					ConsumeQmHudRow(Content);
					ConsumeQmHudRow(Content);
				}
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesPulse, &g_Config.m_Qm3DParticlesPulse) || Changed;
				if(g_Config.m_Qm3DParticlesPulse)
				{
					ConsumeQmHudRow(Content);
					ConsumeQmHudRow(Content); // pulse strength/speed are two rows
				}
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_Qm3DParticlesTwinkle, &g_Config.m_Qm3DParticlesTwinkle) || Changed;
				return Changed;
			};
		case EQmModuleId::BindStatusHud:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusReset, &g_Config.m_ClShowhudKeyStatusReset);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusHammer, &g_Config.m_ClShowhudKeyStatusHammer) || Changed;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusControl, &g_Config.m_ClShowhudKeyStatusControl) || Changed;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_ClShowhudKeyStatusSync, &g_Config.m_ClShowhudKeyStatusSync) || Changed;
				return Changed;
			};
		default:
			return {};
		}
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 注意：目录的 Hud 清单含独立的 qm:lyrics 卡（本地此前把歌词画在 SMTC 卡内，已在上方移出），
	// 故切换后 HUD 页会多出一张歌词卡——这是远程的结构意图，注册表与布局表本地早已具备。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::HudCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_HudPrewarmOrderModel;
	static bool s_HudPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_HudPrewarmDeck;
	if(ReadOnly && !s_HudPrewarmOrderModelInitialized)
	{
		s_HudPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_HudPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_HudPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_HudPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "hud", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

// 卡片测量所需的内容量状态（取自上方的文件静态变量）。目录的 Function 分类测量要读它：
// 否则词条过滤/关键词回复/收藏地图三张卡的内容量会按默认值（0/1）计算，高度偏小。
// 与远程同名函数等价，字段顺序须与 qm_card_catalog::SQmFunctionCardLayoutState 一致。
static qm_card_catalog::SQmFunctionCardLayoutState ResolveFunctionCardLayoutState()
{
	return {s_BlockWordsLayoutRevision, s_KeywordRulesLayoutRevision, s_KeywordRulesLayoutCount, s_KeywordRulesLayoutHalfFilled, s_FavoriteMapsLayoutRevision};
}

void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_function", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ModuleStateIndex = [](EQmModuleId Id) { return std::clamp((int)Id, 0, (int)QmModuleCount - 1); };
	if(str_comp(s_aBlockWordsLayoutConfigCache, g_Config.m_QmBlockWordsList) != 0)
	{
		str_copy(s_aBlockWordsLayoutConfigCache, g_Config.m_QmBlockWordsList, sizeof(s_aBlockWordsLayoutConfigCache));
		++s_BlockWordsLayoutRevision;
	}
	if(str_comp(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules) != 0)
	{
		char aDecodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		QmKeywordReplyRules::DecodeFromConfig(g_Config.m_QmKeywordReplyRules, aDecodedRules, sizeof(aDecodedRules));
		s_KeywordRuleRowsInited = false;
		UpdateKeywordRulesLayoutState(CountAutoReplyRules(aDecodedRules), false);
		str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
	const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
	if(s_FavoriteMapsLayoutCount != FavoriteMapCount)
	{
		s_FavoriteMapsLayoutCount = FavoriteMapCount;
		++s_FavoriteMapsLayoutRevision;
	}
	auto ToggleCollapsed = [](void *pUser, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		const bool WasCollapsed = s_aCollapsed[Index];
		const bool Collapsed = !WasCollapsed;
		if(!qm_card_collapse::SetQmModuleCollapsed(Id, Collapsed))
			return;
		s_aCollapsed[Index] = Collapsed;
		if(Id == EQmModuleId::BlockWords && WasCollapsed != Collapsed && !Collapsed)
			++s_BlockWordsLayoutRevision;
		else if(Id == EQmModuleId::QiaFen && WasCollapsed != Collapsed && !Collapsed)
		{
			s_KeywordRuleRowsInited = false;
			++s_KeywordRulesLayoutRevision;
		}
		else if(Id == EQmModuleId::FavoriteMaps && WasCollapsed != Collapsed && !Collapsed)
		{
			const size_t FavoriteMapCount = static_cast<CMenus *>(pUser)->GameClient()->TClientComponent().GetFavoriteMaps().size();
			if(s_FavoriteMapsLayoutCount != FavoriteMapCount)
			{
				s_FavoriteMapsLayoutCount = FavoriteMapCount;
				++s_FavoriteMapsLayoutRevision;
			}
		}
	};
	auto MeasureContentHeight = [this, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, Metrics](EQmModuleId Id, float ContentWidth) {
		const auto Rows = [LineHeight, LineSpacing](float Count) { return Count * (LineHeight + LineSpacing); };
		const auto Row = [LineHeight, LineSpacing](float Spacing = 1.0f) { return LineHeight + LineSpacing * Spacing; };
		switch(Id)
		{
		case EQmModuleId::GoresActor:
			return !g_Config.m_TcFreezeChatEnabled ? Row() : Row() * (g_Config.m_TcFreezeChatEmoticon ? 5.0f : 4.0f);
		case EQmModuleId::Gores:
			// 3 行固定项 + Axiom 登录的 2 行密码框 + 开关组 7 行（与 RenderQmFunctionGoresContent 逐项对应）+ 键位行。
			return Row() * (3.0f + (g_Config.m_QmAxiomAutoLogin ? 2.0f : 0.0f) + ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 7.0f : 0.0f)) + LineHeight;
		case EQmModuleId::KeyBinds: return Rows(8.0f);
		case EQmModuleId::MiniFeatures:
			// IME 已独立为 qm:ime 卡；这里按剩余小功能行测量。
			// 与 RenderQmFunctionMiniFeaturesContent 逐行对应；新增控件时须同步更新此计数。
			return Rows(16.0f);
		case EQmModuleId::JumpHint: return Row() * 5.0f;
		case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory == 0 ? Row() : Row() * 6.0f;
		case EQmModuleId::FriendNotify:
			return Row() * (6.0f + (g_Config.m_QmFriendOnlineAutoRefresh ? 1.0f : 0.0f) + (g_Config.m_QmFriendEnterBroadcast ? 1.0f : 0.0f) + (g_Config.m_QmFriendEnterAutoGreet ? 1.0f : 0.0f));
		case EQmModuleId::BlockWords: return Row() * (g_Config.m_QmBlockWordsAction == 0 ? 7.0f : 4.0f) + CalcQiaFenInputHeight(TextRender(), g_Config.m_QmBlockWordsList, std::max(1.0f, ContentWidth - LabelWidth), BodySize, std::clamp(2.0f * UiScale, 1.0f, 2.0f), LineHeight);
		case EQmModuleId::Translate:
		{
			const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
			const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
			const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
			const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
			const bool IsDeeplBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0;
			float Height = Rows(9.0f) + LineHeight * 1.6f + LineSpacing * 1.35f;
			if(IsFtapiBackend)
				Height += Row() + LineHeight * 0.8f + LineSpacing;
			if(IsDeeplBackend)
				Height += Row() + LineHeight * 0.8f + LineSpacing * 1.5f;
			if(IsTencentCloudBackend)
				Height += Row() * 4.0f;
			else if(IsLibreTranslateBackend)
				Height += Row() * 2.0f;
			if(IsLlmBackend)
			{
				Height += Row() * 7.0f + LineHeight + LineSpacing * 0.5f;
				if(g_Config.m_QmTranslateLlmEnableThinking && (g_Config.m_QmTranslateLlmProvider == 2 || g_Config.m_QmTranslateLlmProvider == 3))
					Height += Metrics.m_SmallSize + Metrics.m_LineSpacing;
			}
			return Height;
		}
		case EQmModuleId::TranslateUi: return Rows(5.0f);
		case EQmModuleId::QiaFen:
			return Row() * (4.0f + (float)s_KeywordRulesLayoutCount) + (s_KeywordRulesLayoutHalfFilled ? Row() : 0.0f);
		case EQmModuleId::PieMenu:
			return qm_card_catalog::QmPieMenuContentHeight(ContentWidth, LineHeight, BodySize, LineSpacing, g_Config.m_QmPieMenuEnabled != 0, g_Config.m_QmPieFollowName[0] != '\0');
		case EQmModuleId::FavoriteMaps:
		{
			const size_t FavoriteCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
			return Rows((float)(4 + std::max<size_t>(1, std::min<size_t>(FavoriteCount, 64))));
		}
		case EQmModuleId::HJAssist: return Row() * (6.0f + (g_Config.m_QmAutoTeamLock ? 1.0f : 0.0f) + (g_Config.m_QmPausedSpectatorFade ? 1.0f : 0.0f));
		default: return Rows(1.0f);
		}
	};
	auto MeasureContentRevision = [this](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id, ResolveFunctionCardLayoutState());
	};
	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	// 内容量状态（词条过滤/关键词回复/收藏地图）经 m_pFunctionLayout 注入，目录测量依赖它。
	const qm_card_catalog::SQmFunctionCardLayoutState FunctionCardLayout = ResolveFunctionCardLayoutState();
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pFunctionLayout = &FunctionCardLayout;
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		CardBuild.m_pToggleCollapsedUser = this;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::FunctionCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_FunctionPrewarmOrderModel;
	static bool s_FunctionPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_FunctionPrewarmDeck;
	if(ReadOnly && !s_FunctionPrewarmOrderModelInitialized)
	{
		s_FunctionPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_FunctionPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_FunctionPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_FunctionPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "function", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const float LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_visual", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_ScrollRegion;
	static std::array<bool, QmModuleCount> s_aCollapsed = {};
	static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aCollapsed);

	auto ModuleStateIndex = [](EQmModuleId Id) { return std::clamp((int)Id, 0, (int)QmModuleCount - 1); };
	auto ToggleCollapsed = [](void *, EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !s_aCollapsed[Index]))
			s_aCollapsed[Index] = !s_aCollapsed[Index];
	};
	auto EstimateContentHeight = [Metrics](EQmModuleId Id) {
		const auto Rows = [&Metrics](float Count) { return Count * Metrics.m_RowStep; };
		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			return g_Config.m_QmChatBubble ? Rows(5.0f) + 2.0f * Metrics.m_LineHeight + 2.0f * Metrics.m_LineSpacing : Rows(1.0f);
		case EQmModuleId::CameraView:
			return Rows(6.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;
		case EQmModuleId::SkinTransition:
			return ResolveQmVisualSkinTransitionHeight(Metrics, g_Config.m_QmSkinChangeTransition != 0);
		case EQmModuleId::SkinAppearance:
			return ResolveQmVisualSkinAppearanceHeight(Metrics);
		case EQmModuleId::FocusMode:
			return ResolveQmVisualFocusModeHeight(Metrics);
		case EQmModuleId::WeaponAnimation:
			return ResolveQmVisualWeaponAnimationHeight(Metrics, g_Config.m_QmWeaponSwitchAnim != 0, g_Config.m_QmWeaponReloadAnim != 0);
		case EQmModuleId::Streamer: return Rows(3.0f);
		case EQmModuleId::EntityOverlay: return Rows(9.0f);
		case EQmModuleId::CollisionHitbox:
			return ResolveQmVisualCollisionHitboxHeight(Metrics, g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox);
		case EQmModuleId::TranslateUi: return Rows(6.0f);
		default: return Rows(1.0f);
		}
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		return qm_card_catalog::MeasureModuleCardRevision(Id);
	};
	const auto ConsumeVisualRow = [LineHeight, LineSpacing](CUIRect &Content) {
		Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
	};
	const auto BuildVisualPreLayoutInput = [this, LineHeight, LineSpacing, ReadOnly, ConsumeVisualRow](EQmModuleId Id) -> FSettingsCardPreLayoutInput {
		if(ReadOnly)
			return {};
		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				return HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmChatBubble, &g_Config.m_QmChatBubble);
			};
		case EQmModuleId::CameraView:
			return [this, LineHeight, LineSpacing, ConsumeVisualRow](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmCameraDrift, &g_Config.m_QmCameraDrift);
				if(g_Config.m_QmCameraDrift)
				{
					ConsumeVisualRow(Content);
					ConsumeVisualRow(Content);
					ConsumeVisualRow(Content);
				}
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmDynamicFov, &g_Config.m_QmDynamicFov) || Changed;
				if(g_Config.m_QmDynamicFov)
				{
					ConsumeVisualRow(Content);
					ConsumeVisualRow(Content);
				}
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmCinematicCamera, &g_Config.m_QmCinematicCamera) || Changed;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, &g_Config.m_QmZoomInstantReverse) || Changed;
				return Changed;
			};
		case EQmModuleId::SkinTransition:
			return [this, LineHeight, LineSpacing, ConsumeVisualRow](CUIRect Content) {
				// 偷皮保持独立，只有动画开关影响其下方五行高级参数的高度。
				ConsumeVisualRow(Content); // hammer skin steal
				return HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinChangeTransition, &g_Config.m_QmSkinChangeTransition);
			};
		case EQmModuleId::SkinAppearance:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinOutlineLocal, &g_Config.m_QmSkinOutlineLocal);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinOutlineOthers, &g_Config.m_QmSkinOutlineOthers) || Changed;
				return Changed;
			};
		case EQmModuleId::WeaponAnimation:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim, &g_Config.m_QmWeaponSwitchAnim);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, &g_Config.m_QmWeaponReloadAnim) || Changed;
				return Changed;
			};
		case EQmModuleId::CollisionHitbox:
			return [this, LineHeight](CUIRect Content) {
				const CUIRect VisibleContent = Content;
				CUIRect Row;
				Content.HSplitTop(LineHeight, &Row, &Content);
				const CUIRect HitRect = Row.Intersection(VisibleContent);
				int HitboxModeEnabled = g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox;
				const bool Changed = HitRect.w > 0.0f && HitRect.h > 0.0f && Ui()->DoButtonLogic(&g_Config.m_QmHitboxMode, HitboxModeEnabled, &HitRect, BUTTONFLAG_LEFT) != 0;
				if(Changed)
				{
					HitboxModeEnabled ^= 1;
					g_Config.m_QmHitboxMode = HitboxModeEnabled;
					g_Config.m_QmShowCollisionHitbox = 0;
				}
				return Changed;
			};
		default:
			return {};
		}
	};

	// 卡片改由全局卡片目录构造（N3）：页面只声明「这一页有哪些卡片」，测量与渲染都在目录里。
	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::SQmCardBuildContext CardBuild;
		CardBuild.m_pMenus = this;
		CardBuild.m_ReadOnly = ReadOnly;
		CardBuild.m_Page = Page;
		CardBuild.m_Metrics = Metrics;
		CardBuild.m_LabelWidth = LabelWidth;
		CardBuild.m_UiContext = CardCtx;
		CardBuild.m_Padding = CardStyle.m_Padding;
		CardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
		CardBuild.m_pCollapsed = s_aCollapsed.data();
		CardBuild.m_pCollapseButtons = s_aCollapseButtons.data();
		CardBuild.m_pToggleCollapsed = ToggleCollapsed;
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::VisualCardStableIds(), vCards);
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
	{
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_aCollapsed[ModuleIndex] ? 1u : 0u);
	}
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_AllowHeaderDrag = !ReadOnly;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	static qm_card_order::CModel s_VisualPrewarmOrderModel;
	static bool s_VisualPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_VisualPrewarmDeck;
	if(ReadOnly && !s_VisualPrewarmOrderModelInitialized)
	{
		s_VisualPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_VisualPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_VisualPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_VisualPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "visual", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsGlobalSearch(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsGlobalSearchContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsQmClient(CUIRect MainView, bool PrewarmOnly)
{
	// 文本计划收集等 RenderOnly 场景也要保证绑定缓存可用（幂等）。
	EnsureSettingsBindCache();

	RenderSettingsQmClientContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float SmallSize = Metrics.m_SmallSize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext SearchCtx = SettingsUiContext("settings_global_search", UiScale);
	if(ReadOnly)
	{
		SearchCtx.m_pAnim = nullptr;
		SearchCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_GlobalSearchScrollRegion;
	static std::array<bool, qm_module::QmModuleCount> s_aGlobalSearchCollapsed = {};
	static std::array<CButtonContainer, qm_module::QmModuleCount> s_aGlobalSearchCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aGlobalSearchCollapsed);
	auto ToggleSearchCollapsed = [](void *pUser, qm_module::EQmModuleId Id) {
		bool *pCollapsed = static_cast<bool *>(pUser);
		if(pCollapsed == nullptr)
			return;
		const int Index = std::clamp((int)Id, 0, (int)qm_module::QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !pCollapsed[Index]))
			pCollapsed[Index] = !pCollapsed[Index];
	};

	CLineInputBuffered<128> &ModuleSearchInput = m_GlobalCardSearchInput;
	const char *pModuleSearch = ModuleSearchInput.GetString();
	struct SGlobalSearchCache
	{
		bool m_Valid = false;
		std::string m_Search;
		std::string m_Language;
		bool m_Sixup = false;
		uint64_t m_LayoutRevision = 0;
		SQmGlobalSearchResults m_Results;
		qm_card_order::CModel m_Model;
	};
	static SGlobalSearchCache s_GlobalSearchCache;
	static qm_card_order::CModel s_GlobalSearchPrewarmOrderModel;
	static CSettingsCardDeck s_GlobalSearchPrewarmDeck;
	static qm_card_catalog::SQmCardBuildContext s_GlobalSearchCardBuild;
	static qm_card_catalog::SQmFunctionCardLayoutState s_GlobalSearchFunctionCardLayout;
	static std::unordered_map<std::string, CButtonContainer> s_GlobalSearchActionButtons;
	// 分类布局只用于解析搜索和导航；以下两个模型仅属于搜索页，不持久化。
	const qm_card_order::CModel &CardOrderModel = SettingsCardOrderModel();
	qm_card_order::CModel &DeckOrderModel = ReadOnly ? s_GlobalSearchPrewarmOrderModel : s_GlobalSearchCache.m_Model;
	CSettingsCardDeck &CardDeck = ReadOnly ? s_GlobalSearchPrewarmDeck : m_SettingsCardDeck;
	const char *pLanguage = g_Config.m_ClLanguagefile;
	const uint64_t LayoutRevision = CardOrderModel.LayoutRevision();
	if(!s_GlobalSearchCache.m_Valid || s_GlobalSearchCache.m_Search != (pModuleSearch != nullptr ? pModuleSearch : "") || s_GlobalSearchCache.m_Language != (pLanguage != nullptr ? pLanguage : "") || s_GlobalSearchCache.m_Sixup != Client()->IsSixup() || s_GlobalSearchCache.m_LayoutRevision != LayoutRevision)
	{
		s_GlobalSearchCache.m_Search = pModuleSearch != nullptr ? pModuleSearch : "";
		s_GlobalSearchCache.m_Language = pLanguage != nullptr ? pLanguage : "";
		s_GlobalSearchCache.m_Sixup = Client()->IsSixup();
		s_GlobalSearchCache.m_LayoutRevision = LayoutRevision;
		CollectGlobalSearchResults(pModuleSearch, s_GlobalSearchCache.m_Sixup, CardOrderModel, s_GlobalSearchCache.m_Results);
		auto &vResults = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vResults.size() + 1);
		// 搜索输入卡片不再进入卡组：它脱离滚动流手动绘制（默认居中，搜索后置顶）。
		vEntries.push_back({"deck:global-search-empty", "global-search", 0, -1});
		for(size_t Index = 0; Index < vResults.size(); ++Index)
		{
			// 搜索结果卡片使用半宽：交替放入左右两列（0=整宽保留给搜索输入与空态卡片）。
			const int ResultColumn = Index % 2 == 0 ? 1 : 2;
			vEntries.push_back({vResults[Index].m_pStableId, "global-search", ResultColumn, (int)(Index / 2)});
		}
		s_GlobalSearchCache.m_Model.SetEntries(vEntries);
		s_GlobalSearchPrewarmOrderModel.SetEntries(std::move(vEntries));
		s_GlobalSearchCache.m_Valid = true;
	}
	const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
	const int SearchMatchedGlobalCardCount = (int)SearchVisibleGlobalCards.size();
	s_GlobalSearchFunctionCardLayout = ResolveFunctionCardLayoutState();
	uint64_t CardLayoutRevision = str_quickhash("global-search");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(pModuleSearch != nullptr ? pModuleSearch : "");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ LayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Client()->IsSixup() ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureModuleCardsRevision(s_GlobalSearchFunctionCardLayout);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_BlockWordsRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_KeywordRulesRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_FavoriteMapsRevision;
	for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(Card.m_pStableId);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	s_GlobalSearchCardBuild = {};
	s_GlobalSearchCardBuild.m_pMenus = this;
	s_GlobalSearchCardBuild.m_ReadOnly = ReadOnly;
	s_GlobalSearchCardBuild.m_Page = Page;
	s_GlobalSearchCardBuild.m_Metrics = Metrics;
	s_GlobalSearchCardBuild.m_LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	s_GlobalSearchCardBuild.m_UiContext = SearchCtx;
	s_GlobalSearchCardBuild.m_Padding = CardStyle.m_Padding;
	s_GlobalSearchCardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
	s_GlobalSearchCardBuild.m_pCollapsed = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pCollapseButtons = s_aGlobalSearchCollapseButtons.data();
	s_GlobalSearchCardBuild.m_pToggleCollapsed = ToggleSearchCollapsed;
	s_GlobalSearchCardBuild.m_pToggleCollapsedUser = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pFunctionLayout = &s_GlobalSearchFunctionCardLayout;

	const bool HasSearchQuery = pModuleSearch != nullptr && pModuleSearch[0] != '\0';
	// s_GlobalSearchCardBuild 是函数内静态对象，直接引用即可，无需附加捕获。
	const auto BuildDefinitions = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, HasSearchQuery, ReadOnly, &SearchVisibleGlobalCards](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(SearchMatchedGlobalCardCount + 1);
		// 搜索输入卡片已脱离卡组，在 RenderCached 之前手动绘制并定位。

		SSettingsCardDefinition EmptyCard;
		EmptyCard.m_Spec = {"deck:global-search-empty", Localize("Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-results")};
		EmptyCard.m_Measure = [LineHeight](float) { return LineHeight; };
		EmptyCard.m_Render = [this, SmallSize, LineHeight](CUIRect Content) {
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			DoSettingsMenuLabel(SETTINGS_SEARCH, -1, -1, "qmclient-search-no-matching-features", &Row, Localize("No matching features found. Try other keywords"), SmallSize, TEXTALIGN_ML, {}, (int)Row.w);
		};
		EmptyCard.m_IsVisible = [HasSearchQuery, SearchMatchedGlobalCardCount] { return HasSearchQuery && SearchMatchedGlobalCardCount == 0; };
		vCards.push_back(std::move(EmptyCard));

		for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		{
			SSettingsCardDefinition Definition;
			if(!qm_card_catalog::BuildCard(s_GlobalSearchCardBuild, Card.m_pStableId, Definition))
			{
				const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(Card.m_pStableId);
				Definition.m_Spec = {Card.m_pStableId, pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : Localize("Global card"), qm_card_registry::ResolveLocalizedDescription(Card.m_pStableId)};
				// 兜底卡无法复用原页面的卡片构建器（构建闭包绑定在各自页面），
				// 退化为「描述 + 定位按钮」的导航卡，而不是只有一行定位。
				Definition.m_Measure = [LineHeight, LineSpacing](float) { return LineHeight + LineSpacing * 0.65f + LineHeight; };
				CButtonContainer *pOpenButton = &s_GlobalSearchActionButtons[std::string("open:") + Card.m_pStableId];
				Definition.m_Render = [this, Description = Card.m_Description, Target = Card.m_Target, pOpenButton, ReadOnly, LineHeight, LineSpacing, SmallSize](CUIRect Content) {
					CUIRect Row;
					Content.HSplitTop(LineHeight, &Row, &Content);
					SLabelProperties DescProps;
					DescProps.m_MaxWidth = Row.w;
					DescProps.m_EllipsisAtEnd = true;
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
					Ui()->DoLabel(&Row, Description.c_str(), SmallSize, TEXTALIGN_ML, DescProps);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
					Content.HSplitTop(LineHeight, &Row, &Content);
					const char *pLabel = Localize("Locate");
					const float TextWidth = TextRender()->TextWidth(SmallSize, pLabel);
					CUIRect LocateButton = {Row.x, Row.y, std::min(Row.w, TextWidth + 16.0f), Row.h};
					if(LocateButton.w <= 0.0f)
						return;
					if(!ReadOnly && Ui()->DoButtonLogic(pOpenButton, 0, &LocateButton, BUTTONFLAG_LEFT))
					{
						NavigateToSettingsCard(Target);
						Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
						m_GlobalCardSearchInput.Deactivate();
						return;
					}
					// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
					const bool Hovered = !ReadOnly && Ui()->MouseHovered(&LocateButton);
					const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, Hovered ? 0.28f : 0.18f);
					DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, 5.0f);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				};
				vCards.push_back(std::move(Definition));
				continue;
			}
			CButtonContainer *pLocateButton = &s_GlobalSearchActionButtons[std::string("locate:") + Card.m_pStableId];
			const FSettingsCardHeaderAction DrawCollapse = Definition.m_HeaderAction;
			Definition.m_HeaderAction = [this, Target = Card.m_Target, pLocateButton, ReadOnly, UiScale, SmallSize, DrawCollapse](const SSettingsCardFrame &Frame, bool Collapsed) {
				if(DrawCollapse)
					DrawCollapse(Frame, Collapsed);
				if(ReadOnly)
					return;
				// 定位按钮放在折叠按钮正左侧：与折叠按钮同高、顶部对齐，宽度按文本自适应。
				const float HandleSize = ui_token::settings::CARD_HANDLE_SIZE * UiScale;
				const float Gap = 4.0f * UiScale;
				const char *pLabel = Localize("Locate");
				const float TextWidth = TextRender()->TextWidth(SmallSize, pLabel);
				const float AvailableWidth = std::max(0.0f, Frame.m_HandleRect.x - Gap - Frame.m_HeaderRect.x);
				CUIRect LocateButton;
				LocateButton.w = std::min(TextWidth + 16.0f * UiScale, AvailableWidth);
				LocateButton.h = std::min(HandleSize, Frame.m_HandleRect.h > 0.0f ? Frame.m_HandleRect.h : HandleSize);
				LocateButton.x = Frame.m_HandleRect.x - Gap - LocateButton.w;
				LocateButton.y = Frame.m_HandleRect.y;
				if(LocateButton.w <= 0.0f || LocateButton.h <= 0.0f)
					return;
				if(Ui()->DoButtonLogic(pLocateButton, 0, &LocateButton, BUTTONFLAG_LEFT))
				{
					NavigateToSettingsCard(Target);
					Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
					m_GlobalCardSearchInput.Deactivate();
					return;
				}
				// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
				const bool Hovered = Ui()->MouseHovered(&LocateButton);
				const float Radius = std::min(ui_token::radius::TIGHT * UiScale, std::min(LocateButton.w, LocateButton.h) * 0.25f);
				const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, Hovered ? 0.28f : 0.18f);
				DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, Radius);
				Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
			};
			vCards.push_back(std::move(Definition));
		}
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	const SCardMotionSpec Motion = SettingsCardMotionSpec();

	// —— 搜索输入卡片：脱离卡组手动绘制 ——
	// 无搜索内容时在视口内垂直居中；输入内容后置顶并把下方空间让位给结果卡片，
	// 两种状态间的过渡沿用卡组让位动画的时长与缓动。
	static CButtonContainer s_GlobalSearchInputCollapseButton;
	static bool s_GlobalSearchInputCollapsed = false;
	const float InputContentHeight = s_GlobalSearchInputCollapsed ? 0.0f : 2.0f * LineHeight + LineSpacing;
	const SSettingsCardSpec InputSpec{"deck:global-search-input", Localize("Feature Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-input")};
	const float InputChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, Page.m_ContentViewport.w, 0.0f}, InputSpec, InputContentHeight, UiScale).m_Rect.h;
	const float CenteredOffsetY = std::max(0.0f, (Page.m_ContentViewport.h - InputChromeHeight) * 0.5f);
	const float TargetOffsetY = HasSearchQuery ? 0.0f : CenteredOffsetY;
	float InputOffsetY = TargetOffsetY;
	CUiV2AnimationRuntime *pSearchAnimRuntime = ReadOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	if(pSearchAnimRuntime != nullptr)
	{
		const uint64_t InputOffsetKey = BuildUiAnimNodeKey(str_quickhash("global-search"), str_quickhash("global-search-input-offset"));
		InputOffsetY = ResolveUiAnimValue(*pSearchAnimRuntime, InputOffsetKey, EUiAnimProperty::POS_Y, TargetOffsetY, Motion.m_ReflowDuration, EEasing::EASE_OUT);
	}
	{
		const CUIRect InputSlot{Page.m_ContentViewport.x, Page.m_ContentViewport.y + InputOffsetY, Page.m_ContentViewport.w, 0.0f};
		SSettingsCardVisualState InputVisualState;
		InputVisualState.m_Collapsed = s_GlobalSearchInputCollapsed;
		InputVisualState.m_ClipContent = true;
		const auto RenderInputContent = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, ReadOnly](CUIRect Content) {
			if(Content.h <= 0.0f || Content.w <= 0.0f)
				return;
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			IUiContext InputCtx = SettingsUiContext("settings_global_search", UiScale);
			if(ReadOnly)
			{
				InputCtx.m_pAnim = nullptr;
				InputCtx.m_pTree = nullptr;
			}
			ui_widget::InputField(InputCtx, &m_GlobalCardSearchInput, Row, BodySize, !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			char aSearchHint[64];
			str_format(aSearchHint, sizeof(aSearchHint), Localize("Found %d global cards"), SearchMatchedGlobalCardCount);
			TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
			Ui()->DoLabel(&Row, aSearchHint, SmallSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		};
		const auto InputHeaderAction = [this, ReadOnly, UiScale, SearchCtx](const SSettingsCardFrame &Frame, bool) mutable {
			if(ReadOnly)
				return;
			if(Ui()->DoButtonLogic(&s_GlobalSearchInputCollapseButton, 0, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
				s_GlobalSearchInputCollapsed = !s_GlobalSearchInputCollapsed;
			RenderSettingsCardCollapseButton(SearchCtx, Frame.m_HandleRect, s_GlobalSearchInputCollapsed);
		};
		SettingsCard(SearchCtx, InputSlot, InputSpec, InputVisualState, SettingsCardDeckVisualOptions(), [InputContentHeight](float) { return InputContentHeight; }, RenderInputContent, InputHeaderAction);
	}

	// 结果卡组从输入卡片底部（含动画中的位置）开始，让位动画期间跟随卡片一起上移。
	SSettingsPageLayoutFrame DeckPage = Page;
	{
		const float DeckTopDelta = InputOffsetY + InputChromeHeight + Page.m_CardGap;
		const auto ShiftRectDown = [DeckTopDelta](CUIRect &Rect) {
			Rect.y += DeckTopDelta;
			Rect.h = std::max(0.0f, Rect.h - DeckTopDelta);
		};
		ShiftRectDown(DeckPage.m_ScrollViewport);
		ShiftRectDown(DeckPage.m_UnreservedScrollViewport);
		ShiftRectDown(DeckPage.m_ContentViewport);
		ShiftRectDown(DeckPage.m_aColumns[0]);
		ShiftRectDown(DeckPage.m_aColumns[1]);
	}

	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = false;
	// 搜索结果按匹配顺序排列，不修改分类页的持久布局。
	InputState.m_AllowHeaderDrag = false;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	CardDeck.RenderCached(SearchCtx, DeckPage, "global-search", DefinitionsRevision, BuildDefinitions, DeckOrderModel, ReadOnly ? nullptr : &s_GlobalSearchScrollRegion, InputState, Motion, SettingsCardDeckVisualOptions());
}

void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool PrewarmOnly)
{
	using namespace qm_module;

	// feat-003 dogfood: when dbg_qm_ui_dogfood is on, take over the QmClient
	// settings panel and render the widget gallery. First visible verification
	// of feat-002 (animation runtime) + feat-003 (tokens + 11 widgets).
	if(g_Config.m_DbgQmUiDogfood != 0)
	{
		if(PrewarmOnly)
			return;

		IUiContext Ctx;
		Ctx.m_pUi = Ui();
		Ctx.m_pMenus = this;
		Ctx.m_pTextRender = TextRender();
		Ctx.m_pTooltips = &GameClient()->m_Tooltips;
		Ctx.m_pAnim = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
		Ctx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();
		Ctx.m_ScopeHash = MakeUiScopeHash("qm_ui_dogfood");
		Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		RenderQmUiDogfood(Ctx, MainView);
		return;
	}

	CPerfTimer RenderTimer;
	const float QmClientUiScale = ResolveSettingsContentMetrics(MainView.w).m_UiScale;
	bool TabTransitionActive = false;
	static bool s_QmTabTelemetryInitialized = false;
	static int s_PrevQmTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	CUIRect TabContentClip = MainView;

	{
		if(m_QmClientSettingsTab < 0 || m_QmClientSettingsTab >= NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
		// 贡献者子页签已并入顶层「贡献者」页；旧持久化索引占位值回落到首个子页签。
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONTRIBUTORS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;

		CUIRect TabBar;
		const SSettingsSubTabLayoutFrame QmClientSubTabs = ResolveSettingsSubTabLayout(MainView, QmClientUiScale);
		TabBar = QmClientSubTabs.m_TabBarRect;
		MainView = QmClientSubTabs.m_ContentRect;
		// 贡献者枚举保留占位以兼容旧配置，可见页签使用独立列表。
		constexpr int aVisibleQmTabs[] = {QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_CONFIG, QMCLIENT_SETTINGS_TAB_BIND};
		constexpr int NumQmTabs = (int)std::size(aVisibleQmTabs);
		const float TabWidth = TabBar.w / (float)NumQmTabs;
		static CButtonContainer s_aPageTabs[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize("Visuals");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize("Functions");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize("HUD");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize("Config");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_BIND] = Localize("Bind");

		{
			CPerfTimer StageTimer;
			// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签文字 —— 滑块压在文字之下。
			CUIRect aQmTabSlots[NumQmTabs];
			CUIRect QmTabsRemainder = TabBar;
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
				QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);
			int ActiveQmTabSlot = -1;
			for(int Slot = 0; Slot < NumQmTabs; ++Slot)
			{
				if(aVisibleQmTabs[Slot] == m_QmClientSettingsTab)
					ActiveQmTabSlot = Slot;
			}
			const IUiContext QmTabBarCtx = TabBarUiContext();
			ui_widget::CapsuleTabBarChrome(QmTabBarCtx, MakeUiScopeHash("settings_qmclient_tabs_capsule"), ui_widget::CapsuleTabBarRowRect(aQmTabSlots, NumQmTabs), ActiveQmTabSlot >= 0 ? &aQmTabSlots[ActiveQmTabSlot] : nullptr, SettingsCapsuleTabBarStyle());
			for(int Tab = 0; Tab < NumQmTabs; ++Tab)
			{
				const int PageTab = aVisibleQmTabs[Tab];
				const bool ClickedTab = DoButton_MenuTab(&s_aPageTabs[PageTab], apQmTabNames[PageTab], m_QmClientSettingsTab == PageTab, &aQmTabSlots[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
				if(!PrewarmOnly && ClickedTab)
					m_QmClientSettingsTab = PageTab;
			}

			char aTabExtra[96];
			str_format(aTabExtra, sizeof(aTabExtra), "tab=%s", QmSettingsTabName(m_QmClientSettingsTab));
			LogQmPerfStage(Client(), "tabbar", StageTimer.ElapsedMs(), false, aTabExtra);
		}

		if(!PrewarmOnly)
		{
			if(!s_QmTabTelemetryInitialized)
			{
				s_PrevQmTab = m_QmClientSettingsTab;
				s_QmTabTelemetryInitialized = true;
			}
			else if(m_QmClientSettingsTab != s_PrevQmTab)
			{
				if(PerfDebugEnabled())
				{
					char aPayload[128];
					str_format(aPayload, sizeof(aPayload), "event=tab_switch from=%s to=%s", QmSettingsTabName(s_PrevQmTab), QmSettingsTabName(m_QmClientSettingsTab));
					QmPerfLogPayload("perf/qmclient", aPayload, Client(), CurrentQmUiPerfPage());
				}
				s_PrevQmTab = m_QmClientSettingsTab;
			}
		}

		CUIRect ContentView = MainView;
		// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面局部状态叠加。
		TabTransitionActive = false;
		TabContentClip = MainView;
		if(TabTransitionActive)
		{
			Ui()->ClipEnable(&TabContentClip);
		}

		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONFIG)
		{
			CPerfTimer StageTimer;
			if(!PrewarmOnly)
				RenderSettingsTClientConfigs(ContentView);
			char aConfigExtra[96];
			str_format(aConfigExtra, sizeof(aConfigExtra), "tab=%s transition=%d", QmSettingsTabName(m_QmClientSettingsTab), TabTransitionActive ? 1 : 0);
			LogQmPerfStage(Client(), "config_tab_total", StageTimer.ElapsedMs(), TabTransitionActive, aConfigExtra);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			LogQmPerfStage(Client(), "render_total", RenderTimer.ElapsedMs(), false, aConfigExtra);
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_BIND)
		{
			RenderSettingsQmClientBindDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_VISUAL)
		{
			RenderSettingsQmClientVisualDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_FUNCTION)
		{
			RenderSettingsQmClientFunctionDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_HUD)
		{
			RenderSettingsQmClientHudDeck(ContentView, PrewarmOnly);
			if(TabTransitionActive)
				Ui()->ClipDisable();
			return;
		}
		MainView = ContentView;
	}
}

std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
bool g_CommandBindCacheInitialized = false;

void CMenus::ClearQmClientSettingsSearchInputs()
{
	if(Ui()->ActiveItem() == &m_GlobalCardSearchInput)
	{
		Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
	}
	else
	{
		m_GlobalCardSearchInput.Deactivate();
	}
	m_GlobalCardSearchInput.Clear();
}

void CMenus::RenderSponsorNudge(CUIRect Screen)
{
	CGameClient *pGameClient = GameClient();
	// 两种触发共用同一套灵动岛表现：启动提醒，以及关掉提醒后的那句问话。
	const bool Visible = pGameClient->SponsorNudgeVisible() || pGameClient->SponsorNudgeFarewellActive();
	// 出场动画收完之前必须继续画，否则「收缩成黑球再上滑」会退化成瞬间消失。
	if(!qm_island::NeedsRender(m_QmSponsorNudgeNotice, Visible))
	{
		qm_island::Reset(m_QmSponsorNudgeNotice);
		return;
	}
	const float DeltaSeconds = GameClient()->UiRuntimeV2()->FrameDt();
	const float UiScale = g_Config.m_QmUiScale / 100.0f;
	// 顶部居中；主体高度取 HUD 动态岛同一档设计高度的量级，保证「一眼是灵动岛」。
	// 倒计时环整条都在主体外轮廓外侧，所以外边距还要留出环的宽度。
	const float BodyH = 26.0f * UiScale;
	const qm_island::SNoticeLayout Layout = qm_island::ResolveLayout(
		Screen, 300.0f * UiScale, BodyH, 10.0f * UiScale, 2.0f * UiScale, 1.5f * UiScale);
	// 入场「掉落 → 展开」与出场「收缩 → 上滑」共用同两条弹簧，先后顺序由状态机门控。
	CUiV2AnimationRuntime &Anim = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t NodeBase = MakeUiScopeHash("menu_sponsor_nudge_island");
	const SHudMediaIslandEntranceSpringResult Entrance = qm_island::ResolveSprings(
		Anim, NodeBase ^ 0x11u, NodeBase ^ 0x22u, m_QmSponsorNudgeNotice, Visible);
	m_QmSponsorNudgeNotice.m_DropProgress = Entrance.m_DropProgress;
	m_QmSponsorNudgeNotice.m_ExpandProgress = Entrance.m_ExpandProgress;
	// 倒计时只在完整展开后开始，否则形变阶段就把时间吃掉一截；到时收起，转入出场动画。
	if(qm_island::AdvanceCountdown(m_QmSponsorNudgeNotice, Visible, DeltaSeconds))
		pGameClient->DismissSponsorNudge(false);
	const float Remaining = qm_island::RemainingFraction(m_QmSponsorNudgeNotice);
	const SHudMediaIslandEntrancePose Pose = QmHudMediaIslandEntrancePose(
		Layout.m_Body, Layout.m_Body.h * 0.5f, ui_token::color::SURFACE_ELEVATED, Entrance.m_ExpandProgress, Entrance.m_DropProgress, Screen.y);
	// 组 SDF 状态：主体是胶囊；倒计时环贴主体外轮廓绕一圈（完全展开后随 ContentAlpha 淡入）。
	// 不画外圈阴影：整块岛与黑球都只留本体轮廓。
	SHudMediaIslandSdfRenderState SdfState;
	SdfState.m_MainRect = Pose.m_Rect;
	SdfState.m_MainRadius = Pose.m_Rect.h * 0.5f;
	SdfState.m_MainCorners = IGraphics::CORNER_ALL;
	SdfState.m_BackgroundColor = Pose.m_BackgroundColor;
	SdfState.m_ScreenPixelSize = Ui()->PixelSize();
	SdfState.m_OutlineRingThickness = Layout.m_RingThickness;
	SdfState.m_OutlineRingOffset = Layout.m_RingOffset;
	SdfState.m_ItemCount = 1;
	SdfState.m_Items[0].m_Center = vec2(Pose.m_Rect.x + Pose.m_Rect.w * 0.5f, Pose.m_Rect.y + Pose.m_Rect.h * 0.5f);
	SdfState.m_Items[0].m_Radii = vec2(Pose.m_Rect.w * 0.5f, Pose.m_Rect.h * 0.5f);
	SdfState.m_Items[0].m_ContentAlpha = Pose.m_ContentAlpha;
	// 进度取「剩余」：弧线随时间消耗，时间到走满一圈。
	SdfState.m_Items[0].m_CountdownProgress = Remaining;
	SdfState.m_Items[0].m_RingColor = qm_island::CountdownRingColor(Remaining);
	SdfState.m_Rect = QmHudMediaIslandSdfOuterRect(SdfState);
	qm_island::Render(Graphics(), SdfState);
	if(Ui()->RenderOnly())
		return;
	char aText[256];
	if(pGameClient->SponsorNudgeFarewellActive())
		str_copy(aText, Localize("Really? Not even a little?"), sizeof(aText));
	else if(!pGameClient->m_QmClient.QmSponsorNames().empty())
		str_format(aText, sizeof(aText), Localize("Still free after %d sponsors. Take a look?"), (int)pGameClient->m_QmClient.QmSponsorNames().size());
	else
		str_copy(aText, Localize("Thanks for supporting QmClient"), sizeof(aText));
	// 内容淡入必须跟着入场 pose 的 ContentAlpha，否则形变期间文字会先于外形出现。
	const ColorRGBA TextColor = ui_token::color::TEXT_PRIMARY.WithMultipliedAlpha(Pose.m_ContentAlpha);
	CUIRect TextRect = Pose.m_Rect;
	TextRect.Margin(10.0f * UiScale, &TextRect);
	TextRender()->TextColor(TextColor);
	Ui()->DoLabel(&TextRect, aText, ui_token::font::BODY * UiScale, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

namespace
{
	// 「新功能」弹窗条目：名称 / 说明 / 用法 / 入口。
	// 入口按「设置 → QmClient → 页签 → 卡片」拼装，全部复用已有译文；
	// 带 tab 与 stableId 时额外显示跳转按钮，直接跳到设置页对应卡片。
	struct SQmNewFeatureEntry
	{
		const char *m_pName;
		const char *m_pSummary;
		const char *m_pUsage;
		const char *m_pSection;
		const char *m_pCardTab;
		const char *m_pCardStableId;
	};

	// 表内字符串用 Localizable 标记为 i18n 源 key，绘制时再走 Localize。
	const SQmNewFeatureEntry g_aQmNewFeatures[] = {
		{
			Localizable("Sponsor title"),
			Localizable("Redeem your sponsor code to unlock a custom title, shown in chat and on your nameplate."),
			Localizable("Enter the sponsor code, press Redeem, then write the title and press Save. Tick \"Only show with this nickname\" to bind it to one nickname."),
			Localizable("Contributors"),
			"qmclient-contributors",
			"deck:qmclient-contributors-title",
		},
		{
			Localizable("DDRace HUD Pro"),
			Localizable("Adds dummy key, hammer, control and copy status to the HUD, plus your own bind status list."),
			Localizable("Switch on the status rows you need, then list the binds you want to watch."),
			Localizable("HUD"),
			"hud",
			"qm:bind_status_hud",
		},
		{
			Localizable("Lyrics"),
			Localizable("Shows lyrics from NetEase Cloud Music, Soda Music, Kugou and QQ Music on the dynamic island."),
			Localizable("Pick the source that matches your music app and keep it playing; the lyrics follow automatically."),
			Localizable("HUD"),
			"hud",
			// 歌词没有独立设置卡，开关在灵动岛卡内；指向不存在的卡会让跳转落空。
			"qm:dynamic_island",
		},
		{
			Localizable("Weapon animation"),
			Localizable("Your weapon slides and rotates in when you switch, and flips while reloading."),
			Localizable("Enable weapon switch animation or reload animation, then tune range, duration, rotation and easing."),
			Localizable("Visuals"),
			"visual",
			"qm:weapon_animation",
		},
		{
			Localizable("Skin transition"),
			Localizable("Plays an animation whenever your skin changes, including skins stolen with the hammer."),
			Localizable("Enable skin transition animation, then choose the type, scope, duration and easing."),
			Localizable("Visuals"),
			"visual",
			"qm:skin_transition",
		},
		{
			Localizable("Message merging"),
			Localizable("Merges chat messages repeated within a short time into a single line."),
			Localizable("Turn on \"Message merging\" in Dream Features."),
			Localizable("Functions"),
			"function",
			"qm:mini_features",
		},
	};

	// 文本按 m_MaxWidth 换行后的行数，用于给弹窗条目预留高度。
	int QmWrappedLineCount(ITextRender *pTextRender, float FontSize, const char *pText, float MaxWidth)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
			return 1;
		int LineCount = 0;
		STextSizeProperties TextSizeProps{};
		TextSizeProps.m_pLineCount = &LineCount;
		pTextRender->TextWidth(FontSize, pText, -1, MaxWidth, 0, TextSizeProps);
		return maximum(1, LineCount);
	}
}

void CMenus::RenderQmNewFeaturesPopup(CUIRect Screen)
{
	const IUiContext Ctx = SettingsUiContext("qm_new_features_popup");
	const float UiScale = std::clamp(g_Config.m_QmUiScale / 100.0f, 0.5f, 2.0f);
	const float TitleSize = ui_token::font::TITLE * UiScale;
	const float HeadlineSize = ui_token::font::HEADLINE * UiScale;
	const float BodySize = ui_token::font::BODY * UiScale;
	const float TipSize = ui_token::font::TIP * UiScale;
	const float Padding = ui_token::spacing::LG * UiScale;
	const float Gap = ui_token::spacing::MD * UiScale;
	const float ButtonH = 26.0f * UiScale;
	const float JumpButtonW = 110.0f * UiScale;

	// 遮罩：弹窗期间抢占启动菜单的绘制，先压暗整屏再画面板。
	Screen.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_NONE, 0.0f);

	CUIRect Panel;
	// 小窗口下先夹住上限，避免 std::clamp 出现 lo > hi。
	const float MaxPanelW = std::max(160.0f, Screen.w - 2.0f * ui_token::spacing::LG);
	const float MaxPanelH = std::max(200.0f, Screen.h - 2.0f * ui_token::spacing::LG);
	Panel.w = std::clamp(820.0f * UiScale, 160.0f, MaxPanelW);
	Panel.h = std::clamp(Screen.h - 4.0f * ui_token::spacing::LG, 200.0f, MaxPanelH);
	Panel.x = Screen.x + (Screen.w - Panel.w) * 0.5f;
	Panel.y = Screen.y + (Screen.h - Panel.h) * 0.5f;

	CUIRect ShadowRect = Panel;
	ShadowRect.x += ui_token::elevation::SHADOW_X_HIGH;
	ShadowRect.y += ui_token::elevation::SHADOW_Y_HIGH;
	DrawRoundedSurface(Ctx, ShadowRect, ui_token::color::SURFACE_SHADOW, ColorRGBA(), ui_token::radius::CARD);
	DrawRoundedSurface(Ctx, Panel, ui_token::color::SURFACE_ELEVATED, ui_token::color::BORDER_SUBTLE, ui_token::radius::CARD);

	CUIRect Inner;
	Panel.Margin(Padding, &Inner);

	const char *pTitle = Localize("New features");
	char aVersion[64];
	str_format(aVersion, sizeof(aVersion), "%s %s", CLIENT_NAME, CLIENT_RELEASE_VERSION);

	CUIRect TitleRow, SubTitleRow;
	Inner.HSplitTop(TitleSize * 1.3f, &TitleRow, &Inner);
	Ui()->DoLabel(&TitleRow, pTitle, TitleSize, TEXTALIGN_ML);
	Inner.HSplitTop(TipSize * 1.6f, &SubTitleRow, &Inner);
	TextRender()->TextColor(ui_token::color::TEXT_TIP);
	Ui()->DoLabel(&SubTitleRow, aVersion, TipSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Inner.HSplitTop(Gap, nullptr, &Inner);

	CQmClient &QmClient = GameClient()->m_QmClient;
	if(m_QmNewFeaturesScrollReset && QmClient.HasDeveloperCredential())
		QmClient.QmNewsReloadDraft();
	// 底部：关闭按钮及仅限开发者凭据的公告草稿操作。
	CUIRect ButtonRow, CloseButtonRect;
	Inner.HSplitBottom(ButtonH, &Inner, &ButtonRow);
	Inner.HSplitBottom(Gap, &Inner, nullptr);
	if(QmClient.HasDeveloperCredential())
	{
		CUIRect DevRow, ReloadDraftRect, PublishRect, OpenFolderRect, DevStatus;
		Inner.HSplitBottom(ButtonH, &Inner, &DevRow);
		Inner.HSplitBottom(Gap, &Inner, nullptr);
		const float DevButtonW = std::min(DevRow.w * 0.28f, 150.0f * UiScale);
		DevRow.VSplitLeft(DevButtonW, &ReloadDraftRect, &DevRow);
		DevRow.VSplitLeft(Gap, nullptr, &DevRow);
		DevRow.VSplitLeft(DevButtonW, &PublishRect, &DevRow);
		DevRow.VSplitLeft(Gap, nullptr, &DevRow);
		DevRow.VSplitLeft(DevButtonW, &OpenFolderRect, &DevStatus);
		DevStatus.VSplitLeft(Gap, nullptr, &DevStatus);

		static CButtonContainer s_ReloadDraftButton, s_PublishButton, s_OpenFolderButton;
		if(ui_widget::SecondaryButton(Ctx, &s_ReloadDraftButton, Localize("Reload"), ReloadDraftRect, QmClient.QmNewsPublishing()))
			QmClient.QmNewsReloadDraft();
		if(ui_widget::PrimaryButton(Ctx, &s_PublishButton, Localize("Publish"), PublishRect, QmClient.QmNewsPublishing() || QmClient.QmNewsDraft()[0] == '\0'))
			QmClient.QmNewsPublishDraft();
		if(ui_widget::SecondaryButton(Ctx, &s_OpenFolderButton, Localize("Open folder"), OpenFolderRect))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient", aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
		}

		const char *pDevStatus = nullptr;
		switch(QmClient.QmNewsStatus())
		{
		case CQmClient::ENewsStatus::PUBLISHING: pDevStatus = Localize("Publishing…"); break;
		case CQmClient::ENewsStatus::PUBLISH_DENIED:
		case CQmClient::ENewsStatus::PUBLISH_TOO_LARGE:
		case CQmClient::ENewsStatus::PUBLISH_FAILED: pDevStatus = Localize("Publish failed"); break;
		case CQmClient::ENewsStatus::PUBLISHED: pDevStatus = Localize("Published"); break;
		default:
			if(QmClient.QmNewsDraft()[0] == '\0')
				pDevStatus = Localize("Draft file is empty");
			break;
		}
		if(pDevStatus != nullptr)
		{
			TextRender()->TextColor(ui_token::color::TEXT_TIP);
			Ui()->DoLabel(&DevStatus, pDevStatus, TipSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
	CloseButtonRect = ButtonRow;
	CloseButtonRect.w = std::min(ButtonRow.w, 200.0f * UiScale);
	CloseButtonRect.x = ButtonRow.x + (ButtonRow.w - CloseButtonRect.w) * 0.5f;
	static CButtonContainer s_CloseButton;
	if(ui_widget::PrimaryButton(Ctx, &s_CloseButton, Localize("Close"), CloseButtonRect) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_Popup = POPUP_NONE;
		return;
	}

	// 条目区：内容超出时滚动；每次打开回到顶部。
	static CScrollRegion s_ScrollRegion;
	if(m_QmNewFeaturesScrollReset)
	{
		s_ScrollRegion.Reset();
		m_QmNewFeaturesScrollReset = false;
	}
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3.0f * BodySize;
	vec2 ScrollOffset;
	CUIRect ScrollArea = Inner;
	s_ScrollRegion.Begin(&ScrollArea, &ScrollOffset, &ScrollParams);

	CUIRect Content = ScrollArea;
	Content.x += ScrollOffset.x;
	Content.y += ScrollOffset.y;

	// 中心服广播有内容时优先显示受限 Markdown；没有广播时保留本地静态新功能列表。
	if(QmClient.HasQmMarkdownBroadcast())
	{
		static std::vector<qm_md::SBlock> s_vBroadcastBlocks;
		static int s_BroadcastRevision = -1;
		if(s_BroadcastRevision != QmClient.QmMarkdownBroadcastRevision())
		{
			s_vBroadcastBlocks = qm_md::Parse(QmClient.QmMarkdownBroadcast());
			s_BroadcastRevision = QmClient.QmMarkdownBroadcastRevision();
		}
		static CButtonContainer s_aBroadcastButtons[16] = {};
		static CButtonContainer s_aBroadcastLinkButtons[16] = {};
		int BroadcastButtonIndex = 0;
		int BroadcastLinkButtonIndex = 0;
		for(const qm_md::SBlock &Block : s_vBroadcastBlocks)
		{
			if(Block.m_Kind == qm_md::EBlockKind::SETTINGS_BUTTON)
			{
				const qm_card_registry::SCardDefault *pCard = qm_card_registry::FindByStableId(Block.m_SettingsCardId.c_str());
				if(pCard == nullptr || pCard->m_pDefaultTab == nullptr)
					continue;
				CUIRect ButtonRect;
				Content.HSplitTop(ButtonH + Gap * 0.5f, &ButtonRect, &Content);
				if(s_ScrollRegion.AddRect(ButtonRect) && BroadcastButtonIndex < (int)std::size(s_aBroadcastButtons))
				{
					ButtonRect.w = std::min(ButtonRect.w, 240.0f * UiScale);
					const char *pLabel = Block.m_SettingsLabel.empty() ? Localize("Open settings") : Block.m_SettingsLabel.c_str();
					if(ui_widget::SecondaryButton(Ctx, &s_aBroadcastButtons[BroadcastButtonIndex], pLabel, ButtonRect))
					{
						qm_card_registry::SCardNavigationTarget Target;
						Target.m_pTab = pCard->m_pDefaultTab;
						Target.m_pStableId = pCard->m_pStableId;
						NavigateToSettingsCard(Target);
						m_Popup = POPUP_NONE;
						SetShowStart(false);
						SetMenuPage(PAGE_SETTINGS);
						s_ScrollRegion.End();
						return;
					}
				}
				++BroadcastButtonIndex;
				continue;
			}
			if(Block.m_Kind == qm_md::EBlockKind::SEPARATOR)
			{
				CUIRect Separator;
				Content.HSplitTop(1.0f + Gap * 0.5f, &Separator, &Content);
				if(s_ScrollRegion.AddRect(Separator))
				{
					Separator.h = 1.0f;
					DrawRoundedSurface(Ctx, Separator, ui_token::color::BORDER_SUBTLE, ColorRGBA(), 0.0f);
				}
				continue;
			}
			std::string Text;
			for(const qm_md::SSpan &Span : Block.m_vSpans)
				Text += Span.m_Text;
			if(Text.empty())
				continue;
			float FontSize = BodySize;
			if(Block.m_Kind == qm_md::EBlockKind::HEADING1)
				FontSize = HeadlineSize * 1.25f;
			else if(Block.m_Kind == qm_md::EBlockKind::HEADING2)
				FontSize = HeadlineSize * 1.1f;
			else if(Block.m_Kind == qm_md::EBlockKind::HEADING3)
				FontSize = HeadlineSize;
			const float Indent = Block.m_Kind == qm_md::EBlockKind::BULLET || Block.m_Kind == qm_md::EBlockKind::NUMBERED ? 18.0f * UiScale : 0.0f;
			const int LineCount = QmWrappedLineCount(TextRender(), FontSize, Text.c_str(), std::max(80.0f, Content.w - Indent));
			CUIRect TextRect;
			Content.HSplitTop(LineCount * FontSize * 1.55f + Gap * 0.5f, &TextRect, &Content);
			if(!s_ScrollRegion.AddRect(TextRect))
				continue;
			if(Indent > 0.0f)
			{
				char aMarker[16];
				if(Block.m_Kind == qm_md::EBlockKind::BULLET)
					str_copy(aMarker, "•");
				else
					str_format(aMarker, sizeof(aMarker), "%d.", Block.m_Number);
				CUIRect Marker;
				TextRect.VSplitLeft(Indent, &Marker, &TextRect);
				TextRender()->TextColor(ui_token::color::TEXT_TIP);
				Ui()->DoLabel(&Marker, aMarker, BodySize, TEXTALIGN_TL);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			if(Block.m_Kind == qm_md::EBlockKind::QUOTE)
			{
				CUIRect Bar = TextRect;
				Bar.w = 3.0f;
				DrawRoundedSurface(Ctx, Bar, ui_token::color::ACCENT_PRIMARY_DIM, ColorRGBA(), 0.0f);
				TextRect.VSplitLeft(10.0f * UiScale, nullptr, &TextRect);
			}
			TextRender()->TextColor(Block.m_Kind == qm_md::EBlockKind::HEADING1 || Block.m_Kind == qm_md::EBlockKind::HEADING2 || Block.m_Kind == qm_md::EBlockKind::HEADING3 ? ui_token::color::TEXT_PRIMARY : ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&TextRect, Text.c_str(), FontSize, TEXTALIGN_TL, {.m_MaxWidth = TextRect.w});
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			for(const qm_md::SSpan &Span : Block.m_vSpans)
			{
				if(Span.m_Link.empty() || BroadcastLinkButtonIndex >= (int)std::size(s_aBroadcastLinkButtons))
					continue;
				CUIRect LinkRect;
				Content.HSplitTop(ButtonH + Gap * 0.5f, &LinkRect, &Content);
				if(s_ScrollRegion.AddRect(LinkRect))
				{
					LinkRect.w = std::min(LinkRect.w, 240.0f * UiScale);
					if(ui_widget::SecondaryButton(Ctx, &s_aBroadcastLinkButtons[BroadcastLinkButtonIndex], Span.m_Text.c_str(), LinkRect))
						Client()->ViewLink(Span.m_Link.c_str());
				}
				++BroadcastLinkButtonIndex;
			}
		}
		s_ScrollRegion.End();
		return;
	}
	const float TextWidth = std::max(80.0f, Content.w - JumpButtonW - Gap - ui_token::spacing::MD * UiScale);

	static CButtonContainer s_aJumpButtons[std::size(g_aQmNewFeatures)] = {};
	for(size_t Index = 0; Index < std::size(g_aQmNewFeatures); ++Index)
	{
		const SQmNewFeatureEntry &Entry = g_aQmNewFeatures[Index];
		char aEntryPath[192];
		str_format(aEntryPath, sizeof(aEntryPath), "%s → QmClient → %s → %s", Localize("Settings"), Localize(Entry.m_pSection), Localize(Entry.m_pName));
		char aEntryLine[256];
		str_format(aEntryLine, sizeof(aEntryLine), Localize("Entry: %s"), aEntryPath);
		const int SummaryLines = QmWrappedLineCount(TextRender(), BodySize, Localize(Entry.m_pSummary), TextWidth);
		const int UsageLines = QmWrappedLineCount(TextRender(), TipSize, Localize(Entry.m_pUsage), TextWidth);
		const int EntryLines = QmWrappedLineCount(TextRender(), TipSize, aEntryLine, TextWidth);
		const float EntryHeight = HeadlineSize * 1.3f + ui_token::spacing::XS * UiScale +
					  SummaryLines * BodySize * 1.5f + (UsageLines + EntryLines) * TipSize * 1.6f +
					  Gap;

		CUIRect EntryRect;
		Content.HSplitTop(EntryHeight, &EntryRect, &Content);
		if(!s_ScrollRegion.AddRect(EntryRect))
			continue;

		CUIRect EntryContent = EntryRect;
		CUIRect JumpRow;
		EntryContent.HSplitTop(HeadlineSize * 1.3f, &JumpRow, &EntryContent);
		if(Index > 0)
		{
			CUIRect Separator;
			Separator.x = EntryRect.x;
			Separator.w = EntryRect.w;
			Separator.h = 1.0f;
			Separator.y = EntryRect.y - Gap * 0.5f;
			DrawRoundedSurface(Ctx, Separator, ui_token::color::BORDER_SUBTLE, ColorRGBA(), 0.0f);
		}

		CUIRect TitleRect = JumpRow;
		TextRender()->TextColor(ui_token::color::TEXT_PRIMARY);
		Ui()->DoLabel(&TitleRect, Localize(Entry.m_pName), HeadlineSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(Entry.m_pCardTab != nullptr && Entry.m_pCardStableId != nullptr)
		{
			CUIRect JumpButtonRect = JumpRow;
			JumpButtonRect.VSplitRight(JumpButtonW, nullptr, &JumpButtonRect);
			JumpButtonRect.h = minimum(JumpButtonRect.h, ButtonH);
			if(ui_widget::SecondaryButton(Ctx, &s_aJumpButtons[Index], Localize("Open settings"), JumpButtonRect))
			{
				qm_card_registry::SCardNavigationTarget Target;
				Target.m_pTab = Entry.m_pCardTab;
				Target.m_pStableId = Entry.m_pCardStableId;
				NavigateToSettingsCard(Target);
				m_Popup = POPUP_NONE;
				SetShowStart(false);
				SetMenuPage(PAGE_SETTINGS);
				s_ScrollRegion.End();
				return;
			}
		}

		CUIRect SummaryRow, UsageRow, EntryRow;
		EntryContent.HSplitTop(SummaryLines * BodySize * 1.5f, &SummaryRow, &EntryContent);
		TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
		Ui()->DoLabel(&SummaryRow, Localize(Entry.m_pSummary), BodySize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		EntryContent.HSplitTop(UsageLines * TipSize * 1.6f, &UsageRow, &EntryContent);
		EntryContent.HSplitTop(EntryLines * TipSize * 1.6f, &EntryRow, &EntryContent);
		TextRender()->TextColor(ui_token::color::TEXT_TIP);
		char aUsage[512];
		str_format(aUsage, sizeof(aUsage), Localize("Usage: %s"), Localize(Entry.m_pUsage));
		Ui()->DoLabel(&UsageRow, aUsage, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		Ui()->DoLabel(&EntryRow, aEntryLine, TipSize, TEXTALIGN_TL, {.m_MaxWidth = TextWidth});
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_ScrollRegion.End();
}

bool CMenus::RenderQmFunctionCheckboxRow(CUIRect &Content, const float LineHeight, const float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue, const bool PrewarmOnly, const char *pTooltip)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const bool Changed = RenderQmFunctionCheckbox(pId, pTextId, pText, pValue, &Row, PrewarmOnly, pTooltip);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	return Changed;
}

void CMenus::RenderQmHudGoresDrownBoardContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	// 行序与卡片目录的预布局输入一致：总开关、显示人数、不透明度、显示玩家 Tee。
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoard, "Show Gores drown board", Localize("Show Gores drown board"), &g_Config.m_QmGoresDrownBoard);
	if(!g_Config.m_QmGoresDrownBoard)
		return;

	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmHudLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmGoresDrownBoardMaxPlayersInputId;
	static int s_QmGoresDrownBoardOpacityInputId;
	RenderValue("qmclient-gores-drown-board-max-players", "Players shown", &s_QmGoresDrownBoardMaxPlayersInputId, &g_Config.m_QmGoresDrownBoardMaxPlayers, 1, 16);
	RenderValue("qmclient-gores-drown-board-opacity", "Card opacity", &s_QmGoresDrownBoardOpacityInputId, &g_Config.m_QmGoresDrownBoardOpacity, 0, 100, "%");
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmGoresDrownBoardShowTee, "Show player Tee", Localize("Show player Tee"), &g_Config.m_QmGoresDrownBoardShowTee);
}
