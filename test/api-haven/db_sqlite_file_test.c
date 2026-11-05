#include "annotation/annotation.h"
#include "database/db_sqlite_file.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: DbSqliteFileTest (src/database/db_sqlite_file_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; descriptors + opaque
 * delegation only, zero sqlite3.h, no drivers, no filesystem)
 * ============================================================================
 * Executable proof of the DbSqliteFile class: read-only defaults,
 * path/caps/bounds setters, describe rendering, bind/unbind of a fake
 * owner table, delegated exec, unbound drop-degrade, and the full
 * cold-seam matrix (nullptr, empty, zero cap, overflow) per Rule 35.4.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static int sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

/* Implements a bounded fake execution callback and counts valid delegated calls. */
static bool fakeExec(void *handle, const char *sql, char *out, size_t outCap) {
    if (!handle || !sql || !out || outCap == 0)
        return false;
    int *calls = (int*) handle;
    (*calls)++;
    const int n = snprintf(out, outCap, "rows for: %s", sql);
    if (n <= 0 || (size_t)n >= outCap)
        return false;
    return true;
}

static const DbSqliteExec sFakeTable = { fakeExec };

/* Exercises descriptor defaults, callback binding/delegation, setters, and rejection guards without SQLite. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- read-only defaults ----------------------------------------------------------
    DbSqliteFile bare = DbSqliteFile();
    CHECK(DbSqliteFile_isReadOnly(&bare));
    CHECK(DbSqliteFile_getPath(&bare) == nullptr);
    CHECK(DbSqliteFile_getCaps(&bare) == 0);
    CHECK(DbSqliteFile_getMaxBytes(&bare) == 0);
    CHECK(DbSqliteFile_getExecHandle(&bare) == nullptr);
    CHECK(DbSqliteFile_getExecTable(&bare) == nullptr);

    DbSqliteFile file = DbSqliteFile("/tmp/catalog.db");
    CHECK(DbSqliteFile_isReadOnly(&file));
    CHECK(strcmp(DbSqliteFile_getPath(&file), "/tmp/catalog.db") == 0);
    CHECK((DbSqliteFile_getCaps(&file) & DB_SQLITE_CAP_READ) != 0);

    // --- describe rendering -----------------------------------------------------------------
    char desc[256];
    DbSqliteFile_setMaxBytes(&file, 1048576);
    CHECK(DbSqliteFile_describe(&file, desc, sizeof(desc)));
    CHECK(strstr(desc, "sqlite file: path=/tmp/catalog.db") != nullptr);
    CHECK(strstr(desc, "readOnly=1") != nullptr);
    CHECK(strstr(desc, "maxBytes=1048576") != nullptr);
    CHECK(!DbSqliteFile_describe(&file, desc, 8)); // overflow
    CHECK(!DbSqliteFile_describe(&bare, desc, sizeof(desc))); // undescribed
    CHECK(!DbSqliteFile_describe(nullptr, desc, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, nullptr, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, desc, 0));

    // --- unbound exec drop-degrades -----------------------------------------------------------------
    char out[256];
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));

    // --- bind + delegated exec ----------------------------------------------------------------------------
    int calls = 0;
    CHECK(DbSqliteFile_getExecHandle(&file) == nullptr);
    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(DbSqliteFile_getExecHandle(&file) == &calls);
    CHECK(DbSqliteFile_getExecTable(&file) == &sFakeTable);
    CHECK(DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(calls == 1);
    CHECK(strcmp(out, "rows for: SELECT 1") == 0);
    CHECK(!DbSqliteFile_bind(&file, &calls, &sFakeTable)); // rebind refused
    int other = 0;
    CHECK(!DbSqliteFile_bind(&file, &other, &sFakeTable)); // unbind first
    DbSqliteFile_unbind(&file);
    CHECK(DbSqliteFile_getExecHandle(&file) == nullptr);
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_bind(&file, nullptr, &sFakeTable));
    CHECK(!DbSqliteFile_bind(&file, &calls, nullptr));
    CHECK(!DbSqliteFile_bind(nullptr, &calls, &sFakeTable));
    static const DbSqliteExec nullTable = { nullptr };
    CHECK(!DbSqliteFile_bind(&file, &calls, &nullTable)); // null fn refused
    DbSqliteFile_unbind(nullptr); // null-safe no-op

    // --- exec guard matrix --------------------------------------------------------------------------------------
    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(!DbSqliteFile_exec(nullptr, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, nullptr, out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", nullptr, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, 0));

    // --- setters round-trip --------------------------------------------------------------------------------------
    DbSqliteFile st = DbSqliteFile();
    DbSqliteFile_setPath(&st, "/data/app.db");
    DbSqliteFile_setCaps(&st, DB_SQLITE_CAP_READ);
    DbSqliteFile_setMaxBytes(&st, 4096);
    DbSqliteFile_setReadOnly(&st, true);
    CHECK(strcmp(DbSqliteFile_getPath(&st), "/data/app.db") == 0);
    CHECK(DbSqliteFile_getCaps(&st) == DB_SQLITE_CAP_READ);
    CHECK(DbSqliteFile_getMaxBytes(&st) == 4096);
    CHECK(DbSqliteFile_isReadOnly(&st));
    DbSqliteFile_setPath(nullptr, "x");
    DbSqliteFile_setCaps(nullptr, 1);
    DbSqliteFile_setMaxBytes(nullptr, 1);
    DbSqliteFile_setReadOnly(nullptr, false);
    CHECK(DbSqliteFile_getPath(nullptr) == nullptr);
    CHECK(DbSqliteFile_getCaps(nullptr) == 0);
    CHECK(DbSqliteFile_getMaxBytes(nullptr) == 0);
    CHECK(!DbSqliteFile_isReadOnly(nullptr));
    CHECK(DbSqliteFile_getExecHandle(nullptr) == nullptr);
    CHECK(DbSqliteFile_getExecTable(nullptr) == nullptr);

    if (sFailures == 0) {
        printf("db_sqlite_file_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("db_sqlite_file_test: %d FAILURES\n", sFailures);
    return 1;
}
