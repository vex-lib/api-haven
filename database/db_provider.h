#ifndef DB_PROVIDER_H
#define DB_PROVIDER_H

#include <stdint.h>

// database/db_provider.h — the database data-source directory (L1
// descriptors), mirroring the ai/ provider pattern.
//
// One behavior class owns the catalog of databases with complete driver
// support: canonical slug, verbatim display name, the engine/driver key a
// future connector resolves, the canonical wire port, and a coarse wire
// family. Rows are static const slot records generated under
// src/database/data/ (see db_provider.c). Pure descriptors: no drivers,
// no includes beyond vexspoke.

// Coarse wire bucket — what a future connector speaks, not an engine claim.
typedef enum DbProviderFamily {
    DB_PROVIDER_FAMILY_SQL,       // relational SQL wire
    DB_PROVIDER_FAMILY_SQL_COMPAT,// SQL-ish analytics / warehouse
    DB_PROVIDER_FAMILY_NO_SQL,    // document / wide-column / key-value
    DB_PROVIDER_FAMILY_EMBEDDED,  // embedded file / local engines
} DbProviderFamily;

// SLOT RECORD — one row of the data-source directory (Rule 3
// co-location). Immutable: rows are static const data in db_provider.c;
// all behavior hangs off the DbProvider table class (Rule 33 Tier-2
// waiver, see ;;INTENTION in db_provider.c).
typedef struct DbProviderSlot {
    const char *slug;        // canonical key, e.g. "postgresql"
    const char *displayName; // verbatim source, e.g. "PostgreSQL"
    const char *engine;      // driver/engine key, e.g. "postgres"
    uint16_t defaultPort;    // canonical wire port; 0 = embedded/SDK-only
    DbProviderFamily family; // coarse wire bucket
    const char *note;        // caveat; NULL when none
} DbProviderSlot;

// The table class — a singleton handle; state lives in static const
// rows generated under src/database/data/.
typedef struct DbProvider {
    uint32_t reserved; // signature/marker; no mutable state
} DbProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
DbProvider *DbProvider_shared(void);

// --- Core functions ---
// Total rows. Null-safe: 0 on NULL self.
uint32_t DbProvider_count(const DbProvider *self);
// Row at index i (alphabetical, source order). NULL when out of range.
const DbProviderSlot *DbProvider_at(const DbProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (~25 rows).
const DbProviderSlot *DbProvider_get(const DbProvider *self, const char *slug);
// Row by driver/engine key (e.g. "postgres", "sqlserver" — many rows may
// share one engine key; returns the first).
const DbProviderSlot *DbProvider_findByEngine(const DbProvider *self,
                                              const char *engine);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *DbProvider_getSlug(const DbProvider *self, const DbProviderSlot *slot);
const char *DbProvider_getDisplayName(const DbProvider *self, const DbProviderSlot *slot);
const char *DbProvider_getEngine(const DbProvider *self, const DbProviderSlot *slot);
uint16_t DbProvider_getDefaultPort(const DbProvider *self, const DbProviderSlot *slot);
DbProviderFamily DbProvider_getFamily(const DbProvider *self, const DbProviderSlot *slot);
const char *DbProvider_getNote(const DbProvider *self, const DbProviderSlot *slot);

#endif