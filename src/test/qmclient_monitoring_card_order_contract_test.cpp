// Settings card order persistence and migration contracts.
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <string>

TEST(QmMonitoringCardOrderContract, GlobalCardOrderConfigHasMigrationLandingZone)
{
	const std::string QmConfig = ReadRepoFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ConfigManager = ReadRepoFile("src/engine/shared/config.cpp");

	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmGlobalCardOrder, qm_global_card_order, 8000, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_INT(QmCardOrderMigrated, qm_card_order_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmSettingsCardOrder, qm_settings_card_order, 2048, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmSidebarCardOrder, qm_sidebar_card_order, 2048, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(QmConfig.find("qm_global_card_order, 2048"), std::string::npos);
	EXPECT_NE(ConfigManager.find("std::vector<char> vLineBuf(pVariable->MaxSerializedSize());"), std::string::npos);
}

TEST(QmMonitoringCardOrderContract, SettingsCardOrderPersistenceCommitsGlobalConfigAtomically)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string LoadBody = ExtractSourceFunctionBody(Menus, "void CMenus::LoadSettingsCardOrderModel()");
	const std::string SaveBody = ExtractSourceFunctionBody(Menus, "bool CMenus::SaveSettingsCardOrderModel()");
	ASSERT_FALSE(LoadBody.empty());
	ASSERT_FALSE(SaveBody.empty());

	const char *pTempBuffer = "char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)]";
	const char *pSerialize = "m_SettingsCardOrderModel.Serialize(aSerialized, sizeof(aSerialized))";
	const char *pCopy = "str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));";
	const size_t CandidateTempPos = LoadBody.find(pTempBuffer);
	const size_t CandidateSerializePos = LoadBody.find("Candidate.Serialize(aSerialized, sizeof(aSerialized))", CandidateTempPos);
	const size_t CandidateCopyPos = LoadBody.find(pCopy, CandidateSerializePos);
	const size_t CandidateCommitPos = LoadBody.find("m_SettingsCardOrderModel.SetEntries(CopyModelEntries(Candidate));", CandidateCopyPos);
	const size_t CandidateDirtyClearPos = LoadBody.find("m_SettingsCardOrderModel.ClearDirty();", CandidateCommitPos);
	const size_t LoadTempPos = LoadBody.find(pTempBuffer, CandidateDirtyClearPos);
	const size_t LoadSerializePos = LoadBody.find(pSerialize, LoadTempPos);
	const size_t LoadCopyPos = LoadBody.find(pCopy, LoadSerializePos);
	const size_t LoadDirtyClearPos = LoadBody.find("m_SettingsCardOrderModel.ClearDirty();", LoadCopyPos);
	const size_t LoadMigratedPos = LoadBody.find("g_Config.m_QmCardOrderMigrated = 1;");
	EXPECT_NE(CandidateTempPos, std::string::npos);
	EXPECT_NE(CandidateSerializePos, std::string::npos);
	EXPECT_NE(CandidateCopyPos, std::string::npos);
	EXPECT_NE(CandidateCommitPos, std::string::npos);
	EXPECT_NE(CandidateDirtyClearPos, std::string::npos);
	EXPECT_LT(CandidateTempPos, CandidateSerializePos);
	EXPECT_LT(CandidateSerializePos, CandidateCopyPos);
	EXPECT_LT(CandidateCopyPos, CandidateCommitPos);
	EXPECT_LT(CandidateCommitPos, CandidateDirtyClearPos);
	EXPECT_NE(LoadTempPos, std::string::npos);
	EXPECT_NE(LoadSerializePos, std::string::npos);
	EXPECT_NE(LoadCopyPos, std::string::npos);
	EXPECT_NE(LoadDirtyClearPos, std::string::npos);
	EXPECT_NE(LoadMigratedPos, std::string::npos);
	EXPECT_LT(LoadTempPos, LoadSerializePos);
	EXPECT_LT(LoadSerializePos, LoadCopyPos);
	EXPECT_LT(LoadCopyPos, LoadDirtyClearPos);
	EXPECT_LT(LoadDirtyClearPos, LoadMigratedPos);

	const size_t SaveTempPos = SaveBody.find(pTempBuffer);
	const size_t SaveSerializePos = SaveBody.find(pSerialize);
	const size_t SaveCopyPos = SaveBody.find(pCopy);
	const size_t SaveDirtyClearPos = SaveBody.find("m_SettingsCardOrderModel.ClearDirty();");
	EXPECT_NE(SaveTempPos, std::string::npos);
	EXPECT_NE(SaveSerializePos, std::string::npos);
	EXPECT_NE(SaveCopyPos, std::string::npos);
	EXPECT_NE(SaveDirtyClearPos, std::string::npos);
	EXPECT_LT(SaveTempPos, SaveSerializePos);
	EXPECT_LT(SaveSerializePos, SaveCopyPos);
	EXPECT_LT(SaveCopyPos, SaveDirtyClearPos);
}

TEST(QmMonitoringCardOrderContract, SettingsCardLayoutVersionMigrationRequiresWholeLegacyGroups)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string LoadBody = ExtractSourceFunctionBody(Menus, "void CMenus::LoadSettingsCardOrderModel()");
	ASSERT_FALSE(LoadBody.empty());

	EXPECT_NE(LoadBody.find("const bool ContributorsStillOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:qmclient-contributors-community\""), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:qmclient-contributors-sponsors\""), std::string::npos);
	EXPECT_NE(LoadBody.find("const bool BindWheelStillOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:tclient-bind-wheel-editor\""), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:tclient-bind-wheel-preview\""), std::string::npos);
	EXPECT_NE(LoadBody.find("MakeCandidate(Candidate);"), std::string::npos);
	EXPECT_NE(LoadBody.find("TabContainsOnlyStableIds(Candidate, \"qmclient-contributors\""), std::string::npos);
	EXPECT_NE(LoadBody.find("TabContainsOnlyStableIds(Candidate, \"tclient-bind-wheel\""), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tclient-profiles\", vLegacyProfileLayout, vTargetProfileLayout, vProfileIds)"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tclient-status-bar\", vLegacyStatusBarDefaults, vTargetStatusBarLayout, vStatusBarIds)"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tee\", vLegacyTeeDefaults, vTargetTeeLayout, vTeeIds)"), std::string::npos);
	EXPECT_EQ(LoadBody.find("MoveIfStillAtOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateTClientMainCardsToAlternatingColumns"), std::string::npos);
	EXPECT_NE(LoadBody.find("TClientMainCardsMigrationCommitPlan(MigrationResult)"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistCandidate(Candidate, CandidateChanged)"), std::string::npos);
	EXPECT_NE(LoadBody.find("g_Config.m_QmCardLayoutVersion = 1;"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistAndAdvanceLayoutVersion(5, true)"), std::string::npos);
	EXPECT_NE(LoadBody.find("if(CommitPlan.m_PersistSerialized)"), std::string::npos);
	EXPECT_NE(LoadBody.find("if(CommitPlan.m_AdvanceVersion)"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistAndAdvanceLayoutVersion(7, true)"), std::string::npos);
}

TEST(QmMonitoringCardOrderContract, CardOrderModelUsesStableIdIndexForFind)
{
	const std::string Model = ReadRepoFile("src/game/client/QmUi/QmCardOrderModel.cpp");
	const std::string FindBody = ExtractSourceFunctionBody(Model, "int CModel::FindByStableId(const char *pStableId) const");
	ASSERT_FALSE(FindBody.empty());

	EXPECT_NE(FindBody.find("return StateIndexForStableId(pStableId);"), std::string::npos);
	EXPECT_EQ(FindBody.find("for(size_t i = 0; i < m_vEntries.size(); ++i)"), std::string::npos);
	EXPECT_NE(Model.find("BuildStateIndex(); // 维护 stableId→index 注册表"), std::string::npos);
}
