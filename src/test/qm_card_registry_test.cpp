#include "test.h"

#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/localization.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

static qm_card_order::CModel RegistryModelAfterRoundTrip()
{
	const std::vector<qm_card_order::SEntry> Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Source;
	Source.SetEntries(Defaults);
	char aSerialized[32768];
	EXPECT_TRUE(Source.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	EXPECT_TRUE(Reloaded.LoadMerged(aSerialized, Defaults));
	return Reloaded;
}
// 意图：注册表是迁移兜底与 SmartDefaults 的唯一依据，必须全覆盖、无重复、命名权威。
TEST(QmCardRegistry, CoversAllCardsNoDuplicates)
{
	const auto &Reg = qm_card_registry::Defaults();
	std::set<std::string> Ids;
	for(const auto &E : Reg)
	{
		EXPECT_TRUE(Ids.insert(E.m_pStableId).second) << "重复 stableId: " << E.m_pStableId;
		EXPECT_NE(E.m_pTitle, nullptr) << E.m_pStableId;
		EXPECT_NE(E.m_pTitle != nullptr ? E.m_pTitle[0] : '\0', '\0') << E.m_pStableId;
	}
}

TEST(QmCardRegistry, P6QmClientContributorsCards)
{
	const auto *pCommunity = qm_card_registry::FindByStableId("deck:qmclient-contributors-community");
	const auto *pSponsors = qm_card_registry::FindByStableId("deck:qmclient-contributors-sponsors");
	ASSERT_NE(pCommunity, nullptr);
	ASSERT_NE(pSponsors, nullptr);
	EXPECT_STREQ(pCommunity->m_pDefaultTab, "qmclient-contributors");
	EXPECT_STREQ(pSponsors->m_pDefaultTab, "qmclient-contributors");
	EXPECT_EQ(pCommunity->m_DefaultColumn, qm_card_registry::ECardColumn::Left);
	EXPECT_EQ(pSponsors->m_DefaultColumn, qm_card_registry::ECardColumn::Right);
	EXPECT_EQ(pCommunity->m_DefaultOrder, 0);
	EXPECT_EQ(pSponsors->m_DefaultOrder, 0);
}

TEST(QmCardRegistry, BindWheelUsesTwoColumnsByDefault)
{
	const auto *pEditor = qm_card_registry::FindByStableId("deck:tclient-bind-wheel-editor");
	const auto *pPreview = qm_card_registry::FindByStableId("deck:tclient-bind-wheel-preview");
	ASSERT_NE(pEditor, nullptr);
	ASSERT_NE(pPreview, nullptr);
	EXPECT_EQ(pEditor->m_DefaultColumn, qm_card_registry::ECardColumn::Left);
	EXPECT_EQ(pPreview->m_DefaultColumn, qm_card_registry::ECardColumn::Right);
	EXPECT_EQ(pEditor->m_DefaultOrder, 0);
	EXPECT_EQ(pPreview->m_DefaultOrder, 0);
}

// 意图：注册表必须覆盖当前 Tclient 运行时 section 的 stable card id。
// 这些 id 是全局 Search/迁移/默认补位的事实来源，漏掉会让对应卡片从全局卡片系统消失。
TEST(QmCardRegistry, CoversCurrentTClientSectionIds)
{
	const char *apIds[] = {
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	};
	for(const char *pId : apIds)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(pId);
		ASSERT_NE(pDefault, nullptr) << pId;
		EXPECT_STREQ(pDefault->m_pDefaultTab, "tclient") << pId;
	}
}

TEST(QmCardRegistry, TClientMainCardsAlternateColumnsByDefault)
{
	const char *apIds[] = {
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	};
	for(size_t Index = 0; Index < std::size(apIds); ++Index)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(apIds[Index]);
		ASSERT_NE(pDefault, nullptr) << apIds[Index];
		EXPECT_EQ(pDefault->m_DefaultColumn, Index % 2 == 0 ? qm_card_registry::ECardColumn::Left : qm_card_registry::ECardColumn::Right) << apIds[Index];
		EXPECT_EQ(pDefault->m_DefaultOrder, static_cast<int>(Index / 2)) << apIds[Index];
	}
}

TEST(QmCardRegistry, TClientMigrationCommitPlanRetriesOnlyPersistenceFailures)
{
	const auto NotLegacy = qm_card_registry::TClientMainCardsMigrationCommitPlan(qm_card_registry::ETClientMainCardsMigrationResult::NOT_LEGACY);
	EXPECT_FALSE(NotLegacy.m_PersistSerialized);
	EXPECT_TRUE(NotLegacy.m_AdvanceVersion);

	const auto PersistFailed = qm_card_registry::TClientMainCardsMigrationCommitPlan(qm_card_registry::ETClientMainCardsMigrationResult::PERSIST_FAILED);
	EXPECT_FALSE(PersistFailed.m_PersistSerialized);
	EXPECT_FALSE(PersistFailed.m_AdvanceVersion);

	const auto PersistedDirty = qm_card_registry::TClientMainCardsMigrationCommitPlan(qm_card_registry::ETClientMainCardsMigrationResult::PERSISTED_DIRTY);
	EXPECT_TRUE(PersistedDirty.m_PersistSerialized);
	EXPECT_TRUE(PersistedDirty.m_AdvanceVersion);

	const auto Migrated = qm_card_registry::TClientMainCardsMigrationCommitPlan(qm_card_registry::ETClientMainCardsMigrationResult::MIGRATED);
	EXPECT_TRUE(Migrated.m_PersistSerialized);
	EXPECT_TRUE(Migrated.m_AdvanceVersion);
}

TEST(QmCardRegistry, TClientLegacyMigrationSkipsCustomizedLayout)
{
	const auto LegacyEntries = [] {
		return std::vector<qm_card_order::SEntry>{
			{"tclient:visual-font-cursor", "tclient", 1, 0},
			{"tclient:visual-nameplates", "tclient", 1, 1},
			{"tclient:visual-effects", "tclient", 1, 2},
			{"tclient:input", "tclient", 1, 3},
			{"tclient:anti-latency-tools", "tclient", 1, 4},
			{"tclient:improved-anti-ping", "tclient", 1, 5},
			{"tclient:execute-on-join", "tclient", 1, 6},
			{"tclient:voting", "tclient", 1, 7},
			{"tclient:auto-reply", "tclient", 1, 8},
			{"tclient:player-indicator", "tclient", 1, 9},
			{"tclient:pet", "tclient", 1, 10},
			{"tclient:hud", "tclient", 1, 11},
			{"tclient:tee-status-bar", "tclient", 1, 12},
			{"tclient:tile-outlines", "tclient", 1, 13},
			{"tclient:ghost-tools", "tclient", 1, 14},
			{"tclient:rainbow", "tclient", 1, 15},
			{"tclient:tee-trails", "tclient", 1, 16},
			{"tclient:background-draw", "tclient", 1, 17},
			{"tclient:finish-name", "tclient", 1, 18},
		};
	};
	qm_card_order::CModel Legacy;
	Legacy.SetEntries(LegacyEntries());
	EXPECT_TRUE(qm_card_registry::IsTClientMainCardsLegacyLeft(Legacy));
	EXPECT_TRUE(qm_card_registry::MoveTClientMainCardsToAlternatingColumns(Legacy));
	const char *apIds[] = {
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	};
	for(size_t Index = 0; Index < std::size(apIds); ++Index)
	{
		const auto &Entry = Legacy.Entry(Legacy.FindByStableId(apIds[Index]));
		EXPECT_EQ(Entry.m_Column, Index % 2 == 0 ? 1 : 2) << apIds[Index];
		EXPECT_EQ(Entry.m_OrderInColumn, static_cast<int>(Index / 2)) << apIds[Index];
	}

	qm_card_order::CModel Customized;
	Customized.SetEntries(LegacyEntries());
	Customized.MoveToTab("tclient:visual-nameplates", "tclient", 2, 0);
	EXPECT_FALSE(qm_card_registry::IsTClientMainCardsLegacyLeft(Customized));
	EXPECT_FALSE(qm_card_registry::MoveTClientMainCardsToAlternatingColumns(Customized));
	qm_card_order::CModel CustomizedTab;
	CustomizedTab.SetEntries(LegacyEntries());
	CustomizedTab.MoveToTab("tclient:visual-nameplates", "visual", 1, 0);
	EXPECT_FALSE(qm_card_registry::IsTClientMainCardsLegacyLeft(CustomizedTab));
	qm_card_order::CModel CustomizedOrder;
	CustomizedOrder.SetEntries(LegacyEntries());
	CustomizedOrder.Move("tclient:visual-nameplates", 1, 0);
	EXPECT_FALSE(qm_card_registry::IsTClientMainCardsLegacyLeft(CustomizedOrder));

	qm_card_order::CModel WithExtraCard;
	WithExtraCard.SetEntries([&] {
		auto Entries = LegacyEntries();
		Entries.push_back({"deck:general-game", "general", 2, 0});
		return Entries;
	}());
	WithExtraCard.MoveToTab("deck:general-game", "tclient", 2, 0);
	EXPECT_FALSE(qm_card_registry::IsTClientMainCardsLegacyLeft(WithExtraCard));
	EXPECT_FALSE(qm_card_registry::MoveTClientMainCardsToAlternatingColumns(WithExtraCard));
	char aNotLegacy[4096] = "unchanged";
	const uint64_t ExtraRevision = WithExtraCard.LayoutRevision();
	EXPECT_EQ(qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(WithExtraCard, aNotLegacy, sizeof(aNotLegacy)), qm_card_registry::ETClientMainCardsMigrationResult::PERSISTED_DIRTY);
	EXPECT_EQ(WithExtraCard.LayoutRevision(), ExtraRevision);
	EXPECT_TRUE(WithExtraCard.IsDirty());
	EXPECT_NE(std::string(aNotLegacy).find("deck:general-game|tclient|right|0;"), std::string::npos);
	WithExtraCard.ClearDirty();
	str_copy(aNotLegacy, "unchanged", sizeof(aNotLegacy));
	EXPECT_EQ(qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(WithExtraCard, aNotLegacy, sizeof(aNotLegacy)), qm_card_registry::ETClientMainCardsMigrationResult::NOT_LEGACY);
	EXPECT_STREQ(aNotLegacy, "unchanged");

	qm_card_order::CModel FailedMigration;
	FailedMigration.SetEntries(LegacyEntries());
	FailedMigration.ClearDirty();
	const uint64_t CleanRevision = FailedMigration.LayoutRevision();
	char aTooSmall[8];
	EXPECT_EQ(qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(FailedMigration, aTooSmall, sizeof(aTooSmall)), qm_card_registry::ETClientMainCardsMigrationResult::PERSIST_FAILED);
	EXPECT_TRUE(qm_card_registry::IsTClientMainCardsLegacyLeft(FailedMigration));
	EXPECT_FALSE(FailedMigration.IsDirty());
	EXPECT_EQ(FailedMigration.LayoutRevision(), CleanRevision);
	EXPECT_EQ(aTooSmall[0], '\0');

	qm_card_order::CModel DirtyFailedMigration;
	DirtyFailedMigration.SetEntries(LegacyEntries());
	const uint64_t DirtyRevision = DirtyFailedMigration.LayoutRevision();
	EXPECT_TRUE(DirtyFailedMigration.IsDirty());
	EXPECT_EQ(qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(DirtyFailedMigration, aTooSmall, sizeof(aTooSmall)), qm_card_registry::ETClientMainCardsMigrationResult::PERSIST_FAILED);
	EXPECT_TRUE(DirtyFailedMigration.IsDirty());
	EXPECT_EQ(DirtyFailedMigration.LayoutRevision(), DirtyRevision);

	qm_card_order::CModel SuccessfulMigration;
	auto SuccessfulEntries = LegacyEntries();
	SuccessfulEntries.push_back({"deck:general-game", "general", 3, 0});
	SuccessfulMigration.SetEntries(SuccessfulEntries);
	char aSerialized[4096];
	EXPECT_EQ(qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(SuccessfulMigration, aSerialized, sizeof(aSerialized)), qm_card_registry::ETClientMainCardsMigrationResult::MIGRATED);
	EXPECT_FALSE(qm_card_registry::IsTClientMainCardsLegacyLeft(SuccessfulMigration));
	EXPECT_NE(aSerialized[0], '\0');
	EXPECT_NE(std::string(aSerialized).find("deck:general-game|general|3|0;"), std::string::npos);
	qm_card_order::CModel ReloadedMigration;
	EXPECT_TRUE(ReloadedMigration.LoadMerged(aSerialized, SuccessfulEntries));
	for(size_t Index = 0; Index < std::size(apIds); ++Index)
	{
		const auto &Entry = ReloadedMigration.Entry(ReloadedMigration.FindByStableId(apIds[Index]));
		EXPECT_EQ(Entry.m_Column, Index % 2 == 0 ? 1 : 2) << apIds[Index];
		EXPECT_EQ(Entry.m_OrderInColumn, static_cast<int>(Index / 2)) << apIds[Index];
	}
	const auto &UnrelatedEntry = ReloadedMigration.Entry(ReloadedMigration.FindByStableId("deck:general-game"));
	EXPECT_EQ(UnrelatedEntry.m_Column, 3);
	EXPECT_EQ(UnrelatedEntry.m_OrderInColumn, 0);

	qm_card_order::CModel InvalidColumn;
	InvalidColumn.SetEntries({{"deck:general-game", "general", -1, 0}});
	char aInvalid[128] = "stale";
	EXPECT_FALSE(InvalidColumn.Serialize(aInvalid, sizeof(aInvalid)));
	EXPECT_EQ(aInvalid[0], '\0');
}

// 意图：注册表必须覆盖当前 settings deck 卡片 stable id。
// deck 卡原来只有内存顺序，没有长期持久化；全局卡片系统必须给它们默认 placement。
TEST(QmCardRegistry, CoversCurrentSettingsDeckIds)
{
	const char *apIds[] = {
		"deck:general-game",
		"deck:general-language",
		"deck:general-client",
		"deck:general-recording",
		"deck:tee-identity",
		"deck:tee-skin-options",
		"deck:tee-skin-list",
		"deck:graphics-display",
		"deck:player-identity",
		"deck:player-country",
		"deck:graphics-visual",
		"deck:graphics-modes",
		"deck:graphics-interaction",
		"deck:sound-toggle",
		"deck:sound-volume",
		"deck:sound-audio-pack",
		"deck:ddnet-demo",
		"deck:ddnet-gameplay",
		"deck:ddnet-background",
		"deck:ddnet-miscellaneous",
		"deck:tclient-bind-wheel-editor",
		"deck:tclient-bind-wheel-preview",
		"deck:tclient-status-bar-settings",
		"deck:tclient-status-bar-preview",
		"deck:appearance-hud-main",
		"deck:appearance-hud-ddrace",
		"deck:appearance-chat-settings",
		"deck:appearance-chat-messages",
		"deck:appearance-chat-preview",
		"deck:appearance-name-plate-settings",
		"deck:appearance-name-plate-preview",
		"deck:appearance-hook-collision-main",
		"deck:appearance-hook-collision-preview",
		"deck:appearance-info-messages",
		"deck:appearance-laser-enhanced",
		"deck:appearance-laser-colors",
		"deck:appearance-laser-preview",
		"deck:controls-mouse",
		"deck:controls-controller",
		"deck:controls-movement",
		"deck:controls-weapon",
		"deck:controls-voting",
		"deck:controls-chat",
		"deck:controls-dummy",
		"deck:controls-miscellaneous",
		"deck:controls-custom",
	};
	for(const char *pId : apIds)
		ASSERT_NE(qm_card_registry::FindByStableId(pId), nullptr) << pId;
	EXPECT_EQ(qm_card_registry::FindByStableId("deck:tclient-status-bar-items"), nullptr);
}

TEST(QmCardRegistry, ControlsStandardPageCardsPersistInVisualOrder)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "controls", 1), (std::vector<std::string>{"deck:controls-mouse", "deck:controls-controller", "deck:controls-movement", "deck:controls-weapon"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "controls", 2), (std::vector<std::string>{"deck:controls-voting", "deck:controls-chat", "deck:controls-dummy", "deck:controls-miscellaneous", "deck:controls-custom"}));
}

// 意图：General 的默认卡片顺序必须随全局 model 往返持久化，避免重启后列投影或 Search 跳转丢失 placement。
TEST(QmCardRegistry, GeneralStandardPageCardsPersistInVisualOrder)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "general", 1),
		(std::vector<std::string>{"deck:general-game", "deck:general-client"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "general", 2),
		(std::vector<std::string>{"deck:general-language", "deck:general-recording"}));
} // 意图：appearance deck 的默认 placement 必须与运行时子页和列顺序对齐。

// 意图：Player 页的身份和国家选择必须在重启后保持左右两列的默认 placement，
// 否则全局 Search 或自定义排序会丢失目标卡片。
TEST(QmCardRegistry, PlayerStandardPageCardsPersistInVisualOrder)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "player", 1),
		(std::vector<std::string>{"deck:player-identity"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "player", 2),
		(std::vector<std::string>{"deck:player-country"}));
}

// 意图：Tee 页按预览、选项、列表拆卡后，宽屏默认保持预览与选项左右排列、搜索列表全宽。
TEST(QmCardRegistry, TeeStandardPageUsesThreeFunctionalCards)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 0),
		(std::vector<std::string>{"deck:tee-skin-list"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 1),
		(std::vector<std::string>{"deck:tee-identity"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 2),
		(std::vector<std::string>{"deck:tee-skin-options"}));
}

// 意图：Tee 拆卡后搜索词必须落到实际承载功能的卡片，而不是都跳到预览卡。
TEST(QmCardRegistry, TeeFunctionalSearchTargetsSplitCards)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	for(const auto &[pQuery, pExpectedId] : {std::pair{"colors", "deck:tee-skin-options"}, std::pair{"eyes", "deck:tee-skin-options"}, std::pair{"search", "deck:tee-skin-list"}, std::pair{"filter", "deck:tee-skin-list"}})
	{
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [pExpectedId](const auto &Result) {
			return std::string(Result.m_pStableId) == pExpectedId;
		});
		ASSERT_NE(It, vResults.end()) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pTab, "tee");
	}
}

TEST(QmCardRegistry, LegacyMergedFunctionalCardMigratesOnlyOldDefaultGroup)
{
	const std::vector<qm_card_order::SEntry> vLegacyLayout = {
		{"deck:tclient-profiles-actions", "tclient-profiles", 0, 0},
		{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
		{"deck:tclient-profiles-list", "tclient-profiles", 1, 0},
	};
	const std::vector<qm_card_order::SEntry> vTargetLayout = {
		{"deck:tclient-profiles-actions", "tclient-profiles", 1, 0},
		{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
		{"deck:tclient-profiles-list", "tclient-profiles", 1, 1},
	};
	const std::vector<const char *> vAllowedIds = {
		"deck:tclient-profiles-actions",
		"deck:tclient-profiles-options",
		"deck:tclient-profiles-list",
	};
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	const char *pLegacySerialized = "deck:tclient-profiles-actions|tclient-profiles|full|0;";
	Model.LoadMerged(pLegacySerialized, vDefaults);

	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout(pLegacySerialized, vLegacyLayout, {"deck:tclient-profiles-actions"}), qm_card_order::EExplicitLayoutStatus::MATCH);
	EXPECT_TRUE(qm_card_order::MigrateExactLayout(Model, "tclient-profiles", vLegacyLayout, vTargetLayout, vAllowedIds));
	const auto &Entry = Model.Entry(Model.FindByStableId("deck:tclient-profiles-actions"));
	EXPECT_EQ(Entry.m_Column, 1);
	EXPECT_EQ(Entry.m_OrderInColumn, 0);
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-profiles", 1),
		(std::vector<std::string>{"deck:tclient-profiles-actions", "deck:tclient-profiles-list"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-profiles", 2),
		(std::vector<std::string>{"deck:tclient-profiles-options"}));

	qm_card_order::CModel CustomizedModel;
	const char *pCustomizedSerialized = "deck:tclient-profiles-actions|tclient-profiles|right|0;";
	CustomizedModel.LoadMerged(pCustomizedSerialized, vDefaults);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout(pCustomizedSerialized, vLegacyLayout, {"deck:tclient-profiles-actions"}), qm_card_order::EExplicitLayoutStatus::NOT_MATCH);
	EXPECT_FALSE(qm_card_order::MigrateExactLayout(CustomizedModel, "tclient-profiles", vLegacyLayout, vTargetLayout, vAllowedIds));
	EXPECT_EQ(CustomizedModel.Entry(CustomizedModel.FindByStableId("deck:tclient-profiles-actions")).m_Column, 2);
}

TEST(QmCardRegistry, LegacyGroupMigrationRejectsExtraCardInTargetTab)
{
	const std::vector<qm_card_order::SEntry> vLegacyDefaults = {
		{"deck:tclient-status-bar-settings", "tclient-status-bar", 1, 0},
		{"deck:tclient-status-bar-items", "tclient-status-bar", 1, 1},
		{"deck:tclient-status-bar-preview", "tclient-status-bar", 1, 2},
	};
	const std::vector<const char *> vAllowedIds = {
		"deck:tclient-status-bar-settings",
		"deck:tclient-status-bar-items",
		"deck:tclient-status-bar-preview",
	};
	const std::vector<qm_card_order::SEntry> vTargetLayout = {
		{"deck:tclient-status-bar-settings", "tclient-status-bar", 1, 0},
		{"deck:tclient-status-bar-items", "tclient-status-bar", 2, 0},
		{"deck:tclient-status-bar-preview", "tclient-status-bar", 1, 1},
	};
	const char *pLegacySerialized =
		"deck:tclient-status-bar-settings|tclient-status-bar|left|0;"
		"deck:tclient-status-bar-items|tclient-status-bar|left|1;"
		"deck:tclient-status-bar-preview|tclient-status-bar|left|2;";
	qm_card_order::CModel Model;
	auto vEntries = vLegacyDefaults;
	vEntries.push_back({"deck:external", "tclient-status-bar", 2, 0});
	Model.LoadMerged(pLegacySerialized, vEntries);
	const uint64_t Revision = Model.LayoutRevision();

	EXPECT_FALSE(qm_card_order::MigrateExactLayout(Model, "tclient-status-bar", vLegacyDefaults, vTargetLayout, vAllowedIds));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-status-bar", 2),
		(std::vector<std::string>{"deck:external"}));
}

TEST(QmCardRegistry, ExactProfileMigrationRejectsCustomizedCompanionCard)
{
	const std::vector<qm_card_order::SEntry> vLegacyLayout = {
		{"deck:tclient-profiles-actions", "tclient-profiles", 0, 0},
		{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
		{"deck:tclient-profiles-list", "tclient-profiles", 1, 0},
	};
	const std::vector<qm_card_order::SEntry> vTargetLayout = {
		{"deck:tclient-profiles-actions", "tclient-profiles", 1, 0},
		{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
		{"deck:tclient-profiles-list", "tclient-profiles", 1, 1},
	};
	const std::vector<const char *> vAllowedIds = {
		"deck:tclient-profiles-actions",
		"deck:tclient-profiles-options",
		"deck:tclient-profiles-list",
	};
	qm_card_order::CModel Customized;
	Customized.SetEntries(vLegacyLayout);
	Customized.MoveToTab("deck:tclient-profiles-options", "tclient-profiles", 1, 0);
	const uint64_t Revision = Customized.LayoutRevision();
	const auto BeforeLeft = Customized.StableIdOrder("deck:", "tclient-profiles", 1);
	const auto BeforeRight = Customized.StableIdOrder("deck:", "tclient-profiles", 2);

	EXPECT_FALSE(qm_card_order::MigrateExactLayout(Customized, "tclient-profiles", vLegacyLayout, vTargetLayout, vAllowedIds));
	EXPECT_EQ(Customized.LayoutRevision(), Revision);
	EXPECT_EQ(Customized.StableIdOrder("deck:", "tclient-profiles", 1), BeforeLeft);
	EXPECT_EQ(Customized.StableIdOrder("deck:", "tclient-profiles", 2), BeforeRight);

	qm_card_order::CModel Legacy;
	Legacy.SetEntries(vLegacyLayout);
	EXPECT_TRUE(qm_card_order::MigrateExactLayout(Legacy, "tclient-profiles", vLegacyLayout, vTargetLayout, vAllowedIds));
	EXPECT_EQ(Legacy.StableIdOrder("deck:", "tclient-profiles", 1),
		(std::vector<std::string>{"deck:tclient-profiles-actions", "deck:tclient-profiles-list"}));
	EXPECT_EQ(Legacy.StableIdOrder("deck:", "tclient-profiles", 2),
		(std::vector<std::string>{"deck:tclient-profiles-options"}));
}

TEST(QmCardRegistry, ExplicitLayoutClassificationRejectsMissingRequiredAndDuplicateIds)
{
	const std::vector<qm_card_order::SEntry> vExpected = {
		{"actions", "profiles", 0, 0},
		{"options", "profiles", 2, 0},
	};
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("options|profiles|right|0;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::INVALID);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("actions|profiles|full|0;actions|profiles|full|0;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::INVALID);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("actions|profiles|right|0;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::NOT_MATCH);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("actions|profiles|full|0;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::MATCH);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("actions|profiles|full|-1;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::INVALID);
	EXPECT_EQ(qm_card_order::ClassifyExplicitLayout("actions|profiles|full|0|unexpected;", vExpected, {"actions"}), qm_card_order::EExplicitLayoutStatus::INVALID);
}

TEST(QmCardRegistry, ExactMigrationRejectsMalformedTargetWithoutChangingModel)
{
	const std::vector<qm_card_order::SEntry> vExpected = {
		{"a", "tab", 1, 0},
		{"b", "tab", 1, 1},
	};
	const std::vector<qm_card_order::SEntry> vMalformedTarget = {
		{"a", "tab", 2, 0},
		{"b", "tab", 2, 0},
	};
	qm_card_order::CModel Model;
	Model.SetEntries(vExpected);
	const uint64_t Revision = Model.LayoutRevision();
	EXPECT_FALSE(qm_card_order::MigrateExactLayout(Model, "tab", vExpected, vMalformedTarget, {"a", "b"}));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	EXPECT_EQ(Model.StableIdOrder("", "tab", 1), (std::vector<std::string>{"a", "b"}));
}

TEST(QmCardRegistry, ExactMigrationRejectsDuplicateStableIdsWithoutChangingModel)
{
	const std::vector<qm_card_order::SEntry> vExpected = {
		{"a", "tab", 1, 0},
		{"b", "tab", 2, 0},
	};
	const std::vector<qm_card_order::SEntry> vTarget = {
		{"a", "tab", 2, 0},
		{"b", "tab", 1, 0},
	};
	qm_card_order::CModel Model;
	Model.SetEntries({vExpected[0], vExpected[1], vExpected[0]});
	const uint64_t Revision = Model.LayoutRevision();
	EXPECT_FALSE(qm_card_order::MigrateExactLayout(Model, "tab", vExpected, vTarget, {"a", "b"}));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
}

// 意图：Graphics 试点卡片的 placement 已是公共 registry 的事实源；补齐滚动时不得破坏序列化后的列顺序。
TEST(QmCardRegistry, GraphicsPilotPlacementSurvivesSerialization)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "graphics", 1),
		(std::vector<std::string>{"deck:graphics-display", "deck:graphics-visual", "deck:graphics-icons"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "graphics", 2),
		(std::vector<std::string>{"deck:graphics-modes", "deck:graphics-interaction"}));
}

// 意图：Sound 页的音频包卡必须默认位于右列，和运行时 Deck 的双列内容密度保持一致。
TEST(QmCardRegistry, SoundStandardPageCardsPersistInVisualOrder)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "sound", 1),
		(std::vector<std::string>{"deck:sound-toggle", "deck:sound-volume"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "sound", 2),
		(std::vector<std::string>{"deck:sound-audio-pack"}));
}

// 意图：DDNet 标准页的四张卡片按默认视觉顺序分布在左右列，重启后不能改变阅读顺序。
TEST(QmCardRegistry, DDNetStandardPageCardsPersistInVisualOrder)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "ddnet", 1),
		(std::vector<std::string>{"deck:ddnet-demo", "deck:ddnet-gameplay"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "ddnet", 2),
		(std::vector<std::string>{"deck:ddnet-background", "deck:ddnet-miscellaneous"}));
}

// 意图：敌对列表是一个完整编辑器，registry 只能暴露一张全宽卡片，避免卡片化破坏功能布局。
TEST(QmCardRegistry, TClientWarListUsesSingleFullWidthCard)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-warlist", 0),
		(std::vector<std::string>{"deck:tclient-warlist"}));
	EXPECT_TRUE(Model.StableIdOrder("deck:", "tclient-warlist", 1).empty());
	EXPECT_TRUE(Model.StableIdOrder("deck:", "tclient-warlist", 2).empty());

	const auto *pCard = qm_card_registry::FindByStableId("deck:tclient-warlist");
	ASSERT_NE(pCard, nullptr);
	EXPECT_STREQ(pCard->m_pTitle, "War List");
	for(const char *pLegacyStableId : {
		    "deck:tclient-warlist-entries",
		    "deck:tclient-warlist-editor",
		    "deck:tclient-warlist-settings",
		    "deck:tclient-warlist-groups",
		    "deck:tclient-warlist-players",
	    })
		EXPECT_EQ(qm_card_registry::FindByStableId(pLegacyStableId), nullptr);
}

// 意图：旧五卡配置经过新 registry 清洗并写回后，全新实例只能恢复完整敌对列表卡片。
TEST(QmCardRegistry, TClientWarListLegacyCardsStayRemovedAfterFreshReload)
{
	const char *pLegacySerialized =
		"deck:tclient-warlist-entries|tclient-warlist|left|0;"
		"deck:tclient-warlist-editor|tclient-warlist|right|0;"
		"deck:tclient-warlist-settings|tclient-warlist|right|1;"
		"deck:tclient-warlist-groups|tclient-warlist|left|1;"
		"deck:tclient-warlist-players|tclient-warlist|left|2;";
	qm_card_order::CModel Migrated;
	EXPECT_FALSE(Migrated.LoadMerged(pLegacySerialized, qm_card_registry::BuildDefaultEntries()));
	char aSerialized[8192];
	ASSERT_TRUE(Migrated.Serialize(aSerialized, sizeof(aSerialized)));

	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, qm_card_registry::BuildDefaultEntries()));
	EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-warlist", 0),
		(std::vector<std::string>{"deck:tclient-warlist"}));
	EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-warlist", 1).empty());
	EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-warlist", 2).empty());
	for(const char *pLegacyStableId : {
		    "deck:tclient-warlist-entries",
		    "deck:tclient-warlist-editor",
		    "deck:tclient-warlist-settings",
		    "deck:tclient-warlist-groups",
		    "deck:tclient-warlist-players",
	    })
		EXPECT_EQ(Reloaded.FindByStableId(pLegacyStableId), -1);
}

// 意图：状态栏代码已并入预览卡，默认和用户自定义 placement 都只能包含两张实际卡片。
TEST(QmCardRegistry, TClientStatusBarUsesMergedPreviewCard)
{
	qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-status-bar", 1),
		(std::vector<std::string>{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}));
	EXPECT_TRUE(Model.StableIdOrder("deck:", "tclient-status-bar", 2).empty());

	Model.Move("deck:tclient-status-bar-preview", 2, 0);
	char aSerialized[8192];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, qm_card_registry::BuildDefaultEntries()));
	EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 2),
		(std::vector<std::string>{"deck:tclient-status-bar-preview"}));
}

// 意图：旧的独立代码卡在 registry 合并和配置写回后不能在下一次加载时重新出现。
TEST(QmCardRegistry, TClientStatusBarLegacyCodesStayRemovedAfterFreshReload)
{
	const char *pLegacySerialized =
		"deck:tclient-status-bar-settings|tclient-status-bar|left|0;"
		"deck:tclient-status-bar-items|tclient-status-bar|left|1;"
		"deck:tclient-status-bar-preview|tclient-status-bar|left|2;";
	qm_card_order::CModel Migrated;
	EXPECT_TRUE(Migrated.LoadMerged(pLegacySerialized, qm_card_registry::BuildDefaultEntries()));
	char aSerialized[8192];
	ASSERT_TRUE(Migrated.Serialize(aSerialized, sizeof(aSerialized)));

	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, qm_card_registry::BuildDefaultEntries()));
	EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 1),
		(std::vector<std::string>{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}));
	EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 2).empty());
	EXPECT_EQ(Reloaded.FindByStableId("deck:tclient-status-bar-items"), -1);
}

TEST(QmCardRegistry, TClientStatusBarMigrationAcceptsLegacyColonFormat)
{
	const std::vector<qm_card_order::SEntry> vLegacyDefaults = {
		{"status-settings", "tclient-status-bar", 1, 0},
		{"status-items", "tclient-status-bar", 1, 1},
		{"status-preview", "tclient-status-bar", 1, 2},
	};
	const char *pLegacySerialized =
		"status-settings:1:0;"
		"status-items:1:1;"
		"status-preview:1:2;";
	const std::vector<qm_card_order::SEntry> vTargetLayout = {
		{"status-settings", "tclient-status-bar", 1, 0},
		{"status-items", "tclient-status-bar", 2, 0},
		{"status-preview", "tclient-status-bar", 1, 1},
	};
	const std::vector<const char *> vAllowedIds = {"status-settings", "status-items", "status-preview"};
	qm_card_order::CModel Model;
	Model.LoadMerged(pLegacySerialized, vLegacyDefaults);
	EXPECT_TRUE(qm_card_order::MigrateExactLayout(Model, "tclient-status-bar", vLegacyDefaults, vTargetLayout, vAllowedIds));
}

TEST(QmCardRegistry, TeeMigrationOnlyReflowsLegacyDefaultLayout)
{
	const std::vector<qm_card_order::SEntry> vLegacyDefaults = {
		{"deck:tee-identity", "tee", 0, 0},
		{"deck:tee-skin-options", "tee", 1, 0},
		{"deck:tee-skin-list", "tee", 2, 0},
	};
	const char *pLegacySerialized =
		"deck:tee-identity|tee|full|0;"
		"deck:tee-skin-options|tee|left|0;"
		"deck:tee-skin-list|tee|right|0;";
	const std::vector<qm_card_order::SEntry> vTargetLayout = {
		{"deck:tee-identity", "tee", 1, 0},
		{"deck:tee-skin-options", "tee", 2, 0},
		{"deck:tee-skin-list", "tee", 0, 0},
	};
	const std::vector<const char *> vAllowedIds = {
		"deck:tee-identity",
		"deck:tee-skin-options",
		"deck:tee-skin-list",
	};
	qm_card_order::CModel LegacyModel;
	LegacyModel.LoadMerged(pLegacySerialized, qm_card_registry::BuildDefaultEntries());
	EXPECT_TRUE(qm_card_order::MigrateExactLayout(LegacyModel, "tee", vLegacyDefaults, vTargetLayout, vAllowedIds));
	EXPECT_EQ(LegacyModel.StableIdOrder("deck:", "tee", 0), (std::vector<std::string>{"deck:tee-skin-list"}));
	EXPECT_EQ(LegacyModel.StableIdOrder("deck:", "tee", 1), (std::vector<std::string>{"deck:tee-identity"}));
	EXPECT_EQ(LegacyModel.StableIdOrder("deck:", "tee", 2), (std::vector<std::string>{"deck:tee-skin-options"}));

	const char *pCustomizedSerialized =
		"deck:tee-identity|tee|left|0;"
		"deck:tee-skin-options|tee|left|1;"
		"deck:tee-skin-list|tee|right|0;";
	qm_card_order::CModel CustomizedModel;
	CustomizedModel.LoadMerged(pCustomizedSerialized, qm_card_registry::BuildDefaultEntries());
	EXPECT_FALSE(qm_card_order::MigrateExactLayout(CustomizedModel, "tee", vLegacyDefaults, vTargetLayout, vAllowedIds));
	EXPECT_EQ(CustomizedModel.StableIdOrder("deck:", "tee", 1), (std::vector<std::string>{"deck:tee-identity", "deck:tee-skin-options"}));
}

TEST(QmCardRegistry, GlobalCardOrderMaximumValueFitsConsoleCommand)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::string ParsedValue;
	pConsole->Register("qm_global_card_order", "s", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { *static_cast<std::string *>(pUser) = pResult->GetString(0); }, &ParsedValue, "");

	std::string Value(sizeof(g_Config.m_QmGlobalCardOrder) - 1, 'a');
	const std::string Command = "qm_global_card_order \"" + Value + "\"";
	pConsole->ExecuteLine(Command.c_str());
	EXPECT_EQ(ParsedValue, Value);
}

TEST(QmCardRegistry, RegisteredCardsFitGlobalOrderConfig)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
	EXPECT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
}

// 意图：卡片顺序必须经过真实 qmclient.cfg 写盘，并能由全新的配置管理器实例完整恢复。
TEST(QmCardRegistry, GlobalCardOrderSurvivesFreshConfigManagerReload)
{
	struct SConfigRestore
	{
		int m_SaveSettings = g_Config.m_ClSaveSettings;
		std::string m_GlobalCardOrder = g_Config.m_QmGlobalCardOrder;
		~SConfigRestore()
		{
			g_Config.m_ClSaveSettings = m_SaveSettings;
			str_copy(g_Config.m_QmGlobalCardOrder, m_GlobalCardOrder.c_str(), sizeof(g_Config.m_QmGlobalCardOrder));
		}
	} ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	qm_card_order::CModel CustomizedModel;
	const std::vector<qm_card_order::SEntry> Defaults = qm_card_registry::BuildDefaultEntries();
	ASSERT_FALSE(CustomizedModel.LoadMerged("", Defaults));
	// 模拟用户拖拽：Move 后再序列化，不能只验证手写配置字符串能被读取。
	CustomizedModel.Move("deck:tee-skin-list", 0, 0);
	CustomizedModel.Move("deck:tclient-status-bar-settings", 2, 0);
	CustomizedModel.Move("deck:tclient-status-bar-preview", 2, 1);
	CustomizedModel.Move("deck:tclient-profiles-actions", 2, 0);
	CustomizedModel.Move("deck:tclient-profiles-options", 1, 0);
	CustomizedModel.Move("deck:tclient-profiles-list", 2, 1);
	char aCustomizedSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
	ASSERT_TRUE(CustomizedModel.Serialize(aCustomizedSerialized, sizeof(aCustomizedSerialized)));
	const char *pExpected = aCustomizedSerialized;

	{
		std::unique_ptr<IKernel> pKernel(IKernel::Create());
		pKernel->RegisterInterface(pStorage.get(), false);
		IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT).release();
		pKernel->RegisterInterface(pConsole);
		IConfigManager *pConfigManager = CreateConfigManager();
		pKernel->RegisterInterface(pConfigManager);
		pConsole->Init();
		pConfigManager->Init();
		g_Config.m_ClSaveSettings = 1;
		str_copy(g_Config.m_QmGlobalCardOrder, pExpected, sizeof(g_Config.m_QmGlobalCardOrder));
		ASSERT_TRUE(pConfigManager->Save());
	}

	g_Config.m_QmGlobalCardOrder[0] = '\0';
	{
		std::unique_ptr<IKernel> pKernel(IKernel::Create());
		pKernel->RegisterInterface(pStorage.get(), false);
		IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT).release();
		pKernel->RegisterInterface(pConsole);
		IConfigManager *pConfigManager = CreateConfigManager();
		pKernel->RegisterInterface(pConfigManager);
		pConsole->Init();
		pConfigManager->Init();
		ASSERT_TRUE(pConsole->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE));
		EXPECT_STREQ(g_Config.m_QmGlobalCardOrder, pExpected);

		qm_card_order::CModel Reloaded;
		ASSERT_TRUE(Reloaded.LoadMerged(g_Config.m_QmGlobalCardOrder, qm_card_registry::BuildDefaultEntries()));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 0), (std::vector<std::string>{"deck:tee-skin-list"}));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 1), (std::vector<std::string>{"deck:tee-identity"}));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 2), (std::vector<std::string>{"deck:tee-skin-options"}));
		EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 1).empty());
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 2), (std::vector<std::string>{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-profiles", 1), (std::vector<std::string>{"deck:tclient-profiles-options"}));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-profiles", 2), (std::vector<std::string>{"deck:tclient-profiles-actions", "deck:tclient-profiles-list"}));
	}
}

// 否则全局默认补位会把不同 appearance 子页混在同一个 tab 下，或留下 order 空洞。
TEST(QmCardRegistry, AppearanceDeckDefaultsUseSubPagePlacements)
{
	struct SExpectedPlacement
	{
		const char *m_pStableId;
		const char *m_pTab;
		qm_card_registry::ECardColumn m_Column;
		int m_Order;
	};
	const SExpectedPlacement aExpected[] = {
		{"deck:appearance-hud-main", "appearance-hud", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-hud-ddrace", "appearance-hud", qm_card_registry::ECardColumn::Right, 0},
		{"deck:appearance-chat-settings", "appearance-chat", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-chat-messages", "appearance-chat", qm_card_registry::ECardColumn::Right, 0},
		{"deck:appearance-chat-preview", "appearance-chat", qm_card_registry::ECardColumn::Left, 1},
		{"deck:appearance-name-plate-settings", "appearance-name-plate", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-name-plate-preview", "appearance-name-plate", qm_card_registry::ECardColumn::Right, 0},
		{"deck:appearance-hook-collision-main", "appearance-hook-collision", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-hook-collision-preview", "appearance-hook-collision", qm_card_registry::ECardColumn::Right, 0},
		{"deck:appearance-info-messages", "appearance-info-messages", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-laser-enhanced", "appearance-laser", qm_card_registry::ECardColumn::Left, 0},
		{"deck:appearance-laser-colors", "appearance-laser", qm_card_registry::ECardColumn::Left, 1},
		{"deck:appearance-laser-preview", "appearance-laser", qm_card_registry::ECardColumn::Right, 0},
	};
	for(const SExpectedPlacement &Expected : aExpected)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(Expected.m_pStableId);
		ASSERT_NE(pDefault, nullptr) << Expected.m_pStableId;
		EXPECT_STREQ(pDefault->m_pDefaultTab, Expected.m_pTab) << Expected.m_pStableId;
		EXPECT_EQ(pDefault->m_DefaultColumn, Expected.m_Column) << Expected.m_pStableId;
		EXPECT_EQ(pDefault->m_DefaultOrder, Expected.m_Order) << Expected.m_pStableId;
	}
}

// 意图：deck 默认 placement 的 order 是 tab+column 内的局部顺序，不是跨所有设置页的全局序号。
// 否则首次迁移/缺失补位会在每个非 Qm 设置页留下大空洞，拖拽持久化后的默认顺序不稳定。
TEST(QmCardRegistry, SettingsDeckDefaultOrdersAreLocalToTabAndColumn)
{
	std::map<std::string, std::vector<int>> OrdersByGroup;
	for(const auto &E : qm_card_registry::Defaults())
	{
		const std::string Id = E.m_pStableId;
		if(Id.rfind("deck:", 0) != 0)
			continue;

		const std::string Tab = E.m_pDefaultTab != nullptr ? E.m_pDefaultTab : "";
		OrdersByGroup[Tab + "|" + std::to_string(static_cast<int>(E.m_DefaultColumn))].push_back(E.m_DefaultOrder);
	}

	ASSERT_FALSE(OrdersByGroup.empty());
	for(auto &Group : OrdersByGroup)
	{
		std::vector<int> &Orders = Group.second;
		std::sort(Orders.begin(), Orders.end());
		for(size_t i = 0; i < Orders.size(); ++i)
			EXPECT_EQ(Orders[i], static_cast<int>(i)) << Group.first;
	}
}

// 意图：默认全局模型必须从注册表一键构建，包含 Qm/Tclient/deck 全部默认 placement，
// 供首次迁移和新增卡片补位复用，避免各页面各自拼全局配置。
TEST(QmCardRegistry, BuildDefaultEntriesCoversEveryRegisteredCard)
{
	const auto &Reg = qm_card_registry::Defaults();
	const std::vector<qm_card_order::SEntry> Entries = qm_card_registry::BuildDefaultEntries();
	ASSERT_EQ(Entries.size(), Reg.size());

	qm_card_order::CModel Model;
	Model.SetEntries(Entries);
	EXPECT_GE(Model.StateIndexForStableId("qm:chat_bubble"), 0);
	EXPECT_GE(Model.StateIndexForStableId("tclient:auto-reply"), 0);
	EXPECT_GE(Model.StateIndexForStableId("deck:sound-audio-pack"), 0);
	EXPECT_GE(Model.StateIndexForStableId("deck:appearance-chat-preview"), 0);

	char aBuf[8192];
	Model.Serialize(aBuf, sizeof(aBuf));
	const std::string Serialized(aBuf);
	EXPECT_NE(Serialized.find("tclient:auto-reply|tclient|left|4;"), std::string::npos);
	EXPECT_NE(Serialized.find("deck:sound-audio-pack|sound|right|0;"), std::string::npos);
	EXPECT_NE(Serialized.find("deck:appearance-chat-preview|appearance-chat|left|1;"), std::string::npos);
}

// 意图：全局搜索页不能只展示 stableId 占位符，注册表要提供可读标题与搜索关键词。
// 这样用户搜索 "audio pack" / "Audio packs" 时能找到音频包卡片。
TEST(QmCardRegistry, ProvidesSearchMetadataForGlobalCards)
{
	const auto *AudioPack = qm_card_registry::FindByStableId("deck:sound-audio-pack");
	ASSERT_NE(AudioPack, nullptr);
	ASSERT_NE(AudioPack->m_pTitle, nullptr);
	EXPECT_STREQ(AudioPack->m_pTitle, "Audio packs");
	ASSERT_NE(AudioPack->m_pSearchKeywords, nullptr);
	EXPECT_NE(std::string(AudioPack->m_pSearchKeywords).find("audio pack"), std::string::npos);

	const auto *TClientInput = qm_card_registry::FindByStableId("tclient:input");
	ASSERT_NE(TClientInput, nullptr);
	ASSERT_NE(TClientInput->m_pTitle, nullptr);
	EXPECT_STREQ(TClientInput->m_pTitle, "Input");
}

// 意图：公共 Search 的 stable ID、标题和跳转目标都必须来自 registry/model，不能为 Search 建第二张 metadata 表。
TEST(QmCardRegistry, SearchReturnsCanonicalStableIdAndNavigationTarget)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("monitor", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const qm_card_registry::SCardSearchResult &Result) {
		return Result.m_pStableId != nullptr && std::string(Result.m_pStableId) == "deck:graphics-display";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_EQ(std::count_if(Results.begin(), Results.end(), [](const qm_card_registry::SCardSearchResult &Result) {
		return Result.m_pStableId != nullptr && std::string(Result.m_pStableId) == "deck:graphics-display";
	}),
		1);
	EXPECT_STREQ(It->m_Target.m_pTab, "graphics");
	EXPECT_STREQ(It->m_Target.m_pStableId, It->m_pStableId);
	EXPECT_EQ(It->m_Description, Localize("Window and monitor"));
}

// 意图：历史/裁剪 model 缺少卡片时，Search 必须使用 registry 默认 tab，保证导航目标仍有效。
TEST(QmCardRegistry, SearchFallsBackToDefaultTabWhenModelDoesNotContainCard)
{
	qm_card_order::CModel Model;
	const auto Results = qm_card_registry::SearchCards("monitor", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const qm_card_registry::SCardSearchResult &Result) {
		return Result.m_pStableId != nullptr && std::string(Result.m_pStableId) == "deck:graphics-display";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "graphics");
}

// 意图：tab 是 model placement 而不是 Search metadata；移动后 Search 必须使用当前 tab，且不复制 registry 条目。
TEST(QmCardRegistry, SearchUsesCurrentModelTabWithoutDuplicatingMetadata)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	Model.MoveToTab("deck:graphics-display", "appearance-hud", 2, 0);
	const auto Results = qm_card_registry::SearchCards("monitor", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const qm_card_registry::SCardSearchResult &Result) {
		return Result.m_pStableId != nullptr && std::string(Result.m_pStableId) == "deck:graphics-display";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "appearance-hud");
}
// 意图：全局搜索页替代旧 QmClient 模块搜索后，registry 必须承接旧中文/拼音功能词。
// 否则用户搜索旧入口能搜到的具体功能时，会在新 Search 页漏掉对应 qm:* 卡片。
TEST(QmCardRegistry, QmCardsPreserveLegacyModuleSearchKeywords)
{
	struct SExpectedKeyword
	{
		const char *m_pStableId;
		const char *m_pKeyword;
	};
	const SExpectedKeyword aExpected[] = {
		{"qm:mini_features", "粒子拖尾"},
		{"qm:mini_features", "候选栏"},
		{"qm:friend_notify", "自动刷新"},
		{"qm:block_words", "屏蔽词"},
		{"qm:speedrun_timer", "速通"},
		{"qm:voice", "按住说话"},
		{"qm:background_3d", "月牙"},
		{"qm:chat_bubble", "消息气泡"},
	};
	for(const SExpectedKeyword &Expected : aExpected)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(Expected.m_pStableId);
		ASSERT_NE(pDefault, nullptr) << Expected.m_pStableId;
		ASSERT_NE(pDefault->m_pSearchKeywords, nullptr) << Expected.m_pStableId;
		EXPECT_NE(std::string(pDefault->m_pSearchKeywords).find(Expected.m_pKeyword), std::string::npos)
			<< Expected.m_pStableId << " missing keyword " << Expected.m_pKeyword;
	}
}

// 意图：QiaFen 三名分裂（枚举 QiaFen / UI 名 keyword_reply / 持久化 key qiafen）是迁移最大陷阱。
// 注册表必须以持久化 key 为权威，否则迁移丢用户布局。
TEST(QmCardRegistry, QiaFenUsesPersistentKeyNotUiName)
{
	const auto *E = qm_card_registry::FindByStableId("qm:qiafen");
	ASSERT_NE(E, nullptr);
	EXPECT_EQ(std::string(E->m_pStableId), "qm:qiafen"); // 不是 qm:keyword_reply
}

// 意图：laser/nameplate_text 原本未归入 tab 分组（数据债），注册表必须给它们 tab。
TEST(QmCardRegistry, DataDebtCardsHaveTabAssignment)
{
	EXPECT_NE(qm_card_registry::FindByStableId("qm:laser")->m_pDefaultTab, nullptr);
	EXPECT_NE(qm_card_registry::FindByStableId("qm:nameplate_text")->m_pDefaultTab, nullptr);
}

// 意图：调试模式卡片必须挂在 HUD 页，携带可搜索的中文/拼音关键词，且模块枚举可反查。
TEST(QmCardRegistry, DebugModeCardRegisteredInHudTab)
{
	const auto *pDefault = qm_card_registry::FindByStableId("qm:debug_mode");
	ASSERT_NE(pDefault, nullptr);
	EXPECT_STREQ(pDefault->m_pDefaultTab, "hud");
	EXPECT_EQ(pDefault->m_DefaultColumn, qm_card_registry::ECardColumn::Right);
	ASSERT_NE(pDefault->m_pSearchKeywords, nullptr);
	EXPECT_NE(std::string(pDefault->m_pSearchKeywords).find("调试模式"), std::string::npos);
	qm_module::EQmModuleId Id;
	ASSERT_TRUE(qm_module::QmModuleIdFromStableId("qm:debug_mode", &Id));
	EXPECT_EQ(Id, qm_module::EQmModuleId::DebugMode);
}

// 意图：全局卡片的默认 placement 必须包含 tab，否则搜索结果和按 tab 筛选没有稳定落点。
TEST(QmCardRegistry, EveryRegisteredCardHasDefaultTabPlacement)
{
	for(const auto &E : qm_card_registry::Defaults())
	{
		EXPECT_NE(E.m_pDefaultTab, nullptr) << E.m_pStableId;
		if(E.m_pDefaultTab != nullptr)
			EXPECT_NE(E.m_pDefaultTab[0], '\0') << E.m_pStableId;
	}
}

// 意图：B2 迁移要把栖梦旧 key（chat_bubble）映射到新 stableId（qm:chat_bubble）。
// 映射函数从注册表派生（DRY）；QiaFen 以持久化 key qiafen 为权威，UI 名 keyword_reply 不映射。
TEST(QmCardRegistry, MigratesLegacyKeyToNamespaced)
{
	EXPECT_EQ(std::string(qm_card_registry::MigrateLegacyKey("chat_bubble")), "qm:chat_bubble");
	EXPECT_EQ(std::string(qm_card_registry::MigrateLegacyKey("qiafen")), "qm:qiafen");
	EXPECT_EQ(qm_card_registry::MigrateLegacyKey("keyword_reply"), nullptr); // UI 名不映射
}

// 意图：栖梦 38 个 m_pKey 必须全部可映射（迁移兜底全覆盖，无遗漏）。
TEST(QmCardRegistry, AllQimengLegacyKeysMigratable)
{
	for(const auto &E : qm_card_registry::Defaults())
	{
		std::string Id = E.m_pStableId;
		if(Id.rfind("qm:", 0) == 0)
		{
			std::string Legacy = Id.substr(3);
			EXPECT_NE(qm_card_registry::MigrateLegacyKey(Legacy.c_str()), nullptr)
				<< "未映射的栖梦 key: " << Legacy;
		}
	}
}
