#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <vector>

TEST(QmCardRegistryMigrationAllocation, RejectedPreparationKeepsLegacyColumnsAndCanRetry)
{
	using EMigration = qm_card_registry::ETClientMainCardsMigrationResult;
	const char *apLegacyIds[] = {
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
	std::vector<qm_card_order::SEntry> vLegacy{{"deck:general-game", "general", 2, 0}};
	for(const char *pId : apLegacyIds)
		vLegacy.push_back({pId, "tclient", 1, static_cast<int>(vLegacy.size()) - 1});

	qm_card_order::CModel Successful;
	Successful.SetEntries(vLegacy);
	char aExpected[4096];
	size_t AllocationCount;
	EMigration Result;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<size_t>::max());
		Result = qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(Successful, aExpected, sizeof(aExpected));
		AllocationCount = Trace.Attempts();
	}
	ASSERT_EQ(Result, EMigration::MIGRATED);
	ASSERT_GT(AllocationCount, 0u);
	bool HadRejection = false;
	for(size_t Index = 0; Index < AllocationCount; ++Index)
	{
		SCOPED_TRACE(Index);
		qm_card_order::CModel Model;
		Model.SetEntries(vLegacy);
		Model.ClearDirty();
		const uint64_t LayoutRevision = Model.LayoutRevision();
		const uint64_t IndexRevision = Model.StateIndexRevision();
		char aResult[4096] = {};
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Index);
			Result = qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(Model, aResult, sizeof(aResult));
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		if(Result == EMigration::PERSIST_FAILED)
		{
			HadRejection = true;
			EXPECT_TRUE(qm_card_registry::IsTClientMainCardsLegacyLeft(Model));
			EXPECT_FALSE(Model.IsDirty());
			EXPECT_EQ(Model.LayoutRevision(), LayoutRevision);
			EXPECT_EQ(Model.StateIndexRevision(), IndexRevision);
			Result = qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(Model, aResult, sizeof(aResult));
		}
		ASSERT_EQ(Result, EMigration::MIGRATED);
		EXPECT_STREQ(aResult, aExpected);
		char aPublished[4096];
		ASSERT_TRUE(Model.Serialize(aPublished, sizeof(aPublished)));
		EXPECT_STREQ(aPublished, aResult);
	}
	EXPECT_TRUE(HadRejection);
}
