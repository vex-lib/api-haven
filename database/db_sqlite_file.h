#ifndef DB_SQLITE_FILE_H
#define DB_SQLITE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"

// database/db_sqlite_file.h — the DbSqliteFile class: catalog-only
// read-only SQLite file descriptor (L1 metadata, R2 api-haven).
//
// A file row the DbProvider directory points at but never opens: path,
// capability caps, byte bounds, and an fn-table for execution. Zero
// `sqlite3.h` include/link — execution delegates through the opaque
// handle to the database owner (db-haven/darkbase-engine `Database` interface)
// or vexspoke File/VFS. Read-only by construction: no write path
// exists on this class (Rule 17: drivers live outside api-haven).

// Capability bits (catalog contract — what the owner may serve).
#define DB_SQLITE_CAP_READ 1u

typedef struct DbSqliteFile DbSqliteFile;

// Opaque execution seam (Rule 33 canonical move): the owner binds its
// read path here. Handles are borrowed views (detach-only, never
// freed by this class — unbind first per Rule 26).
typedef struct DbSqliteExec {
    bool (*exec)(void *handle, const char *sql, char *out, size_t outCap);
} DbSqliteExec;

struct DbSqliteFile {
    const char *path;              // borrowed file path (NULL = undescribed)
    uint32_t caps;                 // capability bits (DB_SQLITE_CAP_* )
    uint64_t maxBytes;             // read bound (0 = unknown)
    bool readOnly;                 // always true on _0/_1; no writer exists
    void *execHandle;              // borrowed owner handle (NULL = unbound)
    const DbSqliteExec *execTable; // borrowed fn-table (NULL = unbound)
};

// --- Constructors (value structs, no allocation) ---
DbSqliteFile DbSqliteFile_0(void);           // undescribed, read-only
DbSqliteFile DbSqliteFile_1(const char *path); // described path, read-only
#define DbSqliteFile(...) CONSTRUCTOR_DISPATCH(DbSqliteFile, __VA_ARGS__)

// --- Core functions ---
// Bind the owner's read path (borrowed handle + table, never owned).
// False on NULL self/handle/table or when already bound (unbind first).
bool DbSqliteFile_bind(DbSqliteFile *self, void *handle, const DbSqliteExec *table);
void DbSqliteFile_unbind(DbSqliteFile *self);
// Render "sqlite file: path=... readOnly=1 caps=.. maxBytes=.." into
// caller-owned out (NUL always). False on NULL args, zero cap, undescribed
// path, or overflow. Dest-last per Rule 9.
bool DbSqliteFile_describe(const DbSqliteFile *self, char *out, size_t outCap);
// Delegate one read through the bound table. False on NULL args, zero
// cap, or unbound (drop-degrade — the owner is absent, not an error to
// log on any hot path). Dest-last per Rule 9.
bool DbSqliteFile_exec(const DbSqliteFile *self, const char *sql,
                       char *out, size_t outCap);

// --- Setters ---
void DbSqliteFile_setPath(DbSqliteFile *self, const char *path);
void DbSqliteFile_setCaps(DbSqliteFile *self, uint32_t caps);
void DbSqliteFile_setMaxBytes(DbSqliteFile *self, uint64_t maxBytes);
void DbSqliteFile_setReadOnly(DbSqliteFile *self, bool readOnly);

// --- Getters (Rule 24, null-safe) ---
const char *DbSqliteFile_getPath(const DbSqliteFile *self);
uint32_t DbSqliteFile_getCaps(const DbSqliteFile *self);
uint64_t DbSqliteFile_getMaxBytes(const DbSqliteFile *self);
bool DbSqliteFile_isReadOnly(const DbSqliteFile *self);
void *DbSqliteFile_getExecHandle(const DbSqliteFile *self);
const DbSqliteExec *DbSqliteFile_getExecTable(const DbSqliteFile *self);

#endif
