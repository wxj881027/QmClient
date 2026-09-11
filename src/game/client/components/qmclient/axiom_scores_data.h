#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_DATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_DATA_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class EQmAxiomMode
{
	NONE,
	GORES,
	AXRACE,
};

// 判断当前服务器属于 Axiom 的哪个积分模式所需的最小环境信息。
struct SQmAxiomServerContext
{
	const char *m_pCommunityType;
	const char *m_pServerName;
};

enum class EQmAxiomParseResult
{
	SUCCESS,
	NOT_FOUND,
	AMBIGUOUS,
	API_ERROR,
	INVALID_RESPONSE,
};

struct SQmAxiomSearchMatch
{
	int64_t m_UserId = 0;
	std::string m_PlayerName;
	std::string m_DummyName;
};

struct SQmAxiomDifficultyStats
{
	std::string m_Name;
	int64_t m_Points = 0;
	std::optional<int64_t> m_GlobalRank;
	std::optional<int64_t> m_TeamRank;
	int64_t m_CompletedMaps = 0;
	int64_t m_RemainingMaps = 0;
	std::optional<int64_t> m_TotalPoints;
	std::optional<int64_t> m_TotalMaps;
};

struct SQmAxiomModeScore
{
	std::string m_PlayerName;
	int64_t m_Points = 0;
	std::optional<int64_t> m_GlobalRank;
	std::optional<int64_t> m_TeamRank;
	int64_t m_TotalPlayTime = 0;
	int64_t m_TotalMapsCompleted = 0;
	int64_t m_PerformancePoints = 0;
	int64_t m_Mileage = 0;
	std::vector<SQmAxiomDifficultyStats> m_vDifficulties;
};

const char *QmAxiomModeName(EQmAxiomMode Mode);
EQmAxiomMode QmResolveAxiomModeFromServerContext(const SQmAxiomServerContext &Context);
std::string QmBuildAxiomSearchUrl(const char *pPlayerName);
std::string QmBuildAxiomInfoUrl(int64_t UserId, EQmAxiomMode Mode);
EQmAxiomParseResult QmParseAxiomSearchResponse(const char *pData, size_t DataSize, const char *pPlayerName, SQmAxiomSearchMatch &OutMatch);
EQmAxiomParseResult QmParseAxiomInfoResponse(const char *pData, size_t DataSize, SQmAxiomModeScore &OutScore);
bool QmAxiomResponseIsCurrent(uint64_t CurrentGeneration, uint64_t ResponseGeneration, EQmAxiomMode CurrentMode, EQmAxiomMode ResponseMode);

#endif
