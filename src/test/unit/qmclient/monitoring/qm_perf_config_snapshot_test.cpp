#include <game/client/components/qmclient/perf_diagnostics.h>

#include <gtest/gtest.h>
#include <test/support/perf_config_manager.h>

#include <new>
#include <string>
#include <vector>

namespace
{
	class CConfigOperations : public CQmPerfDiagnosticOperations
	{
	public:
		const char *m_pFailReadName = nullptr;
		int m_FailPart = -1;
		int m_FailEmitAt = -1;
		int m_EmitAttempts = 0;
		std::vector<std::string> m_vFields;

		SQmPerfConfigValue ReadValue(const SConfigVariable &Variable) override
		{
			if(m_pFailReadName && str_comp(Variable.m_pScriptName, m_pFailReadName) == 0)
				throw std::bad_alloc();
			return CQmPerfDiagnosticOperations::ReadValue(Variable);
		}

		std::string ConfigFields(const SConfigVariable &Variable, const SQmPerfConfigValue &Value, const std::string &Chunk, bool Initial, uint64_t Revision, size_t Part, size_t Parts) override
		{
			auto Fields = CQmPerfDiagnosticOperations::ConfigFields(Variable, Value, Chunk, Initial, Revision, Part, Parts);
			if(static_cast<int>(Part) == m_FailPart)
				throw std::bad_alloc();
			return Fields;
		}

		void Emit(const char *, const std::string &Fields, const IClient *) override
		{
			if(++m_EmitAttempts == m_FailEmitAt)
				throw std::bad_alloc();
			m_vFields.push_back(Fields);
		}

		void Clear()
		{
			m_vFields.clear();
			m_EmitAttempts = 0;
		}
	};

	class QmPerfConfigSnapshot : public ::testing::Test
	{
	protected:
		int m_First = 0;
		int m_Second = 0;
		SIntConfigVariable m_FirstVar{nullptr, "qm_test_first", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_First, 1, 0, 100};
		SIntConfigVariable m_SecondVar{nullptr, "qm_test_second", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Second, 2, 0, 100};
		CPerfConfigManager m_Manager;
		CConfigOperations m_Operations;
		CQmPerfConfigSnapshot m_Snapshot{&m_Operations};

		void SetUp() override
		{
			m_Manager.m_vVariables = {&m_FirstVar, &m_SecondVar};
		}
	};
}

TEST_F(QmPerfConfigSnapshot, CompleteSnapshotSkipsUnchangedValuesAndEmitsUserChange)
{
	EXPECT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 3u);
	EXPECT_EQ(m_Operations.m_vFields[0], "\"event\":\"config_snapshot\",\"expected_variables\":2");
	m_Operations.Clear();
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	EXPECT_TRUE(m_Operations.m_vFields.empty());
	m_First = 3;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"initial\":0"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"value\":\"3\""), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, PartialVariableCollectionIsRetriedFromTheFullManager)
{
	m_Manager.m_FailAfter = 1;
	EXPECT_FALSE(m_Snapshot.Start(&m_Manager, nullptr));
	EXPECT_TRUE(m_Operations.m_vFields.empty());
	m_Manager.m_FailAfter = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 3u);
	EXPECT_EQ(m_Manager.m_CollectionCalls, 2);
	EXPECT_EQ(m_Operations.m_vFields[0], "\"event\":\"config_snapshot\",\"expected_variables\":2");
	EXPECT_NE(m_Operations.m_vFields[2].find("qm_test_second"), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, SnapshotHeaderFailureDoesNotCommitTruncatedStartupState)
{
	m_Operations.m_FailEmitAt = 1;
	EXPECT_FALSE(m_Snapshot.Start(&m_Manager, nullptr));
	EXPECT_TRUE(m_Operations.m_vFields.empty());
	m_Operations.Clear();
	m_Operations.m_FailEmitAt = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	EXPECT_EQ(m_Manager.m_CollectionCalls, 2);
	EXPECT_EQ(m_Operations.m_vFields.size(), 3u);
}

TEST_F(QmPerfConfigSnapshot, ReadFailureRetainsEarlierSuccessfulValuesAndRetriesMissingInitialValue)
{
	m_Operations.m_pFailReadName = "qm_test_second";
	EXPECT_FALSE(m_Snapshot.Start(&m_Manager, nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 2u);
	m_Operations.Clear();
	m_Operations.m_pFailReadName = nullptr;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_NE(m_Operations.m_vFields[0].find("qm_test_second"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"initial\":1"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"revision\":2"), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, LateValueFormattingFailureRetriesInitialValue)
{
	m_Operations.m_FailPart = 0;
	EXPECT_FALSE(m_Snapshot.Start(&m_Manager, nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	m_Operations.Clear();
	m_Operations.m_FailPart = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 2u);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"initial\":1"), std::string::npos);
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	EXPECT_EQ(m_Operations.m_vFields.size(), 2u);
}

TEST_F(QmPerfConfigSnapshot, FailedChangeEmissionDoesNotAdvanceBaseline)
{
	ASSERT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	m_Operations.Clear();
	m_First = 7;
	m_Operations.m_FailEmitAt = 1;
	EXPECT_FALSE(m_Snapshot.Update(nullptr));
	EXPECT_TRUE(m_Operations.m_vFields.empty());
	m_Operations.m_FailEmitAt = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"initial\":0"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"value\":\"7\""), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"revision\":3"), std::string::npos);
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	EXPECT_EQ(m_Operations.m_vFields.size(), 1u);
}

TEST_F(QmPerfConfigSnapshot, UserChangeBeforeRetryLogsLatestValue)
{
	ASSERT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	m_Operations.Clear();
	m_First = 7;
	m_Operations.m_FailPart = 0;
	EXPECT_FALSE(m_Snapshot.Update(nullptr));
	m_First = 8;
	m_Operations.m_FailPart = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"value\":\"8\""), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, ReturningToDefaultAfterFailedEmissionReplaysCommittedValue)
{
	ASSERT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	m_Operations.Clear();
	m_First = 7;
	m_Operations.m_FailEmitAt = 1;
	EXPECT_FALSE(m_Snapshot.Update(nullptr));
	m_First = 1;
	m_Operations.m_FailEmitAt = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"initial\":0"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[0].find("\"value\":\"1\""), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, ReopeningClearsOldBaselineAndEmitsAllInitialValuesAgain)
{
	ASSERT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	m_Operations.Clear();
	EXPECT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 3u);
	EXPECT_NE(m_Operations.m_vFields[1].find("\"initial\":1"), std::string::npos);
	EXPECT_NE(m_Operations.m_vFields[1].find("\"revision\":1"), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, FailedRestartDoesNotResumePreviousVariableList)
{
	ASSERT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	m_Operations.Clear();
	m_Manager.m_vVariables = {&m_SecondVar};
	m_Manager.m_FailAfter = 0;
	EXPECT_FALSE(m_Snapshot.Start(&m_Manager, nullptr));
	m_First = 4;
	m_Manager.m_FailAfter = -1;
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 2u);
	EXPECT_EQ(m_Operations.m_vFields[0], "\"event\":\"config_snapshot\",\"expected_variables\":1");
	EXPECT_NE(m_Operations.m_vFields[1].find("qm_test_second"), std::string::npos);
}

TEST_F(QmPerfConfigSnapshot, EmptyManagerEmitsOneHeaderWithoutRepeatedInitialization)
{
	m_Manager.m_vVariables.clear();
	EXPECT_TRUE(m_Snapshot.Start(&m_Manager, nullptr));
	EXPECT_TRUE(m_Snapshot.Update(nullptr));
	ASSERT_EQ(m_Operations.m_vFields.size(), 1u);
	EXPECT_EQ(m_Operations.m_vFields[0], "\"event\":\"config_snapshot\",\"expected_variables\":0");
	EXPECT_EQ(m_Manager.m_CollectionCalls, 1);
}
