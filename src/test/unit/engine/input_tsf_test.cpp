#include <engine/client/input_tsf.h>
#include <engine/shared/qm_ime_policy.h>

#include <gtest/gtest.h>
#include <test/support/qm_tsf_test.h>

#include <limits>
#include <string>
#include <vector>

using qm_tsf_test::CreateCandidates;

TEST(QmTsfCandidates, UnequalLastPageAcceptsCompleteSFalsePageTable)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings = {L"零", L"一", L"二", L"三", L"四", L"五", L"六"};
	pElement->m_vPageStarts = {0, 2, 5};
	pElement->m_CurrentPage = 2;
	pElement->m_Selection = 6;
	// 微软示例在页表缓冲区恰好够用时仍返回 S_FALSE。
	pElement->m_PageResult = S_FALSE;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"五", "六"}));
	EXPECT_EQ(Result->m_SelectedIndex, 1);
	EXPECT_EQ(Result->m_PageStart, 5);
	EXPECT_EQ(Result->m_PageSize, 2);
	EXPECT_EQ(Result->m_PageIndex, 2);
	EXPECT_EQ(Result->m_PageCount, 3);
	EXPECT_EQ(Result->m_TotalCount, 7);
	EXPECT_EQ(pElement->m_StringCalls, 2);
}

TEST(QmTsfCandidates, NoSelectionIgnoresUndefinedOutputIndex)
{
	const auto pElement = CreateCandidates();
	pElement->m_SelectionResult = S_FALSE;
	pElement->m_Selection = std::numeric_limits<UINT>::max();

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_SelectedIndex, -1);
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"你", "好", "吗"}));
}

TEST(QmTsfCandidates, CountSentinelDoesNotCreateAnEmptyLastPage)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings = {L"甲", L"乙", L"丙", L"丁"};
	pElement->m_vPageStarts = {0, 2, 4};
	pElement->m_CurrentPage = 1;
	pElement->m_Selection = 3;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"丙", "丁"}));
	EXPECT_EQ(Result->m_PageStart, 2);
	EXPECT_EQ(Result->m_PageSize, 2);
	EXPECT_EQ(Result->m_PageIndex, 1);
	EXPECT_EQ(Result->m_PageCount, 2);
	EXPECT_EQ(Result->m_SelectedIndex, 1);
}

TEST(QmTsfCandidates, EmptyListDoesNotReadUnavailablePageMetadata)
{
	const auto pElement = CreateCandidates();
	pElement->m_vStrings.clear();
	pElement->m_SelectionResult = E_FAIL;
	pElement->m_PageResult = E_FAIL;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(Result->m_vCandidates.empty());
	EXPECT_EQ(Result->m_TotalCount, 0);
	EXPECT_EQ(Result->m_SelectedIndex, -1);
	EXPECT_EQ(pElement->m_MetadataCalls, 0);
	EXPECT_EQ(pElement->m_StringCalls, 0);
}

TEST(QmTsfCandidates, FailedStringDiscardsAlreadyReadCandidates)
{
	const auto pElement = CreateCandidates();
	pElement->m_FailedStringIndex = 1;

	const auto Result = QmReadTsfCandidates(pElement.Get());

	EXPECT_FALSE(Result.has_value());
	EXPECT_EQ(pElement->m_StringCalls, 2);
}

TEST(QmTsfCandidates, InvalidPageStartsRejectTheSnapshot)
{
	const std::vector<std::vector<UINT>> vvInvalidPages{{1}, {0, 0}, {0, 4}};
	for(size_t i = 0; i < vvInvalidPages.size(); ++i)
	{
		SCOPED_TRACE(i);
		const auto pElement = CreateCandidates();
		pElement->m_vPageStarts = vvInvalidPages[i];

		EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
		EXPECT_EQ(pElement->m_StringCalls, 0);
	}
}

TEST(QmTsfCandidates, PageCountGrowthRejectsIncompletePageTable)
{
	const auto pElement = CreateCandidates();
	pElement->m_ExtraReturnedPages = 1;
	pElement->m_PageResult = S_FALSE;

	EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
	EXPECT_EQ(pElement->m_StringCalls, 0);
}

TEST(QmTsfCandidates, CurrentPageOutsidePageTableRejectsTheSnapshot)
{
	const auto pElement = CreateCandidates();
	pElement->m_CurrentPage = 1;

	EXPECT_FALSE(QmReadTsfCandidates(pElement.Get()).has_value());
	EXPECT_EQ(pElement->m_StringCalls, 0);
}
