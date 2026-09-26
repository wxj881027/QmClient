// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmStatisticsPersistence, KeepsRemoteIdStatsSeparateFromLocalInGameTime)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/qmclient.h");
	const std::string State = ReadRepoFile("src/game/client/components/qmclient/ddnet_player_stats_state.h");
	const std::string Save = ExtractSourceFunctionBody(QmClient, "bool CQmClient::SaveQmClientStatistics() const");
	const std::string Load = ExtractSourceFunctionBody(QmClient, "void CQmClient::LoadQmClientLocalModeStats()");
	const std::string Select = ExtractSourceFunctionBody(QmClient, "void CQmClient::SelectQmDdnetPlayerStats(const char *pFallbackPlayerName)");
	const std::string RecordFinish = ExtractSourceFunctionBody(QmClient, "void CQmClient::RecordQmClientLocalMapFinish(const char *pGameMode, int Score)");
	const std::string Update = ExtractSourceFunctionBody(QmClient, "void CQmClient::UpdateQmDdnetPlayerStats()");
	const std::string OnUpdate = ExtractSourceFunctionBody(QmClient, "void CQmClient::OnUpdate()");

	EXPECT_NE(Header.find("struct SQmClientDdnetPlayerStats"), std::string::npos);
	EXPECT_NE(Header.find("m_QmDdnetPrimaryPlayerName"), std::string::npos);
	EXPECT_EQ(Header.find("m_QmClientActiveLocalPlayerName"), std::string::npos);
	EXPECT_NE(Save.find("Writer.WriteAttribute(\"primary_player_name\")"), std::string::npos);
	EXPECT_NE(Load.find("JsonObjectField(pRemote, \"primary_player_name\")"), std::string::npos);
	EXPECT_EQ(Save.find("Writer.WriteAttribute(\"player_name\")"), std::string::npos);
	EXPECT_EQ(Load.find("JsonObjectField(pMode, \"player_name\")"), std::string::npos);
	EXPECT_NE(Select.find("m_QmDdnetPrimaryPlayerName"), std::string::npos);
	EXPECT_EQ(Select.find("SPlayerUsage"), std::string::npos);
	EXPECT_EQ(RecordFinish.find("g_Config.m_PlayerName"), std::string::npos);
	EXPECT_NE(QmClient.find("StoreQmDdnetPlayerStats(ParsePlayerName.c_str()"), std::string::npos);
	EXPECT_NE(QmClient.find("void CQmClient::UseCurrentQmDdnetPlayerName()"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus.cpp").find("Use current name"), std::string::npos);
	EXPECT_NE(State.find("if(!CurrentPlayer)\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Update.find("SelectQmDdnetPlayerStats(g_Config.m_PlayerName);"), std::string::npos);
	EXPECT_NE(Update.find("FetchQmDdnetPlayerStats(m_QmDdnetPlayerState.PlayerName().c_str());"), std::string::npos);
	EXPECT_NE(OnUpdate.find("SaveQmClientStatistics()"), std::string::npos);
}

TEST(QmStatisticsPersistence, PersistsCompletedRemoteQueriesOutsideManualRefresh)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/qmclient.cpp");
	const std::string OnUpdate = ExtractSourceFunctionBody(QmClient, "void CQmClient::OnUpdate()");
	ASSERT_FALSE(OnUpdate.empty());

	EXPECT_NE(QmClient.find("StoreQmDdnetPlayerStats(ParsePlayerName.c_str()"), std::string::npos);
	EXPECT_NE(QmClient.find("SaveQmClientStatistics();"), std::string::npos);
	EXPECT_NE(OnUpdate.find("PersistentCacheDirty()"), std::string::npos);
	EXPECT_NE(OnUpdate.find("ClearPersistentCacheDirty()"), std::string::npos);
	EXPECT_NE(OnUpdate.find("!m_QmStatisticsFileInvalid"), std::string::npos);
	EXPECT_NE(OnUpdate.find("m_QmStatisticsNextSaveRetryTick"), std::string::npos);
}

// 这些路径挂在完整客户端生命周期上，无法用稳定运行时接口观察，
// 只能按本文件既有的源码契约方式钉住关键接线。
TEST(QmStatisticsPersistence, SavesLocalStatsWithinSessionWithBoundedEntries)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/qmclient.h");
	const std::string GameClientSource = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string OnUpdate = ExtractSourceFunctionBody(QmClient, "void CQmClient::OnUpdate()");
	const std::string RecordFinish = ExtractSourceFunctionBody(QmClient, "void CQmClient::RecordQmClientLocalMapFinish(const char *pGameMode, int Score)");
	const std::string Accumulate = ExtractSourceFunctionBody(QmClient, "void CQmClient::AccumulateQmClientLocalModePlaytime(int64_t Now)");
	const std::string Load = ExtractSourceFunctionBody(QmClient, "void CQmClient::LoadQmClientLocalModeStats()");
	ASSERT_FALSE(OnUpdate.empty());
	ASSERT_FALSE(RecordFinish.empty());
	ASSERT_FALSE(Accumulate.empty());

	// 本地统计置脏 + 节流落盘：Record/Accumulate 置脏，OnUpdate 消费。
	EXPECT_NE(RecordFinish.find("m_QmStatisticsLocalStatsDirty = true"), std::string::npos);
	EXPECT_NE(Accumulate.find("m_QmStatisticsLocalStatsDirty = true"), std::string::npos);
	EXPECT_NE(OnUpdate.find("m_QmStatisticsLocalStatsDirty"), std::string::npos);
	EXPECT_NE(OnUpdate.find("m_QmStatisticsLocalSaveDueTick"), std::string::npos);

	// 条目上限同时约束运行时新增与文件加载。
	EXPECT_NE(QmClient.find("QMCLIENT_MAX_LOCAL_MODE_STATS = 256"), std::string::npos);
	EXPECT_NE(RecordFinish.find("QMCLIENT_MAX_LOCAL_MODE_STATS"), std::string::npos);
	EXPECT_NE(Accumulate.find("QMCLIENT_MAX_LOCAL_MODE_STATS"), std::string::npos);
	EXPECT_NE(Load.find("QMCLIENT_MAX_LOCAL_MODE_STATS"), std::string::npos);

	// 完成检测迁移到消息通道（0.7 RaceFinish 事件 + 0.6 finished-in 聊天广播），
	// 旧 GAMEOVER 路径必须从 gameclient.cpp 移除，避免残留死挂点。
	EXPECT_NE(QmClient.find("NETMSGTYPE_SV_RACEFINISH"), std::string::npos);
	EXPECT_NE(QmClient.find("TimeFromFinishMessage"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("RecordQmClientLocalMapFinish"), std::string::npos);

	// DDNet 档案查询成功一次后，行内不再永久显示加载中。
	EXPECT_NE(QmClient.find("m_QmDdnetStatsSucceededOnce = true"), std::string::npos);
	EXPECT_NE(Header.find("QmDdnetStatsSucceededOnce"), std::string::npos);
	EXPECT_NE(Header.find("QmStatisticsFileInvalid"), std::string::npos);
}

TEST(QmStatisticsPersistence, UsesSingleUnversionedStatisticsDocument)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/qmclient.cpp");
	const std::string SaveFunction = ExtractSourceFunctionBody(Source, "bool CQmClient::SaveQmClientStatistics() const");
	const std::string LoadFunction = ExtractSourceFunctionBody(Source, "void CQmClient::LoadQmClientLocalModeStats()");
	ASSERT_FALSE(SaveFunction.empty());
	ASSERT_FALSE(LoadFunction.empty());

	EXPECT_NE(SaveFunction.find("WriteAttribute(\"local\")"), std::string::npos);
	EXPECT_NE(SaveFunction.find("WriteAttribute(\"remote\")"), std::string::npos);
	EXPECT_EQ(SaveFunction.find("WriteAttribute(\"version\")"), std::string::npos);
	EXPECT_EQ(SaveFunction.find("WriteAttribute(\"ddstats_import\")"), std::string::npos);
	EXPECT_NE(LoadFunction.find("JsonObjectField(pRoot, \"local\")"), std::string::npos);
	EXPECT_EQ(LoadFunction.find("JsonObjectField(pRoot, \"local_modes\")"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_QmStatisticsFileExists)"), std::string::npos);
	EXPECT_NE(Source.find("RefreshQmClientStatistics()"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus.cpp").find("Sync remote stats"), std::string::npos);
	EXPECT_NE(Source.find("Writer.Finish()"), std::string::npos);
	EXPECT_NE(Source.find("m_QmDdnetPlayerState"), std::string::npos);
	EXPECT_NE(Source.find("m_QmStatisticsFileInvalid"), std::string::npos);
	EXPECT_NE(Source.find("refusing to overwrite invalid statistics file"), std::string::npos);
	EXPECT_NE(Source.find("pValue->u.dbl >= static_cast<double>(std::numeric_limits<int64_t>::max())"), std::string::npos);
}

TEST(QmMonitoringPersistenceContract, AudioPackDirectoryOpensWritableSaveFolder)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Storage()->GetCompletePath(IStorage::TYPE_SAVE, \"audio\", aBuf, sizeof(aBuf));"), std::string::npos);
	EXPECT_EQ(Body.find("Storage()->GetCompletePath(IStorage::TYPE_ALL, \"audio\""), std::string::npos);
}
