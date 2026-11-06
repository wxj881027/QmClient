#include <game/client/components/qmclient/statistics_file.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	void WriteStorageFile(IStorage *pStorage, const char *pPath, std::string_view Content)
	{
		IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		EXPECT_EQ(io_write(File, Content.data(), static_cast<unsigned>(Content.size())), Content.size());
		EXPECT_EQ(io_close(File), 0);
	}

	std::string ReadStorageFile(IStorage *pStorage, const char *pPath)
	{
		void *pData = nullptr;
		unsigned Size = 0;
		const auto Result = pStorage->ReadFileChecked(pPath, IStorage::TYPE_SAVE, &pData, &Size);
		std::unique_ptr<void, decltype(&free)> Data(pData, free);
		EXPECT_EQ(Result, IStorage::EReadFileResult::SUCCESS);
		return Data ? std::string(static_cast<const char *>(Data.get()), Size) : "";
	}
}

TEST(QmStatisticsFile, DistinguishesMissingInvalidAndValidDocuments)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::NOT_FOUND);
	EXPECT_EQ(pRoot, nullptr);

	const char *pInvalidJson = "{\"local\":";
	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", pInvalidJson);
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/statistics.json"), pInvalidJson);

	const char *pInvalidStructure = "{\"local\":{}}";
	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", pInvalidStructure);
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/statistics.json"), pInvalidStructure);

	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", "{\"local\":{\"modes\":[]},\"remote\":{}}");
	ASSERT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::VALID);
	ASSERT_NE(pRoot, nullptr);
	EXPECT_EQ(json_object_get(json_object_get(pRoot, "local"), "modes")->type, json_array);
	json_value_free(pRoot);
}

TEST(QmStatisticsFile, ClearsOutputForInvalidArguments)
{
	json_value DummyRoot;
	json_value *pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);

	pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, nullptr, &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);

	pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "qmclient/statistics.json", nullptr), EQmStatisticsFileLoadResult::INVALID);
}

TEST(QmStatisticsFile, UnreadableDirectoryIsInvalidRatherThanMissing)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder("qmclient/statistics.json", IStorage::TYPE_SAVE));
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_TRUE(pStorage->FolderExists("qmclient/statistics.json", IStorage::TYPE_SAVE));
}

TEST(QmStatisticsFile, EmbeddedZeroIsRejectedWithoutChangingFile)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	const std::string Bytes = std::string(R"({"local":{},"remote":{}})") + '\0' + "other";
	ASSERT_NO_FATAL_FAILURE(WriteStorageFile(pStorage.get(), "qmclient/statistics.json", Bytes));
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/statistics.json"), Bytes);
}

TEST(QmStatisticsFile, EmptyExistingFileIsInvalidRatherThanMissing)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(WriteStorageFile(pStorage.get(), "qmclient/statistics.json", ""));
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_TRUE(pStorage->FileExists("qmclient/statistics.json", IStorage::TYPE_SAVE));
}

TEST(QmStatisticsFile, StoredDdnetDecimalStringKeepsPrecisionThroughFileReader)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	constexpr char JSON[] = R"({"local":{"modes":[]},"remote":{"ddnet":{"players":[{"name":"player","points":"9007199254740993","points_total":"9223372036854775807","finishes":1}]}}})";
	ASSERT_NO_FATAL_FAILURE(WriteStorageFile(pStorage.get(), "qmclient/statistics.json", JSON));
	json_value *pRoot = nullptr;
	ASSERT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::VALID);
	std::unique_ptr<json_value, decltype(&json_value_free)> Root(pRoot, json_value_free);
	const auto *pPlayers = qm_json::Field(qm_json::Field(qm_json::Field(Root.get(), "remote"), "ddnet"), "players");
	const auto *pPlayer = json_array_get(pPlayers, 0);
	int64_t Value = -1;
	ASSERT_TRUE(QmStatisticsNonNegativeInt64(qm_json::Field(pPlayer, "points"), Value));
	EXPECT_EQ(Value, INT64_C(9007199254740993));
	ASSERT_TRUE(QmStatisticsNonNegativeInt64(qm_json::Field(pPlayer, "points_total"), Value));
	EXPECT_EQ(Value, INT64_C(9223372036854775807));
}

TEST(QmStatisticsFile, OversizedOtherwiseValidDocumentRemainsOnDiskWithoutPublishingRoot)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	constexpr char PATH[] = "qmclient/statistics.json";
	constexpr char DOCUMENT[] = R"({"local":{},"remote":{}})";
	{
		std::unique_ptr<void, decltype(&io_close)> File(pStorage->OpenFile(PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
		ASSERT_NE(File, nullptr);
		ASSERT_EQ(io_write(File.get(), DOCUMENT, sizeof(DOCUMENT) - 1), sizeof(DOCUMENT) - 1);
		const std::string Padding(1024 * 1024, ' ');
		unsigned Remaining = QM_STATISTICS_MAX_FILE_BYTES + 1 - (sizeof(DOCUMENT) - 1);
		while(Remaining)
		{
			const unsigned Size = std::min(Remaining, static_cast<unsigned>(Padding.size()));
			ASSERT_EQ(io_write(File.get(), Padding.data(), Size), Size);
			Remaining -= Size;
		}
		ASSERT_EQ(io_close(File.release()), 0);
	}
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), PATH, &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	std::unique_ptr<void, decltype(&io_close)> File(pStorage->OpenFile(PATH, IOFLAG_READ, IStorage::TYPE_SAVE), io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_length(File.get()), static_cast<int64_t>(QM_STATISTICS_MAX_FILE_BYTES) + 1);
}
