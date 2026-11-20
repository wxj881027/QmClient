#include <game/client/QmUi/SettingsBindCache.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	constexpr char COMMAND[] = "say settings-command-with-a-long-identity";
	constexpr int CTRL = 1 << KeyModifier::CTRL;
	constexpr int SHIFT = 1 << KeyModifier::SHIFT;
}

TEST(SettingsBindCache, CompleteCacheUsesFirstModifierThenFirstKey)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(KEY_Y, COMMAND));
	ASSERT_TRUE(pBinds->Bind(KEY_X, COMMAND));
	ASSERT_TRUE(pBinds->Bind(KEY_A, COMMAND, false, CTRL));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_TRUE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_X, KeyModifier::NONE));
	EXPECT_EQ(Cache.Find(*pBinds, "say missing-command"), EMPTY_BIND_SLOT);
	EXPECT_EQ(Cache.Find(*pBinds, ""), EMPTY_BIND_SLOT);
}

TEST(SettingsBindCache, UnavailableCachePreservesBothExistingFallbackPriorities)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(KEY_Y, COMMAND));
	ASSERT_TRUE(pBinds->Bind(KEY_X, COMMAND));
	ASSERT_TRUE(pBinds->Bind(KEY_B, COMMAND, false, SHIFT));
	ASSERT_TRUE(pBinds->Bind(KEY_A, COMMAND, false, SHIFT));
	CSettingsBindCache Cache;
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_X, KeyModifier::NONE));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND, CSettingsBindCache::EFallback::LAST_MODIFIER_MATCH), CBindSlot(KEY_A, SHIFT));
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND, CSettingsBindCache::EFallback::LAST_MODIFIER_MATCH), CBindSlot(KEY_X, KeyModifier::NONE));
}

TEST(SettingsBindCache, ExternalReplacementUsesActualBindingsBeforeRebuild)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(KEY_X, COMMAND));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	ASSERT_TRUE(pBinds->Bind(KEY_X, "say replacement"));
	ASSERT_TRUE(pBinds->Bind(KEY_Y, COMMAND));
	EXPECT_FALSE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
	EXPECT_EQ(Cache.Find(*pBinds, "say replacement"), CBindSlot(KEY_X, KeyModifier::NONE));
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_TRUE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
}

TEST(SettingsBindCache, UnbindAllDiscardsTheOldSnapshotAndCanBuildAnEmptyCache)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(KEY_X, COMMAND));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	pBinds->UnbindAll();
	EXPECT_FALSE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), EMPTY_BIND_SLOT);
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_TRUE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), EMPTY_BIND_SLOT);
}

TEST(SettingsBindCache, ClearingOneSlotDisplaysTheRemainingRealBind)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(KEY_X, COMMAND));
	ASSERT_TRUE(pBinds->Bind(KEY_Y, COMMAND));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	ASSERT_TRUE(pBinds->Bind(KEY_X, ""));
	Cache.Invalidate();
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
}

TEST(SettingsBindCache, EqualRevisionsFromDifferentOwnersDoNotReuseTheSnapshot)
{
	auto pFirst = std::make_unique<CBindStorage>();
	auto pSecond = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pFirst->Bind(KEY_X, COMMAND));
	ASSERT_TRUE(pSecond->Bind(KEY_Y, COMMAND));
	ASSERT_EQ(pFirst->Revision(), pSecond->Revision());
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pFirst));
	EXPECT_FALSE(Cache.Ready(*pSecond));
	EXPECT_EQ(Cache.Find(*pSecond, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
	ASSERT_TRUE(Cache.Ensure(*pSecond));
	EXPECT_EQ(Cache.Find(*pSecond, COMMAND), CBindSlot(KEY_Y, KeyModifier::NONE));
}

TEST(SettingsBindCache, InitialReserveDoesNotLimitTheNumberOfCommands)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_LT(80, KEY_LAST);
	for(int Key = 1; Key <= 80; ++Key)
		ASSERT_TRUE(pBinds->Bind(Key, ("say settings-command-" + std::to_string(Key)).c_str()));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	for(int Key = 1; Key <= 80; ++Key)
		EXPECT_EQ(Cache.Find(*pBinds, ("say settings-command-" + std::to_string(Key)).c_str()), CBindSlot(Key, KeyModifier::NONE));
	Cache.Invalidate();
	EXPECT_FALSE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, "say settings-command-80"), CBindSlot(80, KeyModifier::NONE));
}
