#include <game/client/components/qmclient/perf_diagnostics.h>

#include <gtest/gtest.h>

#include <limits>
#include <new>
#include <string>
#include <vector>

namespace
{
	class CFrameOperations : public CQmPerfDiagnosticOperations
	{
	public:
		bool m_FailBeforeFormat = false;
		bool m_FailAfterFormat = false;
		bool m_FailEmit = false;
		int m_EmitAttempts = 0;
		std::vector<std::string> m_vFields;

		std::string FrameFields(const uint64_t *pFrames, const double *pDurations, size_t Count) override
		{
			if(m_FailBeforeFormat)
				throw std::bad_alloc();
			auto Fields = CQmPerfDiagnosticOperations::FrameFields(pFrames, pDurations, Count);
			if(m_FailAfterFormat)
				throw std::bad_alloc();
			return Fields;
		}

		void Emit(const char *, const std::string &Fields, const IClient *) override
		{
			++m_EmitAttempts;
			if(m_FailEmit)
				throw std::bad_alloc();
			m_vFields.push_back(Fields);
		}
	};
}

TEST(QmPerfFrameBatchFailure, EmptyFlushDoesNotFormatOrEmit)
{
	CFrameOperations Operations;
	Operations.m_FailBeforeFormat = true;
	Operations.m_FailEmit = true;
	CQmPerfFrameBatch Batch(&Operations);
	EXPECT_TRUE(Batch.Flush(nullptr));
	EXPECT_EQ(Operations.m_EmitAttempts, 0);
}

TEST(QmPerfFrameBatchFailure, LateFormattingFailureDoesNotConsumeTakeFields)
{
	CFrameOperations Operations;
	CQmPerfFrameBatch Batch(&Operations);
	Batch.Record(17, 2.125);
	Operations.m_FailAfterFormat = true;
	EXPECT_THROW(Batch.TakeFields(), std::bad_alloc);
	EXPECT_EQ(Batch.Count(), 1u);
	Operations.m_FailAfterFormat = false;
	EXPECT_EQ(Batch.TakeFields(), "\"event\":\"frame_batch\",\"frames\":[17],\"durations_ms\":[2.125]");
	EXPECT_EQ(Batch.Count(), 0u);
}

TEST(QmPerfFrameBatchFailure, FormattingFailureIsContainedAndTailCanBeFlushedLater)
{
	CFrameOperations Operations;
	CQmPerfFrameBatch Batch(&Operations);
	Batch.Record(4, 0.5);
	Operations.m_FailBeforeFormat = true;
	EXPECT_FALSE(Batch.Flush(nullptr));
	EXPECT_EQ(Batch.Count(), 1u);
	EXPECT_EQ(Operations.m_EmitAttempts, 0);
	Operations.m_FailBeforeFormat = false;
	EXPECT_TRUE(Batch.Flush(nullptr));
	ASSERT_EQ(Operations.m_vFields.size(), 1u);
	EXPECT_EQ(Operations.m_vFields[0], "\"event\":\"frame_batch\",\"frames\":[4],\"durations_ms\":[0.500]");
}

TEST(QmPerfFrameBatchFailure, EmissionFailurePreservesCompletedPayloadAndElapsedTime)
{
	CFrameOperations Operations;
	CQmPerfFrameBatch Batch(&Operations);
	Operations.m_FailEmit = true;
	EXPECT_FALSE(Batch.RecordAndLog(8, 1000.0, nullptr));
	EXPECT_EQ(Batch.Count(), 1u);
	Operations.m_FailEmit = false;
	EXPECT_TRUE(Batch.RecordAndLog(9, 0.5, nullptr));
	ASSERT_EQ(Operations.m_vFields.size(), 1u);
	EXPECT_EQ(Operations.m_vFields[0], "\"event\":\"frame_batch\",\"frames\":[8,9],\"durations_ms\":[1000.000,0.500]");
	EXPECT_EQ(Batch.Count(), 0u);
	EXPECT_FALSE(Batch.Record(10, 500.0));
}

TEST(QmPerfFrameBatchFailure, RecordingAnAlreadyFullBatchDoesNotOverwriteStoredFrames)
{
	CFrameOperations Operations;
	CQmPerfFrameBatch Batch(&Operations);
	for(uint64_t Frame = 1; Frame <= 64; ++Frame)
		Batch.Record(Frame, 0.5);
	EXPECT_TRUE(Batch.Record(900, 1.0));
	EXPECT_EQ(Batch.Count(), 64u);
	const auto Fields = Batch.TakeFields();
	EXPECT_NE(Fields.find("\"frames\":[1,2,3,"), std::string::npos);
	EXPECT_NE(Fields.find(",63,64]"), std::string::npos);
	EXPECT_EQ(Fields.find("900"), std::string::npos);
}

TEST(QmPerfFrameBatchFailure, RepeatedFailuresKeepFullBatchBounded)
{
	CFrameOperations Operations;
	Operations.m_FailEmit = true;
	CQmPerfFrameBatch Batch(&Operations);
	for(uint64_t Frame = 1; Frame < 64; ++Frame)
		EXPECT_TRUE(Batch.RecordAndLog(Frame, 0.5, nullptr));
	EXPECT_FALSE(Batch.RecordAndLog(64, 0.5, nullptr));
	for(uint64_t Frame = 65; Frame <= 256; ++Frame)
	{
		EXPECT_FALSE(Batch.RecordAndLog(Frame, 0.5, nullptr));
		EXPECT_EQ(Batch.Count(), 64u);
	}
	EXPECT_EQ(Operations.m_EmitAttempts, 193);
	EXPECT_TRUE(Operations.m_vFields.empty());
}

TEST(QmPerfFrameBatchFailure, RecoverySendsOldFullBatchThenKeepsCurrentFrame)
{
	CFrameOperations Operations;
	Operations.m_FailAfterFormat = true;
	CQmPerfFrameBatch Batch(&Operations);
	for(uint64_t Frame = 1; Frame <= 64; ++Frame)
		Batch.RecordAndLog(Frame, 0.5, nullptr);
	EXPECT_FALSE(Batch.RecordAndLog(65, 0.5, nullptr));
	Operations.m_FailAfterFormat = false;
	EXPECT_TRUE(Batch.RecordAndLog(66, 1.0, nullptr));
	ASSERT_EQ(Operations.m_vFields.size(), 1u);
	EXPECT_EQ(Batch.Count(), 1u);
	EXPECT_TRUE(Batch.Flush(nullptr));
	ASSERT_EQ(Operations.m_vFields.size(), 2u);
	EXPECT_NE(Operations.m_vFields[0].find(",63,64]"), std::string::npos);
	EXPECT_EQ(Operations.m_vFields[1], "\"event\":\"frame_batch\",\"frames\":[66],\"durations_ms\":[1.000]");
}

TEST(QmPerfFrameBatchFailure, RecoveryCanFlushOldAndCurrentSlowFrameInOneCall)
{
	CFrameOperations Operations;
	Operations.m_FailEmit = true;
	CQmPerfFrameBatch Batch(&Operations);
	for(uint64_t Frame = 1; Frame <= 64; ++Frame)
		Batch.RecordAndLog(Frame, 0.5, nullptr);
	Operations.m_FailEmit = false;
	EXPECT_TRUE(Batch.RecordAndLog(65, 1000.0, nullptr));
	EXPECT_EQ(Operations.m_vFields.size(), 2u);
	EXPECT_EQ(Batch.Count(), 0u);
}

TEST(QmPerfFrameBatchFailure, InvalidDurationsDoNotOccupySamples)
{
	CQmPerfFrameBatch Batch;
	for(const double Duration : {0.0, -1.0, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		EXPECT_TRUE(Batch.RecordAndLog(1, Duration, nullptr));
		EXPECT_EQ(Batch.Count(), 0u);
	}
}
