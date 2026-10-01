// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/detect.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/shared/config.h>

#include <game/server/scoreworker.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#if defined(CONF_TEST_MYSQL)
int DummyMysqlInit = (MysqlInit(), 1);
#endif

TEST(SQLite, Version)
{
	ASSERT_GE(sqlite3_libversion_number(), 3025000) << "SQLite >= 3.25.0 required for Window functions";
}

struct Score : public testing::TestWithParam<IDbConnection *>
{
	Score()
	{
		Connect();
		LoadBestTime();
		InsertMap("Kobra 3", "Zerodin", "Novice", 5, 5);
	}

	~Score()
	{
		m_pConn->Disconnect();
	}

	void Connect()
	{
		ASSERT_TRUE(m_pConn->Connect(m_aError, sizeof(m_aError))) << m_aError;

		// Delete all existing entries for persistent databases like MySQL
		int NumInserted = 0;
		ASSERT_TRUE(m_pConn->PrepareStatement("DELETE FROM record_race", m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->PrepareStatement("DELETE FROM record_teamrace", m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->PrepareStatement("DELETE FROM record_maps", m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->PrepareStatement("DELETE FROM record_points", m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->PrepareStatement("DELETE FROM record_saves", m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
	}

	void LoadBestTime()
	{
		CSqlLoadBestTimeRequest loadBestTimeReq(std::make_shared<CScoreLoadBestTimeResult>());
		str_copy(loadBestTimeReq.m_aMap, "Kobra 3", sizeof(loadBestTimeReq.m_aMap));
		ASSERT_TRUE(CScoreWorker::LoadBestTime(m_pConn, &loadBestTimeReq, m_aError, sizeof(m_aError))) << m_aError;
	}

	void InsertMap(const char *pName, const char *pMapper, const char *pServer, int Points, int Stars)
	{
		char aTimestamp[32];
		str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf),
			"%s into %s_maps(Map, Server, Mapper, Points, Stars, Timestamp) "
			"VALUES (\"%s\", \"%s\", \"%s\", %d, %d, %s)",
			m_pConn->InsertIgnore(), m_pConn->GetPrefix(), pName, pServer, pMapper, Points, Stars, m_pConn->InsertTimestampAsUtc());
		ASSERT_TRUE(m_pConn->PrepareStatement(aBuf, m_aError, sizeof(m_aError))) << m_aError;
		m_pConn->BindString(1, aTimestamp);
		int NumInserted = 0;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_EQ(NumInserted, 1);
	}

	void InsertRank(float Time = 100.0, bool WithTimeCheckPoints = false, const char *pName = "nameless tee")
	{
		str_copy(g_Config.m_SvSqlServerName, "USA", sizeof(g_Config.m_SvSqlServerName));
		CSqlScoreData ScoreData(std::make_shared<CScorePlayerResult>());
		str_copy(ScoreData.m_aMap, "Kobra 3", sizeof(ScoreData.m_aMap));
		str_copy(ScoreData.m_aGameUuid, "8d300ecf-5873-4297-bee5-95668fdff320", sizeof(ScoreData.m_aGameUuid));
		str_copy(ScoreData.m_aName, pName, sizeof(ScoreData.m_aName));
		ScoreData.m_ClientId = 0;
		ScoreData.m_Time = Time;
		str_copy(ScoreData.m_aTimestamp, "2021-11-24 19:24:08", sizeof(ScoreData.m_aTimestamp));
		for(int i = 0; i < NUM_CHECKPOINTS; i++)
			ScoreData.m_aCurrentTimeCp[i] = WithTimeCheckPoints ? i : 0;
		str_copy(ScoreData.m_aRequestingPlayer, "deen", sizeof(ScoreData.m_aRequestingPlayer));
		ASSERT_TRUE(CScoreWorker::SaveScore(m_pConn, &ScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;
	}

	void InsertRawSave(const char *pMap, const char *pCode, const char *pSavegame, const char *pServer, const char *pSaveId)
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf),
			"INSERT INTO %s_saves(Savegame, Map, Code, Timestamp, Server, SaveId, DDNet7) "
			"VALUES (?, ?, ?, CURRENT_TIMESTAMP, ?, ?, %s)",
			m_pConn->GetPrefix(), m_pConn->False());
		ASSERT_TRUE(m_pConn->PrepareStatement(aBuf, m_aError, sizeof(m_aError))) << m_aError;
		m_pConn->BindString(1, pSavegame);
		m_pConn->BindString(2, pMap);
		m_pConn->BindString(3, pCode);
		m_pConn->BindString(4, pServer);
		m_pConn->BindString(5, pSaveId);
		int NumInserted = 0;
		ASSERT_TRUE(m_pConn->ExecuteUpdate(&NumInserted, m_aError, sizeof(m_aError))) << m_aError;
		ASSERT_EQ(NumInserted, 1);
	}

	void ExpectLines(const std::shared_ptr<CScorePlayerResult> &pPlayerResult, std::initializer_list<const char *> Lines, bool All = false)
	{
		EXPECT_EQ(pPlayerResult->m_MessageKind, All ? CScorePlayerResult::ALL : CScorePlayerResult::DIRECT);

		int i = 0;
		for(const char *pLine : Lines)
		{
			EXPECT_STREQ(pPlayerResult->m_Data.m_aaMessages[i], pLine);
			i++;
		}

		for(; i < CScorePlayerResult::MAX_MESSAGES; i++)
		{
			EXPECT_STREQ(pPlayerResult->m_Data.m_aaMessages[i], "");
		}
	}

	IDbConnection *m_pConn{GetParam()};
	char m_aError[256] = {};
	std::shared_ptr<CScorePlayerResult> m_pPlayerResult{std::make_shared<CScorePlayerResult>()};
	CSqlPlayerRequest m_PlayerRequest{m_pPlayerResult};
};

struct SingleScore : public Score
{
	SingleScore()
	{
		InsertRank();
		str_copy(m_PlayerRequest.m_aMap, "Kobra 3", sizeof(m_PlayerRequest.m_aMap));
		str_copy(m_PlayerRequest.m_aRequestingPlayer, "brainless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
		m_PlayerRequest.m_Offset = 0;
		str_copy(m_PlayerRequest.m_aServer, "GER", sizeof(m_PlayerRequest.m_aServer));
		str_copy(m_PlayerRequest.m_aName, "nameless tee", sizeof(m_PlayerRequest.m_aMap));
	}
};

TEST_P(SingleScore, TopRegional)
{
	g_Config.m_SvRegionalRankings = true;
	ASSERT_TRUE(CScoreWorker::ShowTop(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------------ 全局排行 ------------",
			"1. nameless tee 时间：01:40.00",
			"------------ GER 排行 ------------"});
}

TEST_P(SingleScore, Top)
{
	g_Config.m_SvRegionalRankings = false;
	ASSERT_TRUE(CScoreWorker::ShowTop(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------------ 全局排行 ------------",
			"1. nameless tee 时间：01:40.00",
			"-----------------------------------------"});
}

TEST_P(SingleScore, RankRegional)
{
	g_Config.m_SvRegionalRankings = true;
	ASSERT_TRUE(CScoreWorker::ShowRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"nameless tee - 01:40.00 - 超过 100% 的玩家 - 由 brainless tee 查询", "全局排名 1 - GER 未上榜"}, true);
}

TEST_P(SingleScore, Rank)
{
	g_Config.m_SvRegionalRankings = false;
	ASSERT_TRUE(CScoreWorker::ShowRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"nameless tee - 01:40.00 - 超过 100% 的玩家 - 由 brainless tee 查询", "全局排名 1"}, true);
}

TEST_P(SingleScore, TopServerRegional)
{
	g_Config.m_SvRegionalRankings = true;
	str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
	ASSERT_TRUE(CScoreWorker::ShowTop(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------------ 全局排行 ------------",
			"1. nameless tee 时间：01:40.00",
			"------------ USA 排行 ------------",
			"1. nameless tee 时间：01:40.00"});
}

TEST_P(SingleScore, TopServer)
{
	g_Config.m_SvRegionalRankings = false;
	str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
	ASSERT_TRUE(CScoreWorker::ShowTop(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------------ 全局排行 ------------",
			"1. nameless tee 时间：01:40.00",
			"-----------------------------------------"});
}

TEST_P(SingleScore, TopPaginationKeepsGlobalRanks)
{
	g_Config.m_SvRegionalRankings = false;
	InsertRank(200.0, false, "second tee");
	InsertRank(300.0, false, "third tee");
	InsertRank(400.0, false, "fourth tee");
	InsertRank(500.0, false, "fifth tee");
	InsertRank(600.0, false, "sixth tee");
	InsertRank(700.0, false, "seventh tee");
	m_PlayerRequest.m_Offset = 6;
	ASSERT_TRUE(CScoreWorker::ShowTop(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------------ 全局排行 ------------",
			"6. sixth tee 时间：10:00.00",
			"7. seventh tee 时间：11:40.00",
			"-----------------------------------------"});
}

TEST_P(SingleScore, RankServerRegional)
{
	g_Config.m_SvRegionalRankings = true;
	str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
	ASSERT_TRUE(CScoreWorker::ShowRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"nameless tee - 01:40.00 - 超过 100% 的玩家 - 由 brainless tee 查询", "全局排名 1 - USA 排名 1"}, true);
}

TEST_P(SingleScore, RankServer)
{
	g_Config.m_SvRegionalRankings = false;
	str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
	ASSERT_TRUE(CScoreWorker::ShowRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"nameless tee - 01:40.00 - 超过 100% 的玩家 - 由 brainless tee 查询", "全局排名 1"}, true);
}

TEST_P(SingleScore, RankPercent)
{
	g_Config.m_SvRegionalRankings = false;
	InsertRank(200.0, false, "second tee");
	InsertRank(300.0, false, "third tee");
	InsertRank(400.0, false, "fourth tee");
	str_copy(m_PlayerRequest.m_aName, "third tee");
	ASSERT_TRUE(CScoreWorker::ShowRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"third tee - 05:00.00 - 超过 33% 的玩家 - 由 brainless tee 查询", "全局排名 3"}, true);
}

TEST_P(SingleScore, LoadPlayerData)
{
	InsertRank(120.0, true);
	str_copy(m_PlayerRequest.m_aName, "", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	ASSERT_TRUE(CScoreWorker::LoadPlayerData(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::PLAYER_INFO);
	ASSERT_FALSE(m_pPlayerResult->m_Data.m_Info.m_Time.has_value());
	for(auto &Time : m_pPlayerResult->m_Data.m_Info.m_aTimeCp)
	{
		ASSERT_EQ(Time, 0);
	}

	str_copy(m_PlayerRequest.m_aRequestingPlayer, "nameless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	str_copy(m_PlayerRequest.m_aName, "", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	ASSERT_TRUE(CScoreWorker::LoadPlayerData(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::PLAYER_INFO);
	ASSERT_TRUE(m_pPlayerResult->m_Data.m_Info.m_Time.has_value());
	ASSERT_EQ(*m_pPlayerResult->m_Data.m_Info.m_Time, 100.0);
	for(int i = 0; i < NUM_CHECKPOINTS; i++)
	{
		ASSERT_EQ(m_pPlayerResult->m_Data.m_Info.m_aTimeCp[i], i);
	}

	str_copy(m_PlayerRequest.m_aRequestingPlayer, "finishless", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	str_copy(m_PlayerRequest.m_aName, "nameless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	ASSERT_TRUE(CScoreWorker::LoadPlayerData(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::PLAYER_INFO);
	ASSERT_FALSE(m_pPlayerResult->m_Data.m_Info.m_Time.has_value());
	for(int i = 0; i < NUM_CHECKPOINTS; i++)
	{
		ASSERT_EQ(m_pPlayerResult->m_Data.m_Info.m_aTimeCp[i], i);
	}
}

TEST_P(SingleScore, TimeCpDoesntExist)
{
	str_copy(m_PlayerRequest.m_aName, "foo", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::LoadPlayerTimeCp(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"'foo' 没有可用的检查点时间"});
}

TEST_P(SingleScore, TimesExists)
{
	ASSERT_TRUE(CScoreWorker::ShowTimes(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::DIRECT);
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[0], "------------- 最近成绩 -------------");
	char aBuf[128];
	str_copy(aBuf, m_pPlayerResult->m_Data.m_aaMessages[1], 7);
	EXPECT_STREQ(aBuf, "[USA] ");

	str_copy(aBuf, m_pPlayerResult->m_Data.m_aaMessages[1] + str_length(m_pPlayerResult->m_Data.m_aaMessages[1]) - 11, 12);
	EXPECT_STREQ(aBuf, "，01:40.00");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[2], "-------------------------------------------");
	for(int i = 3; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

TEST_P(SingleScore, TimesDoesntExist)
{
	str_copy(m_PlayerRequest.m_aName, "foo", sizeof(m_PlayerRequest.m_aMap));
	ASSERT_TRUE(CScoreWorker::ShowTimes(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"指定范围内没有成绩记录"});
}

struct TeamScore : public Score
{
	void SetUp() override
	{
		InsertTeamRank(100.0);
	}

	void InsertTeamRank(float Time = 100.0)
	{
		str_copy(g_Config.m_SvSqlServerName, "USA", sizeof(g_Config.m_SvSqlServerName));
		CSqlTeamScoreData teamScoreData;
		CSqlScoreData ScoreData(std::make_shared<CScorePlayerResult>());
		str_copy(teamScoreData.m_aMap, "Kobra 3", sizeof(teamScoreData.m_aMap));
		str_copy(ScoreData.m_aMap, "Kobra 3", sizeof(ScoreData.m_aMap));
		str_copy(teamScoreData.m_aGameUuid, "8d300ecf-5873-4297-bee5-95668fdff320", sizeof(teamScoreData.m_aGameUuid));
		str_copy(ScoreData.m_aGameUuid, "8d300ecf-5873-4297-bee5-95668fdff320", sizeof(ScoreData.m_aGameUuid));
		teamScoreData.m_Size = 2;
		str_copy(teamScoreData.m_aaNames[0], "nameless tee", sizeof(teamScoreData.m_aaNames[0]));
		str_copy(teamScoreData.m_aaNames[1], "brainless tee", sizeof(teamScoreData.m_aaNames[1]));
		teamScoreData.m_Time = Time;
		ScoreData.m_Time = Time;
		str_copy(teamScoreData.m_aTimestamp, "2021-11-24 19:24:08", sizeof(teamScoreData.m_aTimestamp));
		str_copy(ScoreData.m_aTimestamp, "2021-11-24 19:24:08", sizeof(ScoreData.m_aTimestamp));
		for(int i = 0; i < NUM_CHECKPOINTS; i++)
			ScoreData.m_aCurrentTimeCp[i] = 0;
		ASSERT_TRUE(CScoreWorker::SaveTeamScore(m_pConn, &teamScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;

		str_copy(m_PlayerRequest.m_aMap, "Kobra 3", sizeof(m_PlayerRequest.m_aMap));
		str_copy(m_PlayerRequest.m_aRequestingPlayer, "brainless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
		str_copy(ScoreData.m_aRequestingPlayer, "brainless tee", sizeof(ScoreData.m_aRequestingPlayer));

		str_copy(ScoreData.m_aName, "nameless tee", sizeof(ScoreData.m_aName));
		ScoreData.m_ClientId = 0;
		ASSERT_TRUE(CScoreWorker::SaveScore(m_pConn, &ScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;
		str_copy(ScoreData.m_aName, "brainless tee", sizeof(ScoreData.m_aName));
		ScoreData.m_ClientId = 1;
		ASSERT_TRUE(CScoreWorker::SaveScore(m_pConn, &ScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;
		m_PlayerRequest.m_Offset = 0;
	}
};

TEST_P(TeamScore, All)
{
	g_Config.m_SvRegionalRankings = false;
	ASSERT_TRUE(CScoreWorker::ShowTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------- 队伍前 5 名 -------",
			"1. brainless tee & nameless tee 队伍时间：01:40.00",
			"-------------------------------"});
}

TEST_P(TeamScore, TeamTop5Regional)
{
	g_Config.m_SvRegionalRankings = true;
	str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
	ASSERT_TRUE(CScoreWorker::ShowTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------- 队伍前 5 名 -------",
			"1. brainless tee & nameless tee 队伍时间：01:40.00",
			"----- USA 队伍排行 -----",
			"1. brainless tee & nameless tee 队伍时间：01:40.00"});
}

TEST_P(TeamScore, PlayerExists)
{
	str_copy(m_PlayerRequest.m_aName, "brainless tee", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::ShowPlayerTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------- 队伍前 5 名 -------",
			"1. brainless tee & nameless tee 队伍时间：01:40.00",
			"---------------------------------"});
}

TEST_P(TeamScore, PlayerDoesntExist)
{
	str_copy(m_PlayerRequest.m_aName, "foo", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::ShowPlayerTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"foo 没有队伍排名"});
}

TEST_P(TeamScore, RankUpdates)
{
	InsertTeamRank(98.0);
	str_copy(m_PlayerRequest.m_aName, "brainless tee", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::ShowPlayerTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"------- 队伍前 5 名 -------",
			"1. brainless tee & nameless tee 队伍时间：01:38.00",
			"---------------------------------"});
}

struct BigTeamScore : public Score
{
	void SetUp() override
	{
		str_copy(g_Config.m_SvSqlServerName, "USA", sizeof(g_Config.m_SvSqlServerName));

		CSqlTeamScoreData TeamScoreData;
		str_copy(TeamScoreData.m_aMap, "Kobra 3", sizeof(TeamScoreData.m_aMap));
		str_copy(TeamScoreData.m_aGameUuid, "8d300ecf-5873-4297-bee5-95668fdff320", sizeof(TeamScoreData.m_aGameUuid));
		TeamScoreData.m_Size = MAX_CLIENTS;
		TeamScoreData.m_Time = 100.0f;
		str_copy(TeamScoreData.m_aTimestamp, "2021-11-24 19:24:08", sizeof(TeamScoreData.m_aTimestamp));
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			str_format(TeamScoreData.m_aaNames[i], sizeof(TeamScoreData.m_aaNames[i]), "playertee12_%03d", i);
			ASSERT_EQ(str_length(TeamScoreData.m_aaNames[i]), MAX_NAME_LENGTH - 1);
		}
		ASSERT_TRUE(CScoreWorker::SaveTeamScore(m_pConn, &TeamScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;

		CSqlScoreData ScoreData(std::make_shared<CScorePlayerResult>());
		str_copy(ScoreData.m_aMap, "Kobra 3", sizeof(ScoreData.m_aMap));
		str_copy(ScoreData.m_aGameUuid, "8d300ecf-5873-4297-bee5-95668fdff320", sizeof(ScoreData.m_aGameUuid));
		ScoreData.m_Time = 100.0f;
		str_copy(ScoreData.m_aTimestamp, "2021-11-24 19:24:08", sizeof(ScoreData.m_aTimestamp));
		for(int i = 0; i < NUM_CHECKPOINTS; i++)
			ScoreData.m_aCurrentTimeCp[i] = 0;
		str_copy(ScoreData.m_aRequestingPlayer, TeamScoreData.m_aaNames[0], sizeof(ScoreData.m_aRequestingPlayer));
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			str_copy(ScoreData.m_aName, TeamScoreData.m_aaNames[i], sizeof(ScoreData.m_aName));
			ScoreData.m_ClientId = i;
			ASSERT_TRUE(CScoreWorker::SaveScore(m_pConn, &ScoreData, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;
		}

		str_copy(m_PlayerRequest.m_aMap, "Kobra 3", sizeof(m_PlayerRequest.m_aMap));
		str_copy(m_PlayerRequest.m_aRequestingPlayer, TeamScoreData.m_aaNames[0], sizeof(m_PlayerRequest.m_aRequestingPlayer));
		str_copy(m_PlayerRequest.m_aName, TeamScoreData.m_aaNames[0], sizeof(m_PlayerRequest.m_aName));
		str_copy(m_PlayerRequest.m_aServer, "USA", sizeof(m_PlayerRequest.m_aServer));
		m_PlayerRequest.m_Offset = 0;
	}

	void ExpectMessagesFitIntoChat()
	{
		for(const auto &aMessage : m_pPlayerResult->m_Data.m_aaMessages)
			EXPECT_LT(str_length(aMessage), MAX_CHAT_LENGTH) << aMessage;
	}
};

TEST_P(BigTeamScore, TeamRankShortensNames)
{
	ASSERT_TRUE(CScoreWorker::ShowTeamRank(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectMessagesFitIntoChat();
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[0], testing::HasSubstr("，还有 "));
}

TEST_P(BigTeamScore, TeamTop5ShortensNames)
{
	g_Config.m_SvRegionalRankings = false;
	ASSERT_TRUE(CScoreWorker::ShowTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectMessagesFitIntoChat();
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[1], testing::HasSubstr("，还有 "));
}

TEST_P(BigTeamScore, PlayerTeamTop5ShortensNames)
{
	ASSERT_TRUE(CScoreWorker::ShowPlayerTeamTop5(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectMessagesFitIntoChat();
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[1], testing::HasSubstr("，还有 "));
}

TEST(ScoreWorker, TeamrankCountsUnstoredNames)
{
	CTeamrank Teamrank;
	Teamrank.m_NumNames = MAX_CLIENTS;
	Teamrank.m_TotalNames = MAX_CLIENTS + 1;
	std::vector<std::string> vNames(MAX_CLIENTS, "a");
	for(int i = 0; i < MAX_CLIENTS; i++)
		str_copy(Teamrank.m_aaNames[i], vNames[i].c_str(), sizeof(Teamrank.m_aaNames[i]));

	EXPECT_FALSE(Teamrank.SamePlayers(&vNames));

	char aNames[MAX_CHAT_LENGTH];
	Teamrank.FormatNames(aNames, sizeof(aNames));
	EXPECT_LT(str_length(aNames), MAX_CHAT_LENGTH);
	EXPECT_THAT(aNames, testing::HasSubstr("，还有 49 人"));
}

struct MapInfo : public Score
{
	MapInfo()
	{
		str_copy(m_PlayerRequest.m_aRequestingPlayer, "brainless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	}
};

TEST_P(MapInfo, ExactNoFinish)
{
	str_copy(m_PlayerRequest.m_aName, "Kobra 3", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapInfo(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::DIRECT);
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[0], testing::MatchesRegex("\"Kobra 3\"，作者 Zerodin，服务器 Novice，★★★★★，5 积分，发布于 .* 前，0 次通关，0 名玩家通关"));
	for(int i = 1; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

TEST_P(MapInfo, ExactFinish)
{
	InsertRank(42.87f);
	str_copy(m_PlayerRequest.m_aRequestingPlayer, "nameless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	str_copy(m_PlayerRequest.m_aName, "Kobra 3", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapInfo(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::DIRECT);
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[0], testing::MatchesRegex("\"Kobra 3\"，作者 Zerodin，服务器 Novice，★★★★★，5 积分，发布于 .* 前，1 次通关，1 名玩家通关，中位时间 00:42，你的成绩：42.87"));
	for(int i = 1; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

TEST_P(MapInfo, Fuzzy)
{
	InsertRank();
	str_copy(m_PlayerRequest.m_aName, "k3", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapInfo(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::DIRECT);
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[0], testing::MatchesRegex("\"Kobra 3\"，作者 Zerodin，服务器 Novice，★★★★★，5 积分，发布于 .* 前，1 次通关，1 名玩家通关，中位时间 01:40"));
	for(int i = 1; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

TEST_P(MapInfo, FuzzyCase)
{
	InsertMap("Reflect", "DarkOort", "Dummy", 20, 3);
	InsertMap("reflects", "Ninjed & Pipou", "Solo", 16, 4);
	str_copy(m_PlayerRequest.m_aName, "reflect", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapInfo(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;

	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::DIRECT);
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[0], testing::MatchesRegex("\"Reflect\"，作者 DarkOort，服务器 Dummy，★★★✰✰，20 积分，发布于 .* 前，0 次通关，0 名玩家通关"));
	for(int i = 1; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

TEST_P(MapInfo, DoesntExit)
{
	str_copy(m_PlayerRequest.m_aName, "f", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapInfo(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"没有找到类似于 \"f\" 的地图。"});
}

struct MapVote : public Score
{
	MapVote()
	{
		str_copy(m_PlayerRequest.m_aRequestingPlayer, "brainless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
	}
};

TEST_P(MapVote, Exact)
{
	str_copy(m_PlayerRequest.m_aName, "Kobra 3", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapVote(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::MAP_VOTE);
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aMap, "Kobra 3");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aReason, "/map");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aServer, "novice");
}

TEST_P(MapVote, Fuzzy)
{
	str_copy(m_PlayerRequest.m_aName, "k3", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapVote(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::MAP_VOTE);
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aMap, "Kobra 3");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aReason, "/map");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aServer, "novice");
}

TEST_P(MapVote, FuzzyCase)
{
	InsertMap("Reflect", "DarkOort", "Dummy", 20, 3);
	InsertMap("reflects", "Ninjed & Pipou", "Solo", 16, 4);
	str_copy(m_PlayerRequest.m_aName, "reflect", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapVote(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pPlayerResult->m_MessageKind, CScorePlayerResult::MAP_VOTE);
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aMap, "Reflect");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aReason, "/map");
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_MapVote.m_aServer, "dummy");
}

TEST_P(MapVote, DoesntExist)
{
	str_copy(m_PlayerRequest.m_aName, "f", sizeof(m_PlayerRequest.m_aName));
	ASSERT_TRUE(CScoreWorker::MapVote(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"没有找到类似于 \"f\" 的地图。如果不知道首字母，可以在开头加 '%'。例如：/map %castle 可以找到 \"Out of Castle\""});
}

struct Points : public Score
{
	Points()
	{
		str_copy(m_PlayerRequest.m_aName, "nameless tee", sizeof(m_PlayerRequest.m_aName));
		str_copy(m_PlayerRequest.m_aRequestingPlayer, "brainless tee", sizeof(m_PlayerRequest.m_aRequestingPlayer));
		m_PlayerRequest.m_Offset = 0;
	}
};

TEST_P(Points, NoPoints)
{
	ASSERT_TRUE(CScoreWorker::ShowPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"nameless tee 目前还没有获得任何积分"});
}

TEST_P(Points, NoPointsTop)
{
	ASSERT_TRUE(CScoreWorker::ShowTopPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"-------- 积分排行 --------",
					     "-------------------------------"});
}

TEST_P(Points, OnePoints)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"1. nameless tee 积分：2，由 brainless tee 查询"}, true);
}

TEST_P(Points, OnePointsTop)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowTopPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"-------- 积分排行 --------",
			"1. nameless tee 积分：2",
			"-------------------------------"});
}

TEST_P(Points, TwoPoints)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("brainless tee", 3, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"2. nameless tee 积分：2，由 brainless tee 查询"}, true);
}

TEST_P(Points, TwoPointsTop)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("brainless tee", 3, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowTopPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"-------- 积分排行 --------",
			"1. brainless tee 积分：3",
			"2. nameless tee 积分：2",
			"-------------------------------"});
}

TEST_P(Points, EqualPoints)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("brainless tee", 3, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("nameless tee", 1, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult, {"1. nameless tee 积分：3，由 brainless tee 查询"}, true);
}

TEST_P(Points, EqualPointsTop)
{
	m_pConn->AddPoints("nameless tee", 2, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("brainless tee", 3, m_aError, sizeof(m_aError));
	m_pConn->AddPoints("nameless tee", 1, m_aError, sizeof(m_aError));
	ASSERT_TRUE(CScoreWorker::ShowTopPoints(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	ExpectLines(m_pPlayerResult,
		{"-------- 积分排行 --------",
			"1. brainless tee 积分：3",
			"1. nameless tee 积分：3",
			"-------------------------------"});
}

struct RandomMap : public Score
{
	std::shared_ptr<CScoreRandomMapResult> m_pRandomMapResult{std::make_shared<CScoreRandomMapResult>(0)};
	CSqlRandomMapRequest m_RandomMapRequest{m_pRandomMapResult};

	RandomMap()
	{
		str_copy(m_RandomMapRequest.m_aServerType, "Novice", sizeof(m_RandomMapRequest.m_aServerType));
		str_copy(m_RandomMapRequest.m_aCurrentMap, "Kobra 4", sizeof(m_RandomMapRequest.m_aCurrentMap));
		str_copy(m_RandomMapRequest.m_aRequestingPlayer, "nameless tee", sizeof(m_RandomMapRequest.m_aRequestingPlayer));
	}
};

TEST_P(RandomMap, NoStars)
{
	m_RandomMapRequest.m_MinStars = -1;
	m_RandomMapRequest.m_MaxStars = -1;
	ASSERT_TRUE(CScoreWorker::RandomMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STREQ(m_pRandomMapResult->m_aMap, "Kobra 3");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "");
}

TEST_P(RandomMap, StarsExists)
{
	m_RandomMapRequest.m_MinStars = 5;
	m_RandomMapRequest.m_MaxStars = 5;
	ASSERT_TRUE(CScoreWorker::RandomMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STREQ(m_pRandomMapResult->m_aMap, "Kobra 3");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "");
}

TEST_P(RandomMap, StarsRangeExists)
{
	m_RandomMapRequest.m_MinStars = 1;
	m_RandomMapRequest.m_MaxStars = 5;
	ASSERT_TRUE(CScoreWorker::RandomMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STRNE(m_pRandomMapResult->m_aMap, "");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "");
}

TEST_P(RandomMap, StarsDoesntExist)
{
	m_RandomMapRequest.m_MinStars = 3;
	m_RandomMapRequest.m_MaxStars = 3;
	ASSERT_TRUE(CScoreWorker::RandomMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STREQ(m_pRandomMapResult->m_aMap, "");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "这个服务器上没有找到符合条件的地图！");
}

TEST_P(RandomMap, UnfinishedExists)
{
	m_RandomMapRequest.m_MinStars = -1;
	m_RandomMapRequest.m_MaxStars = -1;
	ASSERT_TRUE(CScoreWorker::RandomUnfinishedMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STREQ(m_pRandomMapResult->m_aMap, "Kobra 3");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "");
}

TEST_P(RandomMap, UnfinishedDoesntExist)
{
	InsertRank();
	ASSERT_TRUE(CScoreWorker::RandomUnfinishedMap(m_pConn, &m_RandomMapRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(m_pRandomMapResult->m_ClientId, 0);
	EXPECT_STREQ(m_pRandomMapResult->m_aMap, "");
	EXPECT_STREQ(m_pRandomMapResult->m_aMessage, "nameless tee 在这个服务器上已经没有未完成的地图了！");
}

TEST_P(Score, LoadTeamReportsCorruptedSaveIdInChinese)
{
	InsertRawSave("Kobra 3", "broken-code", "broken-save", "GER", "not-a-uuid");

	auto pSaveResult = std::make_shared<CScoreSaveResult>(0, "nameless tee", "GER");
	CSqlTeamLoadRequest Request(pSaveResult);
	str_copy(Request.m_aMap, "Kobra 3", sizeof(Request.m_aMap));
	str_copy(Request.m_aCode, "broken-code", sizeof(Request.m_aCode));
	str_copy(Request.m_aRequestingPlayer, "nameless tee", sizeof(Request.m_aRequestingPlayer));

	ASSERT_TRUE(CScoreWorker::LoadTeam(m_pConn, &Request, Write::NORMAL, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_EQ(pSaveResult->m_Status, CScoreSaveResult::LOAD_FAILED);
	EXPECT_STREQ(pSaveResult->m_aMessage, "无法载入存档：存档编号已损坏");
}

TEST_P(Score, ListSavesUsesChineseUnknownPlayerPlaceholder)
{
	str_copy(m_PlayerRequest.m_aMap, "Kobra 3", sizeof(m_PlayerRequest.m_aMap));
	InsertRawSave("Kobra 3", "mystery", "broken-save", "GER", "11111111-1111-1111-1111-111111111111");

	ASSERT_TRUE(CScoreWorker::ListSaves(m_pConn, &m_PlayerRequest, m_aError, sizeof(m_aError))) << m_aError;
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[0], "------- Kobra 3 的存档 -------");
	EXPECT_THAT(m_pPlayerResult->m_Data.m_aaMessages[1], testing::MatchesRegex("\\[未知玩家\\] mystery（.* 前）"));
	EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[2], "---------------------------");
	for(int i = 3; i < CScorePlayerResult::MAX_MESSAGES; i++)
	{
		EXPECT_STREQ(m_pPlayerResult->m_Data.m_aaMessages[i], "");
	}
}

auto g_pSqliteConn = CreateSqliteConnection(":memory:", true);
#if defined(CONF_TEST_MYSQL)
CMysqlConfig gMysqlConfig{
	"ddnet", // database
	"record", // prefix
	"ddnet", // user
	"thebestpassword", // password
	"localhost", // ip
	"", // bindaddr
	3306, // port
	true, // setup
};
auto g_pMysqlConn = CreateMysqlConnection(gMysqlConfig);
#endif

auto g_TestValues{
	testing::Values(
#if defined(CONF_TEST_MYSQL)
		g_pMysqlConn.get(),
#endif
		g_pSqliteConn.get())};

#define INSTANTIATE(SUITE) \
	INSTANTIATE_TEST_SUITE_P(Sql, SUITE, g_TestValues, \
		[](const testing::TestParamInfo<Score::ParamType> &Info) { \
			switch(Info.index) \
			{ \
			case 0: return "SQLite"; \
			case 1: return "MySQL"; \
			default: return "Unknown"; \
			} \
		})

INSTANTIATE(SingleScore);
INSTANTIATE(TeamScore);
INSTANTIATE(BigTeamScore);
INSTANTIATE(MapInfo);
INSTANTIATE(MapVote);
INSTANTIATE(Points);
INSTANTIATE(RandomMap);
INSTANTIATE(Score);
