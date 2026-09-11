#include "axiom_scores_data.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <limits>

namespace
{
	constexpr size_t MAX_AXIOM_JSON_BYTES = 8 * 1024 * 1024;
	constexpr unsigned MAX_AXIOM_SEARCH_RESULTS = 64;
	constexpr unsigned MAX_AXIOM_DIFFICULTIES = 128;
	constexpr size_t MAX_AXIOM_PLAYER_NAME_BYTES = 256;
	constexpr size_t MAX_AXIOM_DIFFICULTY_NAME_BYTES = 192;

	// DDNet 社区信息里 Axiom 的服务器分类名，正好对应两个积分模式。
	constexpr const char *AXIOM_COMMUNITY_TYPE_GORES = "Gores";
	constexpr const char *AXIOM_COMMUNITY_TYPE_OTHER = "Other";
	// 分类缺失时的兜底：AXRace 服务器的名字里带 AXRace，Gores 服务器不带。
	constexpr const char *AXIOM_SERVER_NAME_MARKER = "AXRace";

	const json_value *JsonField(const json_value *pObject, const char *pName)
	{
		if(!pObject || pObject->type != json_object)
			return &json_value_none;
		return json_object_get(pObject, pName);
	}

	bool ReadNonNegativeInteger(const json_value *pObject, const char *pName, int64_t &Out)
	{
		const json_value *pValue = JsonField(pObject, pName);
		if(pValue->type != json_integer || pValue->u.integer < 0)
			return false;
		Out = pValue->u.integer;
		return true;
	}

	bool ReadOptionalNonNegativeInteger(const json_value *pObject, const char *pName, std::optional<int64_t> &Out)
	{
		const json_value *pValue = JsonField(pObject, pName);
		// Axiom 对没有数据的难度会返回 null，缺失与 null 都表示「没有该统计」。
		if(pValue == &json_value_none || pValue->type == json_null)
		{
			Out.reset();
			return true;
		}
		if(pValue->type != json_integer || pValue->u.integer < 0)
			return false;
		Out = pValue->u.integer;
		return true;
	}

	bool ReadOptionalRank(const json_value *pObject, const char *pName, std::optional<int64_t> &Out)
	{
		const json_value *pValue = JsonField(pObject, pName);
		if(pValue == &json_value_none || pValue->type == json_null)
		{
			Out.reset();
			return true;
		}
		if(pValue->type != json_integer || pValue->u.integer <= 0)
			return false;
		Out = pValue->u.integer;
		return true;
	}

	bool ReadString(const json_value *pObject, const char *pName, std::string &Out, size_t MaxBytes, bool AllowEmpty = false)
	{
		const json_value *pValue = JsonField(pObject, pName);
		if(pValue->type != json_string)
			return false;
		const char *pText = json_string_get(pValue);
		if(!pText || (!AllowEmpty && pText[0] == '\0') || str_length(pText) > MaxBytes || !str_utf8_check(pText))
			return false;
		Out = pText;
		return true;
	}

	bool ReadOptionalString(const json_value *pObject, const char *pName, std::string &Out)
	{
		const json_value *pValue = JsonField(pObject, pName);
		if(pValue == &json_value_none || pValue->type == json_null)
		{
			Out.clear();
			return true;
		}
		return ReadString(pObject, pName, Out, MAX_AXIOM_PLAYER_NAME_BYTES, true);
	}

	EQmAxiomParseResult ParseSearchRoot(const json_value *pRoot, const char *pPlayerName, SQmAxiomSearchMatch &OutMatch)
	{
		if(!pRoot || pRoot->type != json_object || !pPlayerName || pPlayerName[0] == '\0' || !str_utf8_check(pPlayerName))
			return EQmAxiomParseResult::INVALID_RESPONSE;

		const json_value *pCode = JsonField(pRoot, "code");
		if(pCode->type != json_integer)
			return EQmAxiomParseResult::INVALID_RESPONSE;
		if(pCode->u.integer != 200)
			return EQmAxiomParseResult::API_ERROR;

		const json_value *pData = JsonField(pRoot, "data");
		const json_value *pResults = JsonField(pData, "results");
		if(pData->type != json_object || pResults->type != json_array || pResults->u.array.length > MAX_AXIOM_SEARCH_RESULTS)
			return EQmAxiomParseResult::INVALID_RESPONSE;

		bool Found = false;
		SQmAxiomSearchMatch Match;
		for(unsigned i = 0; i < pResults->u.array.length; ++i)
		{
			const json_value *pEntry = pResults->u.array.values[i];
			if(!pEntry || pEntry->type != json_object)
				continue;

			std::string PlayerName;
			std::string DummyName;
			if(!ReadString(pEntry, "player_name", PlayerName, MAX_AXIOM_PLAYER_NAME_BYTES) || !ReadOptionalString(pEntry, "dummy_name", DummyName))
				continue;
			if(PlayerName != pPlayerName && DummyName != pPlayerName)
				continue;

			const json_value *pUserId = JsonField(pEntry, "user_id");
			if(pUserId->type != json_integer || pUserId->u.integer <= 0)
				return EQmAxiomParseResult::INVALID_RESPONSE;

			if(Found && Match.m_UserId != pUserId->u.integer)
				return EQmAxiomParseResult::AMBIGUOUS;

			Found = true;
			Match.m_UserId = pUserId->u.integer;
			Match.m_PlayerName = std::move(PlayerName);
			Match.m_DummyName = std::move(DummyName);
		}

		if(!Found)
			return EQmAxiomParseResult::NOT_FOUND;

		OutMatch = std::move(Match);
		return EQmAxiomParseResult::SUCCESS;
	}

	EQmAxiomParseResult ParseInfoRoot(const json_value *pRoot, SQmAxiomModeScore &OutScore)
	{
		if(!pRoot || pRoot->type != json_object)
			return EQmAxiomParseResult::INVALID_RESPONSE;

		const json_value *pCode = JsonField(pRoot, "code");
		if(pCode->type != json_integer)
			return EQmAxiomParseResult::INVALID_RESPONSE;
		if(pCode->u.integer != 200)
			return EQmAxiomParseResult::API_ERROR;

		const json_value *pData = JsonField(pRoot, "data");
		const json_value *pPlayer = JsonField(pData, "player");
		const json_value *pDifficultyData = JsonField(pData, "difficultyData");
		if(pData->type != json_object || pPlayer->type != json_object || pDifficultyData->type != json_object || pDifficultyData->u.object.length > MAX_AXIOM_DIFFICULTIES)
			return EQmAxiomParseResult::INVALID_RESPONSE;

		SQmAxiomModeScore Score;
		if(!ReadString(pPlayer, "player_name", Score.m_PlayerName, MAX_AXIOM_PLAYER_NAME_BYTES) ||
			!ReadNonNegativeInteger(pPlayer, "points", Score.m_Points) ||
			!ReadOptionalRank(pPlayer, "global_rank", Score.m_GlobalRank) ||
			!ReadOptionalRank(pPlayer, "team_rank", Score.m_TeamRank) ||
			!ReadNonNegativeInteger(pPlayer, "total_play_time", Score.m_TotalPlayTime) ||
			!ReadNonNegativeInteger(pPlayer, "total_maps_completed", Score.m_TotalMapsCompleted) ||
			!ReadNonNegativeInteger(pPlayer, "performance_points", Score.m_PerformancePoints) ||
			!ReadNonNegativeInteger(pPlayer, "mileage", Score.m_Mileage))
		{
			return EQmAxiomParseResult::INVALID_RESPONSE;
		}

		Score.m_vDifficulties.reserve(pDifficultyData->u.object.length);
		for(unsigned i = 0; i < pDifficultyData->u.object.length; ++i)
		{
			const char *pName = pDifficultyData->u.object.values[i].name;
			const json_value *pDifficulty = pDifficultyData->u.object.values[i].value;
			if(!pName || pName[0] == '\0' || str_length(pName) > MAX_AXIOM_DIFFICULTY_NAME_BYTES || !str_utf8_check(pName) || !pDifficulty || pDifficulty->type != json_object)
				return EQmAxiomParseResult::INVALID_RESPONSE;

			const json_value *pStats = JsonField(pDifficulty, "stats");
			SQmAxiomDifficultyStats Difficulty;
			Difficulty.m_Name = pName;
			if(pStats->type != json_object ||
				!ReadNonNegativeInteger(pStats, "points", Difficulty.m_Points) ||
				!ReadOptionalRank(pStats, "global_rank", Difficulty.m_GlobalRank) ||
				!ReadOptionalRank(pStats, "team_rank", Difficulty.m_TeamRank) ||
				!ReadNonNegativeInteger(pStats, "completed_maps", Difficulty.m_CompletedMaps) ||
				!ReadNonNegativeInteger(pStats, "remaining_maps", Difficulty.m_RemainingMaps) ||
				!ReadOptionalNonNegativeInteger(pStats, "total_points", Difficulty.m_TotalPoints) ||
				!ReadOptionalNonNegativeInteger(pStats, "total_maps", Difficulty.m_TotalMaps))
			{
				return EQmAxiomParseResult::INVALID_RESPONSE;
			}
			Score.m_vDifficulties.push_back(std::move(Difficulty));
		}

		OutScore = std::move(Score);
		return EQmAxiomParseResult::SUCCESS;
	}
}

const char *QmAxiomModeName(EQmAxiomMode Mode)
{
	switch(Mode)
	{
	case EQmAxiomMode::NONE:
		return "";
	case EQmAxiomMode::GORES:
		return "Gores";
	case EQmAxiomMode::AXRACE:
		return "AXRace";
	}
	return "";
}

EQmAxiomMode QmResolveAxiomModeFromServerContext(const SQmAxiomServerContext &Context)
{
	const char *pCommunityType = Context.m_pCommunityType;
	if(pCommunityType != nullptr && pCommunityType[0] != '\0')
	{
		if(str_comp_nocase(pCommunityType, AXIOM_COMMUNITY_TYPE_GORES) == 0)
			return EQmAxiomMode::GORES;
		if(str_comp_nocase(pCommunityType, AXIOM_COMMUNITY_TYPE_OTHER) == 0)
			return EQmAxiomMode::AXRACE;
	}

	const char *pServerName = Context.m_pServerName;
	if(pServerName != nullptr && str_find_nocase(pServerName, AXIOM_SERVER_NAME_MARKER) != nullptr)
		return EQmAxiomMode::AXRACE;

	return EQmAxiomMode::GORES;
}

std::string QmBuildAxiomSearchUrl(const char *pPlayerName)
{
	char aEncodedName[1024];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName ? pPlayerName : "");
	char aUrl[1400];
	str_format(aUrl, sizeof(aUrl), "https://api.axiom.teeworlds.cn/v1/query/search?q=%s&search_type=player&page=1&limit=18", aEncodedName);
	return aUrl;
}

std::string QmBuildAxiomInfoUrl(int64_t UserId, EQmAxiomMode Mode)
{
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "https://api.axiom.teeworlds.cn/v1/query/user/info?user_id=%lld&mode=%s", (long long)UserId, QmAxiomModeName(Mode));
	return aUrl;
}

EQmAxiomParseResult QmParseAxiomSearchResponse(const char *pData, size_t DataSize, const char *pPlayerName, SQmAxiomSearchMatch &OutMatch)
{
	if(!pData || DataSize == 0 || DataSize > MAX_AXIOM_JSON_BYTES)
		return EQmAxiomParseResult::INVALID_RESPONSE;
	json_value *pRoot = JsonParse(pData, DataSize);
	if(!pRoot)
		return EQmAxiomParseResult::INVALID_RESPONSE;
	const EQmAxiomParseResult Result = ParseSearchRoot(pRoot, pPlayerName, OutMatch);
	json_value_free(pRoot);
	return Result;
}

EQmAxiomParseResult QmParseAxiomInfoResponse(const char *pData, size_t DataSize, SQmAxiomModeScore &OutScore)
{
	if(!pData || DataSize == 0 || DataSize > MAX_AXIOM_JSON_BYTES)
		return EQmAxiomParseResult::INVALID_RESPONSE;
	json_value *pRoot = JsonParse(pData, DataSize);
	if(!pRoot)
		return EQmAxiomParseResult::INVALID_RESPONSE;
	const EQmAxiomParseResult Result = ParseInfoRoot(pRoot, OutScore);
	json_value_free(pRoot);
	return Result;
}

bool QmAxiomResponseIsCurrent(uint64_t CurrentGeneration, uint64_t ResponseGeneration, EQmAxiomMode CurrentMode, EQmAxiomMode ResponseMode)
{
	return CurrentGeneration == ResponseGeneration && CurrentMode == ResponseMode;
}
