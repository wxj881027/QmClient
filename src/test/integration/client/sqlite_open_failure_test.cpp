#include <base/logger.h>

#include <engine/sqlite.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <test/test.h>

#include <string_view>

TEST(SqliteOpenFailure, MissingParentDirectoryReportsTheOpenError)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_TRUE(pStorage);

	CMemoryLogger Logger;
	const CLogScope Scope(&Logger);
	EXPECT_FALSE(SqliteOpen(pStorage.get(), "missing-parent/cache.sqlite3"));
	const auto vLines = Logger.Lines();
	ASSERT_EQ(vLines.size(), 1U);
	EXPECT_STREQ(vLines[0].m_aSystem, "sqlite3");
	EXPECT_TRUE(std::string_view(vLines[0].Message()).starts_with(sqlite3_errstr(SQLITE_CANTOPEN))) << vLines[0].Message();
}

TEST(SqliteOpenFailure, ErrorMacroUsesTheHandleWrittenByFailedOpen)
{
	static constexpr char s_aMissingVfs[] = "qmclient-test-missing-sqlite-vfs";

	CMemoryLogger Logger;
	const CLogScope Scope(&Logger);
	sqlite3 *pSqlite = nullptr;
	const int Result = SQLITE_HANDLE_ERROR(sqlite3_open_v2(":memory:", &pSqlite, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, s_aMissingVfs));
	const CSqlite pDb{pSqlite};
	ASSERT_EQ(Result, SQLITE_ERROR);
	ASSERT_TRUE(pDb);

	const char *pMessage = sqlite3_errmsg(pDb.get());
	ASSERT_NE(pMessage, nullptr);
	ASSERT_STRNE(pMessage, sqlite3_errstr(Result));
	const auto vLines = Logger.Lines();
	ASSERT_EQ(vLines.size(), 1U);
	EXPECT_STREQ(vLines[0].m_aSystem, "sqlite3");
	EXPECT_TRUE(std::string_view(vLines[0].Message()).starts_with(pMessage)) << vLines[0].Message();
}
