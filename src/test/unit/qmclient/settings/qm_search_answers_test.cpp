#include <game/client/components/qmclient/search_answers.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(QmSearchAnswers, CommonPhrasingsResolveToTheirAnswer)
{
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pId;
	};
	for(const auto &Case : {
		    SCase{"我身上的十字架怎么关？", "hitbox"},
		    SCase{"请问如何关闭碰撞箱呢？", "hitbox"},
		    SCase{"十字架怎么关", "hitbox"},
		    SCase{"碰撞箱怎么关闭", "hitbox"},
		    SCase{"黑色气泡", "notifications"},
		    SCase{"右侧通知", "notifications"},
		    SCase{"通知栏", "notifications"},
		    SCase{"截图在哪里", "screenshots"},
		    SCase{"我想打开武器辅助线", "trajectory"},
		    SCase{"游戏没有声音怎么办", "game-sound"},
		    SCase{"语音没声音", "voice-sound"},
		    SCase{"音乐没有声音", "music-sound"},
		    SCase{"翻译如何用？", "translation"}})
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Result = qm_search_answers::Search(Case.m_pQuery);
		ASSERT_NE(Result.m_pAnswer, nullptr);
		EXPECT_STREQ(Result.m_pAnswer->m_pId, Case.m_pId);
	}
}

TEST(QmSearchAnswers, AmbiguousSoundQueryOffersDifferentCauses)
{
	const auto Result = qm_search_answers::Search("请问没声音怎么办？");
	EXPECT_EQ(Result.m_pAnswer, nullptr);
	std::vector<std::string> vIds;
	for(const auto *pAnswer : Result.m_vCandidates)
		vIds.emplace_back(pAnswer->m_pId);
	std::sort(vIds.begin(), vIds.end());
	EXPECT_EQ(vIds, (std::vector<std::string>{"game-sound", "music-sound", "voice-sound"}));
}

TEST(QmSearchAnswers, RequestedFaqQueriesResolveToTheirAnswer)
{
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pId;
	};
	for(const auto &Case : {
		    SCase{"如何赞助?我能得到什么?", "sponsor"},
		    SCase{"贡献者名单里面没有我怎么办?", "missing-credit"},
		    SCase{"八角定位的bind是什么?", "octagonal-aim"},
		    SCase{"卡键是什么?", "held-keys"},
		    SCase{"游戏崩溃了怎么办?", "crash"},
		    SCase{"怎么启动QmClient的同时拉起Steam?", "steam-start"},
		    SCase{"有BUG怎么办?", "bug-report"},
		    SCase{"为什么锤人的声音有两遍", "hammer-double-sound"}})
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Result = qm_search_answers::Search(Case.m_pQuery);
		ASSERT_NE(Result.m_pAnswer, nullptr);
		EXPECT_STREQ(Result.m_pAnswer->m_pId, Case.m_pId);
	}
}

TEST(QmSearchAnswers, QmTcAndDdnetFeatureQueriesResolveToTheirAnswer)
{
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pId;
	};
	for(const auto &Case : {
		    SCase{"赞助有什么好处", "sponsor"},
		    SCase{"名单漏了", "missing-credit"},
		    SCase{"卡锤", "held-keys"},
		    SCase{"卡锤状态", "bind-status"},
		    SCase{"闪退", "crash"},
		    SCase{"自动启动Steam", "steam-start"},
		    SCase{"找不到Steam", "steam-path"},
		    SCase{"steam不计时", "steam-playtime"},
		    SCase{"锤子声音重复", "hammer-double-sound"},
		    SCase{"改名", "player-name"},
		    SCase{"皮肤放哪里", "skins"},
		    SCase{"连接分身", "dummy"},
		    SCase{"鼠标灵敏度", "mouse-sensitivity"},
		    SCase{"实体层怎么开", "entities"},
		    SCase{"录demo", "demo-recording"},
		    SCase{"钩线", "hook-line"},
		    SCase{"队内聊天", "team-chat"},
		    SCase{"BIND怎么用", "bind-basics"},
		    SCase{"取消绑定", "unbind"},
		    SCase{"TC轮盘", "tc-bind-wheel"},
		    SCase{"WarList", "tc-warlist"},
		    SCase{"身份预设", "tc-profiles"},
		    SCase{"TC状态栏", "tc-status-bar"},
		    SCase{"彩虹钩子", "tc-rainbow"},
		    SCase{"中文字体", "tc-font"},
		    SCase{"头顶气泡", "chat-bubble"},
		    SCase{"右边气泡", "notifications"},
		    SCase{"备份好友", "friend-code"},
		    SCase{"自杀不了", "self-kill"},
		    SCase{"X对齐", "coordinates"}})
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Result = qm_search_answers::Search(Case.m_pQuery);
		ASSERT_NE(Result.m_pAnswer, nullptr);
		EXPECT_STREQ(Result.m_pAnswer->m_pId, Case.m_pId);
	}
}

TEST(QmSearchAnswers, SelectingAnyCandidateQuestionResolvesThatAnswer)
{
	for(const auto &Answer : qm_search_answers::Answers())
	{
		SCOPED_TRACE(Answer.m_pId);
		const auto Result = qm_search_answers::Search(Answer.m_pQuestion);
		EXPECT_EQ(Result.m_pAnswer, &Answer);
	}
}

TEST(QmSearchAnswers, UnrecognizedContextRequiresChoosingACandidate)
{
	const auto Result = qm_search_answers::Search("截图和语音都出问题了");
	EXPECT_EQ(Result.m_pAnswer, nullptr);
	EXPECT_FALSE(Result.m_vCandidates.empty());
	EXPECT_LE(Result.m_vCandidates.size(), 5u);
}

TEST(QmSearchAnswers, UnknownEmptyAndGenericQueriesHaveNoAnswers)
{
	for(const char *pQuery : {"", " 　？！", "我想请问怎么开启呢", "怎么关", "明天天气怎么样", "a", "不存在的配置xyz"})
	{
		SCOPED_TRACE(pQuery);
		const auto Result = qm_search_answers::Search(pQuery);
		EXPECT_EQ(Result.m_pAnswer, nullptr);
		EXPECT_TRUE(Result.m_vCandidates.empty());
	}
	EXPECT_TRUE(qm_search_answers::Search(nullptr).m_vCandidates.empty());
}

TEST(QmSearchAnswers, CaseWhitespaceAndFullWidthFormsAreEquivalent)
{
	for(const char *pQuery : {"SCREENSHOTS", "　ＳＣＲＥＥＮＳＨＯＴＳ　？", "screenshots\n", "截 图"})
	{
		SCOPED_TRACE(pQuery);
		const auto Result = qm_search_answers::Search(pQuery);
		ASSERT_NE(Result.m_pAnswer, nullptr);
		EXPECT_STREQ(Result.m_pAnswer->m_pId, "screenshots");
	}
}

TEST(QmSearchAnswers, InvalidOrOversizedInputDoesNotReturnAnAnswer)
{
	EXPECT_TRUE(qm_search_answers::Search("截图\xff").m_vCandidates.empty());
	const std::string LongQuery = "截图" + std::string(513, '?');
	EXPECT_TRUE(qm_search_answers::Search(LongQuery.c_str()).m_vCandidates.empty());
}

TEST(QmSearchAnswers, QueriesDoNotRetainPreviousAnswerOrSelection)
{
	const auto First = qm_search_answers::Search("截图");
	ASSERT_NE(First.m_pAnswer, nullptr);
	EXPECT_TRUE(qm_search_answers::Search("").m_vCandidates.empty());
	EXPECT_EQ(qm_search_answers::Search("没声音").m_pAnswer, nullptr);
	const auto Reopened = qm_search_answers::Search("截图");
	EXPECT_EQ(Reopened.m_pAnswer, First.m_pAnswer);
	EXPECT_EQ(Reopened.m_vCandidates, First.m_vCandidates);
}

TEST(QmSearchAnswers, ActionsKeepFixedCommandsAndStorageRelativeDirectories)
{
	const auto Screenshot = qm_search_answers::Search("截图");
	ASSERT_NE(Screenshot.m_pAnswer, nullptr);
	EXPECT_STREQ(Screenshot.m_pAnswer->m_pDirectory, "screenshots");
	EXPECT_EQ(Screenshot.m_pAnswer->m_pCommand, nullptr);
	const auto Voice = qm_search_answers::Search("语音衰减");
	ASSERT_NE(Voice.m_pAnswer, nullptr);
	EXPECT_STREQ(Voice.m_pAnswer->m_pCommand, "qm_voice_group_global 1");
	const auto Cursor = qm_search_answers::Search("toggle_scoreboard_cursor");
	ASSERT_NE(Cursor.m_pAnswer, nullptr);
	EXPECT_STREQ(Cursor.m_pAnswer->m_pCommand, "bind key toggle_scoreboard_cursor");
}

TEST(QmSearchAnswers, OctagonalAimCopiesTheSuppliedBinding)
{
	const auto Result = qm_search_answers::Search("八角定位");
	ASSERT_NE(Result.m_pAnswer, nullptr);
	EXPECT_STREQ(Result.m_pAnswer->m_pCommand, "bind <key> \"+toggle cl_mouse_max_distance 2 330; +toggle inp_mousesens 1 114\"");
}

TEST(QmSearchAnswers, RepeatedHammerSoundOffersDisablingPredictedEvents)
{
	const auto Hammer = qm_search_answers::Search("为什么锤人的声音有两遍");
	ASSERT_NE(Hammer.m_pAnswer, nullptr);
	EXPECT_STREQ(Hammer.m_pAnswer->m_pCommand, "cl_predict_events 0");
}

TEST(QmSearchAnswers, StartingSteamWithClientOffersEnablingAutoLaunch)
{
	const auto Steam = qm_search_answers::Search("自动启动Steam");
	ASSERT_NE(Steam.m_pAnswer, nullptr);
	EXPECT_STREQ(Steam.m_pAnswer->m_pCommand, "qm_steam_auto_launch 1");
}

TEST(QmSearchAnswers, CrashAnswerOpensReportsWithoutExecutingCommands)
{
	const auto Crash = qm_search_answers::Search("闪退");
	ASSERT_NE(Crash.m_pAnswer, nullptr);
	EXPECT_STREQ(Crash.m_pAnswer->m_pDirectory, "dumps/QmClient_Crash");
	EXPECT_EQ(Crash.m_pAnswer->m_pCommand, nullptr);
}
