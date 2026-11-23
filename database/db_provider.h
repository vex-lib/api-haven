#ifndef DB_PROVIDER_H
#define DB_PROVIDER_H

#include "vex.h"

/**
 * database/db_provider.h, the database data-source directory (L1
 * descriptors), mirroring the ai/ provider pattern.
 *
 * One behavior class owns the catalog of databases with complete driver
 * support: canonical slug, verbatim display name, the engine/driver key a
 * future connector resolves, the canonical wire port, and a coarse wire
 * family. Rows are static const slot records generated under
 * src/database/data/ (see db_provider.c). Pure descriptors: no drivers,
 * no includes beyond vexspoke.
 */

/**
 * Coarse wire bucket, what a future connector speaks, not an engine claim.
 *
 * DB_PROVIDER_FAMILY_SQL        - relational SQL wire
 * DB_PROVIDER_FAMILY_SQL_COMPAT - SQL-ish analytics / warehouse
 * DB_PROVIDER_FAMILY_NO_SQL     - document / wide-column / key-value
 * DB_PROVIDER_FAMILY_EMBEDDED   - embedded file / local engines
 */
typedef enum DbProviderFamily {
    DB_PROVIDER_FAMILY_SQL,
    DB_PROVIDER_FAMILY_SQL_COMPAT,
    DB_PROVIDER_FAMILY_NO_SQL,
    DB_PROVIDER_FAMILY_EMBEDDED,
} DbProviderFamily;

/**
 * SLOT RECORD, one row of the data-source directory (Rule 3
 * co-location). Immutable: rows are static const data in db_provider.c;
 * all behavior hangs off the DbProvider table class (Rule 33 Tier-2
 * waiver, see ;;INTENTION in db_provider.c).
 *
 * string slug        - canonical key, e.g. "postgresql"
 * string displayName - verbatim source, e.g. "PostgreSQL"
 * string engine      - driver/engine key, e.g. "postgres"
 * u16 defaultPort         - canonical wire port; 0 = embedded/SDK-only
 * DbProviderFamily family - coarse wire bucket
 * string note        - caveat; nullptr when none
 */
typedef struct DbProviderSlot {
    string slug;
    string displayName;
    string engine;
    u16 defaultPort;
    DbProviderFamily family;
    string note;
} DbProviderSlot;

/**
 * The table class, a singleton handle; state lives in static const
 * rows generated under src/database/data/.
 *
 * u32 reserved - signature/marker; no mutable state
 */
typedef struct DbProvider {
    u32 reserved;
} DbProvider;

/**
 * --- Constructor ---
 * Returns the shared directory handle (static, zero-init, never nullptr).
 */
DbProvider *DbProvider_shared();

/**
 * --- Core functions ---
 * Total rows. Null-safe: 0 on nullptr self.
 */
u32 DbProvider_count(const DbProvider *self);
/** Row at index i (alphabetical, source order). nullptr when out of range. */
const DbProviderSlot *DbProvider_at(const DbProvider *self, u32 i);
/** Row by canonical slug. nullptr when absent. Linear scan (~25 rows). */
const DbProviderSlot *DbProvider_get(const DbProvider *self, string slug);
/**
 * Row by driver/engine key (e.g. "postgres", "sqlserver", many rows may
 * share one engine key; returns the first).
 */
const DbProviderSlot *DbProvider_findByEngine(const DbProvider *self,
                                              string engine);

/** --- Getters (Rule 24; null-safe; setters omitted, immutable rows) --- */
string DbProvider_getSlug(const DbProvider *self, const DbProviderSlot *slot);
string DbProvider_getDisplayName(const DbProvider *self, const DbProviderSlot *slot);
string DbProvider_getEngine(const DbProvider *self, const DbProviderSlot *slot);
u16 DbProvider_getDefaultPort(const DbProvider *self, const DbProviderSlot *slot);
DbProviderFamily DbProvider_getFamily(const DbProvider *self, const DbProviderSlot *slot);
string DbProvider_getNote(const DbProvider *self, const DbProviderSlot *slot);

#endif