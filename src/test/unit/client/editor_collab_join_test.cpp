#include <game/editor/collab_join.h>

#include <gtest/gtest.h>

#include <initializer_list>

TEST(EditorCollabJoin, FailedInitialSnapshotKeepsUploadsDisabledUntilRetrySucceeds)
{
	int Revision = 0;
	bool Joined = false;
	int Loads = 0;
	const auto Load = [&](const char *pSnapshot, int RoomRevision) {
		EXPECT_STREQ(pSnapshot, "snapshot");
		EXPECT_EQ(RoomRevision, 6);
		EXPECT_EQ(Revision, 0);
		EXPECT_FALSE(Joined);
		return ++Loads == 2;
	};
	EXPECT_FALSE(EditorCollab::AcceptJoinSnapshot("snapshot", 6, false, Revision, Joined, Load));
	EXPECT_FALSE(Joined);
	EXPECT_EQ(Revision, 0);
	EXPECT_TRUE(EditorCollab::AcceptJoinSnapshot("snapshot", 6, false, Revision, Joined, Load));
	EXPECT_TRUE(Joined);
	EXPECT_EQ(Revision, 6);
	EXPECT_EQ(Loads, 2);
}

TEST(EditorCollabJoin, InitialSnapshotIsAppliedEvenIfRevisionAlreadyMatches)
{
	int Revision = 0;
	bool Joined = false;
	int Loads = 0;
	EXPECT_TRUE(EditorCollab::AcceptJoinSnapshot("snapshot", 0, false, Revision, Joined, [&](const char *, int) {
		++Loads;
		return true;
	}));
	EXPECT_EQ(Loads, 1);
	EXPECT_TRUE(Joined);
}

TEST(EditorCollabJoin, EmptyRoomAllowsJoiningWithoutLoadingAMap)
{
	int Revision = 7;
	bool Joined = false;
	EXPECT_TRUE(EditorCollab::AcceptJoinSnapshot("", 0, false, Revision, Joined, [](const char *, int) {
		ADD_FAILURE() << "Unexpected snapshot load";
		return false;
	}));
	EXPECT_TRUE(Joined);
	EXPECT_EQ(Revision, 0);
}

TEST(EditorCollabJoin, RejoiningAnUnchangedRoomPreservesLocalEdits)
{
	for(int RoomRevision : {4, 5})
	{
		int Revision = 5;
		bool Joined = false;
		EXPECT_TRUE(EditorCollab::AcceptJoinSnapshot("snapshot", RoomRevision, true, Revision, Joined, [](const char *, int) {
			ADD_FAILURE() << "Unexpected snapshot load";
			return false;
		}));
		EXPECT_TRUE(Joined);
		EXPECT_EQ(Revision, 5);
	}
}

TEST(EditorCollabJoin, FailedNewerSnapshotOnReconnectDoesNotEnableUploads)
{
	int Revision = 3;
	bool Joined = false;
	EXPECT_FALSE(EditorCollab::AcceptJoinSnapshot("snapshot", 4, true, Revision, Joined, [](const char *, int) { return false; }));
	EXPECT_FALSE(Joined);
	EXPECT_EQ(Revision, 3);
}
