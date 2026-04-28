#include "qmclient.h"
#include <base/hash.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/external/tinyexpr.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/jobs.h>
#include <engine/client/updater.h>
#include <engine/map.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

[[maybe_unused]] static constexpr const char *TCLIENT_INFO_URL = "http://42.194.185.210:8080/client/version";
[[maybe_unused]] static constexpr const char *TCLIENT_UPDATE_EXE_URL = "https://github.com/wxj881027/QmClient/releases/latest/download/DDNet.exe";
[[maybe_unused]] static constexpr const char *MAP_CATEGORY_CACHE_FILE = "qmclient/map_categories.json";
[[maybe_unused]] static constexpr int64_t MAP_CATEGORY_CACHE_SAVE_DELAY_SEC = 5;
static constexpr int QMCLIENT_SYNC_INTERVAL_SECONDS = 30;
static constexpr int QMCLIENT_VOICE_SYNC_INTERVAL_SECONDS = 10;
static constexpr const char *QMCLIENT_DEFAULT_VOICE_SERVER = "42.194.185.210:9987";
static constexpr const char *QMCLIENT_TOKEN_PATH = "/qm/token";
static constexpr const char *QMCLIENT_REPORT_PATH = "/qm/report";
static constexpr const char *QMCLIENT_USERS_PATH = "/qm/users.json";
static constexpr const char *QMCLIENT_HEALTH_URL = "http://42.194.185.210:8080/healthz";
static constexpr const char *QMCLIENT_PLAYTIME_START_URL = "http://42.194.185.210:8080/playtime/start";
static constexpr const char *QMCLIENT_PLAYTIME_STOP_URL = "http://42.194.185.210:8080/playtime/stop";
static constexpr const char *QMCLIENT_PLAYTIME_QUERY_URL = "http://42.194.185.210:8080/playtime/query";
static constexpr const char *QMCLIENT_LIFECYCLE_MARKER_FILE = "qmclient/lifecycle_pending.marker";
static constexpr const char *QMCLIENT_PLAYTIME_CLIENT_ID_FILE = "qmclient/playtime_client_id.txt";
static constexpr const char *QMCLIENT_MACHINE_ID_FALLBACK_FILE = "qmclient/voice_machine_id.txt";
static constexpr int QMCLIENT_SERVER_TIME_SYNC_INTERVAL_SECONDS = 15;
static constexpr int QMCLIENT_PLAYTIME_QUERY_INTERVAL_SECONDS = 10;
static constexpr int QMCLIENT_RECOVERY_RETRY_SECONDS = 3;
static constexpr int QMCLIENT_MARKER_FLUSH_INTERVAL_SECONDS = 5;
static constexpr const char *DDNET_PLAYER_STATS_URL = "https://ddnet.org/players/?json2=";
static constexpr int QMCLIENT_DDNET_PLAYER_SYNC_INTERVAL_SECONDS = 120;
static constexpr int QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS = 10;
static constexpr const char *QMCLIENT_FREEZE_WAKEUP_TEXT = "快醒醒!";
[[maybe_unused]] static constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_MAX_ATTEMPTS = 3;
[[maybe_unused]] static constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_RETRY_DELAY_SECONDS = 2;

[[maybe_unused]] static bool TextContainsAny(const char *pText, const std::initializer_list<const char *> &Tokens)
{
	if(!pText || pText[0] == '\0')
		return false;

	for(const char *pToken : Tokens)
	{
		if(pToken && pToken[0] != '\0' && str_find_nocase(pText, pToken))
			return true;
	}
	return false;
}
static constexpr float QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION = 2.0f;
static constexpr float QMCLIENT_COMBO_POPUP_DURATION = 1.25f;
[[maybe_unused]] static constexpr float QMCLIENT_TEXT_POPUP_FONT_SIZE = 30.0f;
[[maybe_unused]] static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET = vec2(34.0f, -78.0f);
[[maybe_unused]] static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT = vec2(18.0f, -16.0f);
[[maybe_unused]] static constexpr int QMCLIENT_COMBO_POPUP_WINDOW_SECONDS = 2;
[[maybe_unused]] static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_FROM = ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f);
[[maybe_unused]] static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_TO = ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f);
[[maybe_unused]] static constexpr ColorRGBA QMCLIENT_COMBO_POPUP_COLOR = ColorRGBA(1.0f, 0.96f, 0.45f, 1.0f);
[[maybe_unused]] static constexpr const char *s_apKeywordNegationWords[] = {
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
[[maybe_unused]] static constexpr const char *s_apKeywordClauseContrastWords[] = {
	"但是",
	"但",
	"不过",
	"然而",
	"可是",
};
static constexpr const char *s_pFriendEnterBroadcastDefaultText = "%s joined this server";

[[maybe_unused]] static int AutoReplySeparatorLength(const char *pStr);
[[maybe_unused]] static bool AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules);
static const json_value *JsonObjectField(const json_value *pObject, const char *pName);
static bool JsonReadNonNegativeInt64(const json_value *pValue, int64_t &OutValue);
static bool JsonReadBoolean(const json_value *pValue, bool &OutValue);

namespace
{
enum class ETextPopupType
{
	FREEZE_WAKEUP = 0,
	COMBO_AMAZING,
	COMBO_FANTASTIC,
	COMBO_UNBELIEVABLE,
	COMBO_UNSTOPPABLE,
	NUM_TYPES,
};

struct STextPopupDefinition
{
	const char *m_pText;
};

[[maybe_unused]] static constexpr std::array<STextPopupDefinition, (int)ETextPopupType::NUM_TYPES> s_aTextPopupDefinitions = {{
	{QMCLIENT_FREEZE_WAKEUP_TEXT},
	{"Amazing!"},
	{"Fantastic!"},
	{"Unbelievable!"},
	{"Unstoppable!"},
}};

class CQmClientUsersParseJob : public IJob
{
public:
	struct SClientMark
	{
		int m_ClientId = -1;
		bool m_FootParticlesEnabled = false;
		bool m_RemoteParticlesEnabled = false;
		bool m_VoiceSupported = false;
		std::string m_Qid;
	};

	struct SResult
	{
		bool m_Parsed = false;
		std::vector<SQmClientServerDistribution> m_vServerDistribution;
		std::vector<SClientMark> m_vLocalServerMarks;
		int m_OnlineUserCount = 0;
		int m_OnlineDummyCount = 0;
	};

private:
	std::shared_ptr<CHttpRequest> m_pTask;
	char m_aServerAddress[NETADDR_MAXSTRSIZE] = "";
	int64_t m_ExpireTick = 0;
	CLock m_Lock;
	SResult m_Result;

	void Run() override REQUIRES(!m_Lock)
	{
		SResult Result;
		if(m_pTask && m_pTask->State() == EHttpState::DONE)
		{
			json_value *pRoot = m_pTask->ResultJson();
			if(pRoot != nullptr)
			{
				const json_value *pUsers = pRoot;
				if(pUsers->type == json_object)
				{
					const json_value *pUsersField = JsonObjectField(pUsers, "users");
					if(pUsersField != &json_value_none)
						pUsers = pUsersField;
				}

				if(pUsers->type == json_array)
				{
					Result.m_Parsed = true;
					std::unordered_map<std::string, size_t> ServerIndexByAddress;
					ServerIndexByAddress.reserve(pUsers->u.array.length);
					Result.m_vServerDistribution.reserve(pUsers->u.array.length);

					for(unsigned Index = 0; Index < pUsers->u.array.length; ++Index)
					{
						const json_value *pEntry = pUsers->u.array.values[Index];
						if(!pEntry || pEntry->type != json_object)
							continue;

						const json_value *pServerAddress = JsonObjectField(pEntry, "server_address");
						if(pServerAddress == &json_value_none)
							pServerAddress = JsonObjectField(pEntry, "server");
						if(pServerAddress == &json_value_none || pServerAddress->type != json_string)
							continue;

						const json_value *pPlayerId = JsonObjectField(pEntry, "player_id");
						if(pPlayerId == &json_value_none)
							pPlayerId = JsonObjectField(pEntry, "id");
						if(pPlayerId == &json_value_none || (pPlayerId->type != json_integer && pPlayerId->type != json_double))
							continue;

						const int ClientId = json_int_get(pPlayerId);
						bool Dummy = false;
						const json_value *pDummy = JsonObjectField(pEntry, "dummy");
						if(pDummy != &json_value_none)
							JsonReadBoolean(pDummy, Dummy);

						const std::string ServerAddress = pServerAddress->u.string.ptr;
						const auto ItServer = ServerIndexByAddress.find(ServerAddress);
						if(ItServer == ServerIndexByAddress.end())
						{
							ServerIndexByAddress[ServerAddress] = Result.m_vServerDistribution.size();
							Result.m_vServerDistribution.push_back({ServerAddress, 0, 0});
						}
						SQmClientServerDistribution &ServerDistribution = Result.m_vServerDistribution[ServerIndexByAddress[ServerAddress]];
						if(Dummy)
						{
							++ServerDistribution.m_DummyCount;
							++Result.m_OnlineDummyCount;
						}
						else
						{
							++ServerDistribution.m_UserCount;
							++Result.m_OnlineUserCount;
						}

						if(str_comp(ServerAddress.c_str(), m_aServerAddress) != 0)
							continue;

						SClientMark &Mark = Result.m_vLocalServerMarks.emplace_back();
						Mark.m_ClientId = ClientId;

						const json_value *pQidField = JsonObjectField(pEntry, "qid");
						if(pQidField != &json_value_none && pQidField->type == json_string)
							Mark.m_Qid = pQidField->u.string.ptr;

						const json_value *pFootParticlesEnabled = JsonObjectField(pEntry, "foot_particles_enabled");
						JsonReadBoolean(pFootParticlesEnabled, Mark.m_FootParticlesEnabled);

						const json_value *pRemoteParticlesEnabled = JsonObjectField(pEntry, "remote_particles_enabled");
						JsonReadBoolean(pRemoteParticlesEnabled, Mark.m_RemoteParticlesEnabled);

						Mark.m_VoiceSupported = true;
						const json_value *pVoiceSupported = JsonObjectField(pEntry, "voice_supported");
						if(pVoiceSupported != &json_value_none)
							JsonReadBoolean(pVoiceSupported, Mark.m_VoiceSupported);
					}

					std::sort(Result.m_vServerDistribution.begin(), Result.m_vServerDistribution.end(), [](const SQmClientServerDistribution &Left, const SQmClientServerDistribution &Right) {
						if(Left.m_UserCount != Right.m_UserCount)
							return Left.m_UserCount > Right.m_UserCount;
						if(Left.m_DummyCount != Right.m_DummyCount)
							return Left.m_DummyCount > Right.m_DummyCount;
						return str_comp(Left.m_ServerAddress.c_str(), Right.m_ServerAddress.c_str()) < 0;
					});
				}
				json_value_free(pRoot);
			}
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result = std::move(Result);
		}
		m_pTask = nullptr;
	}

public:
	CQmClientUsersParseJob(std::shared_ptr<CHttpRequest> pTask, const char *pServerAddress, int64_t ExpireTick) :
		m_pTask(std::move(pTask)),
		m_ExpireTick(ExpireTick)
	{
		str_copy(m_aServerAddress, pServerAddress, sizeof(m_aServerAddress));
	}

	SResult TakeResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}

	int64_t ExpireTick() const { return m_ExpireTick; }
};

class CQmDdnetPlayerStatsParseJob : public IJob
{
public:
	struct SResult
	{
		bool m_Parsed = false;
		std::string m_FavoritePartner;
		int m_TotalFinishes = -1;
	};

private:
	std::shared_ptr<CHttpRequest> m_pTask;
	CLock m_Lock;
	SResult m_Result;

	void Run() override REQUIRES(!m_Lock)
	{
		SResult Result;
		if(m_pTask && m_pTask->State() == EHttpState::DONE && m_pTask->StatusCode() == 200)
		{
			json_value *pRoot = m_pTask->ResultJson();
			if(pRoot && pRoot->type == json_object)
			{
				const json_value *pFavoritePartners = JsonObjectField(pRoot, "favorite_partners");
				if(pFavoritePartners->type == json_array)
				{
					const char *pBestPartner = nullptr;
					int BestPartnerFinishes = -1;
					for(unsigned i = 0; i < pFavoritePartners->u.array.length; ++i)
					{
						const json_value &Partner = (*pFavoritePartners)[i];
						if(Partner.type != json_object)
							continue;

						const json_value *pName = JsonObjectField(&Partner, "name");
						if(pName->type != json_string)
							continue;

						const char *pPartnerName = json_string_get(pName);
						if(!pPartnerName || pPartnerName[0] == '\0')
							continue;

						int PartnerFinishes = 0;
						const json_value *pFinishes = JsonObjectField(&Partner, "finishes");
						if(pFinishes->type == json_integer && pFinishes->u.integer > 0)
						{
							if(pFinishes->u.integer > std::numeric_limits<int>::max())
								PartnerFinishes = std::numeric_limits<int>::max();
							else
								PartnerFinishes = (int)pFinishes->u.integer;
						}

						if(!pBestPartner ||
							PartnerFinishes > BestPartnerFinishes ||
							(PartnerFinishes == BestPartnerFinishes && str_comp_nocase(pPartnerName, pBestPartner) < 0))
						{
							pBestPartner = pPartnerName;
							BestPartnerFinishes = PartnerFinishes;
						}
					}

					if(pBestPartner)
						Result.m_FavoritePartner = pBestPartner;
				}

				const json_value *pTypes = JsonObjectField(pRoot, "types");
				if(pTypes->type == json_object)
				{
					Result.m_Parsed = true;
					int64_t TotalFinishes = 0;
					for(unsigned i = 0; i < pTypes->u.object.length; ++i)
					{
						const json_value *pTypeObj = pTypes->u.object.values[i].value;
						if(!pTypeObj || pTypeObj->type != json_object)
							continue;

						const json_value *pMaps = JsonObjectField(pTypeObj, "maps");
						if(pMaps->type != json_object)
							continue;

						for(unsigned j = 0; j < pMaps->u.object.length; ++j)
						{
							const json_value *pMapObj = pMaps->u.object.values[j].value;
							if(!pMapObj || pMapObj->type != json_object)
								continue;

							const json_value *pFinishes = JsonObjectField(pMapObj, "finishes");
							if(pFinishes->type != json_integer || pFinishes->u.integer <= 0 || TotalFinishes >= std::numeric_limits<int>::max())
								continue;

							int64_t SafeAdd = pFinishes->u.integer;
							if(SafeAdd > std::numeric_limits<int>::max())
								SafeAdd = std::numeric_limits<int>::max();

							const int64_t MaxTotal = std::numeric_limits<int>::max();
							if(SafeAdd > MaxTotal - TotalFinishes)
								TotalFinishes = MaxTotal;
							else
								TotalFinishes += SafeAdd;
						}
					}
					Result.m_TotalFinishes = (int)TotalFinishes;
				}
				json_value_free(pRoot);
			}
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result = std::move(Result);
		}
		m_pTask = nullptr;
	}

public:
	explicit CQmDdnetPlayerStatsParseJob(std::shared_ptr<CHttpRequest> pTask) :
		m_pTask(std::move(pTask))
	{
	}

	SResult TakeResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

class CQmClientLifecycleMarkerWriteJob : public IJob
{
	IStorage *m_pStorage = nullptr;
	std::string m_Content;

	void Run() override
	{
		if(m_pStorage == nullptr || State() == IJob::STATE_ABORTED)
			return;

		m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		IOHANDLE File = m_pStorage->OpenFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return;
		if(State() == IJob::STATE_ABORTED)
		{
			io_close(File);
			return;
		}

		io_write(File, m_Content.data(), m_Content.size());
		io_close(File);
	}

public:
	CQmClientLifecycleMarkerWriteJob(IStorage *pStorage, std::string Content) :
		m_pStorage(pStorage),
		m_Content(std::move(Content))
	{
		Abortable(true);
	}
};

enum class EFreezeWakeupType
{
	NONE,
	LOCAL_HAMMER,
	EXTERNAL_HAMMER,
};

[[maybe_unused]] int ComboPopupTextTypeFromCount(int ComboCount)
{
	switch(ComboCount)
	{
	case 2: return (int)ETextPopupType::COMBO_AMAZING;
	case 3: return (int)ETextPopupType::COMBO_FANTASTIC;
	case 4: return (int)ETextPopupType::COMBO_UNBELIEVABLE;
	default: return (int)ETextPopupType::COMBO_UNSTOPPABLE;
	}
}

bool IsComboPopupTextType(int TextType)
{
	return TextType >= (int)ETextPopupType::COMBO_AMAZING &&
		TextType <= (int)ETextPopupType::COMBO_UNSTOPPABLE;
}

[[maybe_unused]] float TextPopupDuration(int TextType)
{
	return IsComboPopupTextType(TextType) ? QMCLIENT_COMBO_POPUP_DURATION : QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION;
}

[[maybe_unused]] EFreezeWakeupType DetectFreezeWakeupType(CGameClient *pGameClient, int ClientId)
{
	const CCharacter *pPredictedChar = pGameClient->m_PredictedWorld.GetCharacterById(ClientId);
	if(pPredictedChar == nullptr)
		return EFreezeWakeupType::NONE;

	const int LastDamageTick = pPredictedChar->GetLastDamageTick();
	const int DamageTickDelta = pGameClient->m_PredictedWorld.GameTick() - LastDamageTick;
	const int DamageTickWindow = maximum(2, pGameClient->m_PredictedWorld.GameTickSpeed() / 6);
	const int DamageFrom = pPredictedChar->GetLastDamageFrom();
	if(LastDamageTick <= 0 ||
		DamageTickDelta < 0 ||
		DamageTickDelta > DamageTickWindow ||
		pPredictedChar->GetLastDamageWeapon() != WEAPON_HAMMER ||
		DamageFrom < 0)
	{
		return EFreezeWakeupType::NONE;
	}

	if(DamageFrom == ClientId ||
		DamageFrom == pGameClient->m_aLocalIds[0] ||
		DamageFrom == pGameClient->m_aLocalIds[1])
	{
		return EFreezeWakeupType::LOCAL_HAMMER;
	}

	return EFreezeWakeupType::EXTERNAL_HAMMER;
}
}

struct SKeywordReplyRule
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
	bool m_HasExplicitRenameFlag = false;
	bool m_HasExplicitRegexFlag = false;
};

static bool ParseQmClientServiceHostPort(const char *pAddrStr, char *pHost, size_t HostSize, int &Port)
{
	const char *pColon = str_rchr(pAddrStr, ':');
	if(!pColon || pColon == pAddrStr || *(pColon + 1) == '\0')
		return false;

	str_truncate(pHost, HostSize, pAddrStr, pColon - pAddrStr);
	if(pHost[0] == '[')
	{
		const int Len = str_length(pHost);
		if(Len >= 2 && pHost[Len - 1] == ']')
		{
			mem_move(pHost, pHost + 1, Len - 2);
			pHost[Len - 2] = '\0';
		}
	}

	Port = str_toint(pColon + 1);
	return Port > 0 && Port <= 65535;
}

const char *GetEffectiveQmVoiceServer()
{
	return g_Config.m_QmVoiceServer[0] != '\0' ? g_Config.m_QmVoiceServer : QMCLIENT_DEFAULT_VOICE_SERVER;
}

static void TrimQmClientTextInPlace(char *pText)
{
	if(!pText || pText[0] == '\0')
		return;
	char *pTrimmed = (char *)str_utf8_skip_whitespaces(pText);
	str_utf8_trim_right(pTrimmed);
	if(pTrimmed != pText)
		mem_move(pText, pTrimmed, str_length(pTrimmed) + 1);
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

[[maybe_unused]] static bool MigrateKeywordReplyRulesAutoRenamePreservingLines(const char *pRules, char *pOutRules, size_t OutRulesSize)
{
	if(!pOutRules || OutRulesSize == 0)
		return false;

	pOutRules[0] = '\0';
	if(!pRules || pRules[0] == '\0')
		return true;

	bool FirstLine = true;
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

		char aMigratedLine[sizeof(aLine)];
		str_copy(aMigratedLine, aLine, sizeof(aMigratedLine));

		char aRuleLine[sizeof(aLine)];
		str_copy(aRuleLine, aLine, sizeof(aRuleLine));
		char *pTrimmedLine = (char *)str_utf8_skip_whitespaces(aRuleLine);
		str_utf8_trim_right(pTrimmedLine);
		if(pTrimmedLine[0] != '\0' && pTrimmedLine[0] != '#')
		{
			bool AutoRename = false;
			bool RegexRule = false;
			bool HasExplicitRenameFlag = false;
			bool HasExplicitRegexFlag = false;
			char *pRuleText = ParseAutoReplyRulePrefixes(pTrimmedLine, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag);
			if(!HasExplicitRenameFlag && str_find(pRuleText, "=>") != nullptr)
				str_format(aMigratedLine, sizeof(aMigratedLine), "[rename] %s", pTrimmedLine);
		}

		const size_t CurrentLen = str_length(pOutRules);
		const size_t LineLenOut = str_length(aMigratedLine);
		const size_t SeparatorLen = FirstLine ? 0 : 1;
		if(CurrentLen + SeparatorLen + LineLenOut >= OutRulesSize)
			return false;

		if(!FirstLine)
			str_append(pOutRules, "\n", OutRulesSize);
		str_append(pOutRules, aMigratedLine, OutRulesSize);
		FirstLine = false;
	}

	return true;
}

[[maybe_unused]] static bool ReadQmClientAbsoluteTextFile(const char *pFilename, char *pBuf, size_t BufSize)
{
	if(!pFilename || !pBuf || BufSize == 0)
		return false;

	IOHANDLE File = io_open(pFilename, IOFLAG_READ);
	if(!File)
		return false;

	const int Read = io_read(File, pBuf, (unsigned)(BufSize - 1));
	io_close(File);
	if(Read <= 0)
		return false;

	pBuf[Read] = '\0';
	TrimQmClientTextInPlace(pBuf);
	return pBuf[0] != '\0';
}

static bool ReadPlatformMachineIdentity(std::string &OutIdentity)
{
#if defined(CONF_FAMILY_WINDOWS)
	HKEY Key = nullptr;
	LONG OpenResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &Key);
	if(OpenResult != ERROR_SUCCESS)
		OpenResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &Key);
	if(OpenResult == ERROR_SUCCESS && Key != nullptr)
	{
		wchar_t aValue[256] = {};
		DWORD Type = 0;
		DWORD Size = sizeof(aValue);
		const LONG QueryResult = RegQueryValueExW(Key, L"MachineGuid", nullptr, &Type, reinterpret_cast<LPBYTE>(aValue), &Size);
		RegCloseKey(Key);
		if(QueryResult == ERROR_SUCCESS && Type == REG_SZ)
		{
			const auto Utf8 = windows_wide_to_utf8(aValue);
			if(Utf8.has_value() && !Utf8->empty())
			{
				OutIdentity = *Utf8;
				return true;
			}
		}
	}
#elif defined(CONF_PLATFORM_LINUX)
	char aBuf[256];
	if(ReadQmClientAbsoluteTextFile("/etc/machine-id", aBuf, sizeof(aBuf)) ||
		ReadQmClientAbsoluteTextFile("/var/lib/dbus/machine-id", aBuf, sizeof(aBuf)))
	{
		OutIdentity = aBuf;
		return true;
	}
#endif

	return false;
}

static bool IsValidQmClientMachineHash(const char *pHash)
{
	if(!pHash || str_length(pHash) != SHA256_DIGEST_LENGTH * 2)
		return false;

	for(const char *p = pHash; *p; ++p)
	{
		if(!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
			return false;
	}
	return true;
}

[[maybe_unused]] static std::string BuildFriendEnterBroadcastText(const char *pTemplate, std::string_view FriendNames)
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

static const json_value *JsonObjectField(const json_value *pObject, const char *pName)
{
	if(!pObject || pObject->type != json_object)
		return &json_value_none;
	return json_object_get(pObject, pName);
}

static bool JsonReadNonNegativeInt64(const json_value *pValue, int64_t &OutValue)
{
	if(!pValue)
		return false;

	if(pValue->type == json_integer)
	{
		if(pValue->u.integer < 0)
			return false;
		OutValue = pValue->u.integer;
		return true;
	}
	if(pValue->type == json_double)
	{
		if(pValue->u.dbl < 0.0)
			return false;
		OutValue = (int64_t)pValue->u.dbl;
		return true;
	}
	return false;
}

static bool JsonReadBoolean(const json_value *pValue, bool &OutValue)
{
	if(!pValue)
		return false;

	if(pValue->type == json_boolean)
	{
		OutValue = json_boolean_get(pValue) != 0;
		return true;
	}
	if(pValue->type == json_integer)
	{
		OutValue = pValue->u.integer != 0;
		return true;
	}
	if(pValue->type == json_double)
	{
		OutValue = pValue->u.dbl != 0.0;
		return true;
	}
	return false;
}

static bool IsValidQmClientPlaytimeId(const char *pClientId)
{
	if(!pClientId)
		return false;

	const int Len = str_length(pClientId);
	if(Len < 8 || Len > 64)
		return false;

	for(int i = 0; i < Len; ++i)
	{
		const unsigned char C = (unsigned char)pClientId[i];
		if(std::isalnum(C) || C == '_' || C == '-')
			continue;
		return false;
	}
	return true;
}

void CQmClient::OnInit()
{
	InitQmClientLifecycle();
}

void CQmClient::OnShutdown()
{
	if(!m_QmClientShutdownReported)
	{
		m_QmClientShutdownReported = true;
		TouchQmClientLifecycleMarker(true);
		SendQmClientLifecyclePing("shutdown", m_pQmClientLifecycleStopTask);
	}

	auto AbortTask = [](std::shared_ptr<CHttpRequest> &pTask) {
		if(pTask)
		{
			pTask->Abort();
			pTask = nullptr;
		}
	};

	AbortTask(m_pQmClientLifecycleStartTask);
	AbortTask(m_pQmClientLifecycleCrashTask);
	AbortTask(m_pQmClientServerTimeTask);
	AbortTask(m_pQmClientPlaytimeQueryTask);
	AbortTask(m_pQmDdnetPlayerTask);
	AbortTask(m_pQmClientAuthTokenTask);
	AbortTask(m_pQmClientUsersTask);
	AbortTask(m_pQmClientUsersSendTask);
	m_pQmClientUsersParseJob = nullptr;
	m_pQmDdnetPlayerParseJob = nullptr;
}

void CQmClient::OnUpdate()
{
	UpdateQmClientRecognition();
	UpdateQmClientLifecycleAndServerTime();
	UpdateQmDdnetPlayerStats();
}

void CQmClient::OnStateChange(int NewState, int OldState)
{
	if((NewState == IClient::STATE_QUITTING || NewState == IClient::STATE_RESTARTING) && !m_QmClientShutdownReported)
	{
		m_QmClientShutdownReported = true;
		TouchQmClientLifecycleMarker(true);
		SendQmClientLifecyclePing(NewState == IClient::STATE_RESTARTING ? "restart" : "shutdown", m_pQmClientLifecycleStopTask);
	}
}

bool CQmClient::ReadQmClientLifecycleMarker(int64_t &OutStartedAt, int64_t &OutLastSeenAt)
{
	OutStartedAt = 0;
	OutLastSeenAt = 0;

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return false;

	std::string Marker;
	if(pFileData && FileSize > 0)
		Marker.assign(static_cast<const char *>(pFileData), FileSize);
	free(pFileData);
	if(Marker.empty())
		return true;

	char aLine[256];
	const char *pStr = Marker.c_str();
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		if(const char *pValue = str_startswith(aLine, "started_at="))
			OutStartedAt = maximum<int64_t>(0, str_toint(pValue));
		else if(const char *pLastSeenValue = str_startswith(aLine, "last_seen_at="))
			OutLastSeenAt = maximum<int64_t>(0, str_toint(pLastSeenValue));
	}
	return true;
}

void CQmClient::WriteQmClientLifecycleMarker()
{
	if(m_QmClientMarkerStartedAt <= 0)
		m_QmClientMarkerStartedAt = time_timestamp();
	if(m_QmClientMarkerLastSeenAt <= 0)
		m_QmClientMarkerLastSeenAt = m_QmClientMarkerStartedAt;

	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);

	IOHANDLE File = Storage()->OpenFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	char aLine[384];
	str_format(aLine, sizeof(aLine),
		"session=%s\nstarted_at=%d\nlast_seen_at=%d\nclient_id=%s\n",
		m_aQmClientLifecycleSessionId, (int)m_QmClientMarkerStartedAt, (int)m_QmClientMarkerLastSeenAt, m_aQmClientPlaytimeClientId);
	io_write(File, aLine, str_length(aLine));
	io_close(File);
}

void CQmClient::TouchQmClientLifecycleMarker(bool ForceWrite)
{
	const int64_t NowTick = time_get();
	const int64_t Interval = (int64_t)QMCLIENT_MARKER_FLUSH_INTERVAL_SECONDS * time_freq();
	if(!ForceWrite && m_QmClientMarkerLastFlushTick != 0 && NowTick - m_QmClientMarkerLastFlushTick < Interval)
		return;

	if(m_QmClientMarkerStartedAt <= 0)
		m_QmClientMarkerStartedAt = time_timestamp();
	m_QmClientMarkerLastSeenAt = time_timestamp();
	m_QmClientMarkerLastFlushTick = NowTick;

	if(ForceWrite)
	{
		if(m_pQmClientLifecycleMarkerWriteJob && !m_pQmClientLifecycleMarkerWriteJob->Done())
		{
			m_pQmClientLifecycleMarkerWriteJob->Abort();
			m_pQmClientLifecycleMarkerWriteJob = nullptr;
		}
		WriteQmClientLifecycleMarker();
		return;
	}

	if(m_pQmClientLifecycleMarkerWriteJob && !m_pQmClientLifecycleMarkerWriteJob->Done())
		return;

	char aLine[384];
	str_format(aLine, sizeof(aLine),
		"session=%s\nstarted_at=%d\nlast_seen_at=%d\nclient_id=%s\n",
		m_aQmClientLifecycleSessionId, (int)m_QmClientMarkerStartedAt, (int)m_QmClientMarkerLastSeenAt, m_aQmClientPlaytimeClientId);
	m_pQmClientLifecycleMarkerWriteJob = std::make_shared<CQmClientLifecycleMarkerWriteJob>(Storage(), aLine);
	Engine()->AddJob(m_pQmClientLifecycleMarkerWriteJob);
}

void CQmClient::ClearQmClientLifecycleMarker()
{
	Storage()->RemoveFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE);
}

void CQmClient::SendQmClientLifecyclePing(const char *pEvent, std::shared_ptr<CHttpRequest> &pTaskSlot)
{
	if(!pEvent || pEvent[0] == '\0')
		return;

	if(pTaskSlot)
	{
		if(!pTaskSlot->Done())
			return;
		pTaskSlot = nullptr;
	}

	if(str_comp(pEvent, "startup") == 0)
	{
		SendQmClientPlaytimeRequest(QMCLIENT_PLAYTIME_START_URL, pTaskSlot);
		return;
	}

	if(str_comp(pEvent, "recover_crash") == 0)
	{
		const int64_t StopAt = m_QmClientRecoveryStopAt > 0 ? m_QmClientRecoveryStopAt : time_timestamp();
		SendQmClientPlaytimeRequest(QMCLIENT_PLAYTIME_STOP_URL, pTaskSlot, StopAt);
		return;
	}

	if(str_comp(pEvent, "shutdown") == 0 || str_comp(pEvent, "restart") == 0)
	{
		SendQmClientPlaytimeRequest(QMCLIENT_PLAYTIME_STOP_URL, pTaskSlot, time_timestamp());
		return;
	}
}

void CQmClient::EnsureQmClientPlaytimeClientId()
{
	if(m_aQmClientPlaytimeClientId[0] != '\0')
		return;

	char aLoaded[128] = "";
	IOHANDLE File = Storage()->OpenFile(QMCLIENT_PLAYTIME_CLIENT_ID_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File)
	{
		const int Read = io_read(File, aLoaded, sizeof(aLoaded) - 1);
		io_close(File);
		if(Read > 0)
		{
			aLoaded[Read] = '\0';
			char *pTrimmed = (char *)str_utf8_skip_whitespaces(aLoaded);
			str_utf8_trim_right(pTrimmed);
			if(IsValidQmClientPlaytimeId(pTrimmed))
				str_copy(m_aQmClientPlaytimeClientId, pTrimmed, sizeof(m_aQmClientPlaytimeClientId));
		}
	}

	if(m_aQmClientPlaytimeClientId[0] == '\0')
	{
		unsigned char aRandom[16];
		secure_random_fill(aRandom, sizeof(aRandom));

		static constexpr const char HEX[] = "0123456789abcdef";
		char aHex[sizeof(aRandom) * 2 + 1];
		for(size_t i = 0; i < sizeof(aRandom); ++i)
		{
			aHex[i * 2] = HEX[aRandom[i] >> 4];
			aHex[i * 2 + 1] = HEX[aRandom[i] & 0x0f];
		}
		aHex[sizeof(aHex) - 1] = '\0';
		str_format(m_aQmClientPlaytimeClientId, sizeof(m_aQmClientPlaytimeClientId), "qm%s", aHex);
	}

	if(!IsValidQmClientPlaytimeId(m_aQmClientPlaytimeClientId))
	{
		m_aQmClientPlaytimeClientId[0] = '\0';
		return;
	}

	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	IOHANDLE OutFile = Storage()->OpenFile(QMCLIENT_PLAYTIME_CLIENT_ID_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(OutFile)
	{
		io_write(OutFile, m_aQmClientPlaytimeClientId, str_length(m_aQmClientPlaytimeClientId));
		io_write(OutFile, "\n", 1);
		io_close(OutFile);
	}
}

void CQmClient::SendQmClientPlaytimeRequest(const char *pUrl, std::shared_ptr<CHttpRequest> &pTaskSlot, int64_t StopAt)
{
	if(!pUrl || pUrl[0] == '\0')
		return;

	EnsureQmClientPlaytimeClientId();
	if(m_aQmClientPlaytimeClientId[0] == '\0')
		return;

	CJsonStringWriter JsonWriter;
	JsonWriter.BeginObject();
	JsonWriter.WriteAttribute("client_id");
	JsonWriter.WriteStrValue(m_aQmClientPlaytimeClientId);
	JsonWriter.WriteAttribute("player_name");
	JsonWriter.WriteStrValue(g_Config.m_PlayerName);
	if(StopAt > 0)
	{
		JsonWriter.WriteAttribute("stop_at");
		const int StopAtInt = StopAt > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : (int)StopAt;
		JsonWriter.WriteIntValue(StopAtInt);
	}
	JsonWriter.EndObject();

	std::string Body = JsonWriter.GetOutputString();
	pTaskSlot = HttpPostJson(pUrl, Body.c_str());
	pTaskSlot->AllowInsecureProtocol();
	pTaskSlot->Timeout(CTimeout{3000, 0, 250, 6});
	pTaskSlot->IpResolve(IPRESOLVE::V4);
	pTaskSlot->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(pTaskSlot);
}

bool CQmClient::FinishQmClientPlaytimeTask(std::shared_ptr<CHttpRequest> &pTaskSlot, bool UpdateSessionStart)
{
	if(!pTaskSlot)
		return false;

	const bool Ok = pTaskSlot->State() == EHttpState::DONE && pTaskSlot->StatusCode() == 200;

	if(Ok)
	{
		json_value *pRoot = pTaskSlot->ResultJson();
		if(pRoot && pRoot->type == json_object)
		{
			int64_t ServerNow = 0;
			if(JsonReadNonNegativeInt64(JsonObjectField(pRoot, "ts"), ServerNow))
				m_QmClientServerNow = ServerNow;

			int64_t TotalSeconds = 0;
			if(JsonReadNonNegativeInt64(JsonObjectField(pRoot, "total_seconds"), TotalSeconds))
				m_QmClientServerPlaytimeSeconds = TotalSeconds;

			const json_value *pRunning = JsonObjectField(pRoot, "running");
			const bool Running = pRunning->type == json_boolean && json_boolean_get(pRunning);
			int64_t LastStartAt = 0;
			if(UpdateSessionStart || Running)
			{
				if(JsonReadNonNegativeInt64(JsonObjectField(pRoot, "last_start_at"), LastStartAt) && LastStartAt > 0)
					m_QmClientServerSessionStart = LastStartAt;
			}
		}
		if(pRoot)
			json_value_free(pRoot);
	}

	if(Ok && (&pTaskSlot == &m_pQmClientLifecycleStopTask || &pTaskSlot == &m_pQmClientLifecycleCrashTask))
		ClearQmClientLifecycleMarker();

	pTaskSlot = nullptr;
	return Ok;
}

void CQmClient::FinishQmClientServerTimeTask()
{
	if(!m_pQmClientServerTimeTask)
		return;

	m_QmClientServerTimeLastSync = time_get();

	if(m_pQmClientServerTimeTask->State() == EHttpState::DONE)
	{
		json_value *pRoot = m_pQmClientServerTimeTask->ResultJson();
		if(pRoot)
		{
			const json_value *pTs = JsonObjectField(pRoot, "ts");
			if(pTs != &json_value_none && pTs->type == json_integer && pTs->u.integer > 0)
			{
				m_QmClientServerNow = pTs->u.integer;
				if(m_QmClientServerSessionStart == 0)
					m_QmClientServerSessionStart = m_QmClientServerNow;
			}
			json_value_free(pRoot);
		}
	}

	m_pQmClientServerTimeTask = nullptr;
}

void CQmClient::UpdateQmClientLifecycleAndServerTime()
{
	if(m_pQmClientLifecycleStartTask && m_pQmClientLifecycleStartTask->Done())
	{
		const bool Ok = FinishQmClientPlaytimeTask(m_pQmClientLifecycleStartTask, true);
		if(!Ok)
		{
			m_QmClientStartupSent = false;
			m_QmClientStartupNextRetry = time_get() + (int64_t)QMCLIENT_RECOVERY_RETRY_SECONDS * time_freq();
		}
		else
		{
			m_QmClientStartupNextRetry = 0;
		}
	}
	if(m_pQmClientLifecycleCrashTask && m_pQmClientLifecycleCrashTask->Done())
	{
		const bool Ok = FinishQmClientPlaytimeTask(m_pQmClientLifecycleCrashTask, false);
		if(Ok)
		{
			m_QmClientAwaitingRecoveryStop = false;
			m_QmClientRecoveryNextRetry = 0;
		}
		else
		{
			m_QmClientRecoveryNextRetry = time_get() + (int64_t)QMCLIENT_RECOVERY_RETRY_SECONDS * time_freq();
		}
	}
	if(m_pQmClientLifecycleStopTask && m_pQmClientLifecycleStopTask->Done())
		FinishQmClientPlaytimeTask(m_pQmClientLifecycleStopTask, false);
	if(m_pQmClientPlaytimeQueryTask && m_pQmClientPlaytimeQueryTask->Done())
	{
		FinishQmClientPlaytimeTask(m_pQmClientPlaytimeQueryTask, false);
		m_QmClientPlaytimeLastSync = time_get();
	}

	if(m_pQmClientServerTimeTask && m_pQmClientServerTimeTask->Done())
		FinishQmClientServerTimeTask();

	if(Client()->State() == IClient::STATE_QUITTING || Client()->State() == IClient::STATE_RESTARTING)
		return;

	const int64_t Now = time_get();

	if(m_QmClientAwaitingRecoveryStop && !m_pQmClientLifecycleCrashTask &&
		(m_QmClientRecoveryNextRetry == 0 || Now >= m_QmClientRecoveryNextRetry))
	{
		SendQmClientLifecyclePing("recover_crash", m_pQmClientLifecycleCrashTask);
		if(m_pQmClientLifecycleCrashTask)
			m_QmClientRecoveryNextRetry = Now + (int64_t)QMCLIENT_RECOVERY_RETRY_SECONDS * time_freq();
	}

	if(!m_QmClientAwaitingRecoveryStop && !m_QmClientStartupSent && !m_pQmClientLifecycleStartTask &&
		(m_QmClientStartupNextRetry == 0 || Now >= m_QmClientStartupNextRetry))
	{
		m_QmClientMarkerStartedAt = time_timestamp();
		m_QmClientMarkerLastSeenAt = m_QmClientMarkerStartedAt;
		m_QmClientMarkerLastFlushTick = Now;
		WriteQmClientLifecycleMarker();

		SendQmClientLifecyclePing("startup", m_pQmClientLifecycleStartTask);
		m_QmClientStartupSent = m_pQmClientLifecycleStartTask != nullptr;
		if(m_QmClientStartupSent)
			m_QmClientStartupNextRetry = 0;
	}

	if(!m_QmClientAwaitingRecoveryStop && m_QmClientStartupSent)
		TouchQmClientLifecycleMarker(false);

	const int64_t PlaytimeIntervalTicks = (int64_t)QMCLIENT_PLAYTIME_QUERY_INTERVAL_SECONDS * time_freq();
	if(!m_pQmClientPlaytimeQueryTask && (m_QmClientPlaytimeLastSync == 0 || Now - m_QmClientPlaytimeLastSync >= PlaytimeIntervalTicks))
		SendQmClientPlaytimeRequest(QMCLIENT_PLAYTIME_QUERY_URL, m_pQmClientPlaytimeQueryTask);

	const int64_t IntervalTicks = (int64_t)QMCLIENT_SERVER_TIME_SYNC_INTERVAL_SECONDS * time_freq();
	if(m_pQmClientServerTimeTask || (m_QmClientServerTimeLastSync != 0 && Now - m_QmClientServerTimeLastSync < IntervalTicks))
		return;

	char aUrl[512];
	const int Nonce = secure_rand_below(1000000);
	str_format(aUrl, sizeof(aUrl), "%s?event=time_sync&session=%s&nonce=%d", QMCLIENT_HEALTH_URL, m_aQmClientLifecycleSessionId, Nonce);

	m_pQmClientServerTimeTask = HttpGet(aUrl);
	m_pQmClientServerTimeTask->AllowInsecureProtocol();
	m_pQmClientServerTimeTask->Timeout(CTimeout{3000, 0, 250, 5});
	m_pQmClientServerTimeTask->IpResolve(IPRESOLVE::V4);
	m_pQmClientServerTimeTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmClientServerTimeTask);
}

void CQmClient::UpdateQmDdnetPlayerStats()
{
	if(m_pQmDdnetPlayerParseJob && m_pQmDdnetPlayerParseJob->Done())
		FinishQmDdnetPlayerStats();
	if(m_pQmDdnetPlayerTask && m_pQmDdnetPlayerTask->Done())
		FinishQmDdnetPlayerStats();

	const char *pConfiguredName = g_Config.m_PlayerName;
	if(!pConfiguredName || pConfiguredName[0] == '\0')
	{
		if(m_pQmDdnetPlayerTask)
		{
			m_pQmDdnetPlayerTask->Abort();
			m_pQmDdnetPlayerTask = nullptr;
		}
		m_pQmDdnetPlayerParseJob = nullptr;
		if(m_aQmDdnetPlayerName[0] != '\0')
		{
			m_aQmDdnetPlayerName[0] = '\0';
			m_aQmDdnetFavoritePartner[0] = '\0';
			m_QmDdnetTotalFinishes = -1;
			m_QmDdnetPlayerLastSync = 0;
			m_QmDdnetPlayerNextRetry = 0;
		}
		return;
	}

	if(str_comp(m_aQmDdnetPlayerName, pConfiguredName) != 0)
	{
		if(m_pQmDdnetPlayerTask)
		{
			m_pQmDdnetPlayerTask->Abort();
			m_pQmDdnetPlayerTask = nullptr;
		}
		m_pQmDdnetPlayerParseJob = nullptr;

		str_copy(m_aQmDdnetPlayerName, pConfiguredName, sizeof(m_aQmDdnetPlayerName));
		m_aQmDdnetFavoritePartner[0] = '\0';
		m_QmDdnetTotalFinishes = -1;
		m_QmDdnetPlayerLastSync = 0;
		m_QmDdnetPlayerNextRetry = 0;
	}

	if(m_pQmDdnetPlayerParseJob)
		return;
	if(m_pQmDdnetPlayerTask)
		return;

	const int64_t Now = time_get();
	if(m_QmDdnetPlayerNextRetry != 0 && Now < m_QmDdnetPlayerNextRetry)
		return;

	if(m_QmDdnetPlayerNextRetry == 0)
	{
		const int64_t SyncIntervalTicks = (int64_t)QMCLIENT_DDNET_PLAYER_SYNC_INTERVAL_SECONDS * time_freq();
		if(m_QmDdnetPlayerLastSync != 0 && Now - m_QmDdnetPlayerLastSync < SyncIntervalTicks)
			return;
	}

	FetchQmDdnetPlayerStats(m_aQmDdnetPlayerName);
}

void CQmClient::FetchQmDdnetPlayerStats(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;
	if(m_pQmDdnetPlayerParseJob && !m_pQmDdnetPlayerParseJob->Done())
		return;
	if(m_pQmDdnetPlayerTask && !m_pQmDdnetPlayerTask->Done())
		return;

	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s", DDNET_PLAYER_STATS_URL, aEncodedName);

	m_pQmDdnetPlayerTask = HttpGet(aUrl);
	m_pQmDdnetPlayerTask->Timeout(CTimeout{10000, 30000, 100, 10});
	m_pQmDdnetPlayerTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmDdnetPlayerTask);
}

void CQmClient::FinishQmDdnetPlayerStats()
{
	if(m_pQmDdnetPlayerParseJob)
	{
		if(!m_pQmDdnetPlayerParseJob->Done())
			return;

		auto pParseJob = std::static_pointer_cast<CQmDdnetPlayerStatsParseJob>(m_pQmDdnetPlayerParseJob);
		CQmDdnetPlayerStatsParseJob::SResult Result = pParseJob->TakeResult();
		m_pQmDdnetPlayerParseJob = nullptr;

		const int64_t Now = time_get();
		if(Result.m_Parsed)
		{
			m_QmDdnetPlayerLastSync = Now;
			m_QmDdnetPlayerNextRetry = 0;
			str_copy(m_aQmDdnetFavoritePartner, Result.m_FavoritePartner.c_str(), sizeof(m_aQmDdnetFavoritePartner));
			m_QmDdnetTotalFinishes = Result.m_TotalFinishes;
		}
		else
		{
			m_QmDdnetPlayerLastSync = 0;
			m_QmDdnetPlayerNextRetry = Now + (int64_t)QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS * time_freq();
		}
		return;
	}

	if(!m_pQmDdnetPlayerTask)
		return;

	if(m_pQmDdnetPlayerTask->State() != EHttpState::DONE || m_pQmDdnetPlayerTask->StatusCode() != 200)
	{
		m_QmDdnetPlayerLastSync = 0;
		m_QmDdnetPlayerNextRetry = time_get() + (int64_t)QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS * time_freq();
		m_pQmDdnetPlayerTask = nullptr;
		return;
	}

	m_pQmDdnetPlayerParseJob = std::make_shared<CQmDdnetPlayerStatsParseJob>(m_pQmDdnetPlayerTask);
	Engine()->AddJob(m_pQmDdnetPlayerParseJob);
	m_pQmDdnetPlayerTask = nullptr;
}

void CQmClient::InitQmClientLifecycle()
{
	unsigned SessionRandom = 0;
	secure_random_fill(&SessionRandom, sizeof(SessionRandom));
	str_format(m_aQmClientLifecycleSessionId, sizeof(m_aQmClientLifecycleSessionId), "%08x%08x", (unsigned)time_timestamp(), SessionRandom);

	EnsureQmClientPlaytimeClientId();
	int64_t PreviousStartedAt = 0;
	int64_t PreviousLastSeenAt = 0;
	const bool HadPendingMarker = ReadQmClientLifecycleMarker(PreviousStartedAt, PreviousLastSeenAt);
	m_QmClientRecoveryStopAt = PreviousLastSeenAt > 0 ? PreviousLastSeenAt : PreviousStartedAt;
	if(m_QmClientRecoveryStopAt <= 0)
		m_QmClientRecoveryStopAt = time_timestamp();
	m_QmClientMarkerStartedAt = PreviousStartedAt;
	m_QmClientMarkerLastSeenAt = PreviousLastSeenAt;
	m_QmClientMarkerLastFlushTick = 0;

	m_QmClientShutdownReported = false;
	m_QmClientAwaitingRecoveryStop = HadPendingMarker;
	m_QmClientStartupSent = false;
	m_QmClientRecoveryNextRetry = 0;
	m_QmClientStartupNextRetry = 0;
	m_QmClientServerNow = 0;
	m_QmClientServerSessionStart = 0;
	m_QmClientServerTimeLastSync = 0;
	m_QmClientServerPlaytimeSeconds = -1;
	m_QmClientPlaytimeLastSync = 0;

	if(HadPendingMarker)
	{
		SendQmClientLifecyclePing("recover_crash", m_pQmClientLifecycleCrashTask);
		if(!m_pQmClientLifecycleCrashTask)
			m_QmClientRecoveryNextRetry = time_get() + (int64_t)QMCLIENT_RECOVERY_RETRY_SECONDS * time_freq();
	}
}

void CQmClient::ResetQmClientRecognitionTasks()
{
	auto AbortTask = [](std::shared_ptr<CHttpRequest> &pTask) {
		if(pTask)
		{
			pTask->Abort();
			pTask = nullptr;
		}
	};
	AbortTask(m_pQmClientAuthTokenTask);
	AbortTask(m_pQmClientUsersTask);
	AbortTask(m_pQmClientUsersSendTask);
	m_pQmClientUsersParseJob = nullptr;
	m_aQmClientAuthToken[0] = '\0';
	m_QmClientLastSync = 0;
	ClearQmClientServerDistribution();
}

bool CQmClient::NeedsQmClientRecognition() const
{
	return HasQmClientRecognitionService();
}

bool CQmClient::HasQmClientRecognitionService() const
{
	return GetEffectiveQmVoiceServer()[0] != '\0';
}

bool CQmClient::NeedsFastQmClientSync() const
{
	return g_Config.m_QmVoiceEnable != 0 || g_Config.m_QmClientShowBadge != 0 || g_Config.m_QmClientMarkTrail != 0;
}

bool CQmClient::BuildQmClientRecognitionUrl(const char *pPath, char *pBuf, size_t BufSize, const char *pQuery) const
{
	if(!pPath || pPath[0] == '\0' || !pBuf || BufSize == 0)
		return false;

	const char *pVoiceServer = GetEffectiveQmVoiceServer();
	char aHost[128];
	int Port = 0;
	if(!ParseQmClientServiceHostPort(pVoiceServer, aHost, sizeof(aHost), Port))
		return false;

	const bool NeedsIpv6Brackets = str_find(aHost, ":") != nullptr;
	if(pQuery && pQuery[0] != '\0')
	{
		if(NeedsIpv6Brackets)
			str_format(pBuf, BufSize, "http://[%s]:%d%s?%s", aHost, Port, pPath, pQuery);
		else
			str_format(pBuf, BufSize, "http://%s:%d%s?%s", aHost, Port, pPath, pQuery);
	}
	else
	{
		if(NeedsIpv6Brackets)
			str_format(pBuf, BufSize, "http://[%s]:%d%s", aHost, Port, pPath);
		else
			str_format(pBuf, BufSize, "http://%s:%d%s", aHost, Port, pPath);
	}

	return pBuf[0] != '\0';
}

void CQmClient::ClearQmClientServerDistribution()
{
	m_vQmClientServerDistribution.clear();
	m_QmClientOnlineUserCount = 0;
	m_QmClientOnlineDummyCount = 0;
}

bool CQmClient::EnsureQmClientMachineHash()
{
	if(IsValidQmClientMachineHash(m_aQmClientMachineHash))
		return true;

	std::string Identity;
	if(!ReadPlatformMachineIdentity(Identity))
	{
		char aLoaded[128] = "";
		IOHANDLE File = Storage()->OpenFile(QMCLIENT_MACHINE_ID_FALLBACK_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(File)
		{
			const int Read = io_read(File, aLoaded, sizeof(aLoaded) - 1);
			io_close(File);
			if(Read > 0)
			{
				aLoaded[Read] = '\0';
				TrimQmClientTextInPlace(aLoaded);
				if(aLoaded[0] != '\0')
					Identity = aLoaded;
			}
		}

		if(Identity.empty())
		{
			unsigned char aRandom[32];
			secure_random_fill(aRandom, sizeof(aRandom));

			static constexpr const char HEX[] = "0123456789abcdef";
			char aHex[sizeof(aRandom) * 2 + 1];
			for(size_t i = 0; i < sizeof(aRandom); ++i)
			{
				aHex[i * 2] = HEX[aRandom[i] >> 4];
				aHex[i * 2 + 1] = HEX[aRandom[i] & 0x0f];
			}
			aHex[sizeof(aHex) - 1] = '\0';
			Identity = aHex;

			Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			IOHANDLE OutFile = Storage()->OpenFile(QMCLIENT_MACHINE_ID_FALLBACK_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(OutFile)
			{
				io_write(OutFile, Identity.c_str(), Identity.size());
				io_write(OutFile, "\n", 1);
				io_close(OutFile);
			}
		}
	}

	if(Identity.empty())
		return false;

	const SHA256_DIGEST Digest = sha256(Identity.data(), Identity.size());
	sha256_str(Digest, m_aQmClientMachineHash, sizeof(m_aQmClientMachineHash));
	return IsValidQmClientMachineHash(m_aQmClientMachineHash);
}

void CQmClient::FetchQmClientAuthToken()
{
	if(m_pQmClientAuthTokenTask && !m_pQmClientAuthTokenTask->Done())
		return;

	if(!EnsureQmClientMachineHash())
		return;

	char aQuery[128];
	str_format(aQuery, sizeof(aQuery), "machine_hash=%s", m_aQmClientMachineHash);

	char aUrl[256];
	if(!BuildQmClientRecognitionUrl(QMCLIENT_TOKEN_PATH, aUrl, sizeof(aUrl), aQuery))
		return;

	m_pQmClientAuthTokenTask = HttpGet(aUrl);
	m_pQmClientAuthTokenTask->AllowInsecureProtocol();
	m_pQmClientAuthTokenTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pQmClientAuthTokenTask->IpResolve(IPRESOLVE::V4);
	m_pQmClientAuthTokenTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmClientAuthTokenTask);
}

void CQmClient::SendQmClientPlayerData()
{
	if(m_aQmClientAuthToken[0] == '\0')
		return;
	if(m_pQmClientUsersSendTask && !m_pQmClientUsersSendTask->Done())
		return;
	if(!EnsureQmClientMachineHash())
		return;

	char aServerAddress[NETADDR_MAXSTRSIZE] = "";
	if(Client()->State() == IClient::STATE_ONLINE)
		net_addr_str(&Client()->ServerAddress(), aServerAddress, sizeof(aServerAddress), true);
	if(aServerAddress[0] == '\0')
		return;

	CJsonStringWriter JsonWriter;
	JsonWriter.BeginObject();
	JsonWriter.WriteAttribute("server_address");
	JsonWriter.WriteStrValue(aServerAddress);
	JsonWriter.WriteAttribute("auth_token");
	JsonWriter.WriteStrValue(m_aQmClientAuthToken);
	JsonWriter.WriteAttribute("machine_hash");
	JsonWriter.WriteStrValue(m_aQmClientMachineHash);
	JsonWriter.WriteAttribute("timestamp");
	JsonWriter.WriteIntValue((int)time_timestamp());
	JsonWriter.WriteAttribute("players");
	JsonWriter.BeginArray();

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
			continue;

		JsonWriter.BeginObject();
		JsonWriter.WriteAttribute("player_id");
		JsonWriter.WriteIntValue(ClientId);
		JsonWriter.WriteAttribute("dummy");
		JsonWriter.WriteBoolValue(Dummy == 1);
		JsonWriter.WriteAttribute("foot_particles_enabled");
		JsonWriter.WriteBoolValue(g_Config.m_QmFootParticles != 0);
		JsonWriter.WriteAttribute("remote_particles_enabled");
		JsonWriter.WriteBoolValue(g_Config.m_QmClientMarkTrail != 0);
		JsonWriter.WriteAttribute("voice_supported");
		JsonWriter.WriteBoolValue(true);
		JsonWriter.EndObject();
	}

	JsonWriter.EndArray();
	JsonWriter.EndObject();

	std::string JsonBody = JsonWriter.GetOutputString();
	char aUrl[256];
	if(!BuildQmClientRecognitionUrl(QMCLIENT_REPORT_PATH, aUrl, sizeof(aUrl)))
		return;

	m_pQmClientUsersSendTask = HttpPostJson(aUrl, JsonBody.c_str());
	m_pQmClientUsersSendTask->AllowInsecureProtocol();
	m_pQmClientUsersSendTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pQmClientUsersSendTask->IpResolve(IPRESOLVE::V4);
	m_pQmClientUsersSendTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmClientUsersSendTask);
}

void CQmClient::FetchQmClientUsers()
{
	if(m_pQmClientUsersTask && !m_pQmClientUsersTask->Done())
		return;

	char aUrl[256];
	if(!BuildQmClientRecognitionUrl(QMCLIENT_USERS_PATH, aUrl, sizeof(aUrl)))
		return;

	m_pQmClientUsersTask = HttpGet(aUrl);
	m_pQmClientUsersTask->AllowInsecureProtocol();
	m_pQmClientUsersTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pQmClientUsersTask->IpResolve(IPRESOLVE::V4);
	m_pQmClientUsersTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmClientUsersTask);
}

void CQmClient::FinishQmClientAuthToken()
{
	if(!m_pQmClientAuthTokenTask || m_pQmClientAuthTokenTask->State() != EHttpState::DONE)
		return;

	json_value *pRoot = m_pQmClientAuthTokenTask->ResultJson();
	if(!pRoot)
		return;

	const json_value *pToken = JsonObjectField(pRoot, "auth_token");
	if(pToken == &json_value_none)
		pToken = JsonObjectField(pRoot, "token");
	if(pToken == &json_value_none)
		pToken = JsonObjectField(pRoot, "qid");
	if(pToken != &json_value_none && pToken->type == json_string)
	{
		str_copy(m_aQmClientAuthToken, pToken->u.string.ptr, sizeof(m_aQmClientAuthToken));
		m_QmClientLastSync = 0;
	}
	json_value_free(pRoot);
}

void CQmClient::FinishQmClientUsers()
{
	if(m_pQmClientUsersParseJob)
	{
		if(!m_pQmClientUsersParseJob->Done())
			return;

		auto pParseJob = std::static_pointer_cast<CQmClientUsersParseJob>(m_pQmClientUsersParseJob);
		const int64_t ExpireTick = pParseJob->ExpireTick();
		CQmClientUsersParseJob::SResult Result = pParseJob->TakeResult();
		m_pQmClientUsersParseJob = nullptr;

		GameClient()->ClearQ1menGSyncMarks();
		GameClient()->ClearQmVoiceSyncMarks();
		ClearQmClientServerDistribution();

		if(!Result.m_Parsed)
			return;

		m_vQmClientServerDistribution = std::move(Result.m_vServerDistribution);
		m_QmClientOnlineUserCount = Result.m_OnlineUserCount;
		m_QmClientOnlineDummyCount = Result.m_OnlineDummyCount;
		for(const auto &Mark : Result.m_vLocalServerMarks)
		{
			GameClient()->MarkQ1menGSyncClient(Mark.m_ClientId, ExpireTick, Mark.m_FootParticlesEnabled, Mark.m_RemoteParticlesEnabled, Mark.m_Qid.c_str());
			if(Mark.m_VoiceSupported)
				GameClient()->MarkQmVoiceSupportedClient(Mark.m_ClientId, ExpireTick);
		}
		return;
	}

	GameClient()->ClearQ1menGSyncMarks();
	GameClient()->ClearQmVoiceSyncMarks();

	if(!m_pQmClientUsersTask)
	{
		ClearQmClientServerDistribution();
		return;
	}

	if(m_pQmClientUsersTask->State() != EHttpState::DONE)
	{
		GameClient()->ClearQ1menGSyncMarks();
		GameClient()->ClearQmVoiceSyncMarks();
		ClearQmClientServerDistribution();
		m_pQmClientUsersTask = nullptr;
		return;
	}

	char aServerAddress[NETADDR_MAXSTRSIZE] = "";
	if(Client()->State() == IClient::STATE_ONLINE)
		net_addr_str(&Client()->ServerAddress(), aServerAddress, sizeof(aServerAddress), true);
	const bool FastSync = NeedsFastQmClientSync();
	const int SyncInterval = FastSync ? QMCLIENT_VOICE_SYNC_INTERVAL_SECONDS : QMCLIENT_SYNC_INTERVAL_SECONDS;
	const int64_t ExpireTick = time_get() + (int64_t)SyncInterval * time_freq() * 2;
	m_pQmClientUsersParseJob = std::make_shared<CQmClientUsersParseJob>(m_pQmClientUsersTask, aServerAddress, ExpireTick);
	Engine()->AddJob(m_pQmClientUsersParseJob);
	m_pQmClientUsersTask = nullptr;
}

void CQmClient::SyncQmClientUsers()
{
	if(m_pQmClientUsersTask && !m_pQmClientUsersTask->Done())
		return;
	if(m_pQmClientUsersParseJob && !m_pQmClientUsersParseJob->Done())
		return;
	if(Client()->State() == IClient::STATE_ONLINE && m_pQmClientUsersSendTask && !m_pQmClientUsersSendTask->Done())
		return;

	if(m_aQmClientAuthToken[0] == '\0')
	{
		FetchQmClientAuthToken();
		return;
	}

	if(Client()->State() == IClient::STATE_ONLINE)
		SendQmClientPlayerData();
	FetchQmClientUsers();
}

void CQmClient::UpdateQmClientRecognition()
{
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	if(!Online)
	{
		GameClient()->ClearQ1menGSyncMarks();
		GameClient()->ClearQmVoiceSyncMarks();
	}

	if(m_pQmClientAuthTokenTask && m_pQmClientAuthTokenTask->Done())
	{
		FinishQmClientAuthToken();
		m_pQmClientAuthTokenTask = nullptr;
	}
	if(m_pQmClientUsersParseJob && m_pQmClientUsersParseJob->Done())
	{
		FinishQmClientUsers();
	}
	if(m_pQmClientUsersTask && m_pQmClientUsersTask->Done())
	{
		FinishQmClientUsers();
	}
	if(m_pQmClientUsersSendTask && m_pQmClientUsersSendTask->Done())
	{
		// Report can fail after center service restart (stale auth token).
		// Clear token to force refetch on the next sync cycle.
		if(m_pQmClientUsersSendTask->State() != EHttpState::DONE || m_pQmClientUsersSendTask->StatusCode() == 401)
			m_aQmClientAuthToken[0] = '\0';
		m_pQmClientUsersSendTask = nullptr;
	}

	const bool NeedRecognition = NeedsQmClientRecognition();
	if(!NeedRecognition)
	{
		if(m_pQmClientAuthTokenTask || m_pQmClientUsersTask || m_pQmClientUsersSendTask || m_pQmClientUsersParseJob || m_aQmClientAuthToken[0] != '\0' || m_QmClientLastSync != 0)
			ResetQmClientRecognitionTasks();
		GameClient()->ClearQ1menGSyncMarks();
		GameClient()->ClearQmVoiceSyncMarks();
		return;
	}

	// Center sync endpoint is intentionally plain HTTP.
	// Ensure libcurl allows HTTP protocol while this feature is enabled.
	if(!g_Config.m_HttpAllowInsecure)
		g_Config.m_HttpAllowInsecure = 1;

	const bool FastSync = g_Config.m_QmVoiceEnable || g_Config.m_QmClientShowBadge;
	const int SyncInterval = FastSync ? QMCLIENT_VOICE_SYNC_INTERVAL_SECONDS : QMCLIENT_SYNC_INTERVAL_SECONDS;
	const int64_t IntervalTicks = (int64_t)SyncInterval * time_freq();
	const int64_t Now = time_get();

	if(m_QmClientLastSync == 0 || Now - m_QmClientLastSync >= IntervalTicks)
	{
		SyncQmClientUsers();
		m_QmClientLastSync = Now;
	}
}
