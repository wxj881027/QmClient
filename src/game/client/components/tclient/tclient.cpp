#include "tclient.h"

#include "swap_countdown_message.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/engine.h>
#include <engine/external/regex.h>
#include <engine/external/tinyexpr.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/data_version.h>
#include <game/client/components/qmclient/keyword_reply_rules.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/qmclient/update_version.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>
#include <game/version.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <engine/shared/qm_update.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

static constexpr int64_t QMCLIENT_UPDATE_RETRY_INTERVAL = 15 * 60;
#if defined(CONF_FAMILY_WINDOWS)
static constexpr const char *QMCLIENT_INFO_URL = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
static constexpr const char *QMCLIENT_UPDATE_PACKAGE_NAME = "QmClient-windows.zip";
static constexpr const char *QMCLIENT_UPDATE_PACKAGE_SIGNATURE_NAME = "QmClient-windows.zip.sig";
static constexpr const char *QMCLIENT_UPDATE_MANIFEST_NAME = "QmClient-windows-update.json";
static constexpr const char *QMCLIENT_UPDATE_MANIFEST_SIGNATURE_NAME = "QmClient-windows-update.json.sig";
static constexpr int64_t QMCLIENT_UPDATE_MAX_PACKAGE_SIZE = 5LL * 1024 * 1024 * 1024;
static constexpr int64_t QMCLIENT_UPDATE_MAX_MANIFEST_SIZE = 32 * 1024 * 1024;
static constexpr int64_t QMCLIENT_UPDATE_CHECK_INTERVAL = 6 * 60 * 60;
#endif
static constexpr const char *MAP_CATEGORY_CACHE_FILE = "qmclient/map_categories.json";
static constexpr int64_t MAP_CATEGORY_CACHE_SAVE_DELAY_SEC = 5;
static constexpr const char *MAP_NOTES_FILE = "qmclient/map_notes.json";
static constexpr int64_t MAP_NOTES_SAVE_DELAY_SEC = 5;
static constexpr const char *MAP_HISTORY_FILE = "qmclient/map_history.json";
static constexpr const char *QMCLIENT_FREEZE_WAKEUP_TEXT = "快醒醒!";
static constexpr int LOCAL_SAVE_JOIN_HINT_MAX_ITEMS = 12;
static constexpr int GORES_DISTANCE_FIELD_TILE_SCAN_BUDGET = 4096;
static constexpr int GORES_DISTANCE_FIELD_VISUAL_TILE_BUDGET = 4096;
static constexpr int GORES_DISTANCE_FIELD_QUEUE_INIT_BUDGET = 4096;
static constexpr int GORES_DISTANCE_FIELD_DIJKSTRA_BUDGET = 2048;
static constexpr int GORES_DISTANCE_FIELD_REACHABLE_SCAN_BUDGET = 4096;

static constexpr float QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION = 2.0f;
static constexpr float QMCLIENT_TEXT_POPUP_FONT_SIZE = 30.0f;
static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET = vec2(34.0f, -78.0f);
static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT = vec2(18.0f, -16.0f);
static constexpr int QMCLIENT_COMBO_POPUP_WINDOW_SECONDS = 2;
static constexpr int QMCLIENT_COMBO_FREEZE_PARTICLE_COUNT = 14;
static constexpr float QMCLIENT_COMBO_FREEZE_PARTICLE_RADIUS = 42.0f;
static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_FROM = ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f);
static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_TO = ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f);
static constexpr const char *s_apKeywordNegationWords[] = {
	"不",
	"没",
	"無",
	"无",
	"別",
	"别",
	"勿",
	"莫",
	"非",
	"未",
	"沒",
};
static constexpr const char *s_apKeywordClauseContrastWords[] = {
	"但是",
	"但",
	"不过",
	"然而",
	"可是",
};
// 好友进服默认文案：英文 source key（extract 靠下方 Localize 字面量收录）
static constexpr const char *s_pFriendEnterBroadcastDefaultText = "%s joined this server";
static constexpr const char *s_pFriendEnterGreetDefaultText = "Hi!";

// 配置为空或仍是出厂默认时走 Localize(字面量)；用户自定义文案再 Localize（无译文则回退原文）
// 兼容旧版中文默认：映射到英文 source 后再 Localize，避免英文界面锁死中文
static const char *ResolveFriendEnterLocalizeText(const char *pConfigText, const char *pDefaultEnglish)
{
	if(pConfigText != nullptr)
	{
		if(str_comp(pConfigText, "%s好友进入本服") == 0)
			pConfigText = s_pFriendEnterBroadcastDefaultText;
		else if(str_comp(pConfigText, "你好啊!") == 0)
			pConfigText = s_pFriendEnterGreetDefaultText;
	}

	const bool UseDefault = pConfigText == nullptr || pConfigText[0] == '\0' || str_comp(pConfigText, pDefaultEnglish) == 0;
	if(UseDefault)
	{
		if(pDefaultEnglish == s_pFriendEnterBroadcastDefaultText || str_comp(pDefaultEnglish, s_pFriendEnterBroadcastDefaultText) == 0)
			return Localize("%s joined this server");
		if(pDefaultEnglish == s_pFriendEnterGreetDefaultText || str_comp(pDefaultEnglish, s_pFriendEnterGreetDefaultText) == 0)
			return Localize("Hi!");
		return Localize(pDefaultEnglish);
	}
	return Localize(pConfigText);
}

static int AutoReplySeparatorLength(const char *pStr);
static bool AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules);

namespace
{
	enum class ETextPopupType
	{
		FREEZE_WAKEUP = 0,
		NUM_TYPES,
	};

	struct STextPopupDefinition
	{
		const char *m_pText;
	};

	static constexpr std::array<STextPopupDefinition, (int)ETextPopupType::NUM_TYPES> s_aTextPopupDefinitions = {{
		{QMCLIENT_FREEZE_WAKEUP_TEXT},
	}};

	enum class EFreezeWakeupType
	{
		NONE,
		LOCAL_HAMMER,
		EXTERNAL_HAMMER,
	};

	float TextPopupDuration(int TextType)
	{
		(void)TextType;
		return QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION;
	}

	void AddComboFreezeParticleRing(CGameClient *pGameClient, vec2 Pos, float Alpha)
	{
		CParticle Part;
		Part.SetDefault();
		Part.m_Spr = SPRITE_PART_SNOWFLAKE;
		Part.m_LifeSpan = 0.7f;
		Part.m_StartSize = 18.0f;
		Part.m_EndSize = 7.0f;
		Part.m_UseAlphaFading = true;
		Part.m_StartAlpha = Alpha;
		Part.m_EndAlpha = 0.0f;
		Part.m_Rotspeed = pi;
		Part.m_Friction = 0.85f;
		Part.m_FlowAffected = 0.0f;
		Part.m_Collides = false;
		Part.m_Color.a = Alpha;

		for(int Index = 0; Index < QMCLIENT_COMBO_FREEZE_PARTICLE_COUNT; ++Index)
		{
			const float Angle = 2.0f * pi * Index / QMCLIENT_COMBO_FREEZE_PARTICLE_COUNT;
			const vec2 Direction = vec2(std::cos(Angle), std::sin(Angle));
			Part.m_Pos = Pos + Direction * QMCLIENT_COMBO_FREEZE_PARTICLE_RADIUS;
			Part.m_Vel = Direction * 95.0f;
			Part.m_Rot = Angle;
			pGameClient->m_Particles.Add(CParticles::GROUP_EXTRA, &Part);
		}
	}

	EFreezeWakeupType DetectFreezeWakeupType(CGameClient *pGameClient, int ClientId, int SnapshotTick)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return EFreezeWakeupType::NONE;
		SQmHammerHitRecord aHits[MAX_CLIENTS];
		const int NumHits = pGameClient->HammerHitTracker().FindTargetHitsAtTick(
			ClientId,
			SnapshotTick,
			aHits,
			std::size(aHits),
			pGameClient->HammerHitConnectionFilter());
		bool LocalHammer = false;
		for(int HitIndex = 0; HitIndex < NumHits; ++HitIndex)
		{
			const SQmHammerHitRecord &Hit = aHits[HitIndex];
			if(!Hit.m_TargetWoke)
				continue;
			const EQmHammerHitRelation Relation = QmClassifyHammerHitRelation(&Hit, ClientId, pGameClient->m_aLocalIds[0], pGameClient->m_aLocalIds[1]);
			if(Relation == EQmHammerHitRelation::EXTERNAL)
				return EFreezeWakeupType::EXTERNAL_HAMMER;
			if(Relation == EQmHammerHitRelation::SELF || Relation == EQmHammerHitRelation::COUNTERPART)
				LocalHammer = true;
		}
		return LocalHammer ? EFreezeWakeupType::LOCAL_HAMMER : EFreezeWakeupType::NONE;
	}
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SKeywordReplyRule
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
	bool m_HasExplicitRenameFlag = false;
	bool m_HasExplicitRegexFlag = false;
};

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

[[maybe_unused]] static void ParseKeywordReplyRules(const char *pRules, std::vector<SKeywordReplyRule> &vOutRules)
{
	vOutRules.clear();
	if(!pRules || pRules[0] == '\0')
		return;

	const char *pCursor = pRules;
	while(*pCursor)
	{
		char aLine[sizeof(g_Config.m_QmKeywordReplyRules)];
		int LineLen = 0;
		while(*pCursor && *pCursor != '\n' && *pCursor != '\r')
		{
			if(LineLen < (int)sizeof(aLine) - 1)
				aLine[LineLen++] = *pCursor;
			++pCursor;
		}
		aLine[LineLen] = '\0';

		while(*pCursor == '\n' || *pCursor == '\r')
			++pCursor;

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
		if(pKeywords[0] == '\0' || pReply[0] == '\0')
			continue;

		vOutRules.push_back({pKeywords, pReply, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag});
	}
}

[[maybe_unused]] static void BuildKeywordReplyRules(const std::vector<SKeywordReplyRule> &vRules, char *pOutRules, size_t OutRulesSize)
{
	if(!pOutRules || OutRulesSize == 0)
		return;

	pOutRules[0] = '\0';
	for(const auto &Rule : vRules)
	{
		if(Rule.m_Keywords.empty() || Rule.m_Reply.empty())
			continue;

		if(pOutRules[0] != '\0')
			str_append(pOutRules, "\n", OutRulesSize);
		if(Rule.m_AutoRename)
			str_append(pOutRules, "[rename] ", OutRulesSize);
		if(Rule.m_Regex)
			str_append(pOutRules, "[regex] ", OutRulesSize);
		str_append(pOutRules, Rule.m_Keywords.c_str(), OutRulesSize);
		str_append(pOutRules, "=>", OutRulesSize);
		str_append(pOutRules, Rule.m_Reply.c_str(), OutRulesSize);
	}
}

static std::string BuildFriendEnterBroadcastText(const char *pTemplate, std::string_view FriendNames)
{
	const char *pFormat = pTemplate != nullptr && pTemplate[0] != '\0' ? pTemplate : s_pFriendEnterBroadcastDefaultText;
	std::string Result;
	Result.reserve(str_length(pFormat) + FriendNames.size() + 8);

	const std::string_view Placeholder = "%s";
	const std::string_view FormatView = pFormat;
	size_t Pos = 0;
	bool Replaced = false;
	while(true)
	{
		const size_t Match = FormatView.find(Placeholder, Pos);
		if(Match == std::string_view::npos)
		{
			Result.append(FormatView.substr(Pos));
			break;
		}

		Result.append(FormatView.substr(Pos, Match - Pos));
		Result.append(FriendNames);
		Pos = Match + Placeholder.size();
		Replaced = true;
	}

	if(!Replaced)
	{
		// Backward compatibility: if users remove '%s', keep friend names visible.
		Result.clear();
		Result.reserve(FriendNames.size() + FormatView.size());
		Result.append(FriendNames);
		Result.append(FormatView);
	}

	return Result;
}

static bool IsHardBlockedForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_SOLID || TileIndex == TILE_NOHOOK;
}

static bool IsHardBlockedGoresDistanceFieldIndex(const CTile *pGame, const CTile *pFront, int Index)
{
	if(!pGame || Index < 0)
		return true;

	return IsHardBlockedForGoresDistanceField(pGame[Index].m_Index) ||
	       (pFront && IsHardBlockedForGoresDistanceField(pFront[Index].m_Index));
}

static bool IsGoresDistanceFieldTileStandable(const CCollision *pCollision, const CTile *pGame, const CTile *pFront, int Index)
{
	if(!pCollision || !pGame || Index < 0)
		return false;

	const vec2 Pos = pCollision->GetPos(Index);
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	const vec2 aSamples[] = {
		vec2(-HalfSize, -HalfSize),
		vec2(HalfSize, -HalfSize),
		vec2(-HalfSize, HalfSize),
		vec2(HalfSize, HalfSize),
	};
	for(const vec2 SampleOffset : aSamples)
	{
		const int SampleIndex = pCollision->GetPureMapIndex(Pos + SampleOffset);
		if(IsHardBlockedGoresDistanceFieldIndex(pGame, pFront, SampleIndex))
			return false;
	}
	return true;
}

static int GoresDistanceFieldMoveBlockMask(int FromIndex, int ToIndex, int Width)
{
	if(Width <= 0)
		return 0;
	if(ToIndex == FromIndex + 1)
		return CANTMOVE_RIGHT;
	if(ToIndex == FromIndex - 1)
		return CANTMOVE_LEFT;
	if(ToIndex == FromIndex + Width)
		return CANTMOVE_DOWN;
	if(ToIndex == FromIndex - Width)
		return CANTMOVE_UP;
	return 0;
}

static bool IsGoresDistanceFieldStepAllowed(const CCollision *pCollision, int FromIndex, int ToIndex, int Width)
{
	const int BlockMask = GoresDistanceFieldMoveBlockMask(FromIndex, ToIndex, Width);
	if(!pCollision || BlockMask == 0)
		return false;

	const int Restrictions = pCollision->GetMoveRestrictions(nullptr, nullptr, pCollision->GetPos(FromIndex), 18.0f, FromIndex);
	return (Restrictions & BlockMask) == 0;
}

static bool IsPenaltyTileForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_DEATH || TileIndex == TILE_FREEZE || TileIndex == TILE_DFREEZE || TileIndex == TILE_LFREEZE;
}

static bool IsRewardTileForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_UNFREEZE || TileIndex == TILE_DUNFREEZE || TileIndex == TILE_LUNFREEZE;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum EGoresCMapValue
{
	GORES_CMAP_NORMAL = 0,
	GORES_CMAP_BLOCKED = 1,
	GORES_CMAP_TELEPORT = 2,
	GORES_CMAP_PENALTY = 3,
	GORES_CMAP_REWARD = 4,
};

static bool IsPlayerTeleportInputTileForGoresDistanceField(int TeleType)
{
	return TeleType == TILE_TELEIN || TeleType == TILE_TELEINEVIL ||
	       TeleType == TILE_TELECHECKIN || TeleType == TILE_TELECHECKINEVIL;
}

static bool IsDirectTeleportInputTileForGoresDistanceField(int TeleType)
{
	return TeleType == TILE_TELEIN || TeleType == TILE_TELEINEVIL;
}

static unsigned char GoresSemanticImageToCMapValue(const char *pImageName)
{
	if(!pImageName || pImageName[0] == '\0')
		return GORES_CMAP_NORMAL;

	// Gores visual semantics:
	//   sun        -> unfreeze/reward
	//   blackwater -> deep freeze/penalty
	//   water      -> freeze/penalty
	if(str_find_nocase(pImageName, "sun") != nullptr)
		return GORES_CMAP_REWARD;

	if(str_find_nocase(pImageName, "blackwater") != nullptr ||
		str_find_nocase(pImageName, "darkwater") != nullptr ||
		str_find_nocase(pImageName, "black_water") != nullptr ||
		str_find_nocase(pImageName, "dark_water") != nullptr ||
		str_find_nocase(pImageName, "water") != nullptr)
		return GORES_CMAP_PENALTY;

	return GORES_CMAP_NORMAL;
}

static int GoresDistanceFieldTraversalCost(unsigned char CMapValue, bool IsStart, bool IsFinish)
{
	static constexpr int NORMAL_COST = 100;
	static constexpr int PENALTY_COST = 4500;
	static constexpr int REWARD_COST = 70;

	if(IsStart || IsFinish)
		return NORMAL_COST;

	if(CMapValue == GORES_CMAP_PENALTY)
		return PENALTY_COST;
	if(CMapValue == GORES_CMAP_REWARD)
		return REWARD_COST;
	return NORMAL_COST;
}

CTClient::CTClient()
{
	OnReset();
}

void CTClient::ConRandomTee(IConsole::IResult *pResult, void *pUserData) {}

void CTClient::ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// Resolve type to randomize
	// Check length of type (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag)
	bool RandomizeBody = false;
	bool RandomizeFeet = false;
	bool RandomizeSkin = false;
	bool RandomizeFlag = false;

	if(pResult->NumArguments() == 0)
	{
		RandomizeBody = true;
		RandomizeFeet = true;
		RandomizeSkin = true;
		RandomizeFlag = true;
	}
	else if(pResult->NumArguments() == 1)
	{
		const char *Type = pResult->GetString(0);
		int Length = Type ? str_length(Type) : 0;
		if(Length == 1 && Type[0] == '0')
		{ // Randomize all
			RandomizeBody = true;
			RandomizeFeet = true;
			RandomizeSkin = true;
			RandomizeFlag = true;
		}
		else if(Length == 1)
		{
			// Randomize body
			RandomizeBody = Type[0] == '1';
		}
		else if(Length == 2)
		{
			// Check for body and feet
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
		}
		else if(Length == 3)
		{
			// Check for body, feet and skin
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
		}
		else if(Length == 4)
		{
			// Check for body, feet, skin and flag
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
			RandomizeFlag = Type[3] == '1';
		}
	}

	if(RandomizeBody)
		RandomBodyColor();
	if(RandomizeFeet)
		RandomFeetColor();
	if(RandomizeSkin)
		RandomSkin(pUserData);
	if(RandomizeFlag)
		RandomFlag(pUserData);
	pThis->GameClient()->SendInfo(false);
}

void CTClient::OnInit()
{
	TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_UpdateAutoEnabled = g_Config.m_QmAutoUpdate != 0;
	if(g_Config.m_QmAutoUpdate)
		FetchQmClientUpdateInfo();

	// 先在 qmclient/ 目录找，找不到再返回上一级目录找
	const bool MissingQmClientFolder = !Storage()->FolderExists("qmclient", IStorage::TYPE_ALL);
	const bool MissingGuiLogo =
		!Storage()->FileExists("qmclient/gui_logo.png", IStorage::TYPE_ALL) &&
		!Storage()->FileExists("gui_logo.png", IStorage::TYPE_ALL);

	char aError[512] = "";
	if(MissingQmClientFolder || MissingGuiLogo)
	{
		str_format(aError, sizeof(aError), Localize("%s not found", DATA_VERSION_PATH), "data/qmclient/gui_logo.png");
		SWarning Warning(aError, Localize("You may have replaced only DDNet.exe and skipped QmClient.zip.\nPlease install the full QmClient package.", "data_version.h"));
		Client()->AddWarning(Warning);
	}
	else
	{
		CheckDataVersion(aError, sizeof(aError), Storage()->OpenFile(DATA_VERSION_PATH, IOFLAG_READ, IStorage::TYPE_ALL));
		if(aError[0] != '\0')
		{
			SWarning Warning(aError, Localize("You may have installed only DDNet.exe. Please use the full QmClient folder.", "data_version.h"));
			Client()->AddWarning(Warning);
		}
	}
	LoadMapCategoryCache();
	LoadMapNotes();
	LoadMapHistory();
}

void CTClient::OnShutdown()
{
	auto AbortTask = [](std::shared_ptr<CHttpRequest> &pTask) {
		if(pTask)
		{
			pTask->Abort();
			pTask = nullptr;
		}
	};

	AbortTask(m_pQmClientUpdateInfoTask);
	AbortTask(m_pUpdatePackageTask);
	AbortTask(m_pUpdatePackageSignatureTask);
	AbortTask(m_pUpdateManifestTask);
	AbortTask(m_pUpdateManifestSignatureTask);
	if(!m_UpdateInstallerStarted)
		RemoveUpdateTempFiles();
	EndMapHistorySession(true);
	if(m_MapHistoryDirty)
		SaveMapHistory();
	if(m_MapNotesDirty)
		SaveMapNotes();
	UnloadTextPopupCaches();
	ClearFreezeWakeupPopups();
}

void CTClient::OnWindowResize()
{
	UnloadTextPopupCaches();
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if(pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

bool CTClient::SendNonDuplicateMessage(int Team, const char *pLine)
{
	if(str_comp(pLine, m_PreviousOwnMessage) != 0)
	{
		GameClient()->m_Chat.SendChat(Team, pLine);
		return true;
	}
	str_copy(m_PreviousOwnMessage, pLine);
	return false;
}

void CTClient::TryAppendKeywordReplyRenameSuffix(bool UseDummy)
{
	char *pConfigName = UseDummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const int ConfigNameSize = UseDummy ? (int)sizeof(g_Config.m_ClDummyName) : (int)sizeof(g_Config.m_PlayerName);
	if(!pConfigName || pConfigName[0] == '\0')
		return;

	const int NameLen = str_length(pConfigName);
	const bool AlreadyHasQia = NameLen >= 3 &&
				   (unsigned char)pConfigName[NameLen - 3] == 0xE6 &&
				   (unsigned char)pConfigName[NameLen - 2] == 0x81 &&
				   (unsigned char)pConfigName[NameLen - 1] == 0xB0;
	if(AlreadyHasQia || NameLen + 3 >= ConfigNameSize)
		return;

	char aNewName[MAX_NAME_LENGTH];
	str_copy(aNewName, pConfigName, sizeof(aNewName));
	str_append(aNewName, "恰", sizeof(aNewName));
	str_copy(pConfigName, aNewName, ConfigNameSize);

	if(UseDummy)
		GameClient()->SendDummyInfo(false);
	else
		GameClient()->SendInfo(false);
}

static int AutoReplySeparatorLength(const char *pStr)
{
	const unsigned char C0 = (unsigned char)pStr[0];
	if(C0 == ',' || C0 == ';' || C0 == '|' || C0 == '\n' || C0 == '\r')
		return 1;
	if(C0 == 0xEF && (unsigned char)pStr[1] == 0xBC)
	{
		const unsigned char C2 = (unsigned char)pStr[2];
		if(C2 == 0x8C || C2 == 0x9B)
			return 3;
	}
	// Full-width vertical bar `｜` (U+FF5C), commonly produced by CJK IMEs.
	if(C0 == 0xEF && (unsigned char)pStr[1] == 0xBD && (unsigned char)pStr[2] == 0x9C)
		return 3;
	return 0;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum class EAutoReplyTokenMode
{
	Literal,
	RegexAuto,
	RegexDelimited,
};

static const char *FindDelimitedAutoReplyTokenEnd(const char *pTokenStart)
{
	if(!pTokenStart || pTokenStart[0] != '/')
		return nullptr;

	// Keep `/.../` together so regex alternation does not collide with the rule separators.
	bool Escaped = false;
	for(const char *pCursor = pTokenStart + 1; *pCursor; ++pCursor)
	{
		if(Escaped)
		{
			Escaped = false;
			continue;
		}
		if(*pCursor == '\\')
		{
			Escaped = true;
			continue;
		}
		if(*pCursor != '/')
			continue;

		++pCursor;
		if(*pCursor == 'i' || *pCursor == 'I')
			++pCursor;

		const char *pAfterWhitespace = str_utf8_skip_whitespaces(pCursor);
		if(*pAfterWhitespace != '\0' && AutoReplySeparatorLength(pAfterWhitespace) == 0)
			return nullptr;
		return pAfterWhitespace;
	}

	return nullptr;
}

template<typename F>
static bool ForEachAutoReplyToken(const char *pText, F &&Fn)
{
	if(!pText || pText[0] == '\0')
		return false;

	const char *pCursor = pText;
	while(*pCursor)
	{
		int SepLen = AutoReplySeparatorLength(pCursor);
		while(*pCursor && SepLen > 0)
		{
			pCursor += SepLen;
			SepLen = AutoReplySeparatorLength(pCursor);
		}

		if(*pCursor == '\0')
			break;

		const char *pTokenStart = pCursor;
		const char *pTokenEnd = nullptr;
		const char *pTrimmedStart = str_utf8_skip_whitespaces(pTokenStart);
		if(*pTrimmedStart == '/')
			pTokenEnd = FindDelimitedAutoReplyTokenEnd(pTrimmedStart);

		if(!pTokenEnd)
		{
			pTokenEnd = pCursor;
			while(*pTokenEnd && AutoReplySeparatorLength(pTokenEnd) == 0)
				++pTokenEnd;
		}

		pCursor = pTokenEnd;

		std::string Token(pTokenStart, pTokenEnd - pTokenStart);
		char *pMutableToken = Token.data();
		char *pTrimmedToken = (char *)str_utf8_skip_whitespaces(pMutableToken);
		str_utf8_trim_right(pTrimmedToken);
		if(pTrimmedToken[0] == '\0')
			continue;

		if(Fn(std::string_view(pTrimmedToken)))
			return true;
	}

	return false;
}

static bool IsAutoReplyRegexEscapeMarker(char C)
{
	switch(C)
	{
	case 'd':
	case 'D':
	case 's':
	case 'S':
	case 'w':
	case 'W':
	case 'b':
	case 'B':
	case 'A':
	case 'Z':
	case 'z':
	case 't':
	case 'n':
	case 'r':
	case 'v':
	case 'f':
	case '\\':
	case '.':
	case '+':
	case '*':
	case '?':
	case '^':
	case '$':
	case '|':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}':
		return true;
	default:
		return false;
	}
}

static bool LooksLikeAutoReplyRegex(std::string_view Token)
{
	if(Token.empty())
		return false;

	if(Token.front() == '^' || Token.back() == '$')
		return true;

	if(Token.find(".*") != std::string_view::npos ||
		Token.find(".+") != std::string_view::npos ||
		Token.find(".?") != std::string_view::npos)
	{
		return true;
	}

	if(Token.find('[') != std::string_view::npos || Token.find(']') != std::string_view::npos ||
		Token.find('(') != std::string_view::npos || Token.find(')') != std::string_view::npos ||
		Token.find('{') != std::string_view::npos || Token.find('}') != std::string_view::npos)
	{
		return true;
	}

	for(size_t i = 0; i + 1 < Token.size(); ++i)
	{
		if(Token[i] == '\\' && IsAutoReplyRegexEscapeMarker(Token[i + 1]))
			return true;
	}

	return false;
}

static EAutoReplyTokenMode ParseAutoReplyTokenMode(std::string_view Token, std::string &OutPattern, bool &OutCaseInsensitive)
{
	OutPattern.clear();
	OutCaseInsensitive = true;

	if(Token.size() >= 2 && Token.front() == '/')
	{
		bool Escaped = false;
		for(size_t i = 1; i < Token.size(); ++i)
		{
			const char C = Token[i];
			if(Escaped)
			{
				Escaped = false;
				continue;
			}
			if(C == '\\')
			{
				Escaped = true;
				continue;
			}
			if(C != '/')
				continue;

			const std::string_view Flags = Token.substr(i + 1);
			if(Flags.empty() || Flags == "i" || Flags == "I")
			{
				OutPattern.assign(Token.substr(1, i - 1));
				return EAutoReplyTokenMode::RegexDelimited;
			}
			break;
		}
	}

	if(LooksLikeAutoReplyRegex(Token))
	{
		OutPattern.assign(Token.begin(), Token.end());
		return EAutoReplyTokenMode::RegexAuto;
	}

	OutPattern.assign(Token.begin(), Token.end());
	return EAutoReplyTokenMode::Literal;
}

static void ParseExplicitAutoReplyRegexPattern(std::string_view PatternText, std::string &OutPattern, bool &OutCaseInsensitive)
{
	OutCaseInsensitive = true;
	if(ParseAutoReplyTokenMode(PatternText, OutPattern, OutCaseInsensitive) == EAutoReplyTokenMode::RegexDelimited)
		return;

	OutPattern.assign(PatternText.begin(), PatternText.end());
}

[[maybe_unused]] static bool AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules)
{
	if(!pOutRules || OutRulesSize == 0 || !pRules || pRules[0] == '\0')
		return false;

	const size_t CurrentLen = str_length(pOutRules);
	const size_t RulesLen = str_length(pRules);
	const size_t SeparatorLen = CurrentLen > 0 && pOutRules[CurrentLen - 1] != '\n' ? 1 : 0;
	if(CurrentLen + SeparatorLen + RulesLen >= OutRulesSize)
		return false;

	if(SeparatorLen > 0)
		str_append(pOutRules, "\n", OutRulesSize);

	str_append(pOutRules, pRules, OutRulesSize);
	return true;
}

static bool IsKeywordClauseSeparatorCodepoint(int Codepoint)
{
	return Codepoint == '\n' || Codepoint == '\r' ||
	       Codepoint == ',' || Codepoint == '.' || Codepoint == ';' || Codepoint == ':' || Codepoint == '!' || Codepoint == '?' ||
	       Codepoint == 0xFF0C || Codepoint == 0x3002 || Codepoint == 0xFF1B || Codepoint == 0xFF1A || Codepoint == 0xFF01 || Codepoint == 0xFF1F;
}

static const char *FindLastKeywordClauseSeparatorBoundary(const char *pMessageStart, const char *pLimit)
{
	const char *pBoundary = pMessageStart;
	const char *pCursor = pMessageStart;
	while(*pCursor && pCursor < pLimit)
	{
		const char *pCharStart = pCursor;
		const int Codepoint = str_utf8_decode(&pCursor);
		if(pCharStart >= pLimit)
			break;
		if(IsKeywordClauseSeparatorCodepoint(Codepoint))
			pBoundary = pCursor;
	}
	return pBoundary;
}

static const char *FindLastKeywordContrastBoundary(const char *pMessageStart, const char *pLimit)
{
	const char *pBoundary = pMessageStart;
	for(const char *pContrast : s_apKeywordClauseContrastWords)
	{
		const char *pSearchCursor = pMessageStart;
		while(pSearchCursor && pSearchCursor < pLimit)
		{
			const char *pMatchEnd = nullptr;
			const char *pMatch = str_utf8_find_nocase(pSearchCursor, pContrast, &pMatchEnd);
			if(!pMatch || pMatch >= pLimit)
				break;
			if(pMatchEnd && pMatchEnd <= pLimit && pMatchEnd > pBoundary)
				pBoundary = pMatchEnd;

			if(!pMatchEnd || pMatchEnd <= pSearchCursor)
				break;
			pSearchCursor = pMatchEnd;
		}
	}
	return pBoundary;
}

static const char *FindKeywordClauseStart(const char *pMessageStart, const char *pMatchStart)
{
	const char *pSeparatorBoundary = FindLastKeywordClauseSeparatorBoundary(pMessageStart, pMatchStart);
	const char *pContrastBoundary = FindLastKeywordContrastBoundary(pMessageStart, pMatchStart);
	return pSeparatorBoundary > pContrastBoundary ? pSeparatorBoundary : pContrastBoundary;
}

static int CountKeywordNegationsInRange(const char *pRangeStart, const char *pRangeEnd)
{
	if(!pRangeStart || !pRangeEnd || pRangeEnd <= pRangeStart)
		return 0;

	int NegationCount = 0;
	for(const char *pNegationWord : s_apKeywordNegationWords)
	{
		const char *pSearchCursor = pRangeStart;
		while(pSearchCursor && pSearchCursor < pRangeEnd)
		{
			const char *pMatchEnd = nullptr;
			const char *pMatch = str_utf8_find_nocase(pSearchCursor, pNegationWord, &pMatchEnd);
			if(!pMatch || pMatch >= pRangeEnd)
				break;
			if(pMatchEnd && pMatchEnd <= pRangeEnd)
				++NegationCount;

			if(!pMatchEnd || pMatchEnd <= pSearchCursor)
				break;
			pSearchCursor = pMatchEnd;
		}
	}

	return NegationCount;
}

static bool HasPositiveKeywordMatch(const char *pMessage, const char *pToken)
{
	const char *pSearchCursor = pMessage;
	while(*pSearchCursor)
	{
		const char *pMatchEnd = nullptr;
		const char *pMatch = str_utf8_find_nocase(pSearchCursor, pToken, &pMatchEnd);
		if(!pMatch)
			return false;

		const char *pClauseStart = FindKeywordClauseStart(pMessage, pMatch);
		const int NegationCount = CountKeywordNegationsInRange(pClauseStart, pMatch);
		if((NegationCount % 2) == 0)
			return true;

		if(!pMatchEnd || pMatchEnd <= pSearchCursor)
			return false;
		pSearchCursor = pMatchEnd;
	}
	return false;
}

static bool MatchAutoReplyLiteralToken(const char *pMessage, const char *pToken, bool UseNegationFilter)
{
	if(!UseNegationFilter)
		return str_utf8_find_nocase(pMessage, pToken) != nullptr;
	return HasPositiveKeywordMatch(pMessage, pToken);
}

static bool MatchAutoReplyRuleKeywords(const char *pMessage, const char *pKeywords, bool UseNegationFilter, bool ForceRegex)
{
	if(ForceRegex)
	{
		std::string Pattern;
		bool CaseInsensitive = true;
		ParseExplicitAutoReplyRegexPattern(pKeywords, Pattern, CaseInsensitive);
		Regex Re(Pattern, CaseInsensitive);
		return Re.error().empty() && Re.test(pMessage);
	}

	return ForEachAutoReplyToken(pKeywords, [&](std::string_view Token) {
		std::string Pattern;
		bool CaseInsensitive = true;
		const EAutoReplyTokenMode Mode = ParseAutoReplyTokenMode(Token, Pattern, CaseInsensitive);
		if(Mode != EAutoReplyTokenMode::Literal)
		{
			Regex Re(Pattern, CaseInsensitive);
			if(Re.error().empty())
				return Re.test(pMessage);
			if(Mode == EAutoReplyTokenMode::RegexDelimited)
				return false;
		}

		std::string TokenString(Token);
		return MatchAutoReplyLiteralToken(pMessage, TokenString.c_str(), UseNegationFilter);
	});
}

static bool MatchAutoReplyRules(const char *pMessage, const char *pRules, char *pOutReply, size_t OutReplySize, bool &OutAutoRename, bool UseNegationFilter)
{
	OutAutoRename = false;
	if(!pRules || pRules[0] == '\0')
		return false;

	static constexpr int MAX_MATCHED_REPLIES = 32;
	char aaMatchedReplies[MAX_MATCHED_REPLIES][256];
	bool aMatchedRenameFlags[MAX_MATCHED_REPLIES] = {};
	int MatchedReplyCount = 0;

	char aDecodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
	QmKeywordReplyRules::DecodeFromConfig(pRules, aDecodedRules, sizeof(aDecodedRules));
	const char *pCursor = aDecodedRules;
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

		if(MatchAutoReplyRuleKeywords(pMessage, pKeywords, UseNegationFilter, RegexRule))
		{
			if(MatchedReplyCount < MAX_MATCHED_REPLIES)
			{
				str_copy(aaMatchedReplies[MatchedReplyCount], pReply, sizeof(aaMatchedReplies[MatchedReplyCount]));
				aMatchedRenameFlags[MatchedReplyCount] = AutoRename;
			}
			MatchedReplyCount++;
		}
	}

	if(MatchedReplyCount <= 0)
		return false;

	const int StoredReplyCount = MatchedReplyCount < MAX_MATCHED_REPLIES ? MatchedReplyCount : MAX_MATCHED_REPLIES;
	const int PickedIndex = secure_rand_below(StoredReplyCount);
	str_copy(pOutReply, aaMatchedReplies[PickedIndex], OutReplySize);
	OutAutoRename = aMatchedRenameFlags[PickedIndex];
	return true;
}

const char *CTClient::CurrentCommunityIdForFinishCheck() const
{
	IServerBrowser *pServerBrowser = ServerBrowser();
	if(!pServerBrowser)
		return nullptr;

	const char *pCommunityId = nullptr;
	const NETADDR *pServerAddr = Client()->ServerAddress();
	const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
	if(pEntry)
		pCommunityId = pEntry->m_Info.m_aCommunityId;
	else if(GameClient()->m_ConnectServerInfo)
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;

	if(!pCommunityId || pCommunityId[0] == '\0')
		return nullptr;
	return pCommunityId;
}

bool CTClient::TryHandleRedPacketAutoClaim(const CNetMsg_Sv_Chat *pMsg)
{
	if(pMsg == nullptr || pMsg->m_ClientId != -1 || pMsg->m_pMessage == nullptr)
		return false;

	const int MainClientId = GameClient()->m_aLocalIds[0];
	if(MainClientId < 0 || MainClientId >= MAX_CLIENTS || !GameClient()->m_aClients[MainClientId].m_Active)
		return false;

	const NETADDR *pServerAddress = Client()->ServerAddress();
	if(pServerAddress == nullptr)
		return false;
	char aServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(pServerAddress, aServerAddress, sizeof(aServerAddress), true);

	std::string Password;
	if(!m_RedPacketAutoClaim.TryPrepare(aServerAddress, GameClient()->m_aClients[MainClientId].m_aName, pMsg->m_pMessage, Password))
		return false;

	GameClient()->m_Chat.SendChatOnConn(IClient::CONN_MAIN, 0, Password.c_str(), true, false);
	return true;
}

void CTClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		HandleMapHistoryDeath(pMsg->m_Victim);
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		HandleMapHistoryTeamDeath(pMsg->m_Team);
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		HandleMapHistoryFinish(pMsg->m_ClientId, pMsg->m_Time);
	}

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;
		TryHandleRedPacketAutoClaim(pMsg);

		if(ClientId < 0)
		{
			return;
		}

		if(ClientId >= MAX_CLIENTS)
			return;
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		const auto IsOwnClientId = [&](int Id) {
			if(Id < 0)
				return false;
			if(Id == GameClient()->m_aLocalIds[0])
				return true;
			return Client()->DummyConnected() && Id == GameClient()->m_aLocalIds[1];
		};
		const bool IsOwnMessage = IsOwnClientId(ClientId);
		if(ClientId == LocalId && pMsg->m_pMessage != nullptr)
			str_copy(m_PreviousOwnMessage, pMsg->m_pMessage);

		// === 复读功能: 保存最新的公屏或队伍消息 ===
		const bool IsRepeatChatChannel = pMsg->m_Team == 0 || pMsg->m_Team == 1;
		if(ClientId >= 0 && ClientId < MAX_CLIENTS && IsRepeatChatChannel && pMsg->m_pMessage != nullptr)
		{
			const char *pMessage = pMsg->m_pMessage;
			const bool IsValidCandidate = pMessage[0] != '\0' && pMessage[0] != '/';
			const bool SenderIsActiveClient = GameClient()->m_aClients[ClientId].m_Active;
			const bool IsRepeatCandidate = !IsOwnMessage && SenderIsActiveClient && IsValidCandidate;
			// 保存最新的公屏或队伍消息（不是自己发的）
			if(IsRepeatCandidate)
			{
				str_copy(m_aLastChatMessage, pMessage, sizeof(m_aLastChatMessage));
				m_LastChatTeam = pMsg->m_Team;
			}
		}

		const auto TrySendAutoReply = [&](const char *pReply, bool UseDummy) -> bool {
			if(!pReply || pReply[0] == '\0')
				return false;
			int Cooldown = g_Config.m_QmAutoReplyCooldown;
			if(Cooldown < 0)
				Cooldown = 0;
			const int64_t Now = time_get();
			if(Cooldown > 0 && Now - m_LastAutoReplyTime < (int64_t)Cooldown * time_freq())
				return false;

			const int TargetConn = UseDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
			GameClient()->m_Chat.SendChatOnConn(TargetConn, 0, pReply);
			m_LastAutoReplyTime = Now;
			return true;
		};

		bool AutoReplyHandled = false;

		// === 关键词回复 ===
		if(!AutoReplyHandled && g_Config.m_QmKeywordReplyEnabled && !IsOwnMessage && pMsg->m_Team == 0 && pMsg->m_pMessage != nullptr)
		{
			const char *pMessage = pMsg->m_pMessage;
			if(pMessage[0] != '\0' && pMessage[0] != '/')
			{
				char aReply[256] = "";
				bool AutoRename = false;
				if(MatchAutoReplyRules(pMessage, g_Config.m_QmKeywordReplyRules, aReply, sizeof(aReply), AutoRename, true))
				{
					const bool UseDummy = g_Config.m_QmKeywordReplyUseDummy && Client()->DummyConnected();
					AutoReplyHandled = TrySendAutoReply(aReply, UseDummy);
					if(AutoReplyHandled && AutoRename)
						TryAppendKeywordReplyRenameSuffix(UseDummy);
				}
			}
		}

		bool PingMessage = false;

		bool ValidIds = !(GameClient()->m_aLocalIds[0] < 0 || (GameClient()->Client()->DummyConnected() && GameClient()->m_aLocalIds[1] < 0));

		if(ValidIds && ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && (!GameClient()->Client()->DummyConnected() || ClientId != GameClient()->m_aLocalIds[1]))
		{
			PingMessage |= LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[0]].m_aName);
			PingMessage |= GameClient()->Client()->DummyConnected() && LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[1]].m_aName);
		}

		if(pMsg->m_Team == TEAM_WHISPER_RECV)
			PingMessage = true;

		if(!PingMessage)
			return;

		char aPlayerName[MAX_NAME_LENGTH];
		str_copy(aPlayerName, GameClient()->m_aClients[ClientId].m_aName, sizeof(aPlayerName));

		bool PlayerMuted = GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore;
		if(g_Config.m_TcAutoReplyMuted && PlayerMuted)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w \"%s\" %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}

		bool WindowActive = m_pGraphics && m_pGraphics->WindowActive();
		if(g_Config.m_TcAutoReplyMinimized && !WindowActive && m_pGraphics)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w \"%s\" %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}
	}

	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy]; // Do not care about spec behaviour
		const bool Afk = LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Afk; // TODO Depends on server afk time
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		if(pMsg->m_Timeout && !Afk)
		{
			char aDescription[VOTE_DESC_LENGTH];
			char aReason[VOTE_REASON_LENGTH];
			str_copy(aDescription, pMsg->m_pDescription);
			str_copy(aReason, pMsg->m_pReason);
			bool KickVote = str_startswith(aDescription, "Kick ") != 0 ? true : false;
			bool SpecVote = str_startswith(aDescription, "Pause ") != 0 ? true : false;
			bool SettingVote = !KickVote && !SpecVote;
			bool RandomMapVote = SettingVote && str_find_nocase(aDescription, "random");
			bool MapCoolDown = SettingVote && (str_find_nocase(aDescription, "change map") || str_find_nocase(aDescription, "no not change map"));
			bool CategoryVote = SettingVote && (str_find_nocase(aDescription, "☐") || str_find_nocase(aDescription, "☒"));
			bool FunVote = SettingVote && str_find_nocase(aDescription, "funvote");
			bool MapVote = SettingVote && !RandomMapVote && !MapCoolDown && !CategoryVote && !FunVote && (str_find_nocase(aDescription, "Map:") || str_find_nocase(aDescription, "★") || str_find_nocase(aDescription, "✰"));

			const int AutoMapVote = std::clamp(g_Config.m_TcAutoVoteWhenFar, 0, 2);
			if(AutoMapVote != 0 && (MapVote || RandomMapVote))
			{
				int RaceTime = 0;
				if(GameClient()->m_Snap.m_pGameInfoObj && GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
					RaceTime = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();

				if(RaceTime / 60 >= g_Config.m_TcAutoVoteWhenFarTime)
				{
					CGameClient::CClientData *pVoteCaller = nullptr;
					int CallerId = -1;
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameClient()->m_aStats[i].IsActive())
							continue;

						char aBuf[MAX_NAME_LENGTH + 4];
						str_format(aBuf, sizeof(aBuf), "\'%s\'", GameClient()->m_aClients[i].m_aName);
						if(str_find_nocase(aBuf, pMsg->m_pDescription) == 0)
						{
							pVoteCaller = &GameClient()->m_aClients[i];
							CallerId = i;
						}
					}
					if(pVoteCaller)
					{
						bool Friend = pVoteCaller->m_Friend;
						bool SameTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) == pVoteCaller->m_Team && pVoteCaller->m_Team != 0;
						bool MySelf = CallerId == GameClient()->m_Snap.m_LocalClientId;

						if(!Friend && !SameTeam && !MySelf)
						{
							GameClient()->m_Voting.Vote(AutoMapVote == 2 ? 1 : -1);
							if(str_comp(g_Config.m_TcAutoVoteWhenFarMessage, "") != 0)
								SendNonDuplicateMessage(0, g_Config.m_TcAutoVoteWhenFarMessage);
						}
					}
				}
			}
		}
	}

	auto &vServerCommands = GameClient()->m_Chat.m_vServerCommands;
	auto AddSpecId = [&](bool Enable) {
		static const CChat::CCommand SpecId("specid", "v[id]", "Spectate a player");
		vServerCommands.erase(std::remove_if(vServerCommands.begin(), vServerCommands.end(), [](const CChat::CCommand &Command) { return Command == SpecId; }), vServerCommands.end());
		if(Enable)
			vServerCommands.push_back(SpecId);
	};
	if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(!ServerCommandExists("specid"));
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(false);
		return;
	}
	if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(false);
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(ServerCommandExists("spec"));
		return;
	}
}

void CTClient::HandleSwapCountdownMessage(const char *pText, int Dummy)
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES || pText == nullptr)
		return;

	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Incoming;
	char aCounterpart[MAX_NAME_LENGTH] = "";
	if(!ParseSwapCountdownMessage(pText, Action, Direction, aCounterpart, sizeof(aCounterpart)))
		return;

	if(Action == ESwapCountdownMessageAction::Start)
		StartSwapCountdown(Dummy, aCounterpart, Direction == ESwapCountdownMessageDirection::Outgoing);
	else if(Action == ESwapCountdownMessageAction::Cancel)
		m_aSwapCountdownTrackers[Dummy].Cancel(aCounterpart, Direction == ESwapCountdownMessageDirection::Outgoing);
	else if(Action == ESwapCountdownMessageAction::Complete)
	{
		char aFirst[MAX_NAME_LENGTH];
		char aSecond[MAX_NAME_LENGTH];
		if(!ParseSwapCompletionMessage(pText, aFirst, sizeof(aFirst), aSecond, sizeof(aSecond)))
			return;

		const int LocalClientId = GameClient()->m_aLocalIds[Dummy];
		const char *pLocalName = LocalClientId >= 0 && LocalClientId < MAX_CLIENTS ? GameClient()->m_aClients[LocalClientId].m_aName :
											     (Dummy == 0 ? g_Config.m_PlayerName : g_Config.m_ClDummyName);
		if(str_comp_nocase(aFirst, pLocalName) == 0)
			m_aSwapCountdownTrackers[Dummy].Remove(aSecond);
		else if(str_comp_nocase(aSecond, pLocalName) == 0)
			m_aSwapCountdownTrackers[Dummy].Remove(aFirst);
	}
}

void CTClient::StartSwapCountdown(int Dummy, const char *pCounterpart, bool Outgoing)
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES)
		return;

	m_aSwapCountdownTrackers[Dummy].Start(pCounterpart, Outgoing, Client()->GameTick(Dummy));
}

void CTClient::ClearSwapCountdown(int Dummy)
{
	if(Dummy < 0)
	{
		for(int i = 0; i < NUM_DUMMIES; ++i)
			ClearSwapCountdown(i);
		return;
	}
	if(Dummy >= NUM_DUMMIES)
		return;

	m_aSwapCountdownTrackers[Dummy].Clear();
}

bool CTClient::HasSwapCountdown(int Dummy) const
{
	if(Dummy >= 0 && Dummy < NUM_DUMMIES)
		return !m_aSwapCountdownTrackers[Dummy].Entries().empty();

	for(int i = 0; i < NUM_DUMMIES; ++i)
	{
		if(!m_aSwapCountdownTrackers[i].Entries().empty())
			return true;
	}
	return false;
}

const std::vector<SSwapCountdownState> &CTClient::GetSwapCountdowns(int Dummy) const
{
	static const std::vector<SSwapCountdownState> s_Empty;
	if(Dummy >= 0 && Dummy < NUM_DUMMIES)
		return m_aSwapCountdownTrackers[Dummy].Entries();
	return s_Empty;
}

void CTClient::ConSpecId(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->SpecId(pResult->GetInteger(0));
}

bool CTClient::ChatDoSpecId(const char *pInput)
{
	const char *pNumber = str_startswith_nocase(pInput, "/specid ");
	if(!pNumber)
		return false;

	const int Length = str_length(pInput);
	CChat::CHistoryEntry *pEntry = GameClient()->m_Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pInput, Length + 1);

	int ClientId = 0;
	if(!str_toint(pNumber, &ClientId))
		return true;

	SpecId(ClientId);
	return true;
}

void CTClient::SpecId(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(ClientId);
		return;
	}

	if(ClientId < 0 || ClientId >= (int)std::size(GameClient()->m_aClients))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
		return;
	char aBuf[256];
	str_copy(aBuf, "/spec \"");
	char *pDst = aBuf + strlen(aBuf);
	str_escape(&pDst, Player.m_aName, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CTClient::ConEmoteCycle(IConsole::IResult *pResult, void *pUserData)
{
	CTClient &This = *(CTClient *)pUserData;
	This.m_EmoteCycle += 1;
	if(This.m_EmoteCycle > 15)
		This.m_EmoteCycle = 0;
	This.GameClient()->m_Emoticon.Emote(This.m_EmoteCycle);
}

void CTClient::AirRescue()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && (GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) == 0)
	{
		GameClient()->Echo(Localize("You are not in practice"));
		return;
	}

	auto IsIndexAirLike = [&](int Index) {
		const auto Tile = Collision()->GetTileIndex(Index);
		return Tile == TILE_AIR || Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	auto IsPosAirLike = [&](vec2 Pos) {
		const int Index = Collision()->GetPureMapIndex(Pos);
		return IsIndexAirLike(Index);
	};
	auto IsRadiusAirLike = [&](vec2 Pos, int Radius) {
		for(int y = -Radius; y <= Radius; ++y)
			for(int x = -Radius; x <= Radius; ++x)
				if(!IsPosAirLike(Pos + vec2(x, y) * 32.0f))
					return false;
		return true;
	};

	auto &AirRescuePositions = m_aAirRescuePositions[g_Config.m_ClDummy];
	while(!AirRescuePositions.empty())
	{
		// Get latest pos from positions
		const vec2 NewPos = AirRescuePositions.front();
		AirRescuePositions.pop_front();
		// Check for safety
		if(!IsRadiusAirLike(NewPos, 2))
			continue;
		// Do it
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/tpxy %f %f", NewPos.x / 32.0f, NewPos.y / 32.0f);
		GameClient()->m_Chat.SendChat(0, aBuf);
		return;
	}

	GameClient()->Echo(Localize("No safe position found"));
}

void CTClient::ConAirRescue(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->AirRescue();
}

void CTClient::ConCalc(IConsole::IResult *pResult, void *pUserData)
{
	int Error = 0;
	double Out = te_interp(pResult->GetString(0), &Error);
	if(Out == NAN || Error != 0)
		log_info("qmclient", "Calc error: %d", Error);
	else
		log_info("qmclient", "Calc result: %lf", Out);
}

void CTClient::OnConsoleInit()
{
	Console()->Register("calc", "r[expression]", CFGFLAG_CLIENT, ConCalc, this, "Evaluate an expression");
	Console()->Register("airrescue", "", CFGFLAG_CLIENT, ConAirRescue, this, "Rescue to a nearby air tile");

	Console()->Register("tc_random_player", "s[type]", CFGFLAG_CLIENT, ConRandomTee, this, "Randomize player color (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag) example: 0011 = randomize skin and flag [number is position]");
	Console()->Chain("tc_random_player", ConchainRandomColor, this);

	Console()->Register("spec_id", "v[id]", CFGFLAG_CLIENT, ConSpecId, this, "Spectate a player by Id");

	Console()->Register("emote_cycle", "", CFGFLAG_CLIENT, ConEmoteCycle, this, "Cycle through emotes");

	// 复读功能命令
	Console()->Register("+qm_repeat", "", CFGFLAG_CLIENT, ConRepeat, this, "Repeat the latest chat message");

	// 收藏地图命令
	Console()->Register("add_favorite_map", "s[map_name]", CFGFLAG_CLIENT, ConAddFavoriteMap, this, "Add a map to favorites");
	Console()->Register("remove_favorite_map", "s[map_name]", CFGFLAG_CLIENT, ConRemoveFavoriteMap, this, "Remove a map from favorites");
	Console()->Register("clear_favorite_maps", "", CFGFLAG_CLIENT, ConClearFavoriteMaps, this, "Clear all favorite maps");

	// 本地存档列表命令
	Console()->Register("savelist", "?s[map]", CFGFLAG_CLIENT, ConSaveList, this, "List local saves for current map (or specified map)");

	// 注册保存回调
	ConfigManager()->RegisterCallback(ConfigSaveFavoriteMaps, this);

	Console()->Chain(
		"tc_allow_any_resolution", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CTClient *)pUserData)->QueueAspectApply();
		},
		this);

	auto AspectConchain = [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
		pfnCallback(pResult, pCallbackUserData);
		((CTClient *)pUserData)->QueueAspectApply();
	};
	Console()->Chain("qm_aspect_preset", AspectConchain, this);
	Console()->Chain("qm_aspect_ratio", AspectConchain, this);

	Console()->Chain(
		"qm_gores", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CTClient *)pUserData)->ApplyGoresFastInputLink(false);
		},
		this);

	Console()->Chain(
		"tc_regex_chat_ignore", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			if(pResult->NumArguments() == 1)
			{
				auto Re = Regex(pResult->GetString(0));
				if(!Re.error().empty())
				{
					log_error("qmclient", "Invalid regex: %s", Re.error().c_str());
					return;
				}
				((CTClient *)pUserData)->m_RegexChatIgnore = std::move(Re);
			}
			pfnCallback(pResult, pCallbackUserData);
		},
		this);
}

void CTClient::RandomBodyColor()
{
	g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomFeetColor()
{
	g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomSkin(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->GameClient()->m_Skins.RandomizeSkin(0);
}

void CTClient::RandomFlag(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// get the flag count
	int FlagCount = pThis->GameClient()->m_CountryFlags.Num();

	// get a random flag number
	int FlagNumber = std::rand() % FlagCount;

	// get the flag name
	const CCountryFlags::CCountryFlag &Flag = pThis->GameClient()->m_CountryFlags.GetByIndex(FlagNumber);

	// set the flag code as number
	g_Config.m_PlayerCountry = Flag.m_CountryCode;
}

void CTClient::ResetFinishRenameState(int Dummy)
{
	const int First = Dummy < 0 ? 0 : Dummy;
	const int Last = Dummy < 0 ? NUM_DUMMIES : Dummy + 1;
	for(int i = First; i < Last; ++i)
	{
		m_aFinishRenamePending[i] = false;
		m_aFinishRenameAttempts[i] = 0;
		m_aFinishRenamePendingSince[i] = 0;
		m_aaFinishRenameTarget[i][0] = '\0';
	}
}

void CTClient::DoFinishCheck()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetFinishRenameState();
		return;
	}
	if(g_Config.m_TcChangeNameNearFinish <= 0)
	{
		m_FinishTextTimeout = 0.0f;
		ResetFinishRenameState();
		return;
	}
	m_FinishTextTimeout -= Client()->RenderFrameTime();
	if(m_FinishTextTimeout > 0.0f)
		return;
	m_FinishTextTimeout = 1.0f;
	static constexpr int FinishTileRadius = 10;
	static const std::array<float, FinishTileRadius * 2 + 1> s_aFinishArcHeights = []() {
		std::array<float, FinishTileRadius * 2 + 1> aHeights{};
		for(int i = 0; i <= FinishTileRadius * 2; ++i)
		{
			aHeights[i] = std::ceil(std::pow(std::sin((float)i * pi / 2.0f / (float)FinishTileRadius), 0.5f) * pi / 2.0f * (float)FinishTileRadius);
		}
		return aHeights;
	}();

	// Check for finish tile.
	const auto &NearFinishTile = [this](vec2 Pos, int Tile) -> bool {
		const CCollision *pCollision = GameClient()->Collision();
		if(!pCollision)
			return false;
		for(int i = 0; i <= FinishTileRadius * 2; ++i)
		{
			const float h = s_aFinishArcHeights[i];
			const vec2 Pos1 = vec2(Pos.x + (float)(i - FinishTileRadius) * 32.0f, Pos.y - h);
			const vec2 Pos2 = vec2(Pos.x + (float)(i - FinishTileRadius) * 32.0f, Pos.y + h);
			std::vector<int> vIndices = pCollision->GetMapIndices(Pos1, Pos2);
			if(vIndices.empty())
				vIndices.push_back(pCollision->GetPureMapIndex(Pos1));
			for(const int Index : vIndices)
			{
				if(Index < 0)
					continue;
				if(pCollision->GetTileIndex(Index) == Tile)
					return true;
				if(pCollision->GetFrontTileIndex(Index) == Tile)
					return true;
			}
		}
		return false;
	};
	const auto &SendUrgentRename = [this](int Conn, const char *pNewName) {
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = pNewName;
		Msg.m_pClan = Conn == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
		Msg.m_Country = Conn == 0 ? g_Config.m_PlayerCountry : g_Config.m_ClDummyCountry;
		Msg.m_pSkin = Conn == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = Conn == 0 ? g_Config.m_ClPlayerUseCustomColor : g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = Conn == 0 ? g_Config.m_ClPlayerColorBody : g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = Conn == 0 ? g_Config.m_ClPlayerColorFeet : g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(Conn, &Packer, MSGFLAG_VITAL);
		// 终点临时改名由本组件自己的 pending/超时状态管理，不能进入通用
		// SendInfo 重发路径，否则会被配置名字覆盖并重复弹通知。
		GameClient()->m_aCheckInfo[Conn] = -1;
	};

	const int Dummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = GameClient()->m_aLocalIds[Dummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
	{
		ResetFinishRenameState(Dummy);
		return;
	}
	const auto &Player = GameClient()->m_aClients[LocalId];
	if(!Player.m_Active)
	{
		ResetFinishRenameState(Dummy);
		return;
	}
	const char *pNewName = g_Config.m_TcFinishName;
	if(!pNewName || pNewName[0] == '\0')
	{
		ResetFinishRenameState(Dummy);
		return;
	}
	if(str_comp(m_aaFinishRenameTarget[Dummy], pNewName) != 0)
	{
		str_copy(m_aaFinishRenameTarget[Dummy], pNewName, sizeof(m_aaFinishRenameTarget[Dummy]));
		m_aFinishRenamePending[Dummy] = false;
		m_aFinishRenameAttempts[Dummy] = 0;
	}
	if(str_comp(Player.m_aName, pNewName) == 0)
	{
		m_aFinishRenamePending[Dummy] = false;
		m_aFinishRenameAttempts[Dummy] = 0;
		return;
	}
	if(!NearFinishTile(Player.m_RenderPos, TILE_FINISH))
	{
		m_aFinishRenamePending[Dummy] = false;
		m_aFinishRenameAttempts[Dummy] = 0;
		return;
	}
	if(m_aFinishRenamePending[Dummy])
	{
		if(time_get() - m_aFinishRenamePendingSince[Dummy] < 8 * time_freq())
			return;
		m_aFinishRenamePending[Dummy] = false;
	}
	if(m_aFinishRenameAttempts[Dummy] >= 3)
		return;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), Localize("Changing name to %s near finish"), pNewName);
	GameClient()->Echo(aBuf);
	SendUrgentRename(Dummy, pNewName);
	m_aFinishRenamePending[Dummy] = true;
	m_aFinishRenamePendingSince[Dummy] = time_get();
	m_aFinishRenameAttempts[Dummy]++;
}

bool CTClient::ServerCommandExists(const char *pCommand)
{
	for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
		if(str_comp_nocase(pCommand, Command.m_aName) == 0)
			return true;
	return false;
}

void CTClient::OnUpdate()
{
	UpdateLocalSaveRestore();
#if defined(CONF_FAMILY_WINDOWS)
	const bool AutoUpdateEnabled = g_Config.m_QmAutoUpdate != 0;
	if(AutoUpdateEnabled != m_UpdateAutoEnabled && !m_UpdateShutdownRequested)
	{
		m_UpdateAutoEnabled = AutoUpdateEnabled;
		if(AutoUpdateEnabled)
			m_UpdateNextCheck = 0;
		else
		{
			ResetUpdateTasks();
			RemoveUpdateTempFiles();
			m_UpdateReady = false;
			m_UpdateCheckFailed = false;
			m_UpdateFailureExitAt = 0;
			m_FetchedQmClientUpdateInfo = false;
		}
	}
#endif
	StartUpdateCheckIfDue();

	if(m_QmAspectApplyPending)
	{
		m_QmAspectApplyPending = false;
		SetForcedAspect();
	}

	if(m_pQmClientUpdateInfoTask)
	{
		if(m_pQmClientUpdateInfoTask->Done())
		{
			const bool InfoOk = m_pQmClientUpdateInfoTask->State() == EHttpState::DONE;
			if(InfoOk)
				FinishQmClientUpdateInfo();
			else
			{
				m_UpdateCheckFailed = true;
				m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
				if(m_UpdateShutdownRequested)
					m_UpdateFailureExitAt = time_get() + 2 * time_freq();
			}
			ResetQmClientUpdateInfoTask();

			if(m_QmClientAutoUpdateAfterCheck)
			{
				if(!InfoOk || !m_FetchedQmClientUpdateInfo || m_UpdateCheckFailed)
				{
					Client()->AddWarning(SWarning(Localize("Update"), Localize("Failed to check for updates")));
				}
				else if(!NeedQmClientUpdate())
				{
					Client()->AddWarning(SWarning(Localize("Update notice"), Localize("You are already on the latest version")));
				}
				else
				{
					Client()->AddWarning(SWarning(Localize("Update notice"), Localize("Downloading update...")));
				}
				m_QmClientAutoUpdateAfterCheck = false;
			}
		}
	}
	if(m_pUpdatePackageTask && m_pUpdatePackageSignatureTask && m_pUpdateManifestTask && m_pUpdateManifestSignatureTask &&
		m_pUpdatePackageTask->Done() && m_pUpdatePackageSignatureTask->Done() && m_pUpdateManifestTask->Done() && m_pUpdateManifestSignatureTask->Done() &&
		!m_UpdateReady && !m_UpdateCheckFailed)
	{
		FinishUpdateDownloads();
	}
	if(m_UpdateShutdownRequested && !IsUpdateChecking() && !IsUpdateDownloading() && !m_UpdateReady && !m_UpdateCheckFailed)
	{
		m_UpdateShutdownRequested = false;
		Client()->Quit();
	}

	if(m_UpdateShutdownRequested && (m_UpdateReady || m_UpdateCheckFailed) &&
		(!m_UpdateCheckFailed || m_UpdateFailureExitAt == 0 || time_get() >= m_UpdateFailureExitAt))
	{
		if(m_UpdateReady && !m_UpdateInstallerStarted)
		{
			if(!LaunchUpdateInstaller())
			{
				m_UpdateReady = false;
				m_UpdateCheckFailed = true;
				m_UpdateFailureExitAt = time_get() + 2 * time_freq();
				RemoveUpdateTempFiles();
				Client()->AddWarning(SWarning(Localize("Update"), Localize("Update failed. Please try again")));
			}
		}
		if((!m_UpdateReady || m_UpdateInstallerStarted || m_UpdateCheckFailed) &&
			(!m_UpdateCheckFailed || m_UpdateFailureExitAt == 0 || time_get() >= m_UpdateFailureExitAt))
		{
			m_UpdateShutdownRequested = false;
			Client()->Quit();
		}
	}

	DoFinishCheck();
	CheckFriendOnline();
	CheckFriendEnterGreet();

	bool RunGameplayTickChecks = false;
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			if(Dummy == 1 && !Client()->DummyConnected())
				continue;

			const int Tick = Client()->GameTick(Dummy);
			if(m_aLastGameplayLogicTick[Dummy] != Tick)
			{
				m_aLastGameplayLogicTick[Dummy] = Tick;
				RunGameplayTickChecks = true;
			}
		}
	}
	else
	{
		m_aLastGameplayLogicTick[0] = -1;
		m_aLastGameplayLogicTick[1] = -1;
	}

	if(RunGameplayTickChecks)
	{
		CheckFreeze();
		CheckComboPopup();
		CheckWaterFall();
		UpdatePlayerStats(); // 更新玩家统计
		UpdateGoresWeaponCycle(); // Gores 锤枪自动切换
		UpdateGoresMapProgress(); // 更新 Gores 地图路径进度
	}

	UpdateMapHistorySession();
	MaybeSaveMapCategoryCache();
	MaybeSaveMapNotes();
	ApplyGoresFastInputLink();
	ApplyFocusModeEffects();
}

void CTClient::OnRender()
{
	RenderGoresDebugRoute();
}

void CTClient::CheckFreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_TcFreezeChatEnabled)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active)
			continue;

		// Check if player is currently frozen
		bool IsInFreeze = Client.m_FreezeEnd != 0;

		// Detect entering freeze (transition from not frozen to frozen)
		if(IsInFreeze && !m_aWasInFreeze[Dummy])
		{
			int64_t Now = time_get();
			int64_t FreqMs = time_freq() / 1000;

			// Send emoticon (with 3 second cooldown)
			if(g_Config.m_TcFreezeChatEmoticon && Now - m_aLastFreezeEmoteTime[Dummy] > 3000 * FreqMs)
			{
				GameClient()->m_Emoticon.Emote(g_Config.m_TcFreezeChatEmoticonId);
				m_aLastFreezeEmoteTime[Dummy] = Now;
			}

			// Send chat message (with 5 second cooldown and probability check)
			if(g_Config.m_TcFreezeChatMessage[0] != '\0' && Now - m_aLastFreezeMessageTime[Dummy] > 5000 * FreqMs)
			{
				// Check probability (0-100%)
				int Chance = g_Config.m_TcFreezeChatChance;
				if(Chance > 0 && (Chance >= 100 || (std::rand() % 100) < Chance))
				{
					// Parse comma-separated messages and pick one randomly
					char aMessages[128];
					str_copy(aMessages, g_Config.m_TcFreezeChatMessage);

					// Count messages and store pointers
					std::vector<const char *> vMessages;
					char *pToken = strtok(aMessages, ",");
					while(pToken != nullptr)
					{
						// Skip leading spaces
						while(*pToken == ' ')
							pToken++;
						if(*pToken != '\0')
							vMessages.push_back(pToken);
						pToken = strtok(nullptr, ",");
					}

					// Pick a random message and send
					if(!vMessages.empty())
					{
						const char *pSelectedMessage = vMessages[std::rand() % vMessages.size()];
						GameClient()->m_Chat.SendChat(0, pSelectedMessage);
						m_aLastFreezeMessageTime[Dummy] = Now;
					}
				}
			}
		}

		m_aWasInFreeze[Dummy] = IsInFreeze;
	}
}

bool CTClient::EnsureTextPopupCache(int TextType)
{
	if(TextType < 0 || TextType >= (int)std::size(s_aTextPopupDefinitions))
		return false;

	const bool FontChanged = str_comp(m_aTextPopupFont, g_Config.m_TcCustomFont) != 0;
	if(FontChanged)
		UnloadTextPopupCaches();

	auto &PopupCache = m_aTextPopupCaches[TextType];
	if(PopupCache.m_TextContainerIndex.Valid())
		return true;

	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

	const auto &Definition = s_aTextPopupDefinitions[TextType];
	const char *pPopupText = Localize(Definition.m_pText);
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(0.0f, 0.0f));
	Cursor.m_FontSize = QMCLIENT_TEXT_POPUP_FONT_SIZE;
	if(!TextRender()->CreateTextContainer(PopupCache.m_TextContainerIndex, &Cursor, pPopupText))
		return false;

	if(PopupCache.m_TextContainerIndex.Valid())
	{
		const STextBoundingBox BoundingBox = TextRender()->GetBoundingBoxTextContainer(PopupCache.m_TextContainerIndex);
		PopupCache.m_TextSize = vec2(BoundingBox.m_W, BoundingBox.m_H);
		str_copy(m_aTextPopupFont, g_Config.m_TcCustomFont, sizeof(m_aTextPopupFont));
	}
	return PopupCache.m_TextContainerIndex.Valid();
}

void CTClient::UnloadTextPopupCaches()
{
	for(auto &PopupCache : m_aTextPopupCaches)
	{
		TextRender()->DeleteTextContainer(PopupCache.m_TextContainerIndex);
		PopupCache.m_TextSize = vec2(0.0f, 0.0f);
	}
	m_aTextPopupFont[0] = '\0';
}

void CTClient::ClearFreezeWakeupPopups()
{
	for(auto &Popup : m_aFreezeWakeupPopups)
		Popup = {};
}

bool CTClient::AddTextPopup(int AnchorClientId, int TextType, bool UseRollingColor, ColorRGBA Color)
{
	if(AnchorClientId < 0 || AnchorClientId >= MAX_CLIENTS)
		return false;

	if(!GameClient()->m_aClients[AnchorClientId].m_Active || !GameClient()->m_Snap.m_aCharacters[AnchorClientId].m_Active)
		return false;

	if(!EnsureTextPopupCache(TextType))
		return false;

	const float Now = LocalTime();
	int PopupIndex = -1;
	float OldestStartTime = Now;
	for(int i = 0; i < FREEZE_WAKEUP_POPUP_MAX; ++i)
	{
		const auto &Popup = m_aFreezeWakeupPopups[i];
		if(!Popup.m_Active || Now - Popup.m_StartTime >= TextPopupDuration(Popup.m_TextType))
		{
			PopupIndex = i;
			break;
		}

		if(PopupIndex < 0 || Popup.m_StartTime < OldestStartTime)
		{
			PopupIndex = i;
			OldestStartTime = Popup.m_StartTime;
		}
	}

	if(PopupIndex < 0)
		return false;

	auto &Popup = m_aFreezeWakeupPopups[PopupIndex];
	Popup = {};
	Popup.m_Active = true;
	Popup.m_AnchorClientId = AnchorClientId;
	Popup.m_TextType = TextType;
	Popup.m_StartTime = Now;
	Popup.m_HorizontalSign = std::rand() % 2 == 0 ? -1.0f : 1.0f;
	Popup.m_ColorPhase = ((float)std::rand() / (float)RAND_MAX) * 2.0f * pi;
	Popup.m_UseRollingColor = UseRollingColor;
	Popup.m_Color = Color;
	return true;
}

void CTClient::AddFreezeWakeupPopup(int WokenDummy)
{
	const int AnchorDummy = WokenDummy ^ 1;
	const int AnchorClientId = GameClient()->m_aLocalIds[AnchorDummy];
	AddTextPopup(AnchorClientId, (int)ETextPopupType::FREEZE_WAKEUP, true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CTClient::CheckHammerWakeupActions()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!g_Config.m_QmFreezeWakeupPopup)
	{
		for(auto &Popup : m_aFreezeWakeupPopups)
		{
			if(Popup.m_Active && Popup.m_TextType == (int)ETextPopupType::FREEZE_WAKEUP)
				Popup.m_Active = false;
		}
	}

	SQmHammerWakeupDecisionInput Input;
	Input.m_ActiveConnection = g_Config.m_ClDummy;
	Input.m_ActiveSpectating = GameClient()->m_Snap.m_SpecInfo.m_Active;
	Input.m_ChatActive = GameClient()->m_Chat.IsActive();
	Input.m_ShowPopup = g_Config.m_QmFreezeWakeupPopup != 0;
	Input.m_AutoUnspec = g_Config.m_QmAutoUnspecOnUnfreeze != 0;
	Input.m_AutoSwitch = g_Config.m_QmAutoSwitchOnUnfreeze != 0 && Client()->DummyConnected();
	Input.m_AutoCloseChat = g_Config.m_QmAutoCloseChatOnUnfreeze != 0;
	const int SnapshotTick = Client()->GameTick(Input.m_ActiveConnection);

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Character.m_HasExtendedData || Character.m_pPrevExtendedData == nullptr)
			continue;
		Input.m_aWasInFreeze[Dummy] = Character.m_pPrevExtendedData->m_FreezeEnd != 0;
		Input.m_aInFreeze[Dummy] = Character.m_ExtendedData.m_FreezeEnd != 0;
		Input.m_aExternalHammerWakeup[Dummy] = Input.m_aWasInFreeze[Dummy] && !Input.m_aInFreeze[Dummy] &&
						       DetectFreezeWakeupType(GameClient(), ClientId, SnapshotTick) == EFreezeWakeupType::EXTERNAL_HAMMER;
	}

	const SQmHammerWakeupDecision Decision = QmDecideHammerWakeupActions(Input);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Decision.m_aShowPopup[Dummy])
			AddFreezeWakeupPopup(Dummy);
	}

	if(Decision.m_UnspecActiveConnection)
	{
		if(Client()->IsSixup())
		{
			protocol7::CNetMsg_Cl_Say Msg;
			Msg.m_Mode = protocol7::CHAT_ALL;
			Msg.m_Target = -1;
			Msg.m_pMessage = "/spec";
			Client()->SendPackMsg(Input.m_ActiveConnection, &Msg, MSGFLAG_VITAL, true);
		}
		else
		{
			CNetMsg_Cl_Say Msg;
			Msg.m_Team = 0;
			Msg.m_pMessage = "/spec";
			Client()->SendPackMsg(Input.m_ActiveConnection, &Msg, MSGFLAG_VITAL);
		}
	}

	if(Decision.m_CloseChat)
	{
		GameClient()->m_Chat.SaveDraft();
		GameClient()->m_Chat.DisableMode();
	}

	if(Decision.m_SwitchToConnection >= 0)
	{
		char aCommand[16];
		str_format(aCommand, sizeof(aCommand), "cl_dummy %d", Decision.m_SwitchToConnection);
		Console()->ExecuteLine(aCommand);
	}
}

void CTClient::ResetComboState(int Dummy)
{
	auto ResetOne = [&](int Index) {
		m_aComboPopupCount[Index] = 0;
		m_aComboLastEventTick[Index] = -1;
		std::fill(std::begin(m_aaComboLastHammerHitSnapshotTick[Index]), std::end(m_aaComboLastHammerHitSnapshotTick[Index]), -1);
		m_aComboLastHookedPlayer[Index] = -1;
	};

	if(Dummy >= 0 && Dummy < NUM_DUMMIES)
	{
		ResetOne(Dummy);
		return;
	}

	for(int Index = 0; Index < NUM_DUMMIES; ++Index)
		ResetOne(Index);
}

void CTClient::CheckComboPopup()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!g_Config.m_QmComboPopup)
	{
		ResetComboState();
		return;
	}

	const int ComboWindowTicks = maximum(1, Client()->GameTickSpeed() * QMCLIENT_COMBO_POPUP_WINDOW_SECONDS);
	auto RegisterComboEvent = [&](int Dummy, vec2 AnchorPos, int TargetPlayer, const CCharacter *pTargetChar) {
		if(TargetPlayer < 0 || TargetPlayer >= MAX_CLIENTS)
			return;

		const int CurrentTick = Client()->GameTick(Dummy);
		if(m_aComboLastEventTick[Dummy] >= 0 &&
			CurrentTick >= m_aComboLastEventTick[Dummy] &&
			CurrentTick - m_aComboLastEventTick[Dummy] <= ComboWindowTicks)
		{
			++m_aComboPopupCount[Dummy];
		}
		else
		{
			m_aComboPopupCount[Dummy] = 1;
		}
		m_aComboLastEventTick[Dummy] = CurrentTick;

		const int ComboCount = m_aComboPopupCount[Dummy];
		if(ComboCount >= 11)
		{
			AddComboFreezeParticleRing(GameClient(), AnchorPos, 1.0f);
			if(pTargetChar != nullptr)
				AddComboFreezeParticleRing(GameClient(), pTargetChar->GetPos(), 1.0f);
		}
		else if(ComboCount >= 7)
		{
			AddComboFreezeParticleRing(GameClient(), AnchorPos, 0.7f);
		}
		else if(ComboCount >= 3)
		{
			if(pTargetChar != nullptr)
				AddComboFreezeParticleRing(GameClient(), pTargetChar->GetPos(), 0.4f);
		}
	};

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
		{
			ResetComboState(Dummy);
			continue;
		}

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS ||
			!GameClient()->m_aClients[ClientId].m_Active ||
			!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			ResetComboState(Dummy);
			continue;
		}

		const CCharacter *pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
		const vec2 AnchorPos = pLocalChar != nullptr ? pLocalChar->GetPos() : GameClient()->m_aClients[ClientId].m_RenderPos;
		const int HookedPlayer = pLocalChar != nullptr ? pLocalChar->Core()->HookedPlayer() : -1;
		if(HookedPlayer >= 0 && HookedPlayer != ClientId && HookedPlayer != m_aComboLastHookedPlayer[Dummy])
			RegisterComboEvent(Dummy, AnchorPos, HookedPlayer, GameClient()->m_PredictedWorld.GetCharacterById(HookedPlayer));
		m_aComboLastHookedPlayer[Dummy] = HookedPlayer >= 0 ? HookedPlayer : -1;

		SQmHammerHitRecord aHits[MAX_CLIENTS];
		const int MinSnapshotTick = Client()->GameTick(g_Config.m_ClDummy) - 1;
		const int NumHits = GameClient()->HammerHitTracker().FindLatestTargets(
			ClientId,
			MinSnapshotTick,
			aHits,
			std::size(aHits),
			GameClient()->HammerHitConnectionFilter());
		for(int HitIndex = 0; HitIndex < NumHits; ++HitIndex)
		{
			const SQmHammerHitRecord &Hit = aHits[HitIndex];
			const int TargetId = Hit.m_TargetId;
			if(TargetId == ClientId)
				continue;
			if(Hit.m_SnapshotTick <= m_aaComboLastHammerHitSnapshotTick[Dummy][TargetId])
				continue;
			m_aaComboLastHammerHitSnapshotTick[Dummy][TargetId] = Hit.m_SnapshotTick;
			RegisterComboEvent(Dummy, AnchorPos, TargetId, GameClient()->m_PredictedWorld.GetCharacterById(TargetId));
		}
	}
}

bool CTClient::HasFreezeWakeupPopups() const
{
	const float Now = LocalTime();
	for(const auto &Popup : m_aFreezeWakeupPopups)
	{
		if(Popup.m_Active && Now - Popup.m_StartTime < TextPopupDuration(Popup.m_TextType))
			return true;
	}
	return false;
}

void CTClient::RenderFreezeWakeupPopups()
{
	if(!HasFreezeWakeupPopups())
		return;

	const float Now = LocalTime();
	for(auto &Popup : m_aFreezeWakeupPopups)
	{
		if(!Popup.m_Active)
			continue;

		const float Elapsed = Now - Popup.m_StartTime;
		const float PopupDuration = TextPopupDuration(Popup.m_TextType);
		if(Elapsed >= PopupDuration)
		{
			Popup.m_Active = false;
			continue;
		}

		if(Popup.m_AnchorClientId < 0 || Popup.m_AnchorClientId >= MAX_CLIENTS ||
			!GameClient()->m_aClients[Popup.m_AnchorClientId].m_Active ||
			!GameClient()->m_Snap.m_aCharacters[Popup.m_AnchorClientId].m_Active)
		{
			Popup.m_Active = false;
			continue;
		}

		if(!EnsureTextPopupCache(Popup.m_TextType))
		{
			Popup.m_Active = false;
			continue;
		}

		const auto &PopupCache = m_aTextPopupCaches[Popup.m_TextType];
		const float Progress = std::clamp(Elapsed / PopupDuration, 0.0f, 1.0f);
		const float PopIn = std::clamp(Elapsed / 0.12f, 0.0f, 1.0f);
		const float FadeOut = 1.0f - std::clamp((Progress - 0.7f) / 0.3f, 0.0f, 1.0f);
		const float Rise = (1.0f - PopIn) * 6.0f;
		const vec2 AnchorPos = GameClient()->m_aClients[Popup.m_AnchorClientId].m_RenderPos +
				       vec2(QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET.x * Popup.m_HorizontalSign, QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET.y) +
				       vec2(QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT.x * Popup.m_HorizontalSign, QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT.y) * Progress;
		const vec2 Pos =
			Popup.m_HorizontalSign < 0.0f ?
				vec2(AnchorPos.x - PopupCache.m_TextSize.x, AnchorPos.y - Rise) :
				vec2(AnchorPos.x, AnchorPos.y - Rise);

		ColorRGBA PopupColor = Popup.m_Color;
		if(Popup.m_UseRollingColor)
		{
			const float RollAmount = 0.5f + 0.5f * std::sin(Elapsed * 6.0f + Popup.m_ColorPhase);
			PopupColor = ColorRGBA(
				QMCLIENT_POPUP_ROLL_COLOR_FROM.r + (QMCLIENT_POPUP_ROLL_COLOR_TO.r - QMCLIENT_POPUP_ROLL_COLOR_FROM.r) * RollAmount,
				QMCLIENT_POPUP_ROLL_COLOR_FROM.g + (QMCLIENT_POPUP_ROLL_COLOR_TO.g - QMCLIENT_POPUP_ROLL_COLOR_FROM.g) * RollAmount,
				QMCLIENT_POPUP_ROLL_COLOR_FROM.b + (QMCLIENT_POPUP_ROLL_COLOR_TO.b - QMCLIENT_POPUP_ROLL_COLOR_FROM.b) * RollAmount,
				1.0f);
		}

		ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
		OutlineColor.a *= FadeOut;
		TextRender()->RenderTextContainer(PopupCache.m_TextContainerIndex, ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f * FadeOut), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), Pos.x + 2.0f, Pos.y + 2.0f);
		TextRender()->RenderTextContainer(PopupCache.m_TextContainerIndex, PopupColor.WithAlpha(FadeOut), OutlineColor, Pos.x, Pos.y);
	}
}

void CTClient::CheckWaterFall()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_TcFreezeChatEnabled)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active)
			continue;

		// Check if player is in death tile
		vec2 Pos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
		int Index = Collision()->GetPureMapIndex(Pos);
		int TileIndex = Collision()->GetTileIndex(Index);
		bool IsInDeath = TileIndex == TILE_DEATH;

		// Detect entering death (transition from not in death to in death)
		if(IsInDeath && !m_aWasInDeath[Dummy])
		{
			int64_t Now = time_get();
			int64_t FreqMs = time_freq() / 1000;

			// Send emoticon (with 3 second cooldown)
			if(g_Config.m_TcFreezeChatEmoticon && Now - m_aLastWaterHeartTime[Dummy] > 3000 * FreqMs)
			{
				GameClient()->m_Emoticon.Emote(g_Config.m_TcFreezeChatEmoticonId);
				m_aLastWaterHeartTime[Dummy] = Now;
			}

			// Send chat message (with 5 second cooldown and probability check)
			if(g_Config.m_TcFreezeChatMessage[0] != '\0' && Now - m_aLastWaterMessageTime[Dummy] > 5000 * FreqMs)
			{
				int Chance = g_Config.m_TcFreezeChatChance;
				if(Chance > 0 && (Chance >= 100 || (std::rand() % 100) < Chance))
				{
					char aMessages[128];
					str_copy(aMessages, g_Config.m_TcFreezeChatMessage);

					std::vector<const char *> vMessages;
					char *pToken = strtok(aMessages, ",");
					while(pToken != nullptr)
					{
						while(*pToken == ' ')
							pToken++;
						if(*pToken != '\0')
							vMessages.push_back(pToken);
						pToken = strtok(nullptr, ",");
					}

					if(!vMessages.empty())
					{
						const char *pSelectedMessage = vMessages[std::rand() % vMessages.size()];
						GameClient()->m_Chat.SendChat(0, pSelectedMessage);
						m_aLastWaterMessageTime[Dummy] = Now;
					}
				}
			}
		}

		m_aWasInDeath[Dummy] = IsInDeath;
	}
}

static void BuildFriendNotifyKey(const char *pName, const char *pClan, bool IgnoreClan, std::string &OutKey)
{
	OutKey.clear();
	if(pName)
		OutKey.append(pName);
	if(!IgnoreClan)
	{
		OutKey.push_back('\t');
		if(pClan)
			OutKey.append(pClan);
	}
}

void CTClient::CheckFriendOnline()
{
	const int Enabled = g_Config.m_QmFriendOnlineNotify;
	const int IgnoreClanSetting = g_Config.m_ClFriendsIgnoreClan;
	const uint64_t FriendRevision = GameClient()->Friends()->Revision();
	if(m_FriendNotifyPrevEnabled != Enabled || m_FriendNotifyPrevIgnoreClan != IgnoreClanSetting || m_FriendNotifyPrevRevision != FriendRevision)
	{
		m_FriendNotifyPrevEnabled = Enabled;
		m_FriendNotifyPrevIgnoreClan = IgnoreClanSetting;
		m_FriendNotifyPrevRevision = FriendRevision;
		m_FriendOnlineTracker.Reset();
		m_FriendOnlineRefreshPending = false;
		m_FriendAutoRefreshNext = 0.0f;
	}

	if(!Enabled || GameClient()->Friends()->NumFriends() <= 0)
		return;

	IServerBrowser *pServerBrowser = ServerBrowser();
	if(!pServerBrowser)
		return;

	const float Now = LocalTime();
	if(g_Config.m_QmFriendOnlineAutoRefresh != m_FriendAutoRefreshPrevEnabled ||
		g_Config.m_QmFriendOnlineRefreshSeconds != m_FriendAutoRefreshPrevSeconds)
	{
		m_FriendAutoRefreshPrevEnabled = g_Config.m_QmFriendOnlineAutoRefresh;
		m_FriendAutoRefreshPrevSeconds = g_Config.m_QmFriendOnlineRefreshSeconds;
		m_FriendAutoRefreshNext = 0.0f;
	}

	if(m_FriendOnlineRefreshPending && !pServerBrowser->IsGettingServerlist())
	{
		m_FriendOnlineRefreshPending = false;
		if(!pServerBrowser->IsServerlistError())
		{
			// 一次刷新只提交一次观测；同帧收集，避免分帧索引混入下一代名单。
			const bool IgnoreClan = IgnoreClanSetting != 0;
			std::unordered_set<std::string> FriendKeys;
			std::unordered_set<std::string> FriendNames;
			std::string Key;
			for(int Index = 0; Index < GameClient()->Friends()->NumFriends(); ++Index)
			{
				const CFriendInfo *pFriend = GameClient()->Friends()->GetFriend(Index);
				if(pFriend->m_aName[0] == '\0')
					continue;
				BuildFriendNotifyKey(pFriend->m_aName, pFriend->m_aClan, IgnoreClan, Key);
				FriendKeys.insert(Key);
				FriendNames.insert(pFriend->m_aName);
			}

			std::vector<qm_friend_notify::CFriend> vCurrentFriends;
			std::unordered_set<std::string> AvailableServers;
			for(int Index = 0; Index < pServerBrowser->NumHttpServers(); ++Index)
			{
				const CServerInfo *pEntry = pServerBrowser->HttpGet(Index);
				if(!pEntry || pEntry->m_NumAddresses <= 0)
					continue;
				char aAddress[NETADDR_MAXSTRSIZE];
				net_addr_str(&pEntry->m_aAddresses[0], aAddress, sizeof(aAddress), true);
				if(pEntry->m_NumReceivedClients == pEntry->m_NumClients)
					AvailableServers.insert(aAddress);
				for(int ClientIndex = 0; ClientIndex < pEntry->m_NumReceivedClients; ++ClientIndex)
				{
					const auto &Client = pEntry->m_aClients[ClientIndex];
					if(Client.m_aName[0] == '\0' || FriendNames.find(Client.m_aName) == FriendNames.end())
						continue;
					BuildFriendNotifyKey(Client.m_aName, Client.m_aClan, IgnoreClan, Key);
					// 同名玩家改战队时继续跟踪在服状态，避免改回后被误判为上线。
					vCurrentFriends.push_back({Key, Client.m_aName, pEntry->m_aMap, aAddress, FriendKeys.find(Key) != FriendKeys.end()});
				}
			}

			for(const auto &Friend : m_FriendOnlineTracker.Update(vCurrentFriends, AvailableServers))
			{
				char aBuf[256];
				const char *pMap = Friend.m_Map.empty() ? Localize("Unknown") : Friend.m_Map.c_str();
				str_format(aBuf, sizeof(aBuf), Localize("Your friend %s is online and currently on map %s!"), Friend.m_Name.c_str(), pMap);
				GameClient()->m_Chat.Echo(aBuf);
			}
		}
	}

	if(Now >= m_FriendAutoRefreshNext && !pServerBrowser->IsGettingServerlist())
	{
		const int CurrentType = pServerBrowser->GetCurrentType();
		if(g_Config.m_QmFriendOnlineAutoRefresh && CurrentType != IServerBrowser::TYPE_LAN)
			pServerBrowser->Refresh(CurrentType, false);
		else
			pServerBrowser->RefreshHttpServerList();
		m_FriendOnlineRefreshPending = true;
		m_FriendAutoRefreshNext = Now + maximum(5.0f, (float)g_Config.m_QmFriendOnlineRefreshSeconds);
	}
}

void CTClient::ResetFriendEnter()
{
	m_FriendEnterTracker.Reset();
	m_FriendEnterPendingNames.clear();
	m_FriendEnterPendingSendAt = 0.0f;
	m_FriendEnterNextCheck = 0.0f;
}

void CTClient::CheckFriendEnterGreet()
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetFriendEnter();
		return;
	}

	const bool AutoGreetEnabled = g_Config.m_QmFriendEnterAutoGreet != 0;
	const bool BroadcastEnabled = g_Config.m_QmFriendEnterBroadcast != 0;
	const int IgnoreClanSetting = g_Config.m_ClFriendsIgnoreClan;
	const int EnabledMask = (AutoGreetEnabled ? 1 : 0) | (BroadcastEnabled ? 2 : 0);
	const uint64_t FriendRevision = GameClient()->Friends()->Revision();
	if(m_FriendEnterPrevEnabled != EnabledMask || m_FriendEnterPrevIgnoreClan != IgnoreClanSetting ||
		m_FriendEnterPrevDummy != g_Config.m_ClDummy || m_FriendEnterPrevRevision != FriendRevision)
	{
		m_FriendEnterPrevEnabled = EnabledMask;
		m_FriendEnterPrevIgnoreClan = IgnoreClanSetting;
		m_FriendEnterPrevDummy = g_Config.m_ClDummy;
		m_FriendEnterPrevRevision = FriendRevision;
		ResetFriendEnter();
	}

	if((!AutoGreetEnabled && !BroadcastEnabled) || GameClient()->Friends()->NumFriends() <= 0)
	{
		ResetFriendEnter();
		return;
	}
	if(!AutoGreetEnabled)
	{
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
	}

	static constexpr float FriendEnterGreetDelaySeconds = 3.0f;
	const float Now = LocalTime();
	if(AutoGreetEnabled && !m_FriendEnterPendingNames.empty() && Now >= m_FriendEnterPendingSendAt)
	{
		// 空配置表示关闭问候；非空则按当前语言 Localize 后再发送
		if(g_Config.m_QmFriendEnterGreetText[0] != '\0')
		{
			char aMsg[256];
			aMsg[0] = '\0';
			str_append(aMsg, m_FriendEnterPendingNames.c_str(), sizeof(aMsg));
			if(aMsg[0] != '\0')
				str_append(aMsg, ": ", sizeof(aMsg));
			str_append(aMsg, ResolveFriendEnterLocalizeText(g_Config.m_QmFriendEnterGreetText, s_pFriendEnterGreetDefaultText), sizeof(aMsg));

			if(aMsg[0] != '\0')
				GameClient()->m_Chat.SendChat(0, aMsg);
		}
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
	}

	if(Now < m_FriendEnterNextCheck)
		return;
	m_FriendEnterNextCheck = Now + 0.2f;

	std::vector<qm_friend_notify::CEnterTracker::CClient> vCurrentClients;
	vCurrentClients.reserve(MAX_CLIENTS);
	const int LocalMain = GameClient()->m_aLocalIds[0];
	const int LocalDummy = GameClient()->m_aLocalIds[1];
	const bool HasDummy = Client()->DummyConnected();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const auto &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active)
			continue;
		vCurrentClients.push_back({ClientId, Client.m_aName, Client.m_aClan,
			GameClient()->Friends()->IsFriend(Client.m_aName, Client.m_aClan, true),
			ClientId == LocalMain || (HasDummy && ClientId == LocalDummy)});
	}
	const auto NewFriends = m_FriendEnterTracker.Update(vCurrentClients, Now, IgnoreClanSetting != 0);

	if(NewFriends.empty())
		return;

	std::string NewNames;
	NewNames.reserve(64);
	for(size_t i = 0; i < NewFriends.size(); ++i)
	{
		if(i > 0)
			NewNames.push_back(' ');
		NewNames.append(NewFriends[i]);
	}

	if(NewNames.empty())
		return;

	if(BroadcastEnabled)
	{
		// 配置默认是英文 source；Localize 后替换 %s 好友名
		const std::string BroadcastText = BuildFriendEnterBroadcastText(
			ResolveFriendEnterLocalizeText(g_Config.m_QmFriendEnterBroadcastText, s_pFriendEnterBroadcastDefaultText),
			NewNames);
		if(!BroadcastText.empty())
			GameClient()->m_Broadcast.DoBroadcast(BroadcastText.c_str());
	}

	if(!AutoGreetEnabled || g_Config.m_QmFriendEnterGreetText[0] == '\0')
		return;

	if(!m_FriendEnterPendingNames.empty())
		m_FriendEnterPendingNames.push_back(' ');
	m_FriendEnterPendingNames.append(NewNames);
	if(m_FriendEnterPendingSendAt <= 0.0f)
		m_FriendEnterPendingSendAt = Now + FriendEnterGreetDelaySeconds;
}

bool CTClient::NeedQmClientUpdate()
{
	return str_comp(m_aQmClientLatestVersionStr, "0") != 0;
}

void CTClient::RequestQmClientUpdateCheckAndUpdate()
{
	if(IsUpdateChecking() || IsUpdateDownloading())
		return;

	m_QmClientAutoUpdateAfterCheck = true;
	m_FetchedQmClientUpdateInfo = false;
	m_UpdateCheckFailed = false;
	FetchQmClientUpdateInfo();
}

void CTClient::StartUpdateDownload()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return;
#else
	if(IsUpdateDownloading())
		return;

	ResetUpdateDownloadTasks();
	RemoveUpdateTempFiles();
	m_UpdateReady = false;
	m_UpdateCheckFailed = false;
	m_UpdateFailureNoticeShown = false;
	m_UpdateFailureExitAt = 0;
	IStorage::FormatTmpPath(m_aUpdatePackageTmp, sizeof(m_aUpdatePackageTmp), QMCLIENT_UPDATE_PACKAGE_NAME);
	IStorage::FormatTmpPath(m_aUpdatePackageSignatureTmp, sizeof(m_aUpdatePackageSignatureTmp), QMCLIENT_UPDATE_PACKAGE_SIGNATURE_NAME);
	IStorage::FormatTmpPath(m_aUpdateManifestTmp, sizeof(m_aUpdateManifestTmp), QMCLIENT_UPDATE_MANIFEST_NAME);
	IStorage::FormatTmpPath(m_aUpdateManifestSignatureTmp, sizeof(m_aUpdateManifestSignatureTmp), QMCLIENT_UPDATE_MANIFEST_SIGNATURE_NAME);

	const auto StartDownload = [&](std::shared_ptr<CHttpRequest> &pTask, const char *pUrl, const char *pDestination, int64_t MaxResponseSize) {
		pTask = HttpGet(pUrl);
		pTask->Timeout(CTimeout{10000, 0, 8192, 20});
		pTask->MaxResponseSize(MaxResponseSize);
		pTask->SkipByFileTime(false);
		pTask->LogProgress(HTTPLOG::FAILURE);
		pTask->WriteToFile(Storage(), pDestination, IStorage::TYPE_SAVE);
		Http()->Run(pTask);
	};
	StartDownload(m_pUpdatePackageTask, m_UpdateRelease.m_aPackageUrl, m_aUpdatePackageTmp, QMCLIENT_UPDATE_MAX_PACKAGE_SIZE);
	StartDownload(m_pUpdatePackageSignatureTask, m_UpdateRelease.m_aPackageSignatureUrl, m_aUpdatePackageSignatureTmp, 64);
	StartDownload(m_pUpdateManifestTask, m_UpdateRelease.m_aManifestUrl, m_aUpdateManifestTmp, QMCLIENT_UPDATE_MAX_MANIFEST_SIZE);
	StartDownload(m_pUpdateManifestSignatureTask, m_UpdateRelease.m_aManifestSignatureUrl, m_aUpdateManifestSignatureTmp, 64);
#endif
}

void CTClient::ResetUpdateDownloadTasks()
{
	const auto ResetTask = [](std::shared_ptr<CHttpRequest> &pTask) {
		if(pTask)
			pTask->Abort();
		pTask = nullptr;
	};
	ResetTask(m_pUpdatePackageTask);
	ResetTask(m_pUpdatePackageSignatureTask);
	ResetTask(m_pUpdateManifestTask);
	ResetTask(m_pUpdateManifestSignatureTask);
}

void CTClient::RemoveUpdateTempFiles()
{
	if(m_aUpdatePackageTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdatePackageTmp, IStorage::TYPE_SAVE);
	if(m_aUpdatePackageSignatureTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdatePackageSignatureTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateManifestTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdateManifestTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateManifestSignatureTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdateManifestSignatureTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateInstallerTmp[0] != '\0' && !m_UpdateInstallerStarted)
	{
		Storage()->RemoveFile(m_aUpdateInstallerTmp, IStorage::TYPE_ABSOLUTE);
	}
	if(!m_UpdateInstallerStarted)
		m_aUpdateInstallerTmp[0] = '\0';
	m_aUpdatePackageTmp[0] = '\0';
	m_aUpdatePackageSignatureTmp[0] = '\0';
	m_aUpdateManifestTmp[0] = '\0';
	m_aUpdateManifestSignatureTmp[0] = '\0';
}

void CTClient::ResetUpdateTasks()
{
	ResetQmClientUpdateInfoTask();
	ResetUpdateDownloadTasks();
}

void CTClient::ResetQmClientUpdateInfoTask()
{
	if(m_pQmClientUpdateInfoTask)
	{
		m_pQmClientUpdateInfoTask->Abort();
		m_pQmClientUpdateInfoTask = NULL;
	}
}

void CTClient::FetchQmClientUpdateInfo()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return;
#else
	if(m_pQmClientUpdateInfoTask && !m_pQmClientUpdateInfoTask->Done())
		return;
	m_FetchedQmClientUpdateInfo = false;
	m_UpdateCheckFailed = false;
	m_UpdateFailureNoticeShown = false;
	m_UpdateFailureExitAt = 0;
	m_aUpdateError[0] = '\0';
	m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_CHECK_INTERVAL;
	m_pQmClientUpdateInfoTask = HttpGet(QMCLIENT_INFO_URL);
	m_pQmClientUpdateInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pQmClientUpdateInfoTask->MaxResponseSize(4 * 1024 * 1024);
	m_pQmClientUpdateInfoTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmClientUpdateInfoTask);
#endif
}

void CTClient::FinishQmClientUpdateInfo()
{
	unsigned char *pResult = nullptr;
	size_t ResultSize = 0;
	m_pQmClientUpdateInfoTask->Result(&pResult, &ResultSize);
	char aError[256];
	SQmClientUpdateRelease Release;
	if(!ParseQmClientUpdateRelease(reinterpret_cast<const char *>(pResult), ResultSize, QMCLIENT_VERSION, Release, aError, sizeof(aError)))
	{
		m_FetchedQmClientUpdateInfo = true;
		m_aQmClientLatestVersionStr[0] = '0';
		m_aQmClientLatestVersionStr[1] = '\0';
		if(str_comp(aError, "GitHub release version is not newer") != 0)
		{
			m_UpdateCheckFailed = true;
			m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
			str_copy(m_aUpdateError, aError, sizeof(m_aUpdateError));
			log_error("qm-update", "release metadata rejected: %s", m_aUpdateError);
		}
		return;
	}
	m_UpdateRelease = Release;
	str_copy(m_aQmClientLatestVersionStr, Release.m_aVersion, sizeof(m_aQmClientLatestVersionStr));
	m_FetchedQmClientUpdateInfo = true;
	m_UpdateCheckFailed = false;
	StartUpdateDownload();
}

void CTClient::StartUpdateCheckIfDue()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!g_Config.m_QmAutoUpdate || m_UpdateShutdownRequested || m_UpdateReady || IsUpdateChecking() || IsUpdateDownloading())
		return;
	if(m_UpdateNextCheck == 0 || time_get() >= m_UpdateNextCheck)
		FetchQmClientUpdateInfo();
#endif
}

void CTClient::FinishUpdateDownloads()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return;
#else
	auto Fail = [&](const char *pMessage) {
		m_UpdateReady = false;
		m_UpdateCheckFailed = true;
		m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
		str_copy(m_aUpdateError, pMessage != nullptr && pMessage[0] != '\0' ? pMessage : "Update failed", sizeof(m_aUpdateError));
		log_error("qm-update", "update download or validation failed: %s", m_aUpdateError);
		ResetUpdateDownloadTasks();
		RemoveUpdateTempFiles();
		if(m_UpdateShutdownRequested)
			m_UpdateFailureExitAt = time_get() + 2 * time_freq();
		if(!m_UpdateFailureNoticeShown)
		{
			Client()->AddWarning(SWarning(Localize("Update"), Localize("Update failed. Please try again")));
			m_UpdateFailureNoticeShown = true;
		}
	};

	const auto IsSuccessful = [](const std::shared_ptr<CHttpRequest> &pTask) {
		return pTask && pTask->State() == EHttpState::DONE;
	};
	if(!IsSuccessful(m_pUpdatePackageTask) || !IsSuccessful(m_pUpdatePackageSignatureTask) ||
		!IsSuccessful(m_pUpdateManifestTask) || !IsSuccessful(m_pUpdateManifestSignatureTask))
	{
		Fail("One or more update assets failed to download");
		return;
	}

	using TUpdateData = std::unique_ptr<unsigned char, void (*)(void *)>;
	TUpdateData ManifestData(nullptr, std::free);
	TUpdateData ManifestSignatureData(nullptr, std::free);
	TUpdateData PackageSignatureData(nullptr, std::free);
	unsigned ManifestSize = 0;
	unsigned ManifestSignatureSize = 0;
	unsigned PackageSignatureSize = 0;
	void *pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdateManifestTmp, IStorage::TYPE_SAVE, &pRawData, &ManifestSize))
	{
		Fail("Failed to read the downloaded update manifest");
		return;
	}
	ManifestData.reset(static_cast<unsigned char *>(pRawData));
	pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdateManifestSignatureTmp, IStorage::TYPE_SAVE, &pRawData, &ManifestSignatureSize))
	{
		Fail("Failed to read the downloaded manifest signature");
		return;
	}
	ManifestSignatureData.reset(static_cast<unsigned char *>(pRawData));
	pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdatePackageSignatureTmp, IStorage::TYPE_SAVE, &pRawData, &PackageSignatureSize))
	{
		Fail("Failed to read the downloaded package signature");
		return;
	}
	PackageSignatureData.reset(static_cast<unsigned char *>(pRawData));

	char aError[256] = "";
	uint64_t SignedPackageSize = 0;
	uint8_t aSignedPackageDigest[SHA256_DIGEST_LENGTH] = {};
	if(!qm_update_verify_manifest_package(ManifestData.get(), ManifestSize, ManifestSignatureData.get(), ManifestSignatureSize,
		   &SignedPackageSize, aSignedPackageDigest, sizeof(aSignedPackageDigest), aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}

	SQmClientUpdateManifest Manifest;
	if(!ParseQmClientUpdateManifest(reinterpret_cast<const char *>(ManifestData.get()), ManifestSize, QMCLIENT_VERSION, Manifest, aError, sizeof(aError)) ||
		str_comp(Manifest.m_aVersion, m_UpdateRelease.m_aVersion) != 0 ||
		Manifest.m_PackageSize != SignedPackageSize || mem_comp(Manifest.m_PackageSha256.data, aSignedPackageDigest, sizeof(aSignedPackageDigest)) != 0)
	{
		Fail(aError[0] != '\0' ? aError : "Update manifest does not match the selected release");
		return;
	}

	const SHA256_DIGEST ActualDigest = m_pUpdatePackageTask->ResultSha256();
	IOHANDLE PackageFile = Storage()->OpenFile(m_aUpdatePackageTmp, IOFLAG_READ, IStorage::TYPE_SAVE);
	const int64_t ActualSize = PackageFile ? io_length(PackageFile) : -1;
	if(PackageFile)
		io_close(PackageFile);
	if(ActualSize < 0 || static_cast<uint64_t>(ActualSize) != SignedPackageSize || ActualDigest != Manifest.m_PackageSha256)
	{
		Fail("Downloaded update package size or SHA-256 is invalid");
		return;
	}
	if(!qm_update_verify_package_digest(ActualDigest.data, sizeof(ActualDigest.data), PackageSignatureData.get(), PackageSignatureSize, aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}
	if(!Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE))
	{
		Fail("Failed to create the update working directory");
		return;
	}
	char aPackagePath[IO_MAX_PATH_LENGTH] = "";
	char aInstallerPath[IO_MAX_PATH_LENGTH] = "";
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageTmp, aPackagePath, sizeof(aPackagePath));
	str_format(m_aUpdateInstallerTmp, sizeof(m_aUpdateInstallerTmp), "qmclient/QmClient-Updater-%d.exe", process_id());
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateInstallerTmp, aInstallerPath, sizeof(aInstallerPath));
	str_copy(m_aUpdateInstallerTmp, aInstallerPath, sizeof(m_aUpdateInstallerTmp));
	Storage()->RemoveFile(aInstallerPath, IStorage::TYPE_ABSOLUTE);
	if(!qm_update_extract_bootstrap_updater(aPackagePath, ManifestData.get(), ManifestSize,
		   ManifestSignatureData.get(), ManifestSignatureSize, aInstallerPath, aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}

	m_UpdateReady = true;
	m_UpdateCheckFailed = false;
	m_aUpdateError[0] = '\0';
	ResetUpdateDownloadTasks();
	Client()->AddWarning(SWarning(Localize("Update notice"), Localize("The update is ready and will be installed when you exit.")));
#endif
}

bool CTClient::LaunchUpdateInstaller()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return false;
#else
	if(!m_UpdateReady || m_UpdateInstallerStarted)
		return m_UpdateInstallerStarted;

	char aPackagePath[IO_MAX_PATH_LENGTH] = "";
	char aPackageSignaturePath[IO_MAX_PATH_LENGTH] = "";
	char aManifestPath[IO_MAX_PATH_LENGTH] = "";
	char aManifestSignaturePath[IO_MAX_PATH_LENGTH] = "";
	char aInstallPath[IO_MAX_PATH_LENGTH] = "";
	if(!fs_is_file(m_aUpdateInstallerTmp))
		return false;

	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageTmp, aPackagePath, sizeof(aPackagePath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageSignatureTmp, aPackageSignaturePath, sizeof(aPackageSignaturePath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateManifestTmp, aManifestPath, sizeof(aManifestPath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateManifestSignatureTmp, aManifestSignaturePath, sizeof(aManifestSignaturePath));
	Storage()->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aInstallPath, sizeof(aInstallPath));
	if(fs_parent_dir(aInstallPath) != 0)
	{
		RemoveUpdateTempFiles();
		return false;
	}

	char aPid[32];
	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {
		"--parent-pid",
		aPid,
		"--package",
		aPackagePath,
		"--package-signature",
		aPackageSignaturePath,
		"--manifest",
		aManifestPath,
		"--manifest-signature",
		aManifestSignaturePath,
		"--install",
		aInstallPath,
	};
	const PROCESS Process = process_execute(m_aUpdateInstallerTmp, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments));
	if(Process == INVALID_PROCESS)
	{
		RemoveUpdateTempFiles();
		return false;
	}
	CloseHandle(static_cast<HANDLE>(Process));
	m_UpdateInstallerStarted = true;
	return true;
#endif
}

bool CTClient::PrepareForShutdown(bool Force)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(m_UpdateInstallerStarted)
		return false;
	if(Force && m_UpdateShutdownRequested && (m_UpdateReady || IsUpdateChecking() || IsUpdateDownloading()))
	{
		ResetUpdateTasks();
		RemoveUpdateTempFiles();
		return false;
	}
	if(m_UpdateReady)
	{
		m_UpdateShutdownRequested = true;
		return true;
	}
	if(!g_Config.m_QmAutoUpdate || m_UpdateCheckFailed)
		return false;
	if(IsUpdateChecking() || IsUpdateDownloading())
	{
		m_UpdateShutdownRequested = true;
		return true;
	}
#else
	(void)Force;
#endif
	return false;
}

const char *CTClient::UpdateShutdownMessage() const
{
	if(m_UpdateCheckFailed)
		return Localize("Update failed. Please try again");
	if(m_UpdateReady)
		return Localize("Installing update. Please wait...");
	return Localize("Downloading update...");
}

void CTClient::QueueAspectApply()
{
	m_QmAspectApplyPending = true;
}

void CTClient::SetForcedAspect()
{
	m_QmAspectApplyPending = false;

	// TODO: Fix flashing on windows
	int State = Client()->State();
	bool Force = true;
	float GameScreenAspectOverride = 0.0f;
	if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;

	if(g_Config.m_QmAspectPreset != 0)
	{
		int AspectRatio = 0;
		switch(g_Config.m_QmAspectPreset)
		{
		case 1: AspectRatio = 125; break;
		case 2: AspectRatio = 133; break;
		case 3: AspectRatio = 150; break;
		case 4: AspectRatio = 178; break;
		case 5: AspectRatio = 233; break;
		case 6: AspectRatio = std::clamp(g_Config.m_QmAspectRatio, 100, 300); break;
		default: AspectRatio = 0; break;
		}

		if(AspectRatio > 0)
			GameScreenAspectOverride = AspectRatio / 100.0f;
	}

	Graphics()->SetGameScreenAspectOverride(GameScreenAspectOverride);
	Graphics()->SetForcedAspect(Force);
}

void CTClient::OnStateChange(int NewState, int OldState)
{
	SetForcedAspect();
	if(NewState != IClient::STATE_ONLINE)
	{
		ResetGoresDummyHammerOverride();
		m_LocalSaveRestore.Reset();
		m_LocalSaveConfirmation.Reset();
		m_vLocalSaveCandidates.clear();
		m_LocalSavePromptActive = false;
		ResetFinishRenameState();
		EndMapHistorySession(true);
		m_MapHistorySuppressedMapId.clear();
	}
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};
	ClearFreezeWakeupPopups();

	if(NewState != IClient::STATE_ONLINE)
	{
		m_GoresModeStateKnown = false;
		m_PrevGoresModeActive = false;
		m_GoresAutoMapKnown = false;
		m_GoresAutoMapToken = 0;
		ClearSwapCountdown();
		m_aLastChatMessage[0] = '\0';
		m_LastChatTeam = 0;
		m_LastRepeatTime = 0;
		m_LastRepeatKeyPressTime = 0;
		m_RepeatKeyDown = false;
		m_LastAutoReplyTime = 0;
		m_RedPacketAutoClaim.Reset();
		m_FinishTextTimeout = 0.0f;
		for(int i = 0; i < NUM_DUMMIES; ++i)
		{
			m_aWasInDeath[i] = false;
			m_aLastWaterFallTime[i] = 0;
			m_aLastWaterHeartTime[i] = 0;
			m_aLastWaterMessageTime[i] = 0;
			m_aWasInFreeze[i] = false;
			m_aLastFreezeEmoteTime[i] = 0;
			m_aLastFreezeMessageTime[i] = 0;
			m_aWasInFreezeForSwitch[i] = false;
			m_aWasInFreezeForGoresHammer[i] = false;
			m_aGoresHammerWakeupFirePendingRelease[i] = false;
			m_aWasInFreezeForChatClose[i] = false;
		}
		ResetComboState();
		InvalidateGoresDistanceField();
		ResetFriendEnter();
		m_FriendOnlineTracker.Reset();
		m_FriendOnlineRefreshPending = false;
		m_FriendAutoRefreshNext = 0.0f;
		m_aLastLocalSaveHintMap[0] = '\0';
	}
	m_aLastGameplayLogicTick[0] = -1;
	m_aLastGameplayLogicTick[1] = -1;

	if(NewState == IClient::STATE_ONLINE)
	{
		m_GoresModeStateKnown = false;
		m_PrevGoresModeActive = IsGoresModuleEnabled();
		m_GoresAutoMapKnown = false;
		m_GoresAutoMapToken = 0;
	}

	// 进入服务器时重置统计数据
	if(NewState == IClient::STATE_ONLINE && g_Config.m_QmPlayerStatsResetOnJoin)
	{
		ResetPlayerStats(-1);
	}
}

void CTClient::OnNewSnapshot()
{
	CheckHammerWakeupActions();
	// snapshot 处理期间不能同步触发全局 resize，延迟到下一次常规更新处理。
	QueueAspectApply();
	ApplyGoresFastInputLink(true);
	MaybeShowLocalSaveJoinHint();
	// Update volleyball
	bool IsVolleyBall = false;
	if(g_Config.m_TcVolleyBallBetterBall > 0 && g_Config.m_TcVolleyBallBetterBallSkin[0] != '\0')
	{
		if(g_Config.m_TcVolleyBallBetterBall > 1)
			IsVolleyBall = true;
		else
			IsVolleyBall = str_startswith_nocase(Client()->GetCurrentMap(), "volleyball");
	};
	for(auto &Client : GameClient()->m_aClients)
	{
		Client.m_IsVolleyBall = IsVolleyBall && Client.m_DeepFrozen;
	}
	// Update air rescue
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int ClientId = GameClient()->m_aLocalIds[Dummy];
			if(ClientId == -1)
				continue;
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active)
				continue;
			if(Client()->GameTick(Dummy) % 10 != 0) // Works for both 25tps and 50tps
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_FreezeEnd == -1) // You aren't safe when frozen
				continue;
			const vec2 NewPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			// If new pos is under 2 tiles from old pos, don't record a new position
			if(!m_aAirRescuePositions[Dummy].empty())
			{
				const vec2 OldPos = m_aAirRescuePositions[Dummy].front();
				if(distance(NewPos, OldPos) < 64.0f)
					continue;
			}
			if(m_aAirRescuePositions[Dummy].size() >= 256)
				m_aAirRescuePositions[Dummy].pop_back();
			m_aAirRescuePositions[Dummy].push_front(NewPos);
		}
	}
}

constexpr const char STRIP_CHARS[] = {'-', '=', '+', '_', ' '};
static bool IsStripChar(char c)
{
	return std::any_of(std::begin(STRIP_CHARS), std::end(STRIP_CHARS), [c](char s) {
		return s == c;
	});
}

static void StripStr(const char *pIn, char *pOut, const char *pEnd)
{
	if(!pIn)
	{
		*pOut = '\0';
		return;
	}

	while(*pIn && IsStripChar(*pIn))
		pIn++;

	// Special behaviour for empty checkbox
	if((unsigned char)*pIn == 0xE2 && (unsigned char)(*(pIn + 1)) == 0x98 && (unsigned char)(*(pIn + 2)) == 0x90)
	{
		pIn += 3;
		while(*pIn && IsStripChar(*pIn))
			pIn++;
	}

	char *pLastValid = nullptr;
	while(*pIn && pOut < pEnd - 1)
	{
		*pOut = *pIn;
		if(!IsStripChar(*pIn))
			pLastValid = pOut;
		pIn++;
		pOut++;
	}

	if(pLastValid)
		*(pLastValid + 1) = '\0';
	else
		*pOut = '\0';
}

void CTClient::RenderMiniVoteHud(bool HudEditorPreview)
{
	CUIRect View = {0.0f, 60.0f, 70.0f, 35.0f};
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::Voting, View);
	Ui()->RenderGaussianBlur(View, 1.0f, HudEditorScope.m_Corners, 3.0f);
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), HudEditorScope.m_Corners, 3.0f);
	View.Margin(3.0f, &View);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = View.w;

	CUIRect Row, LeftColumn, RightColumn, ProgressSpinner;
	char aBuf[256];
	char aVoteDescription[256];
	char aVoteReason[256];

	// Vote description
	View.HSplitTop(6.0f, &Row, &View);
	if(HudEditorPreview && !GameClient()->m_Voting.IsVoting())
		str_copy(aVoteDescription, "funvote", sizeof(aVoteDescription));
	else
		GameClient()->FormatStreamerVoteText(GameClient()->m_Voting.VoteDescription(), aVoteDescription, sizeof(aVoteDescription));
	StripStr(aVoteDescription, aBuf, aBuf + sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	// Vote reason
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	if(HudEditorPreview && !GameClient()->m_Voting.IsVoting())
		str_copy(aVoteReason, "No reason given", sizeof(aVoteReason));
	else
		GameClient()->FormatStreamerVoteText(GameClient()->m_Voting.VoteReason(), aVoteReason, sizeof(aVoteReason));
	Ui()->DoLabel(&Row, aVoteReason, 4.0f, TEXTALIGN_ML, Props);

	// Time left
	int Seconds = GameClient()->m_Voting.SecondsLeft();
	if(HudEditorPreview && Seconds < 0)
		Seconds = 24;
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), Seconds);
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	Row.VSplitLeft(2.0f, nullptr, &Row);
	Row.VSplitLeft(3.0f, &ProgressSpinner, &Row);
	Row.VSplitLeft(2.0f, nullptr, &Row);

	SProgressSpinnerProperties ProgressProps;
	ProgressProps.m_Progress = std::clamp((time() - GameClient()->m_Voting.m_Opentime) / (float)(GameClient()->m_Voting.m_Closetime - GameClient()->m_Voting.m_Opentime), 0.0f, 1.0f);
	Ui()->RenderProgressSpinner(ProgressSpinner.Center(), ProgressSpinner.h / 2.0f, ProgressProps);

	Ui()->DoLabel(&Row, aBuf, 3.0f, TEXTALIGN_ML);

	// Bars
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	GameClient()->m_Voting.RenderBars(Row);

	// F3 / F4
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(0.5f, &Row, &View);
	Row.VSplitMid(&LeftColumn, &RightColumn, 4.0f);

	char aKey[64];
	GameClient()->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == 1 ? ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&LeftColumn, aKey[0] == '\0' ? Localize("Agree") : aKey, 0.5f, TEXTALIGN_ML);

	GameClient()->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == -1 ? ColorRGBA(0.95f, 0.25f, 0.25f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&RightColumn, aKey[0] == '\0' ? Localize("Disagree") : aKey, 0.5f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::Voting, {0.0f, 60.0f, 70.0f, 35.0f});
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CTClient::RenderCenterLines()
{
	if(g_Config.m_TcShowCenter <= 0)
		return;

	if(GameClient()->m_Scoreboard.IsActive())
		return;

	Graphics()->TextureClear();

	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const float XMid = (X0 + X1) / 2.0f;
	const float YMid = (Y0 + Y1) / 2.0f;

	if(g_Config.m_TcShowCenterWidth == 0)
	{
		Graphics()->LinesBegin();
		IGraphics::CLineItem aLines[2] = {
			{XMid, Y0, XMid, Y1},
			{X0, YMid, X1, YMid}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
	}
	else
	{
		const float W = g_Config.m_TcShowCenterWidth;
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem aQuads[3] = {
			{XMid, mix(Y0, Y1, 0.25f) - W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, mix(Y0, Y1, 0.75f) + W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, YMid, X1 - X0, W}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->QuadsDraw(aQuads, std::size(aQuads));
		Graphics()->QuadsEnd();
	}
}

void CTClient::RenderCtfFlag(vec2 Pos, float Alpha)
{
	// from CItems::RenderFlag
	float Size = 42.0f;
	int QuadOffset;
	if(g_Config.m_TcFakeCtfFlags == 1)
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
		QuadOffset = GameClient()->m_Items.m_RedFlagOffset;
	}
	else
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
		QuadOffset = GameClient()->m_Items.m_BlueFlagOffset;
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->RenderQuadContainerAsSprite(GameClient()->m_Items.m_ItemsQuadContainerIndex, QuadOffset, Pos.x, Pos.y - Size * 0.75f);
}

void CTClient::ResetPlayerStats(int Dummy)
{
	if(Dummy < 0)
	{
		// 重置所有
		for(auto &Stats : m_aPlayerStats)
			Stats.Reset();
	}
	else if(Dummy < NUM_DUMMIES)
	{
		m_aPlayerStats[Dummy].Reset();
	}
}

void CTClient::UpdatePlayerStats()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Only check for active dummy
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active)
			continue;

		SPlayerStats &Stats = m_aPlayerStats[Dummy];
		float CurrentX = (float)Char.m_Cur.m_X;
		float CurrentY = (float)Char.m_Cur.m_Y;

		// 检测 freeze 状态变化（用于存活时长统计）
		bool IsInFreeze = ClientData.m_FreezeEnd != 0;

		if(!IsInFreeze && !Stats.m_IsAlive)
		{
			// 刚解冻，开始计时
			Stats.m_IsAlive = true;
			Stats.m_CurrentAliveStart = Client()->GameTick(Dummy);

			// 检查位置是否变化很大（重生了），如果是则不算被救醒
			float Dist = 0.0f;
			if(Stats.m_FreezeX != 0.0f || Stats.m_FreezeY != 0.0f)
			{
				float Dx = CurrentX - Stats.m_FreezeX;
				float Dy = CurrentY - Stats.m_FreezeY;
				Dist = std::sqrt(Dx * Dx + Dy * Dy);
			}

			// 如果位置变化小于200单位，说明是原地解冻，算被救醒
			const float RespawnThreshold = 200.0f;
			if(Dist < RespawnThreshold && (Stats.m_FreezeX != 0.0f || Stats.m_FreezeY != 0.0f))
			{
				Stats.m_RescueCount++;
			}
		}
		else if(IsInFreeze && Stats.m_IsAlive)
		{
			// 刚被冻结，结束计时，落水次数+1，记录冻结位置
			Stats.m_IsAlive = false;
			Stats.m_FreezeCount++;
			Stats.m_FreezeX = CurrentX;
			Stats.m_FreezeY = CurrentY;
			int AliveTime = Client()->GameTick(Dummy) - Stats.m_CurrentAliveStart;
			if(AliveTime > 0)
			{
				Stats.m_TotalAliveTime += AliveTime;
				Stats.m_AliveCount++;
				if(AliveTime > Stats.m_MaxAliveTime)
					Stats.m_MaxAliveTime = AliveTime;
			}
		}

		// 跟踪出钩方向
		TrackHookDirection(Dummy);
	}
}

void CTClient::TrackHookDirection(int Dummy)
{
	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	if(ClientId < 0)
		return;

	const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(!Char.m_Active)
		return;

	SPlayerStats &Stats = m_aPlayerStats[Dummy];

	// 检测 hook 状态
	bool IsHooking = Char.m_Cur.m_HookState > 0;

	// 检测开始出钩的瞬间
	if(IsHooking && !Stats.m_WasHooking)
	{
		// 使用钩子位置相对于玩家位置来判断方向
		float HookX = (float)(Char.m_Cur.m_HookX - Char.m_Cur.m_X);
		if(HookX < 0)
			Stats.m_HookLeftCount++;
		else if(HookX > 0)
			Stats.m_HookRightCount++;
	}

	Stats.m_WasHooking = IsHooking;
}

bool CTClient::IsGoresGameMode() const
{
	const char *pGameType = GameClient()->m_GameInfo.m_aGameType;
	return pGameType != nullptr && pGameType[0] != '\0' && str_find_nocase(pGameType, "gores") != nullptr;
}

bool CTClient::IsGoresMapProgressMap() const
{
	if(IsGoresGameMode() || IsDDraceMapProgressMap())
		return true;

	const char *pMap = Client()->GetCurrentMap();
	return pMap != nullptr && str_comp_nocase(pMap, "NUT_race9") == 0;
}

bool CTClient::IsDDraceMapProgressMap() const
{
	const char *pMap = Client()->GetCurrentMap();
	return !IsGoresGameMode() && GameClient()->m_GameInfo.m_Race && GameClient()->m_GameInfo.m_PredictDDRace &&
	       (!pMap || str_comp_nocase(pMap, "NUT_race9") != 0);
}

bool CTClient::IsGoresModuleEnabled() const
{
	return g_Config.m_QmGores != 0;
}

bool CTClient::IsFastInputActive() const
{
	return g_Config.m_TcFastInput != 0;
}

bool CTClient::IsFastInputOthersActive() const
{
	return g_Config.m_TcFastInputOthers != 0;
}

bool CTClient::ShouldHideGoresGuides(bool ManualGuideVisible) const
{
	return ShouldHideGoresGuide(IsGoresModuleEnabled(), g_Config.m_QmGoresHideGuides != 0, ManualGuideVisible);
}

bool CTClient::HasBlockingGoresWeapon() const
{
	if(!g_Config.m_QmGoresDisableIfWeapons)
		return false;
	return HasExtraGoresWeapon();
}

bool CTClient::HasExtraGoresWeapon() const
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalCharacter)
		return false;
	const CCharacterCore &Core = GameClient()->m_PredictedPrevChar;
	return Core.m_aWeapons[WEAPON_SHOTGUN].m_Got ||
	       Core.m_aWeapons[WEAPON_GRENADE].m_Got ||
	       Core.m_aWeapons[WEAPON_LASER].m_Got ||
	       Core.m_aWeapons[WEAPON_NINJA].m_Got;
}

bool CTClient::ShouldAppendGoresPrevWeapon() const
{
	return Client()->State() == IClient::STATE_ONLINE &&
	       !GameClient()->m_Snap.m_SpecInfo.m_Active &&
	       GameClient()->m_Snap.m_pLocalCharacter != nullptr &&
	       IsGoresModuleEnabled() &&
	       g_Config.m_QmGoresAutoWeaponSwitch != 0 &&
	       !HasExtraGoresWeapon();
}

void CTClient::UpdateGoresWeaponCycle()
{
	const int Dummy = g_Config.m_ClDummy;
	CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[Dummy];
	const bool FireHeld = (Input.m_Fire & 1) != 0;
	const bool FireJustPressed = FireHeld && !m_aPrevFireForGores[Dummy];
	m_aPrevFireForGores[Dummy] = FireHeld;
	if(ShouldReleaseQmGoresHammerWakeupFire(m_aGoresHammerWakeupFirePendingRelease[Dummy], Input.m_Fire))
		Input.m_Fire = QmGoresHammerWakeupReleaseFireState(Input.m_Fire);
	m_aGoresHammerWakeupFirePendingRelease[Dummy] = false;

	const bool GoresCycleActive = ShouldAppendGoresPrevWeapon();
	const bool MultiWeaponPulseActive =
		Client()->State() == IClient::STATE_ONLINE &&
		!GameClient()->m_Snap.m_SpecInfo.m_Active &&
		GameClient()->m_Snap.m_pLocalCharacter != nullptr &&
		IsGoresModuleEnabled() &&
		g_Config.m_QmGoresAutoWeaponSwitch != 0 &&
		g_Config.m_QmGoresDisableIfWeapons != 0 &&
		HasExtraGoresWeapon();
	if(!GoresCycleActive && !MultiWeaponPulseActive)
	{
		for(bool &WasInFreeze : m_aWasInFreezeForGoresHammer)
			WasInFreeze = false;
		m_aGoresHasPreHammerWeapon[Dummy] = false;
		return;
	}

	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	bool InFreeze = false;
	bool ExternalHammerWakeup = false;
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_Active)
	{
		InFreeze = GameClient()->m_aClients[ClientId].m_FreezeEnd != 0;
		const bool JustUnfrozen = m_aWasInFreezeForGoresHammer[Dummy] && !InFreeze;
		ExternalHammerWakeup = JustUnfrozen && DetectFreezeWakeupType(GameClient(), ClientId, Client()->GameTick(Dummy)) == EFreezeWakeupType::EXTERNAL_HAMMER;
	}

	const bool CurrentWeaponIsHammer = GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_HAMMER;
	if(ShouldPulseGoresHammerOnFire(MultiWeaponPulseActive, FireJustPressed, CurrentWeaponIsHammer, ExternalHammerWakeup))
	{
		m_aGoresPreHammerWeapon[Dummy] = GameClient()->m_Snap.m_pLocalCharacter->m_Weapon;
		m_aGoresHasPreHammerWeapon[Dummy] = true;
		Input.m_WantedWeapon = WEAPON_HAMMER + 1;
		Input.m_Fire = QmGoresHammerWakeupFireState(Input.m_Fire);
		m_aGoresHammerWakeupFirePendingRelease[Dummy] = !FireHeld;
		m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
		return;
	}

	if(ShouldRestoreGoresWeaponAfterHammer(CurrentWeaponIsHammer, m_aGoresHasPreHammerWeapon[Dummy]))
	{
		const int RestoreWeapon = GoresRestoreWeaponAfterHammer(m_aGoresPreHammerWeapon[Dummy], true);
		GameClient()->m_Controls.m_aInputData[Dummy].m_WantedWeapon = RestoreWeapon + 1;
		m_aGoresHasPreHammerWeapon[Dummy] = false;
		m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
		return;
	}

	if(!GoresCycleActive)
	{
		m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
		return;
	}

	const bool HammerRequested = Input.m_WantedWeapon == WEAPON_HAMMER + 1;
	if(ShouldTriggerQmGoresHammerWakeup(GoresCycleActive, HammerRequested, ExternalHammerWakeup))
	{
		Input.m_WantedWeapon = WEAPON_HAMMER + 1;
		Input.m_Fire = QmGoresHammerWakeupFireState(Input.m_Fire);
		m_aGoresHammerWakeupFirePendingRelease[Dummy] = !FireHeld;
		m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
		return;
	}

	if(ShouldKeepQmGoresHammerInFreeze(GoresCycleActive, InFreeze, HammerRequested))
	{
		m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
		return;
	}

	if(GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_HAMMER)
	{
		const int RestoreWeapon = GoresRestoreWeaponAfterHammer(m_aGoresPreHammerWeapon[Dummy], m_aGoresHasPreHammerWeapon[Dummy]);
		GameClient()->m_Controls.m_aInputData[Dummy].m_WantedWeapon = RestoreWeapon + 1;
		m_aGoresHasPreHammerWeapon[Dummy] = false;
	}

	m_aWasInFreezeForGoresHammer[Dummy] = InFreeze;
}

bool CTClient::IsGoresMapProgressEnabled() const
{
	return IsGoresMapProgressMap() && g_Config.m_QmPlayerStatsMapProgress;
}

bool CTClient::IsGoresMapProgressDebugRouteEnabled() const
{
	return IsGoresMapProgressMap() && g_Config.m_QmPlayerStatsMapProgressDbgRoute != 0;
}

void CTClient::InvalidateGoresDistanceField()
{
	ResetDDraceMapProgress();
	m_GoresDistanceFieldValid = false;
	m_GoresDistanceFieldAttempted = false;
	m_GoresDistanceFieldNextBuildTryTick = 0;
	m_GoresDistanceFieldWidth = 0;
	m_GoresDistanceFieldHeight = 0;
	m_vGoresCMap.clear();
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceToFinish.clear();
	m_GoresRouteStartIndex.Reset();
	m_GoresDebugRouteVisited.Reset();
	ResetGoresDistanceFieldBuild();
	for(int i = 0; i < NUM_DUMMIES; ++i)
	{
		m_aGoresWasOnStartLastTick[i] = false;
		m_aGoresRunStarted[i] = false;
		m_aGoresRunStartDistanceToFinish[i] = 0;
		m_aGoresMapProgressValid[i] = false;
		m_aGoresMapProgress[i] = 0.0f;
	}
}

void CTClient::EnsureGoresDistanceField()
{
	if(Client()->State() != IClient::STATE_ONLINE || !IsGoresMapProgressMap() || IsDDraceMapProgressMap())
		return;

	const char *pCurrentMap = Client()->GetCurrentMap();
	const char *pMap = pCurrentMap ? pCurrentMap : "";
	if(str_comp(m_aGoresDistanceFieldMap, pMap) != 0)
	{
		InvalidateGoresDistanceField();
		str_copy(m_aGoresDistanceFieldMap, pMap, sizeof(m_aGoresDistanceFieldMap));
	}

	if(m_GoresDistanceFieldValid)
		return;

	const int64_t Now = time_get();
	if(m_GoresDistanceFieldBuildStage == EGoresDistanceFieldBuildStage::IDLE && m_GoresDistanceFieldAttempted && Now < m_GoresDistanceFieldNextBuildTryTick)
		return;

	if(m_GoresDistanceFieldBuildStage == EGoresDistanceFieldBuildStage::IDLE)
	{
		m_GoresDistanceFieldAttempted = true;
		StartGoresDistanceFieldBuild();
	}
	ContinueGoresDistanceFieldBuild();
	if(m_GoresDistanceFieldValid || m_GoresDistanceFieldBuildStage != EGoresDistanceFieldBuildStage::IDLE)
		m_GoresDistanceFieldNextBuildTryTick = 0;
	else
		m_GoresDistanceFieldNextBuildTryTick = Now + time_freq();
}

void CTClient::ResetGoresDistanceFieldBuild()
{
	ReleaseGoresDistanceFieldVisualLayerData();
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::IDLE;
	m_GoresDistanceFieldBuildMapSize = 0;
	m_GoresDistanceFieldBuildCursor = 0;
	m_GoresDistanceFieldBuildGroup = 0;
	m_GoresDistanceFieldBuildLayer = 0;
	m_GoresDistanceFieldBuildLoadedVisualLayerData = -1;
	m_GoresDistanceFieldBuildHadStart = false;
	m_pGoresDistanceFieldBuildMap = nullptr;
	m_pGoresDistanceFieldBuildGameLayer = nullptr;
	m_pGoresDistanceFieldBuildFrontLayer = nullptr;
	m_pGoresDistanceFieldBuildTeleLayer = nullptr;
	m_GoresDistanceFieldBuildPendingTeleNumber = 0;
	m_GoresDistanceFieldBuildPendingTeleCursor = 0;
	m_GoresDistanceFieldBuildPendingTeleDistance = 0;
	m_vGoresDistanceFieldBuildPassable.clear();
	m_vGoresDistanceFieldBuildImageSemantics.clear();
	m_vGoresDistanceFieldBuildFinishIndices.clear();
	m_vvGoresDistanceFieldBuildDirectTeleInputs.clear();
	m_GoresDistanceFieldBuildQueue = {};
}

void CTClient::ReleaseGoresDistanceFieldVisualLayerData()
{
	if(m_GoresDistanceFieldBuildLoadedVisualLayerData < 0)
		return;
	if(const CLayers *pLayers = Layers())
	{
		if(IMap *pMap = pLayers->Map())
		{
			if(pMap == m_pGoresDistanceFieldBuildMap)
				pMap->UnloadData(m_GoresDistanceFieldBuildLoadedVisualLayerData);
		}
	}
	m_GoresDistanceFieldBuildLoadedVisualLayerData = -1;
}

void CTClient::StartGoresDistanceFieldBuild()
{
	ResetGoresDistanceFieldBuild();
	m_GoresDistanceFieldValid = false;
	m_GoresDistanceFieldWidth = 0;
	m_GoresDistanceFieldHeight = 0;
	m_vGoresCMap.clear();
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceToFinish.clear();
	m_GoresRouteStartIndex.Reset();

	const CCollision *pCollision = Collision();
	if(!pCollision)
		return;

	const CTile *pGame = pCollision->GameLayer();
	if(!pGame)
		return;
	const CTile *pFront = pCollision->FrontLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	if(Width <= 0 || Height <= 0)
		return;

	const int64_t MapSize64 = (int64_t)Width * Height;
	if(MapSize64 <= 0 || MapSize64 > std::numeric_limits<int>::max())
		return;
	const int MapSize = (int)MapSize64;

	m_pGoresDistanceFieldBuildMap = Layers() ? Layers()->Map() : nullptr;
	m_pGoresDistanceFieldBuildGameLayer = pGame;
	m_pGoresDistanceFieldBuildFrontLayer = pFront;
	m_pGoresDistanceFieldBuildTeleLayer = pTele;
	m_vGoresCMap.assign((size_t)MapSize, GORES_CMAP_NORMAL);
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceFieldBuildPassable.assign((size_t)MapSize, 0);
	m_vGoresDistanceFieldBuildFinishIndices.reserve(16);
	m_GoresDistanceFieldBuildMapSize = MapSize;
	m_GoresDistanceFieldWidth = Width;
	m_GoresDistanceFieldHeight = Height;
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::SCAN_TILES;
}

void CTClient::ContinueGoresDistanceFieldBuild()
{
	if(!IsGoresDistanceFieldBuildContextCurrent())
	{
		FailGoresDistanceFieldBuild();
		return;
	}

	switch(m_GoresDistanceFieldBuildStage)
	{
	case EGoresDistanceFieldBuildStage::IDLE:
		return;
	case EGoresDistanceFieldBuildStage::SCAN_TILES:
		StepGoresDistanceFieldTileScan(GORES_DISTANCE_FIELD_TILE_SCAN_BUDGET);
		break;
	case EGoresDistanceFieldBuildStage::SCAN_VISUAL_LAYERS:
		StepGoresDistanceFieldVisualLayers(GORES_DISTANCE_FIELD_VISUAL_TILE_BUDGET);
		break;
	case EGoresDistanceFieldBuildStage::INIT_QUEUE:
		StepGoresDistanceFieldQueueInit(GORES_DISTANCE_FIELD_QUEUE_INIT_BUDGET);
		break;
	case EGoresDistanceFieldBuildStage::DIJKSTRA:
		StepGoresDistanceFieldDijkstra(GORES_DISTANCE_FIELD_DIJKSTRA_BUDGET);
		break;
	case EGoresDistanceFieldBuildStage::CHECK_REACHABLE_START:
		StepGoresDistanceFieldReachableStartCheck(GORES_DISTANCE_FIELD_REACHABLE_SCAN_BUDGET);
		break;
	}
}

bool CTClient::IsGoresDistanceFieldBuildContextCurrent() const
{
	if(m_GoresDistanceFieldBuildStage == EGoresDistanceFieldBuildStage::IDLE)
		return true;
	const CCollision *pCollision = Collision();
	if(!pCollision)
		return false;
	if(pCollision->GetWidth() != m_GoresDistanceFieldWidth ||
		pCollision->GetHeight() != m_GoresDistanceFieldHeight)
		return false;
	if(pCollision->GameLayer() != m_pGoresDistanceFieldBuildGameLayer ||
		pCollision->FrontLayer() != m_pGoresDistanceFieldBuildFrontLayer ||
		pCollision->TeleLayer() != m_pGoresDistanceFieldBuildTeleLayer)
		return false;
	const CLayers *pLayers = Layers();
	return (pLayers ? pLayers->Map() : nullptr) == m_pGoresDistanceFieldBuildMap;
}

void CTClient::FailGoresDistanceFieldBuild()
{
	m_GoresDistanceFieldValid = false;
	m_GoresDistanceFieldWidth = 0;
	m_GoresDistanceFieldHeight = 0;
	m_vGoresCMap.clear();
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceToFinish.clear();
	m_GoresRouteStartIndex.Reset();
	ResetGoresDistanceFieldBuild();
}

void CTClient::CompleteGoresDistanceFieldBuild()
{
	m_GoresDistanceFieldValid = true;
	ResetGoresDistanceFieldBuild();
}

void CTClient::StepGoresDistanceFieldTileScan(int Budget)
{
	const CCollision *pCollision = Collision();
	if(!pCollision || m_GoresDistanceFieldBuildMapSize <= 0)
	{
		FailGoresDistanceFieldBuild();
		return;
	}

	const CTile *pGame = pCollision->GameLayer();
	if(!pGame)
	{
		FailGoresDistanceFieldBuild();
		return;
	}
	const CTile *pFront = pCollision->FrontLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();
	const int MapSize = m_GoresDistanceFieldBuildMapSize;
	const int Start = m_GoresDistanceFieldBuildCursor;
	ConsumeQmBudgetedWork(m_GoresDistanceFieldBuildCursor, MapSize, Budget);
	for(int Index = Start; Index < m_GoresDistanceFieldBuildCursor; ++Index)
	{
		const int Tile = pGame[Index].m_Index;
		const int FrontTile = pFront ? pFront[Index].m_Index : TILE_AIR;
		const bool IsStart = m_GoresRouteStartIndex.AddTile(Index, Tile, FrontTile);
		const bool IsFinish = Tile == TILE_FINISH || FrontTile == TILE_FINISH;
		const bool HasPenalty = IsPenaltyTileForGoresDistanceField(Tile) || IsPenaltyTileForGoresDistanceField(FrontTile);
		const bool HasReward = IsRewardTileForGoresDistanceField(Tile) || IsRewardTileForGoresDistanceField(FrontTile);
		const bool HasTeleport = pTele && pTele[Index].m_Type != 0;
		const bool HasTeeSpace = IsStart || IsFinish || IsGoresDistanceFieldTileStandable(pCollision, pGame, pFront, Index);
		const bool IsBlocked = !IsStart && !IsFinish &&
				       (!HasTeeSpace || IsHardBlockedForGoresDistanceField(Tile) || IsHardBlockedForGoresDistanceField(FrontTile));
		if(IsStart)
			m_GoresDistanceFieldBuildHadStart = true;
		if(IsFinish)
			m_vGoresDistanceFieldBuildFinishIndices.push_back(Index);

		m_vGoresDistanceFieldBuildPassable[(size_t)Index] = (!IsBlocked || IsStart || IsFinish) ? 1 : 0;
		if(IsBlocked)
			m_vGoresCMap[(size_t)Index] = GORES_CMAP_BLOCKED;
		else if(HasPenalty)
			m_vGoresCMap[(size_t)Index] = GORES_CMAP_PENALTY;
		else if(HasReward)
			m_vGoresCMap[(size_t)Index] = GORES_CMAP_REWARD;
		else if(HasTeleport)
			m_vGoresCMap[(size_t)Index] = GORES_CMAP_TELEPORT;

		if(pTele)
		{
			const int TeleType = pTele[Index].m_Type;
			const int TeleNumber = pTele[Index].m_Number;
			if(IsDirectTeleportInputTileForGoresDistanceField(TeleType) && TeleNumber > 0)
			{
				if((int)m_vvGoresDistanceFieldBuildDirectTeleInputs.size() < TeleNumber)
					m_vvGoresDistanceFieldBuildDirectTeleInputs.resize(TeleNumber);
				m_vvGoresDistanceFieldBuildDirectTeleInputs[(size_t)TeleNumber - 1].push_back(Index);
			}
			else if(TeleType == TILE_TELEOUT && TeleNumber > 0)
			{
				if((int)m_vvGoresDirectTeleOuts.size() < TeleNumber)
					m_vvGoresDirectTeleOuts.resize(TeleNumber);
				m_vvGoresDirectTeleOuts[(size_t)TeleNumber - 1].push_back(Index);
			}
		}
	}

	if(m_GoresDistanceFieldBuildCursor < MapSize)
		return;

	if(!m_GoresDistanceFieldBuildHadStart || m_vGoresDistanceFieldBuildFinishIndices.empty())
	{
		FailGoresDistanceFieldBuild();
		return;
	}

	m_GoresDistanceFieldBuildCursor = 0;
	m_GoresDistanceFieldBuildGroup = 0;
	m_GoresDistanceFieldBuildLayer = 0;
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::SCAN_VISUAL_LAYERS;
}

void CTClient::StepGoresDistanceFieldVisualLayers(int Budget)
{
	if(m_GoresDistanceFieldBuildMapSize <= 0)
	{
		FailGoresDistanceFieldBuild();
		return;
	}

	if(const CLayers *pLayers = Layers())
	{
		if(IMap *pMap = pLayers->Map())
		{
			if(m_vGoresDistanceFieldBuildImageSemantics.empty())
			{
				int ImageStart = 0;
				int ImageCount = 0;
				pMap->GetType(MAPITEMTYPE_IMAGE, &ImageStart, &ImageCount);
				m_vGoresDistanceFieldBuildImageSemantics.assign((size_t)maximum(ImageCount, 0), GORES_CMAP_NORMAL);
				for(int ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex)
				{
					const auto *pImage = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(ImageStart + ImageIndex));
					if(!pImage)
						continue;

					const char *pImageName = pMap->GetDataString(pImage->m_ImageName);
					m_vGoresDistanceFieldBuildImageSemantics[(size_t)ImageIndex] = GoresSemanticImageToCMapValue(pImageName);
					pMap->UnloadData(pImage->m_ImageName);
				}
			}

			int WorkLeft = maximum(0, Budget);
			for(; m_GoresDistanceFieldBuildGroup < pLayers->NumGroups(); ++m_GoresDistanceFieldBuildGroup)
			{
				const CMapItemGroup *pGroup = pLayers->GetGroup(m_GoresDistanceFieldBuildGroup);
				if(!pGroup)
					continue;

				for(; m_GoresDistanceFieldBuildLayer < pGroup->m_NumLayers; ++m_GoresDistanceFieldBuildLayer)
				{
					const CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + m_GoresDistanceFieldBuildLayer);
					if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
						continue;

					const auto *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
					if(pTilemap->m_Flags != 0 ||
						pTilemap->m_Width != m_GoresDistanceFieldWidth ||
						pTilemap->m_Height != m_GoresDistanceFieldHeight ||
						pTilemap->m_Image < 0 ||
						pTilemap->m_Image >= (int)m_vGoresDistanceFieldBuildImageSemantics.size())
						continue;

					const unsigned char SemanticValue = m_vGoresDistanceFieldBuildImageSemantics[(size_t)pTilemap->m_Image];
					if(SemanticValue != GORES_CMAP_PENALTY && SemanticValue != GORES_CMAP_REWARD)
						continue;

					if(pMap->GetDataSize(pTilemap->m_Data) < m_GoresDistanceFieldBuildMapSize * (int)sizeof(CTile))
						continue;

					if(m_GoresDistanceFieldBuildLoadedVisualLayerData != pTilemap->m_Data)
					{
						ReleaseGoresDistanceFieldVisualLayerData();
						m_GoresDistanceFieldBuildLoadedVisualLayerData = pTilemap->m_Data;
					}
					const CTile *pLayerTiles = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
					if(!pLayerTiles)
					{
						ReleaseGoresDistanceFieldVisualLayerData();
						continue;
					}

					const int End = std::min(m_GoresDistanceFieldBuildMapSize, m_GoresDistanceFieldBuildCursor + WorkLeft);
					for(int Index = m_GoresDistanceFieldBuildCursor; Index < End; ++Index)
					{
						if(pLayerTiles[Index].m_Index == TILE_AIR)
							continue;

						unsigned char &CellValue = m_vGoresCMap[(size_t)Index];
						if(CellValue == GORES_CMAP_BLOCKED || CellValue == GORES_CMAP_TELEPORT || CellValue == GORES_CMAP_PENALTY)
							continue;

						if(SemanticValue == GORES_CMAP_PENALTY)
							CellValue = GORES_CMAP_PENALTY;
						else if(CellValue == GORES_CMAP_NORMAL)
							CellValue = GORES_CMAP_REWARD;
					}

					WorkLeft -= End - m_GoresDistanceFieldBuildCursor;
					m_GoresDistanceFieldBuildCursor = End;
					if(m_GoresDistanceFieldBuildCursor < m_GoresDistanceFieldBuildMapSize)
						return;
					ReleaseGoresDistanceFieldVisualLayerData();
					m_GoresDistanceFieldBuildCursor = 0;
					if(WorkLeft <= 0)
					{
						++m_GoresDistanceFieldBuildLayer;
						return;
					}
				}
				m_GoresDistanceFieldBuildLayer = 0;
			}
		}
	}

	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	m_vGoresDistanceToFinish.assign((size_t)m_GoresDistanceFieldBuildMapSize, DISTANCE_INF);
	m_GoresDistanceFieldBuildCursor = 0;
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::INIT_QUEUE;
}

void CTClient::StepGoresDistanceFieldQueueInit(int Budget)
{
	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	const int Total = (int)m_vGoresDistanceFieldBuildFinishIndices.size();
	const int End = std::min(Total, m_GoresDistanceFieldBuildCursor + maximum(0, Budget));
	for(int Cursor = m_GoresDistanceFieldBuildCursor; Cursor < End; ++Cursor)
	{
		const int FinishIndex = m_vGoresDistanceFieldBuildFinishIndices[(size_t)Cursor];
		if(FinishIndex < 0 || FinishIndex >= m_GoresDistanceFieldBuildMapSize || !m_vGoresDistanceFieldBuildPassable[(size_t)FinishIndex] ||
			m_vGoresDistanceToFinish[(size_t)FinishIndex] != DISTANCE_INF)
			continue;
		m_vGoresDistanceToFinish[(size_t)FinishIndex] = 0;
		m_GoresDistanceFieldBuildQueue.emplace(0, FinishIndex);
	}
	m_GoresDistanceFieldBuildCursor = End;
	if(m_GoresDistanceFieldBuildCursor < Total)
		return;

	if(m_GoresDistanceFieldBuildQueue.empty())
	{
		FailGoresDistanceFieldBuild();
		return;
	}
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::DIJKSTRA;
}

void CTClient::StepGoresDistanceFieldDijkstra(int Budget)
{
	const CCollision *pCollision = Collision();
	if(!pCollision || !pCollision->GameLayer() || m_GoresDistanceFieldWidth <= 0)
	{
		FailGoresDistanceFieldBuild();
		return;
	}
	const CTile *pGame = pCollision->GameLayer();
	const CTile *pFront = pCollision->FrontLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();
	const int Width = m_GoresDistanceFieldWidth;
	const int Height = m_GoresDistanceFieldHeight;
	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();

	const auto TryRelax = [&](int Index, int NewDistance) {
		if(NewDistance < m_vGoresDistanceToFinish[(size_t)Index])
		{
			m_vGoresDistanceToFinish[(size_t)Index] = NewDistance;
			m_GoresDistanceFieldBuildQueue.emplace(NewDistance, Index);
		}
	};

	int WorkLeft = maximum(0, Budget);
	while(WorkLeft > 0)
	{
		if(m_GoresDistanceFieldBuildPendingTeleNumber > 0)
		{
			const int TeleIndex = m_GoresDistanceFieldBuildPendingTeleNumber - 1;
			if(TeleIndex < 0 || TeleIndex >= (int)m_vvGoresDistanceFieldBuildDirectTeleInputs.size())
			{
				m_GoresDistanceFieldBuildPendingTeleNumber = 0;
				m_GoresDistanceFieldBuildPendingTeleCursor = 0;
				continue;
			}
			const auto &vInputs = m_vvGoresDistanceFieldBuildDirectTeleInputs[(size_t)TeleIndex];
			while(WorkLeft > 0 && m_GoresDistanceFieldBuildPendingTeleCursor < (int)vInputs.size())
			{
				--WorkLeft;
				TryRelax(vInputs[(size_t)m_GoresDistanceFieldBuildPendingTeleCursor], m_GoresDistanceFieldBuildPendingTeleDistance);
				++m_GoresDistanceFieldBuildPendingTeleCursor;
			}
			if(m_GoresDistanceFieldBuildPendingTeleCursor < (int)vInputs.size())
				return;
			m_GoresDistanceFieldBuildPendingTeleNumber = 0;
			m_GoresDistanceFieldBuildPendingTeleCursor = 0;
			m_GoresDistanceFieldBuildPendingTeleDistance = 0;
			continue;
		}

		if(m_GoresDistanceFieldBuildQueue.empty())
			break;

		{
			--WorkLeft;
			const int CurDistance = m_GoresDistanceFieldBuildQueue.top().first;
			const int Cur = m_GoresDistanceFieldBuildQueue.top().second;
			m_GoresDistanceFieldBuildQueue.pop();
			if(CurDistance != m_vGoresDistanceToFinish[(size_t)Cur])
				continue;

			const int X = Cur % Width;
			const int Y = Cur / Width;
			const int CurGameTile = pGame[Cur].m_Index;
			const int CurFrontTile = pFront ? pFront[Cur].m_Index : TILE_AIR;
			const bool CurIsStart = CurGameTile == TILE_START || CurFrontTile == TILE_START;
			const bool CurIsFinish = CurGameTile == TILE_FINISH || CurFrontTile == TILE_FINISH;
			const int CurEnterCost = GoresDistanceFieldTraversalCost(m_vGoresCMap[(size_t)Cur], CurIsStart, CurIsFinish);
			const int aDx[4] = {1, -1, 0, 0};
			const int aDy[4] = {0, 0, 1, -1};

			for(int Dir = 0; Dir < 4; ++Dir)
			{
				const int PredX = X + aDx[Dir];
				const int PredY = Y + aDy[Dir];
				if(PredX < 0 || PredY < 0 || PredX >= Width || PredY >= Height)
					continue;

				const int PredIndex = PredY * Width + PredX;
				if(!m_vGoresDistanceFieldBuildPassable[(size_t)PredIndex])
					continue;
				if(!IsGoresDistanceFieldStepAllowed(pCollision, PredIndex, Cur, Width))
					continue;

				const int PredTeleType = pTele ? pTele[PredIndex].m_Type : 0;
				if(IsPlayerTeleportInputTileForGoresDistanceField(PredTeleType))
					continue;
				if(CurDistance <= DISTANCE_INF - CurEnterCost)
					TryRelax(PredIndex, CurDistance + CurEnterCost);
			}

			if(pTele && pTele[Cur].m_Type == TILE_TELEOUT)
			{
				const int TeleNumber = pTele[Cur].m_Number;
				if(TeleNumber > 0 && (int)m_vvGoresDistanceFieldBuildDirectTeleInputs.size() >= TeleNumber)
				{
					m_GoresDistanceFieldBuildPendingTeleNumber = TeleNumber;
					m_GoresDistanceFieldBuildPendingTeleCursor = 0;
					m_GoresDistanceFieldBuildPendingTeleDistance = CurDistance;
				}
			}
		}
	}

	if(!m_GoresDistanceFieldBuildQueue.empty() || m_GoresDistanceFieldBuildPendingTeleNumber > 0)
		return;
	m_GoresDistanceFieldBuildCursor = 0;
	m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::CHECK_REACHABLE_START;
}

void CTClient::StepGoresDistanceFieldReachableStartCheck(int Budget)
{
	const CCollision *pCollision = Collision();
	if(!pCollision || !pCollision->GameLayer())
	{
		FailGoresDistanceFieldBuild();
		return;
	}
	const CTile *pGame = pCollision->GameLayer();
	const CTile *pFront = pCollision->FrontLayer();
	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	const int End = std::min(m_GoresDistanceFieldBuildMapSize, m_GoresDistanceFieldBuildCursor + maximum(0, Budget));
	for(int Index = m_GoresDistanceFieldBuildCursor; Index < End; ++Index)
	{
		const int Tile = pGame[Index].m_Index;
		const int FrontTile = pFront ? pFront[Index].m_Index : TILE_AIR;
		const bool IsStart = Tile == TILE_START || FrontTile == TILE_START;
		if(IsStart && m_vGoresDistanceToFinish[(size_t)Index] != DISTANCE_INF)
		{
			CompleteGoresDistanceFieldBuild();
			return;
		}
	}

	m_GoresDistanceFieldBuildCursor = End;
	if(m_GoresDistanceFieldBuildCursor < m_GoresDistanceFieldBuildMapSize)
		return;

	FailGoresDistanceFieldBuild();
}

void CTClient::ApplyGoresFastInputLink(bool AutoMapCheck)
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_GoresModeStateKnown = false;
		m_PrevGoresModeActive = false;
		m_GoresAutoMapKnown = false;
		m_GoresAutoMapToken = 0;
		return;
	}

	bool FastInputConfigChanged = false;
	const unsigned GoresMapToken = str_quickhash(Client()->GetCurrentMap());
	const bool MapChanged = !m_GoresAutoMapKnown || m_GoresAutoMapToken != GoresMapToken;
	if(AutoMapCheck && MapChanged)
	{
		const bool GoresGameMode = IsGoresGameMode();
		if(g_Config.m_QmGoresAutoEnable != 0 && g_Config.m_QmGores != (GoresGameMode ? 1 : 0))
			g_Config.m_QmGores = GoresGameMode ? 1 : 0;
		m_GoresAutoMapKnown = true;
		m_GoresAutoMapToken = GoresMapToken;
	}

	const bool StateWasKnown = m_GoresModeStateKnown;
	if(!m_GoresModeStateKnown)
	{
		m_GoresModeStateKnown = true;
	}

	bool TcFastInputChanged = false;
	bool TcFastInputOthersChanged = false;
	const bool GoresActive = g_Config.m_QmGores != 0;
	const bool TcFastInput = ApplyQmGoresLinkedConfig(GoresActive, g_Config.m_QmGoresFastInput != 0, g_Config.m_TcFastInput != 0, TcFastInputChanged);
	const bool TcFastInputOthers = ApplyQmGoresLinkedConfig(GoresActive, g_Config.m_QmGoresFastInputOthers != 0, g_Config.m_TcFastInputOthers != 0, TcFastInputOthersChanged);
	if(TcFastInputChanged)
		g_Config.m_TcFastInput = TcFastInput ? 1 : 0;
	if(TcFastInputOthersChanged)
		g_Config.m_TcFastInputOthers = TcFastInputOthers ? 1 : 0;
	bool DummyHammerChanged = false;
	const int DummyHammer = ApplyQmGoresDummyHammerOverride(m_GoresDummyHammerOverride, GoresActive, g_Config.m_QmGoresDisableDummyHammer != 0, g_Config.m_ClDummyHammer, DummyHammerChanged);
	if(DummyHammerChanged)
		g_Config.m_ClDummyHammer = DummyHammer;
	if(!StateWasKnown)
		m_PrevGoresModeActive = GoresActive;
	if(StateWasKnown && GoresActive != m_PrevGoresModeActive)
	{
		char aGoresMsg[128];
		str_format(aGoresMsg, sizeof(aGoresMsg), "%s%s: %s",
			GoresActive ? "[[$FF7F7F]]" : "[[$A5FFA5]]",
			Localize("Gores Mode"),
			Localize(GoresActive ? "On" : "Off"));
		GameClient()->Echo(aGoresMsg);
	}

	FastInputConfigChanged = TcFastInputChanged || TcFastInputOthersChanged || DummyHammerChanged;

	m_PrevGoresModeActive = GoresActive;

	if(FastInputConfigChanged)
	{
		GameClient()->RequestPredictionRefresh();
	}
}

void CTClient::ResetGoresDummyHammerOverride()
{
	if(m_GoresDummyHammerOverride.m_WasActive && m_GoresDummyHammerOverride.m_AutoChangedValue && g_Config.m_ClDummyHammer == 0)
		g_Config.m_ClDummyHammer = m_GoresDummyHammerOverride.m_SavedValue;
	m_GoresDummyHammerOverride = {};
}

void CTClient::ApplyFocusModeEffects()
{
	const bool FocusActive = g_Config.m_QmFocusMode != 0;
	const auto ApplyFocusOverride = [](SQmConfigOverrideState &State, bool HideActive, int &ConfigValue, int HiddenValue) {
		bool Changed = false;
		const int NextValue = ApplyQmConfigOverride(State, HideActive, ConfigValue, HiddenValue, Changed);
		if(Changed)
			ConfigValue = NextValue;
	};
	const bool StateWasKnown = m_FocusModeStateKnown;
	const bool HideFocusHud = ShouldHideFocusHud(FocusActive, g_Config.m_QmFocusModeHideHud != 0);
	const bool HideFocusNameplates = ShouldHideFocusNameplates(FocusActive, g_Config.m_QmFocusModeHideNameplates != 0);
	const bool HideFocusDirectionIndicators = ShouldHideFocusDirectionIndicators(FocusActive, g_Config.m_QmFocusModeHideDirectionIndicators != 0);
	if(!m_FocusModeStateKnown)
	{
		m_FocusModeStateKnown = true;
		if(!FocusActive)
		{
			m_PrevFocusModeActive = false;
			return;
		}
		m_PrevFocusModeActive = false;
	}

	if(StateWasKnown && FocusActive != m_PrevFocusModeActive)
	{
		char aFocusMsg[128];
		str_format(aFocusMsg, sizeof(aFocusMsg), "%s%s: %s",
			FocusActive ? "[[$FF7F7F]]" : "[[$A5FFA5]]",
			Localize("Zen Mode"),
			Localize(FocusActive ? "On" : "Off"));
		GameClient()->Echo(aFocusMsg);
	}

	ApplyFocusOverride(m_FocusHudOverrideState, HideFocusHud, g_Config.m_ClShowhud, 0);
	// 昵称由六档范围 qm_nameplate_show_scope 统一决定，「无」即隐藏全部昵称。
	{
		int NamePlateShowScope = g_Config.m_QmNameplateShowScope;
		ApplyFocusOverride(m_FocusNamePlatesOverrideState, HideFocusNameplates, NamePlateShowScope, QM_NAMEPLATE_SHOW_SCOPE_OFF);
		ApplyFocusOverride(m_FocusNamePlatesOwnOverrideState, HideFocusNameplates, NamePlateShowScope, QM_NAMEPLATE_SHOW_SCOPE_OFF);
		g_Config.m_QmNameplateShowScope = NamePlateShowScope;
	}
	ApplyFocusOverride(m_FocusNameplateCoordsOverrideState, HideFocusNameplates, g_Config.m_QmNameplateCoords, 0);
	ApplyFocusOverride(m_FocusNameplateCoordsOwnOverrideState, HideFocusNameplates, g_Config.m_QmNameplateCoordsOwn, 0);
	ApplyFocusOverride(m_FocusNameplateCoordXOverrideState, HideFocusNameplates, g_Config.m_QmNameplateCoordX, 0);
	ApplyFocusOverride(m_FocusNameplateCoordYOverrideState, HideFocusNameplates, g_Config.m_QmNameplateCoordY, 0);
	ApplyFocusOverride(m_FocusDirectionOverrideState, HideFocusDirectionIndicators, g_Config.m_ClShowDirection, 0);
	ApplyFocusOverride(m_FocusVideoHudOverrideState, HideFocusHud, g_Config.m_ClVideoShowhud, 0);
	ApplyFocusOverride(m_FocusVideoDirectionOverrideState, HideFocusDirectionIndicators, g_Config.m_ClVideoShowDirection, 0);
	m_PrevFocusModeActive = FocusActive;
}

bool CTClient::BuildGoresDebugRoute(std::vector<vec2> &vRoutePoints, int Dummy) const
{
	vRoutePoints.clear();

	const CCollision *pCollision = Collision();
	if(IsDDraceMapProgressMap())
	{
		if(!pCollision)
			return false;
		std::vector<int> vIndices;
		if(!m_aQmDDraceProgress[std::clamp(Dummy, 0, NUM_DUMMIES - 1)].BuildRoute(vIndices))
			return false;
		for(const int Index : vIndices)
			vRoutePoints.push_back(pCollision->GetPos(Index));
		return !vRoutePoints.empty();
	}
	if(!pCollision || !m_GoresDistanceFieldValid)
		return false;

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	const int64_t MapCellCount64 = (int64_t)Width * Height;
	if(MapCellCount64 <= 0 || MapCellCount64 > std::numeric_limits<int>::max())
		return false;
	const int MapCellCount = (int)MapCellCount64;
	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	if(MapCellCount <= 0 ||
		m_GoresDistanceFieldWidth != Width ||
		m_GoresDistanceFieldHeight != Height ||
		m_vGoresCMap.size() != (size_t)MapCellCount ||
		m_vGoresDistanceToFinish.size() != (size_t)MapCellCount)
		return false;

	const int DummyIndex = Dummy < 0 ? 0 : (Dummy >= NUM_DUMMIES ? NUM_DUMMIES - 1 : Dummy);
	if(DummyIndex == 1 && !Client()->DummyConnected())
		return false;

	const int ClientId = GameClient()->m_aLocalIds[DummyIndex];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(!Char.m_Active)
		return false;

	const CTile *pGame = pCollision->GameLayer();
	if(!pGame)
		return false;
	const CTile *pFront = pCollision->FrontLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();

	const vec2 RefPos = GameClient()->m_aClients[ClientId].m_RenderPos;
	const auto IsReachableIndex = [&](int Index) {
		return Index >= 0 && Index < MapCellCount &&
		       m_vGoresCMap[(size_t)Index] != GORES_CMAP_BLOCKED &&
		       m_vGoresDistanceToFinish[(size_t)Index] != DISTANCE_INF;
	};

	int StartIndex = pCollision->GetPureMapIndex(RefPos);
	if(!IsReachableIndex(StartIndex))
	{
		StartIndex = m_GoresRouteStartIndex.FindClosest(RefPos, StartIndex, [&](int Index) { return IsReachableIndex(Index) && (pGame[Index].m_Index == TILE_START || (pFront && pFront[Index].m_Index == TILE_START)); }, [&](int Index) { return pCollision->GetPos(Index); });
	}

	if(!IsReachableIndex(StartIndex))
		return false;

	m_GoresDebugRouteVisited.Begin((size_t)MapCellCount);
	vRoutePoints.reserve(256);
	int CurrentIndex = StartIndex;
	for(int Guard = 0; Guard < MapCellCount + 64; ++Guard)
	{
		if(!IsReachableIndex(CurrentIndex) || !m_GoresDebugRouteVisited.Visit((size_t)CurrentIndex))
			break;

		vRoutePoints.push_back(pCollision->GetPos(CurrentIndex));

		const int CurrentDistance = m_vGoresDistanceToFinish[(size_t)CurrentIndex];
		if(CurrentDistance == 0)
			return !vRoutePoints.empty();

		int BestNextIndex = -1;
		int BestNextDistance = DISTANCE_INF;
		const auto ConsiderNext = [&](int NextIndex, int ExpectedDistance) {
			if(!IsReachableIndex(NextIndex) || ExpectedDistance != CurrentDistance)
				return;

			const int NextDistance = m_vGoresDistanceToFinish[(size_t)NextIndex];
			if(BestNextIndex < 0 || NextDistance < BestNextDistance)
			{
				BestNextIndex = NextIndex;
				BestNextDistance = NextDistance;
			}
		};

		if(pTele && IsDirectTeleportInputTileForGoresDistanceField(pTele[CurrentIndex].m_Type))
		{
			const int TeleNumber = pTele[CurrentIndex].m_Number;
			if(TeleNumber > 0 && (int)m_vvGoresDirectTeleOuts.size() >= TeleNumber)
			{
				for(const int TeleOutIndex : m_vvGoresDirectTeleOuts[(size_t)TeleNumber - 1])
					ConsiderNext(TeleOutIndex, m_vGoresDistanceToFinish[(size_t)TeleOutIndex]);
			}
		}

		const int X = CurrentIndex % Width;
		const int Y = CurrentIndex / Width;
		const int aDx[4] = {1, -1, 0, 0};
		const int aDy[4] = {0, 0, 1, -1};
		for(int Dir = 0; Dir < 4; ++Dir)
		{
			const int NextX = X + aDx[Dir];
			const int NextY = Y + aDy[Dir];
			if(NextX < 0 || NextY < 0 || NextX >= Width || NextY >= Height)
				continue;

			const int NextIndex = NextY * Width + NextX;
			if(!IsReachableIndex(NextIndex))
				continue;
			if(!IsGoresDistanceFieldStepAllowed(pCollision, CurrentIndex, NextIndex, Width))
				continue;

			const int NextTile = pGame[NextIndex].m_Index;
			const int NextFrontTile = pFront ? pFront[NextIndex].m_Index : TILE_AIR;
			const bool NextIsStart = NextTile == TILE_START || NextFrontTile == TILE_START;
			const bool NextIsFinish = NextTile == TILE_FINISH || NextFrontTile == TILE_FINISH;
			const int StepCost = GoresDistanceFieldTraversalCost(m_vGoresCMap[(size_t)NextIndex], NextIsStart, NextIsFinish);
			if(m_vGoresDistanceToFinish[(size_t)NextIndex] <= DISTANCE_INF - StepCost)
				ConsiderNext(NextIndex, m_vGoresDistanceToFinish[(size_t)NextIndex] + StepCost);
		}

		if(BestNextIndex < 0)
			break;
		CurrentIndex = BestNextIndex;
	}

	return !vRoutePoints.empty();
}

void CTClient::RenderGoresDebugRoute()
{
	if(!ShouldRenderGoresDebugRoute(Client()->State() == IClient::STATE_ONLINE, g_Config.m_QmPlayerStatsMapProgressDbgRoute != 0, IsGoresMapProgressEnabled()))
		return;

	EnsureGoresDistanceField();
	std::vector<vec2> vRoutePoints;
	if(!BuildGoresDebugRoute(vRoutePoints, g_Config.m_ClDummy) || vRoutePoints.empty())
		return;

	float ScreenX0 = 0.0f;
	float ScreenY0 = 0.0f;
	float ScreenX1 = 0.0f;
	float ScreenY1 = 0.0f;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	constexpr float DotSize = 6.0f;
	constexpr float StartDotSize = 8.0f;
	const float Margin = 48.0f;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.15f, 1.0f, 0.45f, 0.95f);
	bool HasStartDot = false;
	IGraphics::CQuadItem StartDot(0.0f, 0.0f, StartDotSize, StartDotSize);
	std::vector<IGraphics::CQuadItem> vRouteDots;
	vRouteDots.reserve(512);

	for(size_t i = 0; i < vRoutePoints.size(); ++i)
	{
		const vec2 &Pos = vRoutePoints[i];
		if(Pos.x < ScreenX0 - Margin || Pos.x > ScreenX1 + Margin ||
			Pos.y < ScreenY0 - Margin || Pos.y > ScreenY1 + Margin)
			continue;

		if(i == 0)
		{
			StartDot = IGraphics::CQuadItem(Pos.x, Pos.y, StartDotSize, StartDotSize);
			HasStartDot = true;
			continue;
		}

		vRouteDots.emplace_back(Pos.x, Pos.y, DotSize, DotSize);
		if(vRouteDots.size() >= 512)
		{
			if(HasStartDot)
			{
				Graphics()->SetColor(0.15f, 1.0f, 0.45f, 0.95f);
				Graphics()->QuadsDraw(&StartDot, 1);
				HasStartDot = false;
			}
			Graphics()->SetColor(0.15f, 1.0f, 0.45f, 0.72f);
			Graphics()->QuadsDraw(vRouteDots.data(), (int)vRouteDots.size());
			vRouteDots.clear();
		}
	}

	if(HasStartDot)
	{
		Graphics()->SetColor(0.15f, 1.0f, 0.45f, 0.95f);
		Graphics()->QuadsDraw(&StartDot, 1);
	}
	if(!vRouteDots.empty())
	{
		Graphics()->SetColor(0.15f, 1.0f, 0.45f, 0.72f);
		Graphics()->QuadsDraw(vRouteDots.data(), (int)vRouteDots.size());
	}
	Graphics()->QuadsEnd();
}

void CTClient::ResetDDraceMapProgress()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aQmDDraceProgress[Dummy].Reset();
		m_aQmDDraceProgressClientId[Dummy] = -1;
		m_aQmDDraceTeleCheckpoint[Dummy] = 0;
		m_aQmDDraceProgressHasPreviousPos[Dummy] = false;
	}
	m_QmDDraceProgressMap = {};
	m_pQmDDraceProgressGame = nullptr;
	m_pQmDDraceProgressFront = nullptr;
	m_pQmDDraceProgressTele = nullptr;
	m_QmDDraceProgressWidth = 0;
	m_QmDDraceProgressScanCursor = 0;
}

void CTClient::UpdateDDraceMapProgress()
{
	for(bool &Valid : m_aGoresMapProgressValid)
		Valid = false;
	const CCollision *pCollision = Collision();
	if(!pCollision || !pCollision->GameLayer())
		return;
	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	const int64_t Size = (int64_t)Width * Height;
	if(Width <= 0 || Height <= 0 || Size > std::numeric_limits<int>::max())
		return;
	const CTile *pGame = pCollision->GameLayer();
	const CTile *pFront = pCollision->FrontLayer();
	const CTeleTile *pTele = pCollision->TeleLayer();
	const char *pMapName = Client()->GetCurrentMap();
	if(str_comp(m_aGoresDistanceFieldMap, pMapName ? pMapName : "") != 0 ||
		m_pQmDDraceProgressGame != pGame || m_pQmDDraceProgressFront != pFront || m_pQmDDraceProgressTele != pTele ||
		m_QmDDraceProgressWidth != Width || m_QmDDraceProgressMap.Size() != Size)
	{
		InvalidateGoresDistanceField();
		str_copy(m_aGoresDistanceFieldMap, pMapName ? pMapName : "");
		m_QmDDraceProgressMap = QmMapProgress::CMap(Width, Height);
		m_pQmDDraceProgressGame = pGame;
		m_pQmDDraceProgressFront = pFront;
		m_pQmDDraceProgressTele = pTele;
		m_QmDDraceProgressWidth = Width;
	}
	const auto ReadTile = [&](int Index) {
		return QmMapProgress::MakeTile(pGame[Index].m_Index, pFront ? pFront[Index].m_Index : TILE_AIR,
			pTele ? pTele[Index].m_Type : 0, pTele ? pTele[Index].m_Number : 0);
	};
	if(m_QmDDraceProgressScanCursor < Size)
	{
		const int End = (int)std::min<int64_t>(Size, (int64_t)m_QmDDraceProgressScanCursor + GORES_DISTANCE_FIELD_TILE_SCAN_BUDGET);
		for(; m_QmDDraceProgressScanCursor < End; ++m_QmDDraceProgressScanCursor)
		{
			const int Index = m_QmDDraceProgressScanCursor;
			auto Tile = ReadTile(Index);
			Tile.m_Restrictions = pCollision->GetMoveRestrictions(nullptr, nullptr, pCollision->GetPos(Index), 18.0f, Index);
			m_QmDDraceProgressMap.SetTile(Index, Tile);
		}
	}
	else if(!m_QmDDraceProgressMap.Finalized())
		m_QmDDraceProgressMap.FinalizeStep(GORES_DISTANCE_FIELD_TILE_SCAN_BUDGET);

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		auto &Progress = m_aQmDDraceProgress[Dummy];
		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if((Dummy == 1 && !Client()->DummyConnected()) || ClientId < 0 || ClientId >= MAX_CLIENTS ||
			!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			if(m_aQmDDraceProgressClientId[Dummy] >= 0)
				Progress.Reset();
			m_aQmDDraceProgressClientId[Dummy] = -1;
			m_aQmDDraceProgressHasPreviousPos[Dummy] = false;
			m_aQmDDraceTeleCheckpoint[Dummy] = 0;
			continue;
		}
		if(m_aQmDDraceProgressClientId[Dummy] != ClientId)
		{
			Progress.Reset();
			m_aQmDDraceProgressHasPreviousPos[Dummy] = false;
			m_aQmDDraceTeleCheckpoint[Dummy] = 0;
			m_aQmDDraceProgressClientId[Dummy] = ClientId;
		}
		const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
		const vec2 Pos((float)Character.m_Cur.m_X, (float)Character.m_Cur.m_Y);
		const int Index = pCollision->GetPureMapIndex(Pos);
		if(Index < 0 || Index >= Size)
			continue;
		const auto Observe = [&](int TileIndex) {
			Progress.Observe(ReadTile(TileIndex), TileIndex);
			if(!Character.m_HasExtendedData && pTele && pTele[TileIndex].m_Type == TILE_TELECHECK)
				m_aQmDDraceTeleCheckpoint[Dummy] = pTele[TileIndex].m_Number;
		};
		if(m_aQmDDraceProgressHasPreviousPos[Dummy])
		{
			const vec2 Previous = m_aQmDDraceProgressPreviousPos[Dummy];
			const int PreviousIndex = pCollision->GetPureMapIndex(Previous);
			const bool TeleportEndpoints = pTele &&
						       (IsPlayerTeleportInputTileForGoresDistanceField(pTele[PreviousIndex].m_Type) ||
							       pTele[Index].m_Type == TILE_TELEOUT || pTele[Index].m_Type == TILE_TELECHECKOUT);
			// 补读正常移动跨过的薄 CP；大跨度移动和传送不沿两点之间虚构触碰。
			if(!TeleportEndpoints && length_squared(Pos - Previous) <= 128.0f * 128.0f && Pos != Previous)
			{
				for(const int CrossedIndex : pCollision->GetMapIndices(Previous, Pos, 8))
					Observe(CrossedIndex);
			}
		}
		Observe(Index);
		if(Character.m_HasExtendedData)
			m_aQmDDraceTeleCheckpoint[Dummy] = Character.m_ExtendedData.m_TeleCheckpoint;
		m_aQmDDraceProgressPreviousPos[Dummy] = Pos;
		m_aQmDDraceProgressHasPreviousPos[Dummy] = true;
		Progress.Update(m_QmDDraceProgressMap, Index, m_aQmDDraceTeleCheckpoint[Dummy], GORES_DISTANCE_FIELD_DIJKSTRA_BUDGET);
		m_aGoresMapProgressValid[Dummy] = Progress.Estimate().m_Valid;
		m_aGoresMapProgress[Dummy] = Progress.Estimate().m_Progress;
	}
}

void CTClient::UpdateGoresMapProgress()
{
	const auto ResetAllProgressState = [this]() {
		for(int i = 0; i < NUM_DUMMIES; ++i)
		{
			m_aGoresWasOnStartLastTick[i] = false;
			m_aGoresRunStarted[i] = false;
			m_aGoresRunStartDistanceToFinish[i] = 0;
			m_aGoresMapProgressValid[i] = false;
			m_aGoresMapProgress[i] = 0.0f;
		}
	};

	if(Client()->State() != IClient::STATE_ONLINE || !IsGoresMapProgressEnabled())
	{
		ResetAllProgressState();
		if(m_QmDDraceProgressMap.Size() > 0)
			ResetDDraceMapProgress();
		return;
	}
	if(IsDDraceMapProgressMap())
	{
		UpdateDDraceMapProgress();
		return;
	}

	const CCollision *pCollision = Collision();
	if(!pCollision)
	{
		ResetAllProgressState();
		return;
	}

	EnsureGoresDistanceField();
	const int MapCellCount = pCollision->GetWidth() * pCollision->GetHeight();
	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	if(!m_GoresDistanceFieldValid || MapCellCount <= 0 ||
		m_GoresDistanceFieldWidth != pCollision->GetWidth() ||
		m_GoresDistanceFieldHeight != pCollision->GetHeight() ||
		m_vGoresCMap.size() != (size_t)MapCellCount ||
		m_vGoresDistanceToFinish.size() != (size_t)MapCellCount)
	{
		ResetAllProgressState();
		return;
	}

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aGoresMapProgressValid[Dummy] = false;

		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;

		const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
		if(!Char.m_Active)
			continue;

		const vec2 Pos = vec2((float)Char.m_Cur.m_X, (float)Char.m_Cur.m_Y);
		const int PosIndex = pCollision->GetPureMapIndex(Pos);
		if(PosIndex < 0 || PosIndex >= MapCellCount)
			continue;

		const int Tile = pCollision->GetTileIndex(PosIndex);
		const int FrontTile = pCollision->GetFrontTileIndex(PosIndex);
		const bool IsOnStart = Tile == TILE_START || FrontTile == TILE_START;
		if(IsOnStart)
		{
			m_aGoresWasOnStartLastTick[Dummy] = true;
			m_aGoresRunStarted[Dummy] = false;
			m_aGoresRunStartDistanceToFinish[Dummy] = 0;
			m_aGoresMapProgress[Dummy] = 0.0f;
			m_aGoresMapProgressValid[Dummy] = true;
			continue;
		}

		const int CurrentDistanceToFinish = m_vGoresDistanceToFinish[(size_t)PosIndex];
		if(CurrentDistanceToFinish == DISTANCE_INF)
			continue;

		if(!m_aGoresRunStarted[Dummy])
		{
			if(!m_aGoresWasOnStartLastTick[Dummy])
			{
				m_aGoresMapProgressValid[Dummy] = false;
				continue;
			}
			m_aGoresRunStarted[Dummy] = true;
			m_aGoresRunStartDistanceToFinish[Dummy] = CurrentDistanceToFinish;
		}
		m_aGoresWasOnStartLastTick[Dummy] = false;

		const int StartDistanceToFinish = maximum(0, m_aGoresRunStartDistanceToFinish[Dummy]);
		if(StartDistanceToFinish <= 0)
			m_aGoresMapProgress[Dummy] = 1.0f;
		else
			m_aGoresMapProgress[Dummy] = std::clamp((StartDistanceToFinish - CurrentDistanceToFinish) / (float)StartDistanceToFinish, 0.0f, 1.0f);
		m_aGoresMapProgressValid[Dummy] = true;
	}
}

// ========== 收藏地图功能实现 ==========

bool CTClient::IsFavoriteMap(const char *pMapName) const
{
	if(!pMapName || pMapName[0] == '\0')
		return false;
	return m_FavoriteMaps.contains(std::string(pMapName));
}

void CTClient::AddFavoriteMap(const char *pMapName)
{
	if(!pMapName || pMapName[0] == '\0')
		return;
	m_FavoriteMaps.insert(std::string(pMapName));
	log_info("qmclient", "Added favorite map: %s", pMapName);
}

void CTClient::RemoveFavoriteMap(const char *pMapName)
{
	if(!pMapName || pMapName[0] == '\0')
		return;
	m_FavoriteMaps.erase(std::string(pMapName));
	log_info("qmclient", "Removed favorite map: %s", pMapName);
}

void CTClient::ClearFavoriteMaps()
{
	m_FavoriteMaps.clear();
	log_info("qmclient", "Cleared all favorite maps");
}

void CTClient::ConAddFavoriteMap(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->AddFavoriteMap(pResult->GetString(0));
}

void CTClient::ConRemoveFavoriteMap(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->RemoveFavoriteMap(pResult->GetString(0));
}

void CTClient::ConClearFavoriteMaps(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	pThis->ClearFavoriteMaps();
}

void CTClient::ConfigSaveFavoriteMaps(IConfigManager *pConfigManager, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	char aBuf[256];
	for(const auto &Map : pThis->m_FavoriteMaps)
	{
		str_format(aBuf, sizeof(aBuf), "add_favorite_map \"%s\"", Map.c_str());
		pConfigManager->WriteLine(aBuf);
	}
}

// ==================== Map category cache ====================

void CTClient::LoadMapCategoryCache()
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(MAP_CATEGORY_CACHE_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pJson = JsonParseEx(&JsonSettings, static_cast<json_char *>(pFileData), FileSize, aError);
	free(pFileData);

	if(pJson == nullptr)
	{
		log_error("qmclient", "map category cache json parse error: %s", aError);
		return;
	}

	if(pJson->type != json_object)
	{
		json_value_free(pJson);
		return;
	}

	const json_value *pMaps = json_object_get(pJson, "maps");
	const json_value *pMapObject = (pMaps && pMaps->type == json_object) ? pMaps : pJson;
	if(pMapObject->type == json_object)
	{
		m_MapCategoryCache.clear();
		for(unsigned i = 0; i < pMapObject->u.object.length; ++i)
		{
			const char *pMapName = pMapObject->u.object.values[i].name;
			const json_value *pCategoryValue = pMapObject->u.object.values[i].value;
			if(!pMapName || !pCategoryValue || pCategoryValue->type != json_string)
				continue;
			if(pMapObject == pJson && str_comp(pMapName, "version") == 0)
				continue;
			const char *pCategoryKey = json_string_get(pCategoryValue);
			if(!pCategoryKey || pCategoryKey[0] == '\0')
				continue;
			m_MapCategoryCache.emplace(pMapName, pCategoryKey);
		}
	}

	json_value_free(pJson);
	m_MapCategoryCacheDirty = false;
	m_MapCategoryCacheNextSave = 0;
}

void CTClient::SaveMapCategoryCache()
{
	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);

	IOHANDLE File = Storage()->OpenFile(MAP_CATEGORY_CACHE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("qmclient", "map category cache file open failed");
		m_MapCategoryCacheNextSave = time_get() + time_freq() * MAP_CATEGORY_CACHE_SAVE_DELAY_SEC;
		return;
	}

	CJsonFileWriter Writer(File);
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("maps");
	Writer.BeginObject();
	for(const auto &Entry : m_MapCategoryCache)
	{
		if(Entry.first.empty() || Entry.second.empty())
			continue;
		Writer.WriteAttribute(Entry.first.c_str());
		Writer.WriteStrValue(Entry.second.c_str());
	}
	Writer.EndObject();
	Writer.EndObject();

	m_MapCategoryCacheDirty = false;
	m_MapCategoryCacheNextSave = 0;
}

void CTClient::MaybeSaveMapCategoryCache()
{
	if(!m_MapCategoryCacheDirty)
		return;

	// Avoid synchronous disk writes during active gameplay frames.
	if(Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive())
		return;

	if(m_MapCategoryCacheNextSave == 0)
		m_MapCategoryCacheNextSave = time_get() + time_freq() * MAP_CATEGORY_CACHE_SAVE_DELAY_SEC;
	if(time_get() >= m_MapCategoryCacheNextSave)
		SaveMapCategoryCache();
}

const char *CTClient::GetCachedMapCategoryKey(const char *pMapName) const
{
	if(!pMapName || pMapName[0] == '\0')
		return nullptr;
	const auto It = m_MapCategoryCache.find(pMapName);
	if(It == m_MapCategoryCache.end() || It->second.empty())
		return nullptr;
	return It->second.c_str();
}

void CTClient::UpdateMapCategoryCache(const char *pMapName, const char *pCategoryKey)
{
	if(!pMapName || pMapName[0] == '\0' || !pCategoryKey || pCategoryKey[0] == '\0')
		return;

	bool Updated = false;
	auto It = m_MapCategoryCache.find(pMapName);
	if(It == m_MapCategoryCache.end())
	{
		m_MapCategoryCache.emplace(pMapName, pCategoryKey);
		Updated = true;
	}
	else if(It->second != pCategoryKey)
	{
		It->second = pCategoryKey;
		Updated = true;
	}

	if(Updated && !m_MapCategoryCacheDirty)
	{
		m_MapCategoryCacheDirty = true;
		m_MapCategoryCacheNextSave = time_get() + time_freq() * MAP_CATEGORY_CACHE_SAVE_DELAY_SEC;
	}
}

void CTClient::LoadMapNotes()
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(MAP_NOTES_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pJson = JsonParseEx(&JsonSettings, static_cast<json_char *>(pFileData), FileSize, aError);
	free(pFileData);

	if(pJson == nullptr)
	{
		log_error("qmclient", "map notes json parse error: %s", aError);
		return;
	}

	if(pJson->type != json_object)
	{
		json_value_free(pJson);
		return;
	}

	const json_value *pNotes = json_object_get(pJson, "notes");
	const json_value *pNotesObject = (pNotes && pNotes->type == json_object) ? pNotes : pJson;
	if(pNotesObject->type == json_object)
	{
		m_MapNotes.clear();
		for(unsigned i = 0; i < pNotesObject->u.object.length; ++i)
		{
			const char *pMapName = pNotesObject->u.object.values[i].name;
			const json_value *pNoteValue = pNotesObject->u.object.values[i].value;
			if(!pMapName || !pNoteValue || pNoteValue->type != json_string)
				continue;
			if(pNotesObject == pJson && str_comp(pMapName, "version") == 0)
				continue;
			const char *pNote = json_string_get(pNoteValue);
			if(!pNote || pNote[0] == '\0')
				continue;
			m_MapNotes.emplace(pMapName, pNote);
		}
	}

	json_value_free(pJson);
	m_MapNotesDirty = false;
	m_MapNotesNextSave = 0;
}

void CTClient::SaveMapNotes()
{
	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);

	IOHANDLE File = Storage()->OpenFile(MAP_NOTES_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("qmclient", "map notes file open failed");
		m_MapNotesNextSave = time_get() + time_freq() * MAP_NOTES_SAVE_DELAY_SEC;
		return;
	}

	CJsonFileWriter Writer(File);
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("notes");
	Writer.BeginObject();
	for(const auto &Entry : m_MapNotes)
	{
		if(Entry.first.empty() || Entry.second.empty())
			continue;
		Writer.WriteAttribute(Entry.first.c_str());
		Writer.WriteStrValue(Entry.second.c_str());
	}
	Writer.EndObject();
	Writer.EndObject();

	m_MapNotesDirty = false;
	m_MapNotesNextSave = 0;
}

void CTClient::MaybeSaveMapNotes()
{
	if(!m_MapNotesDirty)
		return;

	// Avoid synchronous disk writes during active gameplay frames.
	if(Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive())
		return;

	if(m_MapNotesNextSave == 0)
		m_MapNotesNextSave = time_get() + time_freq() * MAP_NOTES_SAVE_DELAY_SEC;
	if(time_get() >= m_MapNotesNextSave)
		SaveMapNotes();
}

const char *CTClient::GetMapNote(const char *pMapName) const
{
	if(!pMapName || pMapName[0] == '\0')
		return nullptr;
	const auto It = m_MapNotes.find(pMapName);
	if(It == m_MapNotes.end() || It->second.empty())
		return nullptr;
	return It->second.c_str();
}

void CTClient::SetMapNote(const char *pMapName, const char *pNote)
{
	if(!pMapName || pMapName[0] == '\0')
		return;

	bool Updated = false;
	const bool HasNote = pNote && pNote[0] != '\0';
	auto It = m_MapNotes.find(pMapName);
	if(!HasNote)
	{
		if(It != m_MapNotes.end())
		{
			m_MapNotes.erase(It);
			Updated = true;
		}
	}
	else if(It == m_MapNotes.end())
	{
		m_MapNotes.emplace(pMapName, pNote);
		Updated = true;
	}
	else if(It->second != pNote)
	{
		It->second = pNote;
		Updated = true;
	}

	if(Updated && !m_MapNotesDirty)
	{
		m_MapNotesDirty = true;
		m_MapNotesNextSave = time_get() + time_freq() * MAP_NOTES_SAVE_DELAY_SEC;
	}
}

void CTClient::LoadMapHistory()
{
	char *pJson = Storage()->ReadFileStr(MAP_HISTORY_FILE, IStorage::TYPE_SAVE);
	if(pJson == nullptr)
		return;

	char aError[256] = "";
	if(!m_MapHistory.FromJson(pJson, aError, sizeof(aError)))
	{
		log_error("qmclient", "map history json parse error: %s", aError);
	}
	free(pJson);

	const size_t OldSize = m_MapHistory.Size();
	m_MapHistory.ApplyLimit(g_Config.m_QmAutoSaveHistoryCount);
	m_MapHistoryDirty = m_MapHistory.Size() != OldSize;
}

void CTClient::SaveMapHistory()
{
	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);

	char aTempFilename[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTempFilename, sizeof(aTempFilename), MAP_HISTORY_FILE);
	IOHANDLE File = Storage()->OpenFile(aTempFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("qmclient", "map history temp file open failed");
		return;
	}

	const std::string Json = m_MapHistory.ToJson();
	const bool Written = io_write(File, Json.data(), (unsigned)Json.size()) == Json.size();
	io_close(File);
	if(!Written)
	{
		Storage()->RemoveFile(aTempFilename, IStorage::TYPE_SAVE);
		log_error("qmclient", "map history temp file write failed");
		return;
	}

	char aBackupFilename[2 * IO_MAX_PATH_LENGTH];
	if(!IStorage::ReplaceFileSafely(Storage(), aTempFilename, MAP_HISTORY_FILE, aBackupFilename, sizeof(aBackupFilename)))
	{
		log_error("qmclient", "map history replacement failed; previous history was restored or remains at %s", aBackupFilename[0] != '\0' ? aBackupFilename : MAP_HISTORY_FILE);
		return;
	}
	m_MapHistoryDirty = false;
}
void CTClient::MarkMapHistoryDirty()
{
	const size_t OldSize = m_MapHistory.Size();
	m_MapHistory.ApplyLimit(g_Config.m_QmAutoSaveHistoryCount);
	m_MapHistoryDirty = true;
	if(m_MapHistory.Size() != OldSize)
		log_info("qmclient", "Trimmed map history to %d entries", g_Config.m_QmAutoSaveHistoryCount);
}

std::string CTClient::CurrentMapHistoryId() const
{
	const char *pMapName = Client()->GetCurrentMap();
	if(pMapName == nullptr || pMapName[0] == '\0')
		return {};

	const char *pMapPath = Client()->GetCurrentMapPath();
	if(pMapPath != nullptr && pMapPath[0] != '\0')
		return std::string("path:") + pMapPath;
	return std::string("name:") + pMapName;
}

int64_t CTClient::CurrentMapHistoryPlayTimeMs() const
{
	if(!m_MapHistorySessionActive || m_MapHistorySessionStart <= 0)
		return 0;
	const int64_t Elapsed = time_get() - m_MapHistorySessionStart;
	if(Elapsed <= 0)
		return 0;
	return Elapsed * 1000 / time_freq();
}

void CTClient::TouchMapHistoryPlayTime()
{
	if(!m_MapHistorySessionActive || m_MapHistoryActiveMapId.empty())
		return;
	m_MapHistory.UpdatePlayTime(m_MapHistoryActiveMapId, CurrentMapHistoryPlayTimeMs());
}

void CTClient::StartMapHistorySession()
{
	if(g_Config.m_QmAutoSaveHistoryCount <= 0 || Client()->State() != IClient::STATE_ONLINE)
		return;

	const char *pMapName = Client()->GetCurrentMap();
	if(pMapName == nullptr || pMapName[0] == '\0')
		return;

	const std::string MapId = CurrentMapHistoryId();
	if(MapId.empty())
		return;
	if(m_MapHistorySuppressedMapId == MapId)
		return;

	if(m_MapHistorySessionActive && m_MapHistoryActiveMapId == MapId)
	{
		TouchMapHistoryPlayTime();
		return;
	}

	EndMapHistorySession(true);

	char aDate[32];
	str_timestamp_format(aDate, sizeof(aDate), "%Y-%m-%d");
	m_MapHistory.RecordVisit(pMapName, MapId, time_timestamp(), aDate);
	m_MapHistorySessionActive = true;
	m_MapHistorySessionStart = time_get();
	m_MapHistoryActiveMapId = MapId;
	m_MapHistoryActiveMapName = pMapName;
	MarkMapHistoryDirty();
}

void CTClient::EndMapHistorySession(bool SaveNow)
{
	if(!m_MapHistorySessionActive)
		return;
	TouchMapHistoryPlayTime();
	m_MapHistorySessionActive = false;
	m_MapHistorySessionStart = 0;
	m_MapHistoryActiveMapId.clear();
	m_MapHistoryActiveMapName.clear();
	MarkMapHistoryDirty();
	if(SaveNow)
		SaveMapHistory();
}

void CTClient::UpdateMapHistorySession()
{
	if(g_Config.m_QmAutoSaveHistoryCount <= 0 || Client()->State() != IClient::STATE_ONLINE)
	{
		EndMapHistorySession(true);
		return;
	}

	const std::string MapId = CurrentMapHistoryId();
	if(MapId.empty())
	{
		EndMapHistorySession(true);
		return;
	}
	if(!m_MapHistorySuppressedMapId.empty() && m_MapHistorySuppressedMapId != MapId)
		m_MapHistorySuppressedMapId.clear();
	if(m_MapHistorySuppressedMapId == MapId)
		return;

	if(m_MapHistorySessionActive && m_MapHistoryActiveMapId != MapId)
		EndMapHistorySession(true);
	StartMapHistorySession();
}

void CTClient::HandleMapHistoryDeath(int ClientId)
{
	if(g_Config.m_QmAutoSaveHistoryCount <= 0 || !m_MapHistorySessionActive || ClientId < 0)
		return;

	const bool OwnMain = ClientId == GameClient()->m_aLocalIds[0];
	const bool OwnDummy = Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[1];
	if(!OwnMain && !OwnDummy)
		return;

	if(m_MapHistory.AddDeath(m_MapHistoryActiveMapId))
		MarkMapHistoryDirty();
}

void CTClient::HandleMapHistoryTeamDeath(int Team)
{
	if(g_Config.m_QmAutoSaveHistoryCount <= 0 || !m_MapHistorySessionActive)
		return;

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;
		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId >= 0 && GameClient()->m_Teams.Team(ClientId) == Team)
		{
			if(m_MapHistory.AddDeath(m_MapHistoryActiveMapId))
				MarkMapHistoryDirty();
			return;
		}
	}
}

void CTClient::HandleMapHistoryFinish(int ClientId, int FinishTimeMs)
{
	if(g_Config.m_QmAutoSaveHistoryCount <= 0 || !m_MapHistorySessionActive || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	bool OwnFinish = false;
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;
		const int LocalId = GameClient()->m_aLocalIds[Dummy];
		if(LocalId < 0)
			continue;
		if(ClientId == LocalId)
		{
			OwnFinish = true;
			break;
		}
		const int LocalTeam = GameClient()->m_Teams.Team(LocalId);
		if(GameClient()->m_GameInfo.m_DDRaceTeam && LocalTeam > TEAM_FLOCK && GameClient()->m_Teams.Team(ClientId) == LocalTeam)
		{
			OwnFinish = true;
			break;
		}
	}

	if(!OwnFinish)
		return;

	m_MapHistory.MarkFinished(m_MapHistoryActiveMapId, FinishTimeMs, CurrentMapHistoryPlayTimeMs());
	MarkMapHistoryDirty();
	SaveMapHistory();
}

void CTClient::RemoveMapHistoryRecord(const char *pMapId)
{
	if(pMapId == nullptr || pMapId[0] == '\0')
		return;
	if(m_MapHistory.Remove(pMapId))
	{
		std::string CurrentMapId;
		if(Client()->State() == IClient::STATE_ONLINE)
			CurrentMapId = CurrentMapHistoryId();
		if(!CurrentMapId.empty() && CurrentMapId == pMapId)
			m_MapHistorySuppressedMapId = CurrentMapId;
		if(m_MapHistoryActiveMapId == pMapId)
		{
			m_MapHistorySessionActive = false;
			m_MapHistorySessionStart = 0;
			m_MapHistoryActiveMapId.clear();
			m_MapHistoryActiveMapName.clear();
		}
		m_MapHistoryDirty = true;
		SaveMapHistory();
	}
}

void CTClient::ClearFinishedMapHistory()
{
	if(m_MapHistory.ClearFinished() > 0)
	{
		m_MapHistoryDirty = true;
		SaveMapHistory();
	}
}

void CTClient::ClearAllMapHistory()
{
	m_MapHistory.Clear();
	m_MapHistorySessionActive = false;
	m_MapHistorySessionStart = 0;
	m_MapHistorySuppressedMapId.clear();
	if(Client()->State() == IClient::STATE_ONLINE)
		m_MapHistorySuppressedMapId = CurrentMapHistoryId();
	m_MapHistoryActiveMapId.clear();
	m_MapHistoryActiveMapName.clear();
	m_MapHistoryDirty = true;
	SaveMapHistory();
}

bool CTClient::LoadLocalSaveEntries(std::vector<SLocalSaveEntry> &vEntries, bool *pFileExists) const
{
	if(pFileExists)
		*pFileExists = false;
	vEntries.clear();
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	if(pFileExists)
		*pFileExists = true;
	char *pContent = io_read_all_str(File);
	io_close(File);
	if(!pContent)
		return false;
	vEntries = QmLocalSaves::ParseEntries(pContent);
	free(pContent);
	return true;
}

bool CTClient::RemoveLocalSaveByCode(const char *pMap, const char *pCode)
{
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	char *pContent = io_read_all_str(File);
	io_close(File);
	if(!pContent)
		return false;
	const std::string Original(pContent);
	free(pContent);
	if(Original.size() > std::numeric_limits<unsigned>::max())
		return false;
	std::string Updated;
	if(!QmLocalSaves::RemoveEntries(Original, pMap, pCode, Updated))
		return false;

	// 先完整写入临时文件，再替换存档列表，避免写入中断损坏其他记录。
	constexpr const char *pTempFile = "ddnet-saves.txt.tmp";
	File = Storage()->OpenFile(pTempFile, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const bool Written = io_write(File, Updated.data(), (unsigned)Updated.size()) == Updated.size() && io_flush(File) == 0 && io_error(File) == 0;
	const int CloseResult = io_close(File);
	if(!Written || CloseResult != 0)
	{
		Storage()->RemoveFile(pTempFile, IStorage::TYPE_SAVE);
		return false;
	}
	if(Storage()->RenameFile(pTempFile, SAVES_FILE, IStorage::TYPE_SAVE))
		return true;

	// Windows 的现有重命名实现可能先移除目标；失败时恢复原列表，临时文件仍保留完整的新列表。
	if(!Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE))
	{
		File = Storage()->OpenFile(SAVES_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			const bool Restored = io_write(File, Original.data(), (unsigned)Original.size()) == Original.size() && io_flush(File) == 0;
			const int RestoreCloseResult = io_close(File);
			if(!Restored || RestoreCloseResult != 0)
				log_error("saves", "Failed to restore saves file; remaining records are in %s", pTempFile);
		}
	}
	return false;
}

QmLocalSaves::CRestore::SWorld CTClient::LocalSaveWorld() const
{
	QmLocalSaves::CRestore::SWorld World;
	World.m_Online = Client()->State() == IClient::STATE_ONLINE;
	World.m_Map = Client()->GetCurrentMap();
	World.m_DummyConnected = Client()->DummyConnected();
	World.m_DummyConnecting = Client()->DummyConnecting() || Client()->DummyConnectingDelayed();
	World.m_PlayersReady = World.m_DummyConnected;
	World.m_CharactersReady = true;
	for(int Conn = 0; Conn < 2; ++Conn)
	{
		const int Id = GameClient()->m_aLocalIds[Conn];
		if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_aClients[Id].m_Active)
		{
			World.m_PlayersReady = false;
			World.m_CharactersReady = false;
			continue;
		}
		World.m_aNames[Conn] = GameClient()->m_aClients[Id].m_aName;
		World.m_aTeams[Conn] = GameClient()->m_Teams.Team(Id);
		World.m_CharactersReady &= GameClient()->m_Snap.m_aCharacters[Id].m_Active;
	}
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		const int Team = GameClient()->m_Teams.Team(Id);
		if(GameClient()->m_aClients[Id].m_Active && Team >= 0 && Team < NUM_DDRACE_TEAMS)
			++World.m_aTeamSizes[Team];
	}
	const CNetObj_GameInfo *pInfo = GameClient()->m_Snap.m_pGameInfoObj;
	World.m_Racing = pInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active && (pInfo->m_GameStateFlags & GAMESTATEFLAG_RACETIME);
	return World;
}

void CTClient::TrackLocalSaveLoadCommand(int Conn, const char *pLine)
{
	const std::string Code = QmLocalSaves::LoadCode(pLine);
	if(Code.empty() || Client()->State() != IClient::STATE_ONLINE || Conn < 0 || Conn > 1)
		return;
	const int Id = GameClient()->m_aLocalIds[Conn];
	if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_aClients[Id].m_Active)
		return;
	const int64_t Now = time_get();
	if(m_LocalSaveConfirmation.Active() && Now > m_LocalSaveConfirmation.Request().m_Deadline)
		m_LocalSaveConfirmation.Reset();
	const char *pMap = Client()->GetCurrentMap();
	std::vector<SLocalSaveEntry> vEntries;
	LoadLocalSaveEntries(vEntries);
	const bool KnownCode = std::any_of(vEntries.begin(), vEntries.end(), [&](const SLocalSaveEntry &Entry) {
		return Entry.m_Code == Code && str_comp_nocase(Entry.m_Map.c_str(), pMap) == 0;
	});
	if(!KnownCode && !m_LocalSaveConfirmation.Active())
		return;
	const auto World = LocalSaveWorld();
	const int Team = GameClient()->m_Teams.Team(Id);
	const bool CanObserveClock = GameClient()->m_Snap.m_pGameInfoObj && GameClient()->m_Snap.m_pLocalCharacter &&
				     World.m_aTeams[g_Config.m_ClDummy] == Team && !GameClient()->m_Snap.m_SpecInfo.m_Active;
	m_LocalSaveConfirmation.Track({pMap, Code, Conn, Team, Client()->GameTick(Conn), World.m_Racing || !CanObserveClock, Now + 30 * time_freq()});
	if(m_LocalSaveRestore.Active() && m_LocalSaveRestore.Entry().m_Code != Code)
		m_LocalSaveRestore.Reset();
}

void CTClient::CompleteLocalSaveLoad(bool Success)
{
	if(!m_LocalSaveConfirmation.Active())
		return;
	const auto Request = m_LocalSaveConfirmation.Request();
	const bool Automatic = m_LocalSaveRestore.Active();
	m_LocalSaveConfirmation.Reset();
	m_LocalSaveRestore.Reset();
	if(Success)
	{
		const bool Removed = RemoveLocalSaveByCode(Request.m_Map.c_str(), Request.m_Code.c_str());
		GameClient()->Echo(Removed ? Localize("Save loaded; local record removed.") : Localize("Save loaded; the local record could not be removed."));
		m_LocalSavePromptActive = false;
		m_vLocalSaveCandidates.clear();
	}
	else if(Automatic)
		GameClient()->Echo(Localize("Save restore stopped without confirming success. The local record was kept."));
}

void CTClient::HandleLocalSaveMessage(const CNetMsg_Sv_Chat *pMsg, int Conn)
{
	if(Client()->State() != IClient::STATE_ONLINE || Conn < 0 || Conn > 1)
		return;
	const int Id = GameClient()->m_aLocalIds[Conn];
	if(Id < 0 || Id >= MAX_CLIENTS)
		return;
	const auto Result = m_LocalSaveConfirmation.Message(Conn, pMsg->m_ClientId, pMsg->m_pMessage, Client()->GetCurrentMap(), GameClient()->m_Teams.Team(Id), time_get());
	if(Result != QmLocalSaves::EResult::NONE)
		CompleteLocalSaveLoad(Result == QmLocalSaves::EResult::SUCCESS);
}

bool CTClient::TryHandleLocalSaveReply(const char *pLine)
{
	const auto Reply = QmLocalSaves::ParseReply(pLine);
	if(Reply.m_Kind == QmLocalSaves::EReply::NONE)
		return false;
	if(Reply.m_Kind == QmLocalSaves::EReply::INVALID)
	{
		GameClient()->Echo(Localize("Use /qm Yes [number] to restore a save, or /qm No to dismiss."));
		return true;
	}
	if(Reply.m_Kind == QmLocalSaves::EReply::NO)
	{
		m_LocalSavePromptActive = false;
		m_LocalSaveRestore.Reset();
		GameClient()->Echo(Localize("Save restore dismissed."));
		return true;
	}
	if(m_LocalSaveRestore.Active() || m_LocalSaveConfirmation.Active())
	{
		GameClient()->Echo(Localize("A save is already being restored. Please wait."));
		return true;
	}
	if(!m_LocalSavePromptActive || Client()->State() != IClient::STATE_ONLINE || str_comp(m_aLastLocalSaveHintMap, Client()->GetCurrentMap()) != 0)
	{
		GameClient()->Echo(Localize("There is no pending save restore prompt."));
		return true;
	}
	if(Reply.m_Index < 1 || (size_t)Reply.m_Index > m_vLocalSaveCandidates.size())
	{
		GameClient()->Echo(Localize("Invalid save number. Use a number from the save list."));
		return true;
	}
	const SLocalSaveEntry &Entry = m_vLocalSaveCandidates[Reply.m_Index - 1];
	std::vector<SLocalSaveEntry> vEntries;
	LoadLocalSaveEntries(vEntries);
	if(std::none_of(vEntries.begin(), vEntries.end(), [&](const SLocalSaveEntry &Current) {
		   return Current.m_Map == Entry.m_Map && Current.m_Code == Entry.m_Code && Current.m_Players == Entry.m_Players;
	   }))
	{
		GameClient()->Echo(Localize("This save is no longer in the local save file."));
		return true;
	}
	auto World = LocalSaveWorld();
	if(World.m_aNames[1].empty())
		World.m_aNames[1] = Client()->DummyName();
	std::array<std::string, 2> Names;
	if(!QmLocalSaves::ParseNames(Entry.m_Players, Names))
		return true;
	Names = QmLocalSaves::AssignNames(Names, World.m_aNames);
	if(Client()->IsSixup() && (World.m_aNames[0] != Names[0] || (World.m_DummyConnected && World.m_aNames[1] != Names[1])))
	{
		GameClient()->Echo(Localize("This connection cannot change names while connected. Reconnect with the saved names first."));
		return true;
	}
	if(World.m_Racing)
	{
		GameClient()->Echo(Localize("Finish or reset the current race before restoring a save."));
		return true;
	}
	m_LocalSaveRestore.Begin(Entry, Names, time_get(), time_freq());
	char aMessage[512];
	str_format(aMessage, sizeof(aMessage), Localize("Restoring save %d: main '%s', dummy '%s'."), Reply.m_Index, Names[0].c_str(), Names[1].c_str());
	GameClient()->Echo(aMessage);
	return true;
}

void CTClient::UpdateLocalSaveRestore()
{
	if(!m_LocalSaveRestore.Active() && !m_LocalSaveConfirmation.Active())
		return;
	const int64_t Now = time_get();
	const auto World = LocalSaveWorld();
	if(m_LocalSaveConfirmation.Active())
	{
		const auto &Request = m_LocalSaveConfirmation.Request();
		const int Id = GameClient()->m_aLocalIds[Request.m_Conn];
		if(!World.m_Online || str_comp_nocase(World.m_Map.c_str(), Request.m_Map.c_str()) != 0 || Now > Request.m_Deadline ||
			Id < 0 || Id >= MAX_CLIENTS || GameClient()->m_Teams.Team(Id) != Request.m_Team)
			CompleteLocalSaveLoad(false);
		else
		{
			const CNetObj_GameInfo *pInfo = GameClient()->m_Snap.m_pGameInfoObj;
			if(pInfo && World.m_Racing && GameClient()->m_Snap.m_pLocalCharacter &&
				m_LocalSaveConfirmation.RestoredRace(World.m_Map.c_str(), World.m_aTeams[g_Config.m_ClDummy], Client()->GameTick(g_Config.m_ClDummy), -pInfo->m_WarmupTimer, Client()->GameTickSpeed(), Now))
				CompleteLocalSaveLoad(true);
		}
	}

	const auto Action = m_LocalSaveRestore.Update(World, Now, time_freq());
	const auto &Names = m_LocalSaveRestore.Names();
	switch(Action)
	{
	case QmLocalSaves::EAction::CONNECT:
		str_copy(g_Config.m_ClDummyName, Names[1].c_str());
		Client()->DummyConnect();
		break;
	case QmLocalSaves::EAction::RENAME:
		if(Client()->IsSixup())
		{
			m_LocalSaveRestore.Reset();
			GameClient()->Echo(Localize("This connection cannot change names while connected. Reconnect with the saved names first."));
			break;
		}
		if(World.m_aNames[0] != Names[0])
		{
			str_copy(g_Config.m_PlayerName, Names[0].c_str());
			GameClient()->SendInfo(false);
		}
		if(World.m_aNames[1] != Names[1])
		{
			str_copy(g_Config.m_ClDummyName, Names[1].c_str());
			GameClient()->SendDummyInfo(false);
		}
		break;
	case QmLocalSaves::EAction::JOIN_MAIN:
	case QmLocalSaves::EAction::JOIN_DUMMY:
	{
		char aCommand[32];
		str_format(aCommand, sizeof(aCommand), "/team %d", m_LocalSaveRestore.Team());
		GameClient()->m_Chat.SendChatOnConn(Action == QmLocalSaves::EAction::JOIN_MAIN ? IClient::CONN_MAIN : IClient::CONN_DUMMY, 0, aCommand);
		break;
	}
	case QmLocalSaves::EAction::INVITE:
	{
		// 邀请分身可兼容用户已有的自动锁队设置。
		const std::string Command = "/invite " + QmLocalSaves::QuotedArgument(Names[1]);
		GameClient()->m_Chat.SendChatOnConn(IClient::CONN_MAIN, 0, Command.c_str());
		break;
	}
	case QmLocalSaves::EAction::LOAD:
	{
		const std::string Command = QmLocalSaves::LoadCommand(m_LocalSaveRestore.Entry().m_Code);
		GameClient()->m_Chat.SendChatOnConn(IClient::CONN_MAIN, 0, Command.c_str());
		break;
	}
	case QmLocalSaves::EAction::FAILED:
		m_LocalSaveRestore.Reset();
		GameClient()->Echo(Localize("Save restore stopped: connection, names or team were not ready. The local record was kept."));
		break;
	case QmLocalSaves::EAction::WAIT:
		break;
	}
}

void CTClient::MaybeShowLocalSaveJoinHint()
{
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_aLocalIds[0] < 0)
		return;

	const char *pCurrentMap = Client()->GetCurrentMap();
	if(!pCurrentMap || pCurrentMap[0] == '\0')
		return;
	if(str_comp(m_aLastLocalSaveHintMap, pCurrentMap) == 0)
		return;

	std::vector<SLocalSaveEntry> vEntries;
	bool FileExists = false;
	if(!LoadLocalSaveEntries(vEntries, &FileExists))
	{
		str_copy(m_aLastLocalSaveHintMap, pCurrentMap, sizeof(m_aLastLocalSaveHintMap));
		return;
	}

	std::vector<const SLocalSaveEntry *> vMatchedEntries;
	for(const SLocalSaveEntry &Entry : vEntries)
	{
		if(!Entry.m_Map.empty() && str_comp_nocase(Entry.m_Map.c_str(), pCurrentMap) == 0)
			vMatchedEntries.push_back(&Entry);
	}

	if(vMatchedEntries.empty())
	{
		str_copy(m_aLastLocalSaveHintMap, pCurrentMap, sizeof(m_aLastLocalSaveHintMap));
		return;
	}

	char aMessage[1024];
	str_format(aMessage, sizeof(aMessage), Localize("- You have %d saves on this map!"), (int)vMatchedEntries.size());
	GameClient()->Echo(aMessage);

	std::string PlayersLine = Localize("- Save owners in order:");
	std::string CodesLine = Localize("- Save codes in order:");
	const int DisplayCount = minimum((int)vMatchedEntries.size(), LOCAL_SAVE_JOIN_HINT_MAX_ITEMS);
	for(int EntryIndex = 0; EntryIndex < DisplayCount; ++EntryIndex)
	{
		const SLocalSaveEntry *pEntry = vMatchedEntries[EntryIndex];
		if(EntryIndex > 0)
		{
			PlayersLine += ",";
			CodesLine += ",";
		}
		PlayersLine += pEntry->m_Players.empty() ? Localize("Unknown") : pEntry->m_Players;
		CodesLine += pEntry->m_Code.empty() ? Localize("No code") : pEntry->m_Code;
	}
	if(DisplayCount < (int)vMatchedEntries.size())
	{
		PlayersLine += ",...";
		CodesLine += ",...";
	}
	GameClient()->Echo(PlayersLine.c_str());
	GameClient()->Echo(CodesLine.c_str());

	m_vLocalSaveCandidates = QmLocalSaves::Candidates(vEntries, pCurrentMap);
	m_LocalSavePromptActive = !m_vLocalSaveCandidates.empty();
	if(m_LocalSavePromptActive)
	{
		auto World = LocalSaveWorld();
		if(World.m_aNames[1].empty())
			World.m_aNames[1] = Client()->DummyName();
		GameClient()->Echo(Localize("Restore a two-player save? /qm Yes uses the newest; /qm Yes number selects another. /qm No dismisses."));
		for(size_t Index = 0; Index < m_vLocalSaveCandidates.size(); ++Index)
		{
			const auto &Entry = m_vLocalSaveCandidates[Index];
			std::array<std::string, 2> Names;
			QmLocalSaves::ParseNames(Entry.m_Players, Names);
			Names = QmLocalSaves::AssignNames(Names, World.m_aNames);
			str_format(aMessage, sizeof(aMessage), Localize("%d. %s | main: %s | dummy: %s"), (int)Index + 1, Entry.m_Time.c_str(), Names[0].c_str(), Names[1].c_str());
			GameClient()->Echo(aMessage);
		}
	}

	str_copy(m_aLastLocalSaveHintMap, pCurrentMap, sizeof(m_aLastLocalSaveHintMap));
}

void CTClient::ConSaveList(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// 如果用户指定了地图名，使用指定的；否则使用当前地图
	const char *pFilterMap = pResult->NumArguments() > 0 ? pResult->GetString(0) : pThis->Client()->GetCurrentMap();

	std::vector<SLocalSaveEntry> vEntries;
	bool FileExists = false;
	if(!pThis->LoadLocalSaveEntries(vEntries, &FileExists))
	{
		pThis->GameClient()->Echo(FileExists ? Localize("Failed to read saves file") : Localize("No local saves file found (ddnet-saves.txt)"));
		return;
	}

	int Count = 0;

	// 显示标题
	char aTitle[256];
	if(pFilterMap && pFilterMap[0] != '\0')
		str_format(aTitle, sizeof(aTitle), Localize("=== Saves for '%s' ==="), pFilterMap);
	else
		str_copy(aTitle, Localize("=== All Local Saves ==="));
	pThis->GameClient()->Echo(aTitle);

	for(const SLocalSaveEntry &Entry : vEntries)
	{
		// 过滤地图
		if(pFilterMap && pFilterMap[0] != '\0')
		{
			// 精确匹配地图名（不区分大小写）
			if(str_comp_nocase(Entry.m_Map.c_str(), pFilterMap) != 0)
				continue;
		}

		// 输出格式: [玩家名] 密码 (地图: xxx, 保存时间: xxx)
		char aOutput[512];
		str_format(aOutput, sizeof(aOutput), Localize("[%s] %s (Map: %s, Time: %s)"),
			Entry.m_Players.empty() ? Localize("Unknown") : Entry.m_Players.c_str(),
			Entry.m_Code.empty() ? Localize("No code") : Entry.m_Code.c_str(),
			Entry.m_Map.empty() ? Localize("Unknown") : Entry.m_Map.c_str(),
			Entry.m_Time.empty() ? Localize("Unknown") : Entry.m_Time.c_str());
		pThis->GameClient()->Echo(aOutput);
		Count++;
	}

	char aCountMsg[128];
	str_format(aCountMsg, sizeof(aCountMsg), Localize("Total: %d save(s)"), Count);
	pThis->GameClient()->Echo(aCountMsg);
}

// ========== 复读功能 ==========

void CTClient::ConRepeat(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	if(!pThis->GameClient())
		return;

	// 仅在按下沿判定，避免按住时键盘重复触发。
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(!Pressed)
	{
		pThis->m_RepeatKeyDown = false;
		return;
	}
	if(pThis->m_RepeatKeyDown)
		return;
	pThis->m_RepeatKeyDown = true;

	if(!g_Config.m_QmRepeatEnabled)
		return;

	const int64_t Now = time_get();
	const int64_t DoublePressWindow = time_freq(); // 1秒内连按两次触发
	if(Now - pThis->m_LastRepeatKeyPressTime > DoublePressWindow)
	{
		pThis->m_LastRepeatKeyPressTime = Now;
		return;
	}

	pThis->m_LastRepeatKeyPressTime = 0;
	pThis->RepeatLastMessage();
}

bool CTClient::OnInput(const IInput::CEvent &Event)
{
	// 不再需要在这里处理复读功能，已通过 +qm_repeat 命令绑定
	return false;
}

void CTClient::RepeatLastMessage()
{
	// 检查是否有消息可以复读
	if(m_aLastChatMessage[0] == '\0')
	{
		GameClient()->m_Chat.AddLine(-2, 0, Localize("No chat message available to repeat"));
		return;
	}

	// 检查冷却时间（1秒）
	int64_t Now = time_get();
	if(Now - m_LastRepeatTime < time_freq())
	{
		return;
	}
	m_LastRepeatTime = Now;

	// 发送复读消息
	GameClient()->m_Chat.SendChat(m_LastChatTeam, m_aLastChatMessage);
}
