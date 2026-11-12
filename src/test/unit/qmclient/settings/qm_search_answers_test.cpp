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

TEST(QmSearchAnswers, ExactByteLimitAcceptsACompleteQueryAndTheNextByteRejectsIt)
{
	const std::string AtLimit = "截图" + std::string(506, '?');
	ASSERT_EQ(AtLimit.size(), 512u);
	const auto Accepted = qm_search_answers::Search(AtLimit.c_str());
	ASSERT_NE(Accepted.m_pAnswer, nullptr);
	EXPECT_STREQ(Accepted.m_pAnswer->m_pId, "screenshots");
	const std::string OverLimit = AtLimit + '?';
	ASSERT_EQ(OverLimit.size(), 513u);
	const auto Rejected = qm_search_answers::Search(OverLimit.c_str());
	EXPECT_EQ(Rejected.m_pAnswer, nullptr);
	EXPECT_TRUE(Rejected.m_vCandidates.empty());
}

TEST(QmSearchAnswers, ByteLimitCountsUtf8PunctuationBeforeNormalization)
{
	std::string AtLimit = "截图";
	for(int Index = 0; Index < 168; ++Index)
		AtLimit += "？";
	AtLimit += "  ";
	ASSERT_EQ(AtLimit.size(), 512u);
	const auto Accepted = qm_search_answers::Search(AtLimit.c_str());
	ASSERT_NE(Accepted.m_pAnswer, nullptr);
	EXPECT_STREQ(Accepted.m_pAnswer->m_pId, "screenshots");
	const auto Rejected = qm_search_answers::Search((AtLimit + ' ').c_str());
	EXPECT_EQ(Rejected.m_pAnswer, nullptr);
	EXPECT_TRUE(Rejected.m_vCandidates.empty());
}

TEST(QmSearchAnswers, MalformedUtf8CannotLeaveAnAnswerFromItsValidPrefix)
{
	for(const char *pQuery : {"截图\xc0\xaf", "截图\xed\xa0\x80", "截图\xf4\x90\x80\x80", "截图\xe4\xb8", "截图\x80"})
	{
		SCOPED_TRACE(pQuery);
		const auto Result = qm_search_answers::Search(pQuery);
		EXPECT_EQ(Result.m_pAnswer, nullptr);
		EXPECT_TRUE(Result.m_vCandidates.empty());
	}
}

TEST(QmSearchAnswers, CandidateLimitKeepsDistinctQuestionsWithoutInventingADirectAnswer)
{
	const auto Result = qm_search_answers::Search("截图复读翻译歌词旁观小眼睛游戏没声音");
	EXPECT_EQ(Result.m_pAnswer, nullptr);
	ASSERT_EQ(Result.m_vCandidates.size(), 5u);
	for(const auto *pCandidate : Result.m_vCandidates)
	{
		ASSERT_NE(pCandidate, nullptr);
		EXPECT_EQ(std::count(Result.m_vCandidates.begin(), Result.m_vCandidates.end(), pCandidate), 1);
	}
}

TEST(QmSearchAnswers, EqualScoreCandidatesKeepTheirOrderAcrossOtherQueries)
{
	const auto First = qm_search_answers::Search("没声音");
	ASSERT_EQ(First.m_vCandidates.size(), 3u);
	EXPECT_STREQ(First.m_vCandidates[0]->m_pId, "game-sound");
	EXPECT_STREQ(First.m_vCandidates[1]->m_pId, "voice-sound");
	EXPECT_STREQ(First.m_vCandidates[2]->m_pId, "music-sound");
	ASSERT_NE(qm_search_answers::Search("截图").m_pAnswer, nullptr);
	const auto Reopened = qm_search_answers::Search("没声音");
	EXPECT_EQ(Reopened.m_pAnswer, nullptr);
	EXPECT_EQ(Reopened.m_vCandidates, First.m_vCandidates);
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
