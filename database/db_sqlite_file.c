#include "annotation/annotation.h"
#include "database/db_sqlite_file.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/** A catalog-only, read-only SQLite row: a borrowed path, capability caps, byte
 * bounds and the owner's fn-table, with no sqlite3.h include or link. It exists
 * so the directory can describe a SQLite source without owning a database
 * engine; execution delegates through the bound opaque handle, and an unbound
 * row drop-degrades to false. Read-only by construction — no write path exists
 * on this class. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DbSqliteFile (database/db_sqlite_file)
 * execution seam; swappable with zero code changes)
 * ============================================================================
 * Catalog-only read-only SQLite file row for the DbProvider directory:
 * a borrowed path, capability caps, byte bounds, and the owner's
 * fn-table — with zero sqlite3.h include/link. Execution delegates
 * through the bound opaque handle to the database owner (db-haven /
 * darkbase-engine Database interface) or vexspoke File/VFS; unbound exec
 * drop-degrades to false. Read-only by construction: no write path
 * exists on this class. Value struct, zero allocation, immutable reads
 * without locks.
 *
 * STRUCT FIELDS (Mirroring database/db_sqlite_file.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   const char *path;              // borrowed file path (NULL = undescribed)
 *   uint32_t caps;                 // capability bits (DB_SQLITE_CAP_*)
 *   uint64_t maxBytes;             // read bound (0 = unknown)
 *   bool readOnly;                 // always true on _0/_1; no writer exists
 *   void *execHandle;              // borrowed owner handle (NULL = unbound)
 *   const DbSqliteExec *execTable; // borrowed fn-table (NULL = unbound)
 *
 * SLOT RECORD: none — this file owns one behavior class only (the
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
;;INTENTION("catalog-only SQLite: api-haven describes the file and delegates reads — the driver, the engine, and sqlite3.h all live with the database owner (Rule 17)")
;;INTENTION("borrowed execution seam: handle + table are detach-only views — unbind before the owner frees, per Rule 26 (Rule 33 canonical move)")

// CONSTRUCTORS

/** Creates an undescribed read-only file descriptor with no execution binding. */
DbSqliteFile DbSqliteFile_0(void) {
    DbSqliteFile file;
    memset(&file, 0, sizeof(file));
    file.readOnly = true;
    return file;
}

/** Creates a read-only descriptor for the borrowed path with read capability. */
DbSqliteFile DbSqliteFile_1(const char *path) {
    DbSqliteFile file = DbSqliteFile();
    file.path = path;
    file.caps = DB_SQLITE_CAP_READ;
    return file;
}

// CORE FUNCTIONS

/** Binds a borrowed owner handle and its execution callback table. */
bool DbSqliteFile_bind(DbSqliteFile *self, void *handle, const DbSqliteExec *table) {
    if (self == nullptr)
        return false;
    if (handle == nullptr || table == nullptr)
        return false;
    if ((*self).execHandle != nullptr)
        return false;
    if ((*table).exec == nullptr)
        return false;
    (*self).execHandle = handle;
    (*self).execTable = table;
    return true;
}

/** Clears the borrowed execution binding without destroying the owner. */
void DbSqliteFile_unbind(DbSqliteFile *self) {
    if (self == nullptr)
        return;
    (*self).execHandle = nullptr;
    (*self).execTable = nullptr;
}

/** Formats this descriptor into out when the complete text fits. */
bool DbSqliteFile_describe(const DbSqliteFile *self, char *out, size_t outCap) {
    if (self == nullptr)
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    const char *path = (*self).path;
    if (path == nullptr || (*path) == '\0')
        return false;
    const int n = snprintf(out, outCap, "sqlite file: path=%s readOnly=%d caps=%u maxBytes=%llu",
                           path, (*self).readOnly ? 1 : 0,
                           (*self).caps, (unsigned long long)(*self).maxBytes);
    if (n <= 0 || (size_t)n >= outCap)
        return false;
    return true;
}

/** Delegates a read request to the bound database owner's execution callback. */
bool DbSqliteFile_exec(const DbSqliteFile *self, const char *sql,
                       char *out, size_t outCap) {
    if (self == nullptr)
        return false;
    if (sql == nullptr || (*sql) == '\0')
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    if ((*self).execHandle == nullptr || (*self).execTable == nullptr)
        return false;
    const DbSqliteExec *table = (*self).execTable;
    if ((*table).exec == nullptr)
        return false;
    return (*table).exec((*self).execHandle, sql, out, outCap);
}

// SETTERS

/** Sets the borrowed path described by this catalog row. */
void DbSqliteFile_setPath(DbSqliteFile *self, const char *path) {
    if (self == nullptr)
        return;
    (*self).path = path;
}

/** Replaces the descriptor's advertised capability flags. */
void DbSqliteFile_setCaps(DbSqliteFile *self, uint32_t caps) {
    if (self == nullptr)
        return;
    (*self).caps = caps;
}

/** Sets the maximum byte bound advertised for reads. */
void DbSqliteFile_setMaxBytes(DbSqliteFile *self, uint64_t maxBytes) {
    if (self == nullptr)
        return;
    (*self).maxBytes = maxBytes;
}

/** Sets the descriptor's read-only flag; no write callback is provided. */
void DbSqliteFile_setReadOnly(DbSqliteFile *self, bool readOnly) {
    if (self == nullptr)
        return;
    (*self).readOnly = readOnly;
}

// GETTERS

/** Returns the borrowed file path, or nullptr for a null receiver. */
const char *DbSqliteFile_getPath(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).path;
}

/** Returns the advertised capability bits, or zero for a null receiver. */
uint32_t DbSqliteFile_getCaps(const DbSqliteFile *self) {
    if (self == nullptr)
        return 0;
    return (*self).caps;
}

/** Returns the read byte limit, or zero for a null receiver. */
uint64_t DbSqliteFile_getMaxBytes(const DbSqliteFile *self) {
    if (self == nullptr)
        return 0;
    return (*self).maxBytes;
}

/** Reports the descriptor's read-only setting. */
bool DbSqliteFile_isReadOnly(const DbSqliteFile *self) {
    if (self == nullptr)
        return false;
    return (*self).readOnly;
}

/** Returns the borrowed owner handle, or nullptr when unbound. */
void *DbSqliteFile_getExecHandle(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).execHandle;
}

/** Returns the borrowed execution callback table, or nullptr when unbound. */
const DbSqliteExec *DbSqliteFile_getExecTable(const DbSqliteFile *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).execTable;
}
