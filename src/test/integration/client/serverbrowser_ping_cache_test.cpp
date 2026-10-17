#include <base/system.h>

#include <engine/client/serverbrowser_ping_cache.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <test/test.h>

#include <iterator>
#include <memory>

namespace
{
	class ServerBrowserPingCache : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IConsole> m_pConsole = CreateConsole(CFGFLAG_CLIENT);
		std::unique_ptr<IStorage> m_pStorage;
		CSqlite m_pDisk;
		std::unique_ptr<IServerBrowserPingCache> m_pCache;
		NETADDR m_Localhost4{}, m_Localhost6{};
		const int m_OldMaxAge = g_Config.m_QmPingCacheMaxAgeHours;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_TRUE(m_pStorage);
			g_Config.m_QmPingCacheMaxAgeHours = 0;
			ASSERT_EQ(net_addr_from_str(&m_Localhost4, "127.0.0.1:8303"), 0);
			ASSERT_EQ(net_addr_from_str(&m_Localhost6, "[::1]:8304"), 0);
		}
		void TearDown() override { g_Config.m_QmPingCacheMaxAgeHours = m_OldMaxAge; }
		void CreateCache()
		{
			m_pCache.reset(CreateServerBrowserPingCache(m_pConsole.get(), m_pStorage.get()));
		}
		int Execute(const char *pSql)
		{
			if(!m_pDisk)
				m_pDisk = SqliteOpen(m_pConsole.get(), m_pStorage.get(), "ddnet-cache.sqlite3");
			return m_pDisk ? sqlite3_exec(m_pDisk.get(), pSql, nullptr, nullptr, nullptr) : SQLITE_CANTOPEN;
		}
	};
}

TEST_F(ServerBrowserPingCache, EmptyCacheAndEmptyAddressListReturnUnknown)
{
	CreateCache();
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 0);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), -1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), -1);
	EXPECT_EQ(m_pCache->GetPing(nullptr, 0), -1);
}

TEST_F(ServerBrowserPingCache, LatestMeasurementReplacesTheSameIpAcrossPortAndProtocol)
{
	CreateCache();
	NETADDR Seven = m_Localhost4;
	Seven.type |= NETTYPE_TW7;
	Seven.port = 8305;
	m_pCache->CachePing(m_Localhost4, 913);
	m_pCache->CachePing(Seven, 234);
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 234);
	EXPECT_EQ(m_pCache->GetPing(&Seven, 1), 234);
}

TEST_F(ServerBrowserPingCache, MultipleAddressesUseTheLowestAvailableMeasurement)
{
	CreateCache();
	m_pCache->CachePing(m_Localhost4, 234);
	m_pCache->CachePing(m_Localhost6, 345);
	const NETADDR aAddresses[] = {m_Localhost4, m_Localhost6};
	EXPECT_EQ(m_pCache->GetPing(aAddresses, std::size(aAddresses)), 234);
	m_pCache->CachePing(m_Localhost4, 1337);
	EXPECT_EQ(m_pCache->GetPing(aAddresses, std::size(aAddresses)), 345);
}

TEST_F(ServerBrowserPingCache, ReopeningAndRepeatedLoadsKeepPersistedMeasurements)
{
	CreateCache();
	m_pCache->CachePing(m_Localhost4, 1337);
	m_pCache->CachePing(m_Localhost6, 345);
	CreateCache();
	for(int Load = 0; Load < 2; ++Load)
	{
		SCOPED_TRACE(Load);
		m_pCache->Load();
		EXPECT_EQ(m_pCache->NumEntries(), 2);
		EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 1337);
		EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), 345);
	}
}

TEST_F(ServerBrowserPingCache, AgeLimitIgnoresOldMeasurementsButKeepsRecentOnes)
{
	CreateCache();
	ASSERT_EQ(Execute("INSERT INTO server_pings VALUES ('127.0.0.1', 171, datetime('now', '-30 days')), ('[::1]', 123, datetime('now'))"), SQLITE_OK);
	g_Config.m_QmPingCacheMaxAgeHours = 72;
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 2);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), -1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), 123);
}

TEST_F(ServerBrowserPingCache, AgeLimitIgnoresUnknownTimestamps)
{
	CreateCache();
	ASSERT_EQ(Execute("INSERT INTO server_pings VALUES ('127.0.0.1', 171, 'invalid timestamp')"), SQLITE_OK);
	g_Config.m_QmPingCacheMaxAgeHours = 72;
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), -1);
}

TEST_F(ServerBrowserPingCache, DisablingAgeLimitRestoresOldAndUnknownMeasurements)
{
	CreateCache();
	ASSERT_EQ(Execute("INSERT INTO server_pings VALUES ('127.0.0.1', 171, datetime('now', '-30 days')), ('[::1]', 123, 'invalid timestamp')"), SQLITE_OK);
	g_Config.m_QmPingCacheMaxAgeHours = 72;
	m_pCache->Load();
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), -1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), -1);
	g_Config.m_QmPingCacheMaxAgeHours = 0;
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 171);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), 123);
}

TEST_F(ServerBrowserPingCache, NullEmptyAndInvalidAddressesDoNotHideLaterValidRows)
{
	// 旧数据库可能已有可空表，生产的 IF NOT EXISTS 不会替换它。
	ASSERT_EQ(Execute("CREATE TABLE server_pings (ip_address, ping, utc_timestamp)"), SQLITE_OK);
	ASSERT_EQ(Execute("INSERT INTO server_pings VALUES (NULL, 100, datetime('now')), ('', 101, datetime('now')), (X'', 102, datetime('now')), ('invalid', 103, datetime('now')), ('127.0.0.1', 104, datetime('now'))"), SQLITE_OK);
	CreateCache();
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 104);
}

TEST_F(ServerBrowserPingCache, FailedReadKeepsOldCacheAndNextLoadCanRecover)
{
	CreateCache();
	m_pCache->CachePing(m_Localhost4, 100);
	// 真实 SQLite 查询先产出一行，再在下一行触发整数溢出错误。
	ASSERT_EQ(Execute("DROP TABLE server_pings; CREATE VIEW server_pings AS SELECT '127.0.0.1' AS ip_address, 200 AS ping, datetime('now') AS utc_timestamp UNION ALL SELECT '[::1]', 300, abs(-9223372036854775808)"), SQLITE_OK);
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 100);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost6, 1), -1);
	ASSERT_EQ(Execute("DROP VIEW server_pings; CREATE TABLE server_pings (ip_address TEXT PRIMARY KEY NOT NULL, ping INTEGER NOT NULL, utc_timestamp TEXT NOT NULL); INSERT INTO server_pings VALUES ('127.0.0.1', 400, datetime('now'))"), SQLITE_OK);
	m_pCache->Load();
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 400);
}

TEST_F(ServerBrowserPingCache, FailedWriteKeepsMemoryAndNextMeasurementPersists)
{
	CreateCache();
	ASSERT_EQ(Execute("CREATE TRIGGER fail_ping BEFORE INSERT ON server_pings BEGIN SELECT RAISE(ABORT, 'injected write failure'); END"), SQLITE_OK);
	m_pCache->CachePing(m_Localhost4, 111);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 111);
	ASSERT_EQ(Execute("DROP TRIGGER fail_ping"), SQLITE_OK);
	m_pCache->CachePing(m_Localhost4, 222);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 222);
	CreateCache();
	m_pCache->Load();
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 222);
}

TEST_F(ServerBrowserPingCache, IncompatibleTableKeepsInMemoryCacheUsable)
{
	ASSERT_EQ(Execute("CREATE TABLE server_pings (unrelated_column INTEGER)"), SQLITE_OK);
	CreateCache();
	m_pCache->CachePing(m_Localhost4, 85);
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 85);
}

TEST_F(ServerBrowserPingCache, InvalidDatabaseFileKeepsInMemoryCacheUsable)
{
	std::unique_ptr<void, decltype(&io_close)> pFile(m_pStorage->OpenFile("ddnet-cache.sqlite3", IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
	ASSERT_TRUE(pFile);
	constexpr char aInvalid[] = "not a sqlite database";
	ASSERT_EQ(io_write(pFile.get(), aInvalid, sizeof(aInvalid)), sizeof(aInvalid));
	pFile.reset();
	CreateCache();
	m_pCache->CachePing(m_Localhost4, 85);
	m_pCache->Load();
	EXPECT_EQ(m_pCache->NumEntries(), 1);
	EXPECT_EQ(m_pCache->GetPing(&m_Localhost4, 1), 85);
}
