#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_DATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FINISH_SUMMARY_DATA_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace QmFinishSummary
{
	enum class ECommunity
	{
		NONE,
		DDNET,
		AXIOM_GORES,
		AXIOM_AXRACE
	};
	enum class EStatus
	{
		LOADING,
		READY,
		PENDING,
		UNAVAILABLE,
		STALE
	};
	enum class EComparison
	{
		UNKNOWN,
		FIRST,
		BEST,
		IMPROVED,
		TIED,
		SLOWER
	};

	struct SValue
	{
		int64_t m_Value = -1;
		EStatus m_Status = EStatus::LOADING;
	};

	struct SRemoteData
	{
		std::string m_Map;
		std::string m_Mapper;
		std::string m_Category;
		int m_Stars = -1;
		int m_MapPoints = -1;
		std::optional<int64_t> m_BestTimeMs;
		std::optional<int64_t> m_AccountBestTimeMs;
		std::optional<int64_t> m_Rank;
		std::optional<int64_t> m_ChinaRank;
		std::optional<int64_t> m_ChinaBestTimeMs;
		bool m_ChinaRankKnown = false;
		std::optional<int64_t> m_Points;
		std::optional<int64_t> m_CompletedMaps;
		std::optional<int64_t> m_TotalMaps;
		bool m_HistoryKnown = false;
		bool m_AccountHistoryKnown = false;
		std::optional<int64_t> m_LatestTimeMs;
		std::optional<int64_t> m_LatestTimestamp;
		int64_t m_UserId = -1;
	};

	struct SSnapshot
	{
		uint64_t m_Id = 0;
		uint64_t m_Revision = 0;
		ECommunity m_Community = ECommunity::NONE;
		std::string m_Map;
		std::string m_Mapper;
		std::string m_Category;
		std::string m_Player;
		std::string m_FinishedAt;
		int m_ClientId = -1;
		int m_Stars = -1;
		int m_MaxStars = 5;
		int64_t m_TimeMs = 0;
		int64_t m_DifferenceMs = 0;
		EComparison m_Comparison = EComparison::UNKNOWN;
		SValue m_LocalRank;
		SValue m_LocalRankTotal;
		SValue m_Rank;
		SValue m_ChinaRank;
		SValue m_CompletedMaps;
		SValue m_TotalMaps;
		SValue m_Points;
		SValue m_PointsGained;
	};

	struct SOnlineTime
	{
		std::string m_Identity;
		std::optional<int64_t> m_TimeMs;
		bool m_Known = false;
	};

	// 返回成功表示已核对响应身份和结构，缺少可选字段仍保留未知状态。
	bool ParseDdnetMap(const char *pJson, size_t Length, const std::string &Map, SRemoteData &Out);
	bool ParseDdnetPlayer(const char *pJson, size_t Length, const std::string &Player, const std::string &Map, SRemoteData &Out);
	bool ParseDdnetChinaRank(const char *pJson, size_t Length, const std::string &Player, const std::string &Map, SRemoteData &Out);
	bool ParseAxiomSearch(const char *pJson, size_t Length, const std::string &Player, SRemoteData &Out);
	bool ParseAxiomMap(const char *pJson, size_t Length, const std::string &Map, SRemoteData &Out, const char *pMode = nullptr);
	bool ParseAxiomTop(const char *pJson, size_t Length, const std::string &Map, int64_t UserId, SRemoteData &Out, const char *pMode = nullptr);
	bool ParseAxiomPoints(const char *pJson, size_t Length, int64_t UserId, SRemoteData &Out, const char *pMode = nullptr);
	bool ParseAxiomProgress(const char *pJson, size_t Length, int64_t UserId, const std::string &Category, SRemoteData &Out, const char *pMode = nullptr);
	bool ParseAxiomTimes(const char *pJson, size_t Length, const std::string &Map, int64_t UserId, SRemoteData &Out, const char *pMode = nullptr);

	void ResolveComparison(SSnapshot &Snapshot, std::optional<int64_t> PreviousBest, bool HistoryKnown, std::optional<int64_t> Difference, bool PersonalRecord);
	void ResolveLocalRank(SSnapshot &Snapshot, const std::vector<SOnlineTime> &vTimes, std::optional<int64_t> PreviousBest, bool Precise);
	// 基线只在通关前取得；账号首通和本次成绩同步分别核对。
	void ApplyRemote(SSnapshot &Snapshot, const SRemoteData &Before, const SRemoteData &After, bool ConfirmedFinish);
}

#endif
