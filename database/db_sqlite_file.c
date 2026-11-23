#include "vex.h"
#include "database/db_sqlite_file.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/** A catalog-only, read-only SQLite row: a borrowed path, capability caps, byte
 * bounds and the owner's fn-table, with no sqlite3.h include or link. It exists
 * so the directory can describe a SQLite source without owning a database
 * engine; execution delegates through the bound opaque handle, and an unbound
 * row drop-degrades to false. Read-only by construction, no write path exists
 * on this class. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DbSqliteFile (database/db_sqlite_file)
 * execution seam; swappable with zero code changes)
 * ============================================================================
 * Catalog-only read-only SQLite file row for the DbProvider directory:
 * a borrowed path, capability caps, byte bounds, and the owner's
 * fn-table, with zero sqlite3.h include/link. Execution delegates
 * through the bound opaque handle to the database owner (db-haven /
 * darkbase-engine Database interface) or vexspoke File/VFS; unbound exec
 * drop-degrades to false. Read-only by construction: no write path
 * exists on this class. Value struct, zero allocation, immutable reads
 * without locks.
 *
 * STRUCT FIELDS (Mirroring database/db_sqlite_file.h, exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   string path;              // borrowed file path (nullptr = undescribed)
 *   u32 caps;                 // capability bits (DB_SQLITE_CAP_*)
 *   u64 maxBytes;             // read bound (0 = unknown)
 *   bool readOnly;                 // always true on _0/_1; no writer exists
 *   any execHandle;              // borrowed owner handle (nullptr = unbound)
 *   const DbSqliteExec *execTable; // borrowed fn-table (nullptr = unbound)
 *
 * SLOT RECORD: none, this file owns one behavior class only (the
 * DbSqliteExec fn-table is a borrowed seam, not a row table).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs, no allocation):
 *   - DbSqliteFile_0()
 *   - DbSqliteFile_1(path)
 *
 * Core Functions:
 *   - DbSqliteFile_bind(self, handle, table)
 *   - DbSqliteFile_unbind(self)
 *   - DbSqliteFile_describe(self, out, outCap)
 *   - DbSqliteFile_exec(self, sql, out, outCap)
 *
 * Setters:
 *   - DbSqliteFile_setPath / setCaps / setMaxBytes / setReadOnly
 *
 * Getters:
 *   - DbSqliteFile_getPath / getCaps / getMaxBytes / isReadOnly /
 *     getExecHandle / getExecTable
 * ============================================================================
 */
;;INTENTION("catalog-only SQLite: api-haven describes the file and delegates reads, the driver, the engine, and sqlite3.h all live with the database owner (Rule 17)")
;;INTENTION("borrowed execution seam: handle + table are detach-only views, unbind before the owner frees, per Rule 26 (Rule 33 canonical move)")

/** CONSTRUCTORS */

/** Creates an undescribed read-only file descriptor with no execution binding. */
DbSqliteFile DbSqliteFile_0() {
    DbSqliteFile file;
    memset(&file, 0, sizeof(file));
    file.readOnly = true;
    return file;
}

/** Creates a read-only descriptor for the borrowed path with read capability. */
DbSqliteFile DbSqliteFile_1(string path) {
    DbSqliteFile file = DbSqliteFile();
    file.path = path;
    file.caps = DB_SQLITE_CAP_READ;
    return file;
}

/** CORE FUNCTIONS */

typedef bool (*DbSqliteExecFn)(any handle, string sql, utf8 *out, bytes outCap);

;;GETTER
static inline DbSqliteExecFn DbSqliteExec_exec(const DbSqliteExec *self)
{
    return (*self).exec;
}
;;SETTER
static inline fn DbSqliteFile_setExecHandle(DbSqliteFile *self, any execHandle)
{
    (*self).execHandle = execHandle;
}
;;SETTER
static inline fn DbSqliteFile_setExecTable(DbSqliteFile *self, const DbSqliteExec *execTable)
{
    (*self).execTable = execTable;
}

/** Binds a borrowed owner handle and its execution callback table. */
bool DbSqliteFile_bind(DbSqliteFile *self, any handle, const DbSqliteExec *table) {
    if (self == nullptr)
        return false;
    if (handle == nullptr || table == nullptr)
        return false;
    if (DbSqliteFile_getExecHandle(self) != nullptr)
        return false;
    if (DbSqliteExec_exec(table) == nullptr)
        return false;
    DbSqliteFile_setExecHandle(self, handle);
    DbSqliteFile_setExecTable(self, table);
    return true;
}

/** Clears the borrowed execution binding without destroying the owner. */
fn DbSqliteFile_unbind(DbSqliteFile *self) {
    if (self == nullptr)
        return;
    DbSqliteFile_setExecHandle(self, nullptr);
    DbSqliteFile_setExecTable(self, nullptr);
}

/** Formats this descriptor into out when the complete text fits. */
bool DbSqliteFile_describe(const DbSqliteFile *self, utf8 *out, bytes outCap) {
    if (self == nullptr)
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    string path = DbSqliteFile_getPath(self);
    if (path == nullptr || (*path) == '\0')
        return false;
    const i32 n = snprintf(out, outCap, "sqlite file: path=%s readOnly=%d caps=%u maxBytes=%llu",
                           path, DbSqliteFile_isReadOnly(self) ? 1 : 0,
                           DbSqliteFile_getCaps(self), (u64)DbSqliteFile_getMaxBytes(self));
    if (n <= 0 || (bytes)n >= outCap)
        return false;
    return true;
}

/** Delegates a read request to the bound database owner's execution callback. */
bool DbSqliteFile_exec(const DbSqliteFile *self, string sql,
                       utf8 *out, bytes outCap) {
    if (self == nullptr)
        return false;
    if (sql == nullptr || (*sql) == '\0')
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    any handle = DbSqliteFile_getExecHandle(self);
    const DbSqliteExec *table = DbSqliteFile_getExecTable(self);
    if (handle == nullptr || table == nullptr)
        return false;
    DbSqliteExecFn execFn = DbSqliteExec_exec(table);
    if (execFn == nullptr)
        return false;
    return execFn(handle, sql, out, outCap);
}

/** SETTERS */

/** Sets the borrowed path described by this catalog row. */
;;SETTER
fn DbSqliteFile_setPath(DbSqliteFile *self, string path) {
    if (self == nullptr)
        return;
    (*self).path = path;
}

/** Replaces the descriptor's advertised capability flags. */
;;SETTER
fn DbSqliteFile_setCaps(DbSqliteFile *self, u32 caps) {
    if (self == nullptr)
        return;
    (*self).caps = caps;
}

/** Sets the maximum byte bound advertised for reads. */
;;SETTER
fn DbSqliteFile_setMaxBytes(DbSqliteFile *self, u64 maxBytes) {
    if (self == nullptr)
        return;
    (*self).maxBytes = maxBytes;
}

/** Sets the descriptor's read-only flag; no write callback is provided. */
;;SETTER
fn DbSqliteFile_setReadOnly(DbSqliteFile *self, bool readOnly) {
    if (self == nullptr)
        return;
    (*self).readOnly = readOnly;
}

/** GETTERS */

/** Returns the borrowed file path, or nullptr for a null receiver. */
;;GETTER
string DbSqliteFile_getPath(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).path;
}

/** Returns the advertised capability bits, or zero for a null receiver. */
;;GETTER
u32 DbSqliteFile_getCaps(const DbSqliteFile *self) {
    if (self == nullptr)
        return 0;
    return (*self).caps;
}

/** Returns the read byte limit, or zero for a null receiver. */
;;GETTER
u64 DbSqliteFile_getMaxBytes(const DbSqliteFile *self) {
    if (self == nullptr)
        return 0;
    return (*self).maxBytes;
}

/** Reports the descriptor's read-only setting. */
;;GETTER
bool DbSqliteFile_isReadOnly(const DbSqliteFile *self) {
    if (self == nullptr)
        return false;
    return (*self).readOnly;
}

/** Returns the borrowed owner handle, or nullptr when unbound. */
;;GETTER
any DbSqliteFile_getExecHandle(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).execHandle;
}

/** Returns the borrowed execution callback table, or nullptr when unbound. */
;;GETTER
const DbSqliteExec *DbSqliteFile_getExecTable(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).execTable;
}
