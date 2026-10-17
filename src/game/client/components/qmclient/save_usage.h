#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SAVE_USAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SAVE_USAGE_H

#include <engine/sqlite.h>

#include <sqlite3.h>

#include <string>

// 本地存档的使用标记（issue #199）。
// 不修改 ddnet-saves.txt 上游格式：使用状态存放在侧车 SQLite 库
// qmclient/save_usage.sqlite3 的 save_usage 表中（与 score_cache 同模式）。
namespace QmSaveUsage
{
	constexpr const char *const DATABASE_FILE = "qmclient/save_usage.sqlite3";

	// 存档键：地图名 + 0x1F 分隔 + 存档代码，与 RemoveLocalSaveByCode 的
	// (Map, Code) 匹配口径一致，是 ddnet-saves.txt 中最稳定的条目标识。
	inline std::string SaveKey(const char *pMap, const char *pCode)
	{
		std::string Key(pMap ? pMap : "");
		Key.push_back('\x1f');
		Key += pCode ? pCode : "";
		return Key;
	}

	inline bool EnsureTable(IConsole *pConsole, sqlite3 *pSqlite)
	{
		static const char TABLE[] =
			"CREATE TABLE IF NOT EXISTS save_usage "
			"(save_key TEXT PRIMARY KEY NOT NULL, map_name TEXT NOT NULL, used_at TEXT NOT NULL)";
		return SqliteHandleError(pConsole, sqlite3_exec(pSqlite, TABLE, nullptr, nullptr, nullptr), pSqlite, "create save_usage") == SQLITE_OK;
	}

	// 标记一次使用：upsert，重复使用刷新 used_at（UTC）。
	inline bool MarkUsed(IConsole *pConsole, sqlite3 *pSqlite, const std::string &Key, const char *pMap)
	{
		CSqliteStmt pStmt = SqlitePrepare(pConsole, pSqlite,
			"INSERT OR REPLACE INTO save_usage (save_key, map_name, used_at) VALUES (?1, ?2, datetime('now'))");
		if(!pStmt)
			return false;
		bool Error = false;
		Error = Error || SqliteHandleError(pConsole, sqlite3_bind_text(pStmt.get(), 1, Key.c_str(), -1, SQLITE_TRANSIENT), pSqlite, "bind key") != SQLITE_OK;
		Error = Error || SqliteHandleError(pConsole, sqlite3_bind_text(pStmt.get(), 2, pMap ? pMap : "", -1, SQLITE_TRANSIENT), pSqlite, "bind map") != SQLITE_OK;
		Error = Error || SqliteHandleError(pConsole, sqlite3_step(pStmt.get()), pSqlite, "step mark used") != SQLITE_DONE;
		return !Error;
	}

	inline bool IsUsed(IConsole *pConsole, sqlite3 *pSqlite, const std::string &Key)
	{
		CSqliteStmt pStmt = SqlitePrepare(pConsole, pSqlite,
			"SELECT 1 FROM save_usage WHERE save_key = ?1");
		if(!pStmt)
			return false;
		if(SqliteHandleError(pConsole, sqlite3_bind_text(pStmt.get(), 1, Key.c_str(), -1, SQLITE_TRANSIENT), pSqlite, "bind key") != SQLITE_OK)
			return false;
		return sqlite3_step(pStmt.get()) == SQLITE_ROW;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_SAVE_USAGE_H
