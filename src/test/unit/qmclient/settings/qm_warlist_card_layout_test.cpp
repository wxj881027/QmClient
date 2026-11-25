#include <game/client/QmUi/cards/QmWarListCardModel.h>

#include <gtest/gtest.h>

using namespace qm_warlist_card;

TEST(QmWarListCardLayout, WideViewBalancesWorkflowAcrossTwoColumns)
{
	const CUIRect Block{10.0f, 20.0f, 480.0f, 0.0f};
	const auto Columns = ResolveColumns(Block.w, 480.0f, 20.0f);
	const std::array<float, SECTION_COUNT> aHeights = {80.0f, 30.0f, 40.0f, 50.0f, 25.0f};
	std::array<CUIRect, SECTION_COUNT> aSections;

	EXPECT_FLOAT_EQ(Columns.Place(Block, aHeights, aSections), 155.0f);
	EXPECT_FLOAT_EQ(aSections[QUICK_ADD].w, 230.0f);
	EXPECT_FLOAT_EQ(aSections[GROUPS].x, 10.0f);
	EXPECT_FLOAT_EQ(aSections[GROUPS].y, 120.0f);
	EXPECT_FLOAT_EQ(aSections[ENTRIES].x, 260.0f);
	EXPECT_FLOAT_EQ(aSections[ENTRIES].y, 20.0f);
	EXPECT_FLOAT_EQ(aSections[EDITOR].y, 80.0f);
	EXPECT_FLOAT_EQ(aSections[RULES].y, 150.0f);
}

TEST(QmWarListCardLayout, NarrowViewKeepsSectionOrderAndFullWidth)
{
	const CUIRect Block{10.0f, 20.0f, 479.0f, 0.0f};
	const auto Columns = ResolveColumns(Block.w, 480.0f, 20.0f);
	const std::array<float, SECTION_COUNT> aHeights = {80.0f, 30.0f, 40.0f, 50.0f, 25.0f};
	std::array<CUIRect, SECTION_COUNT> aSections;

	EXPECT_FLOAT_EQ(Columns.Place(Block, aHeights, aSections), 305.0f);
	for(const auto &Section : aSections)
	{
		EXPECT_FLOAT_EQ(Section.x, Block.x);
		EXPECT_FLOAT_EQ(Section.w, Block.w);
	}
	EXPECT_FLOAT_EQ(aSections[ENTRIES].y, 170.0f);
	EXPECT_FLOAT_EQ(aSections[RULES].y, 300.0f);
}

TEST(QmWarListCardLayout, FriendsShareTheOnlineNameAndClanSearch)
{
	for(const auto Source : {ESource::ONLINE, ESource::FRIENDS})
	{
		EXPECT_TRUE(HasPlayerSearch(Source));
		EXPECT_TRUE(MatchesCandidateFilter(Source, "Alice", "Runners", "ali"));
		EXPECT_TRUE(MatchesCandidateFilter(Source, "Alice", "Runners", "RUN"));
		EXPECT_FALSE(MatchesCandidateFilter(Source, "Alice", "Runners", "Bob"));
	}
	EXPECT_FALSE(HasPlayerSearch(ESource::SELF));
	EXPECT_TRUE(MatchesCandidateFilter(ESource::SELF, "Alice", "Runners", "Bob"));
	EXPECT_FALSE(HasPlayerSearch(ESource::DUMMY));
	EXPECT_TRUE(MatchesCandidateFilter(ESource::DUMMY, "Alice", "Runners", "Bob"));
}
