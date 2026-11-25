#include <game/client/components/qmclient/favorite_map_note_edit.h>

#include <gtest/gtest.h>

TEST(QmFavoriteMapNoteEditor, DoubleClickRequiresTwoClicksOnTheSameMap)
{
	CQmFavoriteMapNoteEditor Editor;
	EXPECT_FALSE(Editor.NoteClicked("A", false));
	EXPECT_FALSE(Editor.NoteClicked("B", true));
	EXPECT_TRUE(Editor.NoteClicked("B", true));
	EXPECT_FALSE(Editor.NoteClicked("B", true));
}

TEST(QmFavoriteMapNoteEditor, SwitchingKeepsThePreviousDraftWithItsOwnMap)
{
	CQmFavoriteMapNoteEditor Editor;
	Editor.BeginEdit("A", "original A");
	const auto Change = Editor.Commit("updated A");
	Editor.BeginEdit("B", "original B");

	ASSERT_TRUE(Change);
	EXPECT_EQ(Change->m_Map, "A");
	EXPECT_EQ(Change->m_Note, "updated A");
	EXPECT_TRUE(Editor.IsEditing("B"));
	EXPECT_EQ(Editor.OriginalNote(), "original B");
	EXPECT_TRUE(Editor.TakePendingFocus());
	EXPECT_FALSE(Editor.TakePendingFocus());
}

TEST(QmFavoriteMapNoteEditor, CancelDiscardsTheDraftAndPendingFocus)
{
	CQmFavoriteMapNoteEditor Editor;
	Editor.BeginEdit("A", "original");
	Editor.Cancel();

	EXPECT_FALSE(Editor.IsEditing());
	EXPECT_FALSE(Editor.TakePendingFocus());
	EXPECT_FALSE(Editor.Commit("discarded draft"));
}

TEST(QmFavoriteMapNoteEditor, DeletingTheEditingMapDiscardsTheDraftAndClick)
{
	CQmFavoriteMapNoteEditor Editor;
	Editor.BeginEdit("A", "original");
	EXPECT_FALSE(Editor.NoteClicked("A", false));
	EXPECT_FALSE(Editor.PruneIfRemoved({"A", "B"}));
	EXPECT_TRUE(Editor.PruneIfRemoved({"B"}));

	EXPECT_FALSE(Editor.IsEditing());
	EXPECT_FALSE(Editor.TakePendingFocus());
	EXPECT_FALSE(Editor.Commit("discarded draft"));
	EXPECT_FALSE(Editor.NoteClicked("A", true));
}

TEST(QmFavoriteMapNoteEditor, EmptyDraftCommitsADeletionAndUnchangedEmptyNoteDoesNotWrite)
{
	CQmFavoriteMapNoteEditor Editor;
	Editor.BeginEdit("A", "original");
	const auto Change = Editor.Commit("");
	ASSERT_TRUE(Change);
	EXPECT_EQ(Change->m_Map, "A");
	EXPECT_TRUE(Change->m_Note.empty());

	Editor.BeginEdit("B", nullptr);
	EXPECT_FALSE(Editor.Commit(""));
	EXPECT_FALSE(Editor.IsEditing());
}
