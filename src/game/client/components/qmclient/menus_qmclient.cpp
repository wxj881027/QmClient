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
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/QmModuleTypes.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiDogfood.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/input_overlay.h>
#include <game/client/components/qmclient/keyword_reply_rules.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

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
	TCLIENT_TAB_INFO,
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
	std::function<bool()> g_QmClientEnsureSponsorQrTexture;
	std::function<void(const CUIRect &, float)> g_QmClientRenderTexture;

	// Visual Deck 需要完整的模块表，才能在切换单张卡片的折叠状态时保留其他 tab 的历史配置。
	const std::array<qm_module::SQmModuleEntry, qm_module::QmModuleCount> s_aQmModuleDefaults = {{{qm_module::EQmModuleId::Info, qm_module::EQmModuleColumn::Full, 0, "info"},
		{qm_module::EQmModuleId::ChatBubble, qm_module::EQmModuleColumn::Left, 0, "chat_bubble"},
		{qm_module::EQmModuleId::SkinTransition, qm_module::EQmModuleColumn::Left, 1, "skin_transition"},
		{qm_module::EQmModuleId::FocusMode, qm_module::EQmModuleColumn::Left, 2, "focus_mode"},
		{qm_module::EQmModuleId::GoresActor, qm_module::EQmModuleColumn::Left, 3, "gores_actor"},
		{qm_module::EQmModuleId::Gores, qm_module::EQmModuleColumn::Left, 4, "gores"},
		{qm_module::EQmModuleId::KeyBinds, qm_module::EQmModuleColumn::Left, 5, "key_binds"},
		{qm_module::EQmModuleId::MiniFeatures, qm_module::EQmModuleColumn::Left, 6, "mini_features"},
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
		{qm_module::EQmModuleId::SpeedrunTimer, qm_module::EQmModuleColumn::Right, 8, "speedrun_timer"},
		{qm_module::EQmModuleId::DebugGraph, qm_module::EQmModuleColumn::Right, 9, "debug_graph"},
		{qm_module::EQmModuleId::InputOverlay, qm_module::EQmModuleColumn::Right, 10, "input_overlay"},
		{qm_module::EQmModuleId::HudNotifications, qm_module::EQmModuleColumn::Right, 11, "hud_notifications"},
		{qm_module::EQmModuleId::Voice, qm_module::EQmModuleColumn::Right, 12, "voice"},
		{qm_module::EQmModuleId::DummyMiniView, qm_module::EQmModuleColumn::Right, 13, "dummy_miniview"},
		{qm_module::EQmModuleId::DynamicIsland, qm_module::EQmModuleColumn::Right, 14, "dynamic_island"},
		{qm_module::EQmModuleId::SystemMediaControls, qm_module::EQmModuleColumn::Right, 15, "system_media_controls"},
		{qm_module::EQmModuleId::Lyrics, qm_module::EQmModuleColumn::Right, 16, "lyrics"},
		{qm_module::EQmModuleId::Background3D, qm_module::EQmModuleColumn::Right, 17, "background_3d"}}};
}

using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;

struct SQmGlobalSearchNavigation
{
	int m_SettingsPage = CMenus::SETTINGS_QMCLIENT;
	int m_QmClientTab = -1;
	int m_TClientTab = -1;
	int m_AppearanceTab = -1;
};

struct SQmGlobalSearchTabRoute
{
	const char *m_pTab;
	int m_SettingsPage;
	int m_TClientTab = -1;
	int m_AppearanceTab = -1;
	int m_QmClientTab = -1;
};

static constexpr SQmGlobalSearchTabRoute s_aGlobalSearchTabRoutes[] = {
	{"general", CMenus::SETTINGS_GENERAL},
	{"player", CMenus::SETTINGS_PLAYER},
	{"tee", CMenus::SETTINGS_TEE},
	{"tee7", CMenus::SETTINGS_TEE},
	{"graphics", CMenus::SETTINGS_GRAPHICS},
	{"sound", CMenus::SETTINGS_SOUND},
	{"ddnet", CMenus::SETTINGS_DDNET},
	{"controls", CMenus::SETTINGS_CONTROLS},
	{"tclient-bind-wheel", CMenus::SETTINGS_TCLIENT, 1},
	{"tclient-warlist", CMenus::SETTINGS_TCLIENT, 2},
	{"tclient-chat-binds", CMenus::SETTINGS_TCLIENT, 3},
	{"tclient-status-bar", CMenus::SETTINGS_TCLIENT, 4},
	{"tclient-info", CMenus::SETTINGS_TCLIENT, 5},
	{"tclient-profiles", CMenus::SETTINGS_PROFILES},
	{"tclient-configs", CMenus::SETTINGS_QMCLIENT, -1, -1, CMenus::QMCLIENT_SETTINGS_TAB_CONFIG},
	{"appearance-hud", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_HUD},
	{"appearance-chat", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_CHAT},
	{"appearance-name-plate", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_NAME_PLATE},
	{"appearance-hook-collision", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_HOOK_COLLISION},
	{"appearance-info-messages", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_INFO_MESSAGES},
	{"appearance-laser", CMenus::SETTINGS_APPEARANCE, -1, CMenus::APPEARANCE_TAB_LASER},
};

struct SQmGlobalSearchResults
{
	std::vector<SQmGlobalSearchCard> m_vAllVisibleCards;
};

namespace
{
	void CollectGlobalSearchResults(const char *pSearch, bool Sixup, const qm_card_order::CModel &Model, SQmGlobalSearchResults &Out)
	{
		Out.m_vAllVisibleCards.clear();
		std::vector<SQmGlobalSearchCard> vCards;
		if(pSearch != nullptr && pSearch[0] != '\0')
			vCards = qm_card_registry::SearchCards(pSearch, Model);
		else
		{
			vCards.reserve(qm_card_registry::Defaults().size());
			for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
			{
				SQmGlobalSearchCard Card;
				Card.m_pStableId = Default.m_pStableId;
				Card.m_Title = Default.m_pTitle != nullptr ? Localize(Default.m_pTitle) : "";
				Card.m_Description = Default.m_pDescription != nullptr ? Localize(Default.m_pDescription) : "";
				Card.m_Target = qm_card_registry::ResolveCardNavigationTarget(Default, Model);
				vCards.push_back(std::move(Card));
			}
		}
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

	SQmGlobalSearchNavigation ResolveGlobalSearchNavigation(const SQmGlobalSearchCard &Card)
	{
		SQmGlobalSearchNavigation Navigation;
		const char *pStableId = Card.m_pStableId != nullptr ? Card.m_pStableId : "";
		const char *pTab = Card.m_Target.m_pTab != nullptr ? Card.m_Target.m_pTab : "";
		if(str_startswith(pStableId, "qm:") != nullptr)
		{
			Navigation.m_SettingsPage = CMenus::SETTINGS_QMCLIENT;
			if(str_comp(pTab, "function") == 0)
				Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION;
			else if(str_comp(pTab, "hud") == 0)
				Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_HUD;
			else
				Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_VISUAL;
			return Navigation;
		}
		if(str_comp(pTab, "qmclient-contributors") == 0)
		{
			Navigation.m_SettingsPage = CMenus::SETTINGS_QMCLIENT;
			Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_CONTRIBUTORS;
			return Navigation;
		}
		if(str_startswith(pStableId, "tclient:") != nullptr)
		{
			Navigation.m_SettingsPage = CMenus::SETTINGS_TCLIENT;
			Navigation.m_TClientTab = 0;
			return Navigation;
		}
		for(const SQmGlobalSearchTabRoute &Route : s_aGlobalSearchTabRoutes)
		{
			if(str_comp(pTab, Route.m_pTab) != 0)
				continue;
			Navigation.m_SettingsPage = Route.m_SettingsPage;
			Navigation.m_TClientTab = Route.m_TClientTab;
			Navigation.m_AppearanceTab = Route.m_AppearanceTab;
			Navigation.m_QmClientTab = Route.m_QmClientTab;
			break;
		}
		return Navigation;
	}

	const char *GlobalSearchNavigationLabel(const SQmGlobalSearchNavigation &Navigation)
	{
		switch(Navigation.m_SettingsPage)
		{
		case CMenus::SETTINGS_QMCLIENT:
			if(Navigation.m_QmClientTab == CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION)
				return Localize("QmClient / Function");
			if(Navigation.m_QmClientTab == CMenus::QMCLIENT_SETTINGS_TAB_HUD)
				return Localize("QmClient / HUD");
			return Localize("QmClient / Visual");
		case CMenus::SETTINGS_TCLIENT:
			if(Navigation.m_TClientTab == 1)
				return Localize("TClient / Bind Wheel");
			if(Navigation.m_TClientTab == 4)
				return Localize("TClient / Status Bar");
			return Localize("TClient");
		case CMenus::SETTINGS_GRAPHICS:
			return Localize("Graphics");
		case CMenus::SETTINGS_SOUND:
			return Localize("Sound");
		case CMenus::SETTINGS_DDNET:
			return Localize("DDNet");
		case CMenus::SETTINGS_APPEARANCE:
			switch(Navigation.m_AppearanceTab)
			{
			case CMenus::APPEARANCE_TAB_CHAT: return Localize("Appearance / Chat");
			case CMenus::APPEARANCE_TAB_NAME_PLATE: return Localize("Appearance / Name Plate");
			case CMenus::APPEARANCE_TAB_HOOK_COLLISION: return Localize("Appearance / Hook Collision");
			case CMenus::APPEARANCE_TAB_INFO_MESSAGES: return Localize("Appearance / Info Messages");
			case CMenus::APPEARANCE_TAB_LASER: return Localize("Appearance / Laser");
			case CMenus::APPEARANCE_TAB_HUD:
			default: return Localize("Appearance / HUD");
			}
		default:
			return Localize("QmClient / Visual");
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

bool CMenus::RenderQmFunctionCheckbox(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, bool PrewarmOnly)
{
	const int OriginalValue = *pValue;
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pId, pTextId, pText, *pValue, pRect) != 0;
	if(Changed)
		*pValue ^= 1;
	if(PrewarmOnly || Ui()->RenderOnly())
		*pValue = OriginalValue;
	return Changed;
}

bool CMenus::IsQmNewFeatureRead(const char *pId) const
{
	if(pId == nullptr || pId[0] == '\0')
		return true;
	char aNeedle[128];
	str_format(aNeedle, sizeof(aNeedle), ";%s;", pId);
	char aMarks[sizeof(g_Config.m_QmNewFeatureMarksRead) + 2];
	str_format(aMarks, sizeof(aMarks), ";%s;", g_Config.m_QmNewFeatureMarksRead);
	return str_find(aMarks, aNeedle) != nullptr;
}

void CMenus::MarkQmNewFeatureRead(const char *pId)
{
	if(IsQmNewFeatureRead(pId))
		return;
	if(g_Config.m_QmNewFeatureMarksRead[0] != '\0')
		str_append(g_Config.m_QmNewFeatureMarksRead, ";", sizeof(g_Config.m_QmNewFeatureMarksRead));
	str_append(g_Config.m_QmNewFeatureMarksRead, pId, sizeof(g_Config.m_QmNewFeatureMarksRead));
}

void CMenus::MarkQmNewFeatureHovered(const char *pId, const CUIRect &Rect, bool PrewarmOnly)
{
	if(!PrewarmOnly && !IsQmNewFeatureRead(pId) && Ui()->MouseHovered(&Rect))
		MarkQmNewFeatureRead(pId);
}

bool CMenus::RenderQmVisualCheckbox(CUIRect &Content, float LineHeight, float LineSpacing, const void *pId, const char *pTextId, const char *pText, int *pValue)
{
	CUIRect Row;
	Content.HSplitTop(LineHeight, &Row, &Content);
	const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, pId, pTextId, pText, *pValue, &Row) != 0;
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
	constexpr const char *pScopeNewFeatureId = "qm_2_62_8_weapon_switch_scope";
	char aScopeLabel[128];
	const bool ScopeUnread = !IsQmNewFeatureRead(pScopeNewFeatureId);
	str_format(aScopeLabel, sizeof(aScopeLabel), "%s%s", Localize("Animation range"), ScopeUnread ? " [new]" : "");
	RenderQmVisualLabel("qmclient-weapon-switch-animation-range", &LabelColumn, aScopeLabel, BodySize);
	if(ScopeUnread)
	{
		CUIRect Dot = LabelColumn;
		Dot.x = Dot.x + Dot.w - 9.0f;
		Dot.y += 2.0f;
		Dot.w = 6.0f;
		Dot.h = 6.0f;
		Dot.Draw(ColorRGBA(1.0f, 0.12f, 0.16f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
	}
	MarkQmNewFeatureHovered(pScopeNewFeatureId, Row, PrewarmOnly);
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

void CMenus::RenderQmVisualSkinTransitionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.9f));
	RenderQmVisualLabel("qmclient-tee-appearance-title", &Row, Localize("Tee appearance"), SmallSize);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCycleTeeHue, "Cycle custom Tee hue", Localize("Cycle custom Tee hue"), &g_Config.m_QmCycleTeeHue);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCycleTeeHueDummy, "Also apply to dummy", Localize("Also apply to dummy"), &g_Config.m_QmCycleTeeHueDummy);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	SLabelProperties CycleHueSpeedLabelProps;
	CycleHueSpeedLabelProps.m_DisallowNewline = true;
	CycleHueSpeedLabelProps.m_StopAtEnd = true;
	CycleHueSpeedLabelProps.m_MinimumFontSize = 6.0f;
	if(!g_Config.m_QmCycleTeeHue)
		TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, 0.55f));
	RenderQmVisualLabel("qmclient-cycle-tee-hue-speed", &LabelColumn, Localize("Hue speed"), BodySize, TEXTALIGN_ML, CycleHueSpeedLabelProps);
	static int s_QmCycleTeeHueSpeedInputId;
	int DisabledSpeedPreview = g_Config.m_QmCycleTeeHueSpeed;
	RenderQmSettingsSliderWithValueInput(&s_QmCycleTeeHueSpeedInputId, ControlColumn, g_Config.m_QmCycleTeeHue ? &g_Config.m_QmCycleTeeHueSpeed : &DisabledSpeedPreview, 0, 360, "°/s", PrewarmOnly);
	if(!g_Config.m_QmCycleTeeHue)
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(SmallSize, &Row, &Content);
	TextRender()->TextColor(ColorRGBA(0.85f, 0.85f, 0.85f, 0.72f));
	RenderQmVisualLabel("qmclient-cycle-tee-hue-custom-note", &Row, Localize("Only affects custom Tee colors."), SmallSize);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(SmallSize, &Row, &Content);
	TextRender()->TextColor(g_Config.m_TcRainbowTees ? ColorRGBA(1.0f, 0.78f, 0.45f, 0.9f) : ColorRGBA(0.85f, 0.85f, 0.85f, 0.72f));
	RenderQmVisualLabel("qmclient-cycle-tee-hue-tclient-note", &Row, Localize("When TClient rainbow Tee is enabled, this feature has no effect."), SmallSize);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHammerSwapSkin, "Hammer skin steal", Localize("Hammer skin steal"), &g_Config.m_QmHammerSwapSkin);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmEmoticonShadow, "Emoticon shadow", Localize("Emoticon shadow"), &g_Config.m_QmEmoticonShadow);

	Content.HSplitTop(LineHeight, &Row, &Content);
	const char *pAnimationFeatureId = "qm_2_72_0_skin_transition_animation_toggle";
	const bool AnimationUnread = !IsQmNewFeatureRead(pAnimationFeatureId);
	if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmSkinChangeTransition, "Skin transition animation", Localize("Skin transition animation"), g_Config.m_QmSkinChangeTransition, &Row))
		g_Config.m_QmSkinChangeTransition ^= 1;
	if(AnimationUnread)
	{
		CUIRect Dot = Row;
		Dot.x = Dot.x + Dot.w - 9.0f;
		Dot.y += 2.0f;
		Dot.w = 6.0f;
		Dot.h = 6.0f;
		Dot.Draw(ColorRGBA(1.0f, 0.12f, 0.16f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
	}
	MarkQmNewFeatureHovered(pAnimationFeatureId, Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!g_Config.m_QmSkinChangeTransition)
		return;

	auto RenderDropDown = [&](const char *pTextId, const char *pText, int *pValue, int MaxValue, const char **ppNames, int NumNames, CUi::SDropDownState &State, CScrollRegion &ScrollRegion) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, Localize(pText), BodySize);
		State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
		const int NewValue = DoSettingsDropDown(&ControlColumn, std::clamp(*pValue, 0, MaxValue), ppNames, NumNames, State);
		if(*pValue != NewValue)
			*pValue = NewValue;
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static CUi::SDropDownState s_SkinTransitionTypeDropDownState;
	static CScrollRegion s_SkinTransitionTypeDropDownScrollRegion;
	const char *apSkinTransitionTypeNames[] = {Localize("Afterimage pop"), Localize("Smooth fade"), Localize("Slide left"), Localize("Spin pop"), Localize("Brightness shift"), Localize("Glitch"), Localize("Elastic")};
	RenderDropDown("qmclient-skin-transition-type", "Skin transition type", &g_Config.m_QmSkinChangeTransitionType, 6, apSkinTransitionTypeNames, std::size(apSkinTransitionTypeNames), s_SkinTransitionTypeDropDownState, s_SkinTransitionTypeDropDownScrollRegion);
	static CUi::SDropDownState s_SkinTransitionScopeDropDownState;
	static CScrollRegion s_SkinTransitionScopeDropDownScrollRegion;
	static std::vector<const char *> s_SkinTransitionScopeDropDownNames;
	s_SkinTransitionScopeDropDownNames = {Localize("Self only"), Localize("Local"), Localize("All players")};
	RenderDropDown("qmclient-skin-transition-range", "Animation range", &g_Config.m_QmSkinChangeTransitionScope, 2, s_SkinTransitionScopeDropDownNames.data(), (int)s_SkinTransitionScopeDropDownNames.size(), s_SkinTransitionScopeDropDownState, s_SkinTransitionScopeDropDownScrollRegion);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	SLabelProperties DurationLabelProps;
	DurationLabelProps.m_DisallowNewline = true;
	DurationLabelProps.m_StopAtEnd = true;
	DurationLabelProps.m_MinimumFontSize = 6.0f;
	RenderQmVisualLabel("qmclient-skin-transition-duration", &LabelColumn, Localize("Skin transition duration"), BodySize, TEXTALIGN_ML, DurationLabelProps);
	static int s_QmSkinChangeTransitionMsInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmSkinChangeTransitionMsInputId, ControlColumn, &g_Config.m_QmSkinChangeTransitionMs, 0, 2000, "ms", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	static CUi::SDropDownState s_SkinTransitionEasingDropDownState;
	static CScrollRegion s_SkinTransitionEasingDropDownScrollRegion;
	const char *apSkinTransitionEasingNames[] = {Localize("Ease out cubic"), Localize("Elastic back"), Localize("Linear"), Localize("Ease in out quad")};
	RenderDropDown("qmclient-skin-transition-easing", "Skin transition easing", &g_Config.m_QmSkinChangeTransitionEasing, 3, apSkinTransitionEasingNames, std::size(apSkinTransitionEasingNames), s_SkinTransitionEasingDropDownState, s_SkinTransitionEasingDropDownScrollRegion);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmVisualLabel("qmclient-skin-transition-intensity", &LabelColumn, Localize("Skin transition intensity"), BodySize);
	static int s_QmSkinChangeTransitionIntensityInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmSkinChangeTransitionIntensityInputId, ControlColumn, &g_Config.m_QmSkinChangeTransitionIntensity, 0, 300, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
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

void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float TipSize = Metrics.m_SmallSize;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	IUiContext CardCtx = SettingsUiContext("settings_qmclient_contributors", UiScale);
	if(ReadOnly)
	{
		CardCtx.m_pAnim = nullptr;
		CardCtx.m_pTree = nullptr;
	}
	const SSettingsCardDeckVisualOptions VisualOptions = SettingsCardDeckVisualOptions();
	static CScrollRegion s_ScrollRegion;
	static bool s_ShowSponsorQrCode = false;
	const bool SponsorImageVisible = FindMenuImage("sponsor") != nullptr;
	uint64_t CardLayoutRevision = str_quickhash("qmclient-contributors");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (s_ShowSponsorQrCode ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (SponsorImageVisible ? 1u : 0u);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [this, BodySize, LineHeight, LineSpacing, TipSize, ReadOnly](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(2);

		SSettingsCardDefinition Community;
		Community.m_Spec = {"deck:qmclient-contributors-community", Localize("QmClient Community"), Localize("Official community links")};
		Community.m_Measure = [LineHeight, LineSpacing](float) { return ResolveSettingsRowsHeight(3, LineHeight, LineSpacing); };
		Community.m_Render = [this, LineHeight, LineSpacing, TipSize, ReadOnly](CUIRect Content) {
			CUIRect Row;
			static int s_QQGroupButtonId;
			static bool s_QQCopied = false;
			static float s_QQCopiedTime = 0.0f;
			static CButtonContainer s_JoinQqGroupButton;
			static CButtonContainer s_RecentUpdateButton;
			static constexpr const char *pQmClientQqGroupLink = "https://qm.qq.com/cgi-bin/qm/qr?k=ntqdhb9_nB5GeWBo8IVMZoypYmbMwCQ1&jump_from=webapi&authKey=e4HiooMF/hxk8UZhTv8qDu7/8bZ9e3xc7rZYaLlyeifWglGT9KDchsQ7zjpinDr7";

			Content.HSplitTop(LineHeight, &Row, &Content);
			if(!ReadOnly && Ui()->MouseInside(&Row))
			{
				Ui()->SetHotItem(&s_QQGroupButtonId);
				if(Ui()->MouseButtonClicked(0))
				{
					Input()->SetClipboardText("1076765929");
					s_QQCopied = true;
					s_QQCopiedTime = Client()->LocalTime();
				}
			}
			if(!ReadOnly && s_QQCopied && Client()->LocalTime() - s_QQCopiedTime > 1.5f)
				s_QQCopied = false;
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, "qmclient-community-qq-group-copy", &Row, s_QQCopied ? Localize("Copied") : Localize("QQ group: 1076765929 (click to copy)"), TipSize, TEXTALIGN_ML, {}, (int)Row.w);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, &s_JoinQqGroupButton, "qmclient-community-join-qq-group", Localize("Join QQ group"), 0, &Row))
				Client()->ViewLink(pQmClientQqGroupLink);
			Content.HSplitTop(LineSpacing, nullptr, &Content);

			Content.HSplitTop(LineHeight, &Row, &Content);
			if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, &s_RecentUpdateButton, "qmclient-community-view-latest-updates", Localize("View latest updates"), 0, &Row))
				Client()->ViewLink("https://qmclient.icu");
		};
		vCards.push_back(std::move(Community));

		SSettingsCardDefinition Sponsors;
		Sponsors.m_Spec = {"deck:qmclient-contributors-sponsors", Localize("Sponsor support"), Localize("Thanks for supporting QmClient")};
		static const char *const s_apSponsors[] = {
			"喵不一", "久桃", "芽芽", "碳烤綿芽", "骨头", "陌浅羽", "树羽小朋友", "望舒", "松子", "平凡..", "cixin", "洗点",
			"秀色", "朱朱", "Twen", "大恐龙", ":luv:", "小左", "Blue°F", "怯修", "yezeen", "鹑", "枫香°", "没问题啊", "·蓝蓝蓝蓝",
			"临渊捕鱼", "?hook?", "放肆zero", "Q币", "洛天依", "spider", "贝塔塔塔", "见月", "咩子的银耳", "Cancer", "少女`",
			"长亭寂寞独自愁", "fantuan", "无言鱼", "胖人老许", "夏日", "张宁我儿", "拌饭", "shengyan", "修勾在修沟", "taffy",
			"杀意没爱意", "DYL", "小信", "哆啦梦", "菜菜羊", "吃了吗chilem", "你就是我的", "xiaopang", "星星🌙", "軽い猫",
			"oxyzo1", "笨蛋猫猫", "信息检索", "炭", "江江", "晚晚晚上好", "AAA乐土猫猫", "一個廢物", "黄花的忧伤", "丘卡", "迟渔", "潇洒的吗喽", "weijiu", "2284463973"};
		const auto BuildSponsorLines = [this, TipSize](float MaxLineWidth) {
			static std::vector<std::string> Lines;
			static float s_CachedMaxLineWidth = -1.0f;
			static uint64_t s_CachedTextGeneration = UINT64_MAX;
			if(std::abs(s_CachedMaxLineWidth - MaxLineWidth) <= 0.01f && s_CachedTextGeneration == m_MenuTextPoolGeneration)
				return std::cref(Lines);

			Lines.clear();
			Lines.emplace_back();
			const char *pSeparator = ", ";
			const float SeparatorWidth = TextRender()->TextWidth(TipSize, pSeparator);
			float LineWidth = 0.0f;
			for(const char *pName : s_apSponsors)
			{
				const float NameWidth = TextRender()->TextWidth(TipSize, pName);
				if(Lines.back().empty())
				{
					Lines.back() = pName;
					LineWidth = NameWidth;
				}
				else if(LineWidth + SeparatorWidth + NameWidth > MaxLineWidth)
				{
					Lines.emplace_back(pName);
					LineWidth = NameWidth;
				}
				else
				{
					Lines.back().append(pSeparator);
					Lines.back().append(pName);
					LineWidth += SeparatorWidth + NameWidth;
				}
			}
			s_CachedMaxLineWidth = MaxLineWidth;
			s_CachedTextGeneration = m_MenuTextPoolGeneration;
			return std::cref(Lines);
		};
		Sponsors.m_Measure = [this, LineHeight, LineSpacing, BuildSponsorLines](float ContentWidth) {
			const float ImageHeight = FindMenuImage("sponsor") != nullptr ? std::clamp(ContentWidth * 0.18f, LineHeight * 2.0f, LineHeight * 4.0f) : 0.0f;
			const float QrHeight = s_ShowSponsorQrCode ? LineHeight * 0.5f + std::clamp(ContentWidth, LineHeight * 8.0f, LineHeight * 12.0f) : 0.0f;
			const float SponsorLinesHeight = ResolveSettingsRowsHeight((int)BuildSponsorLines(ContentWidth).get().size(), LineHeight, LineSpacing);
			return ImageHeight + LineHeight + QrHeight + 2.0f * (LineHeight + LineSpacing) + SponsorLinesHeight + LineSpacing + LineHeight;
		};
		Sponsors.m_MeasureRevision = (FindMenuImage("sponsor") != nullptr ? 1u : 0u) | (s_ShowSponsorQrCode ? 2u : 0u);
		Sponsors.m_Render = [this, BodySize, LineHeight, LineSpacing, TipSize, ReadOnly, BuildSponsorLines](CUIRect Content) {
			CUIRect Row;
			static CButtonContainer s_SponsorButton;

			if(const CMenuImage *pSponsorImage = FindMenuImage("sponsor"))
			{
				Content.HSplitTop(std::clamp(Content.w * 0.18f, LineHeight * 2.0f, LineHeight * 4.0f), &Row, &Content);
				if(!ReadOnly)
				{
					Graphics()->TextureSet(pSponsorImage->m_OrgTexture);
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					IGraphics::CQuadItem Image(Row.x, Row.y, Row.w, Row.h);
					Graphics()->QuadsDrawTL(&Image, 1);
					Graphics()->QuadsEnd();
				}
			}

			Content.HSplitTop(LineHeight, &Row, &Content);
			if(!ReadOnly && DoButton_Menu(&s_SponsorButton, s_ShowSponsorQrCode ? Localize("Hide sponsor QR code") : Localize("Show sponsor QR code"), 0, &Row))
				s_ShowSponsorQrCode = !s_ShowSponsorQrCode;
			if(s_ShowSponsorQrCode)
			{
				Content.HSplitTop(LineHeight * 0.5f, nullptr, &Content);
				const float QrSide = std::clamp(Content.w, LineHeight * 8.0f, LineHeight * 12.0f);
				Content.HSplitTop(QrSide, &Row, &Content);
				if(!ReadOnly && g_QmClientEnsureSponsorQrTexture && g_QmClientEnsureSponsorQrTexture() && g_QmClientRenderTexture)
				{
					CUIRect QrRect = Row;
					QrRect.Margin(LineHeight * 0.35f, &QrRect);
					DrawRoundedSurface(Ui(), QrRect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), ColorRGBA(), 5.0f);
					QrRect.Margin(LineHeight * 0.5f, &QrRect);
					if(QrRect.w > QrRect.h)
					{
						const float Pad = (QrRect.w - QrRect.h) * 0.5f;
						QrRect.VSplitLeft(Pad, nullptr, &QrRect);
						QrRect.VSplitRight(Pad, &QrRect, nullptr);
					}
					else if(QrRect.h > QrRect.w)
					{
						const float Pad = (QrRect.h - QrRect.w) * 0.5f;
						QrRect.HSplitTop(Pad, nullptr, &QrRect);
						QrRect.HSplitBottom(Pad, &QrRect, nullptr);
					}
					g_QmClientRenderTexture(QrRect, 1.0f);
				}
				else if(!ReadOnly)
				{
					DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, "qmclient-community-sponsor-qr-decode-failed", &Row, Localize("Could not load sponsor QR code. Check the Base64 data"), TipSize, TEXTALIGN_ML, {}, (int)Row.w);
				}
			}

			Content.HSplitTop(LineHeight, &Row, &Content);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, "qmclient-community-developers-names", &Row, "栖梦(璇梦),夏日,DYL", BodySize, TEXTALIGN_ML, {}, (int)Row.w);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, "qmclient-community-sponsors-label", &Row, Localize("Sponsors:"), TipSize, TEXTALIGN_ML, {}, (int)Row.w);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			TextRender()->TextColor(ColorRGBA(0.95f, 0.8f, 0.2f, 1.0f));
			const std::vector<std::string> &SponsorLines = BuildSponsorLines(Content.w).get();
			for(size_t Index = 0; Index < SponsorLines.size(); ++Index)
			{
				Content.HSplitTop(LineHeight, &Row, &Content);
				Ui()->DoLabel(&Row, SponsorLines[Index].c_str(), TipSize, TEXTALIGN_ML);
				if(Index + 1 < SponsorLines.size())
					Content.HSplitTop(LineSpacing, nullptr, &Content);
			}
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, QMCLIENT_SETTINGS_TAB_CONTRIBUTORS, "qmclient-community-thanks", &Row, Localize("Thank you for your company and trust. This is what gives me the courage to keep going."), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		};
		vCards.push_back(std::move(Sponsors));
	};

	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_ScrollRegion.State();
	(void)ScrollState;
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
	static qm_card_order::CModel s_ContributorsPrewarmOrderModel;
	static bool s_ContributorsPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_ContributorsPrewarmDeck;
	if(ReadOnly && !s_ContributorsPrewarmOrderModelInitialized)
	{
		s_ContributorsPrewarmOrderModel.LoadMerged("", qm_card_registry::BuildDefaultEntries());
		s_ContributorsPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_ContributorsPrewarmOrderModel : SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = ReadOnly ? s_ContributorsPrewarmDeck : m_SettingsCardDeck;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(CardCtx, Page, "qmclient-contributors", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), VisualOptions);
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
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

void CMenus::RenderQmFunctionMiniFeaturesContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	CUIRect Row;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmFootParticles, "Local particle effects", &g_Config.m_QmFootParticles);
	RenderCheckbox(&g_Config.m_QmClientMarkTrail, "Remote particle effects", &g_Config.m_QmClientMarkTrail);
	RenderCheckbox(&g_Config.m_QmClientShowBadge, "Show Qm badge", &g_Config.m_QmClientShowBadge);
	RenderCheckbox(&g_Config.m_QmAutoUpdate, "Automatic updates", &g_Config.m_QmAutoUpdate);
	RenderCheckbox(&g_Config.m_QmShowOutdatedVersionWarning, "Show outdated version warning", &g_Config.m_QmShowOutdatedVersionWarning);
	RenderCheckbox(&g_Config.m_QmBetterScoreboard, "Better scoreboard", &g_Config.m_QmBetterScoreboard);
	RenderCheckbox(&g_Config.m_QmScoreboardPoints, "Scoreboard point check", &g_Config.m_QmScoreboardPoints);
	RenderCheckbox(&g_Config.m_QmScoreboardOnDeath, "Show scoreboard after death", &g_Config.m_QmScoreboardOnDeath);
	RenderCheckbox(&g_Config.m_QmHideJoinServerInfo, "Hide server information on join", &g_Config.m_QmHideJoinServerInfo);
	RenderCheckbox(&g_Config.m_QmMessageMerge, "Message merging", &g_Config.m_QmMessageMerge);
	RenderCheckbox(&g_Config.m_QmNewUi, "New UI", &g_Config.m_QmNewUi);
	RenderCheckbox(&g_Config.m_QmShortServerNames, "Short server names", &g_Config.m_QmShortServerNames);
	RenderCheckbox(&g_Config.m_QmImeAutoManage, "Auto manage IME while typing", &g_Config.m_QmImeAutoManage);
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmNewIme, "New IME", Localize("New IME"), &g_Config.m_QmNewIme, &Row, PrewarmOnly);
	if(!IsQmNewFeatureRead("qm_2_63_0_new_ime"))
	{
		CUIRect Dot;
		constexpr float DotSize = 6.0f;
		Dot = {Row.x + Row.w - DotSize - 3.0f, Row.y + 3.0f, DotSize, DotSize};
		Dot.Draw(ColorRGBA(1.0f, 0.12f, 0.16f, 0.95f), IGraphics::CORNER_ALL, DotSize * 0.5f);
	}
	MarkQmNewFeatureHovered("qm_2_63_0_new_ime", Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	RenderCheckbox(&g_Config.m_QmProcessHighPriority, "High process priority", &g_Config.m_QmProcessHighPriority);
	RenderCheckbox(&g_Config.m_QmRepeatEnabled, "Enable repeat", &g_Config.m_QmRepeatEnabled);
	RenderCheckbox(&g_Config.m_QmRandomEmoteOnHit, "Random emoticon", &g_Config.m_QmRandomEmoteOnHit);
	RenderCheckbox(&g_Config.m_QmComboPopup, "Combo", &g_Config.m_QmComboPopup);
	RenderCheckbox(&g_Config.m_QmSayNoPop, "Hide input emoticon", &g_Config.m_QmSayNoPop);
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
	if(DoButtonLineSize_Menu(&s_BlockWordsActionReplace, Localize("Replace mode"), BlockWordsAction == 0, &ActionButton, LineHeight, false, 0, IGraphics::CORNER_L, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_QmBlockWordsAction = 0;
	if(DoButtonLineSize_Menu(&s_BlockWordsActionHide, Localize("Hide player messages"), BlockWordsAction == 1, &ActionRow, LineHeight, false, 0, IGraphics::CORNER_R, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
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
		if(DoButtonLineSize_Menu(&s_BlockWordsModeRegex, Localize("Regular expression"), g_Config.m_QmBlockWordsMode == 0, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_L, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 0;
		ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
		if(DoButtonLineSize_Menu(&s_BlockWordsModeFull, Localize("Literal"), g_Config.m_QmBlockWordsMode == 1, &ModeButton, LineHeight, false, 0, IGraphics::CORNER_NONE, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			g_Config.m_QmBlockWordsMode = 1;
		if(DoButtonLineSize_Menu(&s_BlockWordsModeBoth, Localize("Both"), g_Config.m_QmBlockWordsMode == 2, &ModeRow, LineHeight, false, 0, IGraphics::CORNER_R, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
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
	QmKeywordReplyRules::SEditorChanges Changes;
	if(!PrewarmOnly && DoButton_Menu(&s_KeywordAddRuleButton, "+", 0, &AddRuleButtonRect))
	{
		auto pNewRule = CreateAutoReplyRuleInputRow();
		pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
		s_vKeywordRuleRows.push_back(std::move(pNewRule));
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), s_KeywordRulesLayoutHalfFilled);
		Changes.m_Added = true;
	}
	s_vKeywordRemoveRuleButtons.resize(s_vKeywordRuleRows.size());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	for(size_t i = 0; i < s_vKeywordRuleRows.size();)
	{
		auto &pRule = s_vKeywordRuleRows[i];
		pRule->m_TriggerInput.SetEmptyText("");
		pRule->m_ReplyInput.SetEmptyText("");
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect OptionsColumn;
		Row.VSplitLeft(LabelWidth, &OptionsColumn, &ControlColumn);
		CUIRect RenameColumn, RegexColumn, TriggerColumn, SendColumn, ReplyColumn, RemoveButtonRect;
		ControlColumn.VSplitRight(maximum(LineHeight, 24.0f * UiScale), &ControlColumn, &RemoveButtonRect);
		ControlColumn.VSplitLeft(ControlColumn.w * 0.45f, &TriggerColumn, &ControlColumn);
		ControlColumn.VSplitLeft(maximum(40.0f, 40.0f * UiScale), &SendColumn, &ReplyColumn);
		OptionsColumn.VSplitLeft(maximum(54.0f, 54.0f * UiScale), &RenameColumn, &OptionsColumn);
		OptionsColumn.VSplitLeft(maximum(54.0f, 54.0f * UiScale), &RegexColumn, &OptionsColumn);
		Changes.m_Rename |= RenderQmFunctionCheckbox(&pRule->m_AutoRename, "Rename", Localize("Rename"), &pRule->m_AutoRename, &RenameColumn, PrewarmOnly);
		Changes.m_Regex |= RenderQmFunctionCheckbox(&pRule->m_Regex, "Regex", Localize("Regex"), &pRule->m_Regex, &RegexColumn, PrewarmOnly);
		Changes.m_TriggerText |= ui_widget::InputField(TextInputCtx, &pRule->m_TriggerInput, TriggerColumn, "", BodySize);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-send-label", &SendColumn, Localize("Send"), BodySize, TEXTALIGN_MC, {}, (int)SendColumn.w);
		Changes.m_ReplyText |= ui_widget::InputField(TextInputCtx, &pRule->m_ReplyInput, ReplyColumn, "", BodySize);
		const bool RemoveClicked = !PrewarmOnly && DoButton_Menu(&s_vKeywordRemoveRuleButtons[i], "-", 0, &RemoveButtonRect);
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

	static std::vector<const char *> s_TranslateBackendDropDownNames;
	s_TranslateBackendDropDownNames = {Localize("Tencent Cloud"), "LibreTranslate", "FTAPI", "LLM API"};
	static CUi::SDropDownState s_TranslateBackendDropDownState;
	static CScrollRegion s_TranslateBackendDropDownScrollRegion;
	s_TranslateBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TranslateBackendDropDownScrollRegion;

	int BackendSelectedOld = 0;
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
		BackendSelectedOld = 1;
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
		BackendSelectedOld = 2;
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
		BackendSelectedOld = 3;

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	CUIElement &TranslationServiceLabel = SettingsTextElement(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-translation-service");
	DoSettingsLabelStreamed(TranslationServiceLabel, &LabelCol, Localize("Translation service"), BodySize, TEXTALIGN_ML);
	const int BackendSelectedNew = DoSettingsDropDown(&ControlCol, BackendSelectedOld, s_TranslateBackendDropDownNames.data(), s_TranslateBackendDropDownNames.size(), s_TranslateBackendDropDownState);
	if(BackendSelectedNew != BackendSelectedOld)
	{
		if(BackendSelectedNew == 1)
		{
			str_copy(g_Config.m_QmTranslateBackend, "libretranslate", sizeof(g_Config.m_QmTranslateBackend));
			str_copy(g_Config.m_QmTranslateLibreEndpoint, "", sizeof(g_Config.m_QmTranslateLibreEndpoint)); // Use default localhost:5000
		}
		else if(BackendSelectedNew == 2)
		{
			str_copy(g_Config.m_QmTranslateBackend, "ftapi", sizeof(g_Config.m_QmTranslateBackend));
			str_copy(g_Config.m_QmTranslateTcEndpoint, "", sizeof(g_Config.m_QmTranslateTcEndpoint)); // Use default ftapi.pythonanywhere.com
		}
		else if(BackendSelectedNew == 3)
		{
			// LLM API - 默认使用智谱AI预设
			str_copy(g_Config.m_QmTranslateBackend, "llm", sizeof(g_Config.m_QmTranslateBackend));
			// LLM API - 清空自定义端点，使用默认
			str_copy(g_Config.m_QmTranslateLlmEndpointCustom, "", sizeof(g_Config.m_QmTranslateLlmEndpointCustom));
		}
		else
		{
			str_copy(g_Config.m_QmTranslateBackend, "腾讯云", sizeof(g_Config.m_QmTranslateBackend));
			str_copy(g_Config.m_QmTranslateTcEndpoint, "", sizeof(g_Config.m_QmTranslateTcEndpoint)); // Use default tencent endpoint
		}
	}
	const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
	const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
	const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
	const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
	Content.HSplitTop(LineSpacing, nullptr, &Content);

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
		const int SelectedIndex = maximum(OldSel, 0);
		const int NewSel = DoSettingsDropDown(&DropRect, SelectedIndex, apNames, Count, DropDownState);
		if(NewSel >= 0 && NewSel != OldSel)
			str_copy(pConfigValue, apCodes[NewSel], ConfigValueSize);

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
	DoSettingsLabelStreamed(TargetLanguageLabel, &LabelCol, Localize("Target language"), BodySize, TEXTALIGN_ML);

	// 下拉框 + 输入框组合
	{
		static const char *s_apLangNames[] = {"中文", "English", "日本語", "한국어", "繁體中文", "Русский", "Deutsch", "Français", "Español", "Português"};
		static const char *s_apLangCodes[] = {"zh", "en", "ja", "ko", "zh-TW", "ru", "de", "fr", "es", "pt"};
		static CUi::SDropDownState s_TargetLangDropDown;

		static CLineInput s_TranslateTarget(g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget));
		RenderLanguageDropDownWithCustomInput(ControlCol, s_apLangNames, s_apLangCodes, std::size(s_apLangCodes), s_TargetLangDropDown, g_Config.m_QmTranslateTarget, sizeof(g_Config.m_QmTranslateTarget), s_TranslateTarget, "zh");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

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
	Content.HSplitTop(LineSpacing * 0.5f, nullptr, &Content);

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
		if(NewProvider != g_Config.m_QmTranslateLlmProvider)
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
		const char *pKeyLabel = Localize("API key");
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

		// 各 Provider 的端点配置（允许覆盖默认）
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

		// 各 Provider 的模型配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-model", &LabelCol, Localize("Model"), BodySize);

		static CLineInput s_LlmModelZhipu(g_Config.m_QmTranslateLlmModelZhipu, sizeof(g_Config.m_QmTranslateLlmModelZhipu));
		static CLineInput s_LlmModelDeepseek(g_Config.m_QmTranslateLlmModelDeepseek, sizeof(g_Config.m_QmTranslateLlmModelDeepseek));
		static CLineInput s_LlmModelOpenai(g_Config.m_QmTranslateLlmModelOpenai, sizeof(g_Config.m_QmTranslateLlmModelOpenai));
		static CLineInput s_LlmModelCustom(g_Config.m_QmTranslateLlmModelCustom, sizeof(g_Config.m_QmTranslateLlmModelCustom));

		CLineInput *pActiveModelInput = nullptr;
		const char *pModelEmptyText = "model-name";
		switch(g_Config.m_QmTranslateLlmProvider)
		{
		case 0: // Zhipu AI
			pModelEmptyText = "glm-4.5-flash";
			s_LlmModelZhipu.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelZhipu;
			break;
		case 1: // DeepSeek
			pModelEmptyText = "deepseek-chat";
			s_LlmModelDeepseek.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelDeepseek;
			break;
		case 2: // OpenAI
			pModelEmptyText = "gpt-4o-mini";
			s_LlmModelOpenai.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelOpenai;
			break;
		case 3: // Custom
		default:
			s_LlmModelCustom.SetEmptyText(pModelEmptyText);
			pActiveModelInput = &s_LlmModelCustom;
			break;
		}
		if(pActiveModelInput)
			ui_widget::InputField(TextInputCtx, pActiveModelInput, ControlCol, pActiveModelInput->GetEmptyText(), BodySize);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// LLM 并发数配置
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		RenderLabel("qmclient-translate-llm-concurrency", &LabelCol, Localize("Concurrency (0 = auto)"), BodySize);
		static int s_LlmConcurrencySelectorId;
		RenderSliderWithNumberInput(&s_LlmConcurrencySelectorId, ControlCol, &g_Config.m_QmTranslateLlmConcurrency, 0, 20);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		// 显示当前有效并发数
		{
			// 计算智能默认值（与 GetEffectiveConcurrency 逻辑一致）
			int EffectiveConcurrency = 3; // 默认值
			if(g_Config.m_QmTranslateLlmConcurrency != 0)
			{
				// 用户手动设置
				EffectiveConcurrency = g_Config.m_QmTranslateLlmConcurrency;
			}
			else
			{
				// 根据 Provider 类型提供智能默认值
				switch(g_Config.m_QmTranslateLlmProvider)
				{
				case 0: // Zhipu AI
				case 1: // DeepSeek
					EffectiveConcurrency = 3;
					break;
				case 2: // OpenAI
					EffectiveConcurrency = 2;
					break;
				case 3: // Custom
				default:
					EffectiveConcurrency = g_Config.m_QmTranslateLlmConcurrencyDefault;
					break;
				}
			}

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
		Content.HSplitTop(LineSpacing * 0.5f, nullptr, &Content);
	}

	// 入站语言和出站语言配置
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	RenderLabel("qmclient-translate-send-source-language", &LabelCol, Localize("Send source language"), BodySize);

	// 下拉框 + 输入框组合
	{
		const std::array<const char *, 11> apSourceNames = {
			Localize("Auto"),
			"中文",
			"English",
			"日本語",
			"한국어",
			"繁體中文",
			"Русский",
			"Deutsch",
			"Français",
			"Español",
			"Português",
		};
		const std::array<const char *, 11> apSourceCodes = {"auto", "zh", "en", "ja", "ko", "zh-TW", "ru", "de", "fr", "es", "pt"};
		static CUi::SDropDownState s_SourceLangDropDown;

		static CLineInput s_SourceLang(g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource));
		RenderLanguageDropDownWithCustomInput(ControlCol, apSourceNames.data(), apSourceCodes.data(), apSourceCodes.size(), s_SourceLangDropDown, g_Config.m_QmTranslateSource, sizeof(g_Config.m_QmTranslateSource), s_SourceLang, "auto");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
	RenderLabel("qmclient-translate-send-target-language", &LabelCol, Localize("Send target language"), BodySize);

	// 下拉框 + 输入框组合
	{
		const std::array<const char *, 10> apOutTargetNames = {"中文", "English", "日本語", "한국어", "繁體中文", "Русский", "Deutsch", "Français", "Español", "Português"};
		const std::array<const char *, 10> apOutTargetCodes = {"zh", "en", "ja", "ko", "zh-TW", "ru", "de", "fr", "es", "pt"};
		static CUi::SDropDownState s_OutTargetLangDropDown;

		static CLineInput s_TargetLang(g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget));
		RenderLanguageDropDownWithCustomInput(ControlCol, apOutTargetNames.data(), apOutTargetCodes.data(), apOutTargetCodes.size(), s_OutTargetLangDropDown, g_Config.m_QmTranslateOutgoingTarget, sizeof(g_Config.m_QmTranslateOutgoingTarget), s_TargetLang, "en");
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

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

	// Content.HSplitTop(LineHeight, &Row, &Content);
	// Ui()->DoLabel(&Row, Localize("Auto-translate will skip simplified Chinese, traditional Chinese, and server messages"), BodySize * 0.8f, TEXTALIGN_ML);
	// Content.HSplitTop(LineSpacing / 2.0f, nullptr, &Content);
	//
	// Content.HSplitTop(LineHeight, &Row, &Content);
	// Ui()->DoLabel(&Row, Localize("Append language codes like [ru], [en], [ja] at the end when sending"), BodySize * 0.8f, TEXTALIGN_ML);
	// Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmFunctionPieMenuContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CornerRadius, bool PrewarmOnly)
{
	CPerfTimer LayoutTimer;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_pie_menu_text_inputs", UiScale);
	char aLayoutExtra[96];
	str_copy(aLayoutExtra, "tab=function module=pie_menu", sizeof(aLayoutExtra));
	LogQmPerfStage(Client(), "pie_menu_layout", LayoutTimer.ElapsedMs(), false, aLayoutExtra);
	CPerfTimer ControlsTimer;
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmPieMenuEnabled, "Enable pie menu", Localize("Enable pie menu"), &g_Config.m_QmPieMenuEnabled, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!g_Config.m_QmPieMenuEnabled)
	{
		LogQmPerfStage(Client(), "pie_menu_controls", ControlsTimer.ElapsedMs(), false, aLayoutExtra);
		return;
	}

	auto RenderSlider = [this, &Content, &Row, &LabelColumn, &ControlColumn, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue, int Min, int Max, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_PieMenuScaleInputId, s_PieMenuOpacityInputId, s_PieMenuMaxDistanceInputId;
	RenderSlider(&s_PieMenuScaleInputId, "qmclient-pie-menu-ui-scale", "UI scale", &g_Config.m_QmPieMenuScale, 50, 200, "%");
	RenderSlider(&s_PieMenuOpacityInputId, "qmclient-pie-menu-opacity", "Opacity", &g_Config.m_QmPieMenuOpacity, 0, 100, "%");
	RenderSlider(&s_PieMenuMaxDistanceInputId, "qmclient-pie-menu-detection-distance", "Detection distance", &g_Config.m_QmPieMenuMaxDistance, 100, 2000);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-rename-queue", &LabelColumn, Localize("Rename queue"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static CLineInput s_PieMenuRenameQueue(g_Config.m_QmPieMenuRenameQueue, sizeof(g_Config.m_QmPieMenuRenameQueue));
	s_PieMenuRenameQueue.SetEmptyText(Localize("Example: name1|name2|name3"));
	ui_widget::InputField(TextInputCtx, &s_PieMenuRenameQueue, ControlColumn, Localize("Example: name1|name2|name3"), BodySize);
	Content.HSplitTop(LineSpacing * 2.0f, nullptr, &Content);

	Content.HSplitTop(BodySize, &Row, &Content);
	TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-option-color", &Row, Localize("Option color"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	struct SPieMenuColorEntry
	{
		const char *m_pName;
		const char *m_pIcon;
		unsigned int *m_pColorValue;
		ColorRGBA m_DefaultColor;
	};
	const std::array<SPieMenuColorEntry, 6> aColorEntries = {{
		{Localize("Friend"), "♥", (unsigned int *)&g_Config.m_QmPieMenuColorFriend, ColorRGBA(0.9f, 0.3f, 0.4f)},
		{Localize("Whisper"), "✉", (unsigned int *)&g_Config.m_QmPieMenuColorWhisper, ColorRGBA(0.5f, 0.35f, 0.7f)},
		{Localize("Mention"), "➤", (unsigned int *)&g_Config.m_QmPieMenuColorMention, ColorRGBA(0.85f, 0.5f, 0.2f)},
		{Localize("Copy skin"), "⚡", (unsigned int *)&g_Config.m_QmPieMenuColorCopySkin, ColorRGBA(0.25f, 0.55f, 0.8f)},
		{Localize("Switch"), "⇄", (unsigned int *)&g_Config.m_QmPieMenuColorSwap, ColorRGBA(0.8f, 0.3f, 0.3f)},
		{Localize("Spectate"), "👁", (unsigned int *)&g_Config.m_QmPieMenuColorSpectate, ColorRGBA(0.45f, 0.55f, 0.6f)},
	}};
	auto OpenColorPopup = [&](unsigned int *pColorValue) {
		const ColorHSLA HslaColor = ColorHSLA(*pColorValue, false);
		m_ColorPickerPopupContext.m_pHslaColor = pColorValue;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = false;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	};
	constexpr float PreviewStartAngle = -90.0f;
	constexpr float PreviewSectorGap = 3.6f;
	constexpr float PreviewInnerRatio = 108.0f / 288.0f;
	constexpr float PreviewHighlightScale = 1.12f;
	const float PreviewBaseSide = minimum(Content.w, std::clamp(Content.w * 0.88f, LineHeight * 10.0f, LineHeight * 13.5f));
	const float PreviewSide = PreviewBaseSide * 0.8f;
	CUIRect PreviewRow, PreviewRect, PreviewInfoRect;
	Content.HSplitTop(PreviewSide, &PreviewRow, &Content);
	PreviewRow.VSplitLeft(PreviewSide, &PreviewRect, &PreviewInfoRect);
	PreviewInfoRect.VSplitLeft(maximum(CardPadding * 0.8f, LineSpacing * 2.0f), nullptr, &PreviewInfoRect);
	PreviewRect.Margin(LineSpacing * 0.5f, &PreviewRect);
	CUIRect PreviewFrame = PreviewRect;
	const char *pHintText = Localize("Click to set color");
	if(!PrewarmOnly)
	{
		{
			CUiScopedGaussianBlurSuppression PreviewBlurSuppression(Ui());
			PreviewFrame.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, CornerRadius * 0.8f);
		}
		PreviewRect.Margin(maximum(4.0f, LineSpacing * 0.6f), &PreviewRect);
		const vec2 PreviewCenter = PreviewRect.Center();
		const float BaseOuterRadius = maximum(1.0f, minimum(PreviewRect.w, PreviewRect.h) * 0.5f - LineSpacing * 0.8f);
		const float InnerRadius = BaseOuterRadius * PreviewInnerRatio;
		const float CenterRadius = maximum(1.0f, InnerRadius - maximum(4.0f, BaseOuterRadius * 0.03f));
		const float AnglePerSector = 360.0f / (float)std::size(aColorEntries);
		const float PreviewAlpha = std::clamp(g_Config.m_QmPieMenuOpacity / 100.0f, 0.2f, 1.0f);
		int PopupSectorIndex = -1;
		if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
		{
			for(size_t i = 0; i < aColorEntries.size(); ++i)
				if(m_ColorPickerPopupContext.m_pHslaColor == aColorEntries[i].m_pColorValue)
					PopupSectorIndex = (int)i;
		}
		int HoveredSector = -1;
		if(Ui()->MouseInside(&PreviewFrame))
		{
			const vec2 MouseDir = Ui()->MousePos() - PreviewCenter;
			const float MouseDist = length(MouseDir);
			if(MouseDist >= InnerRadius && MouseDist <= BaseOuterRadius * PreviewHighlightScale)
			{
				float MouseAngle = atan2(MouseDir.y, MouseDir.x) * 180.0f / pi;
				while(MouseAngle < 0.0f)
					MouseAngle += 360.0f;
				const float AdjustedAngle = fmodf(MouseAngle - PreviewStartAngle + 360.0f, 360.0f);
				const int SectorIndex = (int)(AdjustedAngle / AnglePerSector);
				const float AngleInSector = AdjustedAngle - SectorIndex * AnglePerSector;
				if(SectorIndex >= 0 && SectorIndex < (int)aColorEntries.size() && AngleInSector >= PreviewSectorGap * 0.5f && AngleInSector <= AnglePerSector - PreviewSectorGap * 0.5f)
					HoveredSector = SectorIndex;
			}
		}
		static CButtonContainer s_ColorPreviewButton;
		if(Ui()->DoButtonLogic(&s_ColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0)
			OpenColorPopup(aColorEntries[HoveredSector].m_pColorValue);
		for(size_t i = 0; i < aColorEntries.size(); ++i)
		{
			const bool Highlighted = (int)i == HoveredSector || (int)i == PopupSectorIndex;
			const float OuterRadius = BaseOuterRadius * (Highlighted ? PreviewHighlightScale : 1.0f);
			const float StartAngle = PreviewStartAngle + AnglePerSector * i + PreviewSectorGap * 0.5f;
			const float EndAngle = StartAngle + AnglePerSector - PreviewSectorGap;
			ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(*aColorEntries[i].m_pColorValue));
			if(Highlighted)
			{
				Color.r = minimum(Color.r * 1.3f, 1.0f);
				Color.g = minimum(Color.g * 1.3f, 1.0f);
				Color.b = minimum(Color.b * 1.3f, 1.0f);
				Color.a = minimum(Color.a * 1.2f, 1.0f);
			}
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a * PreviewAlpha);
			for(int Segment = 0; Segment < 24; ++Segment)
			{
				const float Rad1 = (StartAngle + (EndAngle - StartAngle) * (Segment / 24.0f)) * pi / 180.0f;
				const float Rad2 = (StartAngle + (EndAngle - StartAngle) * ((Segment + 1) / 24.0f)) * pi / 180.0f;
				const vec2 Inner1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * InnerRadius;
				const vec2 Outer1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * OuterRadius;
				const vec2 Inner2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * InnerRadius;
				const vec2 Outer2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * OuterRadius;
				const IGraphics::CFreeformItem Freeform(Inner1.x, Inner1.y, Outer1.x, Outer1.y, Inner2.x, Inner2.y, Outer2.x, Outer2.y);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}
			Graphics()->QuadsEnd();
			const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
			const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * ((InnerRadius + OuterRadius) * 0.5f);
			const float IconSize = maximum(BodySize * 1.45f, BaseOuterRadius * (Highlighted ? 0.20f : 0.163f));
			const float TextSize = maximum(BodySize * 0.95f, BaseOuterRadius * (Highlighted ? 0.10f : 0.08f));
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, PreviewAlpha);
			const float IconWidth = TextRender()->TextWidth(IconSize, aColorEntries[i].m_pIcon);
			const float IconYOffset = BaseOuterRadius * 0.0625f;
			TextRender()->Text(ItemPos.x - IconWidth * 0.5f, ItemPos.y - IconSize * 0.5f - IconYOffset, IconSize, aColorEntries[i].m_pIcon);
			const float NameWidth = TextRender()->TextWidth(TextSize, aColorEntries[i].m_pName);
			TextRender()->Text(ItemPos.x - NameWidth * 0.5f, ItemPos.y + BaseOuterRadius * 0.0486f, TextSize, aColorEntries[i].m_pName);
		}
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.15f, 0.15f, 0.2f, 0.9f * PreviewAlpha);
		Graphics()->DrawCircle(PreviewCenter.x, PreviewCenter.y, CenterRadius, 48);
		Graphics()->QuadsEnd();
		const int FocusedSector = HoveredSector >= 0 ? HoveredSector : PopupSectorIndex;
		const char *pCenterTitle = FocusedSector >= 0 ? aColorEntries[FocusedSector].m_pName : Localize("Click to set color");
		const char *pCenterSubtitle = FocusedSector >= 0 ? Localize("Open color picker") : Localize("Set color");
		pHintText = FocusedSector >= 0 ? aColorEntries[FocusedSector].m_pName : Localize("Click to set color");
		const float CenterTitleSize = maximum(BodySize * 1.05f, BaseOuterRadius * 0.095f);
		const float CenterSubtitleSize = maximum(BodySize * 0.75f, BaseOuterRadius * 0.055f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.98f);
		const float CenterTitleWidth = TextRender()->TextWidth(CenterTitleSize, pCenterTitle);
		TextRender()->Text(PreviewCenter.x - CenterTitleWidth * 0.5f, PreviewCenter.y - CenterTitleSize * 0.9f, CenterTitleSize, pCenterTitle);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.68f);
		const float CenterSubtitleWidth = TextRender()->TextWidth(CenterSubtitleSize, pCenterSubtitle);
		TextRender()->Text(PreviewCenter.x - CenterSubtitleWidth * 0.5f, PreviewCenter.y + CenterSubtitleSize * 0.1f, CenterSubtitleSize, pCenterSubtitle);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect PreviewInfoContent = PreviewInfoRect;
	const float InfoSpacing = LineSpacing * 0.75f;
	const float InfoHeight = LineHeight + InfoSpacing + ButtonHeight;
	if(PreviewInfoContent.h > InfoHeight)
		PreviewInfoContent.HSplitTop((PreviewInfoContent.h - InfoHeight) * 0.5f, nullptr, &PreviewInfoContent);
	CUIRect HintRow, ResetRow;
	PreviewInfoContent.HSplitTop(LineHeight, &HintRow, &PreviewInfoContent);
	PreviewInfoContent.HSplitTop(InfoSpacing, nullptr, &PreviewInfoContent);
	PreviewInfoContent.HSplitTop(ButtonHeight, &ResetRow, &PreviewInfoContent);
	Ui()->DoLabel(&HintRow, pHintText, BodySize * 0.9f, TEXTALIGN_MR);
	static CButtonContainer s_ResetAllColorsButton;
	CUIRect ResetButton;
	const float ResetWidth = maximum(ButtonHeight * 4.0f, TextRender()->TextWidth(BodySize, Localize("Reset")) + ButtonHeight * 2.0f);
	ResetRow.VSplitRight(ResetWidth, nullptr, &ResetButton);
	if(!PrewarmOnly && DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, &s_ResetAllColorsButton, "qmclient-pie-menu-reset-colors", Localize("Reset"), 0, &ResetButton))
		for(const auto &Entry : aColorEntries)
			*Entry.m_pColorValue = color_cast<ColorHSLA>(Entry.m_DefaultColor).Pack(false);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	LogQmPerfStage(Client(), "pie_menu_controls", ControlsTimer.ElapsedMs(), false, aLayoutExtra);
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
	auto MapTypeDisplayName = [this](const char *pType) -> const char * {
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
		if(ui_widget::IconButton(IconButtonCtx, &s_aMapRemoveButtons[MapIndex], FONT_ICON_XMARK, RowRemove))
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

void CMenus::RenderQmFunctionHJAssistContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	RenderCheckbox(&g_Config.m_QmAutoUnspecOnUnfreeze, "Auto unspec on unfreeze", &g_Config.m_QmAutoUnspecOnUnfreeze);
	RenderCheckbox(&g_Config.m_QmAutoSwitchOnUnfreeze, "Auto switch to the tee that got unfrozen", &g_Config.m_QmAutoSwitchOnUnfreeze);
	RenderCheckbox(&g_Config.m_QmAutoCloseChatOnUnfreeze, "Automatically close the current chat after waking from freeze", &g_Config.m_QmAutoCloseChatOnUnfreeze);
	RenderCheckbox(&g_Config.m_QmFreezeWakeupPopup, "Show wake-up popup on the other tee", &g_Config.m_QmFreezeWakeupPopup);
	RenderCheckbox(&g_Config.m_QmAutoTeamLock, "Auto team lock", &g_Config.m_QmAutoTeamLock);
	if(!g_Config.m_QmAutoTeamLock)
		return;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-hj-assist-lock-delay", &LabelColumn, Localize("Lock delay"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static int s_QmAutoTeamLockDelayInputId;
	RenderQmSettingsSliderWithValueInput(&s_QmAutoTeamLockDelayInputId, ControlColumn, &g_Config.m_QmAutoTeamLockDelay, 0, 30, "s", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudSpeedrunTimerContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &g_Config.m_QmSpeedrunTimer, "Enable speedrun timer", Localize("Enable speedrun timer"), g_Config.m_QmSpeedrunTimer, &Row))
		g_Config.m_QmSpeedrunTimer ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!g_Config.m_QmSpeedrunTimer)
		return;

	auto RenderValue = [&](const char *pTextId, const char *pText, const void *pInputId, int *pValue, int MinValue, int MaxValue) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue, MinValue, MaxValue, "", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_QmSpeedrunTimerHoursInputId;
	static int s_QmSpeedrunTimerMinutesInputId;
	static int s_QmSpeedrunTimerSecondsInputId;
	static int s_QmSpeedrunTimerMillisecondsInputId;
	RenderValue("qmclient-speedrun-timer-hours", "Hours", &s_QmSpeedrunTimerHoursInputId, &g_Config.m_QmSpeedrunTimerHours, 0, 99);
	RenderValue("qmclient-speedrun-timer-minutes", "Minutes", &s_QmSpeedrunTimerMinutesInputId, &g_Config.m_QmSpeedrunTimerMinutes, 0, 59);
	RenderValue("qmclient-speedrun-timer-seconds", "Seconds", &s_QmSpeedrunTimerSecondsInputId, &g_Config.m_QmSpeedrunTimerSeconds, 0, 59);
	RenderValue("qmclient-speedrun-timer-milliseconds", "Milliseconds", &s_QmSpeedrunTimerMillisecondsInputId, &g_Config.m_QmSpeedrunTimerMilliseconds, 0, 999);
	Content.HSplitTop(LineHeight, &Row, &Content);
	if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &g_Config.m_QmSpeedrunTimerAutoDisable, "Auto disable when time expires", Localize("Auto disable when time expires"), g_Config.m_QmSpeedrunTimerAutoDisable, &Row))
		g_Config.m_QmSpeedrunTimerAutoDisable ^= 1;
	Content.HSplitTop(LineSpacing, nullptr, &Content);
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
	RenderValue("qmclient-input-overlay-keyboard-size", "Keyboard size", &s_QmInputOverlayScaleInputId, &g_Config.m_QmInputOverlayScale, 1, 200);
	RenderValue("qmclient-input-overlay-mouse-size", "Mouse size", &s_QmInputOverlayMouseScaleInputId, &g_Config.m_QmInputOverlayMouseScale, 1, 200);
	RenderValue("qmclient-input-overlay-opacity", "Opacity", &s_QmInputOverlayOpacityInputId, &g_Config.m_QmInputOverlayOpacity, 0, 100);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	RenderQmHudLabel("qmclient-input-overlay-editor", &LabelColumn, Localize("Editor"), BodySize);
	static CButtonContainer s_InputOverlayEditorButton;
	if(!PrewarmOnly && DoButton_Menu(&s_InputOverlayEditorButton, Localize("Open editor"), 0, &ControlColumn))
		GameClient()->m_InputOverlay.OpenEditor();
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
	DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize("Background color"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80);
}

void CMenus::RenderQmHudSystemMediaControlsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcEnable, "Enable system media control", Localize("Enable system media control"), &g_Config.m_QmSmtcEnable);
	if(!g_Config.m_QmSmtcEnable)
		return;

	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSmtcShowHud, "Show song info in top-left corner", Localize("Show song info in top-left corner"), &g_Config.m_QmSmtcShowHud);
	CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
	Content.HSplitTop(LineHeight, &MediaButtons, &Content);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing * 2.0f) / 3.0f, &PrevButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	MediaButtons.VSplitLeft((MediaButtons.w - LineSpacing) / 2.0f, &PlayButton, &MediaButtons);
	MediaButtons.VSplitLeft(LineSpacing, nullptr, &MediaButtons);
	NextButton = MediaButtons;

	static CButtonContainer s_SmtcPrev;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPrev, "qmclient-smtc-previous", Localize("Previous"), 0, &PrevButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, 5.0f))
		GameClient()->m_SystemMediaControls.Previous();
	static CButtonContainer s_SmtcPlayPause;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcPlayPause, "qmclient-smtc-play-pause", Localize("Play/Pause"), 0, &PlayButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, 5.0f))
		GameClient()->m_SystemMediaControls.PlayPause();
	static CButtonContainer s_SmtcNext;
	if(DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, &s_SmtcNext, "qmclient-smtc-next", Localize("Next"), 0, &NextButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, 5.0f))
		GameClient()->m_SystemMediaControls.Next();
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}

void CMenus::RenderQmHudLyricsContent(CUIRect &Content, float LineHeight, float LineSpacing, bool PrewarmOnly)
{
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmNeteaseHookEnable, "Enable Netease music Hook", Localize("Enable Netease music Hook"), &g_Config.m_QmNeteaseHookEnable);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSodaHookEnable, "Enable SodaMusic Hook", Localize("Enable SodaMusic Hook"), &g_Config.m_QmSodaHookEnable);
	// 两个 Hook 互斥:同一时间只能有一个歌词来源,避免优先级歧义。
	if(g_Config.m_QmNeteaseHookEnable != 0 && g_Config.m_QmSodaHookEnable != 0)
		g_Config.m_QmSodaHookEnable = 0;
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, "Enable lyrics", Localize("Enable lyrics"), &g_Config.m_QmLyrics);
	RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, "Show lyrics inside Dynamic Island", Localize("Show lyrics inside Dynamic Island"), &g_Config.m_QmLyricsInMediaIsland);
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
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect) != 0;
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
			DoQmSettingsLabel("qmclient-voice-server-ip", &LabelCol, Localize("Server IP"), BodySize);
			static CLineInput s_VoiceServer(g_Config.m_QmVoiceServer, sizeof(g_Config.m_QmVoiceServer));
			if(!PrewarmOnly && !Ui()->RenderOnly())
				s_VoiceServer.SetEmptyText("42.194.185.210:9987");
			ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceServer, ControlCol, "42.194.185.210:9987", BodySize);
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
	static char s_aCollapsedConfigCache[sizeof(g_Config.m_QmSidebarCardCollapsed)] = {};
	static bool s_CollapsedInitialized = false;
	if(!s_CollapsedInitialized || str_comp(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed) != 0)
	{
		ParseLegacyQmCollapsed(g_Config.m_QmSidebarCardCollapsed, s_aQmModuleDefaults, s_aCollapsed);
		s_CollapsedInitialized = true;
	}
	char aNormalizedCollapsed[sizeof(g_Config.m_QmSidebarCardCollapsed)];
	SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, aNormalizedCollapsed, sizeof(aNormalizedCollapsed));
	if(!Ui()->RenderOnly() && str_comp(aNormalizedCollapsed, g_Config.m_QmSidebarCardCollapsed) != 0)
		str_copy(g_Config.m_QmSidebarCardCollapsed, aNormalizedCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
	str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));

	auto ModuleStateIndex = [](EQmModuleId Id) { return std::clamp((int)Id, 0, (int)QmModuleCount - 1); };
	auto ToggleCollapsed = [](EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		s_aCollapsed[Index] = !s_aCollapsed[Index];
		SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, g_Config.m_QmSidebarCardCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
		str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));
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
		case EQmModuleId::SpeedrunTimer: return g_Config.m_QmSpeedrunTimer ? Rows(6.0f) : Rows(1.0f);
		case EQmModuleId::DebugGraph: return Rows(2.0f);
		case EQmModuleId::InputOverlay: return ResolveQmHudInputOverlayHeight(Metrics, g_Config.m_QmInputOverlay != 0);
		case EQmModuleId::HudNotifications: return ResolveQmHudNotificationsHeight(Metrics, g_Config.m_QmHudNotificationsShowAdvanced != 0, g_Config.m_QmHudNotificationsUseCategoryFilters != 0);
		case EQmModuleId::Voice: return ResolveQmHudVoiceHeight(Metrics, g_Config.m_QmVoiceEnable != 0, g_Config.m_QmVoiceShowAdvanced != 0, g_Config.m_QmVoiceShowConnectionStatus != 0, g_Config.m_QmVoiceNoiseSuppressEnable, g_Config.m_QmVoiceVadEnable != 0, g_Config.m_QmVoiceStereo != 0);
		case EQmModuleId::DynamicIsland: return ResolveQmHudDynamicIslandHeight(Metrics, DynamicIslandOriginalStyle, ContentWidth);
		case EQmModuleId::SystemMediaControls: return g_Config.m_QmSmtcEnable ? Rows(3.0f) : Rows(1.0f);
		case EQmModuleId::Lyrics: return Rows(4.0f);
		case EQmModuleId::Background3D: return ResolveQmHudBackground3DHeight(Metrics, ContentWidth, g_Config.m_Qm3DParticles != 0, g_Config.m_Qm3DParticlesColorMode == 1, g_Config.m_Qm3DParticlesGlow != 0, g_Config.m_Qm3DParticlesTrail != 0, g_Config.m_Qm3DParticlesPulse != 0, g_Config.m_Qm3DParticlesTwinkle != 0);
		default: return Rows(1.0f);
		}
	};
	auto MeasureContentRevision = [DummyMiniViewExpanded, DynamicIslandOriginalStyle](EQmModuleId Id) -> uint64_t {
		switch(Id)
		{
		case EQmModuleId::DummyMiniView: return DummyMiniViewExpanded ? 1u : 0u;
		case EQmModuleId::PlayerStats: return (g_Config.m_QmPlayerStatsMapProgress ? 1u : 0u) | (g_Config.m_QmPlayerStatsMapProgressStyle ? 2u : 0u);
		case EQmModuleId::SpeedrunTimer: return g_Config.m_QmSpeedrunTimer ? 1u : 0u;
		case EQmModuleId::InputOverlay: return g_Config.m_QmInputOverlay ? 1u : 0u;
		case EQmModuleId::HudNotifications:
		{
			uint64_t Revision = g_Config.m_QmHudNotificationsShowAdvanced ? 1u : 0u;
			if(g_Config.m_QmHudNotificationsShowAdvanced && g_Config.m_QmHudNotificationsUseCategoryFilters)
				Revision |= 2u;
			return Revision;
		}
		case EQmModuleId::Voice: return ResolveQmHudVoiceRevision(g_Config.m_QmVoiceEnable != 0, g_Config.m_QmVoiceShowAdvanced != 0, g_Config.m_QmVoiceShowConnectionStatus != 0, g_Config.m_QmVoiceNoiseSuppressEnable, g_Config.m_QmVoiceVadEnable != 0, g_Config.m_QmVoiceStereo != 0);
		case EQmModuleId::DynamicIsland: return DynamicIslandOriginalStyle ? 1u : 0u;
		case EQmModuleId::SystemMediaControls: return g_Config.m_QmSmtcEnable ? 1u : 0u;
		case EQmModuleId::Lyrics: return 0u;
		case EQmModuleId::Background3D: return ResolveQmHudBackground3DRevision(g_Config.m_Qm3DParticles != 0, g_Config.m_Qm3DParticlesColorMode == 1, g_Config.m_Qm3DParticlesGlow != 0, g_Config.m_Qm3DParticlesTrail != 0, g_Config.m_Qm3DParticlesPulse != 0, g_Config.m_Qm3DParticlesTwinkle != 0);
		default: return 0u;
		}
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
		case EQmModuleId::SpeedrunTimer:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				return HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSpeedrunTimer, &g_Config.m_QmSpeedrunTimer);
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
		case EQmModuleId::Lyrics:
			return [this, LineHeight, LineSpacing](CUIRect Content) {
				bool Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmNeteaseHookEnable, &g_Config.m_QmNeteaseHookEnable);
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSodaHookEnable, &g_Config.m_QmSodaHookEnable) || Changed;
				// 互斥:同一时间只能有一个歌词来源。
				if(g_Config.m_QmNeteaseHookEnable != 0 && g_Config.m_QmSodaHookEnable != 0)
					g_Config.m_QmSodaHookEnable = 0;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmLyrics, &g_Config.m_QmLyrics) || Changed;
				Changed = HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmLyricsInMediaIsland, &g_Config.m_QmLyricsInMediaIsland) || Changed;
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
		default:
			return {};
		}
	};

	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(12);
		const auto AddCard = [&](EQmModuleId Id, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			SSettingsCardDefinition Definition;
			const char *pRegisteredSubtitle = qm_card_registry::ResolveLocalizedDescription(pStableId);
			Definition.m_Spec = {pStableId, Localize(pTitle), pRegisteredSubtitle != nullptr ? pRegisteredSubtitle : Localize(pSubtitle)};
			Definition.m_Measure = [Id, EstimateContentHeight](float ContentWidth) { return EstimateContentHeight(Id, ContentWidth); };
			Definition.m_Render = [Render](CUIRect Content) { Render(Content); };
			Definition.m_IsCollapsed = [Id, &Collapsed = s_aCollapsed, ModuleStateIndex] { return Collapsed[ModuleStateIndex(Id)]; };
			Definition.m_PreLayoutHeaderInput = [this, Id, ToggleCollapsed, ReadOnly, ModuleStateIndex, &CollapseButtons = s_aCollapseButtons](const SSettingsCardFrame &Frame, bool IsCollapsed) {
				const int Index = ModuleStateIndex(Id);
				if(ReadOnly || !Ui()->DoButtonLogic(&CollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				ToggleCollapsed(Id);
				return true;
			};
			Definition.m_HeaderAction = [CardCtx](const SSettingsCardFrame &Frame, bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
			Definition.m_MeasureRevision = MeasureContentRevision(Id);
			Definition.m_PreLayoutInput = BuildHudPreLayoutInput(Id);
			vCards.push_back(std::move(Definition));
		};

		AddCard(EQmModuleId::DummyMiniView, "qm:dummy_miniview", "Dummy Window", "Show a small view of the dummy", [this, LineHeight, BodySize, LineSpacing, LabelWidth, DummyMiniViewExpanded, ReadOnly](CUIRect &Content) { RenderQmHudDummyMiniViewContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, DummyMiniViewExpanded, ReadOnly); });
		AddCard(EQmModuleId::Coords, "qm:coords", "Show Coordinates", "Show coordinates above players", [this, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudCoordsContent(Content, Metrics, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::PlayerStats, "qm:player_stats", "Player data", "Player stats and info display", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudPlayerStatsContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::SpeedrunTimer, "qm:speedrun_timer", "Speedrun Timer", "Speedrun countdown timer", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudSpeedrunTimerContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::DebugGraph, "qm:debug_graph", "Debug graph", "Debug performance graph panel", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudDebugGraphContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::InputOverlay, "qm:input_overlay", "Input overlay", "Input overlay display", [this, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudInputOverlayContent(Content, Metrics, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::HudNotifications, "qm:hud_notifications", "Notifications", "Show important server prompts and Echo messages as popups", [this, Metrics, LabelWidth, ReadOnly](CUIRect &Content) {
			RenderQmHudNotificationsBasicContent(Content, Metrics, LabelWidth, ReadOnly);
			if(g_Config.m_QmHudNotificationsShowAdvanced)
				RenderQmHudNotificationsAdvancedContent(Content, Metrics, LabelWidth, ReadOnly);
		});
		AddCard(EQmModuleId::Voice, "qm:voice", "Voice", "Voice chat settings and diagnostics", [this, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudVoiceContent(Content, Metrics, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::DynamicIsland, "qm:dynamic_island", "Dynamic Island", "HUD island appearance settings", [this, LineHeight, LineSpacing, DynamicIslandOriginalStyle](CUIRect &Content) { RenderQmHudDynamicIslandContent(Content, LineHeight, LineSpacing, DynamicIslandOriginalStyle); });
		AddCard(EQmModuleId::SystemMediaControls, "qm:system_media_controls", "SMTC", "System media control", [this, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { RenderQmHudSystemMediaControlsContent(Content, LineHeight, BodySize, LineSpacing, ReadOnly); });
		AddCard(EQmModuleId::Lyrics, "qm:lyrics", "Lyrics", "Lyrics sources and display", [this, LineHeight, LineSpacing, ReadOnly](CUIRect &Content) { RenderQmHudLyricsContent(Content, LineHeight, LineSpacing, ReadOnly); });
		AddCard(EQmModuleId::Background3D, "qm:background_3d", "3D Background", "Configure background 3D particle effects", [this, Metrics, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmHudBackground3DContent(Content, Metrics, LabelWidth, ReadOnly); });
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
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
	static char s_aCollapsedConfigCache[sizeof(g_Config.m_QmSidebarCardCollapsed)] = {};
	static bool s_CollapsedInitialized = false;
	if(!s_CollapsedInitialized || str_comp(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed) != 0)
	{
		ParseLegacyQmCollapsed(g_Config.m_QmSidebarCardCollapsed, s_aQmModuleDefaults, s_aCollapsed);
		s_CollapsedInitialized = true;
	}
	char aNormalizedCollapsed[sizeof(g_Config.m_QmSidebarCardCollapsed)];
	SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, aNormalizedCollapsed, sizeof(aNormalizedCollapsed));
	if(!Ui()->RenderOnly() && str_comp(aNormalizedCollapsed, g_Config.m_QmSidebarCardCollapsed) != 0)
		str_copy(g_Config.m_QmSidebarCardCollapsed, aNormalizedCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
	str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));

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
	auto ToggleCollapsed = [this](EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		const bool WasCollapsed = s_aCollapsed[Index];
		s_aCollapsed[Index] = !s_aCollapsed[Index];
		if(Id == EQmModuleId::BlockWords && WasCollapsed)
			++s_BlockWordsLayoutRevision;
		else if(Id == EQmModuleId::QiaFen && WasCollapsed)
		{
			s_KeywordRuleRowsInited = false;
			++s_KeywordRulesLayoutRevision;
		}
		else if(Id == EQmModuleId::FavoriteMaps && WasCollapsed)
		{
			const size_t FavoriteMapCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
			if(s_FavoriteMapsLayoutCount != FavoriteMapCount)
			{
				s_FavoriteMapsLayoutCount = FavoriteMapCount;
				++s_FavoriteMapsLayoutRevision;
			}
		}
		SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, g_Config.m_QmSidebarCardCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
		str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));
	};
	auto MeasureContentHeight = [this, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, Metrics](EQmModuleId Id, float ContentWidth) {
		const auto Rows = [LineHeight, LineSpacing](float Count) { return Count * (LineHeight + LineSpacing); };
		const auto Row = [LineHeight, LineSpacing](float Spacing = 1.0f) { return LineHeight + LineSpacing * Spacing; };
		switch(Id)
		{
		case EQmModuleId::GoresActor:
			return !g_Config.m_TcFreezeChatEnabled ? Row() : Row() * (g_Config.m_TcFreezeChatEmoticon ? 5.0f : 4.0f);
		case EQmModuleId::Gores:
			return Row() * (3.0f + (g_Config.m_QmAxiomAutoLogin ? 2.0f : 0.0f) + ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 6.0f : 0.0f)) + LineHeight;
		case EQmModuleId::KeyBinds: return Rows(8.0f);
		case EQmModuleId::MiniFeatures: return Rows(19.0f);
		case EQmModuleId::JumpHint: return Row() * 5.0f;
		case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory == 0 ? Row() : Row() * 5.0f;
		case EQmModuleId::FriendNotify:
			return Row() * (5.0f + (g_Config.m_QmFriendOnlineAutoRefresh ? 1.0f : 0.0f) + (g_Config.m_QmFriendEnterBroadcast ? 1.0f : 0.0f) + (g_Config.m_QmFriendEnterAutoGreet ? 1.0f : 0.0f));
		case EQmModuleId::BlockWords: return Row() * (g_Config.m_QmBlockWordsAction == 0 ? 7.0f : 4.0f) + CalcQiaFenInputHeight(TextRender(), g_Config.m_QmBlockWordsList, std::max(1.0f, ContentWidth - LabelWidth), BodySize, std::clamp(2.0f * UiScale, 1.0f, 2.0f), LineHeight);
		case EQmModuleId::Translate:
		{
			const bool IsTencentCloudBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0;
			const bool IsLibreTranslateBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0;
			const bool IsLlmBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0;
			const bool IsFtapiBackend = str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0;
			float Height = Rows(9.0f) + LineHeight * 1.6f + LineSpacing * 1.35f;
			if(IsFtapiBackend)
				Height += Row() + LineHeight * 0.8f + LineSpacing;
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
			if(!g_Config.m_QmPieMenuEnabled)
				return Row();
			return Row() * 5.0f + BodySize + LineSpacing * 3.0f + std::min(ContentWidth, std::clamp(ContentWidth * 0.88f, LineHeight * 10.0f, LineHeight * 13.5f)) * 0.8f;
		case EQmModuleId::FavoriteMaps:
		{
			const size_t FavoriteCount = GameClient()->TClientComponent().GetFavoriteMaps().size();
			return Rows((float)std::max<size_t>(1, std::min<size_t>(FavoriteCount, 64)));
		}
		case EQmModuleId::HJAssist: return Row() * (g_Config.m_QmAutoTeamLock ? 6.0f : 5.0f);
		default: return Rows(1.0f);
		}
	};
	auto MeasureContentRevision = [](EQmModuleId Id) -> uint64_t {
		switch(Id)
		{
		case EQmModuleId::GoresActor:
			return g_Config.m_TcFreezeChatEnabled ? 1u | (g_Config.m_TcFreezeChatEmoticon ? 2u : 0u) : 0u;
		case EQmModuleId::Gores:
			return (g_Config.m_QmAxiomAutoLogin ? 1u : 0u) |
			       ((g_Config.m_QmGores || g_Config.m_QmGoresAutoEnable) ? 2u : 0u);
		case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory != 0 ? 1u : 0u;
		case EQmModuleId::FriendNotify:
			return (g_Config.m_QmFriendOnlineAutoRefresh ? 1u : 0u) |
			       (g_Config.m_QmFriendEnterBroadcast ? 2u : 0u) |
			       (g_Config.m_QmFriendEnterAutoGreet ? 4u : 0u);
		case EQmModuleId::BlockWords: return s_BlockWordsLayoutRevision * 2u + (g_Config.m_QmBlockWordsAction == 0 ? 0u : 1u);
		case EQmModuleId::Translate:
			if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
				return 1u;
			if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
				return 2u;
			if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
				return 3u;
			if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
				return 4u | ((g_Config.m_QmTranslateLlmEnableThinking && (g_Config.m_QmTranslateLlmProvider == 2 || g_Config.m_QmTranslateLlmProvider == 3)) ? 8u : 0u);
			return 0u;
		case EQmModuleId::QiaFen: return s_KeywordRulesLayoutRevision;
		case EQmModuleId::PieMenu: return g_Config.m_QmPieMenuEnabled ? 1u : 0u;
		case EQmModuleId::FavoriteMaps: return s_FavoriteMapsLayoutRevision;
		case EQmModuleId::HJAssist: return g_Config.m_QmAutoTeamLock ? 1u : 0u;
		default: return 0u;
		}
	};
	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(14);
		const auto AddCard = [&](EQmModuleId Id, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			SSettingsCardDefinition Definition;
			const char *pRegisteredSubtitle = qm_card_registry::ResolveLocalizedDescription(pStableId);
			Definition.m_Spec = {pStableId, Localize(pTitle), pRegisteredSubtitle != nullptr ? pRegisteredSubtitle : Localize(pSubtitle)};
			Definition.m_Measure = [Id, MeasureContentHeight](float ContentWidth) { return MeasureContentHeight(Id, ContentWidth); };
			Definition.m_Render = [Render](CUIRect Content) { Render(Content); };
			Definition.m_IsCollapsed = [Id, &Collapsed = s_aCollapsed, ModuleStateIndex] { return Collapsed[ModuleStateIndex(Id)]; };
			Definition.m_PreLayoutHeaderInput = [this, Id, ToggleCollapsed, ReadOnly, ModuleStateIndex, &CollapseButtons = s_aCollapseButtons](const SSettingsCardFrame &Frame, bool IsCollapsed) {
				const int Index = ModuleStateIndex(Id);
				if(ReadOnly || !Ui()->DoButtonLogic(&CollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				ToggleCollapsed(Id);
				return true;
			};
			Definition.m_HeaderAction = [CardCtx](const SSettingsCardFrame &Frame, bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
			Definition.m_MeasureRevision = MeasureContentRevision(Id);
			vCards.push_back(std::move(Definition));
		};

		AddCard(EQmModuleId::GoresActor, "qm:gores_actor", "Gores Actor", "Auto chat when dying in water", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionGoresActorContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::Gores, "qm:gores", "Gores Mode", "Gores auto weapon switch", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionGoresContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::KeyBinds, "qm:key_binds", "Key Bindings", "Common key bindings", [this, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { RenderQmFunctionKeyBindsContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth); });
		AddCard(EQmModuleId::MiniFeatures, "qm:mini_features", "Dream Features", "Only what you can't imagine, nothing Dream can't do", [this, LineHeight, LineSpacing, ReadOnly](CUIRect &Content) { RenderQmFunctionMiniFeaturesContent(Content, LineHeight, LineSpacing, ReadOnly); });
		AddCard(EQmModuleId::JumpHint, "qm:jump_hint", "Position jump hint", "Jump hint text", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionJumpHintContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::WeaponTrajectory, "qm:weapon_trajectory", "Weapon Trajectory", "Show grenade and laser trajectory preview", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionWeaponTrajectoryContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::FriendNotify, "qm:friend_notify", "Friend Notifications", "Friend online and join notifications", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionFriendNotifyContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::BlockWords, "qm:block_words", "Word Filter", "Chat word filtering", [this, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionBlockWordsContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::Translate, "qm:translate", "Translate", "Chat translation settings", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionTranslateContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::TranslateUi, "qm:translate_ui", "Translate button", "Customize translate button and menu colors", [this, LineHeight, BodySize, LineSpacing](CUIRect &Content) { RenderQmVisualTranslateUiContent(Content, LineHeight, BodySize, LineSpacing); });
		AddCard(EQmModuleId::QiaFen, "qm:qiafen", "Keyword Reply", "I am a robot", [this, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionKeywordReplyContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::PieMenu, "qm:pie_menu", "Pie Menu", "Quick action menu for players", [this, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight = Metrics.m_ButtonHeight, CardStyle, ReadOnly](CUIRect &Content) { RenderQmFunctionPieMenuContent(Content, UiScale, LineHeight, BodySize, LineSpacing, LabelWidth, ButtonHeight, CardStyle.m_Padding, CardStyle.m_CornerRadius, ReadOnly); });
		AddCard(EQmModuleId::FavoriteMaps, "qm:favorite_maps", "Favorite maps", "Your favorite map manager", [this, UiScale, LineHeight, BodySize, LineSpacing, ReadOnly](CUIRect &Content) { RenderQmFunctionFavoriteMapsContent(Content, UiScale, LineHeight, BodySize, LineSpacing, ReadOnly); });
		AddCard(EQmModuleId::HJAssist, "qm:hj_assist", "HJ Assist", "What's done is done, no use saying more", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmFunctionHJAssistContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
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
	static char s_aCollapsedConfigCache[sizeof(g_Config.m_QmSidebarCardCollapsed)] = {};
	static bool s_CollapsedInitialized = false;
	const bool CollapsedConfigChanged = !s_CollapsedInitialized || str_comp(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed) != 0;
	if(CollapsedConfigChanged)
	{
		ParseLegacyQmCollapsed(g_Config.m_QmSidebarCardCollapsed, s_aQmModuleDefaults, s_aCollapsed);
		s_CollapsedInitialized = true;
	}
	char aNormalizedCollapsed[sizeof(g_Config.m_QmSidebarCardCollapsed)];
	SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, aNormalizedCollapsed, sizeof(aNormalizedCollapsed));
	if(!Ui()->RenderOnly() && str_comp(aNormalizedCollapsed, g_Config.m_QmSidebarCardCollapsed) != 0)
		str_copy(g_Config.m_QmSidebarCardCollapsed, aNormalizedCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
	str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));

	auto ModuleStateIndex = [](EQmModuleId Id) { return std::clamp((int)Id, 0, (int)QmModuleCount - 1); };
	auto ToggleCollapsed = [](EQmModuleId Id) {
		const int Index = std::clamp((int)Id, 0, (int)QmModuleCount - 1);
		s_aCollapsed[Index] = !s_aCollapsed[Index];
		SerializeLegacyQmCollapsed(s_aQmModuleDefaults, s_aCollapsed, g_Config.m_QmSidebarCardCollapsed, sizeof(g_Config.m_QmSidebarCardCollapsed));
		str_copy(s_aCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aCollapsedConfigCache));
	};
	auto EstimateContentHeight = [Metrics](EQmModuleId Id) {
		const auto Rows = [&Metrics](float Count) { return Count * Metrics.m_RowStep; };
		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			return g_Config.m_QmChatBubble ? Rows(5.0f) + 2.0f * Metrics.m_LineHeight + 2.0f * Metrics.m_LineSpacing : Rows(1.0f);
		case EQmModuleId::CameraView:
			return Rows(5.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;
		case EQmModuleId::SkinTransition:
			return ResolveQmVisualSkinTransitionHeight(Metrics, g_Config.m_QmSkinChangeTransition != 0);
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
		switch(Id)
		{
		case EQmModuleId::ChatBubble: return g_Config.m_QmChatBubble ? 1u : 0u;
		case EQmModuleId::CameraView:
			return (g_Config.m_QmCameraDrift ? 1u : 0u) |
			       (g_Config.m_QmDynamicFov ? 2u : 0u) |
			       (g_Config.m_QmAspectPreset == 6 ? 4u : 0u);
		case EQmModuleId::SkinTransition: return g_Config.m_QmSkinChangeTransition ? 1u : 0u;
		case EQmModuleId::WeaponAnimation:
			return (g_Config.m_QmWeaponSwitchAnim ? 1u : 0u) |
			       (g_Config.m_QmWeaponReloadAnim ? 2u : 0u);
		case EQmModuleId::CollisionHitbox:
			return (g_Config.m_QmHitboxMode || g_Config.m_QmShowCollisionHitbox ? 1u : 0u) |
			       (g_Config.m_QmHitboxShowMap ? 1u << 1 : 0u) |
			       (g_Config.m_QmHitboxShowTeeCollision ? 1u << 2 : 0u) |
			       (g_Config.m_QmHitboxShowTeeFreeze ? 1u << 3 : 0u) |
			       (g_Config.m_QmHitboxShowTeeDeath ? 1u << 4 : 0u) |
			       (g_Config.m_QmHitboxShowPickups ? 1u << 5 : 0u) |
			       (g_Config.m_QmHitboxShowHammer ? 1u << 6 : 0u) |
			       (g_Config.m_QmHitboxShowProjectiles ? 1u << 7 : 0u) |
			       (g_Config.m_QmHitboxShowLasers ? 1u << 8 : 0u) |
			       (g_Config.m_QmHitboxShowFreezeLasers ? 1u << 9 : 0u) |
			       (g_Config.m_QmHitboxShowHook ? 1u << 10 : 0u);
		default: return 0u;
		}
	};
	const auto ConsumeVisualRow = [LineHeight, LineSpacing](CUIRect &Content) {
		Content.HSplitTop(LineHeight + LineSpacing, nullptr, &Content);
	};
	const auto ConsumeVisualHeight = [](CUIRect &Content, const float Height) {
		Content.HSplitTop(std::max(0.0f, Height), nullptr, &Content);
	};
	const auto BuildVisualPreLayoutInput = [this, Metrics, LineHeight, LineSpacing, ReadOnly, ConsumeVisualRow, ConsumeVisualHeight](EQmModuleId Id) -> FSettingsCardPreLayoutInput {
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
				return Changed;
			};
		case EQmModuleId::SkinTransition:
			return [this, Metrics, LineHeight, LineSpacing, ConsumeVisualRow, ConsumeVisualHeight](CUIRect Content) {
				ConsumeVisualRow(Content); // Tee appearance heading
				for(int Index = 0; Index < 3; ++Index)
					ConsumeVisualRow(Content); // hue toggles and speed
				ConsumeVisualHeight(Content, Metrics.m_SmallSize + LineSpacing);
				ConsumeVisualHeight(Content, Metrics.m_SmallSize + LineSpacing);
				ConsumeVisualRow(Content); // hammer skin steal
				ConsumeVisualRow(Content); // emoticon shadow
				return HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinChangeTransition, &g_Config.m_QmSkinChangeTransition);
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

	auto BuildDefinitions = [&](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(9);
		const auto AddCard = [&](EQmModuleId Id, const char *pStableId, const char *pTitle, const char *pSubtitle, const FSettingsCardRenderMeasured &Render) {
			SSettingsCardDefinition Definition;
			const char *pRegisteredSubtitle = qm_card_registry::ResolveLocalizedDescription(pStableId);
			Definition.m_Spec = {pStableId, Localize(pTitle), pRegisteredSubtitle != nullptr ? pRegisteredSubtitle : Localize(pSubtitle)};
			Definition.m_Measure = [Id, EstimateContentHeight](float) { return EstimateContentHeight(Id); };
			Definition.m_Render = [Render](CUIRect Content) { Render(Content); };
			Definition.m_IsCollapsed = [Id, &Collapsed = s_aCollapsed, ModuleStateIndex] { return Collapsed[ModuleStateIndex(Id)]; };
			Definition.m_PreLayoutHeaderInput = [this, Id, ToggleCollapsed, ReadOnly, ModuleStateIndex, &CollapseButtons = s_aCollapseButtons](const SSettingsCardFrame &Frame, bool IsCollapsed) {
				const int Index = ModuleStateIndex(Id);
				if(ReadOnly || !Ui()->DoButtonLogic(&CollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
					return false;
				ToggleCollapsed(Id);
				return true;
			};
			Definition.m_HeaderAction = [CardCtx](const SSettingsCardFrame &Frame, bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
			Definition.m_MeasureRevision = MeasureContentRevision(Id);
			Definition.m_PreLayoutInput = BuildVisualPreLayoutInput(Id);
			vCards.push_back(std::move(Definition));
		};

		AddCard(EQmModuleId::ChatBubble, "qm:chat_bubble", "Chat Bubble", "Show chat messages above players", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualChatBubbleContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::CameraView, "qm:camera_view", "Camera & FOV", "Adjust game camera and FOV settings", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualCameraViewContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::SkinTransition, "qm:skin_transition", "Skin transition", "Configure hammer skin steal and skin transition animations", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualSkinTransitionContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::FocusMode, "qm:focus_mode", "Zen Mode", "Hide UI for focused gameplay", [this, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content) { RenderQmVisualFocusModeContent(Content, LineHeight, BodySize, LineSpacing, LineSpacing, LabelWidth); });
		AddCard(EQmModuleId::WeaponAnimation, "qm:weapon_animation", "Weapon animation", "Play a slide-in rotation animation when switching weapons", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualWeaponAnimationContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, LineSpacing, ReadOnly); });
		AddCard(EQmModuleId::Streamer, "qm:streamer", "Streamer Mode", "Protect names and skins while streaming", [this, LineHeight, LineSpacing](CUIRect &Content) { RenderQmVisualStreamerContent(Content, LineHeight, LineSpacing); });
		AddCard(EQmModuleId::EntityOverlay, "qm:entity_overlay", "Entity Layer Colors", "Adjust opacity of entity layers", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualEntityOverlayContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
		AddCard(EQmModuleId::CollisionHitbox, "qm:collision_hitbox", "Hitbox mode", "Show collision and weapon interaction", [this, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { RenderQmVisualCollisionHitboxContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); });
	};
	uint64_t CardLayoutRevision = 0;
	for(int ModuleIndex = 0; ModuleIndex < (int)QmModuleCount; ++ModuleIndex)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ MeasureContentRevision((EQmModuleId)ModuleIndex);
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

void CMenus::RenderSettingsQmClient(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)
{
	if(!PrewarmOnly)
		EnsureSettingsBindCache();

	RenderSettingsQmClientContent(MainView, ContributorsPage, PrewarmOnly);
}

void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float SmallSize = Metrics.m_SmallSize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const float CardGap = Metrics.m_CardGap;
	const float ResultHeight = std::clamp(84.0f * UiScale, 70.0f, 84.0f);
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext SearchCtx = SettingsUiContext("settings_global_search", UiScale);
	if(ReadOnly)
	{
		SearchCtx.m_pAnim = nullptr;
		SearchCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_GlobalSearchScrollRegion;

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
	};
	static SGlobalSearchCache s_GlobalSearchCache;
	static qm_card_order::CModel s_GlobalSearchPrewarmOrderModel;
	static std::string s_GlobalSearchPrewarmOrderSource;
	static bool s_GlobalSearchPrewarmOrderModelInitialized = false;
	static CSettingsCardDeck s_GlobalSearchPrewarmDeck;
	if(ReadOnly && (!s_GlobalSearchPrewarmOrderModelInitialized || s_GlobalSearchPrewarmOrderSource != g_Config.m_QmGlobalCardOrder))
	{
		s_GlobalSearchPrewarmOrderModel.LoadMerged(g_Config.m_QmGlobalCardOrder, qm_card_registry::BuildDefaultEntries());
		s_GlobalSearchPrewarmOrderSource = g_Config.m_QmGlobalCardOrder;
		s_GlobalSearchPrewarmOrderModelInitialized = true;
	}
	qm_card_order::CModel &CardOrderModel = ReadOnly ? s_GlobalSearchPrewarmOrderModel : SettingsCardOrderModel();
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
		s_GlobalSearchCache.m_Valid = true;
	}
	const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
	const int SearchMatchedGlobalCardCount = (int)SearchVisibleGlobalCards.size();
	uint64_t CardLayoutRevision = str_quickhash("global-search");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(pModuleSearch != nullptr ? pModuleSearch : "");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ LayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Client()->IsSixup() ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)SearchMatchedGlobalCardCount;
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);
	const auto BuildDefinitions = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, ResultHeight, CardGap, SearchMatchedGlobalCardCount, ReadOnly](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(2);
		SSettingsCardDefinition InputCard;
		InputCard.m_Spec = {"deck:global-search-input", Localize("Feature Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-input")};
		InputCard.m_Measure = [LineHeight, LineSpacing](float) { return 2.0f * LineHeight + LineSpacing; };
		InputCard.m_Render = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, ReadOnly](CUIRect Content) {
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
		vCards.push_back(std::move(InputCard));

		SSettingsCardDefinition ResultsCard;
		ResultsCard.m_Spec = {"deck:global-search-results", Localize("Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-results")};
		ResultsCard.m_Measure = [SearchMatchedGlobalCardCount, ResultHeight, CardGap, LineHeight](float) {
			return SearchMatchedGlobalCardCount == 0 ? LineHeight : SearchMatchedGlobalCardCount * ResultHeight + std::max(0, SearchMatchedGlobalCardCount - 1) * CardGap;
		};
		ResultsCard.m_Render = [this, BodySize, SmallSize, LineHeight, LineSpacing, ResultHeight, CardGap, ReadOnly](CUIRect Content) {
			const std::vector<SQmGlobalSearchCard> &VisibleCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
			if(VisibleCards.empty())
			{
				DoSettingsMenuLabel(SETTINGS_SEARCH, -1, -1, "qmclient-search-no-matching-features", &Content, Localize("No matching features found. Try other keywords"), SmallSize, TEXTALIGN_ML, {}, (int)Content.w);
				return;
			}
			for(size_t Index = 0; Index < VisibleCards.size(); ++Index)
			{
				const SQmGlobalSearchCard &Card = VisibleCards[Index];
				CUIRect ResultRect;
				Content.HSplitTop(ResultHeight, &ResultRect, &Content);
				const SQmGlobalSearchNavigation Navigation = ResolveGlobalSearchNavigation(Card);
				const bool Clicked = !ReadOnly && Ui()->DoButtonLogic(Card.m_pStableId, 0, &ResultRect, BUTTONFLAG_LEFT);
				if(Clicked)
				{
					NavigateToSettingsCard(Card.m_Target);
					Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
					m_GlobalCardSearchInput.Deactivate();
				}
				CUIRect ResultContent;
				ResultRect.Margin(std::max(4.0f, LineSpacing), &ResultContent);
				CUIRect Row;
				ResultContent.HSplitTop(LineHeight, &Row, &ResultContent);
				DoSettingsLabelStreamed(SettingsTextElement(SETTINGS_SEARCH, -1, "qmclient-search-global-card-title"), &Row, Card.m_Title.empty() ? Localize("Global card") : Card.m_Title.c_str(), BodySize, TEXTALIGN_ML);
				ResultContent.HSplitTop(LineSpacing * 0.3f, nullptr, &ResultContent);
				ResultContent.HSplitTop(LineHeight, &Row, &ResultContent);
				DoSettingsLabelStreamed(SettingsTextElement(SETTINGS_SEARCH, -1, "qmclient-search-global-card-destination"), &Row, GlobalSearchNavigationLabel(Navigation), BodySize * 0.9f, TEXTALIGN_ML);
				if(Index + 1 < VisibleCards.size())
					Content.HSplitTop(CardGap, nullptr, &Content);
			}
		};
		ResultsCard.m_MeasureRevision = SearchMatchedGlobalCardCount;
		vCards.push_back(std::move(ResultsCard));
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = !ReadOnly && Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = CardDeck.RenderCached(SearchCtx, Page, "global-search", DefinitionsRevision, BuildDefinitions, CardOrderModel, ReadOnly ? nullptr : &s_GlobalSearchScrollRegion, InputState, SettingsCardMotionSpec(), SettingsCardDeckVisualOptions());
	if(!ReadOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)
{
	using namespace qm_module;
	const bool UseNewUi = g_Config.m_QmNewUi != 0;

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
		Ctx.m_pIconManager = GameClient()->QmIconManager();
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
	if(ContributorsPage && !PrewarmOnly)
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_CONTRIBUTORS;

	auto IsQmNewFeatureMarkRead = [](const char *pId) {
		if(pId == nullptr || pId[0] == '\0')
			return true;

		char aNeedle[128];
		str_format(aNeedle, sizeof(aNeedle), ";%s;", pId);
		char aMarks[sizeof(g_Config.m_QmNewFeatureMarksRead) + 2];
		str_format(aMarks, sizeof(aMarks), ";%s;", g_Config.m_QmNewFeatureMarksRead);
		return str_find(aMarks, aNeedle) != nullptr;
	};

	auto MarkQmNewFeatureRead = [&](const char *pId) {
		if(IsQmNewFeatureMarkRead(pId))
			return;

		if(g_Config.m_QmNewFeatureMarksRead[0] != '\0')
			str_append(g_Config.m_QmNewFeatureMarksRead, ";", sizeof(g_Config.m_QmNewFeatureMarksRead));
		str_append(g_Config.m_QmNewFeatureMarksRead, pId, sizeof(g_Config.m_QmNewFeatureMarksRead));
	};

	auto MarkQmNewFeatureHovered = [&](const char *pId, const CUIRect &Rect) {
		if(!PrewarmOnly && !IsQmNewFeatureMarkRead(pId) && Ui()->MouseHovered(&Rect))
			MarkQmNewFeatureRead(pId);
	};

	auto AnyQmNewFeatureUnread = [&](const char *const *ppIds, int NumIds) {
		for(int i = 0; i < NumIds; ++i)
		{
			if(ppIds[i] != nullptr && !IsQmNewFeatureMarkRead(ppIds[i]))
				return true;
		}
		return false;
	};

	auto DrawQmNewFeatureDot = [&](const CUIRect &Rect) {
		CUIRect Dot;
		constexpr float DotSize = 6.0f;
		Dot.w = DotSize;
		Dot.h = DotSize;
		Dot.x = Rect.x + Rect.w - DotSize - 3.0f;
		Dot.y = Rect.y + 3.0f;
		Dot.Draw(ColorRGBA(1.0f, 0.12f, 0.16f, 0.95f), IGraphics::CORNER_ALL, DotSize * 0.5f);
	};

	auto BuildQmFeatureLabel = [&](const char *pText, const char *pId, char *pBuf, size_t BufSize) -> const char * {
		if(!IsQmNewFeatureMarkRead(pId))
		{
			str_format(pBuf, BufSize, "%s [new]", pText);
			return pBuf;
		}
		return pText;
	};
	auto MiniFeaturesNewFeatureId = [&]() {
		if(!IsQmNewFeatureMarkRead("qm_2_70_0_chat_context_spectate"))
			return "qm_2_70_0_chat_context_spectate";
		if(!IsQmNewFeatureMarkRead("qm_2_69_0_chat_context_menu"))
			return "qm_2_69_0_chat_context_menu";
		if(!IsQmNewFeatureMarkRead("qm_2_66_0_editor_collab_4p"))
			return "qm_2_66_0_editor_collab_4p";
		return "qm_2_63_0_new_ime";
	};
	const char *pSkinTransitionAnimationFeatureId = "qm_2_72_0_skin_transition_animation_toggle";

	{
		if(m_QmClientSettingsTab < 0 || m_QmClientSettingsTab >= NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;

		CUIRect TabBar, Button;
		const SSettingsSubTabLayoutFrame QmClientSubTabs = ResolveSettingsSubTabLayout(MainView, QmClientUiScale);
		TabBar = QmClientSubTabs.m_TabBarRect;
		MainView = QmClientSubTabs.m_ContentRect;
		const float TabWidth = TabBar.w / NUMBER_OF_QMCLIENT_SETTINGS_TABS;
		static CButtonContainer s_aPageTabs[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};
		apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize("Visuals");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize("Functions");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize("HUD");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_CONTRIBUTORS] = Localize("Contributors");
		apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize("Config");

		{
			CPerfTimer StageTimer;
			for(int Tab = 0; Tab < NUMBER_OF_QMCLIENT_SETTINGS_TABS; ++Tab)
			{
				TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
				const int Corners = Tab == 0                                    ? IGraphics::CORNER_L :
						    Tab == NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1 ? IGraphics::CORNER_R :
												  IGraphics::CORNER_NONE;
				const char *pTabName = apQmTabNames[Tab];
				char aVisualTabName[64];
				if(Tab == QMCLIENT_SETTINGS_TAB_VISUAL)
				{
					const char *pNewFeatureId = "qm_2_62_8_visual_tab";
					const char *apVisualFeatureIds[] = {
						pNewFeatureId,
						pSkinTransitionAnimationFeatureId,
						"qm_2_62_8_weapon_animation",
						"qm_2_62_8_weapon_switch_scope",
					};
					if(AnyQmNewFeatureUnread(apVisualFeatureIds, (int)std::size(apVisualFeatureIds)))
						DrawQmNewFeatureDot(Button);
					pTabName = BuildQmFeatureLabel(pTabName, pNewFeatureId, aVisualTabName, sizeof(aVisualTabName));
				}
				else if(Tab == QMCLIENT_SETTINGS_TAB_FUNCTION)
				{
					const char *apFunctionFeatureIds[] = {
						"qm_2_63_0_new_ime",
						"qm_2_66_0_editor_collab_4p",
						"qm_2_69_0_chat_context_menu",
						"qm_2_70_0_chat_context_spectate",
					};
					if(AnyQmNewFeatureUnread(apFunctionFeatureIds, (int)std::size(apFunctionFeatureIds)))
						DrawQmNewFeatureDot(Button);
				}
				else if(Tab == QMCLIENT_SETTINGS_TAB_HUD)
				{
					const char *apHudFeatureIds[] = {
						"qm_lyrics_phase1",
					};
					if(AnyQmNewFeatureUnread(apHudFeatureIds, (int)std::size(apHudFeatureIds)))
						DrawQmNewFeatureDot(Button);
				}
				const bool ClickedTab = DoButton_MenuTab(&s_aPageTabs[Tab], pTabName, m_QmClientSettingsTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f);
				if(!PrewarmOnly && ClickedTab)
					m_QmClientSettingsTab = Tab;
				if(!PrewarmOnly && Tab == QMCLIENT_SETTINGS_TAB_VISUAL && (m_QmClientSettingsTab == Tab || Ui()->MouseHovered(&Button)))
					MarkQmNewFeatureRead("qm_2_62_8_visual_tab");
			}

			char aTabExtra[96];
			str_format(aTabExtra, sizeof(aTabExtra), "tab=%s contributors=%d", QmSettingsTabName(m_QmClientSettingsTab), ContributorsPage ? 1 : 0);
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
	static bool s_SponsorQrTextureTried = false;
	static bool s_SponsorQrTextureReady = false;
	static bool s_SponsorQrDecodeFailed = false;
	static IGraphics::CTextureHandle s_SponsorQrTexture;
	static const char *const s_apSponsorQrPngBase64[] = {
		"data:image/png;base64,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AAAAAAAAQAYjAAQAAAAAAAAyGAEgAAAAAAAAkMEIAAEAAAAAAIAMRgAIAAAAAAAAZDACQAAAAAAAACCDEQACAAAAAAAAGYwAEAAAAAAAAMhgBIAAAAAAAABABiMABAAAAAAAADIYASAAAAAAAACQwQgAAQAAAAAAgAxGAAgAAAAAAABkMAJAAAAAAAAAIIMRAAIAAAAAAAAZjAAQAAAAAAAAyGAEgAAAAAAAAEAGIwAEAAAAAAAAMhgBIAAAAAAAAJDBCAABAAAAAACADEYACAAAAAAAAGQwAkAAAAAAAAAggxEAAgAAAAAAABmMABAAAAAAAADIYASAAAAAAAAAQAYjAAQAAAAAAAAyGAEgAAAAAAAAkMEIAAEAAAAAAIAMRgAIAAAAAAAAZDACQAAAAAAAACCDEQACAAAAAAAAGYwAEAAAAAAAAMhgBIAAAAAAAABABiMABAAAAAAAADIYASAAAA",
		"AAAACQwQgAAQAAAAAAgAxGAAgAAAAAAABkMAJAAAAAAAAAIIMRAAIAAAAAAAAZjAAQAAAAAAAAyGAEgAAAAAAAAEAGIwAEAAAAAAAAMhgBIAAAAAAAAJDBCAABAAAAAACADEYACAAAAAAAAGQwAkAAAAAAAAAggxEAAgAAAAAAABmMABAAAAAAAADIYASAAAAAAAAAQAYjAAQAAAAAAAAyGAEgAAAAAAAAkMEIAAEAAAAAAIAMRgAIAAAAAAAAZDACQAAAAAAAACCDEQACAAAAAAAAGYwAEAAAAAAAAMhgBIAAAAAAAABABiMABAAAAAAAADIYASAAAAAAAACQwQgAAQAAAAAAgAxGAAgAAAAAAABkMAJAAAAAAAAAIIMRAAIAAAAAAAAZjAAQAAAAAAAAyGAEgAAAAAAAAEAGIwAEAAAAAAAAMhgBIAAAAAAAAJDBCAABAAAAAACADEYACAAAAAAAAGQwAkAAAAAAAAAggxEAAgAAAAAAABmMABAAAAAAAADIYASAAAAAAAAAQAYjAAQAAAAAAAAyGAEgAAAAAAAAkMEIAAEAAAAAAIAMRgAIAAAAAAAAZDACQAAAAAAAACCDEQACAAAAAAAAGYwAEAAAAAAAAMhgBIAAAAAAAABABiMABAAAAAAAADIYASAAAAAAAACQwQgAAQAAAAAAgAxGAAgAAAAAAABkMAJAAAAAAAAAIIMRAAIAAAAAAAAZjAAQAAAAAAAAyGAEgAAAAAAAAEAGIwAEAAAAAAAAMhgBIAAAAAAAAJDBCAABAAAAAACADEYACAAAAAAAAGQwAkAAAAAAAAAggxEAAgAAAAAAABmMABAAAAAAAADIYASAAAAAAAAAQAYjAAQAAAAAAAAyGAEgAAAAAAAAkMEIAAEAAAAAAIAMRgAIAAAAAAAAZDACQAAAAAAAACCDEQACAAAAAAAAGYwAEAAAAAAAAMhgBIAAAAAAAABABiMABAAAAAAAADIYASAAAAAAAACQwQgAAQAAAAAAgAxGAAgAAAAAAABkMAJAAAAAAAAAIIMRAAIAAAAAAAAZjAAQAAAAAAAAyGAEgAAAAAAAAEAGIwAEAAAAAAAAMhgBIAAAAAAAAJDBCAABAAAAAACADEYACAAAAAAAAGQwAkAAAAAAAAAggxEAAgAAAAAAABmMABAAAAAAAADIYASAAAAAAAAAQAYjAA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9ZoWIoFNLp06f129/+Vp988ona2tpua1CM9bmzc+dO/fSnP9X27dvnZRs2AAD3EgJAAABuwTRNdXR06Pe//73+9Kc/qbW1VZFIRA6HQ+Xl5dq7d69+8YtfaMWKFTPut2ZdeDocjusulq37q6iouC5cma1KoXA4rCNHjujjjz9WZ2fndUHP2NiYTp06pc8++0xr166dceXY7XI4HFqyZIleeukl9fb26uuvv75pW2Rubq42b96sJUuWzOp9W4/HNE01Nzen3ZIaiUR0/Phx7d+/X8uWLVN5eflt9wazQsa///u/12effXbdcAKr+tFmsyk/P18NDQ1pw7TJPdysCtFIJDJtAGL1NYzH46nXzq3WPbnicrrHFIvFUrc7E8lkUn19fWptbZ2276PNZtPSpUvV0NAw6xNYJz/2uaq6y87O1pYtW3TgwAFduXLluufH5XJp0aJFWrdu3V33N5xOJBLRuXPn9I//+I9699131d/ff9N7/MSJE/r000+1fv161dbW3tFaDMNIVT9PDsclqays7KZzPtWfn4lYLKb+/n598803+v3vf6/9+/drfHx8xq89m82mrKwsLVu2TH/1V3+lH/zgB1qyZMmMB40AAJBJCAABALiFiYkJff3113r99dd19uzZ6y7sx8bGFAqFVFpaqsrKShUUFNzWbU8VRlhhzeSfzdZ2wVAolJrEO9VFdDgcVkdHh/r7+6cNpe7E5CAqOztbjz32mGKxmHJycnTmzBmNjY3JMAx5vV499NBD+vGPf5wKE2abNUQgFAqlPWZkZEQnTpxQa2uriouLb2u7onUfp06d0okTJ27aUmuapkZGRvT5559r/fr1Ki0tVV5eXtrbunFAg8fjmXbrdyKRUH9/v/r7+xWPx285sMPqrdbV1XXTAIsb1+FyueR0Omf8WoxGo2pvb1dLS8u04aLL5VJdXd2sDeWYybmcTW63W48//rja29v17rvvqre3V/F4XG63W9XV1Xruuee0bds2+Xy+1DmfrbVYlX/nz5/Xa6+9pg8//FADAwNTnu9QKKRr165pYGBAtbW1C3KOb3zsN/57MplUIBDQpUuX9NFHH+n999/X+fPnp31t3sjlcqmkpETbtm3Tj370Iz355JPy+/1zVn0JAMC9jgAQAIBpmKap0dFRnT17Vl1dXTddUFtBktVn63YDwNu5EJ2Ni9ZoNHrdlr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FY1uNsNhtqa2tRUFCwqm2r6c/HarWioqICHo8H/f39Gff5k1JidHQUIyMja/A05zKbzaisrERFRUVqrYqisO2XiIiIXigMAImIiOiFkgz81lsApKoq3G43VFXNGKoJIWAymeByuWCxWNZsnZqmweFw5JwCDAB2u31NW6rTrdfXnIiIiGi18E9BRERERGtMCAGr1YqNGzfmDNacTidKSkpgNpvXrGpRVVUUFRWhuLg4a6CW3H+vurp6TdZIRERERHMxACQiIiJaY1JKmEwmNDc3w+v1Zgz3VFVFVVUVGhoacu6/t9JUVUVlZSU2bdoEp9OZ8RiLxYJNmzahpKRkzdZJRERERI8xACQiIiJaY8n21C1btuDgwYNwu92p6johBFRVRVlZGQ4ePIiNGzfCbDav2VqTU3SPHDmCbdu2wWazpQLLZJtyU1MTXnnllXXTAkxERET0ouMegERERERrSEqZ2qOupqYGP/7xj+F0OnHlyhX4fD",
		"4oioLy8nIcOnQI7733HqqqqqCq6pqu2W634+DBgwgGgygvL0dnZycCgQAsFgvq6urw9ttv48iRIzCZTGv9eImIiIgIgJBSytW4kK4bmJzypX5ts1rgcPBfhYmIiIjShUIh9Pf349GjR5icnEwFgLW1tSgrK1vT6r90uq5jenoavb296O/vx9TUFJxOJ8rLy9HQ0ICioiIO3SAiIiJKEwyGEI5EU78uLPBAVVfnz0sMAImIiIjWGcMwoOs6dF1PtQAripKqFFwvpJRz1ppcZ3qF4npaLxEREdFaWssAkC3AREREROuIlBKKokBRFJhMplSLcPJz60kynFRVNbXO+T8SERER0dpjXwYRERHROpMe9KWHf+stUMu0ToZ/REREROsPA0AiIiKidSZTeLYeA7Vsa1qPayUiIiJ6kTEAJCIiIlpHGJ4RERER0XJjAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaAREREREREREREeYwBIBERERERERERUR5jAEhERERERERERJTHGAASERERERERERHlMQaARERERES0roVCIfj9fkgp13op9BQMw0AwGIRhGGu9lLwUDofh9/v5fIkoJ22tF/AiMQwDhmFA07I/9kQiAUVRoCjPTzYrpUzdm2EYUFU15z3S+pB83YQQS36/Jf+A4XQ6oarq/5+99w6L4tzf/+/ZZXdZekc6UgSkqSgIoihWUBTF3rvRxJwk5iQ5Sc7npHlOTI8xxq6xa+yxYQENioJKURFBepHe27Jtfn/82Pk6zgK7sNTs67q8Ep6dnbYzT7nfracvpdPU19dDLBZDV1e3X1yPmp6ltrYWRUVFaGpqotpMTExgaWnZJ/p2kiRRW1uL1NRU8Pl8uLq6gsfj9fRp9WlIkkRdXR2am5upNjabDR0dHXC53J4+PaWRSqUQi8XQ0NDosmeaJEmIRCKIxWJaO4fDgYaGBgiC6OnboOY1hEIhbt68CSMjIxgbG8PAwAB6enoqe8YfPXqE+/fvY8CAAXBwcICFhQXMzMygo6PT05euRgEKCwtx/vx5GBgYYNCgQbCxsYGpqenfZs0gFApx69Yt6Onp0d4PVY2vSUlJiI2NhbGxMRwcHGBpaQlzc3Po6ur29KWrUaOmF/H36HF7GIlEgvz8fDx69AhWVlYYOXKk3O1ycnJw/fp1jBkzBi4uLj192gqRmpqK6OhoVFRUQCKRQCqVIiIiAp6enq0uCgQCAUpLS2FkZNSlkzaSJFWyQBCLxXj06BFKS0thaGgIfX19GBsbw8TERGWTWqFQiMrKSupvNpsNXV1daGpqdtn9qaioQExMDOrq6uDi4gI7OzuYmJi0OxETCoWIjIzEw4cPYWlpCQcHB9ja2sLZ2Rl8Pr/LzrcruX//PmJiYuDh4QF/f39YW1urdP91dXV49uwZqqurqTZbW1u4ubn19KWrUTGyCf7Zs2dRX19PtdvZ2eGjjz6CiYlJT59iq5AkierqaiQmJuLOnTt48OABbG1tsXHjRri6uvb06fVpBAIBIiMjcffuXarNzs4OYWFhcHJy6unTU5iqqi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kYOnQord3IyAh2dnbtCoAsFosSTKdNm9bhe+/o6IgNGzZg/vz5YLFYCk84hw0bhmXLlmHv3r3IyclReRgTQRDUc/H222/D399f4cGkt1cjIwgCEydOxKRJ/197/x0lSXqf957PGyZ9lm/ve7y3wAw8ZuAIJwIkSBCQBBISAVF3JXGNzu5Zac8e7RGv7p6rQ+3RXrl7V6REgiRAirwkaOFEeMINgJnBeNPd095Ul8tKFxkR7/4Rka58dVd3VWV/P+fUZGZkZGREVGVO55O/9/29e0O2d+DAAf3gBz8gAMSWFUWRnnnmmUWd7lzX1X333adPf/rTOnz48Jq2dejQIR0+fHjJxh7q+YbedV35vt+ZPuDd73633v/+92vXrl0b9s36G9/4Rn3+85/f7NO7Lk888YQ+8YlPXFXH3Wvhne98p+6///4tFwBezeT24+Pjeuc736kf/OAHnSAvl8tpZGREIyMjKpfLKpVKKhaLKpVKnYCuHd",
		"r1BnYLf652GGg75AuCQE888UQnBCyVSpqZmdlyfx9X4tChQ+sKANtfFBQKBY2NjekXf/EX9dGPfnTdIWImk9Htt9+u++67T0888cSS6+TzeQ0PD2tkZEQPPvig3vGOd+jBBx9cNO0OcK0cOHBgXXN2u66rXC6nYrGocrmsX/zFX9Tf/tt/e0Oa//V6/PHH9fjjj1/Xc5HL5fSud73ruj7n9dBoNHTixAlNTU31VaPncjn9yq/8yorz5t900006dOiQTp06tei+3mpl3/eVy+V077336uMf/7geeuihG7aZ1KAiANzGyuWy/t7f+3t6z3ves+6hHbt379av/dqv6Xvf+56+853v6IUXXtDc3Fxn2OF6/sHe/gdr+41jdHRU9957rx599FE99NBDa/4fSTsgupaMMdq3b59+9Vd/Vf/23/5bvelNb9LHPvaxZQNDY4weffRRPfvss0ve77qustmsisWi3vCGN+hTn/rUouMdGRnR7bffvmQA2P5Qm81mNTo6qscee0wf+tCHtHfv3qs+1tUamCzkOI5+4Rd+Qbfeeqs+//nP6+mnn1aj0VAYhlf8Aa79P5T2t2z333+/3vOe9+iee+7Z8H9gALh+fN/XT//0T6vVauk//If/oFOnTskYo5tvvln/4B/8Az300ENr7mJ5xx136JOf/OSiMLHNdV1lMhmVSiWNj49r79692r9//zX5/0X7C6ztpFgsbsn5w3bu3LnuSvatbseOHfpn/+yfKYqivk6MW6EywnEcPfDAA/rUpz6lubk5vfDCCxoaGtKHPvQh/eN//I+valjbVrFr1y498MADS3ZgbuuttBweHtbNN9+su+++W4888siy/9Zbiz179uiBBx7ozPtsjFG5XNbY2JhGRkZ077336i1veYvuv/9+7d27d0u+JjHYdu7cqfvvv1+jo6PL/ru99/UxNDSkm266SXfffbcefvjhNQ81xuYpFAr6yEc+olqtps985jO6cOGCHMfRO97xDn3yk59c8bF33323PvnJTy45DLgdBg8NDWnHjh06cOCAdu/ezWe1AUUAOACudF6XUqmkd7zjHXrHO96hIAg0Pz+vIAiuaLhnO+Rpf5O0Ff4xvBzP8/T6179e//pf/2vt2bNnxXkujDF6xzveseQHwt7grj1h7lLfvI2NjelnfuZn9NBDDy3adnsb4+Pj2rNnj8bHxzf13Bhj9NBDD+nBBx/UzMyMpqenVa/Xr7hboeM48n1fxWJRo6OjKhQKW/pvY62y2aweeuihTmWiMUa33Xbbhm2/WCzq3nvv7RtGsN07smH7cF1XExMTffOZ7t69e1HVTPsfoqVSSf/xP/5H1Wo1/dIv/ZIee+yxdVUhHDp06Iom5Ecim83qlltu6avw2r9//zV5r3UcR3v37u2bb3jv3r03zIcE13XX/eXa9eT7vt72trfp4sWL+oM/+AM98sgj+uVf/mXt3r37qgIpz/N0+PBhveUtb+ksu/XWW6/7kNZsNqtf//VfX/Xfqe0vDXK53Ia9Dtoh30033dQZ3nv33XfrwQcf1F133aU9e/YMxL9vtirP87Rjx46+0UpDQ0NbImh1HEcHDx7se30cPXp0Q4aUrkcmk9Gv/dqvrTqdk+M4ymQynXmxsb2MjY3pE5/4hBzH0Wc+8xnt2bNHv/qrv7rqlzyHDx9e88gMDDZjr9PYjCiKNT3Tnd8nn8uqWFzbkFUA2EqiKFK1Wu37EOJ53oZ9GAqCQHNzc31zdBaLxev+j0msXxzH+uIXv7io83e5XNY//+f/fFsMo6hUKnr++ec7Xdnb+//www8v+Tder9f1rW99SxcvXtR73/veLR2QDKLLly/rqaee6vtgPDY2pjvvvHPD/96CINCPfvSjvqYto6OjuuWWW7b8VA03ksuXL+uFF17QkSNHtGfPnqsOSeI41tzcXN/UIJ7ndTrY3ihOnjypp59+Wjt27NDRo0c1Nja25kpnXJ3jx4/rq1/9at+X0fv27dOb3vSmTZ9b0Vqrubm5zhx3Sl8fQ0ND2+L/+dvJ6dOn9dnPflbPPPNMZ9nhw4f16U9/esMbHW117SaTd9xxhx599NEb5ou4QVGt1lRvdDvaj44My3WvTyBPAAgAwAZqtVpLfgOfy+W2RLXCtdDuiEknXgAAcC0EQaCpqam+L8g9z9PExMQNGYDVajVls1m+iNiGNjMAZAgwAAAbyPf9Gy4Ic12Xf4ACAIBrJpPJLNnl9ka11gagQC8G/gMAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRg",
		"AIAAAAAAAADDBv/uyX5Lh5OV5Rxiuk1wsyXkHGzcoYV5LZ7P0EAAAAAAAAsIiVbKQ4asqGNcVhTXFU67lelzf13P9XbmZYTmZEbs+PkxmR45fleMUkEHRzcty8jJuT8fLJdScjGYoIAQAAAAAAgGvGxrJxU3HUkA3riqO6bPt6XFfcqipuzSkKZhUHM4p6fuJgTl4cNWQbgdScSqr9jJteOjJORo5XSIJAf0iuX5aTGZbjl+X6Q52qwSQczCYVg05OjpeTcbIEhAAAAAAAAMAqrI2lOJCNGorjZhLuRcllHDVlo7qiVkVxa15xMJuEfa2K4tac4lZFcViTjQPJRsm2bCRro85tTzZO7lAou+jpTRIGOq6M8dJLX8a4Mo4v42aSCkF/SE6mHRKmQaE/JMcryXg5GSejWJ7iZpCGgr5sLMlmJeNu9jkGAAAAAAAArp20gs+G85INpLilVnVakULFcSAbNhSH80mY15pTHMwpaod7rTlFYVU2aiaBXtyS4kjWhrI2lOIwzfbssk/vrbJ3yYaiUFbNxXebNCA0XhIIGk/G8STHkzFeUhXoleRmhmX8IYVRTsYbkvHLCsNxBXYiGVJs/PQxSbCo9naMJ0MFIQAAAAAAALYoa62MbBLM2UhxHEo2lOKWrI1kbUs2aiisTCqqXpYNK7LhnOanG4pbc7LBjKJWNQkI24Fe3BvutdJqPnvF++hd8SOTI0x2RqFs1FhihXYFYRIIWuNISsK9luOr5mXl+KVk3sHsmNzMmJzsqLzMmNxsct1x82mVoEmGJRsjqX1pGGIMAAAAAACAayyWtTYN4eIkE0svZSNFUU1xc0ZhMKWoOa24OaUomFLUnErm4QvnFbeaiuNWJ9gLTSwbt0O+aMUKvqt1dQHgqnorCHuXJiIZGaengtDxkyHCjivjZGSMnwSE/nASEObG5GZG5WbG5GZHk5DQLyePAQAAAAAAADZa3FLUmlPUnFbUnFKchnxhGvLFzZmk624UpNV+Lcm2FMfJpY3DdC6+/oDv2sV9i13jAHA1NjkJnQpC0393OsTYOJmkwYibleNkJCfbbTri5uX6RbnZUTn+sJzMcNLV2B+W64/IyQylVYRUCgIAAAAAAKCHjRVH9XTOvRnFwayi1mzSaCOYURjMKG7Ny4aNZIhu1EybdARJk464KRsHywzRvZ4R38o2OQBcaMGJsVbWxskY6rC69EOMI8fNyDh5OV4+6Ujs5aX0tuPl5bgFOX5RjleW65dl/LRxiV9Km5UUZJxsOqRYydDlZON9ywAAAAAAALCV2DROskmh2YJlNg7S4bfzioM5xdG84qDS03BjXnFYUxw2ZOO64rAuGzcUhzUpaioKq+lcfvFmH+hV2WIB4BWwseKwIamhKJhechXj+GkYWJTjFWW8YhIKZspyvEKyzC0kQaBXSJcV5Ljt2/mkitDxe4JAk1Qotm+35yQEAAAAAADABrCdqjqbBnzdKrsk3LNREtbZsK44qqVhXk02vYyjanK9VU1Cv7DndlRNA7/WZh/oNbf9A8A1sHFLUdBSFMwtvYJx5DgZGb+UzDnoleX45aRC0CvLzZST0NArJhWGbi69TCoOjZeX42TTjsbu4jDQ9AaDhIQAAAAAAOBGZ3suFneOsHGYVOJFjWT4bZRej+pp6FeXjWqKWvP91XyttLovqCgOK+nw3K0zFHez3BAB4KpsrDhqSFFDUWNSS+W+xjgybk6uPyTHH5KbGUrnHBxKlmWS5Y5bkHEzSRjoZmScbPLjZpKQMG1yQhAIAAAAAABuREkX3EBx1Ezn0Qs6c+spnVsvjmrJkN3WnKJWehnM9i+LGtI2H5p7vRAArpG1sRTV1YoaMo1LnSHA3aHATlJJ6OVl/HJPUDjcExgOyfHK6VyF2Z6ux37a3CS5nSz3CAkBAAAAAMD2Ya2sDWXjlmwcJHPnxUH600p+ombSdKM1pyioKE7DvaT5xlxnuY3qko1lFfcM/bWy1sqkl1g7AsB1sLZ3Qsn+CtX21TiqywSzCo2bDgd2007GjmRcyXhy3Kwcr5QOMS6lw43LSaMSv9wNCZ12SOin4eCCH+PT3RgAAAAAAFx7aZPWzo/tXldneVNxWE+H4M7JhvOK2kNyW+lPOK84akpxmBRb2SjtoBt1rie3l6/sI/pbPwLAjWbjtDNMa/k/SOPIGC+p8nM8GZNU/BnHk4yXBnzZToOSblBY7AkOi3L8YtK9uOfxxnhpWJheN54MQ44BAAAAAMCSbBK4xWFavRemXW/DdB6+9HbcTJpqtEO8cL6nsUYS7tmwrjhupo9rSbb3smf7DNu97ggAN4ONZW1SArusnpBwURWg6VYBOl5exivL7YSEpZ5mJiUZr5R0N3a8pBpRbhIIGicJBztVig7VhAAAAAAADAybDsmNeqrs4gW3I9k4TLvm9lToteYVdUK9pIrPpt1ye6v/FIc9w3uTgI9wb2siANyq1hISSmmw58tZZpiwnKT5SGd4cXt+wsyQHG8ouZ0ZSoLDdhdjmUVzHEpGpm85AAAAAADYHLYzTVnv/Hi98+TJhmm33KQzbhRUFIdzioN2U41K2jG32m3E0TvEtz2817ZkoyAJDLFtEQBuczYOJYWKovqy65h2tZ+bkUkDQXWuZ2TcTBIUevnuEGN/SK5fThqXpJeun8xXKCebbBMAAAAAAFxfNk4aaYTznaYZ7TAvaod6rbmkei9qpENykxCv03k3bcaRFB6F3QARA4sA8AaQTKqZJPlSVZFR35yApn3d8ZIuxG5WjpNNKgLdbHq7fT0nx8vJ8YoyXlGuX5LxismchO35Ct3kMpmfkGpBAAAAAABWZW0S1oVJVV4yv177J5lvLxmmW5ON6kmQFzUVRw0pbqTBXno7aiqO0rn3FgZ7fd1zCf1uFASANxTbc2EXLpWiKPlGIJxX1B7u2w4IO6GhSeYldLMybl6Om0/mIXRz6e0kIJSTT5qYuHk5fl7GTa4bryDj9dzn5WWcHPMPAgAAAAAGk40VR40ktAvrslEtaZYR1hRHyaXtXDYUR+llWJftPC69jOrpcN2wZ/ivkmG/nU/3dtHnfoAAEAvYpQPCnvcNGwdSWJc0kyzoq/Iz3djQ8ZKwzy0mjUj8ohy3kHYzLshxizJeQY5fkOmpOHTcXHo7XeZk5aSViEln46WqCqk0BAAAAABcC0sEadYmHW3bVXhxMw3rulV4NmzKxo20Sq8m26ql1Xw1xVFyaVtVxVGyPGmyEaZh3oLntXb5fQHWgAAQV2ipN6L+e2wUSlFDkaZX2E7SVMTx8nLcktxMOelc7Jfl+kU5XjmdkzDpcpxUGWbThifpPIZuRtb4ctxMt/mJ8dMOxys8LwAAAADgBrRMiGatpDjpZhsHPU0xAiluKQqDdHqt5CcJ8GqKw0qn0Uane24wLxtWFLXmZaN6MjUXsIkIALHJki5FcSuZ3yBsXFhhXSPHzXSakjj+kNzMcLdJSWao0+k4CQtzMsaTcdwkEHTcpILQ8dPlXhISOh6BIAAAAAAMLJuEejaU4lA2jpK58WyYVNy177Oh4qihuDXfaaQRdRpqzCkK5jrX41ZFcRwsKogBtioCQGwjVnEUyMZTCptTMnLS4cDp3ITG6VQUyrhJsxK/LMcrpx2Nk+7GTibtcOwlYaHxiklVoXHTqkE3DQ297m3jSk56SVgIAAAAAJvLWllFUhxJNpJNf5JgL1me3I6SIbmtardLblhRHMzJhhWF7a65QUW2VUmG7tpIUtwJ95LqPdspYLGKk0+FhH/YRggAsc1Y2fabsOL+kcgL1ozDeZnmdCfAa4d5nSCvvdzx0+7G7U7G3Q7HjluQ8Uty067Hxs3LuJm+7XWrCJ1uVWHnPpqbAAAAAMCa2LgntIskRUmFnu0J89rXoyCdO68mm3bMjcL55PqCDro2aiRDeXu2oXZIqPS5OsvXNlSX6A/bDQEgBpeN029qWiu/ORuThnbpsODOEGE/XebLOJ7keDKmHRbmk4YmPV2NjZtPQ8R0edrUJGlc4vRXGPbddjqXVBcCAAAAGBy2J9TrCfds3A3c4qTaLmmoEXQ74raqScfbsCYb1RT1dsoN60mlXtxKKv7iVs",
		"8Q3/b1Vmdob/K8RHa4sREAAmn3JimUjVZb2XSrBtsNR5xM3+2kQjC97mXlOHmZdlDYFxomt42XT5ub5NJhx04SCspJg0MnqS40pnPbpMsAAAAA4LrqFFrE3aYZNl6wvH076ZLbCfXCetINN6wl19PlNqwlYV/clI1aydx6cSsN9lqdRhxJqNe+HVGHB6wDASCwLu1W76EU1Vddux0Wysl0g0E3k3YwTjsZu73Xc50KQsdtVxV2Q0OTLm9XHCZBYToPolG3gjCdG9H03RYVhgAAAAD62WRuu/ZES73LFt62NpZNA7worPeFd3HYHo5b64Z87aG3UbM/yGv/REmXXbW77Xbm2gOw0QgAgWvI2kg2iqSoIbVWW9t0KwidjIyTleMml51lbrbnMiunXT3oFeS4+e5w5HZ1oZvrVBkmFYYMMwYAAACQSObSa6SVeI00yKt3QrxkCG5atdeqy8Z1xVEzCfSiQNYG6e0grd5LL+MgWceGDL0FtggCQGDLsOk3YS1J1Z6qPXUr+foq/YwcNyM5WTlutjvnoJOVcdNl6fXkdi69nUuDwVznMd3r7eXpesbv2w8AAAAAW5XtVNvFUSMJ9qJGGuwll+37bPsnbqYBXqMT3sXpekmg13M7SoI9KW4/XU/loHoq92znPgBbBwEgsOX0ltn3LVl0I4pbkqklTejTYb9aMCTYtIcDG0cyvoyXS6sLu0FfEhamIaCX7b/tZNKAMdMNFNPQUZ0qxaQqUWkDFQAAAABXyUaK4wXVdWkI1w7nbBT0rNMb8AVpwNdIw8DeYK/RreKLg3S+PtsT5dnuEODO8p5lALYlPqkD21r7f8rL/7+4f7GRWqYvGFRPeGh6g0RjJOPLcbNpw5J2s5J8p4mJcfNpBWE+CRbdfDp8OdPpntxtkJJJwsGepilyfDnGkxyXockAAAAYUFaKI1kb9jW3SDrUBknH2qUaXsRpZV7YruBLO9+2G2aEyZDdpLlGEvYlXXHb8/X1zONn+y8tgR5wwyEABG4otlNZuNRUHEv97z/uCwjVX2nY01ykHR46bkZyC3K9QtK0xCvKuMml47ebmBRlvLxcv5SGiGkHZMdNOyG7MsaVjCvjeMml8dIOyO3lhIYAAAC4vqy1MooVx6GMjRXbUMZGiuOw0/VWNpJsmHSptZFsHEpxQ1G7QUZYTRpmREnTjLhVlW1fD6uddZJtxN0KPC1ozmF7GncQ5gFYBQEggFX0hIZavdIwDmuSZhRKSwd0ZuFyI8fxk0YmflnGK8nxS3K9kpxMWY5XSn789mUxDRALMk5GkpMOb3bSgNBIcpIfJ+2SbBwZObImXRcAAAA3OJsGdkmgZ20sq2QorBZcl+J03ViKW0lw15pPfsJqellR3KoqDucVtyo991eSYC9uLfiHtF3m39WEeACuDQJAANfQcmWG/cvjOFbcbMoEs7JSd3hyT9fidnCXBHlKqgSdbDcQTC+Tn1JacVjqBItJ5WFJxkuGKcsYGTk9Q56dnqHRTt9Q6L71AAAAsIW0K+HiBcNc4+4QWBv3rdcJ8sK6bJiGdmE1Ce6iahrkdS9tet1GVcVRsxsGSrI2lmnPnpc+TxIqJsNsTboOAGw2AkAAm68zLDnqvdm/yhIPM8ZRFEynFYBu/xDhzqWz4NKTcTOduQyNm5fjFWS8nBwnnd+wM99hrud2LmmEYrw0DHT6w0GTVh22Q8R2eGm6VYgAAABYQifAi/sCO9t7uzfks+mwWBvKxkES5EUN2bDenSsvrCmOk/nz2re7l2kzDBsnQ3TTH3Uu487t7vXu5aLdX+qQVrgPADYDASCAbSv5R1u8zn9YmW4YmDYqSeYfTOYadBy/c19n7kEnub8dHiYdktNOyk4m6aTstbsmZ9P7sj230/XaQ5X7gsH+isNkHdMfJnbuBwAA2Iq6w2mTyCsN7Gz3emedvoq8JFSzcasbyvV2qI0asnEzbXDRXRZ37ksaaCRBYJjMvxeHsopko1Y6/14rCe7S+5MmGVHni2cAuFEQAAK4wdjuN7xxsL6HGpOEgMaX3OSy2+04k4aIfk/nY6/T7bjdCdlxs53QsNsxOSPjZtNwccGy3q7Kxu3sR2/35r6uzp2AMR06rbTykApEAACwpP7mEv0hnvor8Trz1nWHuibVeS3ZKFAcB2mIFyQ/UdDTzTa5HkeBFDc7gV4cBWko1+5+2+2Iu3CZ4lYyl55tdQM/S40dAKwFASAArJW1srYlq5a03i+N0+HJyTBiX8bNSMaT0w4O2yFf+iPjy3H9tDoxvS8NCNshonqCQie9LjeTVh32rut3Gqb0dm7u2bl0cf+y7sVSjwEAAFtDb4C3YFnatXbhsv7HtCvwWkm1XRRIC0O7Bbc7IV8n4GsHdUEazCVhne37CaQ4VBwHaWDYvY+BsgBw7REAAsD1kE4Wnfwj90oYGdeXMRk5bqYbGC6sGnQyyXqd62ng2Bcwdq8ngWHP+sZPH99dT44vx8kklYzGo5oQAICtwMbp0NdWEqr1XHZCtyhIvrzsCerUCeoWB3SLQ72g/yfqXk+64TaTIbgEeACw5REAAsC2YJN/dKulOKp2uyMvrNozPdeVDhe2SWdlY9x0OHJPOOj0BIBuRsa0l/s9VYZ+z309w5vT4c6dIc7tuRR7rnfmWDS+jON270vnVgQA4IYTh51579oBnk3nsOufy647zLV/+Gu4OMhbIrSL45bUCQDbFXsLQ72WbBwtXR24oLLQLrzPiuAPALYRPn0BwLbS/w9uu8TdyzwqDQXraffi/jkEu01IFi9rX293MzaOu6Dq0E+GHberCp3eoNDvqyzUwmpE43YasLSbsXSXpQGi3O4ciI6XBIlpiGgct1uV2J4jEQCADZPOixeH6fzBUTrvXNizLOxrNpGEdz3rtdfphHntAG/B0FkbJgGdTav4op5Az7Zkw6asbaVDdHuHzvbO22e7c/n1XNdS91v61ALAjYQAEABuGN1v8pf70n5tHwGWazri9CxPA8XeILFvXdMJ+vqqEHsqE9tDj/uGMruZbsjYqVzsViAmIaAr46Sdntsdn42bdl52pfZ9cjuBZvIYNzkGx5Hk9jyeIc8AsC3YuNNVVoqSxhVxcmltmDSuiONOMzApvR7H3fU7IV97Wbt7bO+cde058hZW3y0YNhu1lhhKm4aFi8K5eOnAzlpZxd1ldsF6AACsEQEgAGCdeqoGbLTgnnXqBIXOou7G3WpEZ/GynjDRtKsUHbcvJDRutjNsudMkpW/oc3fYc2dexSWGQRvH7z6ncZJQUEbGOLIm3ff0dne9hceUruM4PcfkLKjEBIBBF6cNteJOZ1kjKxvHkkm7ybY7zrZDMWtlekIvqygNwtr3x+l2457usc1OMCcbpF1mgwVBXTekU9xMm1y0pKipOK22U7varr1ffcFc3F95l+5PN7RbIcQDAGATEAACADZPZ5hSvCg9vLKPSAurD7XkkGbT2w150VDn3sek3ZvTbsvtH6fTaTnbU7WY7ZtXMQkVk3XldEPG7mW2U+lojNvznOrvvGxMz9GZxd2azRK3bW8j595tibARQI+F3WOtrE3fRqxddp2Fl51JKewy21T6hVHc30G2HcwpChTHzZ5QrtlTWbdweW+g10h+2oFfHPZVxtllhr6299Uus3xR2AcAwAAgAAQADBDbP1n5inMirlFPoGiN0oo/dav6egPEBaGjMb3rdi+NMUo2lq7neEmnZTcrx+0GhsYkgaMcPw0M/Z4GLb6cvi7N7SpHr9O12emZp1FOT3Wk8a4gCDRLXr3ibQC4unDJXs12bKexRHueud756DodZaNAUtJAQnHYrZKLg5512s0pmp2usIqbaUjX7IZ5cToEV1q6es4orQhsh4+966qnqi5O17Uy6TLTrhoEAADLIgAEAGAlnSrFdnHIVQ57XlIaJKaVgJ25B9tzK7aHE/eFjt1lpm9YdPfS9MzP2Dss2Rin05FZTrvJSk/jlrRLc6dRSzq/YtKIxe2ZV9Fd0MjFTRuz9NxnPMlx+rfRs27S0KV/ee956WtuvWSAuGCZMSvf37tsQddso77iyRUev9K2VlgHa2BXuWu1V5xddMv01IOtvC279PZWDN",
		"oWPN+i7Sbzycm255JrN4aIOvPQdeeYizsNI/ruj1d6XO/tbmfZpAlF2NNgIpRs2jSi3Yiid5htz5DWZA492xmu27m0C9frBnJWcRrG9Qzx7QzN7Q75vVZ/LdToAQCwOgJAAAA2XVoJk1awXMsPs8aYdIhfOwx0ZeXI6YSObne+w/ZchcZ01rGSjOPKqqcztJz+oLE3oFR3/kPbqYp0+uZzNAvCy6Qhi+l0eDadfVqisYvjSLa3iUtyPGZhs5dOwNht9OKY9rpuf+DaDlut6QSnyTlYWNXZDmHNorA12U674jM9b+3j1FIhpV09KFxzjrhRgeMa/hLX/Me6XLjW7kYayxgpjpM54dQ351vv3Glx9zFxnA5V7Zk3rrehQu9cbe3mDjZKH5s2gbDtBhELmkLYOJmXTnGncq29vP2Y3sv28thGyXx17W3F6Ws6fd7u/tgl9jvqG4q6cB68TvAmK2ujbtVbb8DWboLRmSevZ196GmSYvg6wAADgRkAACADADaT9oT8JMySrUJIW1DVusk4o6PRVQ7YDyd5wcqnbfRWTndtuN9jrdHte8Nglm9CYxQ1qlpxTcnETG9vX4EYrdMjun8exW4+4TEWjSdayCx5lTe+tnufsuegOPV84T5sW1Mkl/zXtruHpsr5quuWq6BY8pu+yrylC7/xr6mu0YBbd3zNcdMnHL5jHbeHw0rRKLQnLok7TiE61Wxq+2d7l6laxtcPAhfclz9EN23rDt608JJXoDwCAGw8BIAAA2Fps3AlPBjao6Buirf7Az7bniVw+AFzUDEY2rVrUgvtN72Y7wWFyaXvmW2vrCerMwsYPpidgU3e9BY+1nceqf33b2yyC5goAAADXEwEgAADA9WbjjWtUAwAAAKzC2ewdAAAAAAAAAHDtEAACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIB5m70DAACsj5GMI8lK1iaX253p+T5uUI7pqqW/ZxvfwOfDLLi9hc+DcWV6XpdWNv3drZ/jFeQXDymYPy7ZSNbGm/t3YByZnt/F1Rxbz0YlY3p+xxuxzdWPY33vL0bGuEsc99ofL9N+TwMAAJuNABAAsI0Y+aWDyk+8XlFzUq36BUW1c4pac5KNNnvnrviYcmP3yS8dVtSYVFg/r7B+XnFrfmsHPteYl9up0r53qzH9E7WqJ2WjhuKwvvicGCPjZGWcjGxYld22fwcLGFdubqe87Hi6wCpsXFDUmNzsPVtSad97VNrzuOKwrrB2Ss3KMVXPfXXdr0vj+Crseqsm7v6nioIpBZVXVbv4HdXOf1NRMH3N9t84GRk3JymWjUPZuNkJrvITDys3co8kycZNNaZ/osbUU2vZqoybk+Nm0m0GsnEoycov7FNu/AE5fkmS1KqeVO3Ct6/p8RV2vklRc1LNmefW9DrxS4c1dPADndut2nlVz35ZUTCzhufz5RcPyM2OqzH1lGwcXLNjAwAAa0MACADYNoybVX78IY3d/g87y+bPfEkzr/xXhfXzm717V8TxyyruflzlA+9Pl1jNvPoZzR77nGzU2Ozd2zSF3W/RyC2flGykqDmlypkvaObV35HiVs+5G5Zf3Kfc+MPKj92r6Zf/i5ozz274vjheUVKsOGxct1DW8QoaPvQzGjr8s+kSq+mXfkOzx37vujz/uhijTOmgsiN3ybhZSVIcVlU7//V1B7LG8ZUbu1cyjtzshPKZcUXNadUufOtaHoDyOx5R+cAHZcOqGrPPq3r2K4qaU5Kk3PiDGj7888lxteZkFa8pAHS8gsoHf1qFnY+qNX9StUvfVX3yCdmoIb90SMNHPiqvsFeSVLvwzWsWADpeUcW979TYrb+sMJjS1HP/bk2hnF/Yq/LBD3duN2eeU+3Sd6VVAkDjZpUfe0ijt31aMkYzr/6Oahe+eUO/nwEAsBUQAAIAtg3jZORmxvqWxeH8tq4ucfxSpwooPUpFwey1Hw64xWVH705PhyvjFWWM6Qv/JKmw680aufkX5eUmJBurtOfxDQ8A3dyESnvfJRs1VDn9BdmovtmnZstx/CF5uV0ybqazrFV9La12Wx/jZJUdvq1zO46qCirHOmHctWC8vDJDtyg//kBSeZnfrfql71/1czpeQfkdjyg7crcyQ7crDuuqT/7wmh3Hcop7HtfoLZ+U8YryvYLG7vgfNP3Sb6o++YMND+WMm1dh5xs1euvfl5ffLUkaOfILslFT9Yt/I2vX/zcBAAA2BgEgAGDbMI4vJzPUtywOZtb8IdY4GTl+ScZcm//92R9dEfMAAEdtSURBVLipuDW/rqonxy3I8Qp9y6L6pTUPnTRuTo5XSudf23hxVFMc1q5rIOm4eeWGbu3uQ2tOrfnXFq3Xqr7WDeSMo9z4/XL8IcWtuavfCePKL+5X+eBPq7T3nUklYjCt6rmvXbfzsF34xQNyczv75iwMZl+6gi0ZecX98gr7O0vCxqTC2tlru//5PcoO3yql890FlVfSIfhXwRi5uR3KlA5LkuLWjFrVE5sSIDteQTYKJF/JNAqFAxq95ZdknIzql76TvL434nn8soq7366Rm39JbnY0XWrlZIblF/aq4WZkw80LAI3jKzN0s9zM8IZutzH9rOJWZdOOCwCAtSIABABsG8bx5fjl7gIbKWpOK15jAOgV9qi4662dypSNFlReUfXcV9c0R1bnmLycHC/fPaQ4VNS4lDQ+WINM+SYVd711QRXhxqlP/kD1ye9vWEiwpmMauUNubkf7jChsTKq5RKAUzL6oVvU1eYU9MsaTm9up/MRDydxzV8m4WZX2/ZRKe9+VDgGWRm7+RbWqZxTMvXzdzsV24OV3y82O9Cyxqk89uf7h0saouOvNMo7f2U5YO6dg/sS123njyi8eUKZ8NHnGqKHm9DOKW7NXt1kno8KORzvvV2H9ooLKNTyOFcyd/FNJRkOHPyI3OyYZR37xkEaOfkzG8VS78G3F4dUFnl5uh4p7363hIx+R47cDNquwcUnzZ76o6rmvrvs9xMvvUqZ8k0zP++OValz+sWQjDR/9mHJj92/cybWRzn//nyq4wedsBQBsDwSAAIBtwzheX9AVRw3F0dqr07zsuAq73qjM0G1rWn+9ahe/kwwdXEcA6Lg5Gbf7ATcO5xVH9TV/mPSL+1Tc+5jc7MQ1Oaa4VVFj6klJiz+8O15R2dG75XjlK9iyVWv+hILKq4vuKex8Q6czso0CteaPq1U7s3gLcUuNy08qN/aAjOfJOFnldzySVuld5YfxOFJYO6u4Nd8JAP3iQY3c/Iuaev7fK6yfuybne/tx5Od394Q+UhzMKZh9cd1bMk5G+YnXdW7bqKFW9YSi+oVrtvduZljZkbs6UwtYxfKLB1XY9b",
		"bOOtnhO3r2Mavs0O0q7nnnom3ZuNkZVuu4BRV2d7ch4yhTPiwvDbZzY/f1vZf5xf0q7n77ivvaqp5Uq3pKdsFQ+NXYqK65k38i2VhDRz8qNzOahIClwxo+/HOSjGoXvnXFIaBfOqzygfertPfd3S9obKywfl5zp/4saRxyBcOpM0O3aeTmvyMvt+uK9qvXxR//C7Xmj8u4eTnexn1ZYsP59PdB+AcA2PoIAAEA24YxvtyeD29xWJWNmpu9W1d3TG5OjpvrHtM26mjsZkY0dOhn5JcOXcGjrSon/2RRAOh4JeXH7usMJ43DeTVnnls25K1Pfl9Dh35GjleUcVxlh26VXzqg1vzJqzo2GzdVu/ANeYV9Kh94fxoCGuXHH9LQoQ9r5tXfvvphogPAzYzIK+yT0xNiB/OvXtG5yQ7fIb93+G9zSsHcK9e0s7OX363c+P2dwNlxCyrvf5/iuPu+4uV2dq4bN6v8xMPKDN+6aFtxc1rN2ecVRU3lxu+XXzzQuc8vHtTw0Y93bjv+cF/lr186rNHbPrXivlZO/rnCxqV1B4BKw9S5U38mSRo68vOdSkCvuF+50bvVuPyjKw8AC/tU2PnmvvCvVT2puRN/qOqFb13xkHzjeHK8Yn/V9xUyjnvV21hK1Kpum/drAAAIAAEA24fjyfjFzk0b1rZ9Z0nj5mR6A8BgdvtMlO94crNjfQHJ2tklK3FyY/fJ7QzRtgobF5Phe8to1c4oqLyaDOs2jtzsuPITr7/qAFCSomBWlVOfl1/cr/zE62UcT8bNqLjnMYX1c5o7+fkbvlmLV9wnv7hfMt35/xrTz15RSFXc+7jkpP80tbHC6mk1pp+5ZvvueCXlxvqDOklyMsNaaUZN4+bk9bxm2yLjyhhXMkal/e9b8FyL5/pcsFV5+T0r729mSNKVz/Vpo7oqp/9S1sYaPvpROZlhNWeeV/XCNxVdxbyZzdkXVZ/8nkp73yPjZtSsvKzZV35H9cs/3FLvzzZuqTn1tOKwesXb8LITypSPdt6z43B+zdM1AACw2QgAAQDbRrsipC1uVdLhsmsT1i9o/sxX5F5+6qr2w3GzKu59V98HehsHipqT6/5w6bhZGTfbuR0F0+vqntqqnFDl5J/J9JyXKziz8nITKux8Y9++xFFNYXPyunZZLux6o4yTfLi2UVPNmWcVNi4t/wBrVZ98Qvkdj8oYR8bLKzd6jyon/0w2vvrq0LB2TrPHPie/sE9+6aAkIzczqtL+96lVO6f6pe9et3OzqYyj/PjDfUGfJOUnXi+/dKRvmY1byk08tGw42qqeVrhgSHcS3D7cs42mouZkEpwtCOhWY+NQUTCzasMNNzehwq43yziZ9HFB8vqL+v/e/eL+vgYncVhT1Ly8aHvt125u9F7lRu7qrh/VFTUmFx1vXyBorVq10yvub9yckbR8tZlfPKDi3neuMsepSRohOZ6MHHm5CZX2v1fFPY8vub2Ft8fv+EdLTnHQDuCVVlEW9z6uwu63rng8zZlnNX/6C8u+v4T1i6pd+m4yZHkBx8kov/ON3dMXt9SqvqZWdelzGDanFEd1VU7/Vd973HoVdr1ZXmGv3J4AkApAAMB2QQAIANgmjIyT6xsCHAVz65pYPgkAvyhdxXAwL7tDo7f+fRk301lm40D1yR+ocurPFa2nG6Rx5LjFvuGTYWNyXdVTwfwJternFwUz6zir8ss3aey2T/c0X0iqK6tnvqDaxW8v22TFRg0Fsy+srQOmMfKy430dXhfOm+WXjig7cldnuJ5xMirsfIsy5ZtX3LSbGU4qryQZ4yk3/qB2v/7Xlw0WbNzUpaf+xzUPUw1mX9Ds8d/X+J3/OJmvMW2iMHTow4oakwoqr0hpRZmbHZd6zuOVcrzCgm6lRm52XP5y58KGCqunr1n1qDGuxu/8R51Oud39LC2qbCsf+KC03H5Yq7nX/lBzr/UHgIVdb+nMw6f2fIA73qDM8J3r3tewflZzJ/4onbty+fNb2PmoMuWbJElRa1aV1/5YtYt/syjQGT76cRX3vENKpx2YP/slzZ/688WHFkeKW7MaOvRPOhViYe2spl/+DbUWNDLJj78uacqRS+bubEw/pann/5cVjysKZhWHy4eabmZE+YnXKTO08utFSQyYDAHO75GbX3qOvYXd0h2/rPzEw8sOyW6v7xf2yCusXM2Y7INV9cyXl509L6i8ovDVc0sM3zXKjT/UFwAGlWOaffUzndfionPXnJFsrKg52dlG+ltbdT972bDa9z653q7vAABsJgJAAMCW4+X3KL/jdX3Vfsa4ygzd0heueLkdKu1994pzV8WteTWmnlKrelLWhrJX0e3SK+zRyM1/R7mx+zofdm3cUn3yR5p+6TfVqp5cthokUzqi/M5H+5Y5bi7pSNkTqvjFAxo69KEVQ8CocUn1y08qSqvzrqZCLzN0s8Zu+1TSBTWt4InDmubPfFFzJ/5QYePispVcUWNS0y/9xqJQaCmOl9fw0Y+rlAaANgpkw/4KvcKuNyUBWvvDuXHk5XfJy69liLHpea6isiN3Lvvh3ob1ReHGSqwNVT3/NWWGb9XQwQ+lu+YpN3qPygc/qJlXfktRc0q5sfs0fOTnlw1U1sdZFKyV9r1bhV1vWnLtOJjRxR//v65hcxKTVnmtft68TgfnJc/moqHfxrgq7nm7jNOzbePKzY4lc9Wtf1fleLkVV3CzEyrte28nzAnmXlXt4ncVzC0OkMLeaj8bKWpeVlA5vuSWCzse7XSZtVFTjakfqXbhW4tez15+b9/rNm5Vlt3m2o/byBh3XX/bMk4SBq7j5K66feNpTV9HGFcrrWijhqKlvnwwjjKlwz0rxmpVX1Nj6qk1VWAbx1dh99s0fOQXZMOq4nBe1XNfS77sWOULJbOgYns7zdkKAAABIABgy/GL+zV06CP9QYIxnYCqLTN0k/zyYckuX8UR1s8rbs0m4dxV8IoHNHb7ryg//lB3yKCN1Jh6SlMv/HuF9bMr7kdm+BaN3PSJ/oXpB/ZeufEHlBu7b8VtNWeeU6t2tqea5cpkhm7Vjvv+7/ILB7rhX1RX9exXNHvss+kwx+X3w9pozR2PbVzq6xYcR3VFPWGsmx1XYcejy8yTdmXVjcs+zph1b9NGDc289J+VG7lTmaGkAYRxcyrseotatTOae+2PZdyc3Oz4Fc6JuDrHK/aF4r0i412zRgfXWn7nG+UXD13F73kBG8vGy8/LZhwv6fybhotxa06Nye8v2ZF6vYL5E6pd+LaKex9XFExr7tRfXtF8iBtyGmyoOJjbMnOKOl4+/fu98nkMlVYt50bv6dyOw6rC+ZNrrgY3bk5+4YAy5SPp+6xVY/q5Vf/+jJOV4w31BaBRMEcFIABg2yAABABsPcaRcTOrz9XUnnR/xVUyi4LD9e5LdugWjd76qSSYS5/PxqEakz/Q5LO/rqg5tcZjWn3uKWO8VXMQ4/hXdUzGuMqN3a/xu/5P6VC9tONua17zZ7+omVc+cwWdO1cZUmdcefluoGvDquJgrvPY4p7H5BX29n0It1Fz1fDEuH4ayJr09xIsmsNtoaQxwfqG/imtjJx85v+jXQ/9S7nZiaQarHEpmc9uHfM2DgQbJ+d6tQYI7ao0J7PM3Z5Ke94hp6e5T3vba2fSIfnp346NVg1lque/pubsCyrvf68cv6jG1FNpBeLifxqbviHdRsb4y76Ww/oFXX7236hy6s/kl4+oNX98yXWN0/++lDSYWcPcdDZO5whd/e83alzSpSf/pZqzL67jXF47w0c/puGjH1+lGcrqnNwO+enQbUmKmlMK5o+v+TVtnEw6vN6kXwZYRc3Lq84n6/gludmRvmVRc/rGe+0DALYtAkAAwNaUVmZ0LZeKLfWhz/Tfvf6sJ9mK4ys7cqdGbv6EcqP39oR/LTUu/0CXnvl1xcH0Go9nqX1d6zFtUGVUWsWS3/F6jd32qZ7wzypuVTR/9suaeeW31zanX3eDcryivNwO2ShQq352ySHDRk7fcM64VVXcSqoH3dyE8hMPy/WHumcgbmn22O+qcuovVnz68sG/paHDPyfHK8hGTc2d/GPNHf9vq+72lXY9bc0f08wrn9",
		"HILZ9UMPeKZl79jJrTP0n3uaEomF427FrfL8rI8QrJnIPtcxZWZZeZAy4KZmTj61eJFMwf19zx31dYv7DyYTgZ5Xc8qqHDP7vk/fkdjyozckd3SL0N1Zx6WpXTK//ee7mZYY3c+qnOXJrWRsvPQWgclQ99uK/BhbWxSgc+sOz2Czu6Q/cdv6zS3nd15u7rE4eaeeW/yi8eVnHfOyVJ2ZE7ltxmpnSor7lFbuL1GrvjH61+3udeUfXsl9cx9+lVvAFuUaU9j/cMF086hTdnX17z442TkeN3q5Ft1FQc1Vbt6G3cXN/jJCXTMNjNqfAEAGC9CAABAFtO3KoomHtRUbPb/dU42XSeuiSEi8Oaosb5JT8I++Wjctx2lYld9YPdUoyTVW7sHg0f/Xga/iXVOjZqqn75CV1+5t+sPfxLO4Q2Z57tW+b4w/IL+7rDb1tzCuvnF1U/GScjv3RIxkkqhKyNr+iYHK+g/M43auSmvyuvsK+n8q+i+TNp5d865kg0jq/M0K0q7X2nCjsfVf3yjzTz8m8l8wYufG6/KKenqUXUmlHYmJQxnoq73pI0Y+irarTJMOFVzvHCqh0bNVZ9zNWwcUvV81+TjQM1Z1/sa+7QmHpardrZBRVjV8bxiirvf39fd9bq2S+rcuYLy50IRSt1S97o8xA1FMyfWHLOvF7GzSVzdy5zjMXdb+lrdhI1Luny8/9uUdOMlXj53Rq99dM9OxevUJloVNj5BuVG77viY/cKe1Uu7F18TuKWZk/8gfzyIZX3v39d2zTGXdNjahe/rdr5r0tae/OjNscvr1oxvdHiuLlsaH1FjKv8xOu62w/ralWOrWs6BOP4/d3ko7q0hmHajpvrH4JvQ0XNy+vq2g4AwGYiAAQAbDnNmed08cf/om+ZXzqsPY/+L50PYMHsi5p+6X9bcnjb3jf9b53OsTYO1l3tZdyc8uMPaujIR5Ubuasn/GuodvE7mn7p/7fukKl+6XuqX/pe77OotPcdGr/r/9zpGFq7+B3NvPybChcEOX7xoHY99K/SIbKSjWrr/lDt+GUVd71VQ0d+Xn5xf1/4Vzn9V5p99XfXFf5JkpsZ1dDhn1Vx99slSbmxB5Ub+3EakPV8KDZO0t3XpMGYjRU1LitsXJJX3K/8jkfkZkfX9dybqR2YLrV8XdWTK3D8cjK8sMMqbEwqmH1psw9/gxjldzyi7Og93YpJG6t+8bvrCv+6m+sGW0lAzrxsC5UPfEDeUpWL14iNW2pMP6vahW9u2Db94gFlhrrDf+PWrBozz61rG8bx+4ac27CxpiHnxs32DV+Ow/qmze8IAMCVIAAEAGx9xpGb29H34StqTilccu49I693qGlYXxSorfhUxlV2+HYN3/RxZYfv6M4tF9ZUvfAtzbz626sOe1zT87gZOdnxvnm/WrWzipcI9ozjyc10h8dGwcy6Qk3jZFTY+UYNHf1oUnHYG/6d+nPNvPq7stH6K4riVkXNqaeV3/GIHDcvL79T+YlH1Jh5TmHtbN/vJJs2zpAkGzcVNS/LGEeFXW9K77u6xgBbkeOXlCnfpObsi+m8g2jz8jtV3PWWtOtzIgpmVDnzV+vfmHH6K9tstEIAaFWffEJhfXGVqpsZUWbo1k5FYtSYVGP6J50mGo5XVG78gc5Q41btrIK5V2TjZud5bVhXq3pa82e/3LPdUeVG75Zxc0nl6MwLChvd95Dc6H1pp2urqDml+uUfLXuowdxLV9z1u7jnsc4XI9dD8jfvbGgAWNrzeOcLE9lYYe28mtPPrmsbxvEWVADWZKPVg7ykA3DvkPzalmmwAgDAWhAAAgC2PGN8Zcs3dcM4GylsXlK8RAdaNzPa0202mdtuXcPD3Kwyw7crO3xnZ1kcVlU7/3XNHvucwtq5DZlTy/GH+sI4G7cU1s4unojeOHIzYzLt8NPGippT62rS4fhl5cbuk1/Y31kWtWY1f+rPNXvsc1cU/ikdOlefelL5meeUH39IklFu/H5lJ+9U1JjsBBVGRn7pUM/5rClqXlZm6DYVdr6xb2jwoPAK+1Tc87iKu9+q2eOfU/Xsf9/sXdoyjJtTcddblR29u2+4dO38N1YdUrzE1pKh8T3Dx62Nlp8P0caaffV3l7wrN/6Qxm77dCcAbMw8p8vP/hvFYVWS5JcOaddD/0pOPgmBmtM/0fRLv7Ho/SW6/CM1ekK8/PhDyg7dIuPmFAezmnvtD1W78K3O/Tvv/xdpx3OroHJMk0//T33b8/K75ZcOycaBwtp5xVFzA34LNj1H659KYMXfhnH7qjE3kuMPKTf+UCfsjaOGmrPPKQrW0ISpbye9vi+TbFhfWwWgk5Hj9c/JSQMQAMB2QgAIANjyjOP2BUg2qitqTi85/CozdLSvU28czMiu6wOzWTRPVlA5psrpv0waXGzQhPqOV5SX39W5nQwfnV1UuWQcPz327jDkOJhd39Az4yz6UN6c+onmXvuTTrhxpcL6BdUufFvZodvSLpljKu56s5q9VYDG6Wu6ELcqsjZWcc/blSkd3ZDzuRGMceX4Q2vqUmptqKg5063+6uGXDmr48EdV2P1WOV5Bw4d/PqlUmllfpdKgMm5ebj7XV00V1i+ocvrPr2h7ffOyKZkP8Uoqs9zMSN+2wvr5VbsJr3qsji83NyGTDjmNWhXZNTfwSGSGbkoa3bg5Naaf1cwrv7XkFwBhY1LVc3+tRtqUJg5mFwwj7zlFwZxql75zZcOtV5Abe0D5HY8seV8w+6Iqp/60M+Q7mH1pXfPn5ccflF/srWCeUe3id9e9j8bxu1+otCv51hgA9v7N2rBKBSAAYFshAAQAbH3GTZtWJOLW/JLVf0rny2uzcWtDGkJEzcuKgtkraryxHMcryM125+OKgqklG5oY48krdhsOxFH9qkM7pcMXr7Tyr5eN6mpO/0TN2efTyfmNcmP3Kzdyl6ppFaBfOtTfAThqyCvsVmHnGztDoOOwqqg5LS+/a0OaaFwJxy+ruPdx5cYeXHXdqDmpysnPK6gcW+JeI798JB0qauSXjmj4yEc19eL/qrB2ZlOObSuxUU21C9+WbKTC7rfLzY5q/swX1Jo/uf6NGSOnPSS0vf2VugAvuxlPXn53TzWqVTD30porvNzsuHJjSWOR2oVvdIItxx9SbuTuTpfjqHFJ0TLvXcvum5OVmx2XX9gnY3zNuZ9bsmdFWL+gyqk/61ZD2ljxMkPP47Cq2oVvq3bx2+s/5ytxvGUDwMb0T5I5W01798I1B4DGyaiw6y3dAM7GilvzknGVGb5t2cfFzWmFzcme926TBnndv5k4rK6pqtK4mb6/tTicpwEIAGBbIQAEAGx5xrjyC7s7t+PWnKLm5SXX9Yr7JNMzrHaZ9Tab4+bl5bqhWFi/tHSwZ1z5+Z5jD6vrbmpyrbVqp1W/9D1lh29LKuj8IRX3PKbG1FMKGxeT4cHtqkwbykZ1edmJvlCwPvmEjKy83LgkX8Z4Ku19j7Ijd6743H7xoIybVBQZx1Nh99v6qkWXMv3Cf1pyXkjjZpUp36zCzjesfszV06qe++qS94XVM6qc/BP5t/1DOZlhGcdTbvx+lQ98QLOvfmbJoPdGYqOmmrMvKGxcUnP2JeVG79b82a/I2kjGeLJaX5dr4+UWPsHyQ4CX4RX3KTtye6d7eNSYVFA5vmoFoJffqcKO1yk38Tplh25VUHlFtYt/IymUjCOvsEe5iSRQtnFLzbmXV5lD1Cxe4vidADG2gexyVcg22tJ/WzYOrnj+wuzoXcoM39r9csAYefndGrvt0ys+rnr+66qc/FNZ20zPpSc3O9o5n2oHeavN0WlcOW6x27BGUtSaZwgwAGBbIQAEAGxxRm5+t9xMt0ts1JxZchJ/Sem8egkbB+tqAHLdjsjJyM3v6pmrUMn8f8HcEusmlUltcWs+qUbcQmzUVGP6aTVnX1J+4mFJUm7sPuXG7lP1/NeUHbunu25YV3PmecXBrAq73irjZhXWz6t69ivKjd/Xe+DKDN2szNA6mhYYV5nSEWVKR1ZcbeaVz0i6dn8X1oaqXfiW/NJhDR/+eck4cryiirvfplb1pOZPX0GjiwEUNS+rduGbakw9pSiYlp",
		"ub0MiRj6k5+4Lmz35pjVsxfaGM2iGzXfsQeePmk3n6Ru7ofHlQv/xDxcH0EkP+uwFdduQOeYU98vN75KShUtjs/l25/rCGDn1YXm6nlA4pDuZeWj6kM0aOV0yahfQEUo4/3Jl7zkaBZDdmGoLtw6iw8419zZ0kI8cfWvULgmD2pb75IY2T6Ws8I0lRMCsbrdxV3XFzcrMjfduKWxW6AAMAthUCQADA1mZM8iGvPYedjRQ2LihsnF9iXUd+fm+3sUbUWNCNdmtwvEISUrU/TMYttaonl6zsM44vL9edKzAKZpetftxMrfnXVJ98QtnhW+X4QzJuXqX9P6X69NPK9oR4cTiv5uyLiltzas2fUGboJlXPf03NmWeVG71rsw9jw8RhTbPHf1/Z4duVG7s/6U6d26ny/vcprJ1TY+rJzd7FLcHGgaLmpDLlmzV2+z9QduQuZcfuUdi4uKZzZNIhsn3iaM2VWca4yo/dp9L+98n1R5KHB3OqnvvrvtejcTLKDt/ZV7XqF/YljXXS13EUzCSBk41lvKJGbv6ECjveIBlXNg7UuPxjNaefWxQqWrWrDI384n6N3f4PFcyfkGTlZkZV2PGIHK+U7FursiFTEbiZEQ0f+YiKex/f0N9nb6OhjZIdvVu50fv6hu1eqaSj+kh3gY0VBzOLmy8tfJyb63+clMwve4UVjQAAbAYCQADAFmeULfcGSHVFjUtLzr2UKR2VkxlKbyUdgFvzryk7cqeGDv3MokqhJZ/NzfQ1rJCkwo5H5WXH11R5FwUzmj/zBTVnnlv+Obx8MlS585jZZF6whcMNjaPM0C0yXnfeq6hxSWH9oop73qHCrjf1DWVbjuMVFw2LLR/4gDKlw2v6ABs2Lmju+H9T2Li47Do2bqkx9WMFOx9NAy8pO3KPhg//rBy/+8E5CUleVBzWVL/843Qusm8tfW5tpLC5codPNzMq43TPQdKcY5V5H5eZHy4KZjT32v++5Lxojj+k8Tv+cWfOwrWIg1lNvfAftfvh/7eczGj6+7xN5YN/S2FjUmHt9Jq3Ncgcf0hjd/4TZUfuSIb7lw5p+OjHFTUn1aqudo5MZwh4m7WtNVVmGSej/PjDGrnll5QpHU6CPBtp/uyXFMy92he0udkxjdzyif73EONKsgprZ1S78C3Nn/2SwvpFufmdGr/9/6Dc2P3p34tNqxq/uOScpGFjMg0FjRy/pNK+93T23xg3Gfaahoxh7cyGVJ0ZL6/syN3KbHA1oTFmA7bS90tSYecb5BX2Ljk8+goOXI7f7Tpu45Ycf0iZ8sqVxl5uQl5+z6J984sHeoaJW0XBnKIV3icBANhMBIAAgC3NyChT7naKjcP5ZefQygzfJqVzRNk4Uti4KBsHcrPjKux4dO0VJAs+xBo3p+zo3WsaehfWL6g++YMV13HcvPxCt7FH2JxUvEQAZuQoO3xHt+tlVE+q/2yoTOmgCjvesMaGGWbRMTleoTNcdzXB/HFVTv3F6utVXlF98gfKlI8mVYCOp/LBD3e6Kts4UGv+tXRYtlXtwjdUu/ANNedeWlwVFQeaeeW/au61P17xOYcOfUTDRz8mxyvIRk3Nnvhvmj32uys+xkbBMsubCuZeUVB5ddF9fvGgrOJ1RxCtynFNv/xfNH7nP5GMl8xTuOMRhbVzmj3+uaSia1syknEXdcxetJbx+oZNLiVuVdS49H1lh29Lt+kpN3qPho98TFMv/q9Ldrzt240FrwEbt1YJttOgbf/7NHz45+RmR9PXmFV98gnNn/nCokYdNm4qalxKqnGtlY2bas0f0/yZL6l28TtpsGdUPvhBDR/++aTBj3EkWbUqxzR37HNqzr6wZBfx2tm/Vnnvu+X4ZcmYZM6/ha9rGytqXlL1/DcVX2nzHhsv7jK+0YGd1HmOJBi7umrF/PiDyk88vKgzd1g/rzPf+vuL1i/sepNGbv7EspWISQVgqXvbzWr46Mc1fOSjK++IMYu+bBk++nc0fPRjnV+ptZHmT/+lpl74Dxt/TgEA2AAEgACALc24OfnlbvVaFMyqtUzlVHbopu6HNBt2hv8akwZgqwQRq+zJohBt6dWcVSpVkrmreuf1C+vnl25WYhxlh2/t3Ew65abrtY/nao5pzY9d43rWqn7p+8pPvF65sXuT+LYnILJhvS8ESa6vsLm4JbtKd85Fc73ZcNXHrHIQSwe9NpauoFjK2lDVc19TZvh2lfe9Jwm43LxK+96jVu2Mqme/vC3nEfOLBzV266cUrtJl2xhPmfKRVbZmNXPsd5QZvlmFnW+WjCPjZlXY9SaF9XOaPf4HKwZ6/ZW9Nm02sfw59UuHNHbbP0wC8J6GQY2ppzXzym8t2dk5btU0f+bLcnM71Zx5TvNnvqTG1JN9c/X5pcMq7nxLp9rTxoGCuZc0/dJ/UWPqx8vuT3PuRV388f9Tpf3vTeYL7KlolY1lo4ZatXOqnv2KgrmXr3gI8Nxrf9zT5fg6sKGCuWNX/HDHH1Jh11vlF5do6mPjJefts/HKcyQa4yZBa+8yx7uij0SLHmdD2XV2nwYA4HoiAAQAbGm5sft75vhKqmCC+ZOL1jPGk1863FNt1lIrXc/aOAkEVqlWSreUbGNBOGZtuKYP3knwsPx6jptTduTObmhho2T+v+bk4j1xM/JLhzu341al0/zExlEaiqwllXJkHHdRMJns6xoeb9e4nqSg8qoal59QpnxEjj/Ud1/UmlNz6uk1bWeQxGFVc8f/QJnyEWWHb08a22THVD7wQUX182pMPbVqt9mtxvFLyo0/uKHbnHrhP8krHug0cXH8IRX3vkut+nnVzn99mVDPyOkdlr1EldvC9TOlI0koaZRMFRBWVZ98QrPHPpsEbEuwcVPVc/9dzZnnlqwQlaTW/Ald+sn/rLHb/6GyI3eocfnHmnn1d9SqntRqGtM/UWP6Jxt6PheaP/PFa7r9jWQcT4Wdjyo/8dAaq5zXumGvM5/ihrOSDRsbsCEAAK4NAkAAwJaW7WkMEYcNtaqnZcPqovX80sGkqi4N7uKo0fkwHzUuqnr+a2ubA9DJKFO+qW/OvCiYUTDz3JJNOhaKg9kV58ozbq5vvqkomFVYO7dk1Vpm6NaepgNWUXNardoZKQ3aaue/Kq1pDsCSMsO3drqRSlLYuKTm9NNrqj6LGpcUL3HOl1O7+F3ld7whCbvS34e1oVrzx9LmBjcaq7B+XnPH/1Bjt/+K3NwOSVJ26Gbld7xeQeX4knPD3WjC+gXNvPxfNH7n/7Hzd+8X96u8/70K6+eTeTWXCuF7XtfWWtkVg3qr2sVvyfFyGj76cVmbDEWvnPoLRc1JefldK04VYG24aD7NhWZP/IEKE4+oduGbkjGrrr9IHCoKZhWH85v2u9hsfvGQirsf61ZK2zhJ2Nb0Jc7ybBykr7eZq9pOdz8P9g0hj2MCQADA1kUACADY0rLDt3Wu26iusH5+6fXG7u2ZJ8qmDQSSypvm7Etqzr60pudzvKLKBz+k0Vu780s1pp7U9Eu/oTAN366GcTPKlLtVfVEws2z4U9jxSLejcRwqbJxX2EjmP6xd+JZqF761pud0czs0eusvq7T3XZ1l1XNf1eyrv6U4rK9pG+sRVF5VMPuiMuWj3TAlbqk5++KGP9d2YeNAjaknNX/2yyofSuZFDCrHFMy/ti2HAKdHtcb11jrPnFV98geaO/l5jRz9BRk3nzQBGrlLpX0/pahxWWH93KJtG9NbIbZaBWBS+Tp/7muKWhUZ46l26buyUUNeYa+GDn1Y2aFbtRGSYfDrFwUzqpz+guqXvnPV++CXDsnNjstc1fQHV8/aWGH1VDr/5xr2u3hQmfJNnb+dsH5Oxs3KzYxd1bQHYf28Lj31LzfsuHbc989U2PW2TpVi75BwAAC2GgJAAMCW5WSG5RcPdm4bJ+nQ6xf2qVU72wkgjOMrO3xHJ2yyNlZQObb1hlUaIzc70Tf/n+MVlCkdVTD3iqKejrfGySg3fn/ndhzWFFbPXvH8X9dbHNVlbW/TDLOxQ/m2oSiYVvXcX8vL75ZxPFVO/bka0z+5yjkLN+9YmtM/WbWSyhhPfvlI2sxmdTZqav70XylTPqzCrremXX",
		"AzKux8k1rVk5o//ZeKW/2Vccbt+buy8Zpe9zaqLwrQHTenTOmosqP3bO65bVxS/dL3N2Rb5X3vUWH329ZU/XwtJU19fnvNw5CjYFpR87Lc3IRsVNf8mS8qN/E6uZnRTT2OhYyT7QtXt+NrGQBw4yAABABsWX7xoIyX79x2/LJKe98lxy2ocvovFMy9LBu35JcOKVM+0g2YbKTm9DObvfuLGOPKLx3s+zDu5XerfOhDcvyy5s98MRniayNlR+6QXzjQWS9uzS3ZnGArcvyy/OL+vuM0Tka5sQfk5b6wtiog4yo7cofyO9+w4mq5kTvTyfglOa4yw7ev+pjm1JPXpPJxLVrVU5o99llZG6s1/9pVd0ndLGHtrGZe/R0Fc6+suJ5xcxo6+KE1B4CSFAfTqpz8U/n5fcqkTXDczLBK+35KYfWM6pPf76ua7A2WrY2leIsF/5vIeEW5mTGZ3nkSN4GNm3LW2oU9fZ00K6/KH7pJ9cs/VvX815UZWfvf0PWSnNf21xyWCkAAwJZGAAgA2LKixiVVz3xJpX3vknGT4b2OP6Tinsfk5naocurP1Lj8Q+XGHpCbneh8EIuDOTUmf7TZu7+ItbFalROqXfy28hOv61QsermdKh/4gLz8LlVOfl7N2RdU7K3asbHC+gUFc2sbxrzZsiN3yS8d7QZzSjoOZ0pHVNj5Rs2d/Pyq2zDGVWHHG5QpHV1xPa+wp3OejPGUH39Ifk+F5VIu/Oj/sWkBoI1byzaRWHwONnfY5maxNlIw+5Iqp/9CI7mJznyAmeIhFXa+Sc25FxU1JtsnaUFl6doqANeqMfVU97muoeKet1/1/HZrYW206hDpjZI0U7qyY4qCaQVzL6tZ2Kf5M19YduqHzWSMlzSo6nmdxjQBAQBsYQSAAIAtK6yf1+yx31NYP6/ywQ/Jy++S0qqi3Pj9cjMjqpUOKTf+oBy/29mxOfeiwuba5pq6rmysYO4lzbzyWwrr51Xc8850Avmkaq6w681ysxOqnf+q8hOv729oUnllwyauv5aMm1Nh4nXycuOL7nP8kvI736zape+t6QO9cfN9XZDX9PyOv+pjNns45JqOwzj9VVvWrmPOve0vjuqqX/quMkM3qbTvvTLGUXPuZTWmn5JdEN72BYCrdgFen9qFb6p26bvX/HgLu94k417jANDGalz+sapnv3TNj8d4BRV3vU258QeueF+bM88qrJ5Rc+7FLTlPpuOX+99LrJWNN+eLBQAA1oIAEACwpYWNS6qc+nO1auc0fPQXks6yafVFZugmubkJOW5OpqcbbvX8NzZ7t5dlbaRg/oRmj/+BWvXzGjr4IfnFZKivcTLKjd4tv7Cnp/uvFLcqalzeehWNS8mO3Kns6F1Ld1I1rjLlI8rveESVNVQB3tCMJzc30bNgY4Ot7SBsXlb13Ffl5XYpblVUOfMFNWeel40WhCx9AaDd0ArAqDWvsHZu1fDVK+xT+cD7unPU2UiN6Wc1f+YLa3ym6xHuWoW1U5o/+5U1rW3cfOcLCqXVq3Ews6Ywzs2MKFO++coDQEmt+ZNqaes2yUkCwP55TTershgAgLUgAAQAbHlxWFX90ncUt2Y0dPjnk+64xpVk5GZG+tYNa2dVvw4VO1fFxoqal1U98yXFzSkNHf65JNg0jmQcubkd3VVtpFblmBpbcE7DhYybU37iYXnFA53h2K3aaTWnn1Np37uldC63wo5HVF+tCtDGCqon1Jx5bsXnzI3cI7+4P/l7sLHC+jnVp3684mPiVmWzT9XK59F4yo0/0Am7lYYvWzUIuWZsrObsC7r8wr+X4paixmVZGy48W31VWMkQ13DdT3W1/OIBFXa9VV5uZ7ofoeIokNYcAG49xd1v1dChn+28loPKq5p5+TcVNi5el+e3cbBh2xo+8vPyS0c2ZFv1yz9U/cK35XjF/mkOZBWHtetybgAAuBIEgACAbcHGLTWnn9VM8J8VNS+rtPedMm5+0XqN6acUh9XN3t01icOqahe/o6g5peGjH1Nu/KFFFSU2CtSYeWZbTC6fHb5dubH7+ib7r577mqpnv6LCzjfI8ctpFeBNyk88rMqpP192W9ZGqp3/+qrzBQ4d+hkNHf45OW5e1rZUu/BtzRz/vZXP+4IusteLl9+p4u7Hlq6ObA/79Qry8nuUHbk9OV/tfQ7rN2S4YKOmwurpFdfprf6VYtlNaALiZkaSSuT26zfe/sNBs6P3KjN0c3LDxgob5xQFU1e72c05lvH7lR97cEO2Fbcqql/6gRwvv2COQ7vlv1wAANzYCAABANuGtZGC6muaeeW3JRurfPCnF62Tn3hEhYlHVLv0nc3e3bUdUxyoMfOsouf/ncbv/qfKjd3fd7/jZlXY+WY1pp5Wc+bZzd7dZTleQfmJh9KmHUnFUFg/r/nTf6WoeUnVC99Uef/7JElublz5idepPvnEClWASTVNHMyt+LxxWEvnx0tGUcbR6o/ZLK4/otLed8kr7ltmDZNUtBlnUfOEKJhW1Nye4cu1tqgL8CZUALqZkUVzEcbN2c0+NVfMOJ5yo3f2HE5TYe2cbHz9z+2GHI/xN27uT+NKJhki3Rs+WxsTAAIAtrQbs70cAGD7slbWhsnwsCU+6LvZMe24/59r6PBHtkWzh+SYkqolGzWTJga9jKPsyO3ace//VcU9jy+qENwajHJj96mw883dxhXWav70FxQF07JxqLnj/01xqx2IOMoO36H8+EOdsHBLM4784r6+rrzG8dIKvbXvfxzVFTYvyzjZZX4yye93Qfhn46aaU0+rOfviZp+JLcgsCt42cg7Ate1C+vfRU9mZzPX52mafnCtW3P1YZzizJEWtyqrD8bey5P8ZrQ35kY0kKzlufwVgMmT5xmnUAwDYfqgABABsK8bNqrDzjSrueVwy3jLrFDR6y9+Tn9+jmWO/m3TPXRisbSGOV1T54N9SduSuTuffBUckr3BAY7f9irzcDlVO/1VaabI1Pmx6uR0q7HyT/NLBzrJW9YSq5/+6M3Q5al5S9fzXVT7wwWTuxty4chMPqT71Y4W1s5t9CMsyjq/c+AMau/1/kHG7gbKbm1Bp37sVzL645jnR4rCqVvVUGnwux8rKdkPhsKr65Sc0d+pPFbe2ZmXjZjILuzrb+DpXqRllR+5SZviO/rkIw+q2DWwdr6jSvvfIOO0u1FZxMKvm3MvpXJvbrxnN/Okvqjn11IZsqzn7UvK+5mZknO77tY2am32YAACsiAAQALBtGMdXfvwhDR/+ObnZ8c7yuFVR3JqXmx3rVKAZN6fS/vfJyY5p9tjvqTV/Yks2UXC8vEr73qPS3nfK8Uud5VEwLRvW5eV3dapM3NyEho9+TG52XHMn/1RR/dz1r3ZawDi+smP3KL/j9Z1qOBsHmj/zpb4hq3HU0PyZL6u46y1yMqNJ1eDIncqN3q35+oUtGSoYN6v8xOs0esvfl5ff21ftZ4yn3Nh9Kh/6sGaPfXZN4Vzcqqgx9eNO1+cl2Ug2DhWHFYX1i2rOPKfG9E+2xRyQm8IsHAJ8HZuAGEdebqfKBz4gf8Gw7jisy8uNKXYzilqVbfP7c/yyhg//nDLtpkTJgcrN7VB5//vUmHpSYe1cckxhddPff9aqdv5rG3+u3GzfYCoCQADAVkcACADYFozjKzd6r4aPfFR+6XBneRzVNX/6r1S79D2V9/+U8jvfKMcrJo9JqwVdf0izJ/5AzemnFYdbZ2J+xyuosOstKh/4YF/n3zic1+yrv6ug8qpGbv5FZUfu7FQXOf6Qygc+ICczosrJP1Ew98qGdstcF+PILx1Sae+75WYnOoubM8+pdum7/U0rbKywdka1i3+j0v73dUKF3PiDakw9vXJH4M04NDevwo7Xa/imv5sEdsZ0jkOyknHleCWV9r5TNqypcvov08Bz+apMGydNSmoXvr3ZhzdAjIzX0wzoKioArY0Uh/NJxXBnc0u/toyblV88pPLBD6qw8w091XIJv3hQOx/8nxTMPq/61FMK5l5WHMwobE7JRovfg6JgRsZNAqSoNac43qAwyUZJ52TrpteXq4Q2cnMTKu97t0oH3ifHK/Td62ZGNHz04yof/Gm15k+oMf",
		"UTNWeeUVg/r6g5pag1t6DK2srGQedYbdzc0LAw6Tj+ukXLM+Wbl22ys9GMk+0fAhxtnf+3AACwFAJAAMCWZ4yr7OjdGr7p7yg7ek9nuY1bqk/+QJXTf6FW9ZRatVMabs6ouP/dcv3h5LGOr9zYfTJeXjOv/Lbqkz9YudrMGBnn2v/v0fEKKux8o4YOf0R+6VD3mKKm5s9+RfNnvqA4rGvq+X+v4aMfV37H63uCzZyKe94u1y9p+qXfUFA5tmLwZIzTN3/dxh1DScVdb1Nu7L7OsjiYVfXcVxXWLyxaP2pVVD3/TeV3PJpWcCZzB2aHb1fUmFy413K8Ul+l53L70AnnjJHjFVd9THKiQ0XB7DLbLCi/41ENH/kFZUqHu5VQNlJj6ikZN6fsyB1JaJIdV/ngT8vxy6qe/6qCyvFtU+11NZzMiPITr5NX2Lviesbx5ZcPr3m76+VmRpO/gZRtB15XIApmVD3312rOPNNZ1po/vuB4MvIKe5QduUulPY8rO3rXovAvWdHIy++Ul9+p/K43KWpMKqi8osb0M2pVTihsXFRYP9/5W5k78Yed9504rKtVeXVDzk9j+tkkmHM8ydol5/Fz/CFlykdV3POYirveKicz3He/jYPuFxBeUdmRu5QduUtxq6Jg/oSaM88pmH1BYf2CWrWziltziqOmGlNPdQNUG6bvUxv0e8+OaddD/2rJ8369pjh3/GLf/yu2S/d5AMCNiwAQALDFGeXGH9TwTR9XbrQbNFkbqTnznCqv/Yla6RxyUWNSM6/+lqKwoqHeqjrjyM0My80Mr9q0wTgZuZmR/oV2Y+faa1f+DR3+iDLlm7pPE7dUv/Q9zZ34o7RS0SqoHNP0y7+hKJhRcc9jnX0zxpObnZDxismH3hX20XELfSFJ+mxX91tJg9Xivnd15z6zkeqXn1Dj8o+WDsFspNb8CdUnn1Bp37uTuQ2zO5QduV2NqSf7t29c5SdeJzczuuJ+ZIZvkTF+5zG5sQeWDmQWiJqTmjn2e4vmhnS8ogo736Chwz+nTPloT/gXqz75hKZf/k15uR0avfWXO5WobnZM5QMfVGboFtUu/o2CyqsKq6cVNie39NyTV8Mv7NPILZ9cQ/WpkVnQ1GQjLWqME4dXPBQzDmZVPf/1JY8hCcmOKFM+qtz4g8qO3LnobzNuzalx+Uk52TH5xX3pa9XIGE9efre8/G7lJx5V1JxUUDmuYOa55IuL+jlVTv3FNankrZ7776qe++9LHJIrLzsuv3xE2ZG7VNj5qPziob5zGYfzql34tqJgWpnyTfKLB+RmxzvrOH5ZudF7lBu9R3Ewq1b1pBozzyuYe0Gt6lk1Lv9QtQvf3PBj6j2GzWLcvLzcrr73muW+UAAAYKsgAAQAbGmFnW/Q8E1/V9nh27oLbaRg9iXNvfZHas4+31fRF4c1zZ34Q8XBnIYO/Yz80sG0Ku1rqk8+seL8YMbxlRm6Ja3u6nk629qwOeocL6/i3ndp6NCH5Re7TTNsHKo++YRmT/y+ovr5noDOKqyd0+zxzyluzam076fk5XcprJ9X5fSfJ5VCK4RMjldUdvTuRfPO2TiQvYpg0/GKKu7p7xTaqp1R9fw3lqz+a4uCKdUvfU/5iYfleAU1Z19SUDm2uGrLOMqO3KnsyJ1r3ynjKjN0szJDN6+6aqtyTDPHPrvomNpVmZnyTQvCvx9o+uX/omDuZbWqp+Qc/5yGj/xCJwQ0bla5sfuUGbpZYe2sgvkTCqunFYcVxVFDilrpEMhrEwhaGyuqX1Rz7qVrsv0lT7fxZNzr/U/JtMozv0PZoVtV2v9TfffGUU1Rc+aKt97LzY7JLx6UXzygTPmoMsO3yS/uXzJMb1VPq3r2y5o/+xW52fEkLBy6TdnhW+QV9nWrd51uGFjY8TqFjUm1qqcVzL6goPqaWvOvqVU9dY3mkzPpvh2WXzyozPCtyg7dJq+wZ1HH9LB+TpXTX1Dl5Odlw5r80gH5paPKlI8oO3y7/NKhJABNXyNOZljZzD3Kjt6tqHFZrepJNedeUjD3qlrzx9WqnRmYOfKSuWgfVGbo5r4KwJXe9wAA2AoIAAEAW5JxMirufpuGjnw0qcRqs7GCyjHNvfZHqk/+cMkPlTZqpENo51Xe/36FjYuqnP4LRc3Lyo7cpdxSw/YcT65fUmboVvmlI313Rc3ZDRnW6fhllQ/8LZUPvF9efnd3f22kxtSTmjvx35I5/RaFjVZR45IqJz+vOKyquOutql/+oWrnv6k4rKqw843yS4f7K62MkXF8uZlRZYZvT5qJ9B5T/dJVVadZGypqXE6DSqM4rKl24dtqTv9kxSGYNg7VnHtJ82e+JMmqduHbCua3xrBZL79ThT1v7w//FKs2+X3Nvvo7CiqvJMcQNVS78C3JWpUPfVjZoVs61UiOV1Rm6BZlhm6RjUPZqJ7MfxaH6Rxs16Zzs40DVc9+5boGgNeCcTzlxh9WYeLh5VaQcQtys2PKlA7LzXXnnpQNFdYvrrkr84INy82MyC/ul1fYKy+/W37xgLzCXvn53emw2MXVw1Ewq8bUk6qd/7rqk08oDucV1s+rOfOc3OzfyC8dkl88mAybHb5NXn53t8qupzIwN3a/ouYltWrn1KocU1A51gmbr6Yy0M2OyS/sl1fYI6+wW37xUHI7v1OOX150THFYVXPqJ5o/9xXVLn1PNh3WGlSOK6gcV/1SIT0v+5Udvl3ZkTvklw7L8fLtnsxycxNycxPKjt6jqHk5CTgrryqovKrm7AuKaueveJh251cdNVQ59RerrteYfnrNTWHc3IS87A4ZZ6nKQiPjZOT4JXnF/SrseHTR8Pfm7AtXdUwAAFxrBIAAgC3HyQyrvP/9Ku9/b/+HrDT8mz3++6pf+u6Kk67bOFDtwrc7H6DDdJiwX9yv0v739TWtUHuePMdfNKzMRg2FtVOKW/NXdUxubkIjN/1dFXa9pW+IsbWhGpd/pNnjf6DmzHMrdiqOghnNn/mimtPPKGpeVhRMS5IyQ7eqfOADiya/N8ZNj6l/Tqw4nFcwfyypbLxCcVhT9fzXlJ94UH7psILZF1S78I2+BgrLHkcaZto4WHl9GylqXr6q876csKdDceeYoqZsq9Zzvqzql36g2WO/p2D2xb7ANAk8v6WoOaXinncov/ORRUNCjePJOGVJ5WtyDH37HlYVX8cmBDYOFDYuKQ7X+bqwUrji79TIy46rfOjDK66zlCiYVTD7wpqaMRg3l1T3FfbKy++Um9spL79bbnZcbmZYTmZYjptb5rms4mBWjZlnVLv4HTWnk2YY/a9dmzTHaE6pOf2M6pe+Ly+/S5mhW5Qfu1+Z4duS94H0by2pDNwjL79HudG7FTWnFdbPqzV/Qs3ZF9WY/knSKGe5SmTjphWL++XldsrLTcjNTsjL7ZSbHZOTGZGbGZJx80s+PA6ras6+qNqFb6o59XTyvrlEcBaHNTVnX1Rz7mU1Lv9IXmG3vMIB5cfuSyuN93Xm4DOO3w04R+9R1JxSWD+bBoEvqjH1lKLm9BVNRxC35jRz7HdX/3OLmmtuCpMdul3lgx+Q4w8t8RdnJOPKuLnk78Mr9r2vRs3Lak4/s6bnAQBgsxAAAgC2nOKut6h84ANp1Vq3+2owf0wzr/5WWvm3esWYjZsK5l7uWxY1p2TjcFGXy+U0Z19QMPfyVVeslA/8LZX2vqsvpLNxS/XLT2j22OcUzL64pkqfuFVZVGkS1s/LON6aj6l+6fvJcLWrqUazsVrzJzR/5ksq7X9vpwHG2h7aWrVKy8ah5k//hebPfuWqzvuy248aiyogo8YlNWaeUX7no3K8QjIk+9hnk1BpieAljupqTD2lVvWU6pefUGHHG5QdvUtebsd1n5/MxhvbZGE1reppzR77rFrV19b92JVC3eQ4Xk0bT6w+l2PncVFT9cknVL3wjVXXNW5WQ4d+VsXdb5PjFWW8vBw3L+NmVpwj1MahwtppNS7/WPXLP1Rr/jWFzclVh7bauKWwfk5h/ZyC2RdVv/Qdudkdyo3dlw4lvUXG7R6rcTLy8rvk5XcqO3y78jvfoKhxUXOv/YlqF/9myfc+x82ruOutyRcBXl6Om5",
		"Nxc2m1oVluxxQ1J9WYelq1S99TUHlFYf3C2qpxbawomFYUTCuYfVnNqSeTqsyhW1XY+QZlR26X43ebiRg326lCzIzcqcKutyisn9fciT9S7eJ31h0C2jhUvIYvG9a3zab84sG+6uy1PdBq/syXFLWYAxAAsLURAAIAtpz65R8rv/ONcnMTMsaTbKzm7Iuafvk/qzn97FUNiWtVTylqTkm9w4qXYG2kYO4lzb32v6852FpJ7cI3VNz91s68fzZqqnbpu0mIMn9szVUqS2nOvqg4aqq3F8KSxxS3VL/8Q82d+MM1VeqtJg5rql38G4X1i6pffmKDmxjEatVOqznz7AZuc/Xzk8zBdlo2rGr22O+pOfvcir8ba0OFjYuKLnxLjamn5PglZYdvU6Z8q7zCbrmZEZk0XEqGaJt17dNahY0LCq7j8F8b1dWqvqZg7pWN3rLiVkVR4/KqHYbb60fBtObPfEmVk3+avLZXE0cKa2fklw8n7y8rH6ha1VNqzjyv+uQTCiqvKG5VFLcqS4bCqz91XXH1tFrVMwoqL2v+7Jfk5fequPutyo8/KK+wp+dvxCTBmbtTjldMq9lay243as3KzY4uWcHWPZ5YUTCl5swLql/+oZozzypqTituza1YfbziKUpfA2HjooL546pPfk9udlz5iUdU2PF6+eUjPXMMGjluXk4+Lzc7pij4zatuSLRRgsqxdXfytXFL1XP/XZWTnx/Yhj8AgMFBAAgA2HLC2llNPffvNHH3/0W5sXvVmHpSl5//dwqrp6/oQ3ffthsX1Zh+Ohnet6hKK5INGwobl9SYeU6NqScVNS5e8QfjXkHlmCZ/8j9rxz3/N7m5CVXPfVUzr/5OUgl3lR8cW9WTalz+sfzi/gUBk5VspDisKqxfUH3qSTVnnktCkg1pamLVqp1ZYvjj9tWaf01zr/2RosZkEjavsfLTxkEyLLt5WWHtrGoXvi05noxx0iGR5lplf+kORAPTZCEOq6qe/7r80sGlV7CxbNRMhvzOH1dj6knFwUzaOXsNp8qGSWXvzPPKjt6z8E5FwXQ69PZ51S79QGHtTDKPY9S4qqB+wRMpbs0rbiVzBgZzL2jWKyk7dq9Ku9+u7Mhd6Rx9iVblVYX1c8u/bm2ksHpaQeWEcmP3LriroVb1tJqzz6eh3/NJmBg109B+4wI4GzUV1i8orF9Uq3JclVN/pkz5qIp7HlN+/MF0zsbkhRDMvqhW9dSatz3z8n9V5bU/SeYcvQZzhkbBtFrV0ytU8FopDpOwtTmt1vxrql38dvKeSgdgAMA2YI7/1WNb42s3AAD6JJPJlw98UHPHf19xWNuwD6rG8VYeomlt2rAh3tjqFOPKLx1Rfvw+zZ/6y/RD7EYdU0Yyyw31k6Q4CU+3cJXK0KGfVXHPY3LcrGwcava1P1L1Gg0BXp5J5/ayW/pcXW/GuBq59e935j0L6xdUO/+NazRHo0m7q66WmNrkNXoFYbZxsyrtfY/Gbv8HihqXFVRfUzD3shpTTyuoHJeNkyHiSeB3Hf+pbFwZ48nL71Bx99tV2PVm+cUDmj32Wc299scrVqg5XkFDh39e5QMfTDpRV15Vc+ZZ1aeeVNyqpO9pm/AeYBwZ48nNjCi/4xEV9zymzNCtmn7xP2n+zBfX8eWB6X+PuwbHYZxMfzOlJVjZ9P3BJl8Q8D4BANgmCAABAABwwzFOVo5fumaNZjZmH335pSOKg2mFjUtrWD8Zaruxw/E3nl86pLB2bsvvJwAAg4QhwAAAALjh2LipqLm1h03buLWuuR23S6DWml9/8xgAAHB1nA3YBgAAAAAAAIAtigAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAADjAAQAAAAAAAAGGAEgAAAAAAAAMAAIwAEAAAAAAAABhgBIAAAAAAAADDACAABAAAAAACAAUYACAAAAAAAAAwwAkAAAAAAAABggBEAAgAAAAAAAAOMABAAAAAAAAAYYASAAAAAAAAAwAAjAAQAAAAAAAAGGAEgAAAAAAAAMMAIAAEAAAAAAIABRgAIAAAAAAAADDACQAAAAAAAAGCAEQACAAAAAAAAA4wAEAAAAAAAABhgBIAAAAAAAADAACMABAAAAAAAAAYYASAAAAAAAAAwwAgAAQAAAAAAgAFGAAgAAAAAAAAMMAJAAAAAAAAAYIARAAIAAAAAAAAD7P8Pn/Q7QI3s8AMAAAAASUVORK5CYII=",
	};
	auto EnsureSponsorQrTexture = [&]() -> bool {
		if(s_SponsorQrTextureReady && s_SponsorQrTexture.IsValid())
			return true;
		if(s_SponsorQrTextureTried)
			return false;
		s_SponsorQrTextureTried = true;

		std::string CleanBase64;
		size_t Base64Size = 0;
		for(const char *pChunk : s_apSponsorQrPngBase64)
			Base64Size += str_length(pChunk);
		CleanBase64.reserve(Base64Size);
		for(const char *pChunk : s_apSponsorQrPngBase64)
		{
			const char *pPayload = pChunk;
			if(const char *pComma = str_find(pPayload, ","))
				pPayload = pComma + 1;
			for(const char *pChar = pPayload; *pChar != '\0'; ++pChar)
			{
				if(*pChar == ' ' || *pChar == '\t' || *pChar == '\r' || *pChar == '\n')
					continue;
				CleanBase64.push_back(*pChar);
			}
		}
		if(CleanBase64.empty())
			return false;

		const int MaxDecodedSize = static_cast<int>((CleanBase64.size() * 3) / 4 + 4);
		std::vector<uint8_t> vDecoded(MaxDecodedSize);
		const int DecodedSize = str_base64_decode(vDecoded.data(), MaxDecodedSize, CleanBase64.c_str());
		if(DecodedSize <= 0)
		{
			s_SponsorQrDecodeFailed = true;
			return false;
		}
		vDecoded.resize(DecodedSize);

		CImageInfo QrImage;
		if(!Graphics()->LoadPng(QrImage, vDecoded.data(), vDecoded.size(), "qmclient_sponsor_qr_base64"))
		{
			s_SponsorQrDecodeFailed = true;
			return false;
		}

		s_SponsorQrTexture = Graphics()->LoadTextureRawMove(QrImage, 0, "qmclient_sponsor_qr");
		s_SponsorQrTextureReady = s_SponsorQrTexture.IsValid();
		s_SponsorQrDecodeFailed = !s_SponsorQrTextureReady;
		return s_SponsorQrTextureReady;
	};
	if(m_QmClientSettingsTab == QMCLIENT_SETTINGS_TAB_CONTRIBUTORS)
	{
		g_QmClientEnsureSponsorQrTexture = EnsureSponsorQrTexture;
		g_QmClientRenderTexture = [&, this](const CUIRect &Rect, float Alpha) {
			if(!s_SponsorQrTexture.IsValid())
				return;
			Graphics()->TextureSet(s_SponsorQrTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
			IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, Rect.w, Rect.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		};
		RenderSettingsQmClientContributors(MainView, PrewarmOnly);
		g_QmClientEnsureSponsorQrTexture = {};
		g_QmClientRenderTexture = {};
		if(TabTransitionActive)
			Ui()->ClipDisable();
		return;
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
