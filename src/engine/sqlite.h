#ifndef ENGINE_SQLITE_H
#define ENGINE_SQLITE_H
#include <memory>

struct sqlite3;
struct sqlite3_stmt;
class IStorage;

class CSqliteDeleter
{
public:
	void operator()(sqlite3 *pSqlite);
};
class CSqliteStmtDeleter
{
public:
	void operator()(sqlite3_stmt *pStmt);
};
typedef std::unique_ptr<sqlite3, CSqliteDeleter> CSqlite;
typedef std::unique_ptr<sqlite3_stmt, CSqliteStmtDeleter> CSqliteStmt;

int SqliteHandleError(int Error, sqlite3 *pSqlite, const char *pContext);
// 操作可能通过输出参数设置 pSqlite，先执行操作再读取数据库句柄。
#define SQLITE_HANDLE_ERROR(x) ([&](int Result) { return SqliteHandleError(Result, pSqlite, #x); }(x))

CSqlite SqliteOpen(IStorage *pStorage, const char *pPath);
CSqliteStmt SqlitePrepare(sqlite3 *pSqlite, const char *pStatement);
#endif // ENGINE_SQLITE_H
