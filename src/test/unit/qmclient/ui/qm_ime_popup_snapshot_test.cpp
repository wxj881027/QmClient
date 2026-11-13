#include <game/client/qm_ime_manager.h>

#include <gtest/gtest.h>
#include <test/support/qm_ime_test_input.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class QmImePopupBuild : public ::testing::Test
	{
	protected:
		SQmImeCandidateList m_Candidates{{"候选", "第二项"}, 1, 10, 5, 23};
		qm_ui_test::CTestImeInput m_Input{m_Candidates};
		const vec2 m_Anchor{320.0f, 180.0f};
	};

	SQmImePopupState VisibleState(const char *pText)
	{
		SQmImePopupState State;
		State.m_Visible = true;
		State.m_Composition = "composition";
		State.m_vCandidates = {pText};
		State.m_SelectedIndex = 0;
		State.m_PageIndex = 2;
		State.m_PageCount = 5;
		State.m_AnchorScreen = vec2(320.0f, 180.0f);
		State.m_LineHeightScreen = 18.0f;
		return State;
	}
}

TEST_F(QmImePopupBuild, SnapshotOwnsTextAndRetainsPageAndAnchorMetadata)
{
	m_Input.m_Composition = "ni hao";
	const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f);
	ASSERT_TRUE(Result.has_value());
	m_Candidates.m_vCandidates.clear();
	m_Input.m_Composition.clear();
	EXPECT_TRUE(QmImeHasPopupContent(*Result));
	EXPECT_EQ(Result->m_Composition, "ni hao");
	EXPECT_EQ(Result->m_vCandidates, (std::vector<std::string>{"候选", "第二项"}));
	EXPECT_EQ(Result->m_SelectedIndex, 1);
	EXPECT_EQ(Result->m_PageIndex, 2);
	EXPECT_EQ(Result->m_PageCount, 5);
	EXPECT_FLOAT_EQ(Result->m_AnchorScreen.x, 320.0f);
	EXPECT_FLOAT_EQ(Result->m_AnchorScreen.y, 180.0f);
	EXPECT_FLOAT_EQ(Result->m_LineHeightScreen, 18.0f);
}

TEST_F(QmImePopupBuild, DisabledInputDoesNotReadOrCopyText)
{
	int Copies = 0;
	const auto Copy = [&](const char *pText) {
		++Copies;
		return std::string(pText);
	};
	const auto Result = QmImeBuildPopupState(&m_Input, false, m_Anchor, 18.0f, Copy);
	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(Result->m_Disabled);
	EXPECT_FALSE(QmImeHasPopupContent(*Result));
	EXPECT_EQ(Copies, 0);
	EXPECT_EQ(m_Input.m_CandidateCountReads, 0);
	EXPECT_EQ(m_Input.m_CandidateReads, 0);
}

TEST_F(QmImePopupBuild, MissingInputBuildsADisabledStateWithoutCopying)
{
	int Copies = 0;
	const auto Result = QmImeBuildPopupState(nullptr, true, m_Anchor, 18.0f, [&](const char *pText) {
		++Copies;
		return std::string(pText);
	});
	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(Result->m_Disabled);
	EXPECT_FALSE(QmImeHasPopupContent(*Result));
	EXPECT_EQ(Copies, 0);
}

TEST_F(QmImePopupBuild, EmptyCandidatesDoNotCopyTheComposition)
{
	m_Candidates.m_vCandidates.clear();
	m_Input.m_Composition = "composition without candidates";
	int Copies = 0;
	const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f, [&](const char *pText) {
		++Copies;
		return std::string(pText);
	});
	ASSERT_TRUE(Result.has_value());
	EXPECT_FALSE(Result->m_Disabled);
	EXPECT_FALSE(Result->m_Visible);
	EXPECT_TRUE(Result->m_Composition.empty());
	EXPECT_EQ(Copies, 0);
	EXPECT_EQ(m_Input.m_CandidateReads, 0);
}

TEST_F(QmImePopupBuild, EmptyCompositionStillShowsTheCandidatePage)
{
	m_Input.m_Composition.clear();
	m_Candidates.m_SelectedIndex = -1;
	const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f);
	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(QmImeHasPopupContent(*Result));
	EXPECT_EQ(Result->m_SelectedIndex, 0);
	EXPECT_EQ(Result->m_vCandidates, m_Candidates.m_vCandidates);
}

TEST_F(QmImePopupBuild, PageKeepsCandidatesBeyondTheDrawingViewport)
{
	m_Candidates.m_vCandidates.clear();
	for(int i = 0; i < 20; ++i)
		m_Candidates.m_vCandidates.push_back("candidate " + std::to_string(i));
	m_Candidates.m_SelectedIndex = 19;
	const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f);
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(Result->m_vCandidates, m_Candidates.m_vCandidates);
	EXPECT_EQ(Result->m_SelectedIndex, 19);
	EXPECT_EQ(m_Input.m_CandidateReads, 20);
}

TEST_F(QmImePopupBuild, LargePageMetadataDoesNotOverflowThePageCount)
{
	constexpr int Max = std::numeric_limits<int>::max();
	m_Candidates.m_vCandidates = {"last candidate"};
	m_Candidates.m_TotalCount = Max;
	m_Candidates.m_PageStart = Max - 1;
	for(const auto &Counts : {std::pair{Max - 1, 2}, std::pair{1, Max}})
	{
		SCOPED_TRACE(Counts.first);
		m_Candidates.m_PageSize = Counts.first;
		const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f);
		ASSERT_TRUE(Result.has_value());
		EXPECT_EQ(Result->m_PageCount, Counts.second);
		EXPECT_EQ(Result->m_PageIndex, Counts.second - 1);
	}
}

TEST_F(QmImePopupBuild, MetadataWithoutMultiplePagesHasNoPageLabel)
{
	for(const auto &Counts : {std::pair{0, 9}, std::pair{-1, 9}, std::pair{5, 5}, std::pair{5, 3}, std::pair{5, 0}})
	{
		SCOPED_TRACE(::testing::Message() << Counts.first << ":" << Counts.second);
		m_Candidates.m_PageSize = Counts.first;
		m_Candidates.m_TotalCount = Counts.second;
		const auto Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f);
		ASSERT_TRUE(Result.has_value());
		EXPECT_EQ(Result->m_PageCount, 0);
		EXPECT_EQ(Result->m_PageIndex, -1);
	}
}

TEST_F(QmImePopupBuild, CompositionCopyExceptionsReturnNoSnapshot)
{
	for(int Kind : {0, 1, 2})
	{
		SCOPED_TRACE(Kind);
		std::optional<SQmImePopupState> Result;
		EXPECT_NO_THROW(Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f, [Kind](const char *) -> std::string {
			switch(Kind)
			{
			case 0: throw std::bad_alloc();
			case 1: throw std::length_error("IME presentation copy");
			default: throw 7;
			}
		}));
		EXPECT_FALSE(Result.has_value());
	}
	EXPECT_EQ(m_Input.m_CandidateReads, 0);
}

TEST_F(QmImePopupBuild, CandidateCopyFailureDoesNotPublishThePreparedPrefix)
{
	int Copies = 0;
	std::optional<SQmImePopupState> Result;
	EXPECT_NO_THROW(Result = QmImeBuildPopupState(&m_Input, true, m_Anchor, 18.0f, [&](const char *pText) {
		if(++Copies == 3)
			throw std::bad_alloc();
		return std::string(pText);
	}));
	EXPECT_FALSE(Result.has_value());
	EXPECT_EQ(Copies, 3);
	EXPECT_EQ(m_Input.m_CandidateReads, 2);
	EXPECT_EQ(m_Candidates.m_vCandidates, (std::vector<std::string>{"候选", "第二项"}));
}

TEST(QmImePopupSnapshot, VisibleUpdateOwnsAndReplacesTheCompletePayload)
{
	CQmImePopupSnapshot Snapshot;
	std::optional<SQmImePopupState> State = VisibleState("first");
	EXPECT_EQ(Snapshot.Update(std::move(State)), CQmImePopupSnapshot::EUpdateResult::VISIBLE);
	State->m_vCandidates = {"changed source"};
	State->m_Composition.clear();
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"first"}));
	EXPECT_EQ(Snapshot.State().m_Composition, "composition");
	State = VisibleState("replacement");
	State->m_PageIndex = 4;
	EXPECT_EQ(Snapshot.Update(std::move(State)), CQmImePopupSnapshot::EUpdateResult::VISIBLE);
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"replacement"}));
	EXPECT_EQ(Snapshot.State().m_PageIndex, 4);
}

TEST(QmImePopupSnapshot, ClosingOrDisablingKeepsTheOwnedPageForFadeOut)
{
	for(bool Disabled : {false, true})
	{
		SCOPED_TRACE(Disabled);
		CQmImePopupSnapshot Snapshot;
		ASSERT_EQ(Snapshot.Update(VisibleState("last page")), CQmImePopupSnapshot::EUpdateResult::VISIBLE);
		std::optional<SQmImePopupState> Hidden{SQmImePopupState{}};
		Hidden->m_Disabled = Disabled;
		EXPECT_EQ(Snapshot.Update(std::move(Hidden)), CQmImePopupSnapshot::EUpdateResult::HIDDEN);
		EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"last page"}));
		EXPECT_EQ(Snapshot.State().m_PageIndex, 2);
		EXPECT_EQ(Snapshot.State().m_PageCount, 5);
		EXPECT_FLOAT_EQ(Snapshot.State().m_AnchorScreen.x, 320.0f);
	}
}

TEST(QmImePopupSnapshot, ResetDiscardsThePreviousCandidateAndComposition)
{
	CQmImePopupSnapshot Snapshot;
	ASSERT_EQ(Snapshot.Update(VisibleState("old candidate")), CQmImePopupSnapshot::EUpdateResult::VISIBLE);
	Snapshot.Reset();
	EXPECT_FALSE(QmImeHasPopupContent(Snapshot.State()));
	EXPECT_TRUE(Snapshot.State().m_Composition.empty());
	EXPECT_TRUE(Snapshot.State().m_vCandidates.empty());
	EXPECT_EQ(Snapshot.State().m_SelectedIndex, -1);
	EXPECT_EQ(Snapshot.State().m_PageIndex, -1);
	EXPECT_EQ(Snapshot.State().m_PageCount, 0);
	EXPECT_FLOAT_EQ(Snapshot.State().m_LineHeightScreen, 0.0f);
}
