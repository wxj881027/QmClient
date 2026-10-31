#include <game/client/components/qmclient/word_filter_list.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <string>
#include <vector>

TEST(QmWordFilterAllocation, EveryParserAllocationFailurePreservesTheCallerListAndAllowsRetry)
{
	const std::vector<std::string> Expected{std::string(100, 'a'), std::string(100, 'b'), std::string(100, 'c')};
	const std::string Content = Expected[0] + "\n" + Expected[1] + "\n" + Expected[2];
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 128; ++Allocation)
	{
		std::vector<std::string> Words{"old"};
		std::string Error;
		bool Parsed, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Parsed = QmWordFilterList::ParseFile(Content, Words, Error);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Parsed);
			EXPECT_EQ(Words, Expected);
			ReachedSuccess = Parsed;
			break;
		}
		++Failures;
		EXPECT_FALSE(Parsed);
		EXPECT_EQ(Words, (std::vector<std::string>{"old"}));
		ASSERT_TRUE(QmWordFilterList::ParseFile(Content, Words, Error));
		EXPECT_EQ(Words, Expected);
		EXPECT_TRUE(Error.empty());
	}
	EXPECT_GE(Failures, 6);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(QmWordFilterAllocation, FileListPreparationFailurePreservesPublishedWordsAndRevision)
{
	const std::string Content(500, 'a');
	bool ReachedSuccess = false;
	for(int Allocation = 0; Allocation < 128; ++Allocation)
	{
		QmWordFilterList::CFileList File;
		ASSERT_TRUE(File.UpdateFromText("old"));
		const uint64_t Before = File.Revision();
		bool Updated, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Updated = File.UpdateFromText(Content);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Updated);
			EXPECT_EQ(File.Words(), (std::vector<std::string>{Content}));
			ReachedSuccess = Updated;
			break;
		}
		EXPECT_FALSE(Updated);
		EXPECT_EQ(File.Words(), (std::vector<std::string>{"old"}));
		EXPECT_EQ(File.Revision(), Before);
		ASSERT_TRUE(File.UpdateFromText(Content));
		EXPECT_EQ(File.Revision(), Before + 1);
		EXPECT_EQ(File.Words(), (std::vector<std::string>{Content}));
		ASSERT_TRUE(File.UpdateFromText(Content));
		EXPECT_EQ(File.Revision(), Before + 1);
		EXPECT_TRUE(File.Error().empty());
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST(QmWordFilterAllocation, HexPreparationFailureKeepsTheOriginalInlineList)
{
	std::array<char, 1024> aList{};
	aList[0] = 'x';
	std::string Hex;
	for(int i = 0; i < 200; ++i)
		Hex += "61";
	bool Appended, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Appended = QmWordFilterList::AppendInlineHexChunk(aList.data(), aList.size(), Hex.c_str());
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Appended);
	EXPECT_STREQ(aList.data(), "x");
	ASSERT_TRUE(QmWordFilterList::AppendInlineHexChunk(aList.data(), aList.size(), Hex.c_str()));
	EXPECT_EQ(std::string(aList.data()), "x" + std::string(200, 'a'));
}

TEST(QmWordFilterAllocation, FailureToPrepareAValidationErrorDoesNotReplaceTheCallerList)
{
	const std::string Content(QmWordFilterList::MAX_FILE_BYTES + 1, 'a');
	std::vector<std::string> Words{"old"};
	std::string Error;
	bool Parsed, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Parsed = QmWordFilterList::ParseFile(Content, Words, Error);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Parsed);
	EXPECT_EQ(Words, (std::vector<std::string>{"old"}));
	ASSERT_TRUE(QmWordFilterList::ParseFile("new", Words, Error));
	EXPECT_EQ(Words, (std::vector<std::string>{"new"}));
	EXPECT_TRUE(Error.empty());
}

namespace
{
	void CheckSerializationFailures(const std::string &Text)
	{
		std::vector<std::string> Expected;
		ASSERT_TRUE(QmWordFilterList::SerializeInline(Text.c_str(), Expected));
		bool ReachedSuccess = false;
		int Failures = 0;
		for(int Allocation = 0; Allocation < 128; ++Allocation)
		{
			std::vector<std::string> Lines{"old output"};
			bool Serialized, Triggered;
			{
				CPredictionAllocationFailure Failure(Allocation);
				Serialized = QmWordFilterList::SerializeInline(Text.c_str(), Lines);
				Triggered = Failure.Triggered();
			}
			if(!Triggered)
			{
				EXPECT_TRUE(Serialized);
				EXPECT_EQ(Lines, Expected);
				ReachedSuccess = Serialized;
				break;
			}
			++Failures;
			EXPECT_FALSE(Serialized);
			EXPECT_EQ(Lines, (std::vector<std::string>{"old output"}));
			ASSERT_TRUE(QmWordFilterList::SerializeInline(Text.c_str(), Lines));
			EXPECT_EQ(Lines, Expected);
		}
		EXPECT_GT(Failures, 0);
		EXPECT_TRUE(ReachedSuccess);
	}
}

TEST(QmWordFilterAllocation, EverySegmentPreparationFailurePreservesThePreviousOutputAndAllowsRetry)
{
	CheckSerializationFailures(std::string(5000, 'a') + "\n尾部");
}

TEST(QmWordFilterAllocation, EveryQuotedPreparationFailurePreservesThePreviousOutputAndAllowsRetry)
{
	CheckSerializationFailures(std::string(100, 'a') + "\\\"尾部");
}

TEST(QmWordFilterAllocation, InvalidUtf8AndOversizedListsDoNotPublishAClearPrefix)
{
	std::vector<std::string> Lines{"old output"};
	const std::string Invalid(4096, static_cast<char>(0x80));
	EXPECT_FALSE(QmWordFilterList::SerializeInline(Invalid.c_str(), Lines));
	EXPECT_EQ(Lines, (std::vector<std::string>{"old output"}));
	const std::string Oversized(QmWordFilterList::INLINE_CAPACITY, 'a');
	EXPECT_FALSE(QmWordFilterList::SerializeInline(Oversized.c_str(), Lines));
	EXPECT_EQ(Lines, (std::vector<std::string>{"old output"}));
	ASSERT_TRUE(QmWordFilterList::SerializeInline("recovered", Lines));
	EXPECT_EQ(Lines, (std::vector<std::string>{"qm_block_words_list \"recovered\""}));
}

TEST(QmWordFilterAllocation, EmptyListStillSerializesAnExplicitClearCommand)
{
	std::vector<std::string> Lines{"old output"};
	ASSERT_TRUE(QmWordFilterList::SerializeInline("", Lines));
	EXPECT_EQ(Lines, (std::vector<std::string>{"qm_block_words_list \"\""}));
}
