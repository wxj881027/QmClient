#include <game/client/components/menus.h>

#include <gtest/gtest.h>

namespace
{
	class CTestDemoBrowserMenus : public CMenus
	{
	public:
		using CMenus::CDemoItem;
		using CMenus::SORT_LENGTH;
		using CMenus::SORT_MARKERS;
	};
}

TEST(QmDemoBrowserMetadataOrder, KeepsReplaysWithLengthAheadOfItemsWithoutLength)
{
	CTestDemoBrowserMenus::CDemoItem Replay{};
	Replay.m_InfosLoaded = true;
	Replay.m_Valid = true;
	str_copy(Replay.m_aFilename, "run.demo");
	CTestDemoBrowserMenus::CDemoItem Photo{};
	str_copy(Photo.m_aFilename, "group.poses");

	for(bool Descending : {false, true})
	{
		EXPECT_TRUE(Replay.SortLess(Photo, CTestDemoBrowserMenus::SORT_LENGTH, Descending));
		EXPECT_FALSE(Photo.SortLess(Replay, CTestDemoBrowserMenus::SORT_LENGTH, Descending));
	}
}

TEST(QmDemoBrowserMetadataOrder, OrdersUnloadedEntriesByNameWhenSortingMarkers)
{
	CTestDemoBrowserMenus::CDemoItem First{};
	str_copy(First.m_aFilename, "first.demo");
	CTestDemoBrowserMenus::CDemoItem Second{};
	str_copy(Second.m_aFilename, "second.demo");

	EXPECT_TRUE(First.SortLess(Second, CTestDemoBrowserMenus::SORT_MARKERS, false));
	EXPECT_TRUE(Second.SortLess(First, CTestDemoBrowserMenus::SORT_MARKERS, true));
}
