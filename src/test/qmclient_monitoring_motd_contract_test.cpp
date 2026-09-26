// Ingame MOTD 与文本运行时调度的静态合同。
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <string>

TEST(QmMonitoringMotdContract, MotdParagraphHydratesOnlyThroughBudgetDrain)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("m_Pending"), std::string::npos);
	EXPECT_NE(Header.find("m_PendingFrame"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput)"), std::string::npos);
	EXPECT_EQ(Source.find("BeginSettingsUiFrameScheduler(\"ingame_server_info_snapshot_text\""), std::string::npos);
	EXPECT_EQ(DrainBody.find("BeginSettingsUiFrameScheduler("), std::string::npos);
	EXPECT_NE(DrainBody.find("ParagraphLayoutTokens <= 0"), std::string::npos);
	EXPECT_NE(DrainBody.find("--m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_budget_blocked"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_cache_hit"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_layout_ms"), std::string::npos);
	EXPECT_NE(DrainBody.find("TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameMotdParagraphCache.m_PendingFrame"), std::string::npos);
	EXPECT_NE(Body.find("RequestIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_EQ(Body.find("DrainIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_NE(Source.find("void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)"), std::string::npos);
	EXPECT_NE(Header.find("m_PendingRect"), std::string::npos);
	EXPECT_NE(Header.find("m_BuildByteOffset"), std::string::npos);
	EXPECT_NE(Header.find("m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(Header.find("m_IngameTextAdaptiveBudgetState"), std::string::npos);
	EXPECT_NE(Header.find("SSettingsAdaptiveBudgetOutput m_IngameTextFrameBudget"), std::string::npos);
	EXPECT_NE(Source.find("DrainIngameMotdParagraphCache(m_IngameMotdParagraphCache.m_PendingRect, m_IngameMotdParagraphCache.m_PendingFontSize, AllowCurrentFrame);"), std::string::npos);
	EXPECT_EQ(Source.find("m_IngameTextAdaptiveBudgetState"), std::string::npos);
	EXPECT_EQ(Source.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens = maximum(1, m_IngameTextFrameBudget.m_ParagraphLayoutTokens)"), std::string::npos);
	EXPECT_EQ(Source.find("m_CurrentSettingsUiFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_EQ(Body.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
}

TEST(QmMonitoringMotdContract, MotdParagraphDrainBuildsInChunks)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("static constexpr int INGAME_MOTD_PARAGRAPH_CHUNK_BYTES"), std::string::npos);
	EXPECT_NE(Header.find("INGAME_MOTD_PARAGRAPH_CHUNK_BYTES = 24"), std::string::npos);
	EXPECT_NE(Header.find("CTextCursor m_BuildCursor"), std::string::npos);
	EXPECT_NE(Header.find("int m_BuildByteOffset"), std::string::npos);
	EXPECT_NE(DrainBody.find("INGAME_MOTD_PARAGRAPH_CHUNK_BYTES"), std::string::npos);
	EXPECT_NE(DrainBody.find("str_utf8_isstart"), std::string::npos);
	EXPECT_NE(DrainBody.find("TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameMotdParagraphCache.m_BuildByteOffset += ChunkLength"), std::string::npos);
	EXPECT_NE(DrainBody.find("std::swap(m_MotdTextContainerIndex, m_IngameMotdParagraphCache.m_BuildTextContainerIndex)"), std::string::npos);
	EXPECT_EQ(DrainBody.find("m_MotdTextContainerIndex = m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_IngameMotdParagraphCache.m_PreviousTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
}

TEST(QmMonitoringMotdContract, MotdParagraphMissDoesNotRecreateInRenderPath)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());
	const std::string RequestBody = ExtractSourceFunctionBody(Source, "bool CMenus::RequestIngameMotdParagraphCache(CUIRect Motd, float FontSize)");
	ASSERT_FALSE(RequestBody.empty());

	EXPECT_NE(Header.find("struct SIngameMotdParagraphCache"), std::string::npos);
	EXPECT_NE(Header.find("m_IngameMotdParagraphCache"), std::string::npos);
	EXPECT_NE(Source.find("RequestIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(Body.find("RequestIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_EQ(Source.find("EnsureIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(RequestBody.find("if(!m_IngameMotdParagraphCache.m_Pending ||"), std::string::npos);
	EXPECT_EQ(RequestBody.find("SettingsAdaptiveBudgetStep("), std::string::npos);
	EXPECT_EQ(RequestBody.find("TextRender()->RecreateTextContainer("), std::string::npos);
	EXPECT_EQ(Body.find("TextRender()->RecreateTextContainer("), std::string::npos);
}

TEST(QmMonitoringMotdContract, IngameTextRuntimeDrainsOutsideRenderFunctions)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string RenderBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	const std::string MotdBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string SnapshotDrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameUiSnapshotTextRuntime()");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)");
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(MotdBody.empty());
	ASSERT_FALSE(SnapshotDrainBody.empty());
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("void DrainIngameUiSnapshotTextRuntime();"), std::string::npos);
	EXPECT_NE(Header.find("void DrainIngameUiTextRuntime(bool AllowCurrentFrame = false);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("DrainSnapshotTextContainers()"), std::string::npos);
	EXPECT_EQ(MotdBody.find("DrainIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(SnapshotDrainBody.find("DrainSnapshotTextContainers()"), std::string::npos);
	EXPECT_NE(DrainBody.find("DrainIngameUiSnapshotTextRuntime()"), std::string::npos);
	EXPECT_NE(DrainBody.find("DrainIngameMotdParagraphCache("), std::string::npos);
}

TEST(QmMonitoringMotdContract, TextRuntimeTelemetryFlushesOncePerUiFrame)
{
	const std::string Header = ReadRepoFile("src/engine/textrender.h");
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string CreateBody = ExtractSourceFunctionBody(Text, "bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const std::string UploadBody = ExtractSourceFunctionBody(Text, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(CreateBody.empty());
	ASSERT_FALSE(UploadBody.empty());
	ASSERT_FALSE(OnRenderBody.empty());

	EXPECT_NE(Header.find("FlushQmTextRuntimeBudgetLog"), std::string::npos);
	EXPECT_EQ(CreateBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_EQ(UploadBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("TextRender()->FlushQmTextRuntimeBudgetLog()"), std::string::npos);
}
