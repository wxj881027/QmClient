#include "serverbrowser_ping_cache.h"

#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/sqlite.h>

#include <sqlite3.h>

#include <unordered_map>
#include <vector>

namespace
{

	class CServerBrowserPingCache : public IServerBrowserPingCache
	{
	public:
		class CEntry
		{
		public:
			NETADDR m_Addr;
			int m_Ping;
			// 缓存写入时间（UTC 秒），0 表示时间未知。用于丢弃过期的历史测量。
			int64_t m_UtcTimestamp;
		};

		CServerBrowserPingCache(IConsole *pConsole, IStorage *pStorage);
		~CServerBrowserPingCache() override = default;

		void Load() override;

		int NumEntries() const override;
		void CachePing(const NETADDR &Addr, int Ping) override;
		int GetPing(const NETADDR *pAddrs, int NumAddrs) const override;

	private:
		IConsole *m_pConsole;

		CSqlite m_pDisk;
		CSqliteStmt m_pLoadStmt;
		CSqliteStmt m_pStoreStmt;

		std::unordered_map<NETADDR, CEntry> m_Entries;
	};

	CServerBrowserPingCache::CServerBrowserPingCache(IConsole *pConsole, IStorage *pStorage) :
		m_pConsole(pConsole)
	{
		m_pDisk = SqliteOpen(pConsole, pStorage, "ddnet-cache.sqlite3");
		if(!m_pDisk)
		{
			pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowse_ping_cache", "failed to open ddnet-cache.sqlite3");
			return;
		}
		sqlite3 *pSqlite = m_pDisk.get();
		static const char TABLE[] = "CREATE TABLE IF NOT EXISTS server_pings (ip_address TEXT PRIMARY KEY NOT NULL, ping INTEGER NOT NULL, utc_timestamp TEXT NOT NULL)";
		if(SQLITE_HANDLE_ERROR(sqlite3_exec(pSqlite, TABLE, nullptr, nullptr, nullptr)))
		{
			m_pDisk = nullptr;
			pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowse_ping_cache", "failed to create server_pings table");
			return;
		}
		m_pLoadStmt = SqlitePrepare(pConsole, pSqlite, "SELECT ip_address, ping, strftime('%s', utc_timestamp) FROM server_pings");
		m_pStoreStmt = SqlitePrepare(pConsole, pSqlite, "INSERT OR REPLACE INTO server_pings (ip_address, ping, utc_timestamp) VALUES (?, ?, datetime('now'))");
	}

	void CServerBrowserPingCache::Load()
	{
		if(m_pDisk)
		{
			std::vector<CEntry> vNewEntries;

			sqlite3 *pSqlite = m_pDisk.get();
			IConsole *pConsole = m_pConsole;
			bool Error = false;
			bool WarnedForBadAddress = false;
			Error = Error || !m_pLoadStmt;
			Error = Error || SQLITE_HANDLE_ERROR(sqlite3_reset(m_pLoadStmt.get())) != SQLITE_OK;
			while(!Error)
			{
				const int StepResult = sqlite3_step(m_pLoadStmt.get());
				if(StepResult == SQLITE_DONE)
				{
					break;
				}
				else if(StepResult == SQLITE_ROW)
				{
					const char *pIpAddress = (const char *)sqlite3_column_text(m_pLoadStmt.get(), 0);
					const int Ping = sqlite3_column_int(m_pLoadStmt.get(), 1);
					// strftime 在时间戳格式异常时返回 NULL，这种记录按"时间未知"处理。
					const bool HasUtcTimestamp = sqlite3_column_type(m_pLoadStmt.get(), 2) != SQLITE_NULL;
					const int64_t UtcTimestamp = HasUtcTimestamp ? sqlite3_column_int64(m_pLoadStmt.get(), 2) : 0;
					NETADDR Addr;
					if(net_addr_from_str(&Addr, pIpAddress))
					{
						if(!WarnedForBadAddress)
						{
							char aBuf[64];
							str_format(aBuf, sizeof(aBuf), "invalid address: %s", pIpAddress);
							pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowse_ping_cache", aBuf);
							WarnedForBadAddress = true;
						}
						continue;
					}
					vNewEntries.push_back(CEntry{Addr, Ping, UtcTimestamp});
				}
				else
				{
					SqliteHandleError(pConsole, StepResult, pSqlite, "sqlite3_step(m_pLoadStmt.get())");
					Error = true;
				}
			}
			if(Error)
			{
				pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowse_ping_cache", "failed to load ping cache");
				return;
			}
			for(const CEntry &Entry : vNewEntries)
			{
				m_Entries[Entry.m_Addr] = Entry;
			}
		}
	}

	int CServerBrowserPingCache::NumEntries() const
	{
		return m_Entries.size();
	}

	void CServerBrowserPingCache::CachePing(const NETADDR &Addr, int Ping)
	{
		NETADDR StoredAddr = Addr;
		StoredAddr.type &= ~NETTYPE_TW7;
		StoredAddr.port = 0;
		m_Entries[StoredAddr] = CEntry{StoredAddr, Ping, time_timestamp()};
		if(m_pDisk)
		{
			sqlite3 *pSqlite = m_pDisk.get();
			IConsole *pConsole = m_pConsole;
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&StoredAddr, aAddr, sizeof(aAddr), false);

			bool Error = false;
			Error = Error || !m_pStoreStmt;
			Error = Error || SQLITE_HANDLE_ERROR(sqlite3_reset(m_pStoreStmt.get())) != SQLITE_OK;
			Error = Error || SQLITE_HANDLE_ERROR(sqlite3_bind_text(m_pStoreStmt.get(), 1, aAddr, -1, SQLITE_STATIC)) != SQLITE_OK;
			Error = Error || SQLITE_HANDLE_ERROR(sqlite3_bind_int(m_pStoreStmt.get(), 2, Ping)) != SQLITE_OK;
			Error = Error || SQLITE_HANDLE_ERROR(sqlite3_step(m_pStoreStmt.get())) != SQLITE_DONE;
			if(Error)
			{
				pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "serverbrowse_ping_cache", "failed to store ping");
			}
		}
	}

	int CServerBrowserPingCache::GetPing(const NETADDR *pAddrs, int NumAddrs) const
	{
		// 超过 qm_ping_cache_max_age_hours 的记录按"未知"处理：延迟列回落到地区估算，
		// 不再把很久以前的测量（或时间未知的旧记录）当成当前延迟显示。
		const int MaxAgeHours = g_Config.m_QmPingCacheMaxAgeHours;
		const int64_t Now = time_timestamp();
		int Ping = -1;
		for(int i = 0; i < NumAddrs; i++)
		{
			NETADDR LookupAddr = pAddrs[i];
			LookupAddr.type &= ~NETTYPE_TW7;
			LookupAddr.port = 0;
			auto Entry = m_Entries.find(LookupAddr);
			if(Entry == m_Entries.end())
			{
				continue;
			}
			if(MaxAgeHours > 0)
			{
				const int64_t MaxAge = static_cast<int64_t>(MaxAgeHours) * 60 * 60;
				if(Entry->second.m_UtcTimestamp <= 0 || Now - Entry->second.m_UtcTimestamp > MaxAge)
				{
					continue;
				}
			}
			if(Ping == -1 || Entry->second.m_Ping < Ping)
			{
				Ping = Entry->second.m_Ping;
			}
		}
		return Ping;
	}

} // namespace

IServerBrowserPingCache *CreateServerBrowserPingCache(IConsole *pConsole, IStorage *pStorage)
{
	return new CServerBrowserPingCache(pConsole, pStorage);
}
