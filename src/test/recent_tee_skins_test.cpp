#include <game/client/components/qmclient/recent_tee_skins.h>
#include <game/client/components/qmclient/tee_skin_apply.h>

#include <gtest/gtest.h>

TEST(RecentTeeSkins, ReapplyingAnAppearanceMovesItToTheFront)
{
	CQmRecentTeeSkins History;
	const SQmRecentTeeSkin First{"default", true, 123, 456};
	const SQmRecentTeeSkin Second{"bluekitty", false};
	ASSERT_TRUE(History.Record(First));
	ASSERT_TRUE(History.Record(Second));
	ASSERT_TRUE(History.Record(First));
	ASSERT_EQ(History.Entries().size(), 2u);
	EXPECT_EQ(History.Entries()[0], First);
	EXPECT_EQ(History.Entries()[1], Second);
	const uint64_t Revision = History.Revision();
	EXPECT_FALSE(History.Record(First));
	EXPECT_EQ(History.Revision(), Revision);
}

TEST(RecentTeeSkins, SameSkinWithDifferentCustomColorsKeepsBothAppearances)
{
	CQmRecentTeeSkins History;
	History.Record({"default", true, 123, 456});
	History.Record({"default", true, 789, 456});
	History.Record({"default", false, 789, 456});
	ASSERT_EQ(History.Entries().size(), 3u);
	EXPECT_FALSE(History.Entries()[0].m_UseCustomColor);
	EXPECT_EQ(History.Entries()[1].m_ColorBody, 789u);
	EXPECT_EQ(History.Entries()[2].m_ColorBody, 123u);
}

TEST(RecentTeeSkins, DisabledColorsDoNotCreateDuplicateRecords)
{
	CQmRecentTeeSkins History;
	ASSERT_TRUE(History.Record({"default", false, 123, 456}));
	EXPECT_FALSE(History.Record({"default", false, 987, 654}));
	ASSERT_EQ(History.Entries().size(), 1u);
	EXPECT_EQ(History.Entries()[0].m_ColorBody, 0u);
	EXPECT_EQ(History.Entries()[0].m_ColorFeet, 0u);
}

TEST(RecentTeeSkins, CapacityEvictsTheLeastRecentlyAppliedAppearance)
{
	CQmRecentTeeSkins History;
	for(int Index = 0; Index < 20; ++Index)
		ASSERT_TRUE(History.Record({"skin" + std::to_string(Index), false}));
	History.Record({"skin0", false});
	History.Record({"skin20", false});
	ASSERT_EQ(History.Entries().size(), 20u);
	EXPECT_EQ(History.Entries().front().m_Name, "skin20");
	EXPECT_EQ(History.Entries()[1].m_Name, "skin0");
	EXPECT_EQ(History.Entries().back().m_Name, "skin2");
}

TEST(RecentTeeSkins, InvalidNamesLeaveExistingHistoryUnchanged)
{
	CQmRecentTeeSkins History;
	History.Record({"default", false});
	const uint64_t Revision = History.Revision();
	for(const std::string &Name : {std::string{}, std::string("../default"), std::string("bad\nname"), std::string(MAX_SKIN_LENGTH, 'a')})
	{
		SCOPED_TRACE(Name);
		EXPECT_FALSE(History.Record({Name, false}));
	}
	EXPECT_EQ(History.Revision(), Revision);
	ASSERT_EQ(History.Entries().size(), 1u);
	EXPECT_EQ(History.Entries().front().m_Name, "default");
}

TEST(RecentTeeSkins, SavedEntriesReplayInTheSameOrderWithColors)
{
	CQmRecentTeeSkins History;
	History.Record({"default", false});
	History.Record({"bluekitty", true, 0x123456, 0xabcdef});
	History.Record({"default", true, 0x123456, 0x654321});
	CQmRecentTeeSkins Restored;
	History.ForEachSavedEntry([&](const SQmRecentTeeSkin &Entry) { Restored.Record(Entry); });
	EXPECT_EQ(Restored.Entries(), History.Entries());
	History.ForEachSavedEntry([&](const SQmRecentTeeSkin &Entry) { Restored.Record(Entry); });
	EXPECT_EQ(Restored.Entries(), History.Entries());
}

TEST(RecentTeeSkins, ClearRemovesSavedRecordsAndAllowsRecordingAgain)
{
	CQmRecentTeeSkins History;
	History.Record({"default", false});
	const uint64_t Revision = History.Revision();
	History.Clear();
	EXPECT_TRUE(History.Entries().empty());
	EXPECT_GT(History.Revision(), Revision);
	int SavedCount = 0;
	History.ForEachSavedEntry([&](const SQmRecentTeeSkin &) { ++SavedCount; });
	EXPECT_EQ(SavedCount, 0);
	const uint64_t ClearedRevision = History.Revision();
	History.Clear();
	EXPECT_EQ(History.Revision(), ClearedRevision);
	EXPECT_TRUE(History.Record({"default", false}));
}

TEST(RecentTeeSkins, VisibleOrderKeepsASecondClickOnTheSameAppearance)
{
	const SQmRecentTeeSkin First{"default", false};
	const SQmRecentTeeSkin Second{"bluekitty", false};
	const SQmRecentTeeSkin New{"default", true, 123, 456};
	const std::vector<SQmRecentTeeSkin> Visible{First, Second};
	EXPECT_EQ(QmStableRecentTeeSkinOrder({Second, First}, Visible), Visible);
	EXPECT_EQ(QmStableRecentTeeSkinOrder({New, Second}, Visible), (std::vector<SQmRecentTeeSkin>{New, Second}));
	EXPECT_EQ(QmStableRecentTeeSkinOrder({Second, First}, {}), (std::vector<SQmRecentTeeSkin>{Second, First}));
	EXPECT_TRUE(QmStableRecentTeeSkinOrder({}, Visible).empty());
}

TEST(RecentTeeSkins, MainAndDummyShareHistoryWithoutChangingTheControlledCharacter)
{
	CConfig Config{};
	str_copy(Config.m_ClPlayerSkin, "default");
	str_copy(Config.m_ClDummySkin, "bluekitty");
	Config.m_ClPlayerUseCustomColor = 1;
	Config.m_ClPlayerColorBody = 0x123456;
	Config.m_ClPlayerColorFeet = 0xabcdef;
	Config.m_ClDummyUseCustomColor = 0;
	Config.m_ClDummy = 0;
	const SQmRecentTeeSkin Main = QmCurrentTeeSkin(Config, false);
	const SQmRecentTeeSkin Dummy = QmCurrentTeeSkin(Config, true);
	CQmRecentTeeSkins History;
	History.Record(Main);
	History.Record(Dummy);
	ASSERT_EQ(History.Entries().size(), 2u);
	const SQmRecentTeeSkin Saved = History.Entries()[1];
	QmApplyTeeSkinToTarget(Config, ETeeSkinApplyTarget::DUMMY, Saved.m_Name.c_str(), true, Saved.m_UseCustomColor, Saved.m_ColorBody, Saved.m_ColorFeet);
	EXPECT_EQ(QmCurrentTeeSkin(Config, true), Main);
	EXPECT_EQ(QmCurrentTeeSkin(Config, false), Main);
	EXPECT_EQ(Config.m_ClDummy, 0);
	Config.m_ClPlayerSkin[0] = '\0';
	EXPECT_EQ(QmCurrentTeeSkin(Config, false).m_Name, "default");
}

TEST(RecentTeeSkins, ContinuousEditsCommitOnlyTheLastAppearance)
{
	CQmRecentTeeSkins History;
	History.Stage({"default", true, 123, 456});
	History.Stage({"default", true, 789, 456});
	EXPECT_TRUE(History.Entries().empty());
	History.CommitPending();
	ASSERT_EQ(History.Entries().size(), 1u);
	EXPECT_EQ(History.Entries().front().m_ColorBody, 789u);
	History.CommitPending();
	EXPECT_EQ(History.Entries().size(), 1u);
}

TEST(RecentTeeSkins, ClearingHistoryDiscardsAnUncommittedEdit)
{
	CQmRecentTeeSkins History;
	History.Record({"default", false});
	History.Stage({"bluekitty", true, 123, 456});
	History.Clear();
	History.CommitPending();
	EXPECT_TRUE(History.Entries().empty());
}
