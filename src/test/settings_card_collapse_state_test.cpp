#include <game/client/QmUi/SettingsCardCollapseState.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	using qm_card_collapse::CState;

	TEST(SettingsCardCollapseState, ParsesAndSerializesStableIdsDeterministically)
	{
		CState State;
		EXPECT_TRUE(State.Load("qm:voice;deck:graphics-icons;qm:voice;;invalid id"));
		EXPECT_TRUE(State.IsCollapsed("qm:voice", false));
		EXPECT_TRUE(State.IsCollapsed("deck:graphics-icons", false));
		EXPECT_TRUE(State.IsCollapsed("invalid id", false));
		EXPECT_FALSE(State.IsCollapsed("deck:graphics-display", false));

		char aSerialized[256];
		ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
		EXPECT_STREQ(aSerialized, "deck:graphics-icons;invalid id;qm:voice");
	}

	TEST(SettingsCardCollapseState, ExplicitExpansionSurvivesReloadAndDefaultChanges)
	{
		CState State;
		ASSERT_TRUE(State.Load("deck:general-game;deck:general-language"));
		EXPECT_TRUE(State.SetCollapsed("deck:general-game", false));
		EXPECT_FALSE(State.IsCollapsed("deck:general-game", false));
		EXPECT_FALSE(State.SetCollapsed("deck:general-game", false));

		char aSerialized[128];
		ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
		CState Reloaded;
		ASSERT_TRUE(Reloaded.Load(aSerialized));
		EXPECT_FALSE(Reloaded.IsCollapsed("deck:general-game", true));
		EXPECT_TRUE(Reloaded.IsCollapsed("deck:general-language", false));
	}

	TEST(SettingsCardCollapseState, UnconfiguredCardsKeepTheirOwnDefaults)
	{
		CState State;
		EXPECT_FALSE(State.IsCollapsed("deck:general-game", false));
		EXPECT_TRUE(State.IsCollapsed("deck:controls-custom", true));
		ASSERT_TRUE(State.SetCollapsed("deck:controls-custom", false));
		EXPECT_FALSE(State.IsCollapsed("deck:controls-custom", true));
	}

	TEST(SettingsCardCollapseState, AllPageFamiliesShareStableIdsAfterReload)
	{
		CState State;
		const char *apStableIds[] = {"deck:general-game", "qm:translate", "deck:tclient-status-bar-settings", "deck:controls-movement"};
		for(const char *pStableId : apStableIds)
			ASSERT_TRUE(State.SetCollapsed(pStableId, true));
		char aSerialized[256];
		ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
		CState SearchState;
		ASSERT_TRUE(SearchState.Load(aSerialized));
		for(const char *pStableId : apStableIds)
			EXPECT_TRUE(SearchState.IsCollapsed(pStableId, false)) << pStableId;
		ASSERT_TRUE(SearchState.SetCollapsed("qm:translate", false));
		ASSERT_TRUE(SearchState.Serialize(aSerialized, sizeof(aSerialized)));
		ASSERT_TRUE(State.Load(aSerialized));
		EXPECT_FALSE(State.IsCollapsed("qm:translate", false));
		EXPECT_TRUE(State.IsCollapsed("deck:controls-movement", false));
	}

	TEST(SettingsCardCollapseState, RejectsIdsThatCannotRoundTrip)
	{
		CState State;
		EXPECT_FALSE(State.SetCollapsed("qm:first;qm:second", true));
		EXPECT_FALSE(State.SetCollapsed("!qm:translate", true));
		EXPECT_FALSE(State.SetCollapsed("qm:translate\n", true));
	}

	TEST(SettingsCardCollapseState, ExactSizeBufferIncludesTerminator)
	{
		CState State;
		ASSERT_TRUE(State.SetCollapsed("a", true));
		char aExact[2];
		ASSERT_TRUE(State.Serialize(aExact, sizeof(aExact)));
		EXPECT_STREQ(aExact, "a");
		char aShort[1];
		EXPECT_FALSE(State.Serialize(aShort, sizeof(aShort)));
		EXPECT_EQ(aShort[0], '\0');
	}

	TEST(SettingsCardCollapseState, RejectsSerializationThatWouldTruncateConfig)
	{
		CState State;
		ASSERT_TRUE(State.Load("a;b;c"));
		char aSerialized[4];
		EXPECT_FALSE(State.Serialize(aSerialized, sizeof(aSerialized)));
	}

	TEST(SettingsCardCollapseState, ImportsLegacyQmKeysAsStableIds)
	{
		CState State;
		EXPECT_TRUE(State.ImportLegacyQm("chat_bubble:legacy;camera_view;unknown;qiafen"));
		EXPECT_TRUE(State.IsCollapsed("qm:chat_bubble", false));
		EXPECT_TRUE(State.IsCollapsed("qm:camera_view", false));
		EXPECT_TRUE(State.IsCollapsed("qm:qiafen", false));
		EXPECT_FALSE(State.IsCollapsed("qm:unknown", false));

		char aSerialized[256];
		ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
		EXPECT_STREQ(aSerialized, "qm:camera_view;qm:chat_bubble;qm:qiafen");
	}

	TEST(SettingsCardCollapseState, EmptyAndNullIdsUseDefaults)
	{
		CState State;
		EXPECT_FALSE(State.SetCollapsed(nullptr, true));
		EXPECT_FALSE(State.SetCollapsed("", true));
		EXPECT_TRUE(State.Load(nullptr));
		EXPECT_TRUE(State.IsCollapsed(nullptr, true));
		EXPECT_FALSE(State.IsCollapsed("", false));
	}
}
