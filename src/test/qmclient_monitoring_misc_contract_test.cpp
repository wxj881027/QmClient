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

TEST(QmMonitoringMiscContract, TClientPrewarmDoesNotRunUnboundedInVisibleTargetFrame)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool TClientVisibleTargetFrame = !ReadOnly"), std::string::npos);
	EXPECT_NE(Body.find("SetProgressiveEnabled(TClientVisibleTargetFrame)"), std::string::npos);
	EXPECT_NE(Body.find("SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?"), std::string::npos);
	EXPECT_NE(Body.find("tclient_settings_left_prewarm_budgeted"), std::string::npos);
	EXPECT_NE(Body.find("tclient_settings_right_prewarm_budgeted"), std::string::npos);
	EXPECT_EQ(Body.find("VisualFontLoader.SetProgressiveEnabled(false);"), std::string::npos);
	EXPECT_EQ(Body.find("RightSectionLoader.SetProgressiveEnabled(false);"), std::string::npos);
}

TEST(QmMonitoringMiscContract, DefaultGateRunsFullAutomatedTests)
{
	const std::string Gate = ReadRepoFile("qmclient_scripts/gate/check_gate.py");
	const std::string ScriptsOverview = ReadRepoFile("qmclient_scripts/scripts_overview.md");
	ASSERT_FALSE(Gate.empty());

	const size_t DefaultMode = Gate.find("\"default\": {");
	ASSERT_NE(DefaultMode, std::string::npos);
	const size_t FullMode = Gate.find("\"full\": {", DefaultMode);
	ASSERT_NE(FullMode, std::string::npos);
	const std::string DefaultSpec = Gate.substr(DefaultMode, FullMode - DefaultMode);

	// The default gate is the normal pre-submit gate, so it must run the full
	// automated test set. Full mode is reserved for extra heavyweight/noisy
	// checks, not for merely getting Rust tests.
	EXPECT_NE(DefaultSpec.find("\"tests\": {\"cxx\": True, \"rust\": True, \"all\": False}"), std::string::npos);
	EXPECT_EQ(DefaultSpec.find("\"strict_build\""), std::string::npos);
	EXPECT_EQ(DefaultSpec.find("\"dilate\""), std::string::npos);
	EXPECT_NE(DefaultSpec.find("C++ 全量测试和 Rust 全量测试"), std::string::npos);
	EXPECT_NE(Gate.substr(FullMode).find("\"strict_build\""), std::string::npos);
	EXPECT_NE(Gate.substr(FullMode).find("\"dilate\""), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("C++ 全量测试和 Rust 全量测试"), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("严格构建与静态分析只属于 full gate"), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("不作为“全量测试”的默认入口"), std::string::npos);
}

TEST(QmMonitoringMiscContract, TimeoutDisconnectReconnectAdvertisesDDNetVersionBeforeSystemInfo)
{
	const std::string Source = ReadRepoFile("src/engine/client/client.cpp");
	const std::string SendInfoBody = ExtractSourceFunctionBody(Source, "void CClient::SendInfo(int Conn)");
	ASSERT_FALSE(SendInfoBody.empty());

	const size_t LegacyVersion = SendInfoBody.find("CMsgPacker MsgLegacyVersion(NETMSGTYPE_CL_ISDDNETLEGACY, false);");
	const size_t TClientInfo = SendInfoBody.find("SendTClientInfo(Conn);");
	const size_t ClientVersion = SendInfoBody.find("CMsgPacker MsgVer(NETMSG_CLIENTVER, true);");
	const size_t SystemInfo = SendInfoBody.find("CMsgPacker Msg(NETMSG_INFO, true);");
	ASSERT_NE(LegacyVersion, std::string::npos);
	ASSERT_NE(TClientInfo, std::string::npos);
	ASSERT_NE(ClientVersion, std::string::npos);
	ASSERT_NE(SystemInfo, std::string::npos);

	EXPECT_LT(LegacyVersion, TClientInfo);
	EXPECT_LT(LegacyVersion, ClientVersion);
	EXPECT_LT(LegacyVersion, SystemInfo);
	EXPECT_NE(SendInfoBody.find("MsgLegacyVersion.AddInt(GameClient()->DDNetVersion());"), std::string::npos);
	EXPECT_NE(SendInfoBody.find("SendMsg(Conn, &MsgLegacyVersion, MSGFLAG_VITAL);"), std::string::npos);
}

TEST(QmMonitoringMiscContract, DemoBrowserFetchInfoUsesBoundedProgress)
{
	const std::string MenusDemo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string ButtonsBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)");
	const std::string SortListBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string DetailsBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserDetails(CUIRect DetailsView)");
	const std::string FetchAllBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::FetchAllHeaders()");
	const std::string EnsureDatesBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::EnsureAllDemoDates()");
	ASSERT_FALSE(ButtonsBody.empty());
	ASSERT_FALSE(SortListBody.empty());
	ASSERT_FALSE(DetailsBody.empty());
	ASSERT_FALSE(FetchAllBody.empty());
	ASSERT_FALSE(EnsureDatesBody.empty());

	EXPECT_NE(ButtonsBody.find("g_Config.m_BrDemoFetchInfo"), std::string::npos);
	EXPECT_EQ(ButtonsBody.find("FetchAllHeaders();"), std::string::npos);
	EXPECT_EQ(SortListBody.find("EnsureAllDemoDates();"), std::string::npos);
	EXPECT_EQ(FetchAllBody.find("for(auto &Item : m_vDemos)"), std::string::npos);
	EXPECT_EQ(EnsureDatesBody.find("for(auto &Item : m_vDemos)"), std::string::npos);
	EXPECT_NE(FetchAllBody.find("AdvanceDemoBrowserMetadata(2, g_Config.m_BrDemoSort == SORT_DATE ? 4 : 0, \"fetch_info\");"), std::string::npos);
	EXPECT_NE(EnsureDatesBody.find("AdvanceDemoBrowserMetadata(0, maximum(1, AdaptiveBudget.m_DemoMetadataTokens), \"ensure_dates\");"), std::string::npos);
	EXPECT_EQ(SortListBody.find("if(EnsureDemoDate(*pItem))"), std::string::npos);
	EXPECT_NE(SortListBody.find("if(pItem->m_DateLoaded && pItem->m_DateValid)"), std::string::npos);
	EXPECT_EQ(DetailsBody.find("!FetchHeader(*pItem)"), std::string::npos);
	EXPECT_NE(DetailsBody.find("!pItem->m_InfosLoaded"), std::string::npos);
	EXPECT_NE(Header.find("size_t m_DemoHeaderFetchCursor"), std::string::npos);
	EXPECT_NE(Header.find("size_t m_DemoDateFetchCursor"), std::string::npos);
	EXPECT_NE(Header.find("bool m_DemoHeaderFetchComplete"), std::string::npos);
	EXPECT_NE(Header.find("bool m_DemoDateFetchComplete"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_header_fetch"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_date_fetch"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_preview_load"), std::string::npos);
	EXPECT_NE(MenusDemo.find("metadata_remaining=%d"), std::string::npos);
}

TEST(QmMonitoringMiscContract, SnapshotCountRequiresValidatedSnapshotStorageInsert)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string Body = ExtractSourceFunctionBody(Client, "void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)");
	ASSERT_FALSE(Body.empty());

	const size_t CrcValidation = Body.find("TmpBuffer3.AsSnapshot()->Crc() != Crc");
	const size_t AltSnapshotValidation = Body.find("if(AltSnapSize < 0)");
	const size_t PartCount = Body.find("m_aSnapshotStats[Conn].m_PartCount++");
	const size_t StorageInsert = Body.find("m_aSnapshotStorage[Conn].Add(");
	const size_t SnapshotCount = Body.find("SnapshotStats.m_SnapshotCount++");
	ASSERT_NE(CrcValidation, std::string::npos);
	ASSERT_NE(AltSnapshotValidation, std::string::npos);
	ASSERT_NE(PartCount, std::string::npos);
	ASSERT_NE(StorageInsert, std::string::npos);
	ASSERT_NE(SnapshotCount, std::string::npos);

	EXPECT_LT(Body.find("if(Unpacker.Error() || NumParts < 1"), PartCount);
	EXPECT_LT(PartCount, Body.find("// Check m_aAckGameTick"));
	EXPECT_LT(CrcValidation, StorageInsert);
	EXPECT_LT(AltSnapshotValidation, StorageInsert);
	EXPECT_LT(StorageInsert, SnapshotCount);
}

TEST(QmMonitoringMiscContract, DemoBrowserStartupDoesNotSynchronouslyFetchAllHeaders)
{
	const std::string MenusDemo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string RenderListBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string PopulateBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::DemolistPopulate()");
	ASSERT_FALSE(RenderListBody.empty());
	ASSERT_FALSE(PopulateBody.empty());

	EXPECT_NE(RenderListBody.find("DemolistPopulate();"), std::string::npos);
	EXPECT_NE(RenderListBody.find("DemolistOnUpdate(true);"), std::string::npos);
	EXPECT_EQ(PopulateBody.find("FetchAllHeaders();"), std::string::npos);
	EXPECT_EQ(PopulateBody.find("EnsureAllDemoDates();"), std::string::npos);
	EXPECT_NE(MenusDemo.find("AdvanceDemoBrowserMetadata("), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_startup"), std::string::npos);
}
