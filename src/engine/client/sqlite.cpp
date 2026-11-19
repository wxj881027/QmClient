#include <base/log.h>
#include <base/str.h>
#include <base/types.h>

#include <engine/sqlite.h>
#include <engine/storage.h>

#include <sqlite3.h>

void CSqliteDeleter::operator()(sqlite3 *pSqlite)
{
	sqlite3_close(pSqlite);
}

void CSqliteStmtDeleter::operator()(sqlite3_stmt *pStmt)
{
	sqlite3_finalize(pStmt);
}

int SqliteHandleError(int Error, sqlite3 *pSqlite, const char *pContext)
{
	if(Error != SQLITE_OK && Error != SQLITE_DONE && Error != SQLITE_ROW)
	{
		const char *pMessage = pSqlite ? sqlite3_errmsg(pSqlite) : nullptr;
		if(!pMessage)
			pMessage = sqlite3_errstr(Error);
		log_error("sqlite3", "%s at %s", pMessage ? pMessage : "unknown SQLite error", pContext);
	}
	return Error;
}

CSqlite SqliteOpen(IStorage *pStorage, const char *pPath)
{
	char aFullPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aFullPath, sizeof(aFullPath));
	sqlite3 *pSqlite = nullptr;
	const int OpenResult = sqlite3_open(aFullPath, &pSqlite);
	// 打开失败时也可能返回句柄。先接管它，再读取本次打开的错误。
	CSqlite pResult{pSqlite};
	if(SqliteHandleError(OpenResult, pResult.get(), "sqlite3_open") != SQLITE_OK)
	{
		return nullptr;
	}
	bool Error = false;
	Error = Error || SQLITE_HANDLE_ERROR(sqlite3_exec(pSqlite, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr));
	Error = Error || SQLITE_HANDLE_ERROR(sqlite3_exec(pSqlite, "PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr));
	if(Error)
	{
		return nullptr;
	}
	return pResult;
}

CSqliteStmt SqlitePrepare(sqlite3 *pSqlite, const char *pStatement)
{
	sqlite3_stmt *pTemp;
	if(SQLITE_HANDLE_ERROR(sqlite3_prepare_v2(pSqlite, pStatement, -1, &pTemp, nullptr)))
	{
		return nullptr;
	}
	return CSqliteStmt{pTemp};
}
