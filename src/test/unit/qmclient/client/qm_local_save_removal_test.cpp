#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/local_saves.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <array>
#include <string>
#include <string_view>

namespace
{
	class QmLocalSaveRemoval : public ::testing::Test
	{
	protected:
		using TString = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
		CTrackedAllocationResource m_Memory;
		const std::string m_Header = "Time,Players,Map,Code\n";
		const std::string m_Removed = "2026,\"Main, Dummy\",Map,remove\n";
		const std::string m_Kept = "2025," + std::string(512, 'p') + ",Other,keep\n";
		const std::string m_Input = m_Header + m_Removed + m_Kept;
		const std::string m_Expected = m_Header + m_Kept;

		void ExpectFailureThenRetry()
		{
			int AllocationPoints = 0;
			{
				TString Probe("previous", CBorrowedAllocator<char>(&m_Memory));
				const int Before = m_Memory.m_Allocations;
				ASSERT_TRUE(QmLocalSaves::RemoveEntries(m_Input, "Map", "remove", Probe));
				AllocationPoints = m_Memory.m_Allocations - Before;
			}
			ASSERT_GT(AllocationPoints, 0);
			// 扫描实际候选分配点，包含已有前缀后的扩容，不假定字符串增长策略。
			for(int Point = 1; Point <= AllocationPoints; ++Point)
			{
				SCOPED_TRACE(Point);
				m_Memory.m_FailAt = -1;
				{
					TString Output("previous", CBorrowedAllocator<char>(&m_Memory));
					const int LiveBefore = m_Memory.m_Live;
					m_Memory.m_FailAt = m_Memory.m_Allocations + Point;
					EXPECT_FALSE(QmLocalSaves::RemoveEntries(m_Input, "Map", "remove", Output));
					EXPECT_EQ(std::string_view(Output.data(), Output.size()), "previous");
					EXPECT_EQ(m_Memory.m_Live, LiveBefore);
					m_Memory.m_FailAt = -1;
					ASSERT_TRUE(QmLocalSaves::RemoveEntries(m_Input, "Map", "remove", Output));
					EXPECT_EQ(std::string_view(Output.data(), Output.size()), m_Expected);
				}
				EXPECT_EQ(m_Memory.m_Live, 0);
			}
		}
	};
}

TEST_F(QmLocalSaveRemoval, CandidateAllocationFailurePreservesOutputAndAllowsRetry)
{
	ExpectFailureThenRetry();
}

TEST_F(QmLocalSaveRemoval, NullCandidateAllocationPreservesOutputAndAllowsRetry)
{
	m_Memory.m_ReturnNull = true;
	ExpectFailureThenRetry();
}

TEST_F(QmLocalSaveRemoval, UnknownCandidateAllocationExceptionPreservesOutputAndAllowsRetry)
{
	m_Memory.m_ThrowUnknown = true;
	ExpectFailureThenRetry();
}

TEST_F(QmLocalSaveRemoval, InputMayReferToTheOutputWithoutBeingClearedDuringPreparation)
{
	std::string Output = m_Input;
	const std::string_view Input(Output);
	ASSERT_TRUE(QmLocalSaves::RemoveEntries(Input, "Map", "remove", Output));
	EXPECT_EQ(Output, m_Expected);
}

TEST_F(QmLocalSaveRemoval, NoMatchRetainsTheExistingUnchangedDocumentOutputContract)
{
	std::string Output = "previous";
	EXPECT_FALSE(QmLocalSaves::RemoveEntries(m_Input, "Map", "missing", Output));
	EXPECT_EQ(Output, m_Input);
}

TEST(QmLocalSaveRemovalBytes, RemovesEveryMatchingRowAndPreservesOtherBytes)
{
	const std::string Header = "\xef\xbb\xbfTime,Players,Map,Code\r\n";
	const std::string Malformed = "bad,row\r\n";
	const std::string Other = "2023,\"Main, Dummy\",Other,key\r\n";
	const std::string Input = Header + "2025,\"Main, Dummy\",Map,key\r\n" + Malformed + "2024,\"Main, Dummy\",MAP,key\n" + Other;
	std::string Output;
	ASSERT_TRUE(QmLocalSaves::RemoveEntries(Input, "map", "key", Output));
	EXPECT_EQ(Output, Header + Malformed + Other);
}

TEST_F(QmLocalSaveRemoval, EmptyMapOrCodeCannotDeleteARecord)
{
	for(const auto &Keys : {std::array<std::string_view, 2>{"", "remove"}, std::array<std::string_view, 2>{"Map", ""}})
	{
		SCOPED_TRACE(Keys[0]);
		std::string Output = "previous";
		EXPECT_FALSE(QmLocalSaves::RemoveEntries(m_Input, Keys[0], Keys[1], Output));
		EXPECT_EQ(Output, m_Input);
	}
}

TEST(QmLocalSaveRemovalBytes, EmptyInputPublishesAnEmptyUnchangedDocument)
{
	std::string Output = "previous";
	EXPECT_FALSE(QmLocalSaves::RemoveEntries("", "Map", "code", Output));
	EXPECT_TRUE(Output.empty());
}
