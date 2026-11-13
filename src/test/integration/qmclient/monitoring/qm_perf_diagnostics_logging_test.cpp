#include <base/logger.h>

#include <engine/shared/json.h>

#include <game/client/components/qmclient/perf_diagnostics.h>

#include <gtest/gtest.h>
#include <test/support/perf_config_manager.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	class CPerfCapture : public ILogger
	{
	public:
		std::vector<std::string> m_vMessages;

		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "perf/frame") == 0 || str_comp(pMessage->m_aSystem, "perf/config") == 0)
				m_vMessages.emplace_back(pMessage->Message());
		}
	};

	class CFailingDiagnosticOperations : public CQmPerfDiagnosticOperations
	{
	public:
		int m_FailAt = -1;
		int m_EmitCount = 0;
		bool m_FailAll = false;

		void Emit(const char *pSystem, const std::string &Fields, const IClient *pClient) override
		{
			if(++m_EmitCount == m_FailAt || m_FailAll)
				throw std::bad_alloc();
			CQmPerfDiagnosticOperations::Emit(pSystem, Fields, pClient);
		}
	};

	class QmPerfDiagnosticsLogging : public ::testing::Test
	{
	protected:
		CPerfCapture m_Capture;
		CLogScope m_LogScope{&m_Capture};
		CFailingDiagnosticOperations m_Operations;

		TJson Parse(size_t Index)
		{
			const auto &Message = m_Capture.m_vMessages.at(Index);
			return TJson(JsonParse(Message.c_str(), Message.size()), json_value_free);
		}
	};

	void ExpectString(const json_value *pRoot, const char *pName, const char *pExpected)
	{
		const auto *pValue = json_object_get(pRoot, pName);
		ASSERT_EQ(pValue->type, json_string) << pName;
		EXPECT_STREQ(pValue->u.string.ptr, pExpected) << pName;
	}

	void ExpectInteger(const json_value *pRoot, const char *pName, int64_t Expected)
	{
		const auto *pValue = json_object_get(pRoot, pName);
		ASSERT_EQ(pValue->type, json_integer) << pName;
		EXPECT_EQ(pValue->u.integer, Expected) << pName;
	}
}

TEST_F(QmPerfDiagnosticsLogging, PartialInitialValueIsReplayedInFullWithANewRevision)
{
	char aValue[512] = "";
	char aOld[512] = "";
	SStringConfigVariable Variable(nullptr, "qm_test_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "default", sizeof(aValue), aOld);
	const std::string Expected(300, 'x');
	str_copy(aValue, Expected.c_str());
	CPerfConfigManager Manager;
	Manager.m_vVariables = {&Variable};
	CQmPerfConfigSnapshot Snapshot(&m_Operations);
	m_Operations.m_FailAt = 3;
	EXPECT_FALSE(Snapshot.Start(&Manager, nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 2u);
	const auto Header = Parse(0);
	ASSERT_NE(Header, nullptr);
	ExpectString(Header.get(), "event", "config_snapshot");
	ExpectInteger(Header.get(), "expected_variables", 1);
	const auto Partial = Parse(1);
	ASSERT_NE(Partial, nullptr);
	ExpectInteger(Partial.get(), "revision", 1);
	ExpectInteger(Partial.get(), "part", 0);
	m_Operations.m_FailAt = -1;
	EXPECT_TRUE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 5u);
	std::string Rebuilt;
	for(int Part = 0; Part < 3; ++Part)
	{
		const auto Root = Parse(2 + Part);
		ASSERT_NE(Root, nullptr);
		ExpectString(Root.get(), "system", "perf/config");
		ExpectString(Root.get(), "event", "config_value");
		ExpectString(Root.get(), "name", "qm_test_text");
		ExpectInteger(Root.get(), "initial", 1);
		ExpectInteger(Root.get(), "revision", 2);
		ExpectInteger(Root.get(), "part", Part);
		ExpectInteger(Root.get(), "parts", 3);
		const auto *pChunk = json_object_get(Root.get(), "value");
		ASSERT_EQ(pChunk->type, json_string);
		Rebuilt += pChunk->u.string.ptr;
	}
	EXPECT_EQ(Rebuilt, Expected);
	EXPECT_TRUE(Snapshot.Update(nullptr));
	EXPECT_EQ(m_Capture.m_vMessages.size(), 5u);
}

TEST_F(QmPerfDiagnosticsLogging, FailedIncrementalValueUsesLatestUserValueDuringRetry)
{
	char aValue[512] = "";
	char aOld[512] = "";
	SStringConfigVariable Variable(nullptr, "qm_test_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "default", sizeof(aValue), aOld);
	CPerfConfigManager Manager;
	Manager.m_vVariables = {&Variable};
	CQmPerfConfigSnapshot Snapshot(&m_Operations);
	ASSERT_TRUE(Snapshot.Start(&Manager, nullptr));
	m_Capture.m_vMessages.clear();
	m_Operations.m_EmitCount = 0;
	m_Operations.m_FailAt = 2;
	str_copy(aValue, std::string(300, 'a').c_str());
	EXPECT_FALSE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	const std::string Expected(260, 'b');
	str_copy(aValue, Expected.c_str());
	m_Operations.m_FailAt = -1;
	EXPECT_TRUE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 4u);
	std::string Rebuilt;
	for(size_t Index = 1; Index < 4; ++Index)
	{
		const auto Root = Parse(Index);
		ASSERT_NE(Root, nullptr);
		ExpectInteger(Root.get(), "initial", 0);
		ExpectInteger(Root.get(), "revision", 3);
		ExpectInteger(Root.get(), "part", Index - 1);
		const auto *pChunk = json_object_get(Root.get(), "value");
		ASSERT_EQ(pChunk->type, json_string);
		Rebuilt += pChunk->u.string.ptr;
	}
	EXPECT_EQ(Rebuilt, Expected);
}

TEST_F(QmPerfDiagnosticsLogging, SensitiveValueRemainsRedactedAfterFailedInitialEmission)
{
	char aValue[256] = "";
	char aOld[256] = "";
	SStringConfigVariable Variable(nullptr, "qm_test_token", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "private-default", sizeof(aValue), aOld);
	CPerfConfigManager Manager;
	Manager.m_vVariables = {&Variable};
	CQmPerfConfigSnapshot Snapshot(&m_Operations);
	m_Operations.m_FailAt = 2;
	EXPECT_FALSE(Snapshot.Start(&Manager, nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	str_copy(aValue, "private-user-value");
	m_Operations.m_FailAt = -1;
	EXPECT_TRUE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 2u);
	const auto Root = Parse(1);
	ASSERT_NE(Root, nullptr);
	ExpectString(Root.get(), "value", "<redacted>");
	ExpectString(Root.get(), "type", "string");
	ExpectInteger(Root.get(), "initial", 1);
	ExpectInteger(Root.get(), "redacted", 1);
	ExpectInteger(Root.get(), "is_default", 0);
	for(const auto &Message : m_Capture.m_vMessages)
		EXPECT_EQ(Message.find("private-"), std::string::npos);
}

TEST_F(QmPerfDiagnosticsLogging, DefaultFlagChangeIsEmittedEvenWhenRedactedValueIsUnchanged)
{
	char aValue[256] = "";
	char aOld[256] = "";
	SStringConfigVariable Variable(nullptr, "qm_test_password", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "default-secret", sizeof(aValue), aOld);
	CPerfConfigManager Manager;
	Manager.m_vVariables = {&Variable};
	CQmPerfConfigSnapshot Snapshot(&m_Operations);
	ASSERT_TRUE(Snapshot.Start(&Manager, nullptr));
	m_Capture.m_vMessages.clear();
	str_copy(aValue, "changed-secret");
	EXPECT_TRUE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	const auto Root = Parse(0);
	ASSERT_NE(Root, nullptr);
	ExpectString(Root.get(), "value", "<redacted>");
	ExpectInteger(Root.get(), "initial", 0);
	ExpectInteger(Root.get(), "is_default", 0);
	ExpectInteger(Root.get(), "redacted", 1);
}

TEST_F(QmPerfDiagnosticsLogging, FullBatchRecoveryKeepsOriginalSamplesAndContinuesWithCurrentFrame)
{
	CQmPerfFrameBatch Batch(&m_Operations);
	m_Operations.m_FailAll = true;
	for(uint64_t Frame = 1; Frame <= 64; ++Frame)
		Batch.RecordAndLog(Frame, 0.5, nullptr);
	EXPECT_FALSE(Batch.RecordAndLog(65, 0.5, nullptr));
	EXPECT_EQ(Batch.Count(), 64u);
	EXPECT_TRUE(m_Capture.m_vMessages.empty());
	m_Operations.m_FailAll = false;
	EXPECT_TRUE(Batch.RecordAndLog(66, 1.25, nullptr));
	EXPECT_TRUE(Batch.Flush(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 2u);
	const auto Full = Parse(0);
	ASSERT_NE(Full, nullptr);
	ExpectString(Full.get(), "event", "frame_batch");
	ExpectString(Full.get(), "system", "perf/frame");
	const auto *pFrames = json_object_get(Full.get(), "frames");
	const auto *pDurations = json_object_get(Full.get(), "durations_ms");
	ASSERT_EQ(pFrames->type, json_array);
	ASSERT_EQ(pDurations->type, json_array);
	ASSERT_EQ(pFrames->u.array.length, 64u);
	ASSERT_EQ(pDurations->u.array.length, 64u);
	for(unsigned Index = 0; Index < 64; ++Index)
	{
		const auto *pFrame = pFrames->u.array.values[Index];
		const auto *pDuration = pDurations->u.array.values[Index];
		ASSERT_EQ(pFrame->type, json_integer);
		ASSERT_EQ(pDuration->type, json_double);
		EXPECT_EQ(pFrame->u.integer, Index + 1);
		EXPECT_DOUBLE_EQ(pDuration->u.dbl, 0.5);
	}
	const auto Tail = Parse(1);
	ASSERT_NE(Tail, nullptr);
	const auto *pTailFrames = json_object_get(Tail.get(), "frames");
	ASSERT_EQ(pTailFrames->type, json_array);
	ASSERT_EQ(pTailFrames->u.array.length, 1u);
	ASSERT_EQ(pTailFrames->u.array.values[0]->type, json_integer);
	EXPECT_EQ(pTailFrames->u.array.values[0]->u.integer, 66);
	const auto *pTailDurations = json_object_get(Tail.get(), "durations_ms");
	ASSERT_EQ(pTailDurations->type, json_array);
	ASSERT_EQ(pTailDurations->u.array.length, 1u);
	ASSERT_EQ(pTailDurations->u.array.values[0]->type, json_double);
	EXPECT_DOUBLE_EQ(pTailDurations->u.array.values[0]->u.dbl, 1.25);
}

TEST_F(QmPerfDiagnosticsLogging, ElapsedBoundaryAndTailPreserveFrameSchemaAndDurations)
{
	CQmPerfFrameBatch Batch(&m_Operations);
	EXPECT_TRUE(Batch.RecordAndLog(10, 0.5, nullptr));
	EXPECT_TRUE(m_Capture.m_vMessages.empty());
	EXPECT_TRUE(Batch.RecordAndLog(11, 999.5, nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	EXPECT_TRUE(Batch.RecordAndLog(12, 12.25, nullptr));
	EXPECT_TRUE(Batch.Flush(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 2u);
	const auto Root = Parse(0);
	ASSERT_NE(Root, nullptr);
	ExpectInteger(Root.get(), "frame", 0);
	const auto *pFrames = json_object_get(Root.get(), "frames");
	const auto *pDurations = json_object_get(Root.get(), "durations_ms");
	ASSERT_EQ(pFrames->type, json_array);
	ASSERT_EQ(pDurations->type, json_array);
	ASSERT_EQ(pFrames->u.array.length, 2u);
	ASSERT_EQ(pDurations->u.array.length, 2u);
	ASSERT_EQ(pFrames->u.array.values[0]->type, json_integer);
	ASSERT_EQ(pFrames->u.array.values[1]->type, json_integer);
	ASSERT_EQ(pDurations->u.array.values[0]->type, json_double);
	ASSERT_EQ(pDurations->u.array.values[1]->type, json_double);
	EXPECT_EQ(pFrames->u.array.values[0]->u.integer, 10);
	EXPECT_EQ(pFrames->u.array.values[1]->u.integer, 11);
	EXPECT_DOUBLE_EQ(pDurations->u.array.values[0]->u.dbl, 0.5);
	EXPECT_DOUBLE_EQ(pDurations->u.array.values[1]->u.dbl, 999.5);
	const auto Tail = Parse(1);
	ASSERT_NE(Tail, nullptr);
	ExpectString(Tail.get(), "event", "frame_batch");
}

TEST_F(QmPerfDiagnosticsLogging, RevertingAfterPartialIncrementalEmissionClosesPendingRevision)
{
	char aValue[512] = "";
	char aOld[512] = "";
	SStringConfigVariable Variable(nullptr, "qm_test_text", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", aValue, "default", sizeof(aValue), aOld);
	CPerfConfigManager Manager;
	Manager.m_vVariables = {&Variable};
	CQmPerfConfigSnapshot Snapshot(&m_Operations);
	ASSERT_TRUE(Snapshot.Start(&Manager, nullptr));
	m_Capture.m_vMessages.clear();
	m_Operations.m_EmitCount = 0;
	m_Operations.m_FailAt = 2;
	str_copy(aValue, std::string(300, 'a').c_str());
	EXPECT_FALSE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	const auto Partial = Parse(0);
	ASSERT_NE(Partial, nullptr);
	ExpectInteger(Partial.get(), "revision", 2);
	ExpectInteger(Partial.get(), "parts", 3);
	str_copy(aValue, "default");
	m_Operations.m_FailAt = -1;
	EXPECT_TRUE(Snapshot.Update(nullptr));
	ASSERT_EQ(m_Capture.m_vMessages.size(), 2u);
	const auto Reverted = Parse(1);
	ASSERT_NE(Reverted, nullptr);
	ExpectString(Reverted.get(), "value", "default");
	ExpectInteger(Reverted.get(), "initial", 0);
	ExpectInteger(Reverted.get(), "is_default", 1);
	ExpectInteger(Reverted.get(), "revision", 3);
	ExpectInteger(Reverted.get(), "part", 0);
	ExpectInteger(Reverted.get(), "parts", 1);
	EXPECT_TRUE(Snapshot.Update(nullptr));
	EXPECT_EQ(m_Capture.m_vMessages.size(), 2u);
}
