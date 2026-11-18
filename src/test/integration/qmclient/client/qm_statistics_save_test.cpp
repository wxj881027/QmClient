#include <game/client/components/qmclient/statistics_file.h>
#include <game/client/components/qmclient/statistics_save_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	constexpr const char *STATISTICS_FILE = "qmclient/state/statistics.json";
	constexpr const char *SNAPSHOT = "{\"local\":{\"modes\":[]},\"remote\":{}}\n";
	constexpr const char *NEW_SNAPSHOT = "{\"local\":{\"modes\":[]},\"remote\":{\"qmclient\":{\"open_seconds\":\"123\"}}}\n";

	std::string ReadSnapshot(IStorage *pStorage)
	{
		char *pContent = pStorage->ReadFileStr(STATISTICS_FILE, IStorage::TYPE_SAVE);
		const std::string Result = pContent ? pContent : "";
		free(pContent);
		return Result;
	}
}

TEST(QmStatisticsSave, ConstructingSnapshotDoesNotWriteUntilDispatched)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CQmStatisticsSaveJob Job(pStorage.get(), STATISTICS_FILE, SNAPSHOT);
	EXPECT_FALSE(pStorage->FileExists(STATISTICS_FILE, IStorage::TYPE_SAVE));
	ASSERT_TRUE(Job.Finish());
	EXPECT_EQ(ReadSnapshot(pStorage.get()), SNAPSHOT);
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), STATISTICS_FILE, &pRoot), EQmStatisticsFileLoadResult::VALID);
	if(pRoot)
		json_value_free(pRoot);
}

TEST(QmStatisticsSave, BackgroundJobPersistsBeforeJobPoolShutdownCompletes)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	auto pJob = std::make_shared<CQmStatisticsSaveJob>(pStorage.get(), STATISTICS_FILE, SNAPSHOT);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_TRUE(pJob->Finish());
	EXPECT_EQ(ReadSnapshot(pStorage.get()), SNAPSHOT);
}

TEST(QmStatisticsSave, TakingOverQueuedJobPreventsItOverwritingLaterManualSave)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	auto pOldJob = std::make_shared<CQmStatisticsSaveJob>(pStorage.get(), STATISTICS_FILE, SNAPSHOT);
	ASSERT_TRUE(pOldJob->Finish());
	CQmStatisticsSaveJob NewJob(pStorage.get(), STATISTICS_FILE, NEW_SNAPSHOT);
	ASSERT_TRUE(NewJob.Finish());
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pOldJob);
	Pool.Shutdown();
	EXPECT_EQ(ReadSnapshot(pStorage.get()), NEW_SNAPSHOT);
}

TEST(QmStatisticsSave, FailedTemporaryWritePreservesPreviousFileAndFreshJobCanRetry)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CQmStatisticsSaveJob Initial(pStorage.get(), STATISTICS_FILE, SNAPSHOT);
	ASSERT_TRUE(Initial.Finish());
	char aTempFilename[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTempFilename, sizeof(aTempFilename), STATISTICS_FILE);
	ASSERT_TRUE(pStorage->CreateFolder(aTempFilename, IStorage::TYPE_SAVE));
	CQmStatisticsSaveJob Failed(pStorage.get(), STATISTICS_FILE, NEW_SNAPSHOT);
	EXPECT_FALSE(Failed.Finish());
	EXPECT_EQ(ReadSnapshot(pStorage.get()), SNAPSHOT);
	ASSERT_TRUE(pStorage->RemoveFolder(aTempFilename, IStorage::TYPE_SAVE));
	CQmStatisticsSaveJob Retry(pStorage.get(), STATISTICS_FILE, NEW_SNAPSHOT);
	EXPECT_TRUE(Retry.Finish());
	EXPECT_EQ(ReadSnapshot(pStorage.get()), NEW_SNAPSHOT);
}
