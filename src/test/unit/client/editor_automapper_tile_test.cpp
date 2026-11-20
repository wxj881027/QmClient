#include <game/editor/auto_map.h>

#include <gtest/gtest.h>

TEST(EditorAutomapperTile, ReadsTheReferenceRowAndSeesInPlaceEdits)
{
	CTile aReference[6]{};
	aReference[4].m_Index = 17;
	aReference[4].m_Flags = TILEFLAG_ROTATE;
	const CTile &Tile = EditorAutomap::ReadTile(aReference, 3, 2, 1, 1);
	EXPECT_EQ(Tile.m_Index, 17);
	EXPECT_EQ(Tile.m_Flags, TILEFLAG_ROTATE);
	aReference[4].m_Index = 9;
	EXPECT_EQ(Tile.m_Index, 9);
}

TEST(EditorAutomapperTile, MissingReferenceCellsAreEmptyWithoutWrappingIntoAnotherRow)
{
	CTile aReference[2]{};
	aReference[1].m_Index = 7;
	aReference[1].m_Flags = TILEFLAG_XFLIP;
	const CTile &Right = EditorAutomap::ReadTile(aReference, 1, 2, 1, 0);
	EXPECT_EQ(Right.m_Index, 0);
	EXPECT_EQ(Right.m_Flags, 0);
	EXPECT_EQ(EditorAutomap::ReadTile(aReference, 1, 2, 0, 2).m_Index, 0);
}
