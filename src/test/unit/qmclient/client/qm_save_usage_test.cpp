// 存档使用标记（issue #199）的 upsert/查询 round-trip 单测：临时 SQLite 库 + save_usage 表。
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/save_usage.h>

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <unordered_set>

TEST(QmSaveUsage, SaveKeyCombinesMapAndCode)
{
	// 0x1F 分隔符用三字符拼接书写，避免 \x1f 吞并后续十六进制字符。
	EXPECT_EQ(QmSaveUsage::SaveKey("Kobra 4", "abc123"), std::string("Kobra 4") + "\x1f" + "abc123");
	EXPECT_EQ(QmSaveUsage::SaveKey(nullptr, nullptr), std::string() + "\x1f");
	EXPECT_EQ(QmSaveUsage::SaveKey("Map", ""), std::string("Map") + "\x1f");
}

TEST(QmSaveUsage, MarkAndQueryRoundTrip)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";

	CSqlite pDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test.sqlite3");
	ASSERT_TRUE(pDb) << "Error opening save usage test database";
	ASSERT_TRUE(QmSaveUsage::EnsureTable(pConsole.get(), pDb.get()));

	const std::string Key = QmSaveUsage::SaveKey("Kobra 4", "abc123");
	EXPECT_FALSE(QmSaveUsage::IsUsed(pConsole.get(), pDb.get(), Key));

	EXPECT_TRUE(QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), Key, "Kobra 4"));
	EXPECT_TRUE(QmSaveUsage::IsUsed(pConsole.get(), pDb.get(), Key));

	// 其他存档不受影响。
	EXPECT_FALSE(QmSaveUsage::IsUsed(pConsole.get(), pDb.get(), QmSaveUsage::SaveKey("Kobra 4", "other")));
	EXPECT_FALSE(QmSaveUsage::IsUsed(pConsole.get(), pDb.get(), QmSaveUsage::SaveKey("Yo-star", "abc123")));
}

TEST(QmSaveUsage, LoadUsedKeysMirrorsAllRows)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";

	CSqlite pDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test.sqlite3");
	ASSERT_TRUE(pDb) << "Error opening save usage test database";
	ASSERT_TRUE(QmSaveUsage::EnsureTable(pConsole.get(), pDb.get()));

	const std::string KeyA = QmSaveUsage::SaveKey("Map A", "code-a");
	const std::string KeyB = QmSaveUsage::SaveKey("Map B", "code-b");
	ASSERT_TRUE(QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), KeyA, "Map A"));
	ASSERT_TRUE(QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), KeyB, "Map B"));

	// 内存镜像应包含全部已标记行；空库/未标记键不出现。
	std::unordered_set<std::string> vKeys;
	EXPECT_TRUE(QmSaveUsage::LoadUsedKeys(pConsole.get(), pDb.get(), vKeys));
	EXPECT_EQ(vKeys.size(), 2u);
	EXPECT_NE(vKeys.find(KeyA), vKeys.end());
	EXPECT_NE(vKeys.find(KeyB), vKeys.end());

	std::unordered_set<std::string> vEmpty;
	CSqlite pEmptyDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test_empty.sqlite3");
	ASSERT_TRUE(pEmptyDb) << "Error opening empty save usage test database";
	ASSERT_TRUE(QmSaveUsage::EnsureTable(pConsole.get(), pEmptyDb.get()));
	EXPECT_TRUE(QmSaveUsage::LoadUsedKeys(pConsole.get(), pEmptyDb.get(), vEmpty));
	EXPECT_TRUE(vEmpty.empty());
}

TEST(QmSaveUsage, RepeatedMarkUpsertsSingleRow)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";

	CSqlite pDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test.sqlite3");
	ASSERT_TRUE(pDb) << "Error opening save usage test database";
	ASSERT_TRUE(QmSaveUsage::EnsureTable(pConsole.get(), pDb.get()));

	const std::string Key = QmSaveUsage::SaveKey("Map A", "code");
	EXPECT_TRUE(QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), Key, "Map A"));
	EXPECT_TRUE(QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), Key, "Map A"));

	// upsert 不应产生重复行。
	CSqliteStmt pCount = SqlitePrepare(pConsole.get(), pDb.get(), "SELECT COUNT(*), map_name FROM save_usage WHERE save_key = ?1");
	ASSERT_TRUE(pCount);
	ASSERT_EQ(sqlite3_bind_text(pCount.get(), 1, Key.c_str(), -1, SQLITE_TRANSIENT), SQLITE_OK);
	ASSERT_EQ(sqlite3_step(pCount.get()), SQLITE_ROW);
	EXPECT_EQ(sqlite3_column_int(pCount.get(), 0), 1);
	EXPECT_STREQ(reinterpret_cast<const char *>(sqlite3_column_text(pCount.get(), 1)), "Map A");
}

TEST(QmSaveUsage, UsageSurvivesReopenAndStoresTimestamp)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;

	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr) << "Error creating test storage";

	const std::string Key = QmSaveUsage::SaveKey("Map B", "xyz");
	{
		CSqlite pDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test.sqlite3");
		ASSERT_TRUE(pDb) << "Error opening save usage test database";
		ASSERT_TRUE(QmSaveUsage::EnsureTable(pConsole.get(), pDb.get()));
		const bool Marked = QmSaveUsage::MarkUsed(pConsole.get(), pDb.get(), Key, "Map B");
		EXPECT_TRUE(Marked);
	}

	// 重新打开后（等同客户端重启）标记仍在，且 used_at 是可读的 UTC 时间戳。
	{
		CSqlite pDb = SqliteOpen(pConsole.get(), pStorage.get(), "qmclient_save_usage_test.sqlite3");
		ASSERT_TRUE(pDb) << "Error reopening save usage test database";
		const bool Used = QmSaveUsage::IsUsed(pConsole.get(), pDb.get(), Key);
		EXPECT_TRUE(Used);

		CSqliteStmt pStmt = SqlitePrepare(pConsole.get(), pDb.get(), "SELECT used_at FROM save_usage WHERE save_key = ?1");
		ASSERT_TRUE(pStmt);
		ASSERT_EQ(sqlite3_bind_text(pStmt.get(), 1, Key.c_str(), -1, SQLITE_TRANSIENT), SQLITE_OK);
		ASSERT_EQ(sqlite3_step(pStmt.get()), SQLITE_ROW);
		const char *pUsedAt = reinterpret_cast<const char *>(sqlite3_column_text(pStmt.get(), 0));
		ASSERT_NE(pUsedAt, nullptr);
		EXPECT_EQ(strlen(pUsedAt), 19u) << "expected 'YYYY-MM-DD HH:MM:SS', got " << pUsedAt;
		EXPECT_EQ(pUsedAt[4], '-') << pUsedAt;
		EXPECT_EQ(pUsedAt[10], ' ') << pUsedAt;
	}
}
