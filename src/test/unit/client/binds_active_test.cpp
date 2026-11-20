#include <game/client/components/binds.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace
{
	using CRecord = CActiveBindStorage::CActiveBind;

	CActiveBindStorage::CId FindKey(const CActiveBindStorage &Binds, int Key)
	{
		return Binds.Find([&](const CRecord &Bind) { return Bind.m_Key == Key; });
	}

	std::vector<int> Keys(CActiveBindStorage &Binds)
	{
		std::vector<int> vKeys;
		Binds.Visit([&](const CRecord &Bind) { vKeys.push_back(Bind.m_Key); });
		return vKeys;
	}
}

TEST(ActiveBinds, PressPublishesRecordOnlyAfterCallbackReturns)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, 1 << KeyModifier::CTRL, [&]() {
		EXPECT_EQ(FindKey(Binds, KEY_X), 0u);
	}));
	const auto Id = FindKey(Binds, KEY_X);
	ASSERT_NE(Id, 0u);
	EXPECT_TRUE(Binds.Update(Id, [](CRecord &Bind) {
		EXPECT_EQ(Bind.m_ModifierMask, 1 << KeyModifier::CTRL);
		return true;
	}));
}

TEST(ActiveBinds, ResetDuringPressPreservesCompletedPressReleaseTracking)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
	EXPECT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, [&]() { Binds.Clear(); }));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_X}));
}

TEST(ActiveBinds, NestedPressKeepsBothRecordsInPublicationOrder)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, [&]() {
		ASSERT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
	}));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_Y, KEY_X}));
}

TEST(ActiveBinds, ThrowingPressLeavesNoRecordAndLaterPressCanPublish)
{
	CActiveBindStorage Binds;
	EXPECT_THROW(Binds.Press(KEY_X, KeyModifier::NONE, []() { throw std::runtime_error("command failed"); }), std::runtime_error);
	EXPECT_EQ(FindKey(Binds, KEY_X), 0u);
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_X}));
}

TEST(ActiveBinds, ResetInsideNestedPressKeepsCompletedAdmissions)
{
	CActiveBindStorage Binds;
	EXPECT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, [&]() {
		EXPECT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, [&]() { Binds.Clear(); }));
		ASSERT_TRUE(Binds.Press(KEY_Z, KeyModifier::NONE, []() {}));
	}));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_Y, KEY_Z, KEY_X}));
}

TEST(ActiveBinds, RetainedShortcutStateIsCommittedAfterCallback)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	const auto Id = FindKey(Binds, KEY_X);
	ASSERT_TRUE(Binds.Update(Id, [](CRecord &Bind) {
		EXPECT_TRUE(Bind.m_ShortcutState.ReleaseWhileUnfocused("+scoreboard;+fire", [](const char *) {}));
		return true;
	}));
	EXPECT_TRUE(Binds.Update(Id, [](CRecord &Bind) {
		EXPECT_TRUE(Bind.m_ShortcutState.WaitingForFocusReturn());
		EXPECT_STREQ(Bind.m_ShortcutState.Command(nullptr), "+scoreboard");
		return true;
	}));
}

TEST(ActiveBinds, ClearingDuringRestrictionDoesNotDestroyCurrentRecord)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	const auto Id = FindKey(Binds, KEY_X);
	int Releases = 0;
	ASSERT_TRUE(Binds.Update(Id, [&](CRecord &Bind) {
		EXPECT_TRUE(Bind.m_ShortcutState.Restrict("+scoreboard;+fire;+left", [&](const char *) {
			++Releases;
			Binds.Clear();
		}));
		EXPECT_STREQ(Bind.m_ShortcutState.Command(nullptr), "+scoreboard");
		return true;
	}));
	EXPECT_EQ(Releases, 2);
	EXPECT_TRUE(Keys(Binds).empty());
}

TEST(ActiveBinds, ClearingAndRepressingSameKeyCannotEraseReplacement)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	const auto OldId = FindKey(Binds, KEY_X);
	ASSERT_TRUE(Binds.Update(OldId, [&](CRecord &) {
		Binds.Clear();
		EXPECT_TRUE(Binds.Press(KEY_X, 1 << KeyModifier::CTRL, []() {}));
		return false;
	}));
	const auto NewId = FindKey(Binds, KEY_X);
	ASSERT_NE(NewId, 0u);
	EXPECT_NE(NewId, OldId);
	EXPECT_FALSE(Binds.Erase(OldId));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_X}));
}

TEST(ActiveBinds, ErasingAndRepressingSameKeyCannotOverwriteReplacementState)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	const auto OldId = FindKey(Binds, KEY_X);
	ASSERT_TRUE(Binds.Update(OldId, [&](CRecord &Bind) {
		EXPECT_TRUE(Bind.m_ShortcutState.Restrict("+scoreboard", [](const char *) {}));
		EXPECT_TRUE(Binds.Erase(OldId));
		EXPECT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
		return true;
	}));
	const auto NewId = FindKey(Binds, KEY_X);
	ASSERT_NE(NewId, 0u);
	EXPECT_TRUE(Binds.Update(NewId, [](CRecord &Bind) {
		EXPECT_FALSE(Bind.m_ShortcutState.Restricted());
		return true;
	}));
}

TEST(ActiveBinds, VisitStopsUsingClearedRecords)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	ASSERT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
	std::vector<int> vVisited;
	Binds.Visit([&](const CRecord &Bind) {
		Binds.Clear();
		vVisited.push_back(Bind.m_Key);
	});
	EXPECT_EQ(vVisited, (std::vector<int>{KEY_X}));
	EXPECT_TRUE(Keys(Binds).empty());
}

TEST(ActiveBinds, VisitDoesNotConsumeRecordsAddedByCallback)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	ASSERT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
	std::vector<int> vVisited;
	Binds.Visit([&](const CRecord &Bind) {
		vVisited.push_back(Bind.m_Key);
		if(Bind.m_Key == KEY_X)
			EXPECT_TRUE(Binds.Press(KEY_Z, KeyModifier::NONE, []() {}));
	});
	EXPECT_EQ(vVisited, (std::vector<int>{KEY_X, KEY_Y}));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_X, KEY_Y, KEY_Z}));
}

TEST(ActiveBinds, VisitCanEraseCurrentRecordWithoutSkippingNext)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	ASSERT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
	std::vector<int> vVisited;
	Binds.Visit([&](const CRecord &Bind) {
		EXPECT_TRUE(Binds.Erase(Bind.m_Id));
		vVisited.push_back(Bind.m_Key);
	});
	EXPECT_EQ(vVisited, (std::vector<int>{KEY_X, KEY_Y}));
	EXPECT_TRUE(Keys(Binds).empty());
}

TEST(ActiveBinds, RestrictionIsVisibleToReentrantQueriesBeforeCallbackReturns)
{
	CActiveBindStorage Binds;
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	const auto Id = FindKey(Binds, KEY_X);
	EXPECT_TRUE(Binds.Update(Id, [&](CRecord &Bind) {
		EXPECT_TRUE(Bind.m_ShortcutState.Restrict("+scoreboard;+fire", [&](const char *) {
			EXPECT_EQ(Binds.Find([](const CRecord &Current) { return Current.m_ShortcutState.Restricted(); }), Id);
		}));
		return true;
	}));
}

TEST(ActiveBinds, ThrowingOuterPressPreservesCompletedNestedPress)
{
	CActiveBindStorage Binds;
	EXPECT_THROW(Binds.Press(KEY_X, KeyModifier::NONE, [&]() {
		EXPECT_TRUE(Binds.Press(KEY_Y, KeyModifier::NONE, []() {}));
		throw std::runtime_error("outer command failed after nested press");
	}),
		std::runtime_error);
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_Y}));
	ASSERT_TRUE(Binds.Press(KEY_X, KeyModifier::NONE, []() {}));
	EXPECT_EQ(Keys(Binds), (std::vector<int>{KEY_Y, KEY_X}));
}

TEST(ActiveBinds, MissingIdentityDoesNotInvokeStateCallback)
{
	CActiveBindStorage Binds;
	EXPECT_FALSE(Binds.Update(0, [](CRecord &) {
		ADD_FAILURE() << "missing identity invoked callback";
		return false;
	}));
	EXPECT_FALSE(Binds.Erase(0));
}
