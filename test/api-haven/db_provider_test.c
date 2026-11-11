#include "annotation/annotation.h"
#include "database/db_provider.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: DbProviderTest (src/database/db_provider_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; descriptors only, no
 * drivers, no network)
 * ============================================================================
 * Executable proof of the database data-source directory: row count,
 * slug/engine lookups, wire families and ports for representative rows,
 * positional access, directory-wide invariants (unique slugs and display
 * names, slug grammar, non-empty names/engines), and every null-safety
 * guard.
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

// [a-z0-9]+(-[a-z0-9]+)* — canonical slug grammar (generator-enforced).
static int isSlugValid(const char *slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (const char *p = slug; *p; p++) {
        char c = *p;
        int isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '-') {
            if (p == slug || *(p + 1) == '\0' || *(p + 1) == '-')
                return 0;
        } else if (!isAlnum) {
            return 0;
        }
    }
    return 1;
}

/* Checks database-provider lookups, directory-wide row invariants, and nullable accessor defaults. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    DbProvider *db = DbProvider_shared();
    CHECK(db != nullptr);
    const u32 total = DbProvider_count(db);
    printf("DbProvider_count = %u\n", total);
    CHECK(total == 25);

    // --- slug lookups ----------------------------------------------------------
    const DbProviderSlot *pg = DbProvider_get(db, "postgresql");
    CHECK(pg != nullptr);
    CHECK(pg && strcmp(DbProvider_getDisplayName(db, pg), "PostgreSQL") == 0);
    CHECK(pg && strcmp(DbProvider_getEngine(db, pg), "postgres") == 0);
    CHECK(pg && DbProvider_getDefaultPort(db, pg) == 5432);
    CHECK(pg && DbProvider_getFamily(db, pg) == DB_PROVIDER_FAMILY_SQL);

    const DbProviderSlot *mysql = DbProvider_get(db, "mysql");
    CHECK(mysql && DbProvider_getDefaultPort(db, mysql) == 3306);
    CHECK(mysql && DbProvider_getFamily(db, mysql) == DB_PROVIDER_FAMILY_SQL);

    const DbProviderSlot *dynamo = DbProvider_get(db, "amazon-dynamodb");
    CHECK(dynamo && DbProvider_getFamily(db, dynamo) == DB_PROVIDER_FAMILY_NO_SQL);
    CHECK(dynamo && DbProvider_getDefaultPort(db, dynamo) == 0); // SDK-only

    const DbProviderSlot *h2 = DbProvider_get(db, "h2");
    CHECK(h2 && DbProvider_getFamily(db, h2) == DB_PROVIDER_FAMILY_EMBEDDED);

    const DbProviderSlot *bq = DbProvider_get(db, "google-bigquery");
    CHECK(bq && DbProvider_getFamily(db, bq) == DB_PROVIDER_FAMILY_SQL_COMPAT);

    CHECK(DbProvider_get(db, "amazon-aurora-mysql") != nullptr);
    CHECK(DbProvider_get(db, "ibm-db2-luw") != nullptr);
    CHECK(DbProvider_get(db, "definitely-not-a-db") == nullptr);

    // --- engine lookups --------------------------------------------------------
    const DbProviderSlot *sqlserver = DbProvider_findByEngine(db, "sqlserver");
    CHECK(sqlserver != nullptr);
    CHECK(sqlserver && strcmp(DbProvider_getSlug(db, sqlserver),
                              "azure-sql-database") == 0);
    CHECK(DbProvider_findByEngine(db, "postgres") != nullptr);
    CHECK(DbProvider_findByEngine(db, "definitely-not-an-engine") == nullptr);

    // --- positional + consistency ----------------------------------------------
    CHECK(DbProvider_at(db, 0) != nullptr);
    CHECK(DbProvider_at(db, total) == nullptr);
    const char *names[25];
    u32 nameCount = 0;
    for (u32 i = 0; i < total; i++) {
        const DbProviderSlot *row = DbProvider_at(db, i);
        CHECK(row != nullptr);
        CHECK(row && isSlugValid(DbProvider_getSlug(db, row)));
        // slug uniqueness: get() must resolve every row back to itself
        CHECK(row && DbProvider_get(db, DbProvider_getSlug(db, row)) == row);
        const char *rowName = row ? DbProvider_getDisplayName(db, row) : nullptr;
        const char *rowEngine = row ? DbProvider_getEngine(db, row) : nullptr;
        CHECK(rowName && (*rowName) != '\0');
        CHECK(rowEngine && (*rowEngine) != '\0');
        if (row && rowName) {
            int dup = 0;
            for (u32 j = 0; j < nameCount; j++) {
                if (strcmp(names[j], rowName) == 0)
                    dup = 1;
            }
            CHECK(!dup); // menu-safe: no two rows share a display name
            names[nameCount++] = rowName;
        }
    }

    // --- null-safety (Rule 24) ------------------------------------------------
    CHECK(DbProvider_count(nullptr) == 0);
    CHECK(DbProvider_get(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_get(db, nullptr) == nullptr);
    CHECK(DbProvider_findByEngine(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_at(nullptr, 0) == nullptr);
    CHECK(DbProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getEngine(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getDefaultPort(nullptr, nullptr) == 0);
    CHECK(DbProvider_getFamily(nullptr, nullptr) == DB_PROVIDER_FAMILY_SQL);

    if (sFailures == 0) {
        printf("db_provider_test: ALL CHECKS PASSED (%u data sources)\n", total);
        return 0;
    }
    printf("db_provider_test: %d FAILURES\n", sFailures);
    return 1;
}
