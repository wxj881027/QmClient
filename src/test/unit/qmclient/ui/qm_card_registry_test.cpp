#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/test.h>

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

namespace
{
	// 默认布局的两列卡片数与估计高度。测试只核对均衡性质（两列都非空、高度同量级），
	// 不逐卡写死列/序——加卡时不需要改这些断言。
	struct STabColumnBalance
	{
		int m_Counts[2] = {0, 0};
		int m_Heights[2] = {0, 0};
	};

	STabColumnBalance DefaultColumnBalance(const char *pTab)
	{
		STabColumnBalance Balance;
		const std::string Tab(pTab != nullptr ? pTab : "");
		for(const qm_card_order::SEntry &Entry : qm_card_registry::BuildDefaultEntries())
		{
			if(Entry.m_Column == 0 || Entry.m_pDefaultTab == nullptr || Tab != Entry.m_pDefaultTab)
				continue;
			const int Side = Entry.m_Column - 1;
			Balance.m_Counts[Side] += 1;
			Balance.m_Heights[Side] += qm_card_registry::DefaultCardHeightWeight(Entry.m_pStableId);
		}
		return Balance;
	}
} // namespace

TEST(QmCardRegistrySearch, MapRouteSettingFindsItsPlayerStatsCardBeforeOpeningIt)
{
	const auto Model = RegistryModelAfterRoundTrip();
	for(const char *pQuery : {"地图", "地图点状调试路径", "Show dotted map route debug", "Progress bar location", "qm_player_stats_map_progress_dbg_route"})
	{
		SCOPED_TRACE(pQuery);
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
			return std::string(Result.m_pStableId) == "qm:player_stats";
		});
		ASSERT_NE(It, Results.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "hud");
		EXPECT_STREQ(It->m_Target.m_pStableId, "qm:player_stats");
	}
}

TEST(QmCardRegistrySearch, InternalSettingLabelsAndConfigNamesFindTheContainingCard)
{
	const auto Model = RegistryModelAfterRoundTrip();
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pCard;
	};
	const SCase aCases[] = {
		{"Emoticon projectile duration", "qm:skin_appearance"},
		{"qm_emoticon_projectile_duration", "qm:skin_appearance"},
		{"Skin outline for other players", "qm:skin_appearance"},
		{"qm_skin_outline_others", "qm:skin_appearance"},
		{"Show key press counts", "qm:input_overlay"},
		{"qm_input_overlay_key_counts", "qm:input_overlay"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Results = qm_card_registry::SearchCards(Case.m_pQuery, Model);
		EXPECT_TRUE(std::any_of(Results.begin(), Results.end(), [&](const auto &Result) {
			return std::string(Result.m_pStableId) == Case.m_pCard;
		}));
	}
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

TEST(QmCardRegistry, BetterScoreboardCardIsRegisteredInFunctionColumn)
{
	const auto *pCard = qm_card_registry::FindByStableId("qm:better_scoreboard");
	ASSERT_NE(pCard, nullptr);
	EXPECT_STREQ(pCard->m_pDefaultTab, "function");
	EXPECT_STREQ(pCard->m_pTitle, "Better scoreboard");

	const auto &vFunctionCards = qm_card_catalog::FunctionCardStableIds();
	const auto BetterScoreboardIt = std::find_if(vFunctionCards.begin(), vFunctionCards.end(), [](const char *pStableId) {
		return pStableId != nullptr && std::string(pStableId) == "qm:better_scoreboard";
	});
	EXPECT_NE(BetterScoreboardIt, vFunctionCards.end());

	// 默认 placement 来自瀑布：落在 function 页的半宽列且只出现一次。
	const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement("qm:better_scoreboard");
	ASSERT_NE(pPlacement, nullptr);
	EXPECT_STREQ(pPlacement->m_pDefaultTab, "function");
	EXPECT_GT(pPlacement->m_Column, 0);
	EXPECT_GE(pPlacement->m_OrderInColumn, 0);
}

TEST(QmCardRegistry, BetterScoreboardSearchTargetsItsFunctionCard)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"更好的计分板", "计分板积分检查", "显示死亡后计分板", "滚动计分板"})
	{
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [](const auto &Result) {
			return Result.m_pStableId != nullptr && std::string(Result.m_pStableId) == "qm:better_scoreboard";
		});
		ASSERT_NE(It, vResults.end()) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pTab, "function");
		EXPECT_STREQ(It->m_Target.m_pStableId, "qm:better_scoreboard");
	}
}

TEST(QmModuleLayoutAdapter, BetterScoreboardStableIdRoundTrips)
{
	// 注：EQmModuleId 及其 stableId 查询都在 qm_module 命名空间内，
	// 与 qm_module_layout_adapter_test.cpp 一致，必须显式限定。
	qm_module::EQmModuleId Id = qm_module::EQmModuleId::Info;
	ASSERT_TRUE(qm_module::QmModuleIdFromStableId("qm:better_scoreboard", &Id));
	EXPECT_STREQ(qm_module::QmModuleStableId(Id), "qm:better_scoreboard");
}

TEST(QmCardRegistry, P6QmClientContributorsCards)
{
	// 贡献者页分两个子页签 deck：栖梦（社区/头衔/赞助）、友链（友链 + DDNet/TClient 署名，全卡半宽）；
	// 配置文件卡在常规页；「TClient 链接」卡已并入开发人员卡。
	const auto *pCommunity = qm_card_registry::FindByStableId("deck:qmclient-contributors-community");
	const auto *pSponsors = qm_card_registry::FindByStableId("deck:qmclient-contributors-sponsors");
	const auto *pSponsorInfo = qm_card_registry::FindByStableId("deck:qmclient-contributors-sponsor-info");
	const auto *pDdnet = qm_card_registry::FindByStableId("deck:qmclient-contributors-ddnet");
	const auto *pTitle = qm_card_registry::FindByStableId("deck:qmclient-contributors-title");
	const auto *pTcDevelopers = qm_card_registry::FindByStableId("deck:tclient-info-developers");
	const auto *pTcFiles = qm_card_registry::FindByStableId("deck:tclient-info-files");
	const auto *pFriendLinks = qm_card_registry::FindByStableId("deck:credits-friend-links");
	ASSERT_NE(pTitle, nullptr);
	EXPECT_STREQ(pTitle->m_pDefaultTab, "credits-qmclient");
	ASSERT_NE(pCommunity, nullptr);
	ASSERT_NE(pSponsors, nullptr);
	EXPECT_EQ(pSponsorInfo, nullptr);
	ASSERT_NE(pDdnet, nullptr);
	ASSERT_NE(pTcDevelopers, nullptr);
	ASSERT_NE(pTcFiles, nullptr);
	ASSERT_NE(pFriendLinks, nullptr);
	EXPECT_STREQ(pCommunity->m_pDefaultTab, "credits-qmclient");
	EXPECT_STREQ(pSponsors->m_pDefaultTab, "credits-qmclient");
	EXPECT_STREQ(pFriendLinks->m_pDefaultTab, "credits-links");
	EXPECT_STREQ(pDdnet->m_pDefaultTab, "credits-links");
	EXPECT_STREQ(pTcDevelopers->m_pDefaultTab, "credits-links");
	EXPECT_STREQ(pTcFiles->m_pDefaultTab, "general");
	// 列/序由瀑布分配：这里只锁定 tab 归属与默认 placement 的一致性。
	for(const qm_card_registry::SCardDefault *pCard : {pCommunity, pSponsors, pDdnet, pTcDevelopers, pTcFiles, pFriendLinks})
	{
		ASSERT_NE(pCard, nullptr);
		const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement(pCard->m_pStableId);
		ASSERT_NE(pPlacement, nullptr) << pCard->m_pStableId;
		EXPECT_STREQ(pPlacement->m_pDefaultTab, pCard->m_pDefaultTab) << pCard->m_pStableId;
	}
	EXPECT_EQ(qm_card_registry::FindByStableId("deck:tclient-info-links"), nullptr);
	EXPECT_EQ(qm_card_registry::FindByStableId("deck:tclient-info-tabs"), nullptr);
}

TEST(QmCardRegistry, BindWheelUsesTwoColumnsByDefault)
{
	const qm_card_order::SEntry *pEditor = qm_card_registry::FindDefaultPlacement("deck:tclient-bind-wheel-editor");
	const qm_card_order::SEntry *pPreview = qm_card_registry::FindDefaultPlacement("deck:tclient-bind-wheel-preview");
	ASSERT_NE(pEditor, nullptr);
	ASSERT_NE(pPreview, nullptr);
	EXPECT_STREQ(pEditor->m_pDefaultTab, "tclient-bind-wheel");
	EXPECT_STREQ(pPreview->m_pDefaultTab, "tclient-bind-wheel");
	// 编辑器和预览各占一列：默认布局是双列而不是堆叠。
	EXPECT_GT(pEditor->m_Column, 0);
	EXPECT_GT(pPreview->m_Column, 0);
	EXPECT_NE(pEditor->m_Column, pPreview->m_Column);
}

// 意图：注册表必须覆盖当前 Tclient 运行时 section 的 stable card id，并写明每张卡的当前归属页。
// 归属页是可变的重构结果（预测相关三卡已迁到 DDNet 页的「网络补偿」子页签），所以断言
// 「注册表覆盖 + 归属正确」，不再用一句「全都属于 tclient」把页面结构焊死。
TEST(QmCardRegistry, CoversCurrentTClientSectionIds)
{
	struct SExpectation
	{
		const char *m_pStableId;
		const char *m_pTab;
	};
	const SExpectation aIds[] = {
		{"tclient:visual-font-cursor", "tclient"},
		{"tclient:visual-nameplates", "tclient"},
		{"tclient:visual-effects", "tclient"},
		// 预测相关三卡：实现仍是 TClient 卡，归属页是 DDNet 页的「网络补偿」子页签。
		{"tclient:input", "ddnet-prediction"},
		{"tclient:anti-latency-tools", "ddnet-prediction"},
		{"tclient:improved-anti-ping", "ddnet-prediction"},
		{"tclient:execute-on-join", "tclient"},
		{"tclient:voting", "tclient"},
		{"tclient:auto-reply", "tclient"},
		{"tclient:player-indicator", "tclient"},
		{"tclient:pet", "tclient"},
		{"tclient:hud", "tclient"},
		{"tclient:tee-status-bar", "tclient"},
		{"tclient:tile-outlines", "tclient"},
		{"tclient:ghost-tools", "tclient"},
		{"tclient:rainbow", "tclient"},
		{"tclient:tee-trails", "tclient"},
		{"tclient:background-draw", "tclient"},
		{"tclient:finish-name", "tclient"},
	};
	for(const SExpectation &Expectation : aIds)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(Expectation.m_pStableId);
		ASSERT_NE(pDefault, nullptr) << Expectation.m_pStableId;
		EXPECT_STREQ(pDefault->m_pDefaultTab, Expectation.m_pTab) << Expectation.m_pStableId;
	}
}

// 意图：Tclient 页卡片全部登记在同一 tab 下，默认瀑布把两列都填满并保持高度接近，
// 不再依赖"逐张交替"的手写列号。页面成员从 CatalogPageStableIds()（生产事实）取，
// 迁移走的卡片不应再留在 TClient 页。
TEST(QmCardRegistry, TClientMainCardsFillBothDefaultColumns)
{
	const auto &vIds = qm_card_catalog::CatalogPageStableIds("tclient");
	ASSERT_FALSE(vIds.empty());
	for(const char *pId : vIds)
	{
		const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement(pId);
		ASSERT_NE(pPlacement, nullptr) << pId;
		EXPECT_STREQ(pPlacement->m_pDefaultTab, "tclient") << pId;
		EXPECT_GT(pPlacement->m_Column, 0) << pId;
	}
	// 预测相关三卡整卡迁到 DDNet 页「网络补偿」子页签，TClient 页不再有它们的副本。
	for(const char *pMoved : {"tclient:input", "tclient:anti-latency-tools", "tclient:improved-anti-ping"})
	{
		const bool StillInTClient = std::any_of(vIds.begin(), vIds.end(), [pMoved](const char *pId) { return str_comp(pId, pMoved) == 0; });
		EXPECT_FALSE(StillInTClient) << pMoved;
	}
	const STabColumnBalance Balance = DefaultColumnBalance("tclient");
	EXPECT_GT(Balance.m_Counts[0], 0);
	EXPECT_GT(Balance.m_Counts[1], 0);
	EXPECT_LE(std::max(Balance.m_Heights[0], Balance.m_Heights[1]), std::min(Balance.m_Heights[0], Balance.m_Heights[1]) * 2);
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
		"deck:tee-skin-queue",
		"deck:tee-glow",
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
		"deck:appearance-scoreboard",
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

// 意图：标准页的默认 placement 必须随全局 model 往返持久化，避免重启后列投影或 Search 跳转丢失卡片。
// 期望值同样来自默认模型：不逐卡写死列/序，加卡时这条测试不需要改动。
TEST(QmCardRegistry, StandardPageCardsPersistInVisualOrder)
{
	const std::vector<qm_card_order::SEntry> Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel DefaultModel;
	DefaultModel.SetEntries(Defaults);
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	for(const char *pTab : {"controls", "general", "player"})
	{
		SCOPED_TRACE(pTab);
		bool AnyCard = false;
		for(int Column = 0; Column < 3; ++Column)
		{
			const auto vExpected = DefaultModel.StableIdOrder("deck:", pTab, Column);
			AnyCard = AnyCard || !vExpected.empty();
			EXPECT_EQ(Model.StableIdOrder("deck:", pTab, Column), vExpected);
		}
		EXPECT_TRUE(AnyCard) << pTab;
	}
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

// 意图：页面默认 placement 由注册表瀑布决定，序列化往返后每个页面每列的卡片序列都不变。
TEST(QmCardRegistry, PageDefaultPlacementSurvivesSerialization)
{
	const std::vector<qm_card_order::SEntry> Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel DefaultModel;
	DefaultModel.SetEntries(Defaults);
	qm_card_order::CModel Source;
	Source.SetEntries(Defaults);
	char aSerialized[32768];
	ASSERT_TRUE(Source.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, Defaults));
	for(const char *pTab : {"visual", "function", "hud", "controls", "general", "player", "tee", "graphics", "sound", "ddnet", "tclient",
		    "appearance-chat", "appearance-hud", "appearance-name-plate", "appearance-laser", "appearance-hook-collision", "tclient-status-bar"})
	{
		SCOPED_TRACE(pTab);
		for(int Column = 0; Column < 3; ++Column)
			EXPECT_EQ(Reloaded.StableIdOrder("", pTab, Column), DefaultModel.StableIdOrder("", pTab, Column));
	}
}

// 意图：实体层调整必须归到 DDNet 页、可被目录页取到，且具备 deck 渲染器。
TEST(QmCardRegistry, DDNetEntityOverlayBelongsToDdnetPage)
{
	const auto *pCard = qm_card_registry::FindByStableId("qm:entity_overlay");
	ASSERT_NE(pCard, nullptr);
	EXPECT_STREQ(pCard->m_pDefaultTab, "ddnet");
	EXPECT_STREQ(pCard->m_pTitle, "Entity layer adjustments");
	const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement("qm:entity_overlay");
	ASSERT_NE(pPlacement, nullptr);
	EXPECT_STREQ(pPlacement->m_pDefaultTab, "ddnet");
	EXPECT_GT(pPlacement->m_Column, 0);
	const auto &vDDNetCards = qm_card_catalog::CatalogPageStableIds("ddnet");
	EXPECT_NE(std::find_if(vDDNetCards.begin(), vDDNetCards.end(), [](const char *pStableId) { return std::string(pStableId) == "qm:entity_overlay"; }), vDDNetCards.end());
	const auto &vVisualCards = qm_card_catalog::VisualCardStableIds();
	EXPECT_EQ(std::find_if(vVisualCards.begin(), vVisualCards.end(), [](const char *pStableId) { return std::string(pStableId) == "qm:entity_overlay"; }), vVisualCards.end());
	EXPECT_TRUE(qm_card_catalog::HasCardModule("qm:entity_overlay"));
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
	// 三列合起来恰好两张卡：设置卡 + 合并后的预览卡（左右列由瀑布分配，不写死在哪一列）。
	std::vector<std::string> vCards;
	for(int Column = 0; Column < 3; ++Column)
	{
		const auto vColumn = Model.StableIdOrder("deck:", "tclient-status-bar", Column);
		vCards.insert(vCards.end(), vColumn.begin(), vColumn.end());
	}
	ASSERT_EQ(vCards.size(), 2u);
	EXPECT_NE(std::find(vCards.begin(), vCards.end(), std::string("deck:tclient-status-bar-settings")), vCards.end());
	EXPECT_NE(std::find(vCards.begin(), vCards.end(), std::string("deck:tclient-status-bar-preview")), vCards.end());
	EXPECT_EQ(Model.FindByStableId("deck:tclient-status-bar-items"), -1);

	// 用户把预览卡拖到左列首位后必须原样保存（默认布局里它在另一列）。
	Model.Move("deck:tclient-status-bar-preview", 1, 0);
	char aSerialized[8192];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, qm_card_registry::BuildDefaultEntries()));
	EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 1),
		(std::vector<std::string>{"deck:tclient-status-bar-preview", "deck:tclient-status-bar-settings"}));
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
	CustomizedModel.Move("deck:tclient-profiles", 2, 0);
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
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 0), CustomizedModel.StableIdOrder("deck:", "tee", 0));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 1), CustomizedModel.StableIdOrder("deck:", "tee", 1));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tee", 2), CustomizedModel.StableIdOrder("deck:", "tee", 2));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 2), CustomizedModel.StableIdOrder("deck:", "tclient-status-bar", 2));
		EXPECT_EQ(Reloaded.StableIdOrder("deck:", "tclient-profiles", 2), CustomizedModel.StableIdOrder("deck:", "tclient-profiles", 2));
		EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-status-bar", 1).empty());
		EXPECT_TRUE(Reloaded.StableIdOrder("deck:", "tclient-profiles", 1).empty());
	}
}

// 意图：全局默认补位不能把不同 appearance 子页混在同一个 tab 下，也不能留下 order 空洞；
// 左右列由对称瀑布分配，聊天气泡卡不再被固定塞进左列（用户报告的左右悬殊）。
TEST(QmCardRegistry, AppearanceDeckDefaultsUseSubPagePlacements)
{
	struct SExpectedTab
	{
		const char *m_pStableId;
		const char *m_pTab;
	};
	const SExpectedTab aExpected[] = {
		{"deck:appearance-hud-main", "appearance-hud"},
		{"deck:appearance-hud-ddrace", "appearance-hud"},
		{"deck:appearance-scoreboard", "appearance-hud"},
		{"deck:appearance-chat-settings", "appearance-chat"},
		{"deck:appearance-chat-messages", "appearance-chat"},
		{"deck:appearance-chat-preview", "appearance-chat"},
		{"qm:chat_bubble", "appearance-chat"},
		{"deck:appearance-name-plate-settings", "appearance-name-plate"},
		{"deck:appearance-name-plate-preview", "appearance-name-plate"},
		{"deck:appearance-hook-collision-main", "appearance-hook-collision"},
		{"deck:appearance-hook-collision-preview", "appearance-hook-collision"},
		{"deck:appearance-info-messages", "appearance-info-messages"},
		{"deck:appearance-laser-enhanced", "appearance-laser"},
		{"deck:appearance-laser-colors", "appearance-laser"},
		{"deck:appearance-laser-preview", "appearance-laser"},
	};
	for(const SExpectedTab &Expected : aExpected)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(Expected.m_pStableId);
		ASSERT_NE(pDefault, nullptr) << Expected.m_pStableId;
		EXPECT_STREQ(pDefault->m_pDefaultTab, Expected.m_pTab) << Expected.m_pStableId;
		const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement(Expected.m_pStableId);
		ASSERT_NE(pPlacement, nullptr) << Expected.m_pStableId;
		EXPECT_STREQ(pPlacement->m_pDefaultTab, Expected.m_pTab) << Expected.m_pStableId;
		EXPECT_GT(pPlacement->m_Column, 0) << Expected.m_pStableId; // 子页卡片都是半宽卡
	}
}

// 意图：聊天气泡卡曾默认留在左列，左列三张、右列一张，两列高度悬殊（用户报告的缺陷）。
// 瀑布分配必须让聊天页两列都拿到卡片，并把两列高度控制在同一个量级。
TEST(QmCardRegistry, ChatPageDefaultsKeepBothColumnsEvenlyFilled)
{
	const STabColumnBalance Balance = DefaultColumnBalance("appearance-chat");
	EXPECT_GT(Balance.m_Counts[0], 0);
	EXPECT_GT(Balance.m_Counts[1], 0);
	const int Smallest = std::min(Balance.m_Heights[0], Balance.m_Heights[1]);
	const int Largest = std::max(Balance.m_Heights[0], Balance.m_Heights[1]);
	EXPECT_GE(Smallest * 4, Largest * 3) << "左=" << Balance.m_Heights[0] << " 右=" << Balance.m_Heights[1];
}

// 意图：默认布局「对称瀑布」的分配规则：按注册表顺序把每张半宽卡放进当前累计高度更矮的一列
// （等高放左列），全宽卡独占一行并重置两列高度。任何一张卡被塞进更深的一列都会让这个重放失败，
// 也就是用户报告的「某张卡被塞到左列、右列深坑」不再出现。
TEST(QmCardRegistry, DefaultLayoutNeverPlacesACardIntoTheDeeperColumn)
{
	struct STabReplay
	{
		int m_aHeight[2] = {0, 0};
		int m_FullWidthCount = 0;
	};
	std::map<std::string, STabReplay> Tabs;
	for(const qm_card_registry::SCardDefault &Card : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Card.m_pStableId);
		const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement(Card.m_pStableId);
		ASSERT_NE(pPlacement, nullptr);
		const std::string Tab(Card.m_pDefaultTab != nullptr ? Card.m_pDefaultTab : "");
		STabReplay &Replay = Tabs[Tab];
		if(Card.m_FullWidth)
		{
			EXPECT_EQ(pPlacement->m_Column, 0) << "全宽卡独占一行";
			EXPECT_EQ(pPlacement->m_OrderInColumn, Replay.m_FullWidthCount) << "全宽卡按注册表顺序排列";
			++Replay.m_FullWidthCount;
			Replay.m_aHeight[0] = 0;
			Replay.m_aHeight[1] = 0;
			continue;
		}
		const int Side = Replay.m_aHeight[0] <= Replay.m_aHeight[1] ? 0 : 1;
		EXPECT_EQ(pPlacement->m_Column, Side + 1) << "左高=" << Replay.m_aHeight[0] << " 右高=" << Replay.m_aHeight[1];
		Replay.m_aHeight[Side] += qm_card_registry::DefaultCardHeightWeight(Card.m_pStableId);
	}
}

// 意图：每张卡的默认 placement 必须可预测且落在注册表顺序上：同一列内的卡片顺序是
// 注册表顺序的子序列，且列内序号从 0 连续递增（否则 NormalizeColumns 之后读序会跳）。
TEST(QmCardRegistry, DefaultColumnOrderFollowsRegistrationOrder)
{
	std::vector<std::string> vRegistrationOrder;
	for(const qm_card_registry::SCardDefault &Card : qm_card_registry::Defaults())
		if(Card.m_pDefaultTab != nullptr)
			vRegistrationOrder.emplace_back(Card.m_pStableId);
	const std::vector<qm_card_order::SEntry> Entries = qm_card_registry::BuildDefaultEntries();
	std::map<std::string, std::vector<std::string>> PerColumn;
	for(const qm_card_order::SEntry &Entry : Entries)
	{
		if(Entry.m_pStableId == nullptr || Entry.m_pDefaultTab == nullptr)
			continue;
		std::string Key(Entry.m_pDefaultTab);
		Key.push_back('#');
		Key.append(std::to_string(Entry.m_Column));
		PerColumn[Key].resize(static_cast<size_t>(Entry.m_OrderInColumn) + 1);
		PerColumn[Key][static_cast<size_t>(Entry.m_OrderInColumn)] = Entry.m_pStableId;
	}
	for(const auto &[Key, vIds] : PerColumn)
	{
		SCOPED_TRACE(Key);
		// 序号连续到 max+1：没有空洞，也没有重复占位。
		EXPECT_EQ(std::count(vIds.begin(), vIds.end(), std::string()), 0) << "列内序号必须连续";
		// 列内顺序是注册表顺序的子序列：瀑布只决定"落哪列"，不允许打乱读序。
		size_t RegistrationCursor = 0;
		for(const std::string &Id : vIds)
		{
			const auto It = std::find(vRegistrationOrder.begin() + RegistrationCursor, vRegistrationOrder.end(), Id);
			ASSERT_NE(It, vRegistrationOrder.end()) << Id;
			RegistrationCursor = static_cast<size_t>(std::distance(vRegistrationOrder.begin(), It)) + 1;
		}
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

	// 列/序自洽：列号只在 0/1/2，order 非负；序列化后能原样重建。
	for(const qm_card_order::SEntry &Entry : Entries)
	{
		ASSERT_NE(Entry.m_pStableId, nullptr);
		EXPECT_GE(Entry.m_Column, 0) << Entry.m_pStableId;
		EXPECT_LE(Entry.m_Column, 2) << Entry.m_pStableId;
		EXPECT_GE(Entry.m_OrderInColumn, 0) << Entry.m_pStableId;
	}
	char aBuf[8192];
	ASSERT_TRUE(Model.Serialize(aBuf, sizeof(aBuf)));
	EXPECT_NE(std::string(aBuf).find("qm:chat_bubble|appearance-chat|"), std::string::npos);
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

// 外观与换皮动画的功能词均指向 Tee 外观卡。
TEST(QmCardRegistry, SkinSettingsSearchFindsOwningCard)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();
	const auto ExpectOwner = [&Model](const char *pQuery, const char *pOwner, const char *pOther, const char *pTab) {
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [pOwner](const auto &Result) {
			return std::string(Result.m_pStableId) == pOwner;
		});
		ASSERT_NE(It, Results.end()) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pTab, pTab) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pStableId, pOwner) << pQuery;
		EXPECT_EQ(std::count_if(Results.begin(), Results.end(), [pOther](const auto &Result) {
			return std::string(Result.m_pStableId) == pOther;
		}),
			0)
			<< pQuery;
	};
	for(const char *pQuery : {"皮肤描边", "循环色调", "表情阴影", "skin outline"})
		ExpectOwner(pQuery, "qm:skin_appearance", "qm:skin_transition", "tee");
	for(const char *pQuery : {"锤中偷皮", "皮肤切换", "换皮", "skin transition animation"})
		ExpectOwner(pQuery, "qm:skin_appearance", "qm:skin_transition", "tee");
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

// 意图：没有 deck 渲染器的注册表条目（激光、名牌文字、项目链接）功能由旧设置页承载，
// 搜索点击必须落到真正显示这些设置的卡，而不是自己那个空 tab。
TEST(QmCardRegistry, RendererlessCardsNavigateToHostingCard)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();

	const auto ExpectTarget = [&Model](const char *pStableId, const char *pExpectedTab, const char *pExpectedStableId) {
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
		ASSERT_NE(pDefault, nullptr) << pStableId;
		const qm_card_registry::SCardNavigationTarget Target = qm_card_registry::ResolveCardNavigationTarget(*pDefault, Model);
		EXPECT_STREQ(Target.m_pTab, pExpectedTab) << pStableId;
		EXPECT_STREQ(Target.m_pStableId, pExpectedStableId) << pStableId;
	};

	ExpectTarget("qm:laser", "appearance-laser", "deck:appearance-laser-enhanced");
	ExpectTarget("qm:nameplate_text", "appearance-name-plate", "deck:appearance-name-plate-text");
	ExpectTarget("qm:info", "credits-qmclient", "deck:qmclient-contributors-community");
	// 歌词已由独立共享卡片承载。
	ExpectTarget("qm:lyrics", "hud", "qm:lyrics");

	// 有自己渲染器的卡不受影响，仍指向自身。
	const qm_card_registry::SCardDefault *pFocus = qm_card_registry::FindByStableId("qm:focus_mode");
	ASSERT_NE(pFocus, nullptr);
	const qm_card_registry::SCardNavigationTarget FocusTarget = qm_card_registry::ResolveCardNavigationTarget(*pFocus, Model);
	EXPECT_STREQ(FocusTarget.m_pTab, "visual");
	EXPECT_STREQ(FocusTarget.m_pStableId, "qm:focus_mode");
}

// 意图：搜索命中激光/名牌文字时，结果携带的目标必须是承载卡的 tab 与 id，
// 否则玩家点进去只会看到一个没有该设置的页面。
TEST(QmCardRegistry, SearchResultsCarryHostingCardTarget)
{
	const qm_card_order::CModel Model = RegistryModelAfterRoundTrip();

	const auto ExpectSearchTarget = [&Model](const char *pQuery, const char *pStableId, const char *pExpectedTab, const char *pExpectedStableId) {
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [pStableId](const auto &Result) {
			return std::string(Result.m_pStableId) == pStableId;
		});
		ASSERT_NE(It, vResults.end()) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pTab, pExpectedTab) << pQuery;
		EXPECT_STREQ(It->m_Target.m_pStableId, pExpectedStableId) << pQuery;
	};

	ExpectSearchTarget("激光", "deck:appearance-laser-enhanced", "appearance-laser", "deck:appearance-laser-enhanced");
	ExpectSearchTarget("名牌", "deck:appearance-name-plate-text", "appearance-name-plate", "deck:appearance-name-plate-text");
	ExpectSearchTarget("歌词", "qm:lyrics", "hud", "qm:lyrics");
}

// 意图：歌词在旧栖梦子布局里有模块条目，适配层也把 Lyrics 映射到 qm:lyrics，
// 注册表必须存在同名条目，否则迁移出的全局顺序里会留下注册表不认识的 id。
TEST(QmCardRegistry, LyricsCardRegisteredForSidebarModuleMigration)
{
	const qm_card_registry::SCardDefault *pLyrics = qm_card_registry::FindByStableId("qm:lyrics");
	ASSERT_NE(pLyrics, nullptr);
	EXPECT_STREQ(pLyrics->m_pDefaultTab, "hud");
	// 默认列/序由瀑布分配决定：这里只锁定它是一条 hud 页的半宽卡，且能随全局顺序往返。
	const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement("qm:lyrics");
	ASSERT_NE(pPlacement, nullptr);
	EXPECT_STREQ(pPlacement->m_pDefaultTab, "hud");
	EXPECT_GT(pPlacement->m_Column, 0);
	EXPECT_GE(RegistryModelAfterRoundTrip().StateIndexForStableId("qm:lyrics"), 0);
}

// 意图：调试模式卡片必须挂在 HUD 页，携带可搜索的中文/拼音关键词，且模块枚举可反查。
TEST(QmCardRegistry, DebugModeCardRegisteredInHudTab)
{
	const auto *pDefault = qm_card_registry::FindByStableId("qm:debug_mode");
	ASSERT_NE(pDefault, nullptr);
	EXPECT_STREQ(pDefault->m_pDefaultTab, "hud");
	const qm_card_order::SEntry *pPlacement = qm_card_registry::FindDefaultPlacement("qm:debug_mode");
	ASSERT_NE(pPlacement, nullptr);
	EXPECT_STREQ(pPlacement->m_pDefaultTab, "hud");
	EXPECT_GT(pPlacement->m_Column, 0);
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

TEST(QmCardRegistry, TimeoutDisconnectSearchPointsToControls)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"qm_timeout_disconnect", "Active disconnect", "主动断开", "异常断开", "timeout disconnect"})
	{
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		ASSERT_FALSE(Results.empty()) << pQuery;
		// 共享搜索索引会同时命中直接绑定与命令编辑器，不能假设同一命令只有一个入口。
		bool Found = false;
		for(const auto &Result : Results)
			Found = Found || std::string(Result.m_pStableId) == "deck:controls-miscellaneous";
		EXPECT_TRUE(Found) << pQuery;
		std::set<std::string> UniqueIds;
		for(const auto &Result : Results)
		{
			EXPECT_TRUE(UniqueIds.emplace(Result.m_pStableId).second) << pQuery;
			ASSERT_NE(Result.m_Target.m_pStableId, nullptr);
			EXPECT_GE(Model.FindByStableId(Result.m_Target.m_pStableId), 0) << pQuery;
		}
		if(std::string(pQuery) == "qm_timeout_disconnect")
		{
			EXPECT_EQ(UniqueIds.count("qm:key_binds"), 1u);
			EXPECT_EQ(UniqueIds.count("qm:bind_editor"), 1u);
		}
	}
}

TEST(QmCardRegistry, BindCardsNavigateToControlsBindTab)
{
	const auto &vDefaults = qm_card_registry::Defaults();
	const auto Model = RegistryModelAfterRoundTrip();
	for(const char *pStableId : {"qm:bind_editor", "qm:bind_overview"})
	{
		const auto It = std::find_if(vDefaults.begin(), vDefaults.end(), [pStableId](const qm_card_registry::SCardDefault &Entry) { return str_comp(Entry.m_pStableId, pStableId) == 0; });
		ASSERT_NE(It, vDefaults.end());
		EXPECT_EQ(str_comp(It->m_pDefaultTab, "controls"), 0);
		EXPECT_EQ(str_comp(It->m_pNavigationTab, "bind"), 0);
		const auto Target = qm_card_registry::ResolveCardNavigationTarget(*It, Model);
		EXPECT_STREQ(Target.m_pTab, "bind");
		EXPECT_STREQ(Target.m_pStableId, pStableId);
	}

	const auto &vBindCards = qm_card_catalog::BindCardStableIds();
	EXPECT_TRUE(std::any_of(vBindCards.begin(), vBindCards.end(), [](const char *pStableId) { return str_comp(pStableId, "qm:bind_overview") == 0; }));
}

TEST(QmCardRegistry, LegacyBindTabMovesCardsIntoControls)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"qm:bind_editor", "bind", 0, 0},
		{"qm:bind_overview", "bind", 0, 1},
	});

	EXPECT_TRUE(qm_card_registry::RepairLegacyBindTab(Model));
	EXPECT_STREQ(Model.Entry(Model.FindByStableId("qm:bind_editor")).m_pDefaultTab, "controls");
	EXPECT_STREQ(Model.Entry(Model.FindByStableId("qm:bind_overview")).m_pDefaultTab, "controls");
	EXPECT_FALSE(qm_card_registry::RepairLegacyBindTab(Model));
}

TEST(QmCardRegistry, FavoriteMapsCardIsRemovedFromSettingsAndSearch)
{
	EXPECT_EQ(qm_card_registry::FindByStableId("qm:favorite_maps"), nullptr);
	EXPECT_FALSE(qm_card_catalog::HasCardModule("qm:favorite_maps"));
	const auto &vCards = qm_card_catalog::FunctionCardStableIds();
	EXPECT_FALSE(std::any_of(vCards.begin(), vCards.end(), [](const char *pId) {
		return std::string(pId) == "qm:favorite_maps";
	}));
	const auto Model = RegistryModelAfterRoundTrip();
	for(const char *pQuery : {"收藏地图", "favorite maps"})
	{
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		EXPECT_FALSE(std::any_of(Results.begin(), Results.end(), [](const auto &Result) {
			return std::string(Result.m_pStableId) == "qm:favorite_maps";
		}));
	}
}

// 标准卡（deck:）与 TClient 卡是两个互斥的渲染目录：DDNet 页「网络补偿」子页签
// 里也有 deck: 前缀的卡（deck:ddnet-prediction），若同时被判成 TClient 卡，
// BuildCard 会先命中 TClient 分派、落到没有该卡分支的实现，卡片变 0 高空壳。
TEST(QmCardRegistry, StandardCardsAreNeverRoutedThroughTheTClientDirectory)
{
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		if(Default.m_pStableId == nullptr || Default.m_pNavigationStableId != nullptr)
			continue;
		if(!qm_card_catalog::IsStandardCard(Default.m_pStableId))
			continue;
		SCOPED_TRACE(Default.m_pStableId);
		EXPECT_FALSE(qm_card_catalog::IsTClientCard(Default.m_pStableId));
	}
	// 迁到「网络补偿」子页签的三张卡实现仍在 TClient 模块，必须继续走 TClient 目录。
	for(const char *pId : {"tclient:input", "tclient:anti-latency-tools", "tclient:improved-anti-ping"})
	{
		SCOPED_TRACE(pId);
		EXPECT_TRUE(qm_card_catalog::IsTClientCard(pId));
		EXPECT_FALSE(qm_card_catalog::IsStandardCard(pId));
	}
	const auto &vPredictionPage = qm_card_catalog::CatalogPageStableIds("ddnet-prediction");
	for(const char *pId : {"deck:ddnet-prediction", "tclient:input", "tclient:anti-latency-tools", "tclient:improved-anti-ping"})
	{
		SCOPED_TRACE(pId);
		EXPECT_TRUE(std::any_of(vPredictionPage.begin(), vPredictionPage.end(), [pId](const char *pCandidate) {
			return std::string(pCandidate) == pId;
		}));
	}
}
