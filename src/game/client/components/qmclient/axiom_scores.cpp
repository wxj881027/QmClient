#include "axiom_scores.h"

#include "axiom_scores_parse_job.h"
#include "qm_json_fields.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
	constexpr int64_t AXIOM_MATCH_CACHE_TTL_MS = 2 * 60 * 60 * 1000;
	constexpr int64_t AXIOM_POINTS_CACHE_TTL_MS = 30 * 60 * 1000;
	constexpr int64_t AXIOM_FAILURE_RETRY_MS = 30 * 1000;
	// 连续失败的退避上限：5 分钟后不再增长，避免服务端长时间不可用时持续重打。
	constexpr int64_t AXIOM_FAILURE_RETRY_MAX_MS = 5 * 60 * 1000;
	// dyl 管线使用的旧常量名(值与上游改名后的 MATCH/POINTS 一致)。
	constexpr int64_t AXIOM_SEARCH_CACHE_TTL_MS = AXIOM_MATCH_CACHE_TTL_MS;
	constexpr int64_t AXIOM_SCORE_CACHE_TTL_MS = AXIOM_POINTS_CACHE_TTL_MS;
	constexpr int64_t AXIOM_MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
	constexpr size_t AXIOM_MAX_CACHE_ENTRIES = 128;
	constexpr size_t AXIOM_MAX_CONCURRENT_REQUESTS = 64;
	constexpr int AXIOM_SEARCH_STARTS_PER_FRAME = 2;
	constexpr size_t AXIOM_MAX_QUERY_NAME_BYTES = 256;
	constexpr size_t AXIOM_MAX_DIFFICULTY_NAME_BYTES = 192;
	constexpr size_t DDSTATS_MAX_GAMETYPE_NAME_BYTES = 256;
	constexpr int AXIOM_CONNECT_TIMEOUT_MS = 5000;
	constexpr int AXIOM_SEARCH_TIMEOUT_MS = 10000;
	// DDStats 的 player/json 要现算全历史 points_graph，实测 572 KB 正文耗时 4-37 秒；
	// 原先 30 秒的阈值紧贴实测上界，会把正常响应判成超时（libcurl error 28）。
	// 放宽到 90 秒覆盖服务端慢响应，与 AXIOM_INFO_TIMEOUT_MS 一致。
	constexpr int DDSTATS_TIMEOUT_MS = 90000;
	// Axiom 的 Gores 模式 user/info 要现算全难度逐图统计，实测服务端耗时 35-47 秒；
	// 原先 45 秒的阈值紧贴实测上界，会把正常响应判成超时（libcurl error 28）。
	// 放宽到 90 秒覆盖服务端慢响应，避免统计页抽风。
	constexpr int AXIOM_INFO_TIMEOUT_MS = 90000;

	template<typename TRequest>
	void TryAbortRequest(const std::shared_ptr<TRequest> &pRequest) noexcept
	{
		if(!pRequest)
			return;
		try
		{
			pRequest->Abort();
		}
		catch(...)
		{
			// 单个外部取消失败不能中断其余请求回收和代际失效。
		}
	}

	void SetErrorDetail(std::string &Detail, const char *pText) noexcept
	{
		try
		{
			Detail = pText;
		}
		catch(...)
		{
			// 错误状态仍然可见，不让可选说明文本妨碍失败收尾。
			Detail.clear();
		}
	}

	const json_value *JsonField(const json_value *pObject, const char *pName)
	{
		return qm_json::Field(pObject, pName);
	}

	bool ReadText(const json_value *pValue, size_t MaxBytes, bool AllowEmpty = true)
	{
		return pValue && pValue->type == json_string && pValue->u.string.length <= MaxBytes && qm_json::Text(pValue, AllowEmpty);
	}

	bool ReadInt64(const json_value *pObject, const char *pName, int64_t &Out)
	{
		const json_value *pValue = JsonField(pObject, pName);
		if(pValue->type == json_integer)
		{
			Out = pValue->u.integer;
			return true;
		}
		// 持久化数值使用规范十进制字符串；先检查完整长度，不能接受 NUL 前缀。
		if(!ReadText(pValue, 20, false))
			return false;
		const char *pText = json_string_get(pValue);
		const int64_t Value = str_toint64_base(pText);
		char aCanonical[64];
		str_format(aCanonical, sizeof(aCanonical), "%lld", (long long)Value);
		if(str_comp(aCanonical, pText) != 0)
			return false;
		Out = Value;
		return true;
	}

	bool ReadNonNegativeInt64(const json_value *pObject, const char *pName, int64_t &Out)
	{
		int64_t Value;
		if(!ReadInt64(pObject, pName, Value) || Value < 0)
			return false;
		Out = Value;
		return true;
	}

	void WriteInt64(CJsonWriter &Writer, const char *pName, int64_t Value)
	{
		char aValue[64];
		str_format(aValue, sizeof(aValue), "%lld", (long long)Value);
		Writer.WriteAttribute(pName);
		Writer.WriteStrValue(aValue);
	}

	void WriteOptionalInt64(CJsonWriter &Writer, const char *pName, const std::optional<int64_t> &Value)
	{
		if(Value)
			WriteInt64(Writer, pName, *Value);
	}

	EQmAxiomScoreStatus ParseStatus(EQmAxiomParseResult Result)
	{
		switch(Result)
		{
		case EQmAxiomParseResult::SUCCESS: return EQmAxiomScoreStatus::READY;
		case EQmAxiomParseResult::NOT_FOUND: return EQmAxiomScoreStatus::NOT_FOUND;
		case EQmAxiomParseResult::AMBIGUOUS: return EQmAxiomScoreStatus::AMBIGUOUS;
		case EQmAxiomParseResult::API_ERROR: return EQmAxiomScoreStatus::API_ERROR;
		case EQmAxiomParseResult::INVALID_RESPONSE: return EQmAxiomScoreStatus::INVALID_RESPONSE;
		}
		return EQmAxiomScoreStatus::INVALID_RESPONSE;
	}

	void PrepareRequest(IHttpRequest *pRequest, int TimeoutMs)
	{
		pRequest->Timeout(CTimeout{AXIOM_CONNECT_TIMEOUT_MS, TimeoutMs, 0, 0});
		pRequest->MaxResponseSize(AXIOM_MAX_RESPONSE_BYTES);
		// 固定走 IPv4：这些站点都有 AAAA 记录，但部分网络只有 DNS 而没有实际 IPv6
		// 路由，curl 的 WHATEVER 模式优先尝试 IPv6 会在几十毫秒内失败
		// （WinError 10051），表现为 schannel 握手失败或 TLS 连接超时。
		// 引擎对 info.ddnet.org 已采用同样处理，见 client.cpp 的 DDNetInfo 任务。
		pRequest->IpResolve(IPRESOLVE::V4);
		// 保留真实网络错误的 HTTP 日志；同名请求的重复取消由查询状态机去重。
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("User-Agent", "QmClient (https://github.com/wxj881027/QmClient)");
	}

	class CNativeAxiomHttpRequest final : public IQmAxiomHttpRequest
	{
		std::shared_ptr<IHttpRequest> m_pRequest;

	public:
		explicit CNativeAxiomHttpRequest(std::shared_ptr<IHttpRequest> pRequest) :
			m_pRequest(std::move(pRequest))
		{
		}

		bool Done() const override { return m_pRequest->Done(); }
		bool TransportSucceeded() const override { return m_pRequest->State() == EHttpState::DONE; }
		int StatusCode() const override { return m_pRequest->StatusCode(); }
		void Result(const unsigned char **ppData, size_t *pDataSize) const override
		{
			unsigned char *pData = nullptr;
			m_pRequest->Result(&pData, pDataSize);
			*ppData = pData;
		}
		void Abort() override { m_pRequest->Abort(); }
		const char *ErrorDetail() const override
		{
			// 传输失败时把可读原因暂存在成员里，供 UI 显示具体错误。
			switch(m_pRequest->State())
			{
			case EHttpState::DONE:
				if(m_pRequest->StatusCode() == 200)
					return "";
				str_format(m_aErrorDetail, sizeof(m_aErrorDetail), "HTTP %d", m_pRequest->StatusCode());
				return m_aErrorDetail;
			case EHttpState::ERROR:
				str_copy(m_aErrorDetail, "network error", sizeof(m_aErrorDetail));
				return m_aErrorDetail;
			case EHttpState::ABORTED:
				str_copy(m_aErrorDetail, "aborted", sizeof(m_aErrorDetail));
				return m_aErrorDetail;
			default:
				str_copy(m_aErrorDetail, "pending", sizeof(m_aErrorDetail));
				return m_aErrorDetail;
			}
		}

	private:
		// mutable：ErrorDetail() 为 const，但需要惰性生成错误文本。
		mutable char m_aErrorDetail[64] = "";
	};
}

void CQmAxiomScores::LoadPersistentCache(const json_value *pRoot) noexcept
try
{
	if(!pRoot || pRoot->type != json_object)
		return;
	const json_value *pRemote = JsonField(pRoot, "remote");
	const json_value *pAxiom = JsonField(pRemote, "axiom");
	const json_value *pPlayers = JsonField(pAxiom, "players");
	if(pPlayers->type != json_array)
		return;
	for(unsigned PlayerIndex = 0; PlayerIndex < pPlayers->u.array.length; ++PlayerIndex)
	{
		const json_value *pPlayer = pPlayers->u.array.values[PlayerIndex];
		const json_value *pName = JsonField(pPlayer, "name");
		int64_t UserId = 0;
		if(!ReadText(pName, AXIOM_MAX_QUERY_NAME_BYTES, false) || !ReadInt64(pPlayer, "user_id", UserId) || UserId <= 0)
			continue;
		// 恢复只补空余位置；满额后仍允许更新已有名字，不淘汰当前可用记录。
		if(m_Cache.size() >= AXIOM_MAX_CACHE_ENTRIES && !m_Cache.contains(json_string_get(pName)))
			continue;
		SCacheEntry Entry;
		Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::READY;
		Entry.m_Result.m_Match.m_UserId = UserId;
		const json_value *pPlayerName = JsonField(pPlayer, "player_name");
		const json_value *pDummyName = JsonField(pPlayer, "dummy_name");
		if(ReadText(pPlayerName, AXIOM_MAX_QUERY_NAME_BYTES))
			Entry.m_Result.m_Match.m_PlayerName = json_string_get(pPlayerName);
		if(ReadText(pDummyName, AXIOM_MAX_QUERY_NAME_BYTES))
			Entry.m_Result.m_Match.m_DummyName = json_string_get(pDummyName);
		const json_value *pModes = JsonField(pPlayer, "modes");
		if(pModes->type != json_array)
			continue;
		const json_value *pDdStatsGameTypes = JsonField(pPlayer, "ddstats_gametypes");
		if(pDdStatsGameTypes->type == json_array && pDdStatsGameTypes->u.array.length <= QM_DDSTATS_MAX_GAME_TYPE_RECORDS)
		{
			Entry.m_vDdStatsGameTypes.reserve(std::min(pDdStatsGameTypes->u.array.length, QM_DDSTATS_MAX_GAME_TYPES));
			const auto Less = [](std::string_view Left, std::string_view Right) {
				return str_comp_nocase(Left.data(), Right.data()) < 0;
			};
			std::map<std::string_view, size_t, decltype(Less)> Indices(Less);
			for(unsigned GameTypeIndex = 0; GameTypeIndex < pDdStatsGameTypes->u.array.length; ++GameTypeIndex)
			{
				const json_value *pGameType = pDdStatsGameTypes->u.array.values[GameTypeIndex];
				const json_value *pGameTypeName = JsonField(pGameType, "name");
				int64_t PlayTimeSeconds = 0;
				if(!ReadText(pGameTypeName, DDSTATS_MAX_GAMETYPE_NAME_BYTES, false) || !ReadNonNegativeInt64(pGameType, "play_time_seconds", PlayTimeSeconds))
					continue;
				SQmDdStatsGameType GameType;
				GameType.m_Name = json_string_get(pGameTypeName);
				GameType.m_PlayTimeSeconds = PlayTimeSeconds;
				const auto Existing = Indices.find(GameType.m_Name);
				if(Existing == Indices.end())
				{
					if(Entry.m_vDdStatsGameTypes.size() >= QM_DDSTATS_MAX_GAME_TYPES)
					{
						// 超限不能把前半份类型列表当成完整权威时长。
						Indices.clear();
						Entry.m_vDdStatsGameTypes.clear();
						break;
					}
					Entry.m_vDdStatsGameTypes.push_back(std::move(GameType));
					Indices.emplace(Entry.m_vDdStatsGameTypes.back().m_Name, Entry.m_vDdStatsGameTypes.size() - 1);
				}
				else
					Entry.m_vDdStatsGameTypes[Existing->second].m_PlayTimeSeconds = PlayTimeSeconds;
			}
		}
		for(const SQmDdStatsGameType &GameType : Entry.m_vDdStatsGameTypes)
		{
			if(str_comp_nocase(GameType.m_Name.c_str(), "Gores") == 0)
			{
				Entry.m_aDdStatsPlayTime[0] = GameType.m_PlayTimeSeconds;
				Entry.m_aHasDdStatsPlayTime[0] = true;
			}
			else if(str_comp_nocase(GameType.m_Name.c_str(), "AXRace") == 0)
			{
				Entry.m_aDdStatsPlayTime[1] = GameType.m_PlayTimeSeconds;
				Entry.m_aHasDdStatsPlayTime[1] = true;
			}
		}
		bool HasMode = false;
		std::array<bool, 2> SeenModes{};
		for(unsigned ModeIndex = 0; ModeIndex < pModes->u.array.length; ++ModeIndex)
		{
			const json_value *pMode = pModes->u.array.values[ModeIndex];
			const json_value *pModeName = JsonField(pMode, "mode");
			if(!ReadText(pModeName, 6, false))
				continue;
			EQmAxiomMode Mode;
			if(str_comp_nocase(json_string_get(pModeName), "AXRace") == 0)
				Mode = EQmAxiomMode::AXRACE;
			else if(str_comp_nocase(json_string_get(pModeName), "Gores") == 0)
				Mode = EQmAxiomMode::GORES;
			else
				continue;
			const int Index = Mode == EQmAxiomMode::AXRACE ? 1 : 0;
			if(SeenModes[Index])
				continue;
			SQmAxiomModeScore &Score = Entry.m_Result.m_aModes[Index].m_Score;
			int64_t Value = 0;
			if(!ReadNonNegativeInt64(pMode, "points", Value) ||
				!ReadNonNegativeInt64(pMode, "total_play_time", Score.m_TotalPlayTime) ||
				!ReadNonNegativeInt64(pMode, "total_maps_completed", Score.m_TotalMapsCompleted) ||
				!ReadNonNegativeInt64(pMode, "performance_points", Score.m_PerformancePoints) ||
				!ReadNonNegativeInt64(pMode, "mileage", Score.m_Mileage))
				continue;
			SeenModes[Index] = true;
			Score.m_Points = Value;
			Value = 0;
			ReadNonNegativeInt64(pMode, "global_rank", Value);
			if(Value > 0)
				Score.m_GlobalRank = Value;
			Value = 0;
			ReadNonNegativeInt64(pMode, "team_rank", Value);
			if(Value > 0)
				Score.m_TeamRank = Value;
			Score.m_PlayerName = Entry.m_Result.m_Match.m_PlayerName;
			int64_t PersistedAxiomPlayTime = Score.m_TotalPlayTime;
			int64_t OriginalPlayTime = 0;
			if(ReadNonNegativeInt64(pMode, "axiom_play_time", OriginalPlayTime))
				PersistedAxiomPlayTime = OriginalPlayTime;
			Entry.m_aAxiomPlayTime[Index] = PersistedAxiomPlayTime;
			Entry.m_aHasAxiomPlayTime[Index] = true;
			if(Entry.m_aHasDdStatsPlayTime[Index])
				Score.m_TotalPlayTime = Entry.m_aDdStatsPlayTime[Index];
			const json_value *pDifficulties = JsonField(pMode, "difficulties");
			if(pDifficulties->type == json_array && pDifficulties->u.array.length <= 128)
			{
				bool ValidDifficulties = true;
				for(unsigned DifficultyIndex = 0; DifficultyIndex < pDifficulties->u.array.length; ++DifficultyIndex)
				{
					const json_value *pDifficulty = pDifficulties->u.array.values[DifficultyIndex];
					const json_value *pDifficultyName = JsonField(pDifficulty, "name");
					SQmAxiomDifficultyStats Difficulty;
					int64_t DifficultyValue = 0;
					if(!ReadText(pDifficultyName, AXIOM_MAX_DIFFICULTY_NAME_BYTES, false) || !ReadNonNegativeInt64(pDifficulty, "points", Difficulty.m_Points) || !ReadNonNegativeInt64(pDifficulty, "completed_maps", Difficulty.m_CompletedMaps) || !ReadNonNegativeInt64(pDifficulty, "remaining_maps", Difficulty.m_RemainingMaps))
					{
						ValidDifficulties = false;
						break;
					}
					Difficulty.m_Name = json_string_get(pDifficultyName);
					DifficultyValue = 0;
					if(ReadNonNegativeInt64(pDifficulty, "global_rank", DifficultyValue) && DifficultyValue > 0)
						Difficulty.m_GlobalRank = DifficultyValue;
					DifficultyValue = 0;
					if(ReadNonNegativeInt64(pDifficulty, "team_rank", DifficultyValue) && DifficultyValue > 0)
						Difficulty.m_TeamRank = DifficultyValue;
					DifficultyValue = 0;
					if(ReadNonNegativeInt64(pDifficulty, "total_points", DifficultyValue))
						Difficulty.m_TotalPoints = DifficultyValue;
					DifficultyValue = 0;
					if(ReadNonNegativeInt64(pDifficulty, "total_maps", DifficultyValue))
						Difficulty.m_TotalMaps = DifficultyValue;
					Score.m_vDifficulties.push_back(std::move(Difficulty));
				}
				if(!ValidDifficulties)
					Score.m_vDifficulties.clear();
			}
			Entry.m_Result.m_aModes[Index].m_Status = EQmAxiomScoreStatus::READY;
			Entry.m_Result.m_aModes[Index].m_HasData = true;
			HasMode = true;
		}
		if(!HasMode && Entry.m_vDdStatsGameTypes.empty())
			continue;
		// Persisted data remains visible, but is refreshed in the background on startup.
		Entry.m_LastSearchSuccessTick = 0;
		for(int Index = 0; Index < 2; ++Index)
			Entry.m_aLastModeSuccessTick[Index] = 0;
		m_Cache[json_string_get(pName)] = std::move(Entry);
	}
}
catch(...)
{
	// 当前条目在本地准备；失败保留旧记录及此前已完整加载的条目。
}

void CQmAxiomScores::WritePersistentCache(CJsonWriter &Writer) const
{
	Writer.WriteAttribute("axiom");
	Writer.BeginObject();
	Writer.WriteAttribute("players");
	Writer.BeginArray();
	for(const auto &[Name, Entry] : m_Cache)
	{
		if(Entry.m_Result.m_Match.m_UserId <= 0)
			continue;
		bool HasReadyMode = false;
		for(const SQmAxiomModeResult &ModeResult : Entry.m_Result.m_aModes)
			HasReadyMode |= ModeResult.m_HasData;
		if(!HasReadyMode && Entry.m_vDdStatsGameTypes.empty())
			continue;
		Writer.BeginObject();
		Writer.WriteAttribute("name");
		Writer.WriteStrValue(Name.c_str());
		WriteInt64(Writer, "user_id", Entry.m_Result.m_Match.m_UserId);
		Writer.WriteAttribute("player_name");
		Writer.WriteStrValue(Entry.m_Result.m_Match.m_PlayerName.c_str());
		Writer.WriteAttribute("dummy_name");
		Writer.WriteStrValue(Entry.m_Result.m_Match.m_DummyName.c_str());
		if(!Entry.m_vDdStatsGameTypes.empty())
		{
			Writer.WriteAttribute("ddstats_gametypes");
			Writer.BeginArray();
			for(const SQmDdStatsGameType &GameType : Entry.m_vDdStatsGameTypes)
			{
				Writer.BeginObject();
				Writer.WriteAttribute("name");
				Writer.WriteStrValue(GameType.m_Name.c_str());
				WriteInt64(Writer, "play_time_seconds", GameType.m_PlayTimeSeconds);
				Writer.EndObject();
			}
			Writer.EndArray();
		}
		Writer.WriteAttribute("modes");
		Writer.BeginArray();
		for(int Index = 0; Index < 2; ++Index)
		{
			const SQmAxiomModeResult &ModeResult = Entry.m_Result.m_aModes[Index];
			if(!ModeResult.m_HasData)
				continue;
			const SQmAxiomModeScore &Score = ModeResult.m_Score;
			Writer.BeginObject();
			Writer.WriteAttribute("mode");
			Writer.WriteStrValue(QmAxiomModeName(ModeFromIndex(Index)));
			WriteInt64(Writer, "points", Score.m_Points);
			WriteOptionalInt64(Writer, "global_rank", Score.m_GlobalRank);
			WriteOptionalInt64(Writer, "team_rank", Score.m_TeamRank);
			WriteInt64(Writer, "total_play_time", Score.m_TotalPlayTime);
			if(Entry.m_aHasAxiomPlayTime[Index])
				WriteInt64(Writer, "axiom_play_time", Entry.m_aAxiomPlayTime[Index]);
			WriteInt64(Writer, "total_maps_completed", Score.m_TotalMapsCompleted);
			WriteInt64(Writer, "performance_points", Score.m_PerformancePoints);
			WriteInt64(Writer, "mileage", Score.m_Mileage);
			Writer.WriteAttribute("difficulties");
			Writer.BeginArray();
			for(const SQmAxiomDifficultyStats &Difficulty : Score.m_vDifficulties)
			{
				Writer.BeginObject();
				Writer.WriteAttribute("name");
				Writer.WriteStrValue(Difficulty.m_Name.c_str());
				WriteInt64(Writer, "points", Difficulty.m_Points);
				WriteOptionalInt64(Writer, "global_rank", Difficulty.m_GlobalRank);
				WriteOptionalInt64(Writer, "team_rank", Difficulty.m_TeamRank);
				WriteInt64(Writer, "completed_maps", Difficulty.m_CompletedMaps);
				WriteInt64(Writer, "remaining_maps", Difficulty.m_RemainingMaps);
				WriteOptionalInt64(Writer, "total_points", Difficulty.m_TotalPoints);
				WriteOptionalInt64(Writer, "total_maps", Difficulty.m_TotalMaps);
				Writer.EndObject();
			}
			Writer.EndArray();
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
}

const SQmAxiomModeResult &SQmAxiomPlayerResult::Mode(EQmAxiomMode Mode) const
{
	return m_aModes[Mode == EQmAxiomMode::AXRACE ? 1 : 0];
}

bool CQmAxiomScores::IsFailureStatus(EQmAxiomScoreStatus Status)
{
	return Status == EQmAxiomScoreStatus::NOT_FOUND ||
	       Status == EQmAxiomScoreStatus::AMBIGUOUS ||
	       Status == EQmAxiomScoreStatus::HTTP_ERROR ||
	       Status == EQmAxiomScoreStatus::API_ERROR ||
	       Status == EQmAxiomScoreStatus::INVALID_RESPONSE;
}

bool CQmAxiomScores::IsWithinWindow(int64_t Timestamp, int64_t Now, int64_t WindowMs)
{
	if(Timestamp <= 0 || Now < Timestamp)
		return false;
	return Now - Timestamp < WindowMs * time_freq() / 1000;
}

int64_t CQmAxiomScores::FailureRetryWindowMs(int ConsecutiveFailures)
{
	if(ConsecutiveFailures <= 0)
		return AXIOM_FAILURE_RETRY_MS;
	// 指数退避：30s、60s、120s……封顶 AXIOM_FAILURE_RETRY_MAX_MS。
	// 服务端长时间不可用时避免固定 30 秒重打，恢复正常后首个成功即复位。
	int64_t Window = AXIOM_FAILURE_RETRY_MS;
	for(int Failure = 1; Failure < ConsecutiveFailures; ++Failure)
	{
		if(Window >= AXIOM_FAILURE_RETRY_MAX_MS || Window > AXIOM_FAILURE_RETRY_MAX_MS / 2)
			return AXIOM_FAILURE_RETRY_MAX_MS;
		Window *= 2;
	}
	return Window < AXIOM_FAILURE_RETRY_MAX_MS ? Window : AXIOM_FAILURE_RETRY_MAX_MS;
}

void CQmAxiomScores::RecordFailure(int &ConsecutiveFailures)
{
	if(ConsecutiveFailures < std::numeric_limits<int>::max())
		++ConsecutiveFailures;
}

void CQmAxiomScores::ClearFailure(int &ConsecutiveFailures)
{
	ConsecutiveFailures = 0;
}

int64_t CQmAxiomScores::CurrentTick() const noexcept
{
	return time_get();
}

std::shared_ptr<IHttpRequest> CQmAxiomScores::CreateNativeRequest(const char *pUrl)
{
	if(Http() == nullptr)
		return nullptr;
	return HttpGet(pUrl);
}

void CQmAxiomScores::SubmitNativeRequest(const std::shared_ptr<IHttpRequest> &pRequest)
{
	Http()->Run(pRequest);
}

std::shared_ptr<IQmAxiomHttpRequest> CQmAxiomScores::StartRequest(const char *pUrl, int TimeoutMs) noexcept
{
	std::shared_ptr<IHttpRequest> pRequest;
	try
	{
		if(m_pHttpOverride != nullptr)
			return m_pHttpOverride->Get(pUrl, AXIOM_CONNECT_TIMEOUT_MS, TimeoutMs, AXIOM_MAX_RESPONSE_BYTES);
		pRequest = CreateNativeRequest(pUrl);
		if(!pRequest)
			return nullptr;
		PrepareRequest(pRequest.get(), TimeoutMs);
		// 提交前准备完整owner；提交后不再为返回包装分配内存。
		auto pWrapped = std::make_shared<CNativeAxiomHttpRequest>(pRequest);
		SubmitNativeRequest(pRequest);
		return pWrapped;
	}
	catch(...)
	{
		TryAbortRequest(pRequest);
		return nullptr;
	}
}

void CQmAxiomScores::FailSearchRequest(SCacheEntry &Entry, const char *pReason) noexcept
{
	if(Entry.m_Result.m_SearchStatus != EQmAxiomScoreStatus::READY)
		Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::HTTP_ERROR;
	SetErrorDetail(Entry.m_Result.m_SearchErrorDetail, pReason);
	RecordFailure(Entry.m_SearchFailureCount);
	Entry.m_LastSearchFailureTick = CurrentTick();
}

void CQmAxiomScores::FailModeRequest(SCacheEntry &Entry, EQmAxiomMode Mode, const char *pReason) noexcept
{
	const int Index = ModeIndex(Mode);
	auto &Result = Entry.m_Result.m_aModes[Index];
	if(!Result.m_HasData)
		Result.m_Status = EQmAxiomScoreStatus::HTTP_ERROR;
	SetErrorDetail(Result.m_ErrorDetail, pReason);
	RecordFailure(Entry.m_aModeFailureCount[Index]);
	Entry.m_aLastModeFailureTick[Index] = CurrentTick();
}

void CQmAxiomScores::FailDdStatsRequest(SCacheEntry &Entry) noexcept
{
	RecordFailure(Entry.m_DdStatsFailureCount);
	Entry.m_LastDdStatsFailureTick = CurrentTick();
}

void CQmAxiomScores::StartSearchRequest(const char *pPlayerName, SCacheEntry &Entry) noexcept
try
{
	SRequestSlot Candidate;
	Candidate.m_PlayerName = pPlayerName;
	Candidate.m_Generation = m_Generation;
	Candidate.m_Mode = m_Mode;
	const std::string Url = QmBuildAxiomSearchUrl(pPlayerName);
	if(m_SearchRequest.m_pRequest)
	{
		TryAbortRequest(m_SearchRequest.m_pRequest);
		m_SearchRequest.m_pRequest.reset();
		m_SearchRequest.m_PlayerName.clear();
	}
	if(Url.empty())
	{
		FailSearchRequest(Entry, "invalid query name");
		if(Entry.m_Result.m_SearchStatus != EQmAxiomScoreStatus::READY)
			Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::INVALID_RESPONSE;
		return;
	}

	Candidate.m_pRequest = StartRequest(Url.c_str(), AXIOM_SEARCH_TIMEOUT_MS);
	if(!Candidate.m_pRequest)
	{
		FailSearchRequest(Entry, "request could not start");
		return;
	}
	m_SearchRequest = std::move(Candidate);
	// 有持久化匹配时保持 READY，只有首次查询才显示加载态。
	if(Entry.m_Result.m_SearchStatus != EQmAxiomScoreStatus::READY || Entry.m_Result.m_Match.m_UserId <= 0)
		Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::FETCHING;
}
catch(...)
{
	FailSearchRequest(Entry, "request could not start");
}

void CQmAxiomScores::StartModeRequest(const char *pPlayerName, SCacheEntry &Entry, EQmAxiomMode Mode) noexcept
try
{
	const int Index = ModeIndex(Mode);
	SRequestSlot &Slot = m_aModeRequests[Index];
	if(Slot.m_pRequest && Slot.m_PlayerName == pPlayerName)
		return;

	SQmAxiomModeResult &ModeResult = Entry.m_Result.m_aModes[Index];
	if(Entry.m_Result.m_Match.m_UserId <= 0)
	{
		FailModeRequest(Entry, Mode, "missing user id");
		return;
	}

	SRequestSlot Candidate;
	Candidate.m_PlayerName = pPlayerName;
	Candidate.m_Generation = m_Generation;
	Candidate.m_Mode = Mode;
	const std::string Url = QmBuildAxiomInfoUrl(Entry.m_Result.m_Match.m_UserId, Mode);
	if(Slot.m_pRequest)
	{
		TryAbortRequest(Slot.m_pRequest);
		Slot.m_pRequest.reset();
		Slot.m_PlayerName.clear();
	}
	Candidate.m_pRequest = StartRequest(Url.c_str(), AXIOM_INFO_TIMEOUT_MS);
	if(!Candidate.m_pRequest)
	{
		FailModeRequest(Entry, Mode, "request could not start");
		return;
	}
	Slot = std::move(Candidate);
	if(!ModeResult.m_HasData || ModeResult.m_Status != EQmAxiomScoreStatus::READY)
		ModeResult.m_Status = EQmAxiomScoreStatus::FETCHING;
}
catch(...)
{
	FailModeRequest(Entry, Mode, "request could not start");
}

void CQmAxiomScores::AbortActiveRequests(bool ResetFetchingStates) noexcept
{
	SCacheEntry *pEntry = nullptr;
	const auto CacheIt = m_Cache.find(m_ActivePlayerName);
	if(CacheIt != m_Cache.end())
		pEntry = &CacheIt->second;

	if(m_SearchRequest.m_pRequest)
	{
		TryAbortRequest(m_SearchRequest.m_pRequest);
		m_SearchRequest.m_pRequest.reset();
		if(ResetFetchingStates && pEntry && pEntry->m_Result.m_SearchStatus == EQmAxiomScoreStatus::FETCHING)
			pEntry->m_Result.m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
	}
	for(int Index = 0; Index < (int)m_aModeRequests.size(); ++Index)
	{
		SRequestSlot &Slot = m_aModeRequests[Index];
		if(!Slot.m_pRequest)
			continue;
		TryAbortRequest(Slot.m_pRequest);
		Slot.m_pRequest.reset();
		if(ResetFetchingStates && pEntry && pEntry->m_Result.m_aModes[Index].m_Status == EQmAxiomScoreStatus::FETCHING)
			pEntry->m_Result.m_aModes[Index].m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
	}
	if(m_DdStatsRequest.m_pRequest)
	{
		TryAbortRequest(m_DdStatsRequest.m_pRequest);
		m_DdStatsRequest.m_pRequest.reset();
	}
	m_SearchRequest.m_PlayerName.clear();
	for(SRequestSlot &Slot : m_aModeRequests)
		Slot.m_PlayerName.clear();
	m_DdStatsRequest.m_PlayerName.clear();
	m_pDdStatsParseJob.reset();
	m_ActivePlayerName.clear();
	++m_Generation;
}

void CQmAxiomScores::SetMode(EQmAxiomMode Mode)
{
	if(m_Mode == Mode)
		return;
	AbortScoreboardRequests();
	m_Mode = Mode;
}

void CQmAxiomScores::AbortScoreboardRequests() noexcept
{
	for(auto &[Name, Slot] : m_SearchRequests)
	{
		TryAbortRequest(Slot.m_pRequest);
		auto It = m_Cache.find(Name);
		if(It != m_Cache.end() && It->second.m_Result.m_SearchStatus == EQmAxiomScoreStatus::FETCHING)
			It->second.m_Result.m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
	}
	m_SearchRequests.clear();
	for(auto &[Name, Slot] : m_ModeRequests)
	{
		TryAbortRequest(Slot.m_pRequest);
		auto It = m_Cache.find(Name);
		if(It != m_Cache.end())
		{
			SQmAxiomModeResult &Result = It->second.m_Result.m_aModes[ModeIndex(Slot.m_Mode)];
			if(Result.m_Status == EQmAxiomScoreStatus::FETCHING)
				Result.m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
		}
	}
	m_ModeRequests.clear();
}

const char *CQmAxiomScores::StatisticsPlayerName() const
{
#if defined(CONF_HEADLESS_CLIENT)
	return g_Config.m_PlayerName;
#else
	const char *pName = GameClient() ? GameClient()->m_QmClient.QmDdnetPlayerName() : nullptr;
	return GameClient() && (!pName || pName[0] == '\0') ? g_Config.m_PlayerName : pName;
#endif
}

CQmAxiomScores::TCache::iterator CQmAxiomScores::FindOrCreateCacheEntry(const char *pPlayerName)
{
	const auto Existing = m_Cache.find(pPlayerName);
	if(Existing != m_Cache.end())
		return Existing;
	auto Oldest = m_Cache.end();
	if(m_Cache.size() >= AXIOM_MAX_CACHE_ENTRIES)
	{
		const char *pSelected = StatisticsPlayerName();
		for(auto It = m_Cache.begin(); It != m_Cache.end(); ++It)
		{
			if((pSelected && It->first == pSelected) || It->first == m_ActivePlayerName || m_SearchRequests.contains(It->first) || m_ModeRequests.contains(It->first))
				continue;
			if(Oldest == m_Cache.end() || It->second.m_LastAccessOrder < Oldest->second.m_LastAccessOrder)
				Oldest = It;
		}
		if(Oldest == m_Cache.end())
			return m_Cache.end();
	}
	// 先取得新节点；分配失败不能先丢掉旧记录。map 插入不使其余节点失效。
	const auto Added = m_Cache.try_emplace(pPlayerName).first;
	if(Oldest != m_Cache.end())
		m_Cache.erase(Oldest);
	return Added;
}

void CQmAxiomScores::EnsureScoreboardQueried(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0' || (size_t)str_length(pPlayerName) > AXIOM_MAX_QUERY_NAME_BYTES || !str_utf8_check(pPlayerName))
		return;
	if(m_Mode == EQmAxiomMode::NONE)
		return;
	const int64_t Now = CurrentTick();
	const auto CacheIt = FindOrCreateCacheEntry(pPlayerName);
	if(CacheIt == m_Cache.end())
		return;
	const std::string &Name = CacheIt->first;
	SCacheEntry &Entry = CacheIt->second;
	Entry.m_LastAccessTick = Now;
	Entry.m_LastAccessOrder = ++m_AccessOrderClock;
	if(m_ActivePlayerName == Name)
		return;

	if(Entry.m_Result.m_SearchStatus != EQmAxiomScoreStatus::READY ||
		!IsWithinWindow(Entry.m_LastSearchSuccessTick, Now, AXIOM_MATCH_CACHE_TTL_MS))
	{
		if(m_SearchRequests.contains(Name) || IsWithinWindow(Entry.m_LastSearchFailureTick, Now, FailureRetryWindowMs(Entry.m_SearchFailureCount)) ||
			m_SearchRequests.size() >= AXIOM_MAX_CONCURRENT_REQUESTS || m_SearchStartsThisFrame >= AXIOM_SEARCH_STARTS_PER_FRAME)
			return;
		const bool HasVisibleMatch = Entry.m_Result.m_SearchStatus == EQmAxiomScoreStatus::READY;
		auto [RequestIt, Inserted] = m_SearchRequests.try_emplace(Name);
		if(!Inserted)
			return;
		try
		{
			auto &Slot = RequestIt->second;
			Slot.m_PlayerName = Name;
			Slot.m_Generation = m_Generation;
			Slot.m_Mode = m_Mode;
			const std::string Url = QmBuildAxiomSearchUrl(Name.c_str());
			if(!Url.empty())
				Slot.m_pRequest = StartRequest(Url.c_str(), AXIOM_SEARCH_TIMEOUT_MS);
			if(Slot.m_pRequest)
			{
				if(!HasVisibleMatch)
					Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::FETCHING;
				++m_SearchStartsThisFrame;
				return;
			}
		}
		catch(...)
		{
			TryAbortRequest(RequestIt->second.m_pRequest);
		}
		m_SearchRequests.erase(RequestIt);
		FailSearchRequest(Entry, "request could not start");
		return;
	}

	SQmAxiomModeResult &Result = Entry.m_Result.m_aModes[ModeIndex(m_Mode)];
	if((Result.m_Status == EQmAxiomScoreStatus::READY && IsWithinWindow(Entry.m_aLastModeSuccessTick[ModeIndex(m_Mode)], Now, AXIOM_POINTS_CACHE_TTL_MS)) ||
		m_ModeRequests.contains(Name) || IsWithinWindow(Entry.m_aLastModeFailureTick[ModeIndex(m_Mode)], Now, FailureRetryWindowMs(Entry.m_aModeFailureCount[ModeIndex(m_Mode)])) ||
		m_ModeRequests.size() >= AXIOM_MAX_CONCURRENT_REQUESTS)
		return;
	auto [RequestIt, Inserted] = m_ModeRequests.try_emplace(Name);
	if(!Inserted)
		return;
	try
	{
		auto &Slot = RequestIt->second;
		Slot.m_PlayerName = Name;
		Slot.m_Generation = m_Generation;
		Slot.m_Mode = m_Mode;
		const std::string Url = QmBuildAxiomInfoUrl(Entry.m_Result.m_Match.m_UserId, m_Mode);
		Slot.m_pRequest = StartRequest(Url.c_str(), AXIOM_INFO_TIMEOUT_MS);
		if(Slot.m_pRequest)
		{
			if(!Result.m_HasData)
				Result.m_Status = EQmAxiomScoreStatus::FETCHING;
			return;
		}
	}
	catch(...)
	{
		TryAbortRequest(RequestIt->second.m_pRequest);
	}
	m_ModeRequests.erase(RequestIt);
	FailModeRequest(Entry, m_Mode, "request could not start");
}

void CQmAxiomScores::ProcessScoreboardRequests()
{
	for(auto It = m_SearchRequests.begin(); It != m_SearchRequests.end();)
	{
		if(!It->second.m_pRequest->Done())
		{
			++It;
			continue;
		}
		auto Node = m_SearchRequests.extract(It++);
		const std::string &Name = Node.key();
		const uint64_t ResponseGeneration = Node.mapped().m_Generation;
		const EQmAxiomMode ResponseMode = Node.mapped().m_Mode;
		auto pRequest = std::move(Node.mapped().m_pRequest);
		if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_Mode, ResponseMode))
			continue;
		auto CacheIt = m_Cache.find(Name);
		if(CacheIt == m_Cache.end())
			continue;
		SCacheEntry &Entry = CacheIt->second;
		const int64_t Now = CurrentTick();
		const bool HasVisibleMatch = Entry.m_Result.m_SearchStatus == EQmAxiomScoreStatus::READY && Entry.m_Result.m_Match.m_UserId > 0;
		if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
		{
			if(!HasVisibleMatch)
				Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::HTTP_ERROR;
			SetErrorDetail(Entry.m_Result.m_SearchErrorDetail, pRequest->ErrorDetail());
			RecordFailure(Entry.m_SearchFailureCount);
			Entry.m_LastSearchFailureTick = Now;
			continue;
		}
		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		pRequest->Result(&pData, &DataSize);
		SQmAxiomSearchMatch Match;
		const EQmAxiomParseResult Parsed = QmParseAxiomSearchResponse(reinterpret_cast<const char *>(pData), DataSize, Name.c_str(), Match);
		if(Parsed != EQmAxiomParseResult::SUCCESS)
		{
			if(!HasVisibleMatch)
				Entry.m_Result.m_SearchStatus = ParseStatus(Parsed);
			SetErrorDetail(Entry.m_Result.m_SearchErrorDetail, QmAxiomParseResultLabel(Parsed));
			RecordFailure(Entry.m_SearchFailureCount);
			Entry.m_LastSearchFailureTick = Now;
			continue;
		}
		if(Entry.m_Result.m_Match.m_UserId != Match.m_UserId)
		{
			auto ModeIt = m_ModeRequests.find(Name);
			if(ModeIt != m_ModeRequests.end())
			{
				TryAbortRequest(ModeIt->second.m_pRequest);
				m_ModeRequests.erase(ModeIt);
			}
			Entry.m_Result.m_aModes = {};
			Entry.m_aLastModeSuccessTick.fill(0);
			Entry.m_aLastModeFailureTick.fill(0);
			Entry.m_aModeFailureCount.fill(0);
			Entry.m_vDdStatsGameTypes.clear();
			Entry.m_aHasDdStatsPlayTime.fill(false);
			Entry.m_aHasAxiomPlayTime.fill(false);
			Entry.m_LastDdStatsSuccessTick = 0;
			m_PersistentCacheDirty = true;
		}
		Entry.m_Result.m_Match = std::move(Match);
		Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::READY;
		Entry.m_Result.m_SearchErrorDetail.clear();
		Entry.m_LastSearchSuccessTick = Now;
		ClearFailure(Entry.m_SearchFailureCount);
		Entry.m_LastSearchFailureTick = 0;
	}

	for(auto It = m_ModeRequests.begin(); It != m_ModeRequests.end();)
	{
		if(!It->second.m_pRequest->Done())
		{
			++It;
			continue;
		}
		auto Node = m_ModeRequests.extract(It++);
		const std::string &Name = Node.key();
		const EQmAxiomMode Mode = Node.mapped().m_Mode;
		const uint64_t ResponseGeneration = Node.mapped().m_Generation;
		auto pRequest = std::move(Node.mapped().m_pRequest);
		if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_Mode, Mode))
			continue;
		auto CacheIt = m_Cache.find(Name);
		if(CacheIt == m_Cache.end())
			continue;
		SCacheEntry &Entry = CacheIt->second;
		const int Index = ModeIndex(Mode);
		SQmAxiomModeResult &Result = Entry.m_Result.m_aModes[Index];
		const bool HasVisibleData = Result.m_HasData && Result.m_Status == EQmAxiomScoreStatus::READY;
		const int64_t Now = CurrentTick();
		if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
		{
			if(!HasVisibleData)
				Result.m_Status = EQmAxiomScoreStatus::HTTP_ERROR;
			SetErrorDetail(Result.m_ErrorDetail, pRequest->ErrorDetail());
			RecordFailure(Entry.m_aModeFailureCount[Index]);
			Entry.m_aLastModeFailureTick[Index] = Now;
			continue;
		}
		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		pRequest->Result(&pData, &DataSize);
		SQmAxiomModeScore Score;
		const EQmAxiomParseResult Parsed = QmParseAxiomInfoResponse(reinterpret_cast<const char *>(pData), DataSize, Score);
		if(Parsed != EQmAxiomParseResult::SUCCESS || Score.m_PlayerName != Entry.m_Result.m_Match.m_PlayerName)
		{
			if(!HasVisibleData)
				Result.m_Status = Parsed == EQmAxiomParseResult::SUCCESS ? EQmAxiomScoreStatus::INVALID_RESPONSE : ParseStatus(Parsed);
			SetErrorDetail(Result.m_ErrorDetail, Parsed == EQmAxiomParseResult::SUCCESS ? "player name mismatch" : QmAxiomParseResultLabel(Parsed));
			RecordFailure(Entry.m_aModeFailureCount[Index]);
			Entry.m_aLastModeFailureTick[Index] = Now;
			continue;
		}
		Result.m_Score = std::move(Score);
		Entry.m_aAxiomPlayTime[Index] = Result.m_Score.m_TotalPlayTime;
		Entry.m_aHasAxiomPlayTime[Index] = true;
		if(Entry.m_aHasDdStatsPlayTime[Index])
			Result.m_Score.m_TotalPlayTime = Entry.m_aDdStatsPlayTime[Index];
		Result.m_Status = EQmAxiomScoreStatus::READY;
		Result.m_HasData = true;
		Result.m_ErrorDetail.clear();
		Entry.m_aLastModeSuccessTick[Index] = Now;
		ClearFailure(Entry.m_aModeFailureCount[Index]);
		Entry.m_aLastModeFailureTick[Index] = 0;
		m_LastSuccessfulSyncTimestamp = time_timestamp();
		m_PersistentCacheDirty = true;
	}
}

void CQmAxiomScores::StartDdStatsRequest(const char *pPlayerName, SCacheEntry &Entry) noexcept
try
{
	if(!m_DdStatsEnabled || m_DdStatsRequest.m_pRequest)
		return;
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;

	SRequestSlot Candidate;
	Candidate.m_PlayerName = pPlayerName;
	Candidate.m_Generation = m_Generation;
	const std::string Url = QmBuildDdStatsPlayerUrl(pPlayerName);
	if(Url.empty())
	{
		FailDdStatsRequest(Entry);
		return;
	}

	Candidate.m_pRequest = StartRequest(Url.c_str(), DDSTATS_TIMEOUT_MS);
	if(!Candidate.m_pRequest)
	{
		FailDdStatsRequest(Entry);
		return;
	}
	m_DdStatsRequest = std::move(Candidate);
}
catch(...)
{
	FailDdStatsRequest(Entry);
}

void CQmAxiomScores::ProcessSearchRequest()
{
	if(!m_SearchRequest.m_pRequest || !m_SearchRequest.m_pRequest->Done())
		return;

	std::shared_ptr<IQmAxiomHttpRequest> pRequest = std::move(m_SearchRequest.m_pRequest);
	const uint64_t ResponseGeneration = m_SearchRequest.m_Generation;
	const std::string ResponsePlayerName = std::move(m_SearchRequest.m_PlayerName);
	if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_ActivePlayerName, ResponsePlayerName))
		return;

	const auto CacheIt = m_Cache.find(ResponsePlayerName);
	if(CacheIt == m_Cache.end())
		return;
	SCacheEntry &Entry = CacheIt->second;
	const int64_t Now = CurrentTick();

	if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
	{
		const bool HadVisibleSearch = Entry.m_Result.m_SearchStatus == EQmAxiomScoreStatus::READY && Entry.m_Result.m_Match.m_UserId > 0;
		if(!HadVisibleSearch)
			Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::HTTP_ERROR;
		SetErrorDetail(Entry.m_Result.m_SearchErrorDetail, pRequest->ErrorDetail());
		RecordFailure(Entry.m_SearchFailureCount);
		Entry.m_LastSearchFailureTick = Now;
		return;
	}

	const unsigned char *pData = nullptr;
	size_t DataSize = 0;
	pRequest->Result(&pData, &DataSize);
	SQmAxiomSearchMatch Match;
	const EQmAxiomParseResult ParseResult = QmParseAxiomSearchResponse(reinterpret_cast<const char *>(pData), DataSize, ResponsePlayerName.c_str(), Match);
	const bool HadVisibleSearch = Entry.m_Result.m_SearchStatus == EQmAxiomScoreStatus::READY && Entry.m_Result.m_Match.m_UserId > 0;
	if(!HadVisibleSearch)
		Entry.m_Result.m_SearchStatus = ParseStatus(ParseResult);
	if(ParseResult != EQmAxiomParseResult::SUCCESS)
	{
		SetErrorDetail(Entry.m_Result.m_SearchErrorDetail, QmAxiomParseResultLabel(ParseResult));
		RecordFailure(Entry.m_SearchFailureCount);
		Entry.m_LastSearchFailureTick = Now;
		return;
	}
	Entry.m_Result.m_SearchErrorDetail.clear();

	const bool MatchChanged = Entry.m_Result.m_Match.m_UserId != Match.m_UserId;
	Entry.m_Result.m_Match = std::move(Match);
	Entry.m_Result.m_SearchStatus = EQmAxiomScoreStatus::READY;
	if(MatchChanged)
	{
		for(SRequestSlot &Slot : m_aModeRequests)
		{
			if(Slot.m_pRequest)
				TryAbortRequest(Slot.m_pRequest);
			Slot.m_pRequest.reset();
			Slot.m_PlayerName.clear();
		}
		m_PersistentCacheDirty = true;
		for(SQmAxiomModeResult &ModeResult : Entry.m_Result.m_aModes)
			ModeResult = {};
		Entry.m_aDdStatsPlayTime.fill(0);
		Entry.m_aHasDdStatsPlayTime.fill(false);
		Entry.m_aAxiomPlayTime.fill(0);
		Entry.m_aHasAxiomPlayTime.fill(false);
		Entry.m_vDdStatsGameTypes.clear();
		Entry.m_LastDdStatsSuccessTick = 0;
		ClearFailure(Entry.m_DdStatsFailureCount);
		Entry.m_LastDdStatsFailureTick = 0;
	}
	Entry.m_LastSearchSuccessTick = Now;
	ClearFailure(Entry.m_SearchFailureCount);
	Entry.m_LastSearchFailureTick = 0;
	StartModeRequest(ResponsePlayerName.c_str(), Entry, EQmAxiomMode::GORES);
	StartModeRequest(ResponsePlayerName.c_str(), Entry, EQmAxiomMode::AXRACE);
	StartDdStatsRequest(ResponsePlayerName.c_str(), Entry);
}

void CQmAxiomScores::BeginActiveQuery(const char *pPlayerName)
{
	if(m_ActivePlayerName == pPlayerName)
		return;
	AbortActiveRequests(true);
	auto SearchIt = m_SearchRequests.find(pPlayerName);
	if(SearchIt != m_SearchRequests.end())
	{
		TryAbortRequest(SearchIt->second.m_pRequest);
		m_SearchRequests.erase(SearchIt);
		auto CacheIt = m_Cache.find(pPlayerName);
		if(CacheIt != m_Cache.end() && CacheIt->second.m_Result.m_SearchStatus == EQmAxiomScoreStatus::FETCHING)
			CacheIt->second.m_Result.m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
	}
	auto ModeIt = m_ModeRequests.find(pPlayerName);
	if(ModeIt != m_ModeRequests.end())
	{
		const int Index = ModeIndex(ModeIt->second.m_Mode);
		TryAbortRequest(ModeIt->second.m_pRequest);
		m_ModeRequests.erase(ModeIt);
		auto CacheIt = m_Cache.find(pPlayerName);
		if(CacheIt != m_Cache.end() && CacheIt->second.m_Result.m_aModes[Index].m_Status == EQmAxiomScoreStatus::FETCHING)
			CacheIt->second.m_Result.m_aModes[Index].m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
	}
	m_ActivePlayerName = pPlayerName;
}

void CQmAxiomScores::ProcessModeRequests()
{
	for(int Index = 0; Index < (int)m_aModeRequests.size(); ++Index)
	{
		SRequestSlot &Slot = m_aModeRequests[Index];
		if(!Slot.m_pRequest || !Slot.m_pRequest->Done())
			continue;

		std::shared_ptr<IQmAxiomHttpRequest> pRequest = std::move(Slot.m_pRequest);
		const uint64_t ResponseGeneration = Slot.m_Generation;
		const std::string ResponsePlayerName = std::move(Slot.m_PlayerName);
		if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_ActivePlayerName, ResponsePlayerName))
			continue;

		const auto CacheIt = m_Cache.find(ResponsePlayerName);
		if(CacheIt == m_Cache.end())
			continue;
		SCacheEntry &Entry = CacheIt->second;
		SQmAxiomModeResult &ModeResult = Entry.m_Result.m_aModes[Index];
		const int64_t Now = CurrentTick();

		if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
		{
			const bool HadVisibleMode = ModeResult.m_HasData && ModeResult.m_Status == EQmAxiomScoreStatus::READY;
			if(!HadVisibleMode)
				ModeResult.m_Status = EQmAxiomScoreStatus::HTTP_ERROR;
			SetErrorDetail(ModeResult.m_ErrorDetail, pRequest->ErrorDetail());
			RecordFailure(Entry.m_aModeFailureCount[Index]);
			Entry.m_aLastModeFailureTick[Index] = Now;
			continue;
		}

		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		pRequest->Result(&pData, &DataSize);
		SQmAxiomModeScore Score;
		const EQmAxiomParseResult ParseResult = QmParseAxiomInfoResponse(reinterpret_cast<const char *>(pData), DataSize, Score);
		const bool HadVisibleMode = ModeResult.m_HasData && ModeResult.m_Status == EQmAxiomScoreStatus::READY;
		if(!HadVisibleMode)
			ModeResult.m_Status = ParseStatus(ParseResult);
		if(ParseResult != EQmAxiomParseResult::SUCCESS || Score.m_PlayerName != Entry.m_Result.m_Match.m_PlayerName)
		{
			if(ParseResult == EQmAxiomParseResult::SUCCESS && !HadVisibleMode)
				ModeResult.m_Status = EQmAxiomScoreStatus::INVALID_RESPONSE;
			SetErrorDetail(ModeResult.m_ErrorDetail, ParseResult == EQmAxiomParseResult::SUCCESS ? "player name mismatch" : QmAxiomParseResultLabel(ParseResult));
			RecordFailure(Entry.m_aModeFailureCount[Index]);
			Entry.m_aLastModeFailureTick[Index] = Now;
			continue;
		}
		ModeResult.m_ErrorDetail.clear();

		ModeResult.m_Score = std::move(Score);
		ModeResult.m_Status = EQmAxiomScoreStatus::READY;
		Entry.m_aAxiomPlayTime[Index] = ModeResult.m_Score.m_TotalPlayTime;
		Entry.m_aHasAxiomPlayTime[Index] = true;
		if(Entry.m_aHasDdStatsPlayTime[Index])
			ModeResult.m_Score.m_TotalPlayTime = Entry.m_aDdStatsPlayTime[Index];
		ModeResult.m_HasData = true;
		Entry.m_aLastModeSuccessTick[Index] = Now;
		ClearFailure(Entry.m_aModeFailureCount[Index]);
		Entry.m_aLastModeFailureTick[Index] = 0;
		m_LastSuccessfulSyncTimestamp = time_timestamp();
		m_PersistentCacheDirty = true;
	}
}

int CQmAxiomScores::ModeIndex(EQmAxiomMode Mode)
{
	return Mode == EQmAxiomMode::AXRACE ? 1 : 0;
}

EQmAxiomMode CQmAxiomScores::ModeFromIndex(int Index)
{
	return Index == 1 ? EQmAxiomMode::AXRACE : EQmAxiomMode::GORES;
}

SQmAxiomLookupResult CQmAxiomScores::GetLookup(const char *pPlayerName) const
{
	// 门面实现:把 dyl 管线的搜索+模式结果合并成上游记分板需要的只读视图。
	SQmAxiomLookupResult Result;
	const SQmAxiomPlayerResult *pPlayer = GetResult(pPlayerName);
	if(pPlayer == nullptr || m_Mode == EQmAxiomMode::NONE)
	{
		Result.m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
		return Result;
	}
	if(pPlayer->m_SearchStatus != EQmAxiomScoreStatus::READY)
	{
		Result.m_Status = pPlayer->m_SearchStatus;
		return Result;
	}
	const SQmAxiomModeResult &ModeResult = pPlayer->Mode(m_Mode);
	Result.m_Status = ModeResult.m_Status;
	Result.m_Points = ModeResult.m_Score.m_Points;
	if(Result.m_Status == EQmAxiomScoreStatus::FETCHING)
		Result.m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
	return Result;
}

bool CQmAxiomScores::SubmitDdStatsParseJob(const std::shared_ptr<CQmAxiomDdStatsParseJob> &pJob)
{
	return Engine()->TryAddJob(pJob);
}

void CQmAxiomScores::ProcessDdStatsRequest()
{
	if(!m_DdStatsRequest.m_pRequest || !m_DdStatsRequest.m_pRequest->Done())
		return;

	if(m_DdStatsRequest.m_pRequest->TransportSucceeded() && m_DdStatsRequest.m_pRequest->StatusCode() == 200)
	{
		if(!m_pDdStatsParseJob)
		{
			try
			{
				auto pJob = std::make_shared<CQmAxiomDdStatsParseJob>(m_DdStatsRequest.m_pRequest, m_DdStatsRequest.m_PlayerName);
				if(SubmitDdStatsParseJob(pJob))
					m_pDdStatsParseJob = std::move(pJob);
			}
			catch(...)
			{
				// 分配或入队失败沿解析失败路径释放本次请求，保留已有数据。
			}
		}
		if(m_pDdStatsParseJob && !m_pDdStatsParseJob->Ready() && !m_pDdStatsParseJob->Done())
			return;
	}
	auto pParseJob = std::move(m_pDdStatsParseJob);
	std::shared_ptr<IQmAxiomHttpRequest> pRequest = std::move(m_DdStatsRequest.m_pRequest);
	const uint64_t ResponseGeneration = m_DdStatsRequest.m_Generation;
	const std::string ResponsePlayerName = std::move(m_DdStatsRequest.m_PlayerName);
	if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_ActivePlayerName, ResponsePlayerName))
		return;

	const auto CacheIt = m_Cache.find(ResponsePlayerName);
	if(CacheIt == m_Cache.end())
		return;
	SCacheEntry &Entry = CacheIt->second;
	const int64_t Now = CurrentTick();
	if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
	{
		RecordFailure(Entry.m_DdStatsFailureCount);
		Entry.m_LastDdStatsFailureTick = Now;
		return;
	}

	const EQmAxiomParseResult ParseResult = pParseJob && pParseJob->Ready() ? pParseJob->Result() : EQmAxiomParseResult::INVALID_RESPONSE;
	if(ParseResult != EQmAxiomParseResult::SUCCESS)
	{
		RecordFailure(Entry.m_DdStatsFailureCount);
		Entry.m_LastDdStatsFailureTick = Now;
		return;
	}

	Entry.m_vDdStatsGameTypes = pParseJob->TakeGameTypes();
	Entry.m_aDdStatsPlayTime.fill(0);
	Entry.m_aHasDdStatsPlayTime.fill(false);
	std::array<int64_t, 2> aPlayTimes{};
	std::array<bool, 2> aHasPlayTimes{};
	for(const SQmDdStatsGameType &GameType : Entry.m_vDdStatsGameTypes)
	{
		if(str_comp_nocase(GameType.m_Name.c_str(), "Gores") == 0)
		{
			aPlayTimes[0] = GameType.m_PlayTimeSeconds;
			aHasPlayTimes[0] = true;
		}
		else if(str_comp_nocase(GameType.m_Name.c_str(), "AXRace") == 0)
		{
			aPlayTimes[1] = GameType.m_PlayTimeSeconds;
			aHasPlayTimes[1] = true;
		}
	}
	for(int Index = 0; Index < 2; ++Index)
	{
		Entry.m_aDdStatsPlayTime[Index] = aPlayTimes[Index];
		Entry.m_aHasDdStatsPlayTime[Index] = aHasPlayTimes[Index];
		if(Entry.m_Result.m_aModes[Index].m_HasData && aHasPlayTimes[Index])
			Entry.m_Result.m_aModes[Index].m_Score.m_TotalPlayTime = aPlayTimes[Index];
		else if(Entry.m_Result.m_aModes[Index].m_HasData && Entry.m_aHasAxiomPlayTime[Index])
			Entry.m_Result.m_aModes[Index].m_Score.m_TotalPlayTime = Entry.m_aAxiomPlayTime[Index];
	}
	Entry.m_LastDdStatsSuccessTick = Now;
	ClearFailure(Entry.m_DdStatsFailureCount);
	Entry.m_LastDdStatsFailureTick = 0;
	m_LastSuccessfulSyncTimestamp = time_timestamp();
	m_PersistentCacheDirty = true;
}

void CQmAxiomScores::FinishActiveQueryIfIdle()
{
	if(m_SearchRequest.m_pRequest)
		return;
	for(const SRequestSlot &Slot : m_aModeRequests)
	{
		if(Slot.m_pRequest)
			return;
	}
	if(m_DdStatsRequest.m_pRequest)
		return;
	m_ActivePlayerName.clear();
}

void CQmAxiomScores::EnsureQueried(const char *pPlayerName) noexcept
try
{
	if(!pPlayerName || pPlayerName[0] == '\0' || static_cast<size_t>(str_length(pPlayerName)) > AXIOM_MAX_QUERY_NAME_BYTES || !str_utf8_check(pPlayerName))
		return;
	// 统计页与记分板调用同一接口；选中的统计玩家继续获取双模式及 DDStats。
	const char *pStatisticsPlayer = StatisticsPlayerName();
	if(m_Mode != EQmAxiomMode::NONE && (!pStatisticsPlayer || pStatisticsPlayer[0] == '\0' || str_comp(pPlayerName, pStatisticsPlayer) != 0))
	{
		EnsureScoreboardQueried(pPlayerName);
		return;
	}
	// 统计页的双模式和 DDStats 查询仍以单活动玩家运行。
	if(m_SearchRequest.m_pRequest || m_aModeRequests[0].m_pRequest || m_aModeRequests[1].m_pRequest || m_DdStatsRequest.m_pRequest)
		return;

	const int64_t Now = CurrentTick();
	const auto CacheIt = FindOrCreateCacheEntry(pPlayerName);
	if(CacheIt == m_Cache.end())
		return;
	// 新节点先拥有名字再淘汰旧节点；后续只借用本次命中的稳定键，不逐帧复制。
	const std::string &Name = CacheIt->first;
	SCacheEntry &Entry = CacheIt->second;
	Entry.m_LastAccessTick = Now;
	Entry.m_LastAccessOrder = ++m_AccessOrderClock;
	const bool SearchFresh = Entry.m_Result.m_SearchStatus == EQmAxiomScoreStatus::READY && IsWithinWindow(Entry.m_LastSearchSuccessTick, Now, AXIOM_SEARCH_CACHE_TTL_MS);
	if(!SearchFresh)
	{
		// 持久化缓存刷新期间仍保持 READY，以便 UI 继续显示旧数据；请求本身
		// 仍然是唯一的 in-flight 标记，不能因为 READY 状态而每帧取消重发。
		if(m_SearchRequest.m_pRequest && m_SearchRequest.m_PlayerName == Name)
			return;
		if(IsWithinWindow(Entry.m_LastSearchFailureTick, Now, FailureRetryWindowMs(Entry.m_SearchFailureCount)))
			return;

		BeginActiveQuery(Name.c_str());
		StartSearchRequest(Name.c_str(), Entry);
		StartDdStatsRequest(Name.c_str(), Entry);
		FinishActiveQueryIfIdle();
		return;
	}

	std::array<bool, 2> aNeedsRequest{};
	bool AnyRequestNeeded = false;
	for(int Index = 0; Index < (int)Entry.m_Result.m_aModes.size(); ++Index)
	{
		const EQmAxiomScoreStatus Status = Entry.m_Result.m_aModes[Index].m_Status;
		if(Status == EQmAxiomScoreStatus::READY && IsWithinWindow(Entry.m_aLastModeSuccessTick[Index], Now, AXIOM_SCORE_CACHE_TTL_MS))
			continue;
		if(m_aModeRequests[Index].m_pRequest && m_aModeRequests[Index].m_PlayerName == Name)
			continue;
		if(IsWithinWindow(Entry.m_aLastModeFailureTick[Index], Now, FailureRetryWindowMs(Entry.m_aModeFailureCount[Index])))
			continue;
		aNeedsRequest[Index] = true;
		AnyRequestNeeded = true;
	}
	const bool DdStatsFresh = !m_DdStatsEnabled || IsWithinWindow(Entry.m_LastDdStatsSuccessTick, Now, AXIOM_SCORE_CACHE_TTL_MS);
	const bool DdStatsFetching = m_DdStatsRequest.m_pRequest && m_ActivePlayerName == Name;
	const bool DdStatsRetryBlocked = IsWithinWindow(Entry.m_LastDdStatsFailureTick, Now, FailureRetryWindowMs(Entry.m_DdStatsFailureCount));
	const bool NeedDdStats = m_DdStatsEnabled && !DdStatsFresh && !DdStatsFetching && !DdStatsRetryBlocked;
	AnyRequestNeeded |= NeedDdStats;
	if(!AnyRequestNeeded)
		return;

	BeginActiveQuery(Name.c_str());
	for(int Index = 0; Index < (int)aNeedsRequest.size(); ++Index)
	{
		if(aNeedsRequest[Index])
			StartModeRequest(Name.c_str(), Entry, ModeFromIndex(Index));
	}
	if(NeedDdStats)
		StartDdStatsRequest(Name.c_str(), Entry);
	FinishActiveQueryIfIdle();
}
catch(...)
{
	// 查表/新键或缓存分配失败尚未启动传输，后续调用可以重新准备。
	FinishActiveQueryIfIdle();
}

void CQmAxiomScores::Refresh(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;
	// 强制刷新必须先取消当前玩家的搜索和模式请求，避免新搜索完成后覆盖仍在运行的旧请求槽。
	AbortActiveRequests(true);
	const auto It = m_Cache.find(pPlayerName);
	if(It != m_Cache.end())
	{
		It->second.m_LastSearchSuccessTick = 0;
		It->second.m_LastSearchFailureTick = 0;
		It->second.m_SearchFailureCount = 0;
		It->second.m_aLastModeSuccessTick.fill(0);
		It->second.m_aLastModeFailureTick.fill(0);
		It->second.m_aModeFailureCount.fill(0);
		It->second.m_LastDdStatsSuccessTick = 0;
		It->second.m_LastDdStatsFailureTick = 0;
		It->second.m_DdStatsFailureCount = 0;
	}
	EnsureQueried(pPlayerName);
}

const SQmAxiomPlayerResult *CQmAxiomScores::GetResult(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return nullptr;
	const auto It = m_Cache.find(pPlayerName);
	return It == m_Cache.end() ? nullptr : &It->second.m_Result;
}

const std::vector<SQmDdStatsGameType> *CQmAxiomScores::GetDdStatsGameTypes(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return nullptr;
	const auto It = m_Cache.find(pPlayerName);
	return It == m_Cache.end() || It->second.m_vDdStatsGameTypes.empty() ? nullptr : &It->second.m_vDdStatsGameTypes;
}

bool CQmAxiomScores::IsFetchingPlayer(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return false;
	const auto It = m_Cache.find(pPlayerName);
	if(It == m_Cache.end())
		return m_SearchRequest.m_pRequest && m_SearchRequest.m_PlayerName == pPlayerName;
	const SQmAxiomPlayerResult &Result = It->second.m_Result;
	if(Result.m_SearchStatus == EQmAxiomScoreStatus::FETCHING)
		return true;
	for(const SQmAxiomModeResult &ModeResult : Result.m_aModes)
		if(ModeResult.m_Status == EQmAxiomScoreStatus::FETCHING)
			return true;
	bool HasVisibleData = !It->second.m_vDdStatsGameTypes.empty();
	for(const SQmAxiomModeResult &ModeResult : Result.m_aModes)
		HasVisibleData |= ModeResult.m_HasData;
	if(HasVisibleData)
		return false;
	if(m_SearchRequest.m_pRequest && m_SearchRequest.m_PlayerName == pPlayerName)
		return true;
	for(const SRequestSlot &Slot : m_aModeRequests)
		if(Slot.m_pRequest && Slot.m_PlayerName == pPlayerName)
			return true;
	return m_DdStatsRequest.m_pRequest && m_DdStatsRequest.m_PlayerName == pPlayerName;
}

bool CQmAxiomScores::IsPlayerFailed(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return false;
	const auto It = m_Cache.find(pPlayerName);
	if(It == m_Cache.end())
		return false;
	const SQmAxiomPlayerResult &Result = It->second.m_Result;
	if(IsFailureStatus(Result.m_SearchStatus))
		return true;
	for(const SQmAxiomModeResult &ModeResult : Result.m_aModes)
	{
		if(IsFailureStatus(ModeResult.m_Status))
			return true;
	}
	return false;
}

void CQmAxiomScores::OnUpdate()
{
	m_SearchStartsThisFrame = 0;
	ProcessSearchRequest();
	ProcessModeRequests();
	ProcessDdStatsRequest();
	ProcessScoreboardRequests();
	FinishActiveQueryIfIdle();
}

void CQmAxiomScores::OnReset()
{
	AbortActiveRequests(true);
	AbortScoreboardRequests();
	// 重置是生命周期边界，下一次进入在线状态应允许立即重新查询。
	for(auto &[Name, Entry] : m_Cache)
	{
		ClearFailure(Entry.m_SearchFailureCount);
		Entry.m_LastSearchFailureTick = 0;
		Entry.m_aLastModeFailureTick.fill(0);
		Entry.m_aModeFailureCount.fill(0);
		ClearFailure(Entry.m_DdStatsFailureCount);
		Entry.m_LastDdStatsFailureTick = 0;
	}
}

void CQmAxiomScores::OnShutdown()
{
	AbortActiveRequests(false);
	AbortScoreboardRequests();
	m_Cache.clear();
}

void CQmAxiomScores::OnStateChange(int NewState, int OldState)
{
	if(NewState < IClient::STATE_ONLINE)
	{
		AbortActiveRequests(true);
		AbortScoreboardRequests();
		m_Mode = EQmAxiomMode::NONE;
	}
	else if(NewState == IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE)
	{
		// 每次入服都重新查询；取消旧入服代际，保留最近成功数据供刷新期间显示。
		AbortActiveRequests(true);
		AbortScoreboardRequests();
		m_SearchStartsThisFrame = 0;
		for(auto &[Name, Entry] : m_Cache)
		{
			Entry.m_LastSearchSuccessTick = 0;
			ClearFailure(Entry.m_SearchFailureCount);
			Entry.m_LastSearchFailureTick = 0;
			Entry.m_aLastModeSuccessTick.fill(0);
			Entry.m_aLastModeFailureTick.fill(0);
			Entry.m_aModeFailureCount.fill(0);
			ClearFailure(Entry.m_DdStatsFailureCount);
			Entry.m_LastDdStatsFailureTick = 0;
		}
	}
}
