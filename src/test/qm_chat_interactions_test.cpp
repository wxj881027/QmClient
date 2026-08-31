// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/qmclient/red_packet_auto_claim.h>
#include <game/client/components/tclient/fast_practice.h>
#include <game/client/components/tclient/warlist.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <iterator>
#include <string>

namespace
{
	int64_t TestTicks(float Seconds)
	{
		return (int64_t)(Seconds * time_freq());
	}

	std::string SourceFunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		EXPECT_NE(FunctionStart, std::string::npos) << Signature;
		const size_t BodyStart = Source.find("{", FunctionStart);
		EXPECT_NE(BodyStart, std::string::npos) << Signature;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Signature;
		return {};
	}
}

TEST(QmChatPresentation, NewLineEntersThenBecomesVisible)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(10.0f);

	CChat::BeginLinePresentation(Presentation, Start, false);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::ENTERING);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_LT(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetY, 0.0f);
	EXPECT_LT(Presentation.m_RenderAlpha, 1.0f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(0.31f), 0.10f, false, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_NEAR(Presentation.m_RenderOffsetX, 0.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderOffsetY, 0.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);
}

TEST(QmWarListEnemyChat, OnlyBuiltInEnemyGroupMatches)
{
	CWarDataCache WarData;
	ASSERT_GE(WarData.m_WarGroupMatches.size(), 3u);

	WarData.m_WarGroupMatches[2] = true;
	EXPECT_FALSE(CWarList::MatchesEnemyGroup(WarData));

	WarData.m_WarGroupMatches[2] = false;
	WarData.m_WarGroupMatches[1] = true;
	EXPECT_TRUE(CWarList::MatchesEnemyGroup(WarData));
}

TEST(QmWarListEnemyChat, ShortGroupDataDoesNotMatchEnemy)
{
	CWarDataCache WarData;
	WarData.m_WarGroupMatches.resize(1);
	EXPECT_FALSE(CWarList::MatchesEnemyGroup(WarData));
}

TEST(QmDummySyncChatCommand, MatchesOnlySupportedCommands)
{
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand(nullptr));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/team 2"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/TEAM 2"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/TeAm 63"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/vote particle"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/VOTE PARTICLE"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/VoTe PaRtIcLe"));

	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/team"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/team "));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/teamwork 2"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/vote particles"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/vote particle on"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("particle"));
}

TEST(QmChatMessageMerge, EligibilityUsesExactTextSlidingWindowAndPlayerMessagesOnly)
{
	const int64_t Start = TestTicks(10.0f);

	EXPECT_TRUE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 1, "same", Start + TestTicks(2.0f)));
	EXPECT_TRUE(CChat::CanMergePlayerMessages(2, 1, "same", Start, 2, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 1, "same", Start + TestTicks(2.01f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 1, "Same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(-1, 0, "same", Start, 7, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, -1, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, TEAM_WHISPER_RECV, "same", Start, 7, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, TEAM_WHISPER_SEND, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Start - 1));
}

TEST(QmChatMessageMerge, ChatAndConsoleKeepStructuredMergedAuthors)
{
	const std::string ChatHeader = ReadTestSourceFile("src/game/client/components/chat.h");
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string ConsoleHeader = ReadTestSourceFile("src/game/client/components/console.h");
	const std::string Console = ReadTestSourceFile("src/game/client/components/console.cpp");
	const std::string Translate = ReadTestSourceFile("src/game/client/components/qmclient/translate/translate.cpp");
	const std::string AddLine = SourceFunctionBody(Chat, "void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional");

	EXPECT_NE(ChatHeader.find("struct SMergedAuthor"), std::string::npos);
	EXPECT_NE(ChatHeader.find("std::vector<SMergedAuthor> m_vMergedAuthors"), std::string::npos);
	EXPECT_NE(AddLine.find("g_Config.m_QmMessageMerge"), std::string::npos);
	EXPECT_NE(AddLine.find("CanMergePlayerMessages("), std::string::npos);
	EXPECT_NE(AddLine.find("PreviousLine.m_Team = false;"), std::string::npos);
	EXPECT_NE(AddLine.find("PreviousLine.m_TeamNumber = 0;"), std::string::npos);
	EXPECT_EQ(AddLine.find("PreviousLine.m_ClientId == ClientId"), std::string::npos);
	EXPECT_NE(Chat.find("if(Author.m_ClientId == ClientId)"), std::string::npos);
	EXPECT_NE(Chat.find("Author.m_NameColor = PlayerNameColor(ClientId, NameColor, false);"), std::string::npos);
	EXPECT_NE(Chat.find("\" [%d]: \", Line.m_TimesRepeated + 1"), std::string::npos);
	EXPECT_NE(Chat.find("FlushPendingConsoleLine"), std::string::npos);
	EXPECT_NE(Chat.find("GameClient()->m_GameConsole.PrintLineWithColorSpans"), std::string::npos);
	EXPECT_NE(Chat.find("const bool MergedPlayerMessages = Line.m_TimesRepeated > 0 && !Line.m_vMergedAuthors.empty();"), std::string::npos);
	EXPECT_NE(Chat.find("m_PlayerLine = Line.m_vMergedAuthors.size() <= 1"), std::string::npos);

	EXPECT_NE(ConsoleHeader.find("struct SColorSpan"), std::string::npos);
	EXPECT_NE(ConsoleHeader.find("m_ColorSpansByExportId"), std::string::npos);
	EXPECT_NE(ConsoleHeader.find("PrintLineWithColorSpans"), std::string::npos);
	EXPECT_NE(Console.find("m_PendingColorSpansByExportId"), std::string::npos);
	EXPECT_NE(Console.find("EntryCursor.m_vColorSplits.emplace_back"), std::string::npos);
	EXPECT_NE(Translate.find("for(const CChat::SMergedAuthor &Author : pLine->m_vMergedAuthors)"), std::string::npos);
}

TEST(QmChatMessageMerge, SettingIsDefaultOnLocalizedInDreamFeaturesAndVersioned)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Translations = ReadTestSourceFile("qmclient_scripts/languages_qmclient/translations/i18n/qmclient.toml");
	const std::string Version = ReadTestSourceFile("src/game/version.h");
	const size_t MiniFeatures = Menus.find("void CMenus::RenderQmFunctionMiniFeaturesContent(");

	ASSERT_NE(MiniFeatures, std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmMessageMerge, qm_message_merge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Menus.find("RenderCheckbox(&g_Config.m_QmMessageMerge, \"Message merging\", &g_Config.m_QmMessageMerge);", MiniFeatures), std::string::npos);
	EXPECT_NE(Translations.find("key = \"Message merging\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"消息合并\""), std::string::npos);
	EXPECT_NE(Version.find("#define QMCLIENT_VERSION \""), std::string::npos);
}

TEST(QmWarListEnemyChat, FilteringKeepsChatLogPersistenceIndependent)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string AddLine = SourceFunctionBody(Chat, "void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional");
	const std::string OnMessage = SourceFunctionBody(Chat, "void CChat::OnMessage(");
	const std::string WarListSettings = SourceFunctionBody(Menus, "void CMenus::RenderSettingsTClientWarList(");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmWarListBlockEnemyChat, qm_warlist_block_enemy_chat, 0, 0, 1"), std::string::npos);
	EXPECT_NE(AddLine.find("g_Config.m_QmWarListBlockEnemyChat"), std::string::npos);
	EXPECT_NE(AddLine.find("GameClient()->m_WarList.IsEnemy(ClientId)"), std::string::npos);
	EXPECT_NE(AddLine.find("GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_QmWarListBlockEnemyChat"), std::string::npos);
	EXPECT_EQ(AddLine.find("m_TcWarList && g_Config.m_QmWarListBlockEnemyChat"), std::string::npos);
	EXPECT_EQ(OnMessage.find("m_QmWarListBlockEnemyChat"), std::string::npos);
	EXPECT_NE(WarListSettings.find("&g_Config.m_QmWarListBlockEnemyChat"), std::string::npos);
	EXPECT_NE(WarListSettings.find("\"tclient-warlist-block-enemy-chat\""), std::string::npos);
	EXPECT_NE(WarListSettings.find("\"Block enemy chat\""), std::string::npos);

	const size_t AddLineCall = OnMessage.find("AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage)");
	const size_t SaveLogCall = OnMessage.find("SaveChatLogLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage)");
	ASSERT_NE(AddLineCall, std::string::npos);
	ASSERT_NE(SaveLogCall, std::string::npos);
	EXPECT_LT(AddLineCall, SaveLogCall);
}

TEST(QmChatPresentation, InactiveOldLineKeepsFullOpacity)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(20.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);
	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(0.31f), 0.10f, false, false, 0, 0.0f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(5.05f), 0.05f, false, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);
}

TEST(QmChatPresentation, InactiveExpiredLineFadesAndCollapses)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(30.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(14.1f), 0.20f, false, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::EXITING);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 0.5f, 0.001f);
	EXPECT_NEAR(Presentation.m_LayoutVisibility, 1.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderOffsetX, -12.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderOffsetY, 0.0f, 0.001f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(14.3f), 0.20f, false, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::COLLAPSED);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 0.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_LayoutVisibility, 0.0f, 0.001f);
}

TEST(QmChatPresentation, DisabledExtraAnimationsUseImmediateVisibilityStates)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(35.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(0.01f), 0.01f, false, false, 0, 0.0f, false);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_EntryProgress, 1.0f);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 1.0f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(14.1f), 0.20f, false, false, 0, 0.0f, false);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::COLLAPSED);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 0.0f);
}

TEST(QmChatPresentation, DisabledExtraAnimationsRecallHistoryImmediately)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(38.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(30.0f), 0.10f, true, false, Start + TestTicks(30.0f), 0.2f, false);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 1.0f);
}

TEST(QmChatPresentation, ReenablingExtraAnimationsDoesNotReplaySettledStates)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(39.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(0.01f), 0.01f, false, false, 0, 0.0f, false);
	ASSERT_TRUE(Presentation.m_AnimationsSuppressed);
	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(0.02f), 0.01f, false, false, 0, 0.0f, true);
	EXPECT_FALSE(Presentation.m_AnimationsSuppressed);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 1.0f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(14.1f), 0.20f, false, false, 0, 0.0f, false);
	ASSERT_TRUE(Presentation.m_AnimationsSuppressed);
	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(14.11f), 0.01f, false, false, 0, 0.0f, true);
	EXPECT_TRUE(Presentation.m_AnimationsSuppressed);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::COLLAPSED);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 0.0f);
}

TEST(QmChatPresentation, ReenablingExtraAnimationsDoesNotReplayExpandedHistory)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(39.0f);
	const int64_t OpenTick = Start + TestTicks(30.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, OpenTick, 0.10f, true, false, OpenTick, 0.2f, false);
	ASSERT_TRUE(Presentation.m_AnimationsSuppressed);
	CChat::UpdateLinePresentation(Presentation, Start, OpenTick + TestTicks(0.01f), 0.01f, true, false, OpenTick, 0.2f, true);
	EXPECT_TRUE(Presentation.m_AnimationsSuppressed);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_RenderOffsetX, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 1.0f);
}

TEST(QmChatPresentation, InputKeepsOldLineOpaque)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(40.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(5.20f), 0.18f, true, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_NEAR(Presentation.m_LayoutVisibility, 1.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderOffsetX, 0.0f, 0.001f);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);
}

TEST(QmChatPresentation, ClosingInputKeepsOldLineVisible)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(50.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);
	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(5.70f), 0.18f, true, false, 0, 0.0f);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(5.72f), 0.02f, false, false, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);
}

TEST(QmChatPresentation, ForceVisibleLineDoesNotAutoDecay)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(60.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);

	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(30.0f), 0.10f, false, true, 0, 0.0f);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 1.0f);
	EXPECT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);
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

TEST(QmModeStatus, UsesExpiringEchoMessages)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/tclient.cpp");
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string FocusBody = SourceFunctionBody(Source, "void CTClient::ApplyFocusModeEffects()");
	const std::string GoresBody = SourceFunctionBody(Source, "void CTClient::ApplyGoresFastInputLink(bool AutoMapCheck)");
	const std::string EchoBody = SourceFunctionBody(Chat, "void CChat::Echo(const char *pString)");

	EXPECT_NE(FocusBody.find("GameClient()->Echo(aFocusMsg);"), std::string::npos);
	EXPECT_EQ(FocusBody.find("GameClient()->Echo(aFocusMsg, true);"), std::string::npos);
	EXPECT_NE(GoresBody.find("GameClient()->Echo(aGoresMsg);"), std::string::npos);
	EXPECT_EQ(GoresBody.find("GameClient()->Echo(aGoresMsg, true);"), std::string::npos);
	EXPECT_NE(EchoBody.find("GameClient()->m_QmHudNotifications.QueueEcho"), std::string::npos);
}

TEST(QmChatPresentation, ResetAndTimeRollbackKeepFiniteFreshState)
{
	CChat::SPresentationState Presentation;
	const int64_t Start = TestTicks(70.0f);
	CChat::BeginLinePresentation(Presentation, Start, false);
	CChat::UpdateLinePresentation(Presentation, Start, Start + TestTicks(5.50f), 0.20f, false, false, 0, 0.0f);
	ASSERT_EQ(Presentation.m_State, CChat::EPresentationState::VISIBLE);
	ASSERT_NEAR(Presentation.m_RenderAlpha, 1.0f, 0.001f);

	CChat::ResetPresentationState(Presentation);
	EXPECT_EQ(Presentation.m_State, CChat::EPresentationState::COLLAPSED);
	EXPECT_FLOAT_EQ(Presentation.m_LayoutVisibility, 0.0f);
	EXPECT_FLOAT_EQ(Presentation.m_RenderAlpha, 0.0f);

	CChat::BeginLinePresentation(Presentation, Start, false);
	CChat::UpdateLinePresentation(Presentation, Start, Start - TestTicks(1.0f), -1.0f, false, false, 0, 0.0f);
	EXPECT_TRUE(std::isfinite(Presentation.m_RenderAlpha));
	EXPECT_TRUE(std::isfinite(Presentation.m_RenderOffsetX));
	EXPECT_TRUE(std::isfinite(Presentation.m_RenderOffsetY));
}

TEST(QmChatPresentation, SmoothYApproachesTargetWithoutOvershoot)
{
	float Y = 200.0f;
	for(int i = 0; i < 16; ++i)
	{
		const float NextY = CChat::SmoothPresentationY(Y, 120.0f, 1.0f / 60.0f);
		EXPECT_TRUE(std::isfinite(NextY));
		EXPECT_LE(NextY, Y);
		EXPECT_GE(NextY, 120.0f);
		Y = NextY;
	}

	for(int i = 0; i < 16; ++i)
	{
		const float NextY = CChat::SmoothPresentationY(Y, 180.0f, 1.0f / 30.0f);
		EXPECT_TRUE(std::isfinite(NextY));
		EXPECT_GE(NextY, Y);
		EXPECT_LE(NextY, 180.0f);
		Y = NextY;
	}
}

TEST(QmWindowModes, WindowedFullscreenRemainsABorderlessNonResizableWindow)
{
	const std::string Backend = ReadTestSourceFile("src/engine/client/backend_sdl.cpp");
	const std::string SetWindowParams = SourceFunctionBody(Backend, "void CGraphicsBackend_SDL_GL::SetWindowParams(");
	const size_t WindowedFullscreenStart = SetWindowParams.find("else // Windowed fullscreen");
	const size_t WindowedStart = SetWindowParams.find("else // Windowed", WindowedFullscreenStart + 1);
	ASSERT_NE(WindowedFullscreenStart, std::string::npos);
	ASSERT_NE(WindowedStart, std::string::npos);
	const std::string WindowedFullscreen = SetWindowParams.substr(WindowedFullscreenStart, WindowedStart - WindowedFullscreenStart);

	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowFullscreen(m_pWindow, 0);"), std::string::npos);
	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowBordered(m_pWindow, SDL_FALSE);"), std::string::npos);
	EXPECT_NE(WindowedFullscreen.find("SDL_SetWindowResizable(m_pWindow, SDL_FALSE);"), std::string::npos);
}

TEST(QmWindowModes, StartupMarksWindowedFullscreenAsBorderless)
{
	const std::string Backend = ReadTestSourceFile("src/engine/client/backend_sdl.cpp");
	const std::string Graphics = ReadTestSourceFile("src/engine/client/graphics_threaded.cpp");
	const std::string IssueInit = SourceFunctionBody(Graphics, "int CGraphics_Threaded::IssueInit()");

	const size_t WindowedFullscreenStart = IssueInit.find("else // Windowed fullscreen");
	const size_t VSyncStart = IssueInit.find("if(g_Config.m_GfxVsync)", WindowedFullscreenStart + 1);
	ASSERT_NE(WindowedFullscreenStart, std::string::npos);
	ASSERT_NE(VSyncStart, std::string::npos);
	const std::string WindowedFullscreen = IssueInit.substr(WindowedFullscreenStart, VSyncStart - WindowedFullscreenStart);

	EXPECT_NE(IssueInit.find("if(IsExclusiveFullscreen)"), std::string::npos);
	EXPECT_NE(IssueInit.find("else if(IsDesktopFullscreen)"), std::string::npos);
	EXPECT_NE(IssueInit.find("else if(IsPurelyWindowed)"), std::string::npos);
	EXPECT_NE(WindowedFullscreen.find("Flags |= IGraphicsBackend::INITFLAG_BORDERLESS;"), std::string::npos);
	EXPECT_NE(Backend.find("const bool IsWindowedFullscreen = g_Config.m_GfxFullscreen == 3;"), std::string::npos);
	EXPECT_NE(Backend.find("if(IsWindowedFullscreen || (IsFullscreen && !SupportedResolution)"), std::string::npos);
}

TEST(QmWindowModes, GraphicsMenuMapsAllFiveModesToDistinctBackendStates)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsGraphics = SourceFunctionBody(Menus, "void CMenus::RenderSettingsGraphics(");

	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->SetWindowParams(0, false);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->SetWindowParams(0, true);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->SetWindowParams(3, false);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->SetWindowParams(2, false);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->SetWindowParams(1, false);"), std::string::npos);
}

TEST(QmChatInteractions, ClampBacklogLine)
{
	EXPECT_EQ(CChat::ClampBacklogLine(-3, 10, 4), 0);
	EXPECT_EQ(CChat::ClampBacklogLine(0, 10, 4), 0);
	EXPECT_EQ(CChat::ClampBacklogLine(6, 10, 4), 6);
	EXPECT_EQ(CChat::ClampBacklogLine(7, 10, 4), 6);
	EXPECT_EQ(CChat::ClampBacklogLine(20, 10, 4), 6);
}

TEST(QmChatInteractions, HudTransformInversePreservesChatOrigin)
{
	const CUIRect DefaultRect = {0.0f, 50.0f, 400.0f, 250.0f};
	const CUIRect TargetRect = {100.0f, 200.0f, 800.0f, 500.0f};
	const vec2 LogicalPoint = {73.0f, 91.0f};
	const float Scale = TargetRect.w / DefaultRect.w;
	const vec2 TransformedPoint = {
		TargetRect.x + (LogicalPoint.x - DefaultRect.x) * Scale,
		TargetRect.y + (LogicalPoint.y - DefaultRect.y) * Scale};

	const vec2 Result = CChat::InverseHudTransformPoint(TransformedPoint, DefaultRect, TargetRect);
	EXPECT_NEAR(Result.x, LogicalPoint.x, 0.001f);
	EXPECT_NEAR(Result.y, LogicalPoint.y, 0.001f);
}

TEST(QmChatInteractions, ChatLineHitStopsAtContentWidth)
{
	const CUIRect ContentRect = {5.0f, 90.0f, 120.0f, 15.0f};

	EXPECT_TRUE(CChat::IsChatLineHit(ContentRect, vec2(100.0f, 95.0f)));
	EXPECT_FALSE(CChat::IsChatLineHit(ContentRect, vec2(130.0f, 95.0f)));
	EXPECT_FALSE(CChat::IsChatLineHit(ContentRect, vec2(100.0f, 106.0f)));
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

TEST(QmChatInteractions, ChatLineMenuReopensOnOtherLinesAndClosesOnEmptySpaceOrOutsideLeftClick)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string OnRender = SourceFunctionBody(Source, "void CChat::OnRender()");
	const std::string UiHeader = ReadTestSourceFile("src/game/client/ui.h");
	const std::string UiSource = ReadTestSourceFile("src/game/client/ui.cpp");

	// 菜单打开时右键不再被整体屏蔽：允许在其它消息行重新定位菜单
	EXPECT_NE(OnRender.find("const bool ChatLineMenuRequested = m_Mode != MODE_NONE && !LanguageMenuOpen && !InsideInputBlock && !InsideTranslateButton && !InsideScrollbar && !m_ScrollbarDragging && !InsideChatLineMenu && Input()->KeyPress(KEY_MOUSE_2);"), std::string::npos);
	EXPECT_EQ(OnRender.find("const bool ChatLineMenuRequested = ChatCopyActive && Input()->KeyPress(KEY_MOUSE_2);"), std::string::npos);
	// 左键拖拽复制仍只在菜单关闭时生效，避免关闭菜单时误复制消息
	EXPECT_NE(OnRender.find("const bool ChatCopyActive = m_Mode != MODE_NONE && !LanguageMenuOpen && !ChatLineMenuOpen && !InsideInputBlock && !InsideTranslateButton && !InsideScrollbar && !m_ScrollbarDragging;"), std::string::npos);
	// 空白处右键关闭已打开的菜单
	EXPECT_NE(OnRender.find("else if(ChatLineMenuOpen)"), std::string::npos);
	// 左键按下非菜单区域立即关闭菜单
	EXPECT_NE(OnRender.find("Ui()->GetPopupMenuRect(&m_ChatLinePopupContext)"), std::string::npos);
	EXPECT_NE(OnRender.find("Input()->KeyPress(KEY_MOUSE_1)"), std::string::npos);
	// CUi 提供弹窗矩形访问器
	EXPECT_NE(UiHeader.find("const CUIRect *GetPopupMenuRect(const SPopupMenuId *pId) const;"), std::string::npos);
	EXPECT_NE(UiSource.find("const CUIRect *CUi::GetPopupMenuRect("), std::string::npos);
}

TEST(QmChatCommandCompletion, KeepsDdnetTabCompletionWithoutQmExtensions)
{
	const std::string ChatHeader = ReadTestSourceFile("src/game/client/components/chat.h");
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string BindChatHeader = ReadTestSourceFile("src/game/client/components/tclient/bindchat.h");
	const std::string BindChat = ReadTestSourceFile("src/game/client/components/tclient/bindchat.cpp");
	const std::string CMake = ReadTestSourceFile("CMakeLists.txt");
	const std::string OnInput = SourceFunctionBody(Chat, "bool CChat::OnInput(");
	const std::string RegisterCommand = SourceFunctionBody(Chat, "void CChat::RegisterCommand(");
	const size_t CompletionBufferGuard = OnInput.find("if(!m_CompletionUsed)");
	const size_t PlayerCompletionGuard = OnInput.find("if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/')");

	EXPECT_EQ(ChatHeader.find("SSlashCommandSuggestion"), std::string::npos);
	EXPECT_EQ(ChatHeader.find("BuildCommandUsagePreview"), std::string::npos);
	EXPECT_NE(ChatHeader.find("m_ServerCommandsNeedSorting"), std::string::npos);
	EXPECT_EQ(Chat.find("RenderSlashCommandSuggestions"), std::string::npos);
	EXPECT_EQ(Chat.find("BuildCommandUsagePreview"), std::string::npos);
	EXPECT_EQ(Chat.find("Autocompletion hint"), std::string::npos);
	EXPECT_EQ(OnInput.find("ApplySelectedSlashCommandSuggestion"), std::string::npos);
	EXPECT_EQ(OnInput.find("Event.m_Key == KEY_TAB && m_Input.GetString()[0] == '/'"), std::string::npos);
	ASSERT_NE(CompletionBufferGuard, std::string::npos);
	ASSERT_NE(PlayerCompletionGuard, std::string::npos);
	EXPECT_LT(CompletionBufferGuard, PlayerCompletionGuard);
	EXPECT_NE(OnInput.find("const bool ShiftPressed = Input()->ShiftIsPressed();"), std::string::npos);
	EXPECT_NE(OnInput.find("if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())"), std::string::npos);
	EXPECT_NE(OnInput.find("str_startswith_nocase(Command.m_aName, pCommandStart)"), std::string::npos);
	EXPECT_NE(OnInput.find("if(m_Input.GetString()[0] == '/' && (str_find(pCompletionString, \" \") || str_find(pCompletionString, \"\\\"\")))"), std::string::npos);
	EXPECT_NE(OnInput.find("m_aPlayerCompletionList"), std::string::npos);
	EXPECT_NE(RegisterCommand.find("m_ServerCommandsNeedSorting = true;"), std::string::npos);
	EXPECT_NE(OnInput.find("std::sort(m_vServerCommands.begin(), m_vServerCommands.end());"), std::string::npos);
	EXPECT_NE(ChatHeader.find("std::vector<CCommand> m_vServerCommands"), std::string::npos);
	EXPECT_EQ(BindChatHeader.find("ChatDoAutocomplete"), std::string::npos);
	EXPECT_EQ(BindChat.find("CBindChat::ChatDoAutocomplete"), std::string::npos);
	EXPECT_EQ(CMake.find("components/chat_completion"), std::string::npos);
}

TEST(QmChatInteractions, ScrollbarValueToBacklogLine)
{
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(1.0f, 12), 0);
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(0.0f, 12), 12);
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(0.5f, 12), 6);
}

TEST(QmChatInteractions, BacklogLineToScrollbarValue)
{
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(0, 12), 1.0f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(12, 12), 0.0f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(6, 12), 0.5f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(20, 12), 0.0f);
}

TEST(QmFastPracticeCommands, TeleCursorTargetMatchesPracticeCursorWorldConversion)
{
	const vec2 CharacterPos(100.0f, 200.0f);
	const vec2 Target(400.0f, 0.0f);
	const vec2 Result = CFastPractice::PracticeTeleCursorTarget(CharacterPos, Target, 2.0f, 100, 50);

	EXPECT_FLOAT_EQ(Result.x, 750.0f);
	EXPECT_FLOAT_EQ(Result.y, 200.0f);
}

TEST(QmFastPracticeCommands, TeleportDefaultsToAimingOrSpectatingPosition)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const size_t CommandBlock = Source.find("if(Cmd == \"tp\" || Cmd == \"teleport\" || Cmd == \"tc\" || Cmd == \"telecursor\")");
	ASSERT_NE(CommandBlock, std::string::npos);
	const size_t TelecursorBranch = Source.find("if(Cmd == \"tc\" || Cmd == \"telecursor\")", CommandBlock);
	ASSERT_NE(TelecursorBranch, std::string::npos);
	const std::string DefaultTargetBlock = Source.substr(CommandBlock, TelecursorBranch - CommandBlock);

	EXPECT_NE(DefaultTargetBlock.find("vec2 Target = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];"), std::string::npos);
	EXPECT_EQ(DefaultTargetBlock.find("PracticeTeleCursorTarget"), std::string::npos);
}

TEST(QmFastPracticeCommands, SpectatorCommandKeepsPracticeStateOnSnapshotMiss)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string Body = SourceFunctionBody(Source, "bool CFastPractice::ConsumeSpectatorCommand()");

	EXPECT_EQ(Body.find("Disable();"), std::string::npos);
	EXPECT_NE(Body.find("m_PracticeWorldInitialized = false;"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->m_PredictedDummyId = -1;"), std::string::npos);
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

TEST(QmChatInteractions, ClickDragThreshold)
{
	EXPECT_TRUE(CChat::IsCopyClickDrag(vec2(10.0f, 10.0f), vec2(12.0f, 12.0f)));
	EXPECT_FALSE(CChat::IsCopyClickDrag(vec2(10.0f, 10.0f), vec2(30.0f, 10.0f)));
}

TEST(QmChatInteractions, ChatInputClipPaddingDoesNotExpandContentScrollArea)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Body = SourceFunctionBody(Source, "void CChat::OnRender()");

	EXPECT_NE(Body.find("const float InputContentHeight = 2.25f * InputCursor.m_FontSize;"), std::string::npos);
	EXPECT_NE(Body.find("const float InputClipPaddingTop = maximum(1.0f, InputCursor.m_FontSize * 0.18f);"), std::string::npos);
	EXPECT_NE(Body.find("const float InputClipPaddingBottom = maximum(1.0f, InputCursor.m_FontSize * 0.10f);"), std::string::npos);
	EXPECT_NE(Body.find("const CUIRect InputContentRect"), std::string::npos);
	EXPECT_NE(Body.find("const CUIRect InputClippingRect"), std::string::npos);
	EXPECT_NE(Body.find("InputContentRect.y + InputClipPaddingTop - ScrollOffset"), std::string::npos);
	EXPECT_NE(Body.find("m_Input.GetCaretPosition().y - InputClipPaddingTop - ScrollOffsetChange"), std::string::npos);
	EXPECT_NE(Body.find("CaretPositionY < InputContentRect.y"), std::string::npos);
	EXPECT_NE(Body.find("InputContentRect.y + InputContentRect.h"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->ClipEnable((int)(InputClippingRect.x * XScale)"), std::string::npos);
	EXPECT_EQ(Body.find("CaretPositionY < InputClippingRect.y"), std::string::npos);
}

TEST(QmChatInteractions, ChatInputPrefixDoesNotReserveSpaceForRightAlignedTranslateButton)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Body = SourceFunctionBody(Source, "void CChat::OnRender()");
	const size_t InputLayoutStart = Body.find("// render chat input");
	ASSERT_NE(InputLayoutStart, std::string::npos);
	const size_t InputLayoutEnd = Body.find("// 渲染翻译按钮", InputLayoutStart);
	ASSERT_NE(InputLayoutEnd, std::string::npos);
	const std::string InputLayout = Body.substr(InputLayoutStart, InputLayoutEnd - InputLayoutStart);

	EXPECT_NE(InputLayout.find("InputCursor.SetPosition(vec2(x, y));"), std::string::npos);
	EXPECT_EQ(InputLayout.find("InputCursor.SetPosition(vec2(x + TranslateButtonSize + TranslateButtonGap, y));"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect TranslateButtonRect = {InputContentRect.x + InputContentRect.w + TranslateButtonGap"), std::string::npos);
}

TEST(QmChatInteractions, AppendsBlockWordsWithSeparator)
{
	char aList[32] = "";

	EXPECT_TRUE(CChat::AppendBlockWordToList(aList, sizeof(aList), "spam"));
	EXPECT_STREQ(aList, "spam");

	EXPECT_TRUE(CChat::AppendBlockWordToList(aList, sizeof(aList), "eggs"));
	EXPECT_STREQ(aList, "spam;eggs");
}

TEST(QmChatInteractions, DoesNotAppendEmptyOrFullBlockWords)
{
	char aList[8] = "filled";

	EXPECT_FALSE(CChat::AppendBlockWordToList(aList, sizeof(aList), ""));
	EXPECT_STREQ(aList, "filled");

	EXPECT_FALSE(CChat::AppendBlockWordToList(aList, sizeof(aList), "x"));
	EXPECT_STREQ(aList, "filled");
}

TEST(QmChatBlockWords, HideActionOnlySuppressesMatchedRemotePlayerMessages)
{
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::REPLACE, true, 5, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, false, 5, false, 0));

	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, 0));
	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, 1));
	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, TEAM_WHISPER_RECV));
}

TEST(QmChatBlockWords, HideActionKeepsLocalAndNonPlayerMessagesVisible)
{
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, true, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, -1, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, -2, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, TEAM_WHISPER_SEND));
}

TEST(QmChatBlockWords, MatchedMessageKeepsRawConsoleAndChatLogPaths)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string AddLine = SourceFunctionBody(Chat, "void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional");
	const std::string OnMessage = SourceFunctionBody(Chat, "void CChat::OnMessage(");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmBlockWordsAction, qm_block_words_action, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Menus.find("g_Config.m_QmBlockWordsAction == 0"), std::string::npos);
	EXPECT_NE(Menus.find("g_Config.m_QmBlockWordsAction = 0;"), std::string::npos);
	EXPECT_NE(Menus.find("BlockWordsAction == 1"), std::string::npos);
	EXPECT_NE(Menus.find("g_Config.m_QmBlockWordsAction = 1;"), std::string::npos);
	EXPECT_NE(Menus.find("qmclient-word-filter-match-mode\", &LabelColumn, Localize(\"Mode\")"), std::string::npos);
	const size_t RawConsoleCall = AddLine.find("PrintBlockedMessageToConsole(ClientId, Team, pLine);");
	const size_t HideBranch = AddLine.find("if(CanHideBlockWordsMessage)");
	ASSERT_NE(RawConsoleCall, std::string::npos);
	ASSERT_NE(HideBranch, std::string::npos);
	EXPECT_LT(RawConsoleCall, HideBranch);
	EXPECT_NE(AddLine.find("BlockWordsConsolePrinted = true;"), std::string::npos);
	EXPECT_NE(AddLine.find("PreviousLine.m_ConsoleSuppressed == BlockWordsConsolePrinted"), std::string::npos);
	EXPECT_NE(AddLine.find("CurrentLine.m_ConsoleSuppressed = BlockWordsConsolePrinted;"), std::string::npos);
	EXPECT_NE(AddLine.find("ShouldHideBlockWordsMessage("), std::string::npos);
	EXPECT_NE(AddLine.find("BlockWordsAction == EBlockWordsAction::REPLACE || CanHideBlockWordsMessage"), std::string::npos);
	EXPECT_NE(AddLine.find("Client()->State() == IClient::STATE_DEMOPLAYBACK"), std::string::npos);
	EXPECT_NE(AddLine.find("ClientId == GameClient()->m_Snap.m_LocalClientId"), std::string::npos);
	EXPECT_NE(AddLine.find("GameClient()->IsLocalClientId(ClientId)"), std::string::npos);
	EXPECT_NE(OnMessage.find("SaveChatLogLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage)"), std::string::npos);
}

TEST(QmChatInteractions, BuildsEscapedWhisperCommand)
{
	char aCommand[128];

	EXPECT_TRUE(CChat::BuildWhisperCommand(aCommand, sizeof(aCommand), "Name \"A\"", "hello"));
	EXPECT_STREQ(aCommand, "/w \"Name \\\"A\\\"\" hello");
}

TEST(QmChatInteractions, BuildsEscapedSpectateCommand)
{
	char aCommand[128];

	EXPECT_TRUE(CChat::BuildSpectateCommand(aCommand, sizeof(aCommand), "Name \"A\""));
	EXPECT_STREQ(aCommand, "say /spec \"Name \\\"A\\\"\"");
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
	EXPECT_NE(Source.find("DoEntry(&pPopupContext->m_SpectateButton, FontIcons::FONT_ICON_EYE, Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->m_Spectator.Spectate(Context.m_ClientId);"), std::string::npos);
	EXPECT_NE(Source.find("Console()->ExecuteLine(aCommand);"), std::string::npos);
}

TEST(QmChatInteractions, ReusesKnownServerMessageClassWithoutReanalysis)
{
	const auto Class = CChat::ResolveLineServerMessageClass(-1, "DDraceNetwork Version: 18.9", QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::Prompt);
}

TEST(QmChatInteractions, FallsBackToLegacyServerMessageClassificationWhenUnknown)
{
	const auto Class = CChat::ResolveLineServerMessageClass(-1, "DDraceNetwork Version: 18.9");
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::BasicInfo);
}

TEST(QmChatInteractions, IgnoresKnownServerClassForNonServerMessages)
{
	const auto Class = CChat::ResolveLineServerMessageClass(3, "DDraceNetwork Version: 18.9", QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::None);
}

TEST(QmChatInteractions, ManualVisibleTranslationCandidatesAreOnlyUntranslatedRemotePlayerLines)
{
	int aLocalIds[] = {2, 7};
	EXPECT_TRUE(CChat::IsManualVisibleTranslateCandidate(3, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(-1, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(-2, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(2, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(3, false, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(3, true, true, aLocalIds, std::size(aLocalIds)));
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

TEST(QmRedPacketAutoClaim, ExtractsPasswordFromServerAnnouncement)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。",
		Password));
	EXPECT_EQ(Password, "deimos:把钱给我！");
}

TEST(QmRedPacketAutoClaim, ExtractsAllPasswordCharactersFromCompatibleAnnouncement)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] taiko 发了 50 币红包，共 50 个。输入口令「\\\" \\\"」即可抢。",
		Password));
	EXPECT_EQ(Password, "\\\" \\\"");
}

TEST(QmRedPacketAutoClaim, AcceptsCompatibleAnnouncementWithAdditionalText)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] 红包提示：输入口令「deimos:把钱给我！」即可抢，先到先得。",
		Password));
	EXPECT_EQ(Password, "deimos:把钱给我！");
}

TEST(QmRedPacketAutoClaim, PreservesWhitespaceOnlyPassword)
{
	CQmRedPacketAutoClaim Claim;
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare(
		"110.42.41.209:8303",
		"璇梦",
		"[Tee新葡京] 红包提示：输入口令「 」即可抢。",
		Password));
	EXPECT_EQ(Password, " ");
}

TEST(QmRedPacketAutoClaim, RequiresExactServerAndMainPlayerName)
{
	const char *pMessage = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。";
	std::string Password;

	CQmRedPacketAutoClaim WrongServer;
	EXPECT_FALSE(WrongServer.TryPrepare("110.42.41.209:8304", "璇梦", pMessage, Password));

	CQmRedPacketAutoClaim WrongName;
	EXPECT_FALSE(WrongName.TryPrepare("110.42.41.209:8303", "璇夢", pMessage, Password));
}

TEST(QmRedPacketAutoClaim, RejectsMalformedAnnouncements)
{
	const char *apInvalidMessages[] = {
		"deimos:把钱给我！",
		"[Tee新葡京] 红包提示：输入口令「」即可抢。",
		"[Tee新葡京] 红包提示：口令「deimos:把钱给我！」即可抢。",
		"[Tee新葡京] 输入口令「deimos:把钱给我！」即可抢。",
		"[Tee新葡京] 红包提示：输入口令「deimos:把钱给我！」。",
		"[Tee新葡京] 红包提示：即可抢。输入口令「deimos:把钱给我！」",
		"[Tee新葡京] 输入口令「deimos:把钱给我！」红包提示，即可抢。",
	};

	for(const char *pMessage : apInvalidMessages)
	{
		CQmRedPacketAutoClaim Claim;
		std::string Password;
		EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password)) << pMessage;
	}
}

TEST(QmRedPacketAutoClaim, SendsEachAnnouncementOnlyOncePerConnection)
{
	CQmRedPacketAutoClaim Claim;
	const char *pMessage = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「deimos:把钱给我！」即可抢。";
	const char *pAnotherMessage = "[Tee新葡京] taiko 发了 50 币红包，共 50 个。输入口令「deimos:把钱给我！」即可抢。";
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pAnotherMessage, Password));

	Claim.Reset();
	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", pMessage, Password));
}

TEST(QmRedPacketAutoClaim, BoundsDeduplicationHistoryDuringLongConnections)
{
	CQmRedPacketAutoClaim Claim;
	constexpr size_t DeduplicationHistoryLimit = 64;
	std::string FirstMessage;
	std::string Password;

	for(size_t i = 0; i <= DeduplicationHistoryLimit; ++i)
	{
		const std::string Message = "[Tee新葡京] 红包提示 " + std::to_string(i) + "：输入口令「claim-" + std::to_string(i) + "」即可抢。";
		if(i == 0)
			FirstMessage = Message;
		EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
	}

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", FirstMessage.c_str(), Password));
	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", FirstMessage.c_str(), Password));
}

TEST(QmRedPacketAutoClaim, PreservesPasswordAtChatCharacterLimit)
{
	CQmRedPacketAutoClaim Claim;
	std::string ExpectedPassword;
	for(size_t i = 0; i < CQmRedPacketAutoClaim::MAX_PASSWORD_CHARACTERS; ++i)
		ExpectedPassword += "钱";
	const std::string Message = "[Tee新葡京] 红包提示：输入口令「" + ExpectedPassword + "」即可抢。";
	std::string Password;

	EXPECT_TRUE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
	EXPECT_EQ(Password, ExpectedPassword);
}

TEST(QmRedPacketAutoClaim, RejectsPasswordThatExceedsChatCharacterLimit)
{
	CQmRedPacketAutoClaim Claim;
	std::string Message = "[Tee新葡京] deimos 发了 80 币红包，共 20 个。输入口令「";
	Message.append(CQmRedPacketAutoClaim::MAX_PASSWORD_CHARACTERS + 1, 'a');
	Message += "」即可抢。";
	std::string Password;

	EXPECT_FALSE(Claim.TryPrepare("110.42.41.209:8303", "璇梦", Message.c_str(), Password));
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
	const size_t LocalSaveGuard = SendChatOnConn.find("if(HandleLocalSaveForLoadCommand)");
	const size_t LocalSaveRemoval = SendChatOnConn.find("TryRemoveLocalSaveForLoadCommand(pLine)");
	ASSERT_NE(LocalSaveGuard, std::string::npos);
	ASSERT_NE(LocalSaveRemoval, std::string::npos);
	EXPECT_LT(LocalSaveGuard, LocalSaveRemoval);
}
