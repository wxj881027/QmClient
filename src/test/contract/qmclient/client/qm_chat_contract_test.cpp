// Chat 静态源码合同。运行时行为测试保留在 qm_chat_interactions_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/chat.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <string>

TEST(QmLocalSaveJoinHint, UsesExpiringEchoMessages)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/tclient.cpp");
	const std::string Body = SourceFunctionBody(Source, "void CTClient::MaybeShowLocalSaveJoinHint()");

	EXPECT_NE(Body.find("GameClient()->Echo(aMessage);"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->Echo(PlayersLine.c_str());"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->Echo(CodesLine.c_str());"), std::string::npos);
	EXPECT_EQ(Body.find("GameClient()->Echo(aMessage, true);"), std::string::npos);
	EXPECT_EQ(Body.find("GameClient()->Echo(PlayersLine.c_str(), true);"), std::string::npos);
	EXPECT_EQ(Body.find("GameClient()->Echo(CodesLine.c_str(), true);"), std::string::npos);
}

TEST(QmChatRepeat, TeamMessagesKeepTheirSendChannel)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/tclient.cpp");
	const std::string OnMessage = SourceFunctionBody(TClient, "void CTClient::OnMessage(");
	const std::string RepeatLastMessage = SourceFunctionBody(TClient, "void CTClient::RepeatLastMessage()");

	EXPECT_NE(OnMessage.find("const bool IsRepeatChatChannel = pMsg->m_Team == 0 || pMsg->m_Team == 1;"), std::string::npos);
	EXPECT_NE(OnMessage.find("IsRepeatChatChannel && pMsg->m_pMessage != nullptr"), std::string::npos);
	EXPECT_NE(OnMessage.find("m_LastChatTeam = pMsg->m_Team;"), std::string::npos);
	EXPECT_NE(RepeatLastMessage.find("GameClient()->m_Chat.SendChat(m_LastChatTeam, m_aLastChatMessage);"), std::string::npos);
}

TEST(QmChatInteractions, VisibleTranslationCollectsCandidatesBeforeStartingJobs)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Body = SourceFunctionBody(Source, "bool CChat::TranslateVisibleChatLines()");
	const size_t ScanLoop = Body.find("for(int i = m_BacklogCurLine; i < MAX_LINES; i++)");
	ASSERT_NE(ScanLoop, std::string::npos);
	const size_t CollectIndex = Body.find("aLineIndices[NumLineIndices++] = LineIndex;", ScanLoop);
	ASSERT_NE(CollectIndex, std::string::npos);
	const size_t TranslateLoop = Body.find("for(int i = 0; i < NumLineIndices; i++)", CollectIndex);
	ASSERT_NE(TranslateLoop, std::string::npos);

	EXPECT_EQ(Body.find("GameClient()->m_Translate.Translate", ScanLoop), TranslateLoop + Body.substr(TranslateLoop).find("GameClient()->m_Translate.Translate"));
}

TEST(QmRedPacketAutoClaim, ServerMessageIntegrationUsesMainConnectionBeforeNegativeClientReturn)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/tclient.cpp");
	const std::string OnMessage = SourceFunctionBody(TClient, "void CTClient::OnMessage(");
	const std::string Handler = SourceFunctionBody(TClient, "bool CTClient::TryHandleRedPacketAutoClaim(");

	const size_t HandlerCall = OnMessage.find("TryHandleRedPacketAutoClaim(pMsg);");
	const size_t NegativeClientReturn = OnMessage.find("if(ClientId < 0)");
	ASSERT_NE(HandlerCall, std::string::npos);
	ASSERT_NE(NegativeClientReturn, std::string::npos);
	EXPECT_LT(HandlerCall, NegativeClientReturn);

	EXPECT_NE(Handler.find("pMsg->m_ClientId != -1"), std::string::npos);
	EXPECT_NE(Handler.find("GameClient()->m_aLocalIds[0]"), std::string::npos);
	EXPECT_NE(Handler.find("m_aClients[MainClientId].m_aName"), std::string::npos);
	EXPECT_NE(Handler.find("SendChatOnConn(IClient::CONN_MAIN, 0, Password.c_str(), true, false)"), std::string::npos);
}

TEST(QmRedPacketAutoClaim, DedicatedSendPathAllowsWhitespaceOnlyPassword)
{
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	// 用限定名定位而不带返回类型：该函数在 master 上已由 void 改为 bool，
	// 写死返回类型会让括号匹配落空，测试就会以「找不到实现」的方式假失败。
	const std::string SendChatOnConn = SourceFunctionBody(Chat, "CChat::SendChatOnConn(");

	EXPECT_NE(SendChatOnConn.find("pLine == nullptr || pLine[0] == '\\0'"), std::string::npos);
	EXPECT_NE(SendChatOnConn.find("!AllowWhitespaceOnly && *str_utf8_skip_whitespaces(pLine) == '\\0'"), std::string::npos);
}
