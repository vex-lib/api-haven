#ifndef DB_SQLITE_FILE_H
#define DB_SQLITE_FILE_H

#include "vex.h"

/**
 * database/db_sqlite_file.h, the DbSqliteFile class: catalog-only
 * read-only SQLite file descriptor (L1 metadata, R2 api-haven).
 *
 * A file row the DbProvider directory points at but never opens: path,
 * capability caps, byte bounds, and an fn-table for execution. Zero
 * `sqlite3.h` include/link, execution delegates through the opaque
 * handle to the database owner (db-haven/darkbase-engine `Database` interface)
 * or vexspoke File/VFS. Read-only by construction: no write path
 * exists on this class (Rule 17: drivers live outside api-haven).
 */

/** Capability bits (catalog contract, what the owner may serve). */
#define DB_SQLITE_CAP_READ 1u

typedef struct DbSqliteFile DbSqliteFile;

/**
 * Opaque execution seam (Rule 33 canonical move): the owner binds its
 * read path here. Handles are borrowed views (detach-only, never
 * freed by this class, unbind first per Rule 26).
 */
typedef struct DbSqliteExec {
    bool (*exec)(any handle, string sql, utf8 *out, bytes outCap);
} DbSqliteExec;

/**
 * string path              - borrowed file path (nullptr = undescribed)
 * u32 caps                      - capability bits (DB_SQLITE_CAP_* )
 * u64 maxBytes                  - read bound (0 = unknown)
 * bool readOnly                 - always true on _0/_1; no writer exists
 * any execHandle                - borrowed owner handle (nullptr = unbound)
 * const DbSqliteExec *execTable - borrowed fn-table (nullptr = unbound)
 */
struct DbSqliteFile {
    string path;
    u32 caps;
    u64 maxBytes;
    bool readOnly;
    any execHandle;
    const DbSqliteExec *execTable;
};

/** --- Constructors (value structs, no allocation) --- */
DbSqliteFile DbSqliteFile_0(); /** undescribed, read-only */
DbSqliteFile DbSqliteFile_1(string path); /** described path, read-only */
#define DbSqliteFile(...) CONSTRUCTOR_DISPATCH(DbSqliteFile, __VA_ARGS__)

/**
 * --- Core functions ---
 * Bind the owner's read path (borrowed handle + table, never owned).
 * False on nullptr self/handle/table or when already bound (unbind first).
 */
bool DbSqliteFile_bind(DbSqliteFile *self, any handle, const DbSqliteExec *table);
fn DbSqliteFile_unbind(DbSqliteFile *self);
/**
 * Render "sqlite file: path=... readOnly=1 caps=.. maxBytes=.." into
 * caller-owned out (NUL always). False on nullptr args, zero cap, undescribed
 * path, or overflow. Dest-last per Rule 9.
 */
bool DbSqliteFile_describe(const DbSqliteFile *self, utf8 *out, bytes outCap);
/**
 * Delegate one read through the bound table. False on nullptr args, zero
 * cap, or unbound (drop-degrade, the owner is absent, not an error to
 * log on any hot path). Dest-last per Rule 9.
 */
bool DbSqliteFile_exec(const DbSqliteFile *self, string sql,
                       utf8 *out, bytes outCap);

/** --- Setters --- */
fn DbSqliteFile_setPath(DbSqliteFile *self, string path);
fn DbSqliteFile_setCaps(DbSqliteFile *self, u32 caps);
fn DbSqliteFile_setMaxBytes(DbSqliteFile *self, u64 maxBytes);
fn DbSqliteFile_setReadOnly(DbSqliteFile *self, bool readOnly);

/** --- Getters (Rule 24, null-safe) --- */
string DbSqliteFile_getPath(const DbSqliteFile *self);
u32 DbSqliteFile_getCaps(const DbSqliteFile *self);
u64 DbSqliteFile_getMaxBytes(const DbSqliteFile *self);
bool DbSqliteFile_isReadOnly(const DbSqliteFile *self);
any DbSqliteFile_getExecHandle(const DbSqliteFile *self);
const DbSqliteExec *DbSqliteFile_getExecTable(const DbSqliteFile *self);

#endif
