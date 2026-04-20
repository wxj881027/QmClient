#include "qmclient.h"

#include "data_version.h"

#include <base/hash.h>
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

static constexpr const char *TCLIENT_INFO_URL = "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/docs/info.json";
static constexpr const char *TCLIENT_UPDATE_EXE_URL = "https://github.com/wxj881027/Q1menG_Client/releases/latest/download/DDNet.exe";
static constexpr const char *MAP_CATEGORY_CACHE_FILE = "qmclient/map_categories.json";
static constexpr int64_t MAP_CATEGORY_CACHE_SAVE_DELAY_SEC = 5;
static constexpr int QMCLIENT_SYNC_INTERVAL_SECONDS = 30;
static constexpr int QMCLIENT_VOICE_SYNC_INTERVAL_SECONDS = 10;
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
static constexpr float QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION = 2.0f;
static constexpr float QMCLIENT_COMBO_POPUP_DURATION = 1.25f;
static constexpr float QMCLIENT_TEXT_POPUP_FONT_SIZE = 30.0f;
static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET = vec2(34.0f, -78.0f);
static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT = vec2(18.0f, -16.0f);
static constexpr int QMCLIENT_COMBO_POPUP_WINDOW_SECONDS = 2;
static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_FROM = ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f);
static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_TO = ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f);
static constexpr ColorRGBA QMCLIENT_COMBO_POPUP_COLOR = ColorRGBA(1.0f, 0.96f, 0.45f, 1.0f);
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
static constexpr const char *s_pFriendEnterBroadcastDefaultText = "%s joined this server";

static int AutoReplySeparatorLength(const char *pStr);
static void AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules);
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

static constexpr std::array<STextPopupDefinition, (int)ETextPopupType::NUM_TYPES> s_aTextPopupDefinitions = {{
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
	std::mutex m_Mutex;
	SResult m_Result;

	void Run() override
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
			std::lock_guard<std::mutex> Lock(m_Mutex);
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

	SResult TakeResult()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
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
	std::mutex m_Mutex;
	SResult m_Result;

	void Run() override
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
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Result = std::move(Result);
		}
		m_pTask = nullptr;
	}

public:
	explicit CQmDdnetPlayerStatsParseJob(std::shared_ptr<CHttpRequest> pTask) :
		m_pTask(std::move(pTask))
	{
	}

	SResult TakeResult()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
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

int ComboPopupTextTypeFromCount(int ComboCount)
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

float TextPopupDuration(int TextType)
{
	return IsComboPopupTextType(TextType) ? QMCLIENT_COMBO_POPUP_DURATION : QMCLIENT_FREEZE_WAKEUP_POPUP_DURATION;
}

EFreezeWakeupType DetectFreezeWakeupType(CGameClient *pGameClient, int ClientId)
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

static void ParseKeywordReplyRules(const char *pRules, std::vector<SKeywordReplyRule> &vOutRules)
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

static void BuildKeywordReplyRules(const std::vector<SKeywordReplyRule> &vRules, char *pOutRules, size_t OutRulesSize)
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

static bool ReadQmClientAbsoluteTextFile(const char *pFilename, char *pBuf, size_t BufSize)
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

static bool IsHardBlockedForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_SOLID || TileIndex == TILE_NOHOOK;
}

static bool IsPenaltyTileForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_DEATH || TileIndex == TILE_FREEZE || TileIndex == TILE_DFREEZE || TileIndex == TILE_LFREEZE;
}

static bool IsRewardTileForGoresDistanceField(int TileIndex)
{
	return TileIndex == TILE_UNFREEZE || TileIndex == TILE_DUNFREEZE || TileIndex == TILE_LUNFREEZE;
}

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
	FetchTClientInfo();

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

	// 兼容旧版恰分配置：将旧规则并入关键词回复，旧隐式关键词和预设触发词不再保留。
	{
		bool MigratedLegacyAutoReply = false;
		char aMergedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		str_copy(aMergedRules, g_Config.m_QmKeywordReplyRules, sizeof(aMergedRules));

		if(g_Config.m_QmQiaFenEnabled)
		{
			g_Config.m_QmKeywordReplyEnabled = 1;
			g_Config.m_QmQiaFenEnabled = 0;
			MigratedLegacyAutoReply = true;
		}

		if(g_Config.m_QmQiaFenUseDummy)
		{
			g_Config.m_QmKeywordReplyUseDummy = 1;
			g_Config.m_QmQiaFenUseDummy = 0;
			MigratedLegacyAutoReply = true;
		}

		if(g_Config.m_QmQiaFenRules[0] != '\0')
		{
			AppendAutoReplyRuleBlock(aMergedRules, sizeof(aMergedRules), g_Config.m_QmQiaFenRules);
			g_Config.m_QmQiaFenRules[0] = '\0';
			MigratedLegacyAutoReply = true;
		}

		if(g_Config.m_QmQiaFenKeywords[0] != '\0')
		{
			g_Config.m_QmQiaFenKeywords[0] = '\0';
			MigratedLegacyAutoReply = true;
		}

		if(g_Config.m_QmKeywordReplyAutoRename)
		{
			std::vector<SKeywordReplyRule> vRules;
			ParseKeywordReplyRules(aMergedRules, vRules);
			for(auto &Rule : vRules)
			{
				if(!Rule.m_HasExplicitRenameFlag)
					Rule.m_AutoRename = true;
			}

			char aMigratedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
			BuildKeywordReplyRules(vRules, aMigratedRules, sizeof(aMigratedRules));
			str_copy(aMergedRules, aMigratedRules, sizeof(aMergedRules));
			g_Config.m_QmKeywordReplyAutoRename = 0;
			MigratedLegacyAutoReply = true;
		}

		if(MigratedLegacyAutoReply)
			str_copy(g_Config.m_QmKeywordReplyRules, aMergedRules, sizeof(g_Config.m_QmKeywordReplyRules));
	}

	InitQmClientLifecycle();
}

void CTClient::OnShutdown()
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
	m_pQmClientUsersParseJob = nullptr;
	m_pQmDdnetPlayerParseJob = nullptr;
	m_pQmClientLifecycleMarkerWriteJob = nullptr;
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

static void AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules)
{
	if(!pOutRules || OutRulesSize == 0 || !pRules || pRules[0] == '\0')
		return;

	if(pOutRules[0] != '\0')
	{
		const int Len = str_length(pOutRules);
		if(Len > 0 && pOutRules[Len - 1] != '\n')
			str_append(pOutRules, "\n", OutRulesSize);
	}

	str_append(pOutRules, pRules, OutRulesSize);
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
	const IServerBrowser::CServerEntry *pEntry = pServerBrowser->Find(Client()->ServerAddress());
	if(pEntry)
		pCommunityId = pEntry->m_Info.m_aCommunityId;
	else if(GameClient()->m_ConnectServerInfo)
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;

	if(!pCommunityId || pCommunityId[0] == '\0')
		return nullptr;
	return pCommunityId;
}

void CTClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;

		if(ClientId < 0)
		{
			if(pMsg->m_pMessage)
			{
				const char *pText = pMsg->m_pMessage;
				if(str_find_nocase(pText, "has requested to swap with you"))
				{
					StartSwapCountdown();
				}
				else if(str_find_nocase(pText, "has canceled swap with you"))
				{
					ClearSwapCountdown();
				}
				else if(str_find_nocase(pText, "has swapped with"))
				{
					const char *pMainName = g_Config.m_PlayerName;
					const char *pDummyName = g_Config.m_ClDummyName;
					const bool MainMatch = pMainName[0] && str_find_nocase(pText, pMainName);
					const bool DummyMatch = Client()->DummyConnected() && pDummyName[0] && str_find_nocase(pText, pDummyName);
					if(MainMatch || DummyMatch)
					{
						ClearSwapCountdown();
					}
				}
			}
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

		// === 复读功能: 保存最新的公屏消息 ===
		if(ClientId >= 0 && ClientId < MAX_CLIENTS && pMsg->m_Team == 0 && pMsg->m_pMessage != nullptr)
		{
			const char *pMessage = pMsg->m_pMessage;
			const bool IsValidCandidate = pMessage[0] != '\0' && pMessage[0] != '/';
			const bool SenderIsActiveClient = GameClient()->m_aClients[ClientId].m_Active;
			const bool IsRepeatCandidate = !IsOwnMessage && SenderIsActiveClient && IsValidCandidate;
			// 保存最新的公屏消息（不是自己发的）
			if(IsRepeatCandidate)
			{
				str_copy(m_aLastChatMessage, pMessage, sizeof(m_aLastChatMessage));
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
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
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
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
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

			if(g_Config.m_TcAutoVoteWhenFar && (MapVote || RandomMapVote))
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
							GameClient()->m_Voting.Vote(-1);
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
		GameClient()->m_Chat.m_ServerCommandsNeedSorting = true;
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

void CTClient::StartSwapCountdown()
{
	m_SwapCountdownActive = true;
	m_SwapCountdownStartTick = Client()->GameTick(g_Config.m_ClDummy);
}

void CTClient::ClearSwapCountdown()
{
	m_SwapCountdownActive = false;
	m_SwapCountdownStartTick = 0;
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
		GameClient()->Echo("You are not in practice");
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

	GameClient()->Echo("No safe position found");
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
	const auto &Skins = pThis->GameClient()->m_Skins.SkinList().Skins();
	str_copy(g_Config.m_ClPlayerSkin, Skins[std::rand() % (int)Skins.size()].SkinContainer()->Name());
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

void CTClient::DoFinishCheck()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_TcChangeNameNearFinish <= 0)
	{
		m_FinishTextTimeout = 0.0f;
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
		GameClient()->m_aCheckInfo[Conn] = Client()->GameTickSpeed(); // 1 second
	};

	const int Dummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = GameClient()->m_aLocalIds[Dummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;
	const auto &Player = GameClient()->m_aClients[LocalId];
	if(!Player.m_Active)
		return;
	const char *pNewName = g_Config.m_TcFinishName;
	if(!pNewName || pNewName[0] == '\0')
		return;
	if(str_comp(Player.m_aName, pNewName) == 0)
		return;
	if(!NearFinishTile(Player.m_RenderPos, TILE_FINISH))
		return;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), Localize("Changing name to %s near finish"), pNewName);
	GameClient()->Echo(aBuf);
	SendUrgentRename(Dummy, pNewName);
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
	if(m_QmAspectApplyPending)
	{
		m_QmAspectApplyPending = false;
		SetForcedAspect();
	}

	if(m_pTClientInfoTask)
	{
		if(m_pTClientInfoTask->Done())
		{
			const bool InfoOk = m_pTClientInfoTask->State() == EHttpState::DONE;
			if(InfoOk)
			{
				FinishTClientInfo();
			}
			ResetTClientInfoTask();

			if(m_AutoUpdateAfterCheck)
			{
				if(!InfoOk || !m_FetchedTClientInfo)
				{
					Client()->AddWarning(SWarning(Localize("Update"), Localize("Failed to check for updates")));
				}
				else if(!NeedUpdate())
				{
					Client()->AddWarning(SWarning(Localize("Update"), Localize("You are already on the latest version")));
				}
				else
				{
					StartUpdateDownload();
				}
				m_AutoUpdateAfterCheck = false;
			}
		}
	}
	if(m_pUpdateExeTask && m_pUpdateExeTask->Done())
	{
		const bool DownloadOk = m_pUpdateExeTask->State() == EHttpState::DONE;
		bool ReplaceOk = false;
		if(DownloadOk)
		{
			ReplaceOk = ReplaceClientFromUpdate();
			if(ReplaceOk)
			{
				if(Client()->State() == IClient::STATE_ONLINE || Client()->EditorHasUnsavedData())
				{
					Client()->AddWarning(SWarning(Localize("Update"), Localize("Update finished. Please restart the client")));
				}
				else
				{
					Client()->AddWarning(SWarning(Localize("Update"), Localize("Update finished. Restarting...")));
					Client()->Restart();
				}
			}
		}

		if(!DownloadOk || !ReplaceOk)
		{
			if(m_aUpdateExeTmp[0] != '\0')
				Storage()->RemoveBinaryFile(m_aUpdateExeTmp);
			Client()->AddWarning(SWarning(Localize("Update"), Localize("Update failed. Please try again")));
		}

		ResetUpdateExeTask();
	}

	UpdateQmClientRecognition();
	UpdateQmClientLifecycleAndServerTime();
	UpdateQmDdnetPlayerStats();

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
		CheckFreezeWakeupPopup();
		CheckComboPopup();
		CheckWaterFall();
		CheckAutoUnspecOnUnfreeze(); // 检测解冻自动取消旁观
		CheckAutoSwitchOnUnfreeze(); // HJ大佬辅助 - 检测自动切换
		CheckAutoCloseChatOnUnfreeze(); // HJ大佬辅助 - 检测解冻后关闭聊天
		UpdatePlayerStats(); // 更新玩家统计
		UpdateGoresWeaponCycle(); // Gores 锤枪自动切换
		UpdateGoresMapProgress(); // 更新 Gores 地图路径进度
	}

	MaybeSaveMapCategoryCache();
}

void CTClient::OnRender()
{
	RenderGoresDebugRoute();
}

bool CTClient::ReadQmClientLifecycleMarker(int64_t &OutStartedAt, int64_t &OutLastSeenAt)
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
		else if(const char *pValue = str_startswith(aLine, "last_seen_at="))
			OutLastSeenAt = maximum<int64_t>(0, str_toint(pValue));
	}
	return true;
}

void CTClient::WriteQmClientLifecycleMarker()
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

void CTClient::TouchQmClientLifecycleMarker(bool ForceWrite)
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

void CTClient::ClearQmClientLifecycleMarker()
{
	Storage()->RemoveFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE);
}

void CTClient::SendQmClientLifecyclePing(const char *pEvent, std::shared_ptr<CHttpRequest> &pTaskSlot)
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

void CTClient::EnsureQmClientPlaytimeClientId()
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

void CTClient::SendQmClientPlaytimeRequest(const char *pUrl, std::shared_ptr<CHttpRequest> &pTaskSlot, int64_t StopAt)
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

bool CTClient::FinishQmClientPlaytimeTask(std::shared_ptr<CHttpRequest> &pTaskSlot, bool UpdateSessionStart)
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

void CTClient::FinishQmClientServerTimeTask()
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

void CTClient::UpdateQmClientLifecycleAndServerTime()
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

void CTClient::UpdateQmDdnetPlayerStats()
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

void CTClient::FetchQmDdnetPlayerStats(const char *pPlayerName)
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

void CTClient::FinishQmDdnetPlayerStats()
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

void CTClient::InitQmClientLifecycle()
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

void CTClient::ResetQmClientRecognitionTasks()
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

bool CTClient::NeedsQmClientRecognition() const
{
	return g_Config.m_RiVoiceServer[0] != '\0';
}

bool CTClient::NeedsFastQmClientSync() const
{
	return g_Config.m_RiVoiceEnable != 0 || g_Config.m_QmClientShowBadge != 0 || g_Config.m_QmClientMarkTrail != 0;
}

bool CTClient::BuildQmClientRecognitionUrl(const char *pPath, char *pBuf, size_t BufSize, const char *pQuery) const
{
	if(!pPath || pPath[0] == '\0' || !pBuf || BufSize == 0)
		return false;
	if(g_Config.m_QmVoiceServer[0] == '\0')
		return false;

	char aHost[128];
	int Port = 0;
	if(!ParseQmClientServiceHostPort(g_Config.m_QmVoiceServer, aHost, sizeof(aHost), Port))
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

void CTClient::ClearQmClientServerDistribution()
{
	m_vQmClientServerDistribution.clear();
	m_QmClientOnlineUserCount = 0;
	m_QmClientOnlineDummyCount = 0;
}

bool CTClient::EnsureQmClientMachineHash()
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

void CTClient::FetchQmClientAuthToken()
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

void CTClient::SendQmClientPlayerData()
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
		JsonWriter.WriteBoolValue(g_Config.m_QmcFootParticles != 0);
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

void CTClient::FetchQmClientUsers()
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

void CTClient::FinishQmClientAuthToken()
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

void CTClient::FinishQmClientUsers()
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

	char aServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aServerAddress, sizeof(aServerAddress), true);
	const bool FastSync = g_Config.m_QmVoiceEnable || g_Config.m_QmClientShowBadge;
	const int SyncInterval = FastSync ? QMCLIENT_VOICE_SYNC_INTERVAL_SECONDS : QMCLIENT_SYNC_INTERVAL_SECONDS;
	const int64_t ExpireTick = time_get() + (int64_t)SyncInterval * time_freq() * 2;

	GameClient()->ClearQ1menGSyncMarks();
	GameClient()->ClearQmVoiceSyncMarks();

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

void CTClient::SyncQmClientUsers()
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

void CTClient::UpdateQmClientRecognition()
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

	const bool NeedRecognition = g_Config.m_QmVoiceServer[0] != '\0';
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

void CTClient::CheckFreezeWakeupPopup()
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
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
			m_aWasInFreezeForWakeupPopup[Dummy] = false;
		return;
	}

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
		{
			m_aWasInFreezeForWakeupPopup[Dummy] = false;
			continue;
		}

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
		{
			m_aWasInFreezeForWakeupPopup[Dummy] = false;
			continue;
		}

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
		{
			m_aWasInFreezeForWakeupPopup[Dummy] = false;
			continue;
		}

		const bool IsInFreeze = ClientData.m_FreezeEnd != 0;
		if(m_aWasInFreezeForWakeupPopup[Dummy] && !IsInFreeze)
		{
			if(DetectFreezeWakeupType(GameClient(), ClientId) == EFreezeWakeupType::EXTERNAL_HAMMER)
				AddFreezeWakeupPopup(Dummy);
		}

		m_aWasInFreezeForWakeupPopup[Dummy] = IsInFreeze;
	}
}

void CTClient::ResetComboState(int Dummy)
{
	auto ResetOne = [&](int Index) {
		m_aComboPopupCount[Index] = 0;
		m_aComboLastEventTick[Index] = -1;
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
		for(auto &Popup : m_aFreezeWakeupPopups)
		{
			if(Popup.m_Active && IsComboPopupTextType(Popup.m_TextType))
				Popup.m_Active = false;
		}
		ResetComboState();
		return;
	}

	const int ComboWindowTicks = maximum(1, Client()->GameTickSpeed() * QMCLIENT_COMBO_POPUP_WINDOW_SECONDS);
	auto RegisterComboEvent = [&](int Dummy, int AnchorClientId, int TargetPlayer) {
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

		if(m_aComboPopupCount[Dummy] >= 2)
			AddTextPopup(AnchorClientId, ComboPopupTextTypeFromCount(m_aComboPopupCount[Dummy]), true, QMCLIENT_COMBO_POPUP_COLOR);
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
		if(pLocalChar == nullptr)
		{
			ResetComboState(Dummy);
			continue;
		}

		const int HookedPlayer = pLocalChar->Core()->HookedPlayer();
		if(HookedPlayer >= 0 && HookedPlayer != ClientId && HookedPlayer != m_aComboLastHookedPlayer[Dummy])
			RegisterComboEvent(Dummy, ClientId, HookedPlayer);
		m_aComboLastHookedPlayer[Dummy] = HookedPlayer >= 0 ? HookedPlayer : -1;

		const int CurrentTick = Client()->GameTick(Dummy);
		for(int TargetId = 0; TargetId < MAX_CLIENTS; ++TargetId)
		{
			if(TargetId == ClientId)
				continue;

			const CCharacter *pTargetChar = GameClient()->m_PredictedWorld.GetCharacterById(TargetId);
			if(pTargetChar == nullptr)
				continue;

			if(pTargetChar->GetLastDamageTick() == CurrentTick &&
				pTargetChar->GetLastDamageFrom() == ClientId &&
				pTargetChar->GetLastDamageWeapon() == WEAPON_HAMMER)
			{
				RegisterComboEvent(Dummy, ClientId, TargetId);
			}
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
	if(m_FriendNotifyPrevEnabled != Enabled || m_FriendNotifyPrevIgnoreClan != IgnoreClanSetting)
	{
		m_FriendNotifyPrevEnabled = Enabled;
		m_FriendNotifyPrevIgnoreClan = IgnoreClanSetting;
		m_FriendNotifyNextCheck = 0.0f;
		m_FriendOnline.clear();
		m_FriendNotifyScanRunning = false;
		m_FriendNotifyScanIndex = 0;
		m_FriendNotifyScanId = 0;
		m_FriendAutoRefreshNext = 0.0f;
	}

	if(!Enabled)
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

	const float RefreshInterval = maximum(5.0f, (float)g_Config.m_QmFriendOnlineRefreshSeconds);
	if(Now >= m_FriendAutoRefreshNext && !pServerBrowser->IsGettingServerlist())
	{
		const int CurrentType = pServerBrowser->GetCurrentType();
		if(g_Config.m_QmFriendOnlineAutoRefresh && CurrentType != IServerBrowser::TYPE_LAN)
			pServerBrowser->Refresh(CurrentType, false);
		else
			pServerBrowser->RefreshHttpServerList();
		m_FriendAutoRefreshNext = Now + RefreshInterval;
	}

	if(GameClient()->Friends()->NumFriends() <= 0)
	{
		m_FriendOnline.clear();
		m_FriendNotifyScanRunning = false;
		m_FriendNotifyScanIndex = 0;
		m_FriendNotifyScanId = 0;
		return;
	}

	constexpr float FriendOfflineTimeout = 10.0f;
	auto PruneFriendOffline = [&]() {
		for(auto It = m_FriendOnline.begin(); It != m_FriendOnline.end();)
		{
			if(Now - It->second.m_LastSeen > FriendOfflineTimeout)
				It = m_FriendOnline.erase(It);
			else
				++It;
		}
	};

	const bool IgnoreClan = IgnoreClanSetting != 0;
	if(!m_FriendNotifyScanRunning)
	{
		if(Now < m_FriendNotifyNextCheck)
		{
			PruneFriendOffline();
			return;
		}

		m_FriendNotifyScanRunning = true;
		m_FriendNotifyScanIndex = 0;
		++m_FriendNotifyScanId;
		if(m_FriendNotifyScanId <= 0)
			m_FriendNotifyScanId = 1;
	}

	const int NumServers = pServerBrowser->NumHttpServers();
	if(NumServers <= 0)
	{
		m_FriendNotifyScanRunning = false;
		m_FriendNotifyScanIndex = 0;
		m_FriendNotifyNextCheck = Now + 1.0f;
	}
	else
	{
		constexpr int ServersPerFrame = 32;
		int ProcessedServers = 0;
		std::string Key;
		Key.reserve(MAX_NAME_LENGTH + MAX_CLAN_LENGTH + 1);
		while(m_FriendNotifyScanIndex < NumServers && ProcessedServers < ServersPerFrame)
		{
			const CServerInfo *pEntry = pServerBrowser->HttpGet(m_FriendNotifyScanIndex);
			++m_FriendNotifyScanIndex;
			++ProcessedServers;
			if(!pEntry || pEntry->m_NumReceivedClients <= 0)
				continue;

			for(int ClientIndex = 0; ClientIndex < pEntry->m_NumReceivedClients; ++ClientIndex)
			{
				const CServerInfo::CClient &Client = pEntry->m_aClients[ClientIndex];
				if(Client.m_aName[0] == '\0')
					continue;
				if(!GameClient()->Friends()->IsFriend(Client.m_aName, Client.m_aClan, true))
					continue;

				BuildFriendNotifyKey(Client.m_aName, Client.m_aClan, IgnoreClan, Key);
				auto It = m_FriendOnline.find(Key);
				if(It == m_FriendOnline.end())
				{
					char aBuf[256];
					const char *pMap = pEntry->m_aMap[0] != '\0' ? pEntry->m_aMap : Localize("Unknown");
				str_format(aBuf, sizeof(aBuf), Localize("Your friend %s is online and currently on map %s!"), Client.m_aName, pMap);
					GameClient()->m_Chat.Echo(aBuf);
					SFriendOnlineState State;
					State.m_LastSeen = Now;
					State.m_Name = Client.m_aName;
					State.m_Map = pEntry->m_aMap;
					State.m_LastSeenScanId = m_FriendNotifyScanId;
					m_FriendOnline.emplace(Key, std::move(State));
				}
				else
				{
					It->second.m_LastSeenScanId = m_FriendNotifyScanId;
					if(It->second.m_Name != Client.m_aName)
						It->second.m_Name = Client.m_aName;
					if(It->second.m_Map != pEntry->m_aMap)
						It->second.m_Map = pEntry->m_aMap;
				}
			}
		}

		if(m_FriendNotifyScanIndex >= NumServers)
		{
			for(auto It = m_FriendOnline.begin(); It != m_FriendOnline.end();)
			{
				if(It->second.m_LastSeenScanId == m_FriendNotifyScanId)
				{
					It->second.m_LastSeen = Now;
					++It;
				}
				else if(Now - It->second.m_LastSeen > FriendOfflineTimeout)
				{
					It = m_FriendOnline.erase(It);
				}
				else
				{
					++It;
				}
			}
			m_FriendNotifyScanRunning = false;
			m_FriendNotifyScanIndex = 0;
			m_FriendNotifyNextCheck = Now + 1.0f;
		}
	}
}

void CTClient::CheckFriendEnterGreet()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_FriendEnterInitialized || !m_FriendEnterOnline.empty())
		{
			m_FriendEnterOnline.clear();
			m_FriendEnterInitialized = false;
		}
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
		m_FriendEnterNextCheck = 0.0f;
		return;
	}

	const bool AutoGreetEnabled = g_Config.m_QmFriendEnterAutoGreet != 0;
	const bool BroadcastEnabled = g_Config.m_QmFriendEnterBroadcast != 0;
	const int IgnoreClanSetting = g_Config.m_ClFriendsIgnoreClan;
	const int EnabledMask = (AutoGreetEnabled ? 1 : 0) | (BroadcastEnabled ? 2 : 0);
	if(m_FriendEnterPrevEnabled != EnabledMask || m_FriendEnterPrevIgnoreClan != IgnoreClanSetting)
	{
		m_FriendEnterPrevEnabled = EnabledMask;
		m_FriendEnterPrevIgnoreClan = IgnoreClanSetting;
		m_FriendEnterOnline.clear();
		m_FriendEnterInitialized = false;
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
		m_FriendEnterNextCheck = 0.0f;
	}

	if(!AutoGreetEnabled && !BroadcastEnabled)
	{
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
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
		if(g_Config.m_QmFriendEnterGreetText[0] != '\0')
		{
			char aMsg[256];
			aMsg[0] = '\0';
			str_append(aMsg, m_FriendEnterPendingNames.c_str(), sizeof(aMsg));
			if(aMsg[0] != '\0')
				str_append(aMsg, ": ", sizeof(aMsg));
			str_append(aMsg, g_Config.m_QmFriendEnterGreetText, sizeof(aMsg));

			if(aMsg[0] != '\0')
				GameClient()->m_Chat.SendChat(0, aMsg);
		}
		m_FriendEnterPendingNames.clear();
		m_FriendEnterPendingSendAt = 0.0f;
	}

	if(GameClient()->Friends()->NumFriends() <= 0)
	{
		m_FriendEnterOnline.clear();
		m_FriendEnterInitialized = false;
		return;
	}

	if(Now < m_FriendEnterNextCheck)
		return;
	m_FriendEnterNextCheck = Now + 0.2f;

	std::unordered_set<std::string> CurrentFriends;
	CurrentFriends.reserve(32);
	std::vector<std::string> NewFriends;
	NewFriends.reserve(8);
	std::string Key;
	Key.reserve(MAX_NAME_LENGTH + MAX_CLAN_LENGTH + 1);
	const bool IgnoreClan = IgnoreClanSetting != 0;
	const int LocalMain = GameClient()->m_aLocalIds[0];
	const int LocalDummy = GameClient()->m_aLocalIds[1];
	const bool HasDummy = Client()->DummyConnected();

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const auto &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active)
			continue;
		if(ClientId == LocalMain || (HasDummy && ClientId == LocalDummy))
			continue;
		if(!GameClient()->Friends()->IsFriend(Client.m_aName, Client.m_aClan, true))
			continue;

		BuildFriendNotifyKey(Client.m_aName, Client.m_aClan, IgnoreClan, Key);
		CurrentFriends.insert(Key);
		if(m_FriendEnterOnline.find(Key) == m_FriendEnterOnline.end())
			NewFriends.push_back(Client.m_aName);
	}

	if(!m_FriendEnterInitialized)
	{
		m_FriendEnterOnline = std::move(CurrentFriends);
		m_FriendEnterInitialized = true;
		return;
	}

	m_FriendEnterOnline = std::move(CurrentFriends);

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
		const std::string BroadcastText = BuildFriendEnterBroadcastText(g_Config.m_QmFriendEnterBroadcastText, NewNames);
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

void CTClient::CheckAutoUnspecOnUnfreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_QmAutoUnspecOnUnfreeze)
		return;

	// 分别检查主玩家和dummy（每个Tee的旁观状态是独立的）
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		// 检查是否处于旁观状态
		bool IsSpectating = GameClient()->m_Snap.m_SpecInfo.m_Active;

		// 检查当前是否被 freeze
		bool IsInFreeze = ClientData.m_FreezeEnd != 0;

		// 检测从 freeze 到 unfreeze 的转换
		if(m_aWasInFreezeForUnspec[Dummy] && !IsInFreeze && IsSpectating)
		{
			if(DetectFreezeWakeupType(GameClient(), ClientId) != EFreezeWakeupType::EXTERNAL_HAMMER)
			{
				m_aWasInFreezeForUnspec[Dummy] = IsInFreeze;
				continue;
			}

			// 被解冻了，且当前处于旁观状态，直接发送网络包（最快方式）
			if(Client()->IsSixup())
			{
				// 0.7协议
				protocol7::CNetMsg_Cl_Say Msg7;
				Msg7.m_Mode = protocol7::CHAT_ALL;
				Msg7.m_Target = -1;
				Msg7.m_pMessage = "/spec";
				Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
			}
			else
			{
				// 0.6协议
				CNetMsg_Cl_Say Msg;
				Msg.m_Team = 0; // 全体聊天
				Msg.m_pMessage = "/spec";
				Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
			}
		}

		m_aWasInFreezeForUnspec[Dummy] = IsInFreeze;
	}
}

void CTClient::CheckAutoSwitchOnUnfreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_QmAutoSwitchOnUnfreeze)
		return;

	// 必须有dummy连接
	if(!Client()->DummyConnected())
		return;

	// 获取本体和dummy的ClientId
	const int MainClientId = GameClient()->m_aLocalIds[0];
	const int DummyClientId = GameClient()->m_aLocalIds[1];

	if(MainClientId < 0 || DummyClientId < 0)
		return;

	const auto &MainClient = GameClient()->m_aClients[MainClientId];
	const auto &DummyClient = GameClient()->m_aClients[DummyClientId];

	if(!MainClient.m_Active || !DummyClient.m_Active)
		return;

	// 获取当前freeze状态
	bool MainInFreeze = MainClient.m_FreezeEnd != 0;
	bool DummyInFreeze = DummyClient.m_FreezeEnd != 0;

	// 获取之前的freeze状态
	bool MainWasInFreeze = m_aWasInFreezeForSwitch[0];
	bool DummyWasInFreeze = m_aWasInFreezeForSwitch[1];

	// 当前操控的是哪个 (0=本体, 1=dummy)
	int CurrentDummy = g_Config.m_ClDummy;
	const bool MainJustUnfrozen = MainWasInFreeze && !MainInFreeze;
	const bool DummyJustUnfrozen = DummyWasInFreeze && !DummyInFreeze;
	const bool MainExternalHammerWakeup = MainJustUnfrozen && DetectFreezeWakeupType(GameClient(), MainClientId) == EFreezeWakeupType::EXTERNAL_HAMMER;
	const bool DummyExternalHammerWakeup = DummyJustUnfrozen && DetectFreezeWakeupType(GameClient(), DummyClientId) == EFreezeWakeupType::EXTERNAL_HAMMER;

	// 核心逻辑：两个都曾被freeze，现在有一个解冻了
	// 如果解冻的不是当前操控的，就切换
	if(MainWasInFreeze && DummyWasInFreeze)
	{
		// 本体刚解冻，而当前操控的是dummy
		if(!MainInFreeze && DummyInFreeze && CurrentDummy == 1 && MainExternalHammerWakeup)
		{
			// 切换到本体
			Console()->ExecuteLine("cl_dummy 0");
		}
		// dummy刚解冻，而当前操控的是本体
		else if(MainInFreeze && !DummyInFreeze && CurrentDummy == 0 && DummyExternalHammerWakeup)
		{
			// 切换到dummy
			Console()->ExecuteLine("cl_dummy 1");
		}
	}

	// 更新状态
	m_aWasInFreezeForSwitch[0] = MainInFreeze;
	m_aWasInFreezeForSwitch[1] = DummyInFreeze;
}

void CTClient::CheckAutoCloseChatOnUnfreeze()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	bool CanCloseChat = g_Config.m_QmAutoCloseChatOnUnfreeze && GameClient()->m_Chat.IsActive();

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;

		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if(ClientId < 0)
			continue;

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		const bool IsInFreeze = ClientData.m_FreezeEnd != 0;

		if(m_aWasInFreezeForChatClose[Dummy] && !IsInFreeze && CanCloseChat)
		{
			if(DetectFreezeWakeupType(GameClient(), ClientId) != EFreezeWakeupType::EXTERNAL_HAMMER)
			{
				m_aWasInFreezeForChatClose[Dummy] = IsInFreeze;
				continue;
			}

			GameClient()->m_Chat.SaveDraft();
			GameClient()->m_Chat.DisableMode();
			CanCloseChat = false;
		}

		m_aWasInFreezeForChatClose[Dummy] = IsInFreeze;
	}
}

bool CTClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CTClient::RequestUpdateCheckAndUpdate()
{
	if((m_pTClientInfoTask && !m_pTClientInfoTask->Done()) || (m_pUpdateExeTask && !m_pUpdateExeTask->Done()))
		return;

	m_AutoUpdateAfterCheck = true;
	m_FetchedTClientInfo = false;
	FetchTClientInfo();
}

void CTClient::StartUpdateDownload()
{
#if !defined(CONF_FAMILY_WINDOWS)
	Client()->AddWarning(SWarning(Localize("Update"), Localize("Automatic updates are only supported on Windows")));
	return;
#endif

	if(m_pUpdateExeTask && !m_pUpdateExeTask->Done())
		return;

	ResetUpdateExeTask();
	IStorage::FormatTmpPath(m_aUpdateExeTmp, sizeof(m_aUpdateExeTmp), PLAT_CLIENT_EXEC);
	m_pUpdateExeTask = HttpGet(TCLIENT_UPDATE_EXE_URL);
	m_pUpdateExeTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pUpdateExeTask->IpResolve(IPRESOLVE::V4);
	m_pUpdateExeTask->WriteToFile(Storage(), m_aUpdateExeTmp, -2);
	Http()->Run(m_pUpdateExeTask);
	Client()->AddWarning(SWarning(Localize("Update"), Localize("Downloading update...")));
}

void CTClient::ResetUpdateExeTask()
{
	if(m_pUpdateExeTask)
	{
		m_pUpdateExeTask->Abort();
		m_pUpdateExeTask = nullptr;
	}
	m_aUpdateExeTmp[0] = '\0';
}

bool CTClient::ReplaceClientFromUpdate()
{
	if(m_aUpdateExeTmp[0] == '\0')
		return false;

	bool Success = true;
	Storage()->RemoveBinaryFile(CLIENT_EXEC ".old");
	Success &= Storage()->RenameBinaryFile(PLAT_CLIENT_EXEC, CLIENT_EXEC ".old");
	Success &= Storage()->RenameBinaryFile(m_aUpdateExeTmp, PLAT_CLIENT_EXEC);
	Storage()->RemoveBinaryFile(CLIENT_EXEC ".old");
	return Success;
}

void CTClient::ResetTClientInfoTask()
{
	if(m_pTClientInfoTask)
	{
		m_pTClientInfoTask->Abort();
		m_pTClientInfoTask = NULL;
	}
}

void CTClient::FetchTClientInfo()
{
	if(m_pTClientInfoTask && !m_pTClientInfoTask->Done())
		return;
	char aUrl[256];
	str_copy(aUrl, TCLIENT_INFO_URL);
	m_pTClientInfoTask = HttpGet(aUrl);
	m_pTClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pTClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pTClientInfoTask);
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidTCVersion = std::make_tuple(-1, -1, -1);

static TVersion ToTCVersion(char *pStr)
{
	int aVersion[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return gs_InvalidTCVersion;

		aVersion[i] = str_toint(p);
		p = strtok(NULL, ".");
	}

	if(p)
		return gs_InvalidTCVersion;

	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CTClient::FinishTClientInfo()
{
	json_value *pJson = m_pTClientInfoTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Json = *pJson;
	const json_value &CurrentVersion = Json["version"];

	if(CurrentVersion.type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, CurrentVersion);
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, TCLIENT_VERSION);
		if(ToTCVersion(aNewVersionStr) > ToTCVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, CurrentVersion);
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
		m_FetchedTClientInfo = true;
	}

	json_value_free(pJson);
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
	float ScreenAspectOverride = 0.0f;
	if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;

	if(g_Config.m_QmAspectPreset != 0)
	{
		int AspectRatio = g_Config.m_QmAspectRatio;
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
			ScreenAspectOverride = AspectRatio / 100.0f;
	}

	Graphics()->SetScreenAspectOverride(ScreenAspectOverride);
	Graphics()->SetForcedAspect(Force);
}

void CTClient::OnStateChange(int NewState, int OldState)
{
	if((NewState == IClient::STATE_QUITTING || NewState == IClient::STATE_RESTARTING) && !m_QmClientShutdownReported)
	{
		m_QmClientShutdownReported = true;
		TouchQmClientLifecycleMarker(true);
		SendQmClientLifecyclePing(NewState == IClient::STATE_RESTARTING ? "restart" : "shutdown", m_pQmClientLifecycleStopTask);
	}

	SetForcedAspect();
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};
	ClearFreezeWakeupPopups();

	if(NewState != IClient::STATE_ONLINE)
	{
		ClearSwapCountdown();
		m_aLastChatMessage[0] = '\0';
		m_LastRepeatTime = 0;
		m_LastRepeatKeyPressTime = 0;
		m_RepeatKeyDown = false;
		m_LastAutoReplyTime = 0;
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
			m_aWasInFreezeForWakeupPopup[i] = false;
			m_aWasInFreezeForUnspec[i] = false;
			m_aWasInFreezeForSwitch[i] = false;
			m_aWasInFreezeForChatClose[i] = false;
		}
		ResetComboState();
		InvalidateGoresDistanceField();
		m_FriendEnterOnline.clear();
		m_FriendEnterInitialized = false;
	}
	m_aLastGameplayLogicTick[0] = -1;
	m_aLastGameplayLogicTick[1] = -1;

	// 进入服务器时重置统计数据
	if(NewState == IClient::STATE_ONLINE && g_Config.m_QmPlayerStatsResetOnJoin)
	{
		ResetPlayerStats(-1);
	}
}

void CTClient::OnNewSnapshot()
{
	SetForcedAspect();
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

void CTClient::RenderMiniVoteHud()
{
	CUIRect View = {0.0f, 60.0f, 70.0f, 35.0f};
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_R, 3.0f);
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
	GameClient()->FormatStreamerVoteText(GameClient()->m_Voting.VoteDescription(), aVoteDescription, sizeof(aVoteDescription));
	StripStr(aVoteDescription, aBuf, aBuf + sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	// Vote reason
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	GameClient()->FormatStreamerVoteText(GameClient()->m_Voting.VoteReason(), aVoteReason, sizeof(aVoteReason));
	Ui()->DoLabel(&Row, aVoteReason, 4.0f, TEXTALIGN_ML, Props);

	// Time left
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), GameClient()->m_Voting.SecondsLeft());
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
	bool IsHooking = Char.m_Cur.m_HookState > 0 && Char.m_Cur.m_HookState != HOOK_RETRACTED;

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

bool CTClient::IsGoresModuleEnabled() const
{
	return g_Config.m_QmGores != 0 || (g_Config.m_QmGoresAutoEnable != 0 && IsGoresGameMode());
}

bool CTClient::HasBlockingGoresWeapon() const
{
	if(!g_Config.m_QmGoresDisableIfWeapons || Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalCharacter)
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
		!HasBlockingGoresWeapon();
}

void CTClient::UpdateGoresWeaponCycle()
{
	if(!ShouldAppendGoresPrevWeapon())
		return;

	if(GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_HAMMER)
		GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = WEAPON_GUN + 1;
}

bool CTClient::IsGoresMapProgressEnabled() const
{
	return IsGoresGameMode() && g_Config.m_QmPlayerStatsMapProgress;
}

bool CTClient::IsGoresMapProgressDebugRouteEnabled() const
{
	return IsGoresGameMode() && g_Config.m_QmPlayerStatsMapProgressDbgRoute != 0;
}

void CTClient::InvalidateGoresDistanceField()
{
	m_GoresDistanceFieldValid = false;
	m_GoresDistanceFieldAttempted = false;
	m_GoresDistanceFieldNextBuildTryTick = 0;
	m_GoresDistanceFieldWidth = 0;
	m_GoresDistanceFieldHeight = 0;
	m_vGoresCMap.clear();
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceToFinish.clear();
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
	if(Client()->State() != IClient::STATE_ONLINE || !IsGoresGameMode())
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
	if(m_GoresDistanceFieldAttempted && Now < m_GoresDistanceFieldNextBuildTryTick)
		return;

	m_GoresDistanceFieldAttempted = true;
	BuildGoresDistanceField();
	m_GoresDistanceFieldNextBuildTryTick = m_GoresDistanceFieldValid ? 0 : (Now + time_freq());
}

void CTClient::BuildGoresDistanceField()
{
	m_GoresDistanceFieldValid = false;
	m_GoresDistanceFieldWidth = 0;
	m_GoresDistanceFieldHeight = 0;
	m_vGoresCMap.clear();
	m_vvGoresDirectTeleOuts.clear();
	m_vGoresDistanceToFinish.clear();

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

	std::vector<int> vFinishIndices;
	std::vector<unsigned char> vPassable(MapSize, 0);
	m_vGoresCMap.assign((size_t)MapSize, GORES_CMAP_NORMAL);
	vFinishIndices.reserve(16);
	std::vector<std::vector<int>> vvDirectTeleInputs;
	m_vvGoresDirectTeleOuts.clear();
	bool HasStart = false;

	for(int Index = 0; Index < MapSize; ++Index)
	{
		const int Tile = pGame[Index].m_Index;
		const int FrontTile = pFront ? pFront[Index].m_Index : TILE_AIR;
		const bool IsStart = Tile == TILE_START || FrontTile == TILE_START;
		const bool IsFinish = Tile == TILE_FINISH || FrontTile == TILE_FINISH;
		const bool HasPenalty = IsPenaltyTileForGoresDistanceField(Tile) || IsPenaltyTileForGoresDistanceField(FrontTile);
		const bool HasReward = IsRewardTileForGoresDistanceField(Tile) || IsRewardTileForGoresDistanceField(FrontTile);
		const bool HasTeleport = pTele && pTele[Index].m_Type != 0;
		const bool IsBlocked = !IsStart && !IsFinish &&
			(IsHardBlockedForGoresDistanceField(Tile) || IsHardBlockedForGoresDistanceField(FrontTile));
		if(IsStart)
			HasStart = true;
		if(IsFinish)
			vFinishIndices.push_back(Index);

		// Keep route generation forgiving: start/finish tiles are always traversable.
		vPassable[Index] = (!IsBlocked || IsStart || IsFinish) ? 1 : 0;
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
				if((int)vvDirectTeleInputs.size() < TeleNumber)
					vvDirectTeleInputs.resize(TeleNumber);
				vvDirectTeleInputs[(size_t)TeleNumber - 1].push_back(Index);
			}
			else if(TeleType == TILE_TELEOUT && TeleNumber > 0)
			{
				if((int)m_vvGoresDirectTeleOuts.size() < TeleNumber)
					m_vvGoresDirectTeleOuts.resize(TeleNumber);
				m_vvGoresDirectTeleOuts[(size_t)TeleNumber - 1].push_back(Index);
			}
		}
	}

	if(!HasStart || vFinishIndices.empty())
	{
		m_vGoresCMap.clear();
		m_vvGoresDirectTeleOuts.clear();
		return;
	}

	if(const CLayers *pLayers = Layers())
	{
		if(IMap *pMap = pLayers->Map())
		{
			int ImageStart = 0;
			int ImageCount = 0;
			pMap->GetType(MAPITEMTYPE_IMAGE, &ImageStart, &ImageCount);
			std::vector<unsigned char> vImageSemantics((size_t)maximum(ImageCount, 0), GORES_CMAP_NORMAL);
			for(int ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex)
			{
				const auto *pImage = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(ImageStart + ImageIndex));
				if(!pImage)
					continue;

				const char *pImageName = pMap->GetDataString(pImage->m_ImageName);
				vImageSemantics[(size_t)ImageIndex] = GoresSemanticImageToCMapValue(pImageName);
				pMap->UnloadData(pImage->m_ImageName);
			}

			for(int GroupIndex = 0; GroupIndex < pLayers->NumGroups(); ++GroupIndex)
			{
				const CMapItemGroup *pGroup = pLayers->GetGroup(GroupIndex);
				if(!pGroup)
					continue;

				for(int LayerOffset = 0; LayerOffset < pGroup->m_NumLayers; ++LayerOffset)
				{
					const CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + LayerOffset);
					if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
						continue;

					const auto *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
					if(pTilemap->m_Flags != 0 ||
						pTilemap->m_Width != Width ||
						pTilemap->m_Height != Height ||
						pTilemap->m_Image < 0 ||
						pTilemap->m_Image >= ImageCount)
						continue;

					const unsigned char SemanticValue = vImageSemantics[(size_t)pTilemap->m_Image];
					if(SemanticValue != GORES_CMAP_PENALTY && SemanticValue != GORES_CMAP_REWARD)
						continue;

					if(pMap->GetDataSize(pTilemap->m_Data) < MapSize * (int)sizeof(CTile))
						continue;

					const CTile *pLayerTiles = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
					if(!pLayerTiles)
						continue;

					for(int Index = 0; Index < MapSize; ++Index)
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

					pMap->UnloadData(pTilemap->m_Data);
				}
			}
		}
	}

	static constexpr int DISTANCE_INF = std::numeric_limits<int>::max();
	m_vGoresDistanceToFinish.assign((size_t)MapSize, DISTANCE_INF);

	using TDistanceNode = std::pair<int, int>;
	std::priority_queue<TDistanceNode, std::vector<TDistanceNode>, std::greater<TDistanceNode>> Queue;
	for(const int FinishIndex : vFinishIndices)
	{
		if(FinishIndex < 0 || FinishIndex >= MapSize || !vPassable[FinishIndex] ||
			m_vGoresDistanceToFinish[(size_t)FinishIndex] != DISTANCE_INF)
			continue;
		m_vGoresDistanceToFinish[(size_t)FinishIndex] = 0;
		Queue.emplace(0, FinishIndex);
	}

	if(Queue.empty())
	{
		m_vGoresCMap.clear();
		m_vvGoresDirectTeleOuts.clear();
		m_vGoresDistanceToFinish.clear();
		return;
	}

	const auto TryRelax = [&](int Index, int NewDistance) {
		if(NewDistance < m_vGoresDistanceToFinish[(size_t)Index])
		{
			m_vGoresDistanceToFinish[(size_t)Index] = NewDistance;
			Queue.emplace(NewDistance, Index);
		}
	};

	while(!Queue.empty())
	{
		const int CurDistance = Queue.top().first;
		const int Cur = Queue.top().second;
		Queue.pop();
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
			if(!vPassable[(size_t)PredIndex])
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
			if(TeleNumber > 0 && (int)vvDirectTeleInputs.size() >= TeleNumber)
			{
				for(const int PredIndex : vvDirectTeleInputs[(size_t)TeleNumber - 1])
					TryRelax(PredIndex, CurDistance);
			}
		}
	}

	bool HasReachableStart = false;
	for(int Index = 0; Index < MapSize; ++Index)
	{
		const int Tile = pGame[Index].m_Index;
		const int FrontTile = pFront ? pFront[Index].m_Index : TILE_AIR;
		const bool IsStart = Tile == TILE_START || FrontTile == TILE_START;
		if(IsStart && m_vGoresDistanceToFinish[(size_t)Index] != DISTANCE_INF)
		{
			HasReachableStart = true;
			break;
		}
	}

	if(!HasReachableStart)
	{
		m_vGoresCMap.clear();
		m_vvGoresDirectTeleOuts.clear();
		m_vGoresDistanceToFinish.clear();
		return;
	}

	m_GoresDistanceFieldWidth = Width;
	m_GoresDistanceFieldHeight = Height;
	m_GoresDistanceFieldValid = true;
}

bool CTClient::BuildGoresDebugRoute(std::vector<vec2> &vRoutePoints, int Dummy) const
{
	vRoutePoints.clear();

	const CCollision *pCollision = Collision();
	if(!pCollision || !m_GoresDistanceFieldValid)
		return false;

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	const int MapCellCount = Width * Height;
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
		float BestDistanceSquared = std::numeric_limits<float>::max();
		for(int Index = 0; Index < MapCellCount; ++Index)
		{
			if(!IsReachableIndex(Index))
				continue;

			const int Tile = pGame[Index].m_Index;
			const int FrontTile = pFront ? pFront[Index].m_Index : TILE_AIR;
			if(Tile != TILE_START && FrontTile != TILE_START)
				continue;

			const float DistanceSquared = length_squared(RefPos - pCollision->GetPos(Index));
			if(DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				StartIndex = Index;
			}
		}
	}

	if(!IsReachableIndex(StartIndex))
		return false;

	std::vector<unsigned char> vVisited((size_t)MapCellCount, 0);
	vRoutePoints.reserve(256);
	int CurrentIndex = StartIndex;
	for(int Guard = 0; Guard < MapCellCount + 64; ++Guard)
	{
		if(!IsReachableIndex(CurrentIndex) || vVisited[(size_t)CurrentIndex] != 0)
			break;

		vVisited[(size_t)CurrentIndex] = 1;
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
	if(Client()->State() != IClient::STATE_ONLINE || !IsGoresMapProgressDebugRouteEnabled())
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
	return m_FavoriteMaps.find(std::string(pMapName)) != m_FavoriteMaps.end();
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
	json_value *pJson = json_parse_ex(&JsonSettings, static_cast<json_char *>(pFileData), FileSize, aError);
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

void CTClient::ConSaveList(IConsole::IResult *pResult, void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// 如果用户指定了地图名，使用指定的；否则使用当前地图
	const char *pFilterMap = pResult->NumArguments() > 0 ? pResult->GetString(0) : pThis->Client()->GetCurrentMap();

	// 打开本地存档文件
	IOHANDLE File = pThis->Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		pThis->GameClient()->Echo("No local saves file found (ddnet-saves.txt)");
		return;
	}

	// 读取整个文件内容
	char *pFileContent = io_read_all_str(File);
	io_close(File);

	if(!pFileContent)
	{
		pThis->GameClient()->Echo("Failed to read saves file");
		return;
	}

	int Count = 0;
	bool IsFirstLine = true;

	// 显示标题
	char aTitle[256];
	if(pFilterMap && pFilterMap[0] != '\0')
		str_format(aTitle, sizeof(aTitle), "=== Saves for '%s' ===", pFilterMap);
	else
		str_copy(aTitle, "=== All Local Saves ===");
	pThis->GameClient()->Echo(aTitle);

	// 逐行处理
	char *pLine = pFileContent;
	while(*pLine)
	{
		// 找到行尾
		char *pLineEnd = pLine;
		while(*pLineEnd && *pLineEnd != '\n' && *pLineEnd != '\r')
			pLineEnd++;

		// 保存行尾字符并临时设为结束符
		char LineEndChar = *pLineEnd;
		if(*pLineEnd)
			*pLineEnd = '\0';

		// 处理这一行
		if(pLine[0] != '\0')
		{
			// 跳过表头
			if(IsFirstLine)
			{
				IsFirstLine = false;
				if(!str_startswith(pLine, "Time"))
				{
					// 如果第一行不是表头，也要处理
					IsFirstLine = false;
				}
				else
				{
					// 跳过表头行
					goto next_line;
				}
			}

			// 解析 CSV 行: Time,Players,Map,Code
			char aTime[64] = {0};
			char aPlayers[256] = {0};
			char aMap[128] = {0};
			char aCode[128] = {0};

			// 简单的 CSV 解析
			const char *pCurrent = pLine;
			char *apFields[4] = {aTime, aPlayers, aMap, aCode};
			int FieldSizes[4] = {sizeof(aTime), sizeof(aPlayers), sizeof(aMap), sizeof(aCode)};
			int FieldIndex = 0;
			bool InQuotes = false;

			for(int i = 0; pCurrent[i] && FieldIndex < 4; i++)
			{
				if(pCurrent[i] == '"')
				{
					InQuotes = !InQuotes;
				}
				else if(pCurrent[i] == ',' && !InQuotes)
				{
					FieldIndex++;
				}
				else if(FieldIndex < 4)
				{
					int Len = str_length(apFields[FieldIndex]);
					if(Len < FieldSizes[FieldIndex] - 1)
					{
						apFields[FieldIndex][Len] = pCurrent[i];
						apFields[FieldIndex][Len + 1] = '\0';
					}
				}
			}

			// 过滤地图
			if(pFilterMap && pFilterMap[0] != '\0')
			{
				// 精确匹配地图名（不区分大小写）
				if(str_comp_nocase(aMap, pFilterMap) != 0)
					goto next_line;
			}

			// 输出格式: [玩家名] 密码 (地图: xxx, 保存时间: xxx)
			char aOutput[512];
			str_format(aOutput, sizeof(aOutput), "[%s] %s (Map: %s, Time: %s)",
				aPlayers[0] ? aPlayers : "Unknown",
				aCode[0] ? aCode : "no-code",
				aMap[0] ? aMap : "Unknown",
				aTime[0] ? aTime : "Unknown");
			pThis->GameClient()->Echo(aOutput);
			Count++;
		}

next_line:
		// 恢复行尾字符并移动到下一行
		if(LineEndChar)
		{
			*pLineEnd = LineEndChar;
			pLine = pLineEnd + 1;
			// 跳过 \r\n 的第二个字符
			if(LineEndChar == '\r' && *pLine == '\n')
				pLine++;
		}
		else
		{
			break;
		}
	}

	free(pFileContent);

	char aCountMsg[128];
	str_format(aCountMsg, sizeof(aCountMsg), "Total: %d save(s)", Count);
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
	GameClient()->m_Chat.SendChat(0, m_aLastChatMessage);
}
