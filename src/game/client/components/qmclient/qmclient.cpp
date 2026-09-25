// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qmclient.h"

#include "qm_title_style.h"
#include "statistics_file.h"
#include "voice/voice_utils.h"

#include <base/hash.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/client/updater.h>
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
#include <engine/shared/localization.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/qmclient/qm_sponsors.h>
#include <game/client/components/qmclient/qm_title_style.h>
#include <game/client/components/qmclient/voice/voice_utils.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>
#include <game/version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

[[maybe_unused]] static constexpr const char *TCLIENT_UPDATE_EXE_URL = "https://github.com/wxj881027/QmClient/releases/latest/download/DDNet.exe";
[[maybe_unused]] static constexpr const char *MAP_CATEGORY_CACHE_FILE = "qmclient/map_categories.json";
[[maybe_unused]] static constexpr int64_t MAP_CATEGORY_CACHE_SAVE_DELAY_SEC = 5;
// 本地统计文件的条目上限：异常服务器可能用任意 gametype 制造新条目，
// 限制文件与内存的增长；达到上限后只更新已有条目，不再新建。
static constexpr int QMCLIENT_MAX_LOCAL_MODE_STATS = 256;
static constexpr int QMCLIENT_REALTIME_PROTOCOL_VERSION = 2;

static void LogQmWebSocketEvent(const char *pChannel, const char *pStage)
{
	if(g_Config.m_QmWebSocketLog)
		log_info("qmclient", "%s websocket %s", pChannel, pStage);
}

static void LogQmClientDistributionEvent(const char *pStage, int Users, int Dummies, int LocalMarks)
{
	log_info("qmclient", "distribution %s: users=%d dummies=%d local_marks=%d", pStage, Users, Dummies, LocalMarks);
}

static void LogQmClientDistributionFailureEvent(const char *pStage, const char *pDetail)
{
	log_warn("qmclient", "distribution %s: %s", pStage, pDetail ? pDetail : "");
}

static constexpr const char *QMCLIENT_DEVELOPER_TOKEN_FILE = "qmclient/developer_token.txt";
static constexpr const char *QMCLIENT_NEWS_PUBLISH_URL = "https://qmclient.icu/api/v1/news/publish";
static constexpr const char *QMCLIENT_NEWS_CACHE_FILE = "qmclient/news_cache.json";
static constexpr const char *QMCLIENT_NEWS_DRAFT_FILE = "qmclient/news_draft.md";
static constexpr int QMCLIENT_NEWS_CACHE_VERSION = 1;
static constexpr int QMCLIENT_NEWS_MAX_BYTES = 64 * 1024;
static constexpr const char *QMCLIENT_SPONSORS_PUBLISH_URL = "https://qmclient.icu/api/v1/sponsors/publish";
static constexpr const char *QMCLIENT_SPONSORS_CACHE_FILE = "qmclient/sponsors_cache.json";
static constexpr const char *QMCLIENT_SPONSORS_DRAFT_FILE = "qmclient/sponsors_draft.md";
static constexpr int QMCLIENT_DEVELOPER_SYNC_INTERVAL_SECONDS = 5;
static constexpr const char *QMCLIENT_LIFECYCLE_MARKER_FILE = "qmclient/lifecycle_pending.marker";
static constexpr const char *QMCLIENT_PLAYTIME_CLIENT_ID_FILE = "qmclient/playtime_client_id.txt";
static constexpr const char *QMCLIENT_MACHINE_ID_FALLBACK_FILE = "qmclient/voice_machine_id.txt";
// 广播 markdown 的磁盘缓存：界面立即生效，落盘只保留最新完整快照（交给作业完成）。
static constexpr const char *QMCLIENT_MARKDOWN_BROADCAST_CACHE_FILE = "qmclient/markdown_broadcast.json";
static constexpr int QMCLIENT_MARKDOWN_BROADCAST_CACHE_VERSION = 1;
static constexpr const char *QMCLIENT_NEWS_PUBLISH_URL = "https://qmclient.icu/api/v1/news/publish";
static constexpr const char *QMCLIENT_NEWS_DRAFT_FILE = "qmclient/news_draft.md";
static constexpr size_t QMCLIENT_NEWS_MAX_BYTES = 64 * 1024;
static constexpr const char *QMCLIENT_SPONSORS_PUBLISH_URL = "https://qmclient.icu/api/v1/sponsors/publish";
static constexpr const char *QMCLIENT_SPONSORS_CACHE_FILE = "qmclient/sponsors_cache.json";
static constexpr const char *QMCLIENT_SPONSORS_DRAFT_FILE = "qmclient/sponsors_draft.md";
static constexpr int QMCLIENT_SPONSORS_MAX_BYTES = 64 * 1024;
static constexpr int QMCLIENT_MARKER_FLUSH_INTERVAL_SECONDS = 5;
static constexpr const char *DDNET_PLAYER_STATS_URL = "https://ddnet.org/players/?json2=";
static constexpr int QMCLIENT_DDNET_PLAYER_SYNC_INTERVAL_SECONDS = 120;
static constexpr int QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS = 10;
static constexpr const char *QMCLIENT_FREEZE_WAKEUP_TEXT = "快醒醒!";
static constexpr const char *QMCLIENT_LOCAL_MODE_STATS_FILE = "qmclient/statistics.json";

static bool ParseStrictInt64(const char *pText, int64_t &Out)
{
	if(!pText || pText[0] == '\0')
		return false;
	Out = str_toint64_base(pText);
	char aCanonical[64];
	str_format(aCanonical, sizeof(aCanonical), "%" PRId64, Out);
	return str_comp(aCanonical, pText) == 0;
}

static int64_t SaturatingAddInt64(int64_t Base, int64_t Add)
{
	if(Add > 0 && Base > std::numeric_limits<int64_t>::max() - Add)
		return std::numeric_limits<int64_t>::max();
	if(Add < 0 && Base < std::numeric_limits<int64_t>::min() - Add)
		return std::numeric_limits<int64_t>::min();
	return Base + Add;
}

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
[[maybe_unused]] static constexpr float QMCLIENT_TEXT_POPUP_FONT_SIZE = 30.0f;
[[maybe_unused]] static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_OFFSET = vec2(34.0f, -78.0f);
[[maybe_unused]] static constexpr vec2 QMCLIENT_FREEZE_WAKEUP_POPUP_DRIFT = vec2(18.0f, -16.0f);
[[maybe_unused]] static constexpr int QMCLIENT_COMBO_POPUP_WINDOW_SECONDS = 2;
[[maybe_unused]] static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_FROM = ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f);
[[maybe_unused]] static constexpr ColorRGBA QMCLIENT_POPUP_ROLL_COLOR_TO = ColorRGBA(1.0f, 0.0f, 1.0f, 1.0f);
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
// 与 tclient 一致：默认英文 source key，空模板回退用
static constexpr const char *s_pFriendEnterBroadcastDefaultText = "%s joined this server";

[[maybe_unused]] static int AutoReplySeparatorLength(const char *pStr);
[[maybe_unused]] static bool AppendAutoReplyRuleBlock(char *pOutRules, size_t OutRulesSize, const char *pRules);
static const json_value *JsonObjectField(const json_value *pObject, const char *pName);
static bool JsonReadNonNegativeInt64(const json_value *pValue, int64_t &OutValue);
static const char *TitleJsonString(const json_value *pRoot, const char *pKey);

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

	[[maybe_unused]] static constexpr std::array<STextPopupDefinition, (int)ETextPopupType::NUM_TYPES> s_aTextPopupDefinitions = {{
		{QMCLIENT_FREEZE_WAKEUP_TEXT},
	}};

	class CQmClientUsersParseJob : public IJob
	{
	public:
		using SResult = SQmClientUsersParseResult;

	private:
		std::shared_ptr<const json_value> m_pPayload;
		char m_aServerAddress[NETADDR_MAXSTRSIZE] = "";
		int64_t m_ExpireTick = 0;
		int64_t m_ConnectionTick = 0;
		CLock m_Lock;
		SResult m_Result;

	protected:
		void Run() override REQUIRES(!m_Lock)
		{
			SResult Result;
			if(m_pPayload)
				ParseQmClientUsersJson(m_pPayload.get(), m_aServerAddress, Result);

			{
				const CLockScope Lock(m_Lock);
				m_Result = std::move(Result);
			}
			m_pPayload.reset();
		}

	public:
		CQmClientUsersParseJob(std::shared_ptr<const json_value> pPayload, const char *pServerAddress, int64_t ExpireTick, int64_t ConnectionTick) :
			m_pPayload(std::move(pPayload)),
			m_ExpireTick(ExpireTick),
			m_ConnectionTick(ConnectionTick)
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

		const char *ServerAddress() const { return m_aServerAddress; }
		int64_t ExpireTick() const { return m_ExpireTick; }
		int64_t ConnectionTick() const { return m_ConnectionTick; }
		const char *ServerAddress() const { return m_aServerAddress; }
	};

	class CQmDdnetPlayerStatsParseJob : public IJob
	{
	public:
		struct SResult
		{
			bool m_Parsed = false;
			std::string m_FavoritePartner;
			int m_TotalFinishes = -1;
			int64_t m_Points = -1;
			int64_t m_PointsTotal = -1;
			int64_t m_PlaytimeHours = -1;
			int64_t m_PlaytimeHoursPastYear = -1;
		};

	private:
		std::shared_ptr<IHttpRequest> m_pTask;
		CLock m_Lock;
		SResult m_Result;

	protected:
		void Run() override REQUIRES(!m_Lock)
		{
			SResult Result;
			if(m_pTask && m_pTask->State() == EHttpState::DONE && m_pTask->StatusCode() == 200)
			{
				json_value *pRoot = m_pTask->ResultJson();
				if(pRoot && pRoot->type == json_object)
				{
					const json_value *pPoints = JsonObjectField(pRoot, "points");
					bool ValidPoints = false;
					if(pPoints != &json_value_none && pPoints->type == json_object)
					{
						const json_value *pCurrent = JsonObjectField(pPoints, "points");
						const json_value *pTotal = JsonObjectField(pPoints, "total");
						if(pCurrent != &json_value_none && pCurrent->type == json_integer && pCurrent->u.integer >= 0)
							Result.m_Points = pCurrent->u.integer;
						if(pTotal != &json_value_none && pTotal->type == json_integer && pTotal->u.integer >= 0)
							Result.m_PointsTotal = pTotal->u.integer;
						ValidPoints = Result.m_Points >= 0 && Result.m_PointsTotal >= 0;
					}

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
					if(ValidPoints && pTypes->type == json_object)
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

					// 官方 json2 直接提供两种口径的游玩小时数：
					//   activity[] 逐日 hours_played 求和 = 生涯累计；
					//   hours_played_past_365_days        = 最近一年。
					// 两者都只作为显示用的可选数据，缺失时不置 m_Parsed，避免影响既有解析成功判定。
					const json_value *pActivity = JsonObjectField(pRoot, "activity");
					if(pActivity->type == json_array)
					{
						int64_t TotalHours = 0;
						bool AnyEntry = false;
						bool Overflow = false;
						for(unsigned i = 0; i < pActivity->u.array.length; ++i)
						{
							const json_value &Entry = (*pActivity)[i];
							if(Entry.type != json_object)
								continue;
							const json_value *pHours = JsonObjectField(&Entry, "hours_played");
							if(pHours->type != json_integer || pHours->u.integer < 0)
								continue;
							AnyEntry = true;
							if(TotalHours > std::numeric_limits<int64_t>::max() - pHours->u.integer)
							{
								Overflow = true;
								break;
							}
							TotalHours += pHours->u.integer;
						}
						if(AnyEntry && !Overflow)
							Result.m_PlaytimeHours = TotalHours;
					}
					const json_value *pPastYearHours = JsonObjectField(pRoot, "hours_played_past_365_days");
					if(pPastYearHours->type == json_integer && pPastYearHours->u.integer >= 0)
						Result.m_PlaytimeHoursPastYear = pPastYearHours->u.integer;

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
		explicit CQmDdnetPlayerStatsParseJob(std::shared_ptr<IHttpRequest> pTask) :
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
		std::shared_ptr<std::mutex> m_pMutex;

	protected:
		void Run() override
		{
			if(m_pMutex == nullptr)
				return;
			std::lock_guard<std::mutex> Lock(*m_pMutex);
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
		CQmClientLifecycleMarkerWriteJob(IStorage *pStorage, std::string Content, std::shared_ptr<std::mutex> pMutex) :
			m_pStorage(pStorage),
			m_Content(std::move(Content)),
			m_pMutex(std::move(pMutex))
		{
			Abortable(true);
		}
	};

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

// NOLINTNEXTLINE(misc-use-internal-linkage)
const char *GetEffectiveQmVoiceServer()
{
	return VoiceUtils::EffectiveVoiceWebSocketUrl(g_Config.m_QmVoiceServer);
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
		// `double(INT64_MAX)` rounds to 2^63, which is already outside int64_t.
		if(!std::isfinite(pValue->u.dbl) || pValue->u.dbl < 0.0 || pValue->u.dbl >= static_cast<double>(std::numeric_limits<int64_t>::max()))
			return false;
		OutValue = (int64_t)pValue->u.dbl;
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
	if(g_Config.m_QmWebSocketUrl[0] == '\0' && g_Config.m_QmRealtimeWebsocketUrl[0] != '\0')
		str_copy(g_Config.m_QmWebSocketUrl, g_Config.m_QmRealtimeWebsocketUrl);
	InitQmClientLifecycle();
	LoadQmClientLocalModeStats();
	// 先把上次会话缓存的广播内容读回来，界面不必等下一次服务端推送。
	LoadQmMarkdownBroadcastCache();
	LoadQmSponsorsCache();
	if(!m_QmStatisticsFileExists)
		SaveQmClientStatistics();
	InitQmDeveloperAuthentication();
	InitTitleAuthentication();
	InitQmNews();
	InitQmSponsors();
	// 实时通道：worker 线程在 StartQmRealtime 里起，真正连接由 OnUpdate 驱动。
	StartQmRealtime();
	StartQmAnonymousEmotes();
}

void CQmClient::LoadQmClientLocalModeStats()
{
	m_vQmClientLocalModeStats.clear();
	m_vQmClientDdnetPlayerStats.clear();
	m_QmDdnetPrimaryPlayerName.clear();
	m_aQmDdnetPlayerName[0] = '\0';
	m_aQmDdnetFavoritePartner[0] = '\0';
	m_QmDdnetTotalFinishes = -1;
	m_QmDdnetPoints = -1;
	m_QmDdnetPointsTotal = -1;
	m_QmClientServerPlaytimeSeconds = -1;
	m_QmStatisticsFileExists = false;
	m_QmStatisticsFileInvalid = false;
	m_QmStatisticsNextSaveRetryTick = 0;
	json_value *pRoot = nullptr;
	const EQmStatisticsFileLoadResult LoadResult = QmLoadStatisticsFile(Storage(), QMCLIENT_LOCAL_MODE_STATS_FILE, &pRoot);
	if(LoadResult == EQmStatisticsFileLoadResult::NOT_FOUND)
		return;
	m_QmStatisticsFileExists = true;
	if(LoadResult != EQmStatisticsFileLoadResult::VALID)
	{
		m_QmStatisticsFileInvalid = true;
		log_warn("qmclient", "statistics file is invalid and will be preserved: %s", QMCLIENT_LOCAL_MODE_STATS_FILE);
		return;
	}
	const json_value *pLocal = JsonObjectField(pRoot, "local");
	const json_value *pRemote = JsonObjectField(pRoot, "remote");
	const json_value *pModes = JsonObjectField(pLocal, "modes");
	if(pModes && pModes->type == json_array)
	{
		for(int Index = 0; Index < json_array_length(pModes); ++Index)
		{
			const json_value *pMode = json_array_get(pModes, Index);
			if(!pMode || pMode->type != json_object)
				continue;
			const json_value *pName = JsonObjectField(pMode, "mode");
			const json_value *pCommunity = JsonObjectField(pMode, "community_id");
			const json_value *pAxiom = JsonObjectField(pMode, "axiom");
			const json_value *pMaps = JsonObjectField(pMode, "maps");
			const json_value *pScore = JsonObjectField(pMode, "score");
			const json_value *pPlaytime = JsonObjectField(pMode, "playtime_seconds");
			if(!pName || pName->type != json_string || !pMaps || pMaps->type != json_integer || !pScore)
				continue;
			const char *pModeName = json_string_get(pName);
			int64_t Maps = pMaps->u.integer;
			int64_t Score = 0;
			if(!pModeName || pModeName[0] == '\0' || static_cast<size_t>(str_length(pModeName)) > 128 || !str_utf8_check(pModeName) || Maps < 0 || Maps > std::numeric_limits<int>::max())
				continue;
			if(pScore->type == json_integer)
				Score = pScore->u.integer;
			else if(pScore->type == json_string)
			{
				const char *pScoreText = json_string_get(pScore);
				if(!ParseStrictInt64(pScoreText, Score))
					continue;
			}
			else
				continue;
			SQmClientLocalModeStats Stats;
			Stats.m_GameMode = pModeName;
			if(pCommunity != &json_value_none && pCommunity->type == json_string && json_string_get(pCommunity) && static_cast<size_t>(str_length(json_string_get(pCommunity))) <= 128 && str_utf8_check(json_string_get(pCommunity)))
				Stats.m_CommunityId = json_string_get(pCommunity);
			Stats.m_IsAxiom = pAxiom->type == json_boolean && json_boolean_get(pAxiom);
			Stats.m_Maps = (int)Maps;
			Stats.m_Score = Score;
			if(pPlaytime)
			{
				if(pPlaytime->type == json_integer && pPlaytime->u.integer >= 0)
					Stats.m_PlaytimeSeconds = pPlaytime->u.integer;
				else if(pPlaytime->type == json_string)
				{
					const char *pPlaytimeText = json_string_get(pPlaytime);
					int64_t ParsedPlaytime = 0;
					if(ParseStrictInt64(pPlaytimeText, ParsedPlaytime) && ParsedPlaytime >= 0)
						Stats.m_PlaytimeSeconds = ParsedPlaytime;
				}
			}
			auto Existing = std::find_if(m_vQmClientLocalModeStats.begin(), m_vQmClientLocalModeStats.end(), [&Stats](const SQmClientLocalModeStats &Entry) {
				return str_comp_nocase(Entry.m_GameMode.c_str(), Stats.m_GameMode.c_str()) == 0 && Entry.m_CommunityId == Stats.m_CommunityId && Entry.m_IsAxiom == Stats.m_IsAxiom;
			});
			if(Existing == m_vQmClientLocalModeStats.end())
			{
				// 文件被手工改成超量条目时截断，保证内存与后续保存有界。
				if((int)m_vQmClientLocalModeStats.size() >= QMCLIENT_MAX_LOCAL_MODE_STATS)
					continue;
				m_vQmClientLocalModeStats.push_back(std::move(Stats));
				continue;
			}
			Existing->m_Maps = (int)std::min<int64_t>(std::numeric_limits<int>::max(), SaturatingAddInt64(Existing->m_Maps, Stats.m_Maps));
			Existing->m_Score = SaturatingAddInt64(Existing->m_Score, Stats.m_Score);
			Existing->m_PlaytimeSeconds = SaturatingAddInt64(Existing->m_PlaytimeSeconds, Stats.m_PlaytimeSeconds);
		}
	}
	const json_value *pQmClient = JsonObjectField(pRemote, "qmclient");
	const json_value *pPrimaryPlayerName = JsonObjectField(pRemote, "primary_player_name");
	if(pPrimaryPlayerName->type == json_string && json_string_get(pPrimaryPlayerName) && json_string_get(pPrimaryPlayerName)[0] != '\0' && static_cast<size_t>(str_length(json_string_get(pPrimaryPlayerName))) < MAX_NAME_LENGTH && str_utf8_check(json_string_get(pPrimaryPlayerName)))
		m_QmDdnetPrimaryPlayerName = json_string_get(pPrimaryPlayerName);
	const json_value *pOpenSeconds = JsonObjectField(pQmClient, "open_seconds");
	if(pOpenSeconds->type == json_integer && pOpenSeconds->u.integer >= 0)
		m_QmClientServerPlaytimeSeconds = pOpenSeconds->u.integer;
	else if(pOpenSeconds->type == json_string)
	{
		int64_t OpenSeconds = 0;
		if(ParseStrictInt64(json_string_get(pOpenSeconds), OpenSeconds) && OpenSeconds >= 0)
			m_QmClientServerPlaytimeSeconds = OpenSeconds;
	}
	const json_value *pDdnet = JsonObjectField(pRemote, "ddnet");
	const json_value *pPlayers = JsonObjectField(pDdnet, "players");
	if(pPlayers->type == json_array)
	{
		for(unsigned Index = 0; Index < pPlayers->u.array.length; ++Index)
		{
			const json_value *pPlayer = pPlayers->u.array.values[Index];
			const json_value *pName = JsonObjectField(pPlayer, "name");
			if(pName->type != json_string || !json_string_get(pName) || json_string_get(pName)[0] == '\0' || static_cast<size_t>(str_length(json_string_get(pName))) >= MAX_NAME_LENGTH || !str_utf8_check(json_string_get(pName)))
				continue;
			if(FindQmDdnetPlayerStats(json_string_get(pName)) != nullptr)
				continue;
			SQmClientDdnetPlayerStats Stats;
			Stats.m_PlayerName = json_string_get(pName);
			const json_value *pFavoritePartner = JsonObjectField(pPlayer, "favorite_partner");
			if(pFavoritePartner->type == json_string && json_string_get(pFavoritePartner) && static_cast<size_t>(str_length(json_string_get(pFavoritePartner))) < MAX_NAME_LENGTH && str_utf8_check(json_string_get(pFavoritePartner)))
				Stats.m_FavoritePartner = json_string_get(pFavoritePartner);
			const json_value *pPoints = JsonObjectField(pPlayer, "points");
			const json_value *pPointsTotal = JsonObjectField(pPlayer, "points_total");
			const json_value *pFinishes = JsonObjectField(pPlayer, "finishes");
			if(!JsonReadNonNegativeInt64(pPoints, Stats.m_Points) || !JsonReadNonNegativeInt64(pPointsTotal, Stats.m_PointsTotal))
				continue;
			int64_t Finishes = 0;
			if(!JsonReadNonNegativeInt64(pFinishes, Finishes) || Finishes > std::numeric_limits<int>::max())
				continue;
			Stats.m_TotalFinishes = (int)Finishes;
			const json_value *pPlaytimeHours = JsonObjectField(pPlayer, "playtime_hours");
			int64_t PlaytimeHours = 0;
			if(JsonReadNonNegativeInt64(pPlaytimeHours, PlaytimeHours))
				Stats.m_PlaytimeHours = PlaytimeHours;
			const json_value *pPlaytimeHoursPastYear = JsonObjectField(pPlayer, "playtime_hours_past_year");
			int64_t PlaytimeHoursPastYear = 0;
			if(JsonReadNonNegativeInt64(pPlaytimeHoursPastYear, PlaytimeHoursPastYear))
				Stats.m_PlaytimeHoursPastYear = PlaytimeHoursPastYear;
			m_vQmClientDdnetPlayerStats.push_back(std::move(Stats));
		}
	}
	SelectQmDdnetPlayerStats();
	if(GameClient() != nullptr)
		GameClient()->m_QmAxiomScores.LoadPersistentCache(pRoot);
	json_value_free(pRoot);
}

int64_t CQmClient::QmStatisticsLastSuccessfulSyncTimestamp() const
{
	const int64_t DdnetTimestamp = m_QmDdnetPlayerState.LastSuccessfulSyncTimestamp();
	const int64_t AxiomTimestamp = GameClient() != nullptr ? GameClient()->m_QmAxiomScores.LastSuccessfulSyncTimestamp() : 0;
	return std::max(DdnetTimestamp, std::max(m_QmClientPlaytimeLastSuccessfulSyncTimestamp, AxiomTimestamp));
}

bool CQmClient::SaveQmClientStatistics() const
{
	if(m_QmStatisticsFileInvalid)
	{
		log_warn("qmclient", "refusing to overwrite invalid statistics file: %s", QMCLIENT_LOCAL_MODE_STATS_FILE);
		return false;
	}
	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	char aTempFilename[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTempFilename, sizeof(aTempFilename), QMCLIENT_LOCAL_MODE_STATS_FILE);
	IOHANDLE File = Storage()->OpenFile(aTempFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	{
		CJsonFileWriter Writer(File);
		Writer.BeginObject();
		Writer.WriteAttribute("local");
		Writer.BeginObject();
		Writer.WriteAttribute("modes");
		Writer.BeginArray();
		for(const SQmClientLocalModeStats &Stats : m_vQmClientLocalModeStats)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("mode");
			Writer.WriteStrValue(Stats.m_GameMode.c_str());
			if(!Stats.m_CommunityId.empty())
			{
				Writer.WriteAttribute("community_id");
				Writer.WriteStrValue(Stats.m_CommunityId.c_str());
			}
			if(Stats.m_IsAxiom)
			{
				Writer.WriteAttribute("axiom");
				Writer.WriteBoolValue(true);
			}
			Writer.WriteAttribute("maps");
			Writer.WriteIntValue(Stats.m_Maps);
			Writer.WriteAttribute("score");
			char aScore[64];
			str_format(aScore, sizeof(aScore), "%" PRId64, Stats.m_Score);
			Writer.WriteStrValue(aScore);
			Writer.WriteAttribute("playtime_seconds");
			char aPlaytime[64];
			str_format(aPlaytime, sizeof(aPlaytime), "%" PRId64, std::max<int64_t>(0, Stats.m_PlaytimeSeconds));
			Writer.WriteStrValue(aPlaytime);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
		Writer.WriteAttribute("remote");
		Writer.BeginObject();
		if(!m_QmDdnetPrimaryPlayerName.empty())
		{
			Writer.WriteAttribute("primary_player_name");
			Writer.WriteStrValue(m_QmDdnetPrimaryPlayerName.c_str());
		}
		Writer.WriteAttribute("qmclient");
		Writer.BeginObject();
		if(m_QmClientServerPlaytimeSeconds >= 0)
		{
			Writer.WriteAttribute("open_seconds");
			char aOpenSeconds[64];
			str_format(aOpenSeconds, sizeof(aOpenSeconds), "%" PRId64, m_QmClientServerPlaytimeSeconds);
			Writer.WriteStrValue(aOpenSeconds);
		}
		Writer.EndObject();
		Writer.WriteAttribute("ddnet");
		Writer.BeginObject();
		Writer.WriteAttribute("players");
		Writer.BeginArray();
		for(const SQmClientDdnetPlayerStats &Stats : m_vQmClientDdnetPlayerStats)
		{
			if(Stats.m_PlayerName.empty() || Stats.m_Points < 0 || Stats.m_PointsTotal < 0 || Stats.m_TotalFinishes < 0)
				continue;
			Writer.BeginObject();
			Writer.WriteAttribute("name");
			Writer.WriteStrValue(Stats.m_PlayerName.c_str());
			Writer.WriteAttribute("favorite_partner");
			Writer.WriteStrValue(Stats.m_FavoritePartner.c_str());
			Writer.WriteAttribute("points");
			char aPoints[64];
			str_format(aPoints, sizeof(aPoints), "%" PRId64, Stats.m_Points);
			Writer.WriteStrValue(aPoints);
			Writer.WriteAttribute("points_total");
			char aPointsTotal[64];
			str_format(aPointsTotal, sizeof(aPointsTotal), "%" PRId64, Stats.m_PointsTotal);
			Writer.WriteStrValue(aPointsTotal);
			Writer.WriteAttribute("finishes");
			Writer.WriteIntValue(Stats.m_TotalFinishes);
			if(Stats.m_PlaytimeHours >= 0)
			{
				Writer.WriteAttribute("playtime_hours");
				char aPlaytimeHours[64];
				str_format(aPlaytimeHours, sizeof(aPlaytimeHours), "%" PRId64, Stats.m_PlaytimeHours);
				Writer.WriteStrValue(aPlaytimeHours);
			}
			if(Stats.m_PlaytimeHoursPastYear >= 0)
			{
				Writer.WriteAttribute("playtime_hours_past_year");
				char aPlaytimeHoursPastYear[64];
				str_format(aPlaytimeHoursPastYear, sizeof(aPlaytimeHoursPastYear), "%" PRId64, Stats.m_PlaytimeHoursPastYear);
				Writer.WriteStrValue(aPlaytimeHoursPastYear);
			}
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
		if(GameClient() != nullptr)
			GameClient()->m_QmAxiomScores.WritePersistentCache(Writer);
		else
		{
			Writer.WriteAttribute("axiom");
			Writer.BeginObject();
			Writer.WriteAttribute("players");
			Writer.BeginArray();
			Writer.EndArray();
			Writer.EndObject();
		}
		Writer.EndObject();
		Writer.EndObject();
		if(!Writer.Finish())
		{
			Storage()->RemoveFile(aTempFilename, IStorage::TYPE_SAVE);
			return false;
		}
	}
	char aBackupFilename[2 * IO_MAX_PATH_LENGTH];
	const bool Saved = IStorage::ReplaceFileSafely(Storage(), aTempFilename, QMCLIENT_LOCAL_MODE_STATS_FILE, aBackupFilename, sizeof(aBackupFilename));
	if(!Saved)
		Storage()->RemoveFile(aTempFilename, IStorage::TYPE_SAVE);
	else
	{
		m_QmStatisticsFileExists = true;
		m_QmStatisticsNextSaveRetryTick = 0;
	}
	return Saved;
}

const SQmClientDdnetPlayerStats *CQmClient::FindQmDdnetPlayerStats(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return nullptr;
	for(const SQmClientDdnetPlayerStats &Stats : m_vQmClientDdnetPlayerStats)
	{
		if(str_comp_nocase(Stats.m_PlayerName.c_str(), pPlayerName) == 0)
			return &Stats;
	}
	return nullptr;
}

void CQmClient::StoreQmDdnetPlayerStats(const char *pPlayerName, const std::string &FavoritePartner, int TotalFinishes, int64_t Points, int64_t PointsTotal, int64_t PlaytimeHours, int64_t PlaytimeHoursPastYear)
{
	if(!pPlayerName || pPlayerName[0] == '\0' || TotalFinishes < 0 || Points < 0 || PointsTotal < 0)
		return;
	for(SQmClientDdnetPlayerStats &Stats : m_vQmClientDdnetPlayerStats)
	{
		if(str_comp_nocase(Stats.m_PlayerName.c_str(), pPlayerName) != 0)
			continue;
		Stats.m_FavoritePartner = FavoritePartner;
		Stats.m_TotalFinishes = TotalFinishes;
		Stats.m_Points = Points;
		Stats.m_PointsTotal = PointsTotal;
		Stats.m_PlaytimeHours = PlaytimeHours;
		Stats.m_PlaytimeHoursPastYear = PlaytimeHoursPastYear;
		return;
	}
	SQmClientDdnetPlayerStats Stats;
	Stats.m_PlayerName = pPlayerName;
	Stats.m_FavoritePartner = FavoritePartner;
	Stats.m_TotalFinishes = TotalFinishes;
	Stats.m_Points = Points;
	Stats.m_PointsTotal = PointsTotal;
	Stats.m_PlaytimeHours = PlaytimeHours;
	Stats.m_PlaytimeHoursPastYear = PlaytimeHoursPastYear;
	m_vQmClientDdnetPlayerStats.push_back(std::move(Stats));
}

void CQmClient::SelectQmDdnetPlayerStats(const char *pFallbackPlayerName)
{
	const char *pSelectedPlayerName = nullptr;
	if(!m_QmDdnetPrimaryPlayerName.empty())
		pSelectedPlayerName = m_QmDdnetPrimaryPlayerName.c_str();
	else if(pFallbackPlayerName && pFallbackPlayerName[0] != '\0')
		pSelectedPlayerName = pFallbackPlayerName;
	if(!pSelectedPlayerName)
	{
		m_aQmDdnetPlayerName[0] = '\0';
		m_aQmDdnetFavoritePartner[0] = '\0';
		m_QmDdnetTotalFinishes = -1;
		m_QmDdnetPoints = -1;
		m_QmDdnetPointsTotal = -1;
		m_QmDdnetPlaytimeHours = -1;
		m_QmDdnetPlaytimeHoursPastYear = -1;
		return;
	}

	str_copy(m_aQmDdnetPlayerName, pSelectedPlayerName, sizeof(m_aQmDdnetPlayerName));
	const SQmClientDdnetPlayerStats *pStats = FindQmDdnetPlayerStats(pSelectedPlayerName);
	if(!pStats)
	{
		m_aQmDdnetFavoritePartner[0] = '\0';
		m_QmDdnetTotalFinishes = -1;
		m_QmDdnetPoints = -1;
		m_QmDdnetPointsTotal = -1;
		m_QmDdnetPlaytimeHours = -1;
		m_QmDdnetPlaytimeHoursPastYear = -1;
		return;
	}
	str_copy(m_aQmDdnetFavoritePartner, pStats->m_FavoritePartner.c_str(), sizeof(m_aQmDdnetFavoritePartner));
	m_QmDdnetTotalFinishes = pStats->m_TotalFinishes;
	m_QmDdnetPoints = pStats->m_Points;
	m_QmDdnetPointsTotal = pStats->m_PointsTotal;
	m_QmDdnetPlaytimeHours = pStats->m_PlaytimeHours;
	m_QmDdnetPlaytimeHoursPastYear = pStats->m_PlaytimeHoursPastYear;
}

void CQmClient::RecordQmClientLocalMapFinish(const char *pGameMode, int Score)
{
	if(!pGameMode || pGameMode[0] == '\0' || static_cast<size_t>(str_length(pGameMode)) > 128 || !str_utf8_check(pGameMode))
		return;
	const bool IsAxiom = Client()->State() == IClient::STATE_ONLINE && GameClient() != nullptr && GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity();
	const std::string CommunityId = Client()->State() == IClient::STATE_ONLINE ? Client()->ServerInfo().m_aCommunityId : "";
	if(CommunityId.size() > 128 || !str_utf8_check(CommunityId.c_str()))
		return;
	AccumulateQmClientLocalModePlaytime(time_get());
	for(SQmClientLocalModeStats &Stats : m_vQmClientLocalModeStats)
	{
		if(str_comp_nocase(Stats.m_GameMode.c_str(), pGameMode) == 0 && Stats.m_CommunityId == CommunityId && Stats.m_IsAxiom == IsAxiom)
		{
			if(Stats.m_Maps < std::numeric_limits<int>::max())
				++Stats.m_Maps;
			Stats.m_Score = SaturatingAddInt64(Stats.m_Score, Score);
			m_QmStatisticsLocalStatsDirty = true;
			return;
		}
	}
	if((int)m_vQmClientLocalModeStats.size() >= QMCLIENT_MAX_LOCAL_MODE_STATS)
		return;
	SQmClientLocalModeStats Stats;
	Stats.m_GameMode = pGameMode;
	Stats.m_CommunityId = CommunityId;
	Stats.m_IsAxiom = IsAxiom;
	Stats.m_Maps = 1;
	Stats.m_Score = Score;
	m_vQmClientLocalModeStats.push_back(std::move(Stats));
	m_QmStatisticsLocalStatsDirty = true;
}

void CQmClient::OnMessage(int MsgType, void *pRawMsg)
{
	// 本地完成计数：DDNet 系服务端不会产生 GAMEOVER，完成通过两条互斥
	// 通道播报——0.6 是「finished in」聊天广播（含中文格式），0.7 是
	// RaceFinish 事件。demo 回放与离线状态不计数。
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(pMsg->m_ClientId >= 0 && pMsg->m_ClientId == GameClient()->m_Snap.m_LocalClientId && pMsg->m_Time > 0)
			RecordQmClientLocalRaceFinish(pMsg->m_Time);
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		const CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
		// m_pMessage 解包失败时可能为 null，必须先判空再交给解析器。
		if(pMsg->m_ClientId != -1 || LocalClientId < 0 || pMsg->m_pMessage == nullptr)
			return;
		char aName[MAX_NAME_LENGTH];
		const int Time = CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName));
		if(Time > 0 && str_comp(aName, GameClient()->m_aClients[LocalClientId].m_aName) == 0)
			RecordQmClientLocalRaceFinish(Time);
	}
}

void CQmClient::RecordQmClientLocalRaceFinish(int TimeMs)
{
	// 双通道兜底去重：同一完成事件在 1 秒内只记一次。
	const int64_t Now = time_get();
	if(m_QmLastLocalFinishRecordTime >= 0 && Now - m_QmLastLocalFinishRecordTime < time_freq())
		return;
	const char *pGameType = Client()->ServerInfo().m_aGameType;
	if(!pGameType || pGameType[0] == '\0')
		return;
	m_QmLastLocalFinishRecordTime = Now;
	// 两条通道的时间都是毫秒；score 字段按正的完成秒数累计，
	// 不再沿用旧 GAMEOVER 路径的负值语义。
	RecordQmClientLocalMapFinish(pGameType, maximum<int>(1, TimeMs / 1000));
}

void CQmClient::AccumulateQmClientLocalModePlaytime(int64_t Now)
{
	if(m_QmClientActiveLocalMode.empty())
		return;
	if(m_QmClientLocalModeLastTick <= 0)
	{
		m_QmClientLocalModeLastTick = Now;
		return;
	}
	const int64_t Delta = Now - m_QmClientLocalModeLastTick;
	m_QmClientLocalModeLastTick = Now;
	if(Delta <= 0 || Delta > 24 * 60 * 60 * time_freq())
	{
		m_QmClientLocalModeTickRemainder = 0;
		return;
	}
	m_QmClientLocalModeTickRemainder += Delta;
	const int64_t Seconds = m_QmClientLocalModeTickRemainder / time_freq();
	m_QmClientLocalModeTickRemainder %= time_freq();
	if(Seconds <= 0)
		return;
	for(SQmClientLocalModeStats &Stats : m_vQmClientLocalModeStats)
	{
		if(str_comp_nocase(Stats.m_GameMode.c_str(), m_QmClientActiveLocalMode.c_str()) == 0 && Stats.m_CommunityId == m_QmClientActiveLocalCommunityId && Stats.m_IsAxiom == m_QmClientActiveLocalIsAxiom)
		{
			Stats.m_PlaytimeSeconds = SaturatingAddInt64(Stats.m_PlaytimeSeconds, Seconds);
			m_QmStatisticsLocalStatsDirty = true;
			return;
		}
	}
	if((int)m_vQmClientLocalModeStats.size() >= QMCLIENT_MAX_LOCAL_MODE_STATS)
		return;
	SQmClientLocalModeStats Stats;
	Stats.m_GameMode = m_QmClientActiveLocalMode;
	Stats.m_CommunityId = m_QmClientActiveLocalCommunityId;
	Stats.m_IsAxiom = m_QmClientActiveLocalIsAxiom;
	Stats.m_PlaytimeSeconds = Seconds;
	m_vQmClientLocalModeStats.push_back(std::move(Stats));
	m_QmStatisticsLocalStatsDirty = true;
}

void CQmClient::UpdateQmClientLocalModePlaytime()
{
	const int64_t Now = time_get();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const char *pMode = Online ? Client()->ServerInfo().m_aGameType : "";
	const std::string CommunityId = Online ? Client()->ServerInfo().m_aCommunityId : "";
	const bool IsAxiom = Online && GameClient() != nullptr && GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity();
	if(m_QmClientActiveLocalMode != pMode || m_QmClientActiveLocalCommunityId != CommunityId || m_QmClientActiveLocalIsAxiom != IsAxiom)
	{
		AccumulateQmClientLocalModePlaytime(Now);
		m_QmClientActiveLocalMode = pMode;
		m_QmClientActiveLocalCommunityId = CommunityId;
		m_QmClientActiveLocalIsAxiom = IsAxiom;
		m_QmClientLocalModeLastTick = pMode[0] != '\0' ? Now : 0;
		m_QmClientLocalModeTickRemainder = 0;
		return;
	}
	AccumulateQmClientLocalModePlaytime(Now);
}

void CQmClient::EndQmClientLocalModePlaytime()
{
	if(!m_QmClientActiveLocalMode.empty())
	{
		AccumulateQmClientLocalModePlaytime(time_get());
		m_QmClientActiveLocalMode.clear();
		m_QmClientActiveLocalCommunityId.clear();
		m_QmClientActiveLocalIsAxiom = false;
		m_QmClientLocalModeLastTick = 0;
		m_QmClientLocalModeTickRemainder = 0;
	}
}

void CQmClient::OnShutdown()
{
	if(!m_QmClientShutdownReported)
	{
		m_QmClientShutdownReported = true;
		TouchQmClientLifecycleMarker(true);
		SendQmRealtimeStop();
	}
	if(m_pQmRealtimeTransport)
		m_pQmRealtimeTransport->Disconnect();
	StopQmAnonymousEmotes();
	m_QmRealtimeEvents.clear();
	m_QmRealtimeEmoticonEvents.clear();
	m_pQmRealtimeUsersPayload.reset();
	EndQmClientLocalModePlaytime();
	SaveQmClientStatistics();

	auto AbortTask = [](auto &pTask) {
		if(pTask)
		{
			pTask->Abort();
			pTask = nullptr;
		}
	};

	AbortTask(m_pQmDdnetPlayerTask);
	AbortTask(m_pTitleOperation);
	AbortTask(m_pQmNewsPublishTask);
	AbortTask(m_pQmSponsorsPublishTask);
	ResetTitlePresences();
	m_pQmClientUsersParseJob = nullptr;
	m_pQmDdnetPlayerParseJob = nullptr;
	m_QmDdnetPlayerState.Reset();
}

void CQmClient::OnUpdate()
{
	UpdateQmRealtime();
	UpdateQmAnonymousEmotes();
	UpdateQmClientLocalModePlaytime();
	UpdateQmClientRecognition();
	UpdateTitleAuthentication();
	UpdateQmClientLifecycleAndServerTime();
	UpdateQmDdnetPlayerStats();
	if(m_pQmNewsPublishTask && m_pQmNewsPublishTask->Done())
		FinishQmNewsPublish();
	if(m_pQmSponsorsPublishTask && m_pQmSponsorsPublishTask->Done())
		FinishQmSponsorsPublish();
	// 远程请求在 Axiom 组件中异步完成；下一帧统一落盘，避免每帧写文件。
	// 本地统计脏标记按 30 秒节流落盘：游玩时长逐秒累计，不能逐秒写文件，
	// 但也不能只在关机时保存——崩溃会把整段会话的本地统计全部丢失。
	const int64_t StatisticsNow = time_get();
	if(GameClient() != nullptr && !m_QmStatisticsFileInvalid &&
		(GameClient()->m_QmAxiomScores.PersistentCacheDirty() ||
			(m_QmStatisticsLocalStatsDirty && StatisticsNow >= m_QmStatisticsLocalSaveDueTick)) &&
		(m_QmStatisticsNextSaveRetryTick == 0 || StatisticsNow >= m_QmStatisticsNextSaveRetryTick))
	{
		if(SaveQmClientStatistics())
		{
			GameClient()->m_QmAxiomScores.ClearPersistentCacheDirty();
			m_QmStatisticsLocalStatsDirty = false;
			m_QmStatisticsLocalSaveDueTick = StatisticsNow + 30 * time_freq();
			m_QmStatisticsNextSaveRetryTick = 0;
		}
		else
			m_QmStatisticsNextSaveRetryTick = StatisticsNow + 5 * time_freq();
	}

	// Axiom 分数在后台按缓存 TTL 查询，统计页面只负责展示，不应成为唯一触发点。
	bool HasAxiomGoresStats = false;
	for(const SQmClientLocalModeStats &Stats : m_vQmClientLocalModeStats)
	{
		if(Stats.m_IsAxiom && str_find_nocase(Stats.m_GameMode.c_str(), "gores") != nullptr)
		{
			HasAxiomGoresStats = true;
			break;
		}
	}
	if((HasAxiomGoresStats || (GameClient() != nullptr && GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity())) && m_aQmDdnetPlayerName[0] != '\0')
		GameClient()->m_QmAxiomScores.EnsureQueried(m_aQmDdnetPlayerName);
}

void CQmClient::ApplyQmRealtimeServiceData(const SQmRealtimeMessage &Message)
{
	const json_value *pPayload = Message.m_pPayload.get();
	if(!pPayload)
		return;
	int64_t Value = 0;
	if((Message.m_Event == EQmRealtimeEvent::TIME || Message.m_Event == EQmRealtimeEvent::PLAYTIME) &&
		JsonReadNonNegativeInt64(JsonObjectField(pPayload, "ts"), Value) && Value > 0)
	{
		m_QmClientServerNow = Value;
		m_QmClientServerTimeLastSync = time_get();
		const double Measured = static_cast<double>(Value) - Client()->GlobalTime();
		m_TitleServerTimeOffset = m_TitleServerTimeOffsetValid ? (m_TitleServerTimeOffset * 0.75 + Measured * 0.25) : Measured;
		m_TitleServerTimeOffsetValid = true;
	}
	if(Message.m_Event == EQmRealtimeEvent::PLAYTIME)
	{
		if(JsonReadNonNegativeInt64(JsonObjectField(pPayload, "total_seconds"), Value))
			m_QmClientServerPlaytimeSeconds = Value;
		if(JsonReadNonNegativeInt64(JsonObjectField(pPayload, "last_start_at"), Value) && Value > 0)
			m_QmClientServerSessionStart = Value;
		m_QmClientPlaytimeLastSync = time_get();
		m_QmClientPlaytimeLastSuccessfulSyncTimestamp = time_timestamp();
		m_QmClientPlaytimeManualRefreshActive = false;
		m_QmClientPlaytimeManualRefreshFailed = false;
		if(str_comp(TitleJsonString(pPayload, "action"), "start") == 0 && !m_QmClientStartupSent)
		{
			m_QmClientMarkerStartedAt = time_timestamp();
			m_QmClientMarkerLastSeenAt = m_QmClientMarkerStartedAt;
			m_QmClientStartupSent = true;
			m_QmClientAwaitingRecoveryStop = false;
			WriteQmClientLifecycleMarker();
		}
		else if(str_comp(TitleJsonString(pPayload, "action"), "stop") == 0)
			ClearQmClientLifecycleMarker();
	}
	if(Message.m_Event == EQmRealtimeEvent::TITLE_PROFILE && !TitleBusy())
	{
		const char *pTitle = TitleJsonString(pPayload, "title");
		const char *pName = TitleJsonString(pPayload, "bound_name");
		const char *pStyle = TitleJsonString(pPayload, "style");
		const json_value *pStatus = JsonObjectField(pPayload, "status");
		const bool Authenticated = pStatus->type == json_integer && pStatus->u.integer == 200 && IsValidQmTitle(pTitle);
		const bool Changed = m_TitleAuthenticated != Authenticated ||
				     str_comp(m_aTitleText, pTitle) || str_comp(m_aTitleBoundName, pName) || str_comp(m_aTitleProfileStyle, pStyle);
		m_TitleAuthenticated = Authenticated;
		str_copy(m_aTitleText, pTitle);
		str_copy(m_aTitleBoundName, pName);
		str_copy(m_aTitleProfileStyle, pStyle);
		m_pTitleStatus = Authenticated ? Localizable("Permanent sponsor verified") : Localizable("Enter your sponsor code");
		if(Changed)
			++m_TitleRevision;
	}
	if(Message.m_Event == EQmRealtimeEvent::TITLE_STATUS)
	{
		const json_value *pStatus = JsonObjectField(pPayload, "status");
		if(pStatus->type == json_integer && pStatus->u.integer == 409)
		{
			m_pTitleStatus = Localizable("Four IP addresses are already online");
		}
	}
}

void CQmClient::ApplyQmRealtimeUsers(const SQmRealtimeMessage &Message)
{
	if(!Message.m_pPayload || Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress())
		return;
	char aServer[NETADDR_MAXSTRSIZE] = "";
	net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	const json_value *pPayload = Message.m_pPayload.get();
	const json_value *pAddress = JsonObjectField(pPayload, "server_address");
	if(pAddress->type != json_string || str_comp(pAddress->u.string.ptr, aServer) != 0 ||
		JsonObjectField(pPayload, "users")->type != json_array)
		return;
	m_pQmRealtimeUsersPayload = Message.m_pPayload;
	str_copy(m_aQmRealtimeUsersServer, aServer);
	m_QmRealtimeUsersExpireTick = time_get() + 20 * time_freq();
}

void CQmClient::ApplyQmRealtimeDevelopers(const SQmRealtimeMessage &Message)
{
	if(!Message.m_pPayload || Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress())
		return;
	char aServer[NETADDR_MAXSTRSIZE] = "";
	net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	const json_value *pPayload = Message.m_pPayload.get();
	const json_value *pAddress = JsonObjectField(pPayload, "server_address");
	if(pAddress->type != json_string || str_comp(pAddress->u.string.ptr, aServer) != 0)
		return;
	SQmDeveloperPresenceParseResult Result;
	if(!ParseQmDeveloperPresencesJson(pPayload, aServer, Result))
		return;
	GameClient()->ClearQmDeveloperMarks();
	const int64_t NowTick = time_get();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			continue;
		const SQmDeveloperPresence *pPresence = FindQmDeveloperPresence(
			Result.m_vPresences, aServer, ClientId, GameClient()->m_aClients[ClientId].m_aName, Result.m_ServerTime);
		if(!pPresence)
			continue;
		const int64_t RemainingSeconds = std::min<int64_t>(
			pPresence->m_ExpiresAt - Result.m_ServerTime, QMCLIENT_DEVELOPER_SYNC_INTERVAL_SECONDS * 2);
		if(RemainingSeconds <= 0)
			continue;
		GameClient()->MarkQmDeveloperClient(ClientId, pPresence->m_PlayerName.c_str(),
			NowTick + RemainingSeconds * time_freq(),
			QmDeveloperBadgeStyleFromBucket(pPresence->m_StyleBucket) == EQmDeveloperBadgeStyle::RAINBOW);
	}
}

void CQmClient::ApplyQmRealtimeBroadcast(const SQmRealtimeMessage &Message)
{
	// 只有内容真的变化时才落盘（Apply 内部已做版本单调与去重判断）。
	if(Message.m_HasBroadcast && m_QmMarkdownBroadcast.Apply(Message.m_BroadcastMarkdown, Message.m_BroadcastVersion))
		SaveQmMarkdownBroadcastCache();
}

void CQmClient::ApplyQmRealtimeTitles(const SQmRealtimeMessage &Message)
{
	if(!Message.m_HasTitles || !Message.m_pTitlePayload ||
		Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress())
		return;

	char aServer[NETADDR_MAXSTRSIZE] = "";
	net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	const json_value *pAddress = JsonObjectField(Message.m_pTitlePayload.get(), "server_address");
	if(pAddress->type != json_string || str_comp(pAddress->u.string.ptr, aServer) != 0)
		return;

	int64_t ServerTime = 0;
	const auto vPresences = ParseQmTitlePresences(Message.m_pTitlePayload.get(), aServer, &ServerTime);
	if(ServerTime <= 0)
		return;
	mem_zero(m_aTitleExpires, sizeof(m_aTitleExpires));
	for(const auto &Presence : vPresences)
	{
		str_copy(m_aaTitleNames[Presence.m_PlayerId], Presence.m_PlayerName.c_str());
		str_format(m_aaPlayerTitles[Presence.m_PlayerId], sizeof(m_aaPlayerTitles[Presence.m_PlayerId]), "[%s]", Presence.m_Title.c_str());
		str_copy(m_aaPlayerTitleStyles[Presence.m_PlayerId], Presence.m_Style.c_str());
		m_aTitleExpires[Presence.m_PlayerId] = time_get() + Presence.m_RemainingSeconds * time_freq();
	}
	const double Measured = static_cast<double>(ServerTime) - Client()->GlobalTime();
	m_TitleServerTimeOffset = m_TitleServerTimeOffsetValid ? (m_TitleServerTimeOffset * 0.75 + Measured * 0.25) : Measured;
	m_TitleServerTimeOffsetValid = true;
}

std::string CQmClient::BuildQmRealtimePresence(bool Hello) const
{
	char aServer[NETADDR_MAXSTRSIZE] = "";
	if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
		net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue(Hello ? "hello" : "presence");
	if(Hello)
	{
		Writer.WriteAttribute("v");
		Writer.WriteIntValue(QMCLIENT_REALTIME_PROTOCOL_VERSION);
		Writer.WriteAttribute("client_version");
		Writer.WriteStrValue(QMCLIENT_VERSION);
		Writer.WriteAttribute("machine_hash");
		Writer.WriteStrValue(m_aQmClientMachineHash);
		Writer.WriteAttribute("client_id");
		Writer.WriteStrValue(m_aQmClientPlaytimeClientId);
		Writer.WriteAttribute("player_name");
		Writer.WriteStrValue(g_Config.m_PlayerName);
		if(m_QmClientAwaitingRecoveryStop)
		{
			Writer.WriteAttribute("recovery_stop_at");
			Writer.WriteIntValue((int)std::clamp<int64_t>(m_QmClientRecoveryStopAt, 0, std::numeric_limits<int>::max()));
		}
	}
	Writer.WriteAttribute("server_address");
	Writer.WriteStrValue(aServer);
	Writer.WriteAttribute("session_id");
	Writer.WriteStrValue(m_aQmDeveloperSessionId);
	if(QmRealtimeAllowsCredentials(m_aQmRealtimeUrl))
	{
		Writer.WriteAttribute("title_token");
		Writer.WriteStrValue(m_aTitleToken);
		Writer.WriteAttribute("developer_token");
		Writer.WriteStrValue(m_aQmDeveloperToken);
	}
	Writer.WriteAttribute("players");
	Writer.BeginArray();
	if(aServer[0] && m_aQmDeveloperSessionId[0])
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int Id = GameClient()->m_aLocalIds[Dummy];
			if((Dummy == 1 && !Client()->DummyConnected()) || Id < 0 || Id >= MAX_CLIENTS ||
				!GameClient()->m_aClients[Id].m_Active)
				continue;
			Writer.BeginObject();
			Writer.WriteAttribute("player_id");
			Writer.WriteIntValue(Id);
			Writer.WriteAttribute("player_name");
			Writer.WriteStrValue(GameClient()->m_aClients[Id].m_aName);
			Writer.WriteAttribute("dummy");
			Writer.WriteBoolValue(Dummy == 1);
			Writer.WriteAttribute("voice_supported");
			Writer.WriteBoolValue(true);
			Writer.EndObject();
		}
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

bool CQmClient::RequestQmRealtimeTitleRefresh()
{
	if(!m_pQmRealtimeTransport || m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED)
		return false;
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("subscribe_titles");
	if(QmRealtimeAllowsCredentials(m_aQmRealtimeUrl))
	{
		Writer.WriteAttribute("title_token");
		Writer.WriteStrValue(m_aTitleToken);
	}
	Writer.EndObject();
	const std::string Body = Writer.GetOutputString();
	if(!m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size()))
		return false;
	m_QmRealtimeTitleRevision = m_TitleRevision;
	m_QmRealtimePresenceBody.clear();
	m_QmRealtimeNextPresenceCheck = 0;
	return true;
}

void CQmClient::SendQmRealtimeStop()
{
	if(!m_pQmRealtimeTransport || m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED)
		return;
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("stop");
	Writer.WriteAttribute("stop_at");
	Writer.WriteIntValue((int)std::clamp<int64_t>(time_timestamp(), 0, std::numeric_limits<int>::max()));
	Writer.EndObject();
	const std::string Body = Writer.GetOutputString();
	m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size());
}

void CQmClient::SaveQmMarkdownBroadcastCache()
{
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, QMCLIENT_MARKDOWN_BROADCAST_CACHE_FILE, aPath, sizeof(aPath));
	// 序列化与文件操作在作业里完成，不占用主线程，也不访问组件与配置。
	if(auto pJob = m_QmMarkdownBroadcastCacheWriter.Enqueue(aPath, QMCLIENT_MARKDOWN_BROADCAST_CACHE_VERSION, m_QmMarkdownBroadcast.Version(), m_QmMarkdownBroadcast.Markdown()))
		Engine()->AddJob(pJob);
}

void CQmClient::QmNewsReloadDraft()
{
	if(QmNewsPublishing())
		return;
	m_QmNewsDraft.clear();
	char *pDraft = Storage()->ReadFileStr(QMCLIENT_NEWS_DRAFT_FILE, IStorage::TYPE_SAVE);
	if(pDraft)
	{
		if(str_length(pDraft) <= QMCLIENT_NEWS_MAX_BYTES && str_utf8_check(pDraft))
			m_QmNewsDraft = pDraft;
		free(pDraft);
	}
	m_QmNewsStatus = EQmNewsStatus::IDLE;
}

void CQmClient::QmNewsPublishDraft()
{
	if(QmNewsPublishing() || !HasDeveloperCredential())
		return;
	if(m_QmNewsDraft.empty())
		QmNewsReloadDraft();
	if(m_QmNewsDraft.empty() || m_QmNewsDraft.size() > QMCLIENT_NEWS_MAX_BYTES || !str_utf8_check(m_QmNewsDraft.c_str()))
	{
		m_QmNewsStatus = EQmNewsStatus::PUBLISH_FAILED;
		return;
	}

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("markdown");
	Writer.WriteStrValue(m_QmNewsDraft.c_str());
	Writer.EndObject();
	const std::string Output = Writer.GetOutputString();
	m_pQmNewsPublishTask = HttpPostJson(QMCLIENT_NEWS_PUBLISH_URL, Output.c_str());
	m_pQmNewsPublishTask->MaxResponseSize(8 * 1024);
	m_pQmNewsPublishTask->FailOnErrorStatus(false);
	char aAuthorization[80];
	str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", m_aQmDeveloperToken);
	m_pQmNewsPublishTask->HeaderString("Authorization", aAuthorization);
	m_pQmNewsPublishTask->Timeout(CTimeout{3000, 5000, 500, 5});
	m_pQmNewsPublishTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmNewsPublishTask);
	m_QmNewsStatus = EQmNewsStatus::PUBLISHING;
}

void CQmClient::FinishQmNewsPublish()
{
	const int StatusCode = m_pQmNewsPublishTask->State() == EHttpState::DONE ? m_pQmNewsPublishTask->StatusCode() : 0;
	if(StatusCode == 200)
	{
		json_value *pRoot = m_pQmNewsPublishTask->ResultJson();
		if(pRoot && pRoot->type == json_object)
		{
			const json_value *pMarkdown = json_object_get(pRoot, "markdown");
			const json_value *pVersion = json_object_get(pRoot, "version");
			if(pMarkdown && pMarkdown->type == json_string && pMarkdown->u.string.length <= QMCLIENT_NEWS_MAX_BYTES &&
				pVersion && pVersion->type == json_integer && pVersion->u.integer >= 0 && pVersion->u.integer <= std::numeric_limits<int>::max() &&
				m_QmMarkdownBroadcast.Apply(std::string(pMarkdown->u.string.ptr, pMarkdown->u.string.length), (int)pVersion->u.integer))
				SaveQmMarkdownBroadcastCache();
		}
		json_value_free(pRoot);
		if(m_pQmRealtimeTransport && m_pQmRealtimeTransport->State() == EQmWebSocketState::CONNECTED)
		{
			static constexpr const char *pRefresh = "{\"type\":\"news\"}";
			m_pQmRealtimeTransport->SendText(pRefresh, str_length(pRefresh));
		}
		m_QmNewsStatus = EQmNewsStatus::PUBLISHED;
	}
	else if(StatusCode == 401 || StatusCode == 403)
		m_QmNewsStatus = EQmNewsStatus::PUBLISH_DENIED;
	else if(StatusCode == 413)
		m_QmNewsStatus = EQmNewsStatus::PUBLISH_TOO_LARGE;
	else
		m_QmNewsStatus = EQmNewsStatus::PUBLISH_FAILED;
	m_pQmNewsPublishTask = nullptr;
}

void CQmClient::LoadQmSponsorsCache()
{
	void *pData = nullptr;
	unsigned Size = 0;
	if(!Storage()->ReadFile(QMCLIENT_SPONSORS_CACHE_FILE, IStorage::TYPE_SAVE, &pData, &Size) || !pData)
	{
		free(pData);
		return;
	}
	json_value *pRoot = Size <= 6 * QMCLIENT_SPONSORS_MAX_BYTES + 1024 ? json_parse(static_cast<const char *>(pData), Size) : nullptr;
	free(pData);
	if(!pRoot)
		return;
	const json_value *pCacheVersion = json_object_get(pRoot, "cache_version");
	if(pCacheVersion && pCacheVersion->type == json_integer && pCacheVersion->u.integer == QMCLIENT_MARKDOWN_BROADCAST_CACHE_VERSION)
		ApplyQmSponsorsPayload(pRoot, false);
	json_value_free(pRoot);
}

void CQmClient::SaveQmSponsorsCache()
{
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, QMCLIENT_SPONSORS_CACHE_FILE, aPath, sizeof(aPath));
	if(auto pJob = m_QmSponsorsCacheWriter.Enqueue(aPath, QMCLIENT_MARKDOWN_BROADCAST_CACHE_VERSION, m_QmSponsors.Version(), m_QmSponsors.Markdown()))
		Engine()->AddJob(pJob);
}

bool CQmClient::ApplyQmSponsorsPayload(const json_value *pPayload, bool SaveCache)
{
	bool Changed = false;
	if(!m_QmSponsors.Apply(pPayload, Changed))
		return false;
	if(Changed)
	{
		if(!QmSponsorsPublishing())
		{
			m_QmSponsorsStatus = m_QmSponsors.Names().empty() ? ESponsorsStatus::EMPTY : ESponsorsStatus::READY;
			++m_QmSponsorsStatusRevision;
		}
		if(SaveCache)
			SaveQmSponsorsCache();
	}
	return true;
}

void CQmClient::QmSponsorsRefresh()
{
	if(!m_pQmRealtimeTransport || m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED)
		return;
	static constexpr const char *pMessage = "{\"type\":\"sponsors\"}";
	m_pQmRealtimeTransport->SendText(pMessage, str_length(pMessage));
}

void CQmClient::QmSponsorsReloadDraft()
{
	if(QmSponsorsPublishing())
		return;
	m_QmSponsorsDraft.clear();
	char *pDraft = Storage()->ReadFileStr(QMCLIENT_SPONSORS_DRAFT_FILE, IStorage::TYPE_SAVE);
	if(pDraft)
	{
		if(str_length(pDraft) <= QMCLIENT_SPONSORS_MAX_BYTES)
			m_QmSponsorsDraft = pDraft;
		free(pDraft);
	}
	++m_QmSponsorsStatusRevision;
}

void CQmClient::QmSponsorsPublishDraft()
{
	if(QmSponsorsPublishing() || !HasDeveloperCredential())
		return;
	if(m_QmSponsorsDraft.empty())
		QmSponsorsReloadDraft();
	if(m_QmSponsorsDraft.empty() || m_QmSponsorsDraft.size() > QMCLIENT_SPONSORS_MAX_BYTES || !str_utf8_check(m_QmSponsorsDraft.c_str()))
	{
		m_QmSponsorsStatus = ESponsorsStatus::PUBLISH_FAILED;
		++m_QmSponsorsStatusRevision;
		return;
	}

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("markdown");
	Writer.WriteStrValue(m_QmSponsorsDraft.c_str());
	Writer.EndObject();
	const std::string Output = Writer.GetOutputString();
	m_pQmSponsorsPublishTask = HttpPostJson(QMCLIENT_SPONSORS_PUBLISH_URL, Output.c_str());
	m_pQmSponsorsPublishTask->MaxResponseSize(6 * QMCLIENT_SPONSORS_MAX_BYTES + 1024);
	m_pQmSponsorsPublishTask->FailOnErrorStatus(false);
	char aAuthorization[80];
	str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", m_aQmDeveloperToken);
	m_pQmSponsorsPublishTask->HeaderString("Authorization", aAuthorization);
	m_pQmSponsorsPublishTask->Timeout(CTimeout{3000, 5000, 500, 5});
	m_pQmSponsorsPublishTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pQmSponsorsPublishTask);
	m_QmSponsorsStatus = ESponsorsStatus::PUBLISHING;
	++m_QmSponsorsStatusRevision;
}

void CQmClient::FinishQmSponsorsPublish()
{
	const int StatusCode = m_pQmSponsorsPublishTask->State() == EHttpState::DONE ? m_pQmSponsorsPublishTask->StatusCode() : 0;
	if(StatusCode == 200)
	{
		json_value *pRoot = m_pQmSponsorsPublishTask->ResultJson();
		const bool Applied = ApplyQmSponsorsPayload(pRoot, true);
		json_value_free(pRoot);
		m_QmSponsorsStatus = Applied ? ESponsorsStatus::PUBLISHED : ESponsorsStatus::PUBLISH_FAILED;
	}
	else if(StatusCode == 401 || StatusCode == 403)
		m_QmSponsorsStatus = ESponsorsStatus::PUBLISH_DENIED;
	else if(StatusCode == 413)
		m_QmSponsorsStatus = ESponsorsStatus::PUBLISH_TOO_LARGE;
	else
		m_QmSponsorsStatus = ESponsorsStatus::PUBLISH_FAILED;
	m_pQmSponsorsPublishTask = nullptr;
	++m_QmSponsorsStatusRevision;
}

void CQmClient::LoadQmMarkdownBroadcastCache()
{
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, QMCLIENT_MARKDOWN_BROADCAST_CACHE_FILE, aPath, sizeof(aPath));
	IOHANDLE File = io_open(aPath, IOFLAG_READ);
	if(!File)
		return;
	const unsigned Length = (unsigned)io_length(File);
	if(Length == 0)
	{
		io_close(File);
		return;
	}
	std::vector<char> vBuffer(Length);
	const bool ReadOk = io_read(File, vBuffer.data(), Length) == Length;
	io_close(File);
	if(!ReadOk)
		return;

	json_value *pRoot = json_parse(vBuffer.data(), Length);
	if(pRoot == nullptr)
		return;
	const json_value *pCacheVersion = json_object_get(pRoot, "cache_version");
	const json_value *pVersion = json_object_get(pRoot, "version");
	const json_value *pMarkdown = json_object_get(pRoot, "markdown");
	// 缓存格式版本不符时直接丢弃：宁可本次会话没有内容，也不用旧格式喂给解析器。
	if(pCacheVersion != nullptr && pCacheVersion->type == json_integer &&
		pCacheVersion->u.integer == QMCLIENT_MARKDOWN_BROADCAST_CACHE_VERSION &&
		pVersion != nullptr && pVersion->type == json_integer &&
		pMarkdown != nullptr && pMarkdown->type == json_string)
	{
		m_QmMarkdownBroadcast.Apply(std::string(pMarkdown->u.string.ptr, pMarkdown->u.string.length), (int)pVersion->u.integer);
	}
	json_value_free(pRoot);
}

void CQmClient::UpdateQmRealtime()
{
	if(m_QmClientShutdownReported)
		return;
	if(!m_pQmRealtimeTransport)
	{
		m_pQmRealtimeTransport = CreateQmWebSocketClient({});
		if(m_pQmRealtimeTransport && m_pQmRealtimeTransport->Available())
		{
			IQmWebSocketClient::STuning Tuning;
			Tuning.m_HeartbeatMs = g_Config.m_QmWebSocketHeartbeat * 1000;
			Tuning.m_BackoffBaseMs = g_Config.m_QmWebSocketBackoffBaseMs;
			Tuning.m_BackoffMaxMs = g_Config.m_QmWebSocketBackoffMaxMs;
			m_pQmRealtimeTransport->SetTuning(Tuning);
		}
	}
	if(!m_pQmRealtimeTransport)
		return;
	const char *pConfiguredUrl = QmRealtimeEffectiveUrl(g_Config.m_QmWebSocketUrl);
	if(str_comp(m_aQmRealtimeUrl, pConfiguredUrl) != 0)
	{
		if(m_pQmRealtimeTransport->Desired())
			m_pQmRealtimeTransport->Disconnect();
		LogQmWebSocketEvent("realtime", "endpoint_changed");
		str_copy(m_aQmRealtimeUrl, pConfiguredUrl);
		m_QmRealtimeFailureLogged = false;
		m_QmRealtimeEvents.clear();
		m_pQmRealtimeUsersPayload.reset();
		m_QmRealtimeHelloSent = false;
		m_QmRealtimeTitleRevision = -1;
		m_QmRealtimeConnectedTick = 0;
		m_QmRealtimePresenceBody.clear();
	}

	if(!m_pQmRealtimeTransport->Available())
		return;

	SQmWebSocketConnectConfig ConnectConfig;
	const std::string ParseError = ParseQmWebSocketUrl(m_aQmRealtimeUrl, ConnectConfig);
	if(!ParseError.empty())
	{
		if(!m_QmRealtimeFailureLogged)
		{
			m_QmRealtimeFailureLogged = true;
			log_warn("qmclient", "realtime WebSocket URL is invalid: %s", ParseError.c_str());
		}
		return;
	}

	ConnectConfig.m_Protocol = g_Config.m_QmWebSocketProtocol;
	ConnectConfig.m_MaxMessageSize = 8 * 1024 * 1024;
	ConnectConfig.m_AllowInsecureTls = g_Config.m_QmWebSocketAllowInsecureTls != 0;
	if(!m_pQmRealtimeTransport->Desired())
	{
		std::string Error;
		if(!m_pQmRealtimeTransport->Connect(ConnectConfig, Error))
		{
			if(!m_QmRealtimeFailureLogged)
			{
				m_QmRealtimeFailureLogged = true;
				log_warn("qmclient", "realtime WebSocket connection rejected: %s", Error.c_str());
			}
		}
		else
		{
			m_QmRealtimeFailureLogged = false;
			LogQmWebSocketEvent("realtime", "connecting");
		}
	}

	if(m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED)
	{
		if(m_QmRealtimeConnectedTick != 0)
			LogQmWebSocketEvent("realtime", "disconnected");
		m_QmRealtimeHelloSent = false;
		m_QmRealtimeTitleRevision = -1;
		m_QmRealtimeConnectedTick = 0;
		if(m_QmClientPlaytimeManualRefreshActive)
		{
			m_QmClientPlaytimeManualRefreshActive = false;
			m_QmClientPlaytimeManualRefreshFailed = true;
		}
		return;
	}

	const int64_t ConnectedTick = m_pQmRealtimeTransport->LastConnectedTick();
	if(m_QmRealtimeConnectedTick != ConnectedTick)
	{
		LogQmWebSocketEvent("realtime", "connected");
		m_QmRealtimeConnectedTick = ConnectedTick;
		m_QmRealtimeHelloSent = false;
		m_QmRealtimeTitleRevision = -1;
		m_QmRealtimePresenceBody.clear();
		m_QmRealtimeNextPresenceCheck = 0;
		m_QmRealtimeEvents.clear();
		m_pQmRealtimeUsersPayload.reset();
	}
	if(!m_QmRealtimeHelloSent && EnsureQmClientMachineHash())
	{
		EnsureQmClientPlaytimeClientId();
		const std::string Body = BuildQmRealtimePresence(true);
		m_QmRealtimeHelloSent = m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size());
		if(m_QmRealtimeHelloSent)
		{
			m_QmRealtimePresenceBody = BuildQmRealtimePresence(false);
			m_QmRealtimeLastPresence = time_get();
		}
	}

	SQmWebSocketMessage Incoming;
	for(int Count = 0; Count < 8 && m_pQmRealtimeTransport->PollMessage(Incoming); ++Count)
	{
		if(Incoming.m_Type == EQmWebSocketMessageType::TEXT)
			EnqueueQmRealtimeMessage(Incoming.m_Data.data(), Incoming.m_Data.size());
	}
	if(m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED ||
		m_pQmRealtimeTransport->LastConnectedTick() != ConnectedTick)
	{
		m_QmRealtimeEvents.clear();
		m_pQmRealtimeUsersPayload.reset();
		m_QmRealtimeHelloSent = false;
		return;
	}

	SQmRealtimeMessage RealtimeMessage;
	while(PopQmRealtimeMessage(RealtimeMessage))
	{
		if(RealtimeMessage.m_Event == EQmRealtimeEvent::BROADCAST)
			ApplyQmRealtimeBroadcast(RealtimeMessage);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::SPONSORS && RealtimeMessage.m_HasRealtimeData)
			ApplyQmSponsorsPayload(RealtimeMessage.m_pPayload.get(), true);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::TITLES)
			ApplyQmRealtimeTitles(RealtimeMessage);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::USERS)
			ApplyQmRealtimeUsers(RealtimeMessage);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::DEVELOPERS)
			ApplyQmRealtimeDevelopers(RealtimeMessage);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::TIME || RealtimeMessage.m_Event == EQmRealtimeEvent::PLAYTIME ||
			RealtimeMessage.m_Event == EQmRealtimeEvent::TITLE_PROFILE || RealtimeMessage.m_Event == EQmRealtimeEvent::TITLE_STATUS)
			ApplyQmRealtimeServiceData(RealtimeMessage);
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::STATE && RealtimeMessage.m_StatePayloadValid)
		{
			if(RealtimeMessage.m_HasOnlineUsers)
				m_QmClientDistribution.m_OnlineUserCount = RealtimeMessage.m_OnlineUsers;
			if(RealtimeMessage.m_HasOnlineDummies)
				m_QmClientDistribution.m_OnlineDummyCount = RealtimeMessage.m_OnlineDummies;
		}
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::PING)
		{
			static constexpr const char *pPong = "{\"type\":\"pong\"}";
			m_pQmRealtimeTransport->SendText(pPong, str_length(pPong));
		}
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::ERROR)
		{
			// 服务端错误字段可能含有用户数据；只记录稳定类别，不输出完整帧。
			LogQmWebSocketEvent("realtime", "service_error");
		}
		else if(RealtimeMessage.m_Event == EQmRealtimeEvent::UNKNOWN)
		{
			LogQmWebSocketEvent("realtime", "unknown_event");
		}
	}

	const int64_t Tick = time_get();
	if(m_QmClientPlaytimeManualRefreshActive && Tick - m_QmClientPlaytimeManualRefreshTick >= 10 * time_freq())
	{
		m_QmClientPlaytimeManualRefreshActive = false;
		m_QmClientPlaytimeManualRefreshFailed = true;
	}
	if(m_QmRealtimeHelloSent && Tick >= m_QmRealtimeNextPresenceCheck)
	{
		m_QmRealtimeNextPresenceCheck = Tick + time_freq() / 4;
		const std::string Body = BuildQmRealtimePresence(false);
		if(Body != m_QmRealtimePresenceBody || Tick - m_QmRealtimeLastPresence >= 5 * time_freq())
		{
			if(m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size()))
			{
				m_QmRealtimePresenceBody = Body;
				m_QmRealtimeLastPresence = Tick;
			}
		}
	}
	if(m_QmRealtimeHelloSent && m_QmRealtimeTitleRevision != m_TitleRevision)
		RequestQmRealtimeTitleRefresh();
}

void CQmClient::StopQmAnonymousEmotes()
{
	if(m_pQmAnonymousEmote)
	{
		m_pQmAnonymousEmote->Disconnect();
		m_pQmAnonymousEmote.reset();
	}
	if(m_QmAnonymousConnectedTick != 0)
		LogQmWebSocketEvent("anonymous_emote", "disconnected");
	m_aQmAnonymousClientId[0] = '\0';
	m_aQmAnonymousSessionId[0] = '\0';
	m_QmAnonymousConnectedTick = 0;
	m_QmAnonymousNextHelloCheck = 0;
	m_QmAnonymousHelloBody.clear();
	m_QmRealtimeEmoticonEvents.clear();
}

std::string CQmClient::BuildQmAnonymousEmoteHello() const
{
	char aServer[NETADDR_MAXSTRSIZE] = "";
	if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
		net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("hello");
	Writer.WriteAttribute("v");
	Writer.WriteIntValue(QMCLIENT_REALTIME_PROTOCOL_VERSION);
	Writer.WriteAttribute("client_id");
	Writer.WriteStrValue(m_aQmAnonymousClientId);
	Writer.WriteAttribute("player_name");
	Writer.WriteStrValue(g_Config.m_PlayerName);
	Writer.WriteAttribute("server_address");
	Writer.WriteStrValue(NormalizeQmServerAddress(aServer).c_str());
	Writer.WriteAttribute("session_id");
	Writer.WriteStrValue(m_aQmAnonymousSessionId);
	Writer.WriteAttribute("players");
	Writer.BeginArray();
	if(aServer[0])
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int Id = GameClient()->m_aLocalIds[Dummy];
			if((Dummy == 1 && !Client()->DummyConnected()) || Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_aClients[Id].m_Active)
				continue;
			Writer.BeginObject();
			Writer.WriteAttribute("player_id");
			Writer.WriteIntValue(Id);
			Writer.WriteAttribute("player_name");
			Writer.WriteStrValue(GameClient()->m_aClients[Id].m_aName);
			Writer.WriteAttribute("dummy");
			Writer.WriteBoolValue(Dummy == 1);
			Writer.EndObject();
		}
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CQmClient::SendQmAnonymousEmoteHello()
{
	if(!m_pQmAnonymousEmote || m_pQmAnonymousEmote->State() != EQmWebSocketState::CONNECTED ||
		Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress())
		return;
	const std::string Body = BuildQmAnonymousEmoteHello();
	if(m_pQmAnonymousEmote->SendText(Body.c_str(), Body.size()))
	{
		m_QmAnonymousHelloBody = Body;
		LogQmWebSocketEvent("anonymous_emote", "hello_sent");
	}
	else
		LogQmWebSocketEvent("anonymous_emote", "hello_send_failed");
}

void CQmClient::UpdateQmAnonymousEmotes()
{
	if(m_QmClientShutdownReported)
		return;
	if(Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress())
	{
		if(m_pQmAnonymousEmote)
			StopQmAnonymousEmotes();
		return;
	}

	if(!m_pQmAnonymousEmote)
	{
		m_pQmAnonymousEmote = CreateQmWebSocketClient({});
		if(m_pQmAnonymousEmote && m_pQmAnonymousEmote->Available())
		{
			IQmWebSocketClient::STuning Tuning;
			Tuning.m_HeartbeatMs = g_Config.m_QmWebSocketHeartbeat * 1000;
			Tuning.m_BackoffBaseMs = g_Config.m_QmWebSocketBackoffBaseMs;
			Tuning.m_BackoffMaxMs = g_Config.m_QmWebSocketBackoffMaxMs;
			Tuning.m_OutgoingQueueCapacity = 16;
			m_pQmAnonymousEmote->SetTuning(Tuning);
		}
	}
	if(!m_pQmAnonymousEmote || !m_pQmAnonymousEmote->Available())
		return;
	if(!m_pQmAnonymousEmote->Desired())
	{
		SQmWebSocketConnectConfig Config;
		const std::string ParseError = ParseQmWebSocketUrl("wss://arghena.site/api/sync/anonymous/ws", Config);
		if(!ParseError.empty())
		{
			LogQmWebSocketEvent("anonymous_emote", "invalid_url");
			return;
		}
		Config.m_Protocol = "qmclient-json";
		Config.m_MaxMessageSize = 128 * 1024;
		std::string Error;
		if(!m_pQmAnonymousEmote->Connect(Config, Error))
		{
			LogQmWebSocketEvent("anonymous_emote", "connect_rejected");
			return;
		}
		LogQmWebSocketEvent("anonymous_emote", "connecting");
	}
	if(m_pQmAnonymousEmote->State() != EQmWebSocketState::CONNECTED)
	{
		if(m_QmAnonymousConnectedTick != 0)
			LogQmWebSocketEvent("anonymous_emote", "disconnected");
		m_QmAnonymousConnectedTick = 0;
		m_QmAnonymousHelloBody.clear();
		m_QmRealtimeEmoticonEvents.clear();
		return;
	}

	const int64_t ConnectedTick = m_pQmAnonymousEmote->LastConnectedTick();
	if(m_QmAnonymousConnectedTick != ConnectedTick)
	{
		LogQmWebSocketEvent("anonymous_emote", "connected");
		m_QmAnonymousConnectedTick = ConnectedTick;
		m_QmAnonymousHelloBody.clear();
		m_QmRealtimeEmoticonEvents.clear();
		secure_random_password(m_aQmAnonymousClientId, sizeof(m_aQmAnonymousClientId), sizeof(m_aQmAnonymousClientId) - 1);
		secure_random_password(m_aQmAnonymousSessionId, sizeof(m_aQmAnonymousSessionId), sizeof(m_aQmAnonymousSessionId) - 1);
		SendQmAnonymousEmoteHello();
	}
	SQmWebSocketMessage Incoming;
	for(int Count = 0; Count < 8 && m_pQmAnonymousEmote->PollMessage(Incoming); ++Count)
	{
		if(Incoming.m_Type != EQmWebSocketMessageType::TEXT)
			continue;
		SQmRealtimeMessage Message;
		if(!ParseQmRealtimeMessage(Incoming.m_Data.c_str(), Incoming.m_Data.size(), Message))
		{
			LogQmWebSocketEvent("anonymous_emote", "event_dropped: invalid_message");
			continue;
		}
		if(Message.m_Type == "hello_ack")
			LogQmWebSocketEvent("anonymous_emote", "hello_ack");
		else if(Message.m_Event == EQmRealtimeEvent::ERROR)
			LogQmWebSocketEvent("anonymous_emote", "server_error");
		else if(Message.m_Event == EQmRealtimeEvent::PING)
			m_pQmAnonymousEmote->SendText("{\"type\":\"pong\"}", 15);
		else if(Message.m_Event == EQmRealtimeEvent::EMOTICON)
			QueueQmAnonymousEmoticonEvent(std::move(Message));
	}
	if(m_pQmAnonymousEmote->State() != EQmWebSocketState::CONNECTED ||
		m_pQmAnonymousEmote->LastConnectedTick() != ConnectedTick)
	{
		m_QmRealtimeEmoticonEvents.clear();
		m_QmAnonymousHelloBody.clear();
		return;
	}
	const int64_t Now = time_get();
	if(Now >= m_QmAnonymousNextHelloCheck)
	{
		m_QmAnonymousNextHelloCheck = Now + time_freq() / 4;
		if(BuildQmAnonymousEmoteHello() != m_QmAnonymousHelloBody)
		{
			m_QmRealtimeEmoticonEvents.clear();
			SendQmAnonymousEmoteHello();
		}
	}
}

void CQmClient::EnqueueQmRealtimeMessage(const char *pData, size_t Size)
{
	SQmRealtimeMessage Message;
	if(!ParseQmRealtimeMessage(pData, Size, Message))
		return;
	// 账号通道不承载匿名表情；两条连接的消息不可互相代送。
	if(Message.m_Event == EQmRealtimeEvent::EMOTICON)
		return;
	if(m_QmRealtimeEvents.size() >= 128)
		m_QmRealtimeEvents.pop_front();
	m_QmRealtimeEvents.push_back(std::move(Message));
}

void CQmClient::QueueQmAnonymousEmoticonEvent(SQmRealtimeMessage Message)
{
	if(!Message.m_HasEmoticon || Client()->State() != IClient::STATE_ONLINE || !Client()->ServerAddress() ||
		Message.m_EmoticonClientId.empty() || Message.m_EmoticonClientId.size() > 64 ||
		Message.m_EmoticonPlayerName.empty() || Message.m_EmoticonPlayerName.size() >= MAX_NAME_LENGTH)
	{
		LogQmWebSocketEvent("anonymous_emote", "event_dropped: invalid_state_or_payload");
		return;
	}
	if(Message.m_EmoticonClientId == m_aQmAnonymousClientId)
	{
		LogQmWebSocketEvent("anonymous_emote", "event_dropped: self");
		return;
	}
	char aServer[NETADDR_MAXSTRSIZE] = "";
	net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	const std::string EventServer = NormalizeQmServerAddress(Message.m_EmoticonServerAddress.c_str());
	if(EventServer.empty() || NormalizeQmServerAddress(aServer) != EventServer)
	{
		LogQmWebSocketEvent("anonymous_emote", "event_dropped: server_mismatch");
		return;
	}

	int PlayerId = Message.m_PlayerId;
	if(PlayerId < 0 || PlayerId >= MAX_CLIENTS ||
		!GameClient()->m_aClients[PlayerId].m_Active ||
		str_comp(GameClient()->m_aClients[PlayerId].m_aName, Message.m_EmoticonPlayerName.c_str()) != 0)
	{
		const int OriginalPlayerId = PlayerId;
		PlayerId = -1;
		for(int Candidate = 0; Candidate < MAX_CLIENTS; ++Candidate)
			if(GameClient()->m_aClients[Candidate].m_Active &&
				str_comp(GameClient()->m_aClients[Candidate].m_aName, Message.m_EmoticonPlayerName.c_str()) == 0)
			{
				PlayerId = Candidate;
				break;
			}
		if(PlayerId >= 0 && g_Config.m_QmWebSocketLog)
			log_info("qmclient", "anonymous_emote player_id_remapped: %d -> %d", OriginalPlayerId, PlayerId);
	}
	if(PlayerId < 0)
	{
		LogQmWebSocketEvent("anonymous_emote", "event_dropped: player_not_found");
		return;
	}
	Message.m_PlayerId = PlayerId;
	Message.m_EmoticonPlayerId = PlayerId;
	if(m_QmRealtimeEmoticonEvents.size() >= 64)
	{
		LogQmWebSocketEvent("anonymous_emote", "event_dropped: queue_full");
		m_QmRealtimeEmoticonEvents.pop_front();
	}
	m_QmRealtimeEmoticonEvents.push_back(std::move(Message));
	LogQmWebSocketEvent("anonymous_emote", "emoticon_received");
}

bool CQmClient::PopQmRealtimeMessage(SQmRealtimeMessage &Message)
{
	if(m_QmRealtimeEvents.empty())
		return false;
	Message = std::move(m_QmRealtimeEvents.front());
	m_QmRealtimeEvents.pop_front();
	return true;
}

bool CQmClient::PopQmRealtimeEmoticon(SQmRealtimeMessage &Message)
{
	if(m_QmRealtimeEmoticonEvents.empty())
		return false;
	Message = std::move(m_QmRealtimeEmoticonEvents.front());
	m_QmRealtimeEmoticonEvents.pop_front();
	return true;
}

void CQmClient::SendQmAnonymousEmoticon(int Emoticon, int PlayerId, bool LaunchMode, bool SuperLaunch)
{
	if(!m_pQmAnonymousEmote || m_pQmAnonymousEmote->State() != EQmWebSocketState::CONNECTED ||
		m_QmAnonymousHelloBody.empty() || m_aQmAnonymousClientId[0] == '\0' ||
		Emoticon < 0 || Emoticon >= NUM_EMOTICONS ||
		PlayerId < 0 || PlayerId >= MAX_CLIENTS || (!LaunchMode && !SuperLaunch))
		return;
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("emoticon");
	Writer.WriteAttribute("emoticon");
	Writer.WriteIntValue(Emoticon);
	Writer.WriteAttribute("player_id");
	Writer.WriteIntValue(PlayerId);
	Writer.WriteAttribute("launch_mode");
	Writer.WriteBoolValue(LaunchMode);
	Writer.WriteAttribute("super_launch");
	Writer.WriteBoolValue(SuperLaunch);
	Writer.EndObject();
	const std::string Body = Writer.GetOutputString();
	if(m_pQmAnonymousEmote->SendText(Body.c_str(), Body.size()))
		LogQmWebSocketEvent("anonymous_emote", "emoticon_sent");
	else
		LogQmWebSocketEvent("anonymous_emote", "emoticon_send_failed");
}

void CQmClient::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && OldState == IClient::STATE_ONLINE)
		EndQmClientLocalModePlaytime();

	if((NewState == IClient::STATE_QUITTING || NewState == IClient::STATE_RESTARTING) && !m_QmClientShutdownReported)
	{
		m_QmClientShutdownReported = true;
		TouchQmClientLifecycleMarker(true);
		SendQmRealtimeStop();
	}
	if(NewState != IClient::STATE_ONLINE && OldState == IClient::STATE_ONLINE)
		StopQmAnonymousEmotes();
	if(NewState == IClient::STATE_ONLINE && OldState != IClient::STATE_ONLINE)
		secure_random_password(m_aQmDeveloperSessionId, sizeof(m_aQmDeveloperSessionId), sizeof(m_aQmDeveloperSessionId) - 1);
}
else if(NewState != IClient::STATE_ONLINE && OldState == IClient::STATE_ONLINE)
{
	StopQmAnonymousEmotes();
	ResetQmDeveloperPresenceTasks();
	ResetTitlePresences();
	m_pQmRealtimeUsersPayload.reset();
	m_QmRealtimeEvents.clear();
	m_QmRealtimeEmoticonEvents.clear();
	GameClient()->ClearQ1menGSyncMarks();
	GameClient()->ClearQmVoiceSyncMarks();
	m_aQmDeveloperSessionId[0] = '\0';
	m_QmRemoteEmoticonEvents.clear();
}
m_QmRealtimePresenceBody.clear();
m_QmRealtimeNextPresenceCheck = 0;
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
	std::lock_guard<std::mutex> Lock(*m_pQmClientLifecycleMarkerMutex);
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
	m_pQmClientLifecycleMarkerWriteJob = std::make_shared<CQmClientLifecycleMarkerWriteJob>(Storage(), aLine, m_pQmClientLifecycleMarkerMutex);
	Engine()->AddJob(m_pQmClientLifecycleMarkerWriteJob);
}

void CQmClient::ClearQmClientLifecycleMarker()
{
	Storage()->RemoveFile(QMCLIENT_LIFECYCLE_MARKER_FILE, IStorage::TYPE_SAVE);
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

void CQmClient::UpdateQmClientLifecycleAndServerTime()
{
	if(m_QmClientStartupSent && !m_QmClientShutdownReported)
		TouchQmClientLifecycleMarker(false);
}

void CQmClient::UpdateQmDdnetPlayerStats()
{
	if(m_pQmDdnetPlayerParseJob && m_pQmDdnetPlayerParseJob->Done())
		FinishQmDdnetPlayerStats();
	if(m_pQmDdnetPlayerTask && m_pQmDdnetPlayerTask->Done())
		FinishQmDdnetPlayerStats();

	SelectQmDdnetPlayerStats(g_Config.m_PlayerName);
	const char *pSelectedPlayerName = m_aQmDdnetPlayerName;
	if(pSelectedPlayerName[0] == '\0')
	{
		m_QmDdnetPlayerState.SetPlayer("");
		if(m_pQmDdnetPlayerTask)
		{
			m_pQmDdnetPlayerTask->Abort();
			m_pQmDdnetPlayerTask = nullptr;
		}
		return;
	}
	if(m_QmDdnetPlayerState.PlayerName() != pSelectedPlayerName)
	{
		if(m_pQmDdnetPlayerTask)
		{
			m_pQmDdnetPlayerTask->Abort();
			m_pQmDdnetPlayerTask = nullptr;
		}
		m_QmDdnetPlayerState.SetPlayer(pSelectedPlayerName);
	}

	if(m_pQmDdnetPlayerParseJob)
		return;
	if(m_pQmDdnetPlayerTask)
		return;

	const int64_t Now = time_get();
	const int64_t SyncIntervalTicks = (int64_t)QMCLIENT_DDNET_PLAYER_SYNC_INTERVAL_SECONDS * time_freq();
	if(!m_QmDdnetPlayerState.ShouldFetch(Now, SyncIntervalTicks))
		return;

	FetchQmDdnetPlayerStats(m_QmDdnetPlayerState.PlayerName().c_str());
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
	if(!m_pQmDdnetPlayerTask)
	{
		m_QmDdnetPlayerState.BeginHttp(pPlayerName);
		m_QmDdnetPlayerState.CompleteHttp(false, time_get(), (int64_t)QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS * time_freq());
		return;
	}
	m_QmDdnetPlayerState.BeginHttp(pPlayerName);
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
		const std::string ParsePlayerName = m_QmDdnetPlayerState.RequestPlayerName();
		CQmDdnetPlayerStatsParseJob::SResult Result = pParseJob->TakeResult();
		m_pQmDdnetPlayerParseJob = nullptr;
		bool StartRefresh = false;
		if(!m_QmDdnetPlayerState.CompleteParse(ParsePlayerName, Result.m_Parsed, time_get(), time_timestamp(), (int64_t)QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS * time_freq(), StartRefresh))
			return;

		if(Result.m_Parsed)
		{
			m_QmDdnetStatsSucceededOnce = true;
			StoreQmDdnetPlayerStats(ParsePlayerName.c_str(), Result.m_FavoritePartner, Result.m_TotalFinishes, Result.m_Points, Result.m_PointsTotal, Result.m_PlaytimeHours, Result.m_PlaytimeHoursPastYear);
			SelectQmDdnetPlayerStats(g_Config.m_PlayerName);
			if(SaveQmClientStatistics() && GameClient() != nullptr)
				GameClient()->m_QmAxiomScores.ClearPersistentCacheDirty();
		}
		else
			log_warn("qmclient", "DDNet statistics response could not be parsed for '%s'", ParsePlayerName.c_str());
		if(StartRefresh && !m_QmDdnetPlayerState.PlayerName().empty())
			FetchQmDdnetPlayerStats(m_QmDdnetPlayerState.PlayerName().c_str());
		return;
	}

	if(!m_pQmDdnetPlayerTask)
		return;

	// libcurl 失败同样会让请求进入终态（Done() 为真但状态非 DONE），
	// 此时调用 StatusCode() 会触发 http.cpp 的断言并弹出崩溃报告
	const EHttpState State = m_pQmDdnetPlayerTask->State();
	const int StatusCode = State == EHttpState::DONE ? m_pQmDdnetPlayerTask->StatusCode() : -1;
	if(State != EHttpState::DONE || StatusCode != 200)
	{
		log_warn("qmclient", "DDNet statistics request failed for '%s': state=%d status=%d",
			m_QmDdnetPlayerState.RequestPlayerName().c_str(), (int)State, StatusCode);
		m_QmDdnetPlayerState.CompleteHttp(false, time_get(), (int64_t)QMCLIENT_DDNET_PLAYER_RETRY_DELAY_SECONDS * time_freq());
		m_pQmDdnetPlayerTask = nullptr;
		return;
	}

	m_pQmDdnetPlayerParseJob = std::make_shared<CQmDdnetPlayerStatsParseJob>(m_pQmDdnetPlayerTask);
	m_QmDdnetPlayerState.CompleteHttp(true, time_get(), 0);
	Engine()->AddJob(m_pQmDdnetPlayerParseJob);
	m_pQmDdnetPlayerTask = nullptr;
}

void CQmClient::RefreshQmDdnetPlayerStats()
{
	if(m_QmDdnetPlayerState.RequestRefresh() == EQmDdnetPlayerStatsRefreshAction::WAIT_FOR_PARSE)
		return;
	if(m_pQmDdnetPlayerParseJob)
	{
		if(!m_pQmDdnetPlayerParseJob->Done())
			return;
		FinishQmDdnetPlayerStats();
	}
	if(m_pQmDdnetPlayerTask)
	{
		m_pQmDdnetPlayerTask->Abort();
		m_pQmDdnetPlayerTask = nullptr;
		m_QmDdnetPlayerState.AbortHttp();
	}
	if(!m_QmDdnetPlayerState.PlayerName().empty())
		FetchQmDdnetPlayerStats(m_QmDdnetPlayerState.PlayerName().c_str());
}

void CQmClient::RefreshQmClientPlaytime()
{
	if(!m_pQmRealtimeTransport || m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED || !m_QmRealtimeHelloSent)
	{
		m_QmClientPlaytimeManualRefreshActive = false;
		m_QmClientPlaytimeManualRefreshFailed = true;
		return;
	}
	m_QmClientPlaytimeManualRefreshActive = true;
	m_QmClientPlaytimeManualRefreshFailed = false;
	const std::string Body = BuildQmRealtimePresence(false);
	if(!m_pQmRealtimeTransport->SendText(Body.c_str(), Body.size()))
	{
		m_QmClientPlaytimeManualRefreshActive = false;
		m_QmClientPlaytimeManualRefreshFailed = true;
	}
	else
	{
		m_QmClientPlaytimeManualRefreshTick = time_get();
		m_QmRealtimePresenceBody = Body;
		m_QmRealtimeLastPresence = m_QmClientPlaytimeManualRefreshTick;
	}
}

void CQmClient::UseCurrentQmDdnetPlayerName()
{
	const char *pPlayerName = g_Config.m_PlayerName;
	if(!pPlayerName || pPlayerName[0] == '\0' || static_cast<size_t>(str_length(pPlayerName)) >= MAX_NAME_LENGTH || !str_utf8_check(pPlayerName))
		return;
	m_QmDdnetPrimaryPlayerName = pPlayerName;
	SelectQmDdnetPlayerStats(pPlayerName);
	m_QmDdnetPlayerState.SetPlayer(m_aQmDdnetPlayerName);
	RefreshQmClientStatistics();
}

void CQmClient::RefreshQmClientStatistics()
{
	RefreshQmClientPlaytime();
	RefreshQmDdnetPlayerStats();
	if(GameClient() != nullptr && m_aQmDdnetPlayerName[0] != '\0')
		GameClient()->m_QmAxiomScores.Refresh(m_aQmDdnetPlayerName);
	if(SaveQmClientStatistics() && GameClient() != nullptr)
		GameClient()->m_QmAxiomScores.ClearPersistentCacheDirty();
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
	m_QmClientServerNow = 0;
	m_QmClientServerSessionStart = 0;
	m_QmClientServerTimeLastSync = 0;
	m_QmClientServerPlaytimeSeconds = -1;
	m_QmClientPlaytimeLastSync = 0;
	m_QmClientPlaytimeLastSuccessfulSyncTimestamp = 0;
	m_QmClientPlaytimeManualRefreshActive = false;
	m_QmClientPlaytimeManualRefreshFailed = false;
}

void CQmClient::InitQmDeveloperAuthentication()
{
	m_aQmDeveloperToken[0] = '\0';
	char *pTokenText = Storage()->ReadFileStr(QMCLIENT_DEVELOPER_TOKEN_FILE, IStorage::TYPE_SAVE);
	if(!pTokenText)
		return;

	char *pToken = str_skip_whitespaces(pTokenText);
	char *pEnd = pToken + str_length(pToken);
	while(pEnd > pToken && std::isspace((unsigned char)pEnd[-1]))
		--pEnd;
	*pEnd = '\0';

	const int TokenLength = str_length(pToken);
	bool Valid = TokenLength == 64;
	for(int i = 0; Valid && i < TokenLength; ++i)
		Valid = std::isxdigit((unsigned char)pToken[i]) != 0;
	if(Valid)
		str_copy(m_aQmDeveloperToken, pToken, sizeof(m_aQmDeveloperToken));
	else
		log_warn("qmclient", "ignored invalid developer credential file");
	free(pTokenText);
}

void CQmClient::ApplyQmRealtimeDevelopers(const json_value *pPayload)
{
	GameClient()->ClearQmDeveloperMarks();
}

bool CQmClient::HasQmClientRecognitionService() const
{
	return m_pQmRealtimeTransport && m_pQmRealtimeTransport->Available();
}

bool CQmClient::QmClientDistributionSyncing() const
{
	return m_QmClientDistribution.IsStale(time_get_impl());
}

void CQmClient::PushQmClientServerCounts()
{
	IServerBrowser *pServerBrowser = ServerBrowser();
	if(pServerBrowser == nullptr)
		return;
	std::unordered_map<std::string, int> Counts;
	Counts.reserve(m_QmClientDistribution.m_vServers.size());
	for(const SQmClientServerDistribution &Distribution : m_QmClientDistribution.m_vServers)
	{
		const int Count = Distribution.m_UserCount + Distribution.m_DummyCount;
		if(Count > 0 && !Distribution.m_ServerAddress.empty())
			Counts.emplace(Distribution.m_ServerAddress, Count);
	}
	pServerBrowser->SetQmClientServerCounts(Counts);
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

void CQmClient::FinishQmClientUsers()
{
	if(m_pQmClientUsersParseJob)
	{
		if(!m_pQmClientUsersParseJob->Done())
			return;

		auto pParseJob = std::static_pointer_cast<CQmClientUsersParseJob>(m_pQmClientUsersParseJob);
		const int64_t ExpireTick = pParseJob->ExpireTick();
		char aCurrentServer[NETADDR_MAXSTRSIZE] = "";
		if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
			net_addr_str(Client()->ServerAddress(), aCurrentServer, sizeof(aCurrentServer), true);
		if(str_comp(aCurrentServer, pParseJob->ServerAddress()) != 0)
		{
			m_pQmClientUsersParseJob.reset();
			return;
		}
		CQmClientUsersParseJob::SResult Result = pParseJob->TakeResult();
		char aCurrentServer[NETADDR_MAXSTRSIZE] = "";
		if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
			net_addr_str(Client()->ServerAddress(), aCurrentServer, sizeof(aCurrentServer), true);
		const bool StaleServer = str_comp(pParseJob->ServerAddress(), aCurrentServer) != 0 ||
					 pParseJob->ConnectionTick() != m_QmRealtimeConnectedTick ||
					 !m_pQmRealtimeTransport || m_pQmRealtimeTransport->State() != EQmWebSocketState::CONNECTED ||
					 m_pQmRealtimeTransport->LastConnectedTick() != m_QmRealtimeConnectedTick;
		m_pQmClientUsersParseJob = nullptr;
		if(StaleServer)
			return;

		if(!m_QmClientDistribution.Apply(Result, ExpireTick))
		{
			m_QmClientDistributionSuccessLatched = false;
			LogQmClientDistributionFailureEvent("parse_failed", "users payload could not be parsed");
			return;
		}

		GameClient()->ClearQ1menGSyncMarks();
		GameClient()->ClearQmVoiceSyncMarks();
		PushQmClientServerCounts();
		if(!m_QmClientDistributionSuccessLatched)
			LogQmClientDistributionEvent("parse_ok", Result.m_OnlineUserCount, Result.m_OnlineDummyCount, (int)Result.m_vLocalServerMarks.size());
		m_QmClientDistributionSuccessLatched = true;
		PushQmClientServerCounts();
		for(const auto &Mark : Result.m_vLocalServerMarks)
		{
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				if(!GameClient()->m_aClients[ClientId].m_Active || str_comp(GameClient()->m_aClients[ClientId].m_aName, Mark.m_Name.c_str()) != 0)
					continue;

				GameClient()->MarkQ1menGSyncClient(ClientId, ExpireTick, Mark.m_Qid.c_str(), Mark.m_ClientBrand);
				if(Mark.m_VoiceSupported)
					GameClient()->MarkQmVoiceSupportedClient(ClientId, ExpireTick);
				break;
			}
		}
		return;
	}
}

void CQmClient::UpdateQmClientRecognition()
{
	if(m_pQmClientUsersParseJob && m_pQmClientUsersParseJob->Done())
		FinishQmClientUsers();
	if(!m_pQmClientUsersParseJob && m_pQmRealtimeUsersPayload)
	{
		m_pQmClientUsersParseJob = std::make_shared<CQmClientUsersParseJob>(
			std::move(m_pQmRealtimeUsersPayload), m_aQmRealtimeUsersServer, m_QmRealtimeUsersExpireTick, m_QmRealtimeConnectedTick);
		Engine()->AddJob(m_pQmClientUsersParseJob);
	}
}

static bool IsTitleHex(const char *pText, int Length)
{
	if(str_length(pText) != Length)
		return false;
	for(int i = 0; i < Length; ++i)
		if(!((pText[i] >= '0' && pText[i] <= '9') || (pText[i] >= 'a' && pText[i] <= 'f')))
			return false;
	return true;
}

static const char *TitleJsonString(const json_value *pRoot, const char *pKey)
{
	if(!pRoot || pRoot->type != json_object)
		return "";
	const json_value *pValue = json_object_get(pRoot, pKey);
	return pValue->type == json_string ? pValue->u.string.ptr : "";
}

std::string CQmClient::BuildQmRealtimePresence(bool Hello) const
{
	char aServer[NETADDR_MAXSTRSIZE] = "";
	if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
		net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue(Hello ? "hello" : "presence");
	if(Hello)
	{
		Writer.WriteAttribute("v");
		Writer.WriteIntValue(QMCLIENT_REALTIME_PROTOCOL_VERSION);
		Writer.WriteAttribute("client_version");
		Writer.WriteStrValue(QMCLIENT_VERSION);
		Writer.WriteAttribute("machine_hash");
		Writer.WriteStrValue(m_aQmClientMachineHash);
		Writer.WriteAttribute("client_id");
		Writer.WriteStrValue(m_aQmClientPlaytimeClientId);
		Writer.WriteAttribute("player_name");
		Writer.WriteStrValue(g_Config.m_PlayerName);
		if(m_QmClientAwaitingRecoveryStop)
		{
			Writer.WriteAttribute("recovery_stop_at");
			Writer.WriteIntValue((int)std::clamp<int64_t>(m_QmClientRecoveryStopAt, 0, std::numeric_limits<int>::max()));
		}
	}
	Writer.WriteAttribute("server_address");
	Writer.WriteStrValue(aServer);
	Writer.WriteAttribute("session_id");
	Writer.WriteStrValue(m_aQmDeveloperSessionId);
	if(QmRealtimeAllowsCredentials(m_aQmRealtimeUrl))
	{
		Writer.WriteAttribute("title_token");
		Writer.WriteStrValue(m_aTitleToken);
		Writer.WriteAttribute("developer_token");
		Writer.WriteStrValue(m_aQmDeveloperToken);
	}
	Writer.WriteAttribute("players");
	Writer.BeginArray();
	if(aServer[0] && m_aQmDeveloperSessionId[0])
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int Id = GameClient()->m_aLocalIds[Dummy];
			if((Dummy == 1 && !Client()->DummyConnected()) || Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_aClients[Id].m_Active)
				continue;
			Writer.BeginObject();
			Writer.WriteAttribute("player_id");
			Writer.WriteIntValue(Id);
			Writer.WriteAttribute("player_name");
			Writer.WriteStrValue(GameClient()->m_aClients[Id].m_aName);
			Writer.WriteAttribute("dummy");
			Writer.WriteBoolValue(Dummy == 1);
			Writer.WriteAttribute("voice_supported");
			Writer.WriteBoolValue(true);
			Writer.EndObject();
		}
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

void CQmClient::SendQmRealtimeStop()
{
	if(!QmRealtimeConnected())
		return;
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("stop");
	Writer.WriteAttribute("stop_at");
	Writer.WriteIntValue((int)std::clamp<int64_t>(time_timestamp(), 0, std::numeric_limits<int>::max()));
	Writer.EndObject();
	const std::string Body = Writer.GetOutputString();
	m_pQmRealtime->SendText(Body.c_str(), Body.size());
}

void CQmClient::ApplyQmRealtimeTitleProfile(const json_value *pPayload)
{
	if(TitleBusy())
		return;
	const char *pTitle = TitleJsonString(pPayload, "title");
	const char *pName = TitleJsonString(pPayload, "bound_name");
	const char *pStyle = TitleJsonString(pPayload, "style");
	const json_value *pStatus = JsonObjectField(pPayload, "status");
	const bool Authenticated = pStatus->type == json_integer && pStatus->u.integer == 200 && IsValidQmTitle(pTitle);
	const bool Changed = m_TitleAuthenticated != Authenticated || str_comp(m_aTitleText, pTitle) || str_comp(m_aTitleBoundName, pName) || str_comp(m_aTitleProfileStyle, pStyle);
	m_TitleAuthenticated = Authenticated;
	str_copy(m_aTitleText, pTitle);
	str_copy(m_aTitleBoundName, pName);
	str_copy(m_aTitleProfileStyle, pStyle);
	m_pTitleStatus = Authenticated ? Localizable("Permanent sponsor verified") : Localizable("Enter your sponsor code");
	if(Changed)
		++m_TitleRevision;
}

void CQmClient::ApplyQmRealtimeServices(const SQmRealtimeMessage &Message)
{
	const json_value *pPayload = Message.m_pPayload.get();
	if(!pPayload)
		return;
	if(Message.m_Event == EQmRealtimeEvent::USERS)
	{
		char aServer[NETADDR_MAXSTRSIZE] = "";
		if(Client()->State() == IClient::STATE_ONLINE && Client()->ServerAddress())
			net_addr_str(Client()->ServerAddress(), aServer, sizeof(aServer), true);
		const json_value *pAddress = JsonObjectField(pPayload, "server_address");
		if(pAddress->type != json_string || str_comp(pAddress->u.string.ptr, aServer) != 0)
			return;
		if(JsonObjectField(pPayload, "users")->type != json_array)
			return;
		m_pQmRealtimeUsersPayload = Message.m_pPayload;
		str_copy(m_aQmRealtimeUsersServer, aServer);
		m_QmRealtimeUsersExpireTick = time_get_impl() + 20 * time_freq();
	}
	else if(Message.m_Event == EQmRealtimeEvent::DEVELOPERS)
		ApplyQmRealtimeDevelopers(pPayload);
	else if(Message.m_Event == EQmRealtimeEvent::TITLE_PROFILE)
		ApplyQmRealtimeTitleProfile(pPayload);
	else if(Message.m_Event == EQmRealtimeEvent::TITLE_STATUS)
	{
		const json_value *pStatus = JsonObjectField(pPayload, "status");
		if(pStatus->type == json_integer && pStatus->u.integer == 409)
			m_pTitleStatus = Localizable("Four IP addresses are already online");
	}
	else if(Message.m_Event == EQmRealtimeEvent::TIME || Message.m_Event == EQmRealtimeEvent::PLAYTIME)
	{
		int64_t Value = 0;
		if(JsonReadNonNegativeInt64(JsonObjectField(pPayload, "ts"), Value) && Value > 0)
		{
			m_QmClientServerNow = Value;
			m_QmClientServerTimeLastSync = time_get();
		}
		if(Message.m_Event == EQmRealtimeEvent::PLAYTIME)
		{
			if(JsonReadNonNegativeInt64(JsonObjectField(pPayload, "total_seconds"), Value))
				m_QmClientServerPlaytimeSeconds = Value;
			if(JsonReadNonNegativeInt64(JsonObjectField(pPayload, "last_start_at"), Value) && Value > 0)
				m_QmClientServerSessionStart = Value;
			m_QmClientPlaytimeLastSync = time_get();
			if(str_comp(TitleJsonString(pPayload, "action"), "start") == 0)
			{
				if(!m_QmClientStartupSent)
				{
					m_QmClientMarkerStartedAt = time_timestamp();
					m_QmClientMarkerLastSeenAt = m_QmClientMarkerStartedAt;
					m_QmClientStartupSent = true;
					m_QmClientAwaitingRecoveryStop = false;
					WriteQmClientLifecycleMarker();
				}
			}
			else if(str_comp(TitleJsonString(pPayload, "action"), "stop") == 0)
				ClearQmClientLifecycleMarker();
		}
	}
	else if(Message.m_Event == EQmRealtimeEvent::ERROR)
		LogQmRealtimeEvent("service_error", TitleJsonString(pPayload, "error"));
}

static constexpr const char *TITLE_TOKEN_FILE = "qmclient/title_token.txt";

void CQmClient::InitTitleAuthentication()
{
	char *pToken = Storage()->ReadFileStr(TITLE_TOKEN_FILE, IStorage::TYPE_SAVE);
	if(!pToken)
		return;
	str_utf8_trim_right(pToken);
	if(IsTitleHex(pToken, 64))
		str_copy(m_aTitleToken, pToken);
	free(pToken);
}

void CQmClient::StartTitleRequest(const char *pPath, const char *pBody, std::shared_ptr<IHttpRequest> &pTask)
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://qmclient.icu/api/v1/titles/%s", pPath);
	pTask = pBody ? HttpPostJson(aUrl, pBody) : HttpGet(aUrl);
	pTask->MaxResponseSize(128 * 1024);
	pTask->Timeout(CTimeout{3000, 5000, 500, 5});
	// 标题接口用 4xx 携带 error JSON，不能让 FAILONERROR 把响应变成 ERROR
	pTask->FailOnErrorStatus(false);
	if(m_aTitleToken[0] && (str_comp(pPath, "profile") == 0 || str_comp(pPath, "presence") == 0))
	{
		char aAuthorization[80];
		str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", m_aTitleToken);
		pTask->HeaderString("Authorization", aAuthorization);
	}
	Http()->Run(pTask);
}

void CQmClient::RedeemTitleCode(const char *pCode)
{
	if(TitleBusy() || m_TitleAuthenticated)
		return;
	if(!IsTitleHex(pCode, 48))
	{
		m_pTitleStatus = Localizable("Invalid sponsor code");
		return;
	}
	if(!m_aTitleToken[0])
	{
		unsigned char aRandom[32];
		secure_random_fill(aRandom, sizeof(aRandom));
		static constexpr const char HEX[] = "0123456789abcdef";
		for(size_t i = 0; i < sizeof(aRandom); ++i)
		{
			m_aTitleToken[i * 2] = HEX[aRandom[i] >> 4];
			m_aTitleToken[i * 2 + 1] = HEX[aRandom[i] & 15];
		}
		m_aTitleToken[64] = '\0';
		Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		IOHANDLE File = Storage()->OpenFile(TITLE_TOKEN_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		bool Saved = false;
		if(File)
		{
			const unsigned Written = io_write(File, m_aTitleToken, 64);
			const int Closed = io_close(File);
			Saved = Written == 64 && Closed == 0;
		}
		if(!Saved)
		{
			m_aTitleToken[0] = '\0';
			m_pTitleStatus = Localizable("Could not save title credential");
			return;
		}
	}
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("code");
	Writer.WriteStrValue(pCode);
	Writer.WriteAttribute("token");
	Writer.WriteStrValue(m_aTitleToken);
	Writer.EndObject();
	StartTitleRequest("redeem", Writer.GetOutputString().c_str(), m_pTitleOperation);
	m_pTitleStatus = Localizable("Contacting title server");
}

void CQmClient::RefreshTitleProfile()
{
	if(TitleBusy() || !m_aTitleToken[0])
		return;
	StartTitleRequest("profile", nullptr, m_pTitleOperation);
	m_pTitleStatus = Localizable("Contacting title server");
}

void CQmClient::SaveTitleProfile(const char *pTitle, const char *pBoundName, const char *pStyle)
{
	if(TitleBusy() || !m_TitleAuthenticated)
		return;
	if(!IsValidQmTitle(pTitle))
	{
		m_pTitleStatus = Localizable("Title too long or contains unsupported characters");
		return;
	}
	const char *pStyleId = pStyle != nullptr ? pStyle : "";
	if(pStyleId[0] != '\0' && QmTitleStyleById(pStyleId) == nullptr)
	{
		m_pTitleStatus = Localizable("Unknown title style");
		return;
	}
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("title");
	Writer.WriteStrValue(pTitle);
	Writer.WriteAttribute("bound_name");
	Writer.WriteStrValue(pBoundName);
	Writer.WriteAttribute("style");
	Writer.WriteStrValue(pStyleId);
	Writer.EndObject();
	StartTitleRequest("profile", Writer.GetOutputString().c_str(), m_pTitleOperation);
	m_pTitleStatus = Localizable("Contacting title server");
}

void CQmClient::ResetTitlePresences()
{
	mem_zero(m_aTitleExpires, sizeof(m_aTitleExpires));
}

const char *CQmClient::PlayerTitle(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active || GameClient()->ShouldHideStreamerIdentity(ClientId))
		return "";
	if(GameClient()->IsQmDeveloperAuthenticated(ClientId))
		return "[开发者]";
	if(m_aTitleExpires[ClientId] > time_get() && str_comp(m_aaTitleNames[ClientId], GameClient()->m_aClients[ClientId].m_aName) == 0)
		return m_aaPlayerTitles[ClientId];
	return "";
}

const char *CQmClient::PlayerTitleStyle(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active || GameClient()->ShouldHideStreamerIdentity(ClientId))
		return "";
	return m_aaPlayerTitleStyles[ClientId];
}

double CQmClient::TitleAnimationTime() const
{
	return QmTitleAnimationTime(Client()->GlobalTime(), m_TitleServerTimeOffset, m_TitleServerTimeOffsetValid);
}

void CQmClient::UpdateTitleAuthentication()
{
	if(m_pTitleOperation && m_pTitleOperation->Done())
	{
		// libcurl failures also make Done() true, but they do not produce a
		// completed HTTP result.  ResultJson() asserts in that state.
		if(m_pTitleOperation->State() != EHttpState::DONE)
		{
			m_pTitleStatus = Localizable("Title server unavailable; retry");
			m_pTitleOperation.reset();
			return;
		}
		json_value *pRoot = m_pTitleOperation->ResultJson();
		const char *pTitle = TitleJsonString(pRoot, "title");
		const char *pName = TitleJsonString(pRoot, "bound_name");
		if(m_pTitleOperation->State() == EHttpState::DONE && m_pTitleOperation->StatusCode() == 200 && IsValidQmTitle(pTitle))
		{
			m_TitleAuthenticated = true;
			str_copy(m_aTitleText, pTitle);
			str_copy(m_aTitleBoundName, pName);
			str_copy(m_aTitleProfileStyle, TitleJsonString(pRoot, "style"));
			++m_TitleRevision;
			m_pTitleStatus = Localizable("Permanent sponsor verified");
			ResetTitlePresences();
		}
		else
		{
			const char *pError = TitleJsonString(pRoot, "error");
			m_pTitleStatus = Localizable("Title server unavailable; retry");
			if(str_comp(pError, "code_used") == 0)
				m_pTitleStatus = Localizable("Sponsor code already claimed");
			else if(str_comp(pError, "invalid_code") == 0)
				m_pTitleStatus = Localizable("Invalid sponsor code");
			else if(str_comp(pError, "invalid_title") == 0)
				m_pTitleStatus = Localizable("Title too long or contains unsupported characters");
			else if(str_comp(pError, "invalid_name") == 0)
				m_pTitleStatus = Localizable("Invalid bound name");
			else if(m_pTitleOperation->StatusCode() == 401)
			{
				m_TitleAuthenticated = false;
				m_pTitleStatus = Localizable("Enter your sponsor code");
			}
		}
		json_value_free(pRoot);
		m_pTitleOperation.reset();
		RequestQmRealtimeTitleRefresh();
	}
}
