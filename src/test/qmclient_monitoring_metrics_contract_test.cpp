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

TEST(QmMonitoringMetricsContract, ManualPingTimeoutIsCheckedBeforeKcpEarlyContinue)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	ASSERT_FALSE(AutomaticPing.empty());
	const size_t Timeout = AutomaticPing.find("m_ManualPingProbe.HandleTimeout");
	const size_t KcpBranch = AutomaticPing.find("m_aNetClient[Conn].IsKcpActive()");
	ASSERT_NE(Timeout, std::string::npos);
	ASSERT_NE(KcpBranch, std::string::npos);
	EXPECT_LT(Timeout, KcpBranch);
}

TEST(QmMonitoringMetricsContract, MenuUiPerfFacadeKeepsOneStableSchemaAndDisabledFastPath)
{
	const std::string Header = ReadRepoFile("src/game/client/QmUi/QmUiPerf.h");
	const std::string Source = ReadRepoFile("src/game/client/QmUi/QmUiPerf.cpp");
	const std::string Cmake = ReadRepoFile("CMakeLists.txt");

	ASSERT_FALSE(Header.empty());
	ASSERT_FALSE(Source.empty());
	EXPECT_NE(Header.find("struct SQmMenuUiFramePerf"), std::string::npos);
	EXPECT_NE(Header.find("void QmLogMenuUiFramePerf(const SQmMenuUiFramePerf &Frame, const IClient *pClient);"), std::string::npos);
	EXPECT_LT(Source.find("if(!QmPerfEnabled())"), Source.find("str_format("));
	EXPECT_NE(Source.find("QmPerfLogPayload(\"perf/menu-ui\", pPayload, pClient);"), std::string::npos);

	const std::array<const char *, 16> apFields = {
		"event=menu_ui_frame", "page=%s", "operation=%s", "frame=%", "items_total=%d", "items_visible=%d",
		"items_processed=%d", "items_skipped=%d", "ui_ms=%.3f", "layout_ms=%.3f", "text_ms=%.3f", "heap_allocs=%d",
		"cache_hits=%d", "cache_misses=%d", "cache_evictions=%d", "source=qm_ui_perf"};
	for(const char *pField : apFields)
		EXPECT_NE(Source.find(pField), std::string::npos) << pField;

	EXPECT_NE(Cmake.find("QmUi/QmUiPerf.cpp"), std::string::npos);
	EXPECT_NE(Cmake.find("QmUi/QmUiPerf.h"), std::string::npos);
}

TEST(QmMonitoringMetricsContract, FrameSchedulerServiceExposesConsumerScopedInterface)
{
	const std::string Header = ReadRepoFile("src/game/client/frame_scheduler.h");
	const std::string Source = ReadRepoFile("src/game/client/frame_scheduler.cpp");
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string Cmake = ReadRepoFile("CMakeLists.txt");

	ASSERT_FALSE(Header.empty());
	ASSERT_FALSE(Source.empty());

	EXPECT_NE(Header.find("class IFrameScheduler : public IInterface"), std::string::npos);
	EXPECT_NE(Header.find("MACRO_INTERFACE(\"frame_scheduler\")"), std::string::npos);
	EXPECT_NE(Header.find("enum class EFrameSchedulerConsumer"), std::string::npos);
	EXPECT_NE(Header.find("SettingsText"), std::string::npos);
	EXPECT_NE(Header.find("IngameText"), std::string::npos);
	EXPECT_NE(Header.find("Assets"), std::string::npos);
	EXPECT_NE(Header.find("DemoBrowser"), std::string::npos);
	EXPECT_NE(Header.find("IngameServerInfo"), std::string::npos);
	EXPECT_NE(Header.find("Count"), std::string::npos);
	EXPECT_NE(Header.find("ComputeBudget"), std::string::npos);
	EXPECT_NE(Header.find("Reset()"), std::string::npos);
	EXPECT_NE(Header.find("BeginFrame"), std::string::npos);
	EXPECT_NE(Header.find("EndFrame"), std::string::npos);
	EXPECT_NE(Header.find("CreateFrameScheduler"), std::string::npos);

	EXPECT_NE(Source.find("SettingsAdaptiveBudgetStep(Input, m_aState"), std::string::npos);
	EXPECT_NE(Source.find("IFrameScheduler *CreateFrameScheduler()"), std::string::npos);

	EXPECT_NE(Client.find("#include <game/client/frame_scheduler.h>"), std::string::npos);
	EXPECT_NE(Client.find("IFrameScheduler *pFrameScheduler = CreateFrameScheduler();"), std::string::npos);
	EXPECT_NE(Client.find("pKernel->RegisterInterface(pFrameScheduler)"), std::string::npos);

	// CMakeLists.txt 必须显式列出 frame_scheduler.cpp 才会被纳入 GAME_CLIENT 目标；
	// set_src(GAME_CLIENT GLOB_RECURSE ...) 仅用 GLOB 校验与磁盘文件对齐，
	// 真正参与编译的是 ${ARGN} 显式列表（见 CMakeLists.txt set_glob 函数）。
	EXPECT_NE(Cmake.find("frame_scheduler.cpp"), std::string::npos);

	EXPECT_NE(Header.find("#include <game/client/components/settings_resource_jobs.h>"), std::string::npos);
}

TEST(QmMonitoringMetricsContract, LegacyPingPathRemainsPresent)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	ASSERT_FALSE(AutomaticPing.empty());
	EXPECT_NE(AutomaticPing.find("BeginLegacy"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
}

TEST(QmMonitoringMetricsContract, AutomaticAndManualPingPathsCoordinateWithExplicitLegacySharing)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	const std::string PingMs = ExtractSourceFunctionBody(Client, "float CClient::PingMs() const");
	const std::string ManualPing = ExtractSourceFunctionBody(Client, "void CClient::Con_Ping(IConsole::IResult *pResult, void *pUserData)");
	const std::string ProcessPacket = ExtractSourceFunctionBody(Client, "void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)");
	const std::string PingReply = ExtractSourceBlock(ProcessPacket, "else if(Msg == NETMSG_PING_REPLY)", "else if(Msg == NETMSG_INPUTTIMING)");

	ASSERT_FALSE(AutomaticPing.empty());
	ASSERT_FALSE(PingMs.empty());
	ASSERT_FALSE(ManualPing.empty());
	ASSERT_FALSE(PingReply.empty());
	EXPECT_NE(AutomaticPing.find("NETMSG_PINGEX"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_EQ(PingMs.find("!m_ServerCapabilities.m_PingEx"), std::string::npos);
	EXPECT_NE(ManualPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_ManualPingProbe.Begin"), std::string::npos);
	EXPECT_NE(PingReply.find("m_ManualPingProbe.HandlePong"), std::string::npos);
	EXPECT_NE(PingReply.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_EQ(Client.find("m_aGamePingIgnoreNextReply"), std::string::npos);
	EXPECT_EQ(Client.find("m_PingStartTime"), std::string::npos);
}
