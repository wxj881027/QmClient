#include <game/client/components/qmclient/ime/qm_ime_lexicon.h>

#include <base/system.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace
{
	constexpr const char *READINGS =
		"U+4F60\tni\nU+597D\thao\nU+5C0F\txiao\nU+9738\tba\nU+738B\twang\n"
		"U+91CD\tzhong chong\nU+5E86\tqing\nU+94F6\tyin\nU+884C\txing hang\n"
		"U+957F\tchang zhang\nU+897F\txi\nU+5B89\tan\nU+4E2D\tzhong\nU+56FD\tguo\n"
		"U+7532\tjia\nU+4E59\tyi\nU+4E19\tbing\nU+4E01\tding\nU+723D\tshuang\nU+3007\tling yuan xing\n"
		"U+54B8\txian jian\nU+7684\tde di\nU+6885\tmei\nU+67FF\tshi\nU+5E72\tgan an\n";

	class CQmImeLexiconTest : public ::testing::Test
	{
	protected:
		int64_t m_Now = 10000;
		CQmImeDomainLexicon m_Lexicon{nullptr, [this] { return m_Now; }};

		void SetUp() override { ASSERT_TRUE(m_Lexicon.LoadReadingTable(READINGS)); }

		std::vector<SQmImeLexiconEntry> Match(const char *pCode, size_t Capacity = 20)
		{
			std::vector<SQmImeLexiconEntry> vResult(Capacity);
			vResult.resize(m_Lexicon.Match(pCode, vResult.data(), vResult.size()));
			return vResult;
		}

		std::string NumberedWord(int Number) const
		{
			char aNumber[8];
			str_format(aNumber, sizeof(aNumber), "%04d", Number);
			return std::string("你好") + aNumber + "中";
		}

		std::string FullLearnedFile() const
		{
			std::string Text = "qmimelex v1\n";
			for(int Index = 0; Index < static_cast<int>(CQmImeDomainLexicon::MAX_LEARNED_WORDS); ++Index)
			{
				const std::string Word = NumberedWord(Index);
				std::string Pinyin = "ni hao";
				for(char Digit : Word.substr(std::string("你好").size(), 4))
					Pinyin += std::string(" ") + Digit;
				Text += "3\t1\t" + std::to_string(Index) + '\t' + Word + '\t' + Pinyin + " zhong\n";
			}
			return Text;
		}
	};
}

TEST_F(CQmImeLexiconTest, FullPinyinAndAbbreviationFindImportedPlayer)
{
	m_Lexicon.FeedPlayerNames({"小霸王"});
	const auto Full = Match("xiaobawang");
	ASSERT_EQ(Full.size(), 1u);
	EXPECT_EQ(Full[0].m_Text, "小霸王");
	EXPECT_EQ(Full[0].m_Pinyin, "xiao ba wang");
	EXPECT_EQ(Full[0].m_Abbrev, "xbw");
	ASSERT_EQ(Match("xbw").size(), 1u);
	EXPECT_EQ(Match("xbw")[0].m_Text, "小霸王");
}

TEST_F(CQmImeLexiconTest, AbbreviationRequiresEveryWordCharacter)
{
	// “咸”有 xian/jiǎn 两个读音；旧逻辑首字命中就返回，单个 j 会误命中整条昵称。
	m_Lexicon.FeedPlayerNames({"咸的梅柿干"});
	EXPECT_TRUE(Match("j").empty());
	EXPECT_EQ(Match("xdmsg").size(), 1u);
}

TEST_F(CQmImeLexiconTest, AlternateReadingsMatchFullCodeAndInitials)
{
	m_Lexicon.FeedPlayerNames({"重庆", "银行行长"});
	EXPECT_EQ(Match("chongqing").size(), 1u);
	EXPECT_EQ(Match("zhongqing").size(), 1u);
	EXPECT_EQ(Match("cq").size(), 1u);
	EXPECT_EQ(Match("yinhangxingzhang").size(), 1u);
	EXPECT_EQ(Match("yhhc").size(), 1u);
	EXPECT_TRUE(Match("chongxing").empty());
}

TEST_F(CQmImeLexiconTest, AmbiguousSyllableBoundaryKeepsBothCharacters)
{
	m_Lexicon.FeedMapName("西安");
	EXPECT_EQ(Match("xian").size(), 1u);
	EXPECT_EQ(Match("xia").size(), 1u);
	EXPECT_EQ(Match("xa").size(), 1u);
	EXPECT_TRUE(Match("xina").empty());
}

TEST_F(CQmImeLexiconTest, PartialLastSyllableAndUppercaseInputMatch)
{
	m_Lexicon.FeedPlayerNames({"小霸王"});
	EXPECT_EQ(Match("XIAOB").size(), 1u);
	EXPECT_EQ(Match("xi").size(), 1u);
	EXPECT_TRUE(Match("xiao'ba").empty());
	EXPECT_TRUE(Match("").empty());
	EXPECT_TRUE(Match("小").empty());
}

TEST_F(CQmImeLexiconTest, MixedNameRetainsAsciiAndDecorationsInSubmittedText)
{
	m_Lexicon.FeedPlayerNames({"[你好A1]", "QmClient"});
	const auto Result = Match("nihaoa1");
	ASSERT_EQ(Result.size(), 1u);
	EXPECT_EQ(Result[0].m_Text, "[你好A1]");
	EXPECT_TRUE(Match("qmclient").empty());
}

TEST_F(CQmImeLexiconTest, ImportedDuplicatesKeepStrongestSourceAndStableRevision)
{
	m_Lexicon.FeedServerName("你好");
	m_Lexicon.FeedPlayerNames({"你好", "你好"});
	m_Lexicon.FeedMapName("你好");
	const uint64_t Revision = m_Lexicon.Revision();
	m_Lexicon.FeedPlayerNames({"你好"});
	EXPECT_EQ(m_Lexicon.Revision(), Revision);
	const auto Result = Match("nh");
	ASSERT_EQ(Result.size(), 1u);
	EXPECT_EQ(Result[0].m_Weight, 300);
	EXPECT_EQ(Result[0].m_Source, EQmImeLexiconSource::PLAYER);
}

TEST_F(CQmImeLexiconTest, SourceWeightsOrderPlayersMapsServersCommandsAndLearnedWords)
{
	m_Lexicon.FeedPlayerNames({"中国甲"});
	m_Lexicon.FeedMapName("中国乙");
	m_Lexicon.FeedServerName("中国丙");
	m_Lexicon.FeedCommands({"/zhong"});
	m_Lexicon.OnCommitted("中国丁");
	const auto Result = Match("zh");
	ASSERT_EQ(Result.size(), 5u);
	EXPECT_EQ(Result[0].m_Weight, 300);
	EXPECT_EQ(Result[1].m_Weight, 300);
	EXPECT_EQ(Result[2].m_Text, "中国丙");
	EXPECT_EQ(Result[2].m_Weight, 200);
	EXPECT_EQ(Result[3].m_Text, "/zhong");
	EXPECT_EQ(Result[3].m_Weight, 150);
	EXPECT_EQ(Result[4].m_Weight, 125);
}

TEST_F(CQmImeLexiconTest, SessionImportsDoNotPersistBeforeCommit)
{
	m_Lexicon.FeedPlayerNames({"你好"});
	m_Lexicon.FeedMapName("重庆");
	m_Lexicon.FeedCommands({"/team"});
	EXPECT_EQ(m_Lexicon.Serialize(), "qmimelex v1\n");
	EXPECT_FALSE(m_Lexicon.Dirty());
	m_Lexicon.OnCommitted("你好");
	EXPECT_EQ(m_Lexicon.LearnedSize(), 1u);
	EXPECT_TRUE(m_Lexicon.Dirty());
	m_Lexicon.BeginSession();
	EXPECT_EQ(Match("nh").size(), 1u);
	EXPECT_TRUE(Match("chongqing").empty());
	EXPECT_TRUE(Match("team").empty());
}

TEST_F(CQmImeLexiconTest, RepeatedCommitsRaiseWeightAndSaturateAtFourHundred)
{
	for(int Index = 0; Index < 20; ++Index)
		m_Lexicon.OnCommitted("你好");
	const auto Result = Match("nihao");
	ASSERT_EQ(Result.size(), 1u);
	EXPECT_EQ(Result[0].m_Weight, 400);
	EXPECT_EQ(m_Lexicon.LearnedSize(), 1u);
}

TEST_F(CQmImeLexiconTest, LearnedAndSessionDuplicateOnlyReturnsHigherWeight)
{
	m_Lexicon.FeedPlayerNames({"你好"});
	for(int Index = 0; Index < 10; ++Index)
		m_Lexicon.OnCommitted("你好");
	const auto Result = Match("nh");
	ASSERT_EQ(Result.size(), 1u);
	EXPECT_EQ(Result[0].m_Source, EQmImeLexiconSource::LEARNED);
	EXPECT_EQ(Result[0].m_Weight, 350);
}

TEST_F(CQmImeLexiconTest, LearningDisabledKeepsExistingWordsWithoutUpdatingThem)
{
	m_Lexicon.OnCommitted("你好");
	const std::string Before = m_Lexicon.Serialize();
	m_Lexicon.SetLearningEnabled(false);
	++m_Now;
	m_Lexicon.OnCommitted("你好");
	m_Lexicon.OnCommitted("重庆");
	EXPECT_EQ(m_Lexicon.Serialize(), Before);
	EXPECT_EQ(Match("nh").size(), 1u);
	EXPECT_TRUE(Match("cq").empty());
	m_Lexicon.SetLearningEnabled(true);
	m_Lexicon.OnCommitted("重庆");
	EXPECT_EQ(Match("cq").size(), 1u);
}

TEST_F(CQmImeLexiconTest, DisablingDomainWordsHidesMatchesAndPreservesLearning)
{
	m_Lexicon.SetEnabled(false);
	m_Lexicon.OnCommitted("你好");
	EXPECT_TRUE(Match("nh").empty());
	m_Lexicon.SetEnabled(true);
	EXPECT_EQ(Match("nh").size(), 1u);
}

TEST_F(CQmImeLexiconTest, AutomaticLearningAcceptsTwoToEightCharactersWithInternalAscii)
{
	for(const char *pText : {"你", "QmClient", "A你好", "你好A", "你好!", "你好你好你好你好你", "你A好"})
		m_Lexicon.OnCommitted(pText);
	EXPECT_EQ(m_Lexicon.LearnedSize(), 1u);
	EXPECT_EQ(Match("niahao").size(), 1u);
	m_Lexicon.OnCommitted("你好你好你好你好");
	EXPECT_EQ(m_Lexicon.LearnedSize(), 2u);
}

TEST_F(CQmImeLexiconTest, SelectedImportedLongNameAndCommandCanBeLearned)
{
	m_Lexicon.FeedPlayerNames({"你好你好你好你好你好"});
	m_Lexicon.FeedCommands({"/team"});
	m_Lexicon.OnCommitted("你好你好你好你好你好");
	m_Lexicon.OnCommitted("/team");
	m_Lexicon.BeginSession();
	EXPECT_EQ(m_Lexicon.LearnedSize(), 2u);
	EXPECT_EQ(Match("nhnhnhnhnh").size(), 1u);
	EXPECT_EQ(Match("team").size(), 1u);
}

TEST_F(CQmImeLexiconTest, SessionLimitStopsNewImportsAndResetAllowsNewSession)
{
	std::vector<std::string> vNames;
	for(int Index = 0; Index < 600; ++Index)
		vNames.push_back(NumberedWord(Index));
	m_Lexicon.FeedPlayerNames(vNames);
	EXPECT_EQ(m_Lexicon.SessionSize(), CQmImeDomainLexicon::MAX_SESSION_WORDS);
	m_Lexicon.BeginSession();
	m_Lexicon.FeedPlayerNames({"重庆"});
	EXPECT_EQ(m_Lexicon.SessionSize(), 1u);
	EXPECT_TRUE(Match("nihao").empty());
	EXPECT_EQ(Match("chongqing").size(), 1u);
}

TEST_F(CQmImeLexiconTest, LearningLimitEvictsOldestAmongLowestWeightEntries)
{
	ASSERT_TRUE(m_Lexicon.LoadText(FullLearnedFile()));
	m_Lexicon.OnCommitted(NumberedWord(0).c_str());
	++m_Now;
	m_Lexicon.OnCommitted(NumberedWord(5000).c_str());
	EXPECT_EQ(m_Lexicon.LearnedSize(), CQmImeDomainLexicon::MAX_LEARNED_WORDS);
	EXPECT_EQ(Match("nihao0000zhong").size(), 1u);
	EXPECT_TRUE(Match("nihao0001zhong").empty());
	EXPECT_EQ(Match("nihao5000zhong").size(), 1u);
}

TEST_F(CQmImeLexiconTest, PersistenceRoundTripRestoresCountsReadingsAndCommands)
{
	m_Lexicon.OnCommitted("重庆");
	++m_Now;
	m_Lexicon.OnCommitted("重庆");
	m_Lexicon.FeedCommands({"/pause"});
	m_Lexicon.OnCommitted("/pause");
	const std::string Saved = m_Lexicon.Serialize();
	CQmImeDomainLexicon Restored;
	ASSERT_TRUE(Restored.LoadReadingTable(READINGS));
	ASSERT_TRUE(Restored.LoadText(Saved));
	EXPECT_EQ(Restored.Serialize(), Saved);
	EXPECT_FALSE(Restored.Dirty());
	std::array<SQmImeLexiconEntry, 3> aOut;
	ASSERT_EQ(Restored.Match("chongqing", aOut.data(), aOut.size()), 1u);
	EXPECT_EQ(aOut[0].m_Weight, 150);
	EXPECT_EQ(Restored.Match("pause", aOut.data(), aOut.size()), 1u);
}

TEST_F(CQmImeLexiconTest, CorruptedFilePublishesNoPartialLearnedStateAndKeepsSession)
{
	m_Lexicon.FeedPlayerNames({"小霸王"});
	const char *apInvalid[] = {
		"qmimelex v2\n", "qmimelex v1\n3\t1\t0\t你好\n",
		"qmimelex v1\n3\t99999999999999999\t0\t你好\tni hao\n",
		"qmimelex v1\n3\t1\t-1\t你好\tni hao\n",
		"qmimelex v1\n3\t1\t0\t你好\\x\tni hao\n",
		"qmimelex v1\n3\t1\t0\t你好\tni hao\ninvalid\n",
		"qmimelex v1\n0\t1\t0\t你好\tni hao\n"};
	for(const char *pInvalid : apInvalid)
	{
		SCOPED_TRACE(pInvalid);
		m_Lexicon.OnCommitted("你好");
		EXPECT_FALSE(m_Lexicon.LoadText(pInvalid));
		EXPECT_EQ(m_Lexicon.LearnedSize(), 0u);
		EXPECT_EQ(Match("xbw").size(), 1u);
	}
}

TEST_F(CQmImeLexiconTest, DuplicatePersistedRowsKeepStrongerRecord)
{
	ASSERT_TRUE(m_Lexicon.LoadText("qmimelex v1\r\n3\t2\t100\t你好\tni hao\r\n3\t1\t200\t你好\tni hao\r\n"));
	ASSERT_EQ(Match("nh").size(), 1u);
	EXPECT_EQ(Match("nh")[0].m_Weight, 150);
	EXPECT_EQ(m_Lexicon.LearnedSize(), 1u);
}

TEST_F(CQmImeLexiconTest, ClearLearnedWorksWhileLearningDisabledAndKeepsSessionWords)
{
	m_Lexicon.FeedPlayerNames({"小霸王"});
	m_Lexicon.OnCommitted("你好");
	m_Lexicon.SetLearningEnabled(false);
	m_Lexicon.ClearLearned();
	EXPECT_EQ(m_Lexicon.Serialize(), "qmimelex v1\n");
	EXPECT_EQ(m_Lexicon.LearnedSize(), 0u);
	EXPECT_TRUE(Match("nh").empty());
	EXPECT_EQ(Match("xbw").size(), 1u);
}

TEST_F(CQmImeLexiconTest, InvalidReadingTableLeavesPreviousReadingsUsable)
{
	EXPECT_FALSE(m_Lexicon.LoadReadingTable("U+4F60\tni\nU+597D\thao7\n"));
	EXPECT_FALSE(m_Lexicon.LoadReadingTable("U+ZZZZ\tni\n"));
	m_Lexicon.FeedPlayerNames({"你好"});
	EXPECT_EQ(Match("nihao").size(), 1u);
}

TEST_F(CQmImeLexiconTest, UnknownReadingsAndInvalidUtf8CannotCreatePartialWords)
{
	m_Lexicon.FeedPlayerNames({"你好未知", std::string("你好") + '\xff'});
	m_Lexicon.OnCommitted((std::string("你好") + '\xff').c_str());
	EXPECT_EQ(m_Lexicon.SessionSize(), 0u);
	EXPECT_EQ(m_Lexicon.LearnedSize(), 0u);
}

TEST_F(CQmImeLexiconTest, NameAndCodeLengthBoundsIncludeTheirMaximum)
{
	m_Lexicon.FeedPlayerNames({"爽爽爽爽", "行行行行行行行行行行行行", "行行行行行行行行行行行行行"});
	EXPECT_EQ(m_Lexicon.SessionSize(), 2u);
	EXPECT_EQ(Match("shuangshuangshuangshuang").size(), 1u);
	EXPECT_TRUE(Match("shuangshuangshuangshuanga").empty());
	EXPECT_EQ(Match("hhhhhhhhhhhh").size(), 1u);
}

TEST_F(CQmImeLexiconTest, OutputCapacityNullPointersAndEmptyCodeAreSafe)
{
	m_Lexicon.FeedPlayerNames({"你好", "你好中"});
	std::array<SQmImeLexiconEntry, 2> aOut;
	EXPECT_EQ(m_Lexicon.Match("ni", aOut.data(), 1), 1u);
	EXPECT_EQ(m_Lexicon.Match("ni", aOut.data(), 0), 0u);
	EXPECT_EQ(m_Lexicon.Match("ni", nullptr, 2), 0u);
	EXPECT_EQ(m_Lexicon.Match(nullptr, aOut.data(), 2), 0u);
	EXPECT_EQ(m_Lexicon.Match("", aOut.data(), 2), 0u);
}

TEST_F(CQmImeLexiconTest, GeneratedIdeographicZeroReadingIsAccepted)
{
	ASSERT_TRUE(m_Lexicon.LoadReadingTable(std::string(READINGS) + "U+E815\tzi\n"));
	m_Lexicon.FeedPlayerNames({"〇你好"});
	EXPECT_EQ(Match("lingnihao").size(), 1u);
	EXPECT_EQ(Match("ynh").size(), 1u);
}

TEST_F(CQmImeLexiconTest, EscapedBackslashInSelectedNameSurvivesPersistence)
{
	m_Lexicon.FeedPlayerNames({"\\你好"});
	m_Lexicon.OnCommitted("\\你好");
	const std::string Saved = m_Lexicon.Serialize();
	m_Lexicon.ClearLearned();
	ASSERT_TRUE(m_Lexicon.LoadText(Saved));
	m_Lexicon.BeginSession();
	const auto Result = Match("nh");
	ASSERT_EQ(Result.size(), 1u);
	EXPECT_EQ(Result[0].m_Text, "\\你好");
}
