#include <game/client/components/qmclient/ime/qm_ime_merge.h>
#include <game/client/components/qmclient/ime/qm_ime_typing.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <unordered_set>

namespace
{
	SQmImeLexiconEntry Domain(const char *pText)
	{
		SQmImeLexiconEntry Entry;
		Entry.m_Text = pText;
		return Entry;
	}

	CQmImeEnglishInput English()
	{
		CQmImeEnglishInput Input;
		EXPECT_TRUE(Input.Load("hello\t30000\nhelp\t20000\nhelium\t100\n"));
		return Input;
	}

	void TypeEnglish(CQmImeEnglishInput &Input, const std::string &Text)
	{
		for(char Value : Text)
		{
			const bool Upper = Value >= 'A' && Value <= 'Z';
			const int Key = Upper ? KEY_A + Value - 'A' : KEY_A + Value - 'a';
			EXPECT_TRUE(Input.OnKey(Key, Upper ? QM_IME_KEYMASK_SHIFT : 0).m_Handled);
		}
	}
}

TEST(QmImeMerge, NativeFirstAndDomainSecondRetainGlobalSelectionIndices)
{
	const std::vector<SQmImeCandidate> vRime = {{"你好", "", 7}, {"泥好", "注释", 11}};
	const auto vMerged = QmImeBuildCandidateList(vRime, {Domain("小霸王")});
	ASSERT_EQ(vMerged.size(), 3u);
	EXPECT_EQ(vMerged[0].m_Text, "你好");
	EXPECT_EQ(vMerged[0].m_RimeIndex, 7);
	EXPECT_EQ(vMerged[1].m_Text, "小霸王");
	EXPECT_EQ(vMerged[1].m_RimeIndex, -1);
	EXPECT_EQ(vMerged[2].m_RimeIndex, 11);
	EXPECT_EQ(vMerged[2].m_Comment, "注释");
}

TEST(QmImeMerge, LearnedNativeTailMovesToSecondWithoutDuplicatingOrLosingSelection)
{
	const auto vMerged = QmImeBuildCandidateList({{"才是", "", 0}, {"采石", "", 1}, {"才识", "", 2}},
		{Domain("才识"), Domain("才是"), Domain("才识")});
	ASSERT_EQ(vMerged.size(), 3u);
	EXPECT_EQ(vMerged[0].m_Text, "才是");
	EXPECT_EQ(vMerged[1].m_Text, "才识");
	EXPECT_EQ(vMerged[1].m_RimeIndex, 2);
	EXPECT_EQ(vMerged[2].m_Text, "采石");
}

TEST(QmImeMerge, MergedPagesPreserveEveryUniqueCandidateAndOriginalIndex)
{
	std::vector<SQmImeCandidate> vRime;
	for(int Index = 0; Index < 37; ++Index)
		vRime.push_back({"候选" + std::to_string(Index), "", 100 + Index});
	const auto vMerged = QmImeBuildCandidateList(vRime, {Domain("领域甲"), Domain("领域乙"), Domain("候选8")});
	ASSERT_EQ(vMerged.size(), 39u);
	std::unordered_set<std::string> Seen;
	std::unordered_set<int> RimeIndices;
	const int Pages = QmImeComputeCandidatePage(0, static_cast<int>(vMerged.size()), 0, 9).m_PageCount;
	for(int PageIndex = 0; PageIndex < Pages; ++PageIndex)
	{
		const auto Page = QmImeComputeCandidatePage(0, static_cast<int>(vMerged.size()), PageIndex, 9);
		for(int Digit = 1; Digit <= Page.m_ShownRime; ++Digit)
		{
			const auto &Item = vMerged[Page.m_RimeStart + Digit - 1];
			EXPECT_TRUE(Seen.insert(Item.m_Text).second);
			if(Item.m_RimeIndex >= 0)
				EXPECT_TRUE(RimeIndices.insert(Item.m_RimeIndex).second);
		}
	}
	EXPECT_EQ(Seen.size(), 39u);
	EXPECT_EQ(RimeIndices.size(), 37u);
}

TEST(QmImeMerge, DomainOnlyAndEmptyCandidatesRemainSelectableWithoutEmptyRows)
{
	const auto vMerged = QmImeBuildCandidateList({{"", "", 0}}, {Domain(""), Domain("小霸王"), Domain("小霸王")});
	ASSERT_EQ(vMerged.size(), 1u);
	EXPECT_EQ(vMerged[0].m_Text, "小霸王");
	EXPECT_TRUE(QmImeBuildCandidateList({}, {}).empty());
}

TEST(QmImeEnglish, PrefixSuggestsWordsAndSpacePreservesTypedText)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	ASSERT_EQ(Input.Candidates().size(), 3u);
	EXPECT_EQ(Input.Candidates()[0], "hello");
	EXPECT_EQ(Input.Candidates()[1], "help");
	const auto Result = Input.OnKey(KEY_SPACE, 0);
	EXPECT_TRUE(Result.m_Handled);
	EXPECT_EQ(Result.m_Commit, "hel ");
	EXPECT_TRUE(Input.Code().empty());
	EXPECT_TRUE(Input.Candidates().empty());
}

TEST(QmImeEnglish, TabAcceptsHighlightedCompletionWithoutAddingSpace)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	Input.OnKey(KEY_DOWN, 0);
	EXPECT_EQ(Input.Selected(), 1);
	EXPECT_EQ(Input.OnKey(KEY_TAB, 0).m_Commit, "help");
	EXPECT_TRUE(Input.Code().empty());
}

TEST(QmImeEnglish, CompletionPreservesTypedCapitalization)
{
	for(const auto &Pair : std::array<std::pair<const char *, const char *>, 3>{{{"hel", "hello"}, {"Hel", "Hello"}, {"HEL", "HELLO"}}})
	{
		auto Input = English();
		TypeEnglish(Input, Pair.first);
		EXPECT_EQ(Input.OnKey(KEY_TAB, 0).m_Commit, Pair.second);
	}
}

TEST(QmImeEnglish, DigitsRemainLiteralRatherThanSelectingSuggestionNumber)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	EXPECT_TRUE(Input.OnKey(KEY_5, 0).m_Handled);
	EXPECT_EQ(Input.OnKey(KEY_SPACE, 0).m_Commit, "hel5 ");
}

TEST(QmImeEnglish, EnterFlushesOriginalBeforePassingToChat)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	const auto Result = Input.OnKey(KEY_RETURN, 0);
	EXPECT_FALSE(Result.m_Handled);
	EXPECT_EQ(Result.m_Commit, "hel");
	EXPECT_TRUE(Input.Code().empty());
}

TEST(QmImeEnglish, BackspaceUpdatesPrefixAndEscapeCancelsWithoutCommit)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	EXPECT_TRUE(Input.OnKey(KEY_BACKSPACE, 0).m_Handled);
	EXPECT_EQ(Input.Code(), "he");
	EXPECT_TRUE(Input.OnKey(KEY_ESCAPE, 0).m_Commit.empty());
	EXPECT_TRUE(Input.Code().empty());
	EXPECT_FALSE(Input.OnKey(KEY_BACKSPACE, 0).m_Handled);
}

TEST(QmImeEnglish, PunctuationCommitsOriginalAndKeepsAsciiPunctuation)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	EXPECT_EQ(Input.OnKey(KEY_COMMA, 0).m_Commit, "hel,");
}

TEST(QmImeEnglish, ClipboardShortcutFlushesOriginalAndPassesThrough)
{
	auto Input = English();
	TypeEnglish(Input, "hel");
	const auto Result = Input.OnKey(KEY_C, QM_IME_KEYMASK_CONTROL);
	EXPECT_FALSE(Result.m_Handled);
	EXPECT_EQ(Result.m_Commit, "hel");
}

TEST(QmImeEnglish, MissingDictionaryAllowsRawTypingAndTabPassThrough)
{
	CQmImeEnglishInput Input;
	TypeEnglish(Input, "hello");
	const auto Result = Input.OnKey(KEY_TAB, 0);
	EXPECT_FALSE(Result.m_Handled);
	EXPECT_EQ(Result.m_Commit, "hello");
}

TEST(QmImeEnglish, InvalidReloadRetainsWorkingSuggestions)
{
	auto Input = English();
	EXPECT_FALSE(Input.Load("wrong\t-1\n"));
	TypeEnglish(Input, "hel");
	EXPECT_EQ(Input.OnKey(KEY_TAB, 0).m_Commit, "hello");
}

TEST(QmImeNumberSuffix, NumberAllowsLatinUnitsAndReturnsToChineseAfterUnit)
{
	CQmImeNumberSuffix Suffix;
	EXPECT_FALSE(Suffix.IsLiteral('k'));
	Suffix.OnCommitted("他5");
	EXPECT_TRUE(Suffix.IsLiteral('k'));
	EXPECT_TRUE(Suffix.IsLiteral('K'));
	EXPECT_FALSE(Suffix.IsLiteral('f'));
	Suffix.OnCommitted("k");
	EXPECT_FALSE(Suffix.IsLiteral('f'));
	EXPECT_FALSE(Suffix.IsLiteral('k'));
}

TEST(QmImeNumberSuffix, DecimalAndMultipleDigitsKeepUnitsUntilBoundaryOrNavigation)
{
	CQmImeNumberSuffix Suffix;
	Suffix.OnCommitted("3");
	EXPECT_TRUE(Suffix.IsLiteral('.'));
	Suffix.OnCommitted(".");
	Suffix.OnCommitted("5");
	EXPECT_TRUE(Suffix.IsLiteral('w'));
	Suffix.OnCommitted(" ");
	EXPECT_FALSE(Suffix.IsLiteral('k'));
	Suffix.OnCommitted("10");
	Suffix.Reset();
	EXPECT_FALSE(Suffix.IsLiteral('K'));
}

TEST(QmImeNumberSuffix, FollowingVowelDistinguishesChineseQuantityFromLatinUnit)
{
	EXPECT_TRUE(CQmImeNumberSuffix::ContinuesPinyin('g', 'e'));
	EXPECT_TRUE(CQmImeNumberSuffix::ContinuesPinyin('t', 'i'));
	EXPECT_TRUE(CQmImeNumberSuffix::ContinuesPinyin('w', 'a'));
	EXPECT_TRUE(CQmImeNumberSuffix::ContinuesPinyin('k', 'e'));
	EXPECT_FALSE(CQmImeNumberSuffix::ContinuesPinyin('k', 'f'));
	EXPECT_FALSE(CQmImeNumberSuffix::ContinuesPinyin('K', 'e'));
}

TEST(QmImeShortcut, CtrlSpaceTogglesOnceAndRepeatsDoNotToggle)
{
	CQmImeModeShortcut Shortcut;
	using E = CQmImeModeShortcut::EResult;
	EXPECT_EQ(Shortcut.OnKey(KEY_SPACE, QM_IME_KEYMASK_CONTROL, true, false, false, true, KEY_SPACE, QM_IME_KEYMASK_CONTROL), E::TOGGLE);
	EXPECT_EQ(Shortcut.OnKey(KEY_SPACE, QM_IME_KEYMASK_CONTROL, true, true, false, true, KEY_SPACE, QM_IME_KEYMASK_CONTROL), E::CONSUME);
	EXPECT_EQ(Shortcut.OnKey(KEY_SPACE, QM_IME_KEYMASK_CONTROL, false, false, false, true, KEY_SPACE, QM_IME_KEYMASK_CONTROL), E::CONSUME);
	EXPECT_EQ(Shortcut.OnKey(KEY_C, QM_IME_KEYMASK_CONTROL, true, false, false, true, KEY_SPACE, QM_IME_KEYMASK_CONTROL), E::PASS);
}

TEST(QmImeShortcut, CtrlShiftWorksWithEitherPressAndReleaseOrder)
{
	using E = CQmImeModeShortcut::EResult;
	for(bool CtrlFirst : {false, true})
		for(bool ReleaseFirstPressed : {false, true})
		{
			SCOPED_TRACE(CtrlFirst);
			SCOPED_TRACE(ReleaseFirstPressed);
			CQmImeModeShortcut Shortcut;
			const int First = CtrlFirst ? KEY_LCTRL : KEY_RSHIFT;
			const int Second = CtrlFirst ? KEY_RSHIFT : KEY_LCTRL;
			Shortcut.OnKey(First, CQmImeModeShortcut::ModifierOf(First), true, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL);
			EXPECT_EQ(Shortcut.OnKey(Second, QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL), E::CONSUME);
			const auto FirstResult = Shortcut.OnKey(ReleaseFirstPressed ? First : Second, CQmImeModeShortcut::ModifierOf(ReleaseFirstPressed ? Second : First), false, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL);
			const auto SecondResult = Shortcut.OnKey(ReleaseFirstPressed ? Second : First, 0, false, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL);
			EXPECT_EQ((FirstResult == E::TOGGLE ? 1 : 0) + (SecondResult == E::TOGGLE ? 1 : 0), 1);
		}
}

TEST(QmImeShortcut, ShiftTapCanBeDisabledAndTypingOrChineseCompositionPreventsToggle)
{
	using E = CQmImeModeShortcut::EResult;
	CQmImeModeShortcut Shortcut;
	EXPECT_EQ(Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_SHIFT, true, false, false, false, KEY_UNKNOWN, 0), E::PASS);
	EXPECT_EQ(Shortcut.OnKey(KEY_LSHIFT, 0, false, false, false, false, KEY_UNKNOWN, 0), E::PASS);
	Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_UNKNOWN, 0);
	Shortcut.OnKey(KEY_A, QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_UNKNOWN, 0);
	EXPECT_EQ(Shortcut.OnKey(KEY_LSHIFT, 0, false, false, false, true, KEY_UNKNOWN, 0), E::CONSUME);
	Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_SHIFT, true, false, true, true, KEY_UNKNOWN, 0);
	EXPECT_EQ(Shortcut.OnKey(KEY_LSHIFT, 0, false, false, true, true, KEY_UNKNOWN, 0), E::CONSUME);
}

TEST(QmImeShortcut, CtrlShiftOtherKeyCancelsToggleAndFocusResetClearsPendingTap)
{
	using E = CQmImeModeShortcut::EResult;
	CQmImeModeShortcut Shortcut;
	Shortcut.OnKey(KEY_LCTRL, QM_IME_KEYMASK_CONTROL, true, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL);
	Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL);
	EXPECT_EQ(Shortcut.OnKey(KEY_C, QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL), E::PASS);
	EXPECT_NE(Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_CONTROL, false, false, false, true, KEY_LSHIFT, QM_IME_KEYMASK_CONTROL), E::TOGGLE);
	Shortcut.OnKey(KEY_LSHIFT, QM_IME_KEYMASK_SHIFT, true, false, false, true, KEY_UNKNOWN, 0);
	Shortcut.Reset();
	EXPECT_NE(Shortcut.OnKey(KEY_LSHIFT, 0, false, false, false, true, KEY_UNKNOWN, 0), E::TOGGLE);
}

TEST(QmImePinyinSlice, SelectedPrefixPreservesRemainingPinyinAndWholePhraseKeepsNativePath)
{
	CQmImePinyinSlicer Slicer;
	ASSERT_TRUE(Slicer.Load("U+624D\tcai\nU+662F\tshi\n"));
	EXPECT_EQ(Slicer.PrefixLength("才", "caishi"), 3u);
	EXPECT_EQ(Slicer.PrefixLength("是", "shi"), 3u);
	EXPECT_EQ(Slicer.PrefixLength("才是", "caishi"), 6u);
	EXPECT_EQ(Slicer.PrefixLength("才", "cai'shi"), 4u);
	EXPECT_EQ(Slicer.PrefixLength("才", "cs"), 1u);
	EXPECT_EQ(Slicer.PrefixLength("是", "caishi"), 0u);
}

TEST(QmImePinyinSlice, PolyphonesAndAmbiguousSyllablesMatchWithoutGuessingUnknownText)
{
	CQmImePinyinSlicer Slicer;
	ASSERT_TRUE(Slicer.Load("U+91CD\tzhong chong\nU+5E86\tqing\nU+897F\txi\nU+5B89\tan\n"));
	EXPECT_EQ(Slicer.PrefixLength("重", "chongqing"), 5u);
	EXPECT_EQ(Slicer.PrefixLength("重", "cq"), 1u);
	EXPECT_EQ(Slicer.PrefixLength("重庆", "cq"), 2u);
	EXPECT_EQ(Slicer.PrefixLength("西安", "xian"), 4u);
	EXPECT_EQ(Slicer.PrefixLength("西", "xian"), 2u);
	EXPECT_EQ(Slicer.PrefixLength("unknown", "xian"), 0u);
	EXPECT_EQ(Slicer.PrefixLength("重生", "chongsheng"), 0u);
}

TEST(QmImePinyinSlice, IncompleteFinalSyllableIsConsumedByNativeCompletion)
{
	CQmImePinyinSlicer Slicer;
	ASSERT_TRUE(Slicer.Load("U+597D\thao\nU+4F60\tni\nU+5C0F\txiao\n"));
	EXPECT_EQ(Slicer.PrefixLength("好", "ha"), 2u);
	EXPECT_EQ(Slicer.PrefixLength("你好", "niha"), 4u);
	EXPECT_EQ(Slicer.PrefixLength("小", "xi"), 2u);
	EXPECT_EQ(Slicer.PrefixLength("你", "nihao"), 2u);
}
