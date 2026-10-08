// Chat 静态源码合同。运行时行为测试保留在 qm_chat_interactions_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/chat.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <string>

TEST(QmChatSecurity, SensitiveLoginCommandsAreNotPersisted)
{
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string OnMessage = SourceFunctionBody(Chat, "void CChat::OnMessage(int MsgType, void *pRawMsg, int SourceConnection)");
	const std::string SendChatQueued = SourceFunctionBody(Chat, "void CChat::SendChatQueued(int Team");

	EXPECT_NE(OnMessage.find("GameClient()->IsLocalClientId(pMsg->m_ClientId)"), std::string::npos);
	EXPECT_NE(OnMessage.find("IsSensitiveChatCommand(pMsg->m_pMessage)"), std::string::npos);
	const size_t SensitiveCheck = SendChatQueued.find("if(IsSensitiveChatCommand(pLine))");
	const size_t SendNow = SendChatQueued.find("SendChat(Team, pLine);");
	const size_t TranslateCheck = SendChatQueued.find("ShouldAutoTranslateOutgoing(pLine)");
	const size_t PendingQueue = SendChatQueued.find("m_PendingChatCounter");
	ASSERT_NE(SensitiveCheck, std::string::npos);
	ASSERT_NE(SendNow, std::string::npos);
	ASSERT_NE(TranslateCheck, std::string::npos);
	ASSERT_NE(PendingQueue, std::string::npos);
	EXPECT_LT(SensitiveCheck, SendNow);
	EXPECT_LT(SensitiveCheck, TranslateCheck);
	EXPECT_LT(SendNow, PendingQueue);
}

TEST(QmChatMessageMerge, HighlightedMessagesAreNotMerged)
{
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string AddLine = SourceFunctionBody(Chat, "void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional");

	const size_t HighlightCheck = AddLine.find("LineShouldHighlight(pLine");
	const size_t MergeCheck = AddLine.find("CanMergePlayerMessages(");
	ASSERT_NE(HighlightCheck, std::string::npos);
	ASSERT_NE(MergeCheck, std::string::npos);
	EXPECT_LT(HighlightCheck, MergeCheck);
	EXPECT_NE(AddLine.find("!Highlighted &&"), std::string::npos);
}

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

TEST(QmWindowModesContract, WindowedFullscreenIsBorderedAndNonResizable)
{
	const std::string Backend = ReadTestSourceFile("src/engine/client/backend_sdl.cpp");
	const std::string SetWindowParams = SourceFunctionBody(Backend, "void CGraphicsBackend_SDL_GL::SetWindowParams(");
	const size_t WindowedFullscreenStart = SetWindowParams.find("else // Windowed fullscreen");
	const size_t WindowedStart = SetWindowParams.find("else // Windowed", WindowedFullscreenStart + 1);
	ASSERT_NE(WindowedFullscreenStart, std::string::npos);
	ASSERT_NE(WindowedStart, std::string::npos);
	const std::string WindowedFullscreen = SetWindowParams.substr(WindowedFullscreenStart, WindowedStart - WindowedFullscreenStart);

	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowFullscreen(m_pWindow, 0);"), std::string::npos);
	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowBordered(m_pWindow, SDL_TRUE);"), std::string::npos);
	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowResizable(m_pWindow, SDL_FALSE);"), std::string::npos);
}

TEST(QmFastPracticeCommands, TeleportDefaultsToAimingOrSpectatingPosition)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const size_t CommandBlock = Source.find("if(Cmd == \"tp\" || Cmd == \"teleport\" || Cmd == \"tc\" || Cmd == \"telecursor\")");
	ASSERT_NE(CommandBlock, std::string::npos);
	const size_t TargetLine = Source.find("vec2 Target = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];", CommandBlock);
	ASSERT_NE(TargetLine, std::string::npos);
	EXPECT_LT(CommandBlock, TargetLine);
	EXPECT_EQ(Source.find("PracticeTeleCursorTarget", CommandBlock), std::string::npos);
}

TEST(QmFastPracticeCommands, ResetRecapturesAnchorFromSnapshotAndServerInputIsNeutral)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string ResetBody = SourceFunctionBody(Source, "void CFastPractice::ResetPracticeToAnchor()");
	ASSERT_NE(ResetBody.find("m_MainAnchor.m_Valid"), std::string::npos);
	// 每次 /r 重新捕获锚点（与远程一致），不再是"沿用开启练习时的锚点"。
	EXPECT_NE(ResetBody.find("CaptureAnchorsFromSnapshot();"), std::string::npos);
	// 必须先初始化练习世界再捕获，否则捕获到的快照与练习世界不同步。
	EXPECT_LT(ResetBody.find("InitPracticeWorld()"), ResetBody.find("CaptureAnchorsFromSnapshot();"));
	EXPECT_LT(ResetBody.find("CaptureAnchorsFromSnapshot();"), ResetBody.find("m_MainAnchor.m_Valid"));

	const std::string LockBody = SourceFunctionBody(Source, "void CFastPractice::CaptureServerLockedInputs()");
	EXPECT_NE(LockBody.find("Input.m_Direction = 0;"), std::string::npos);
	EXPECT_NE(LockBody.find("Input.m_Jump = 0;"), std::string::npos);
	// 钩子被有意保留（避免挂墙本体松钩掉落），只释放移动、跳跃与开火。
	EXPECT_EQ(LockBody.find("Input.m_Hook = 0;"), std::string::npos);
}

TEST(QmFastPracticeCommands, LateDummyAttachKeepsSessionAnchorAndSpectatorInputLocked)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string AttachBody = SourceFunctionBody(Source, "bool CFastPractice::TryAttachDummyFromSnapshot()");
	const std::string SendBody = SourceFunctionBody(Source, "void CFastPractice::PrepareInputForSend(");

	EXPECT_NE(AttachBody.find("CaptureAnchorFromSnapshot(m_EnableDummyClientId, m_DummyAnchor)"), std::string::npos);
	EXPECT_EQ(AttachBody.find("CaptureAnchorsFromSnapshot()"), std::string::npos);
	EXPECT_NE(SendBody.find("m_aHasServerLockedInputs[Slot]"), std::string::npos);
	EXPECT_EQ(SendBody.find("m_Snap.m_SpecInfo.m_Active"), std::string::npos);
}

TEST(QmFastPracticeCommands, PredictionLoopsReuseNormalPreInputAndFreezeSemantics)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string OverrideBody = SourceFunctionBody(Source, "bool CFastPractice::OverridePredict()");
	const std::string VisualBody = SourceFunctionBody(Source, "int CFastPractice::ApplyVisualFastInputPrediction(");

	EXPECT_NE(OverrideBody.find("GameClient()->ApplyPreInputs(Tick, true, GameClient()->m_PredictedWorld);"), std::string::npos);
	EXPECT_NE(OverrideBody.find("GameClient()->ApplyPreInputs(Tick, false, GameClient()->m_PredictedWorld);"), std::string::npos);
	EXPECT_NE(OverrideBody.find("g_Config.m_ClPredictFreeze == 2"), std::string::npos);
	EXPECT_NE(VisualBody.find("GameClient()->ApplyPreInputs(Tick, true, VisualWorld);"), std::string::npos);
	EXPECT_NE(VisualBody.find("GameClient()->ApplyPreInputs(Tick, false, VisualWorld);"), std::string::npos);
	EXPECT_NE(VisualBody.find("VisualWorld.m_WorldConfig.m_PredictEvents = false;"), std::string::npos);
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

TEST(QmChatInteractions, ChatLineMenuKeepsSpectateAction)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/chat.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");

	EXPECT_NE(Header.find("CButtonContainer m_SpectateButton;"), std::string::npos);
	EXPECT_NE(Header.find("void SpectateChatLine(const CChatLinePopupContext &Context);"), std::string::npos);
	EXPECT_NE(Source.find("DoEntry(&pPopupContext->m_SpectateButton, EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->m_Spectator.Spectate(Context.m_ClientId);"), std::string::npos);
	EXPECT_NE(Source.find("Console()->ExecuteLine(aCommand);"), std::string::npos);
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
	const std::string SendChatOnConn = SourceFunctionBody(Chat, "void CChat::SendChatOnConn(");

	EXPECT_NE(SendChatOnConn.find("pLine == nullptr || pLine[0] == '\\0'"), std::string::npos);
	EXPECT_NE(SendChatOnConn.find("!AllowWhitespaceOnly && *str_utf8_skip_whitespaces(pLine) == '\\0'"), std::string::npos);
}

TEST(QmChatInteractions, ChatLineMenuUsesContentBoundsAndKeepsTargetHighlighted)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/chat.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string OnRender = SourceFunctionBody(Source, "void CChat::OnRender()");
	const std::string OpenMenu = SourceFunctionBody(Source, "void CChat::OpenChatLineMenu(");

	EXPECT_NE(Header.find("float m_ContentWidth"), std::string::npos);
	EXPECT_NE(Header.find("int m_LineIndex = -1"), std::string::npos);
	EXPECT_NE(OnRender.find("Line.m_ContentWidth"), std::string::npos);
	EXPECT_NE(OnRender.find("const float RenderedContentWidth = Line.m_ContentWidth * RenderScale;"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool MouseInsideLine = IsChatLineHit(RenderedTextRect, MousePos);"), std::string::npos);
	EXPECT_NE(OnRender.find("ChatLineMenuOpen && m_ChatLinePopupContext.m_LineIndex == LineIndex"), std::string::npos);
	EXPECT_NE(OnRender.find("const ColorRGBA SelectionColor"), std::string::npos);
	EXPECT_NE(OnRender.find("Graphics()->DrawRect(RenderedTextRect.x"), std::string::npos);
	EXPECT_NE(OpenMenu.find("m_ChatLinePopupContext.m_LineIndex = GetLineIndex(&Line);"), std::string::npos);
	EXPECT_NE(OpenMenu.find("UiMousePos.x, UiMousePos.y"), std::string::npos);
	EXPECT_EQ(OpenMenu.find("ChatToUiScale"), std::string::npos);
	EXPECT_NE(OnRender.find("OpenChatLineMenu(*pMenuLine, GetUiMousePos());"), std::string::npos);
}
