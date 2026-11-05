#ifndef ENGINE_PROVIDER_H
#define ENGINE_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// harness/engine_provider.h — the CLI coding-engine directory (L1
// descriptors), mirroring the ai/ provider pattern.
//
// One behavior class owns the catalog of CLI coding engines the engine
// may drive: canonical slug, verbatim display name, the CLI binary a
// vexspoke/R3 driver spawns, a coarse transport family, the credential
// scheme, and an optional note. Rows are static const slot records in
// engine_provider.c. Pure descriptors: no spawn, no popen, no includes
// beyond standard headers (Rule 17: exec lives behind the injected
// HarnessDriverTable in harness.h, implemented outside api-haven).

// Coarse transport bucket — how a future driver reaches the engine.
typedef enum EngineProviderFamily {
    ENGINE_PROVIDER_FAMILY_CLI,    // prompt via CLI stdin/args, driver spawns
    ENGINE_PROVIDER_FAMILY_LOCAL,  // local socket/daemon under the CLI
    ENGINE_PROVIDER_FAMILY_REMOTE, // remote API reached through CLI auth
} EngineProviderFamily;

// Credential scheme of the engine's CLI session.
typedef enum EngineProviderAuth {
    ENGINE_PROVIDER_AUTH_NONE,   // no credential (local, unattended)
    ENGINE_PROVIDER_AUTH_API_KEY,// provider key in env/config file
    ENGINE_PROVIDER_AUTH_OAUTH,  // subscription/token login via CLI
    ENGINE_PROVIDER_AUTH_SYSTEM, // OS keychain / ambient credential
} EngineProviderAuth;

// SLOT RECORD — one row of the engine directory (Rule 3 co-location).
// Immutable: rows are static const data in engine_provider.c; all
// behavior hangs off the EngineProvider table class (Rule 33 Tier-2
// waiver, see ;;INTENTION in engine_provider.c).
typedef struct EngineProviderSlot {
    const char *slug;        // canonical key, e.g. "claude-code"
    const char *displayName; // human label, e.g. "Claude Code"
    const char *cliName;     // CLI binary the driver spawns, e.g. "claude"
    EngineProviderFamily family; // transport bucket
    EngineProviderAuth auth;     // credential scheme
    const char *note;        // caveat; NULL when none
} EngineProviderSlot;

// The table class — a singleton handle; state lives in the static const
// rows in engine_provider.c.
typedef struct EngineProvider {
    uint32_t reserved; // signature/marker; no mutable state
} EngineProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
EngineProvider *EngineProvider_shared(void);

// --- Core functions ---
// Total rows. Null-safe: 0 on NULL self.
uint32_t EngineProvider_count(const EngineProvider *self);
// Row at index i. NULL when out of range.
const EngineProviderSlot *EngineProvider_at(const EngineProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (20 rows).
const EngineProviderSlot *EngineProvider_get(const EngineProvider *self, const char *slug);
// Copies the row's cliName into outBuf (NUL-terminated, dest-last per
// Rule 9). False on NULL self/slot/buf, zero cap, or truncation.
bool EngineProvider_resolveCli(const EngineProvider *self,
                               const EngineProviderSlot *slot,
                               char *outBuf, size_t outCap);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *EngineProvider_getSlug(const EngineProvider *self,
                                   const EngineProviderSlot *slot);
const char *EngineProvider_getDisplayName(const EngineProvider *self,
                                          const EngineProviderSlot *slot);
const char *EngineProvider_getCliName(const EngineProvider *self,
                                      const EngineProviderSlot *slot);
EngineProviderFamily EngineProvider_getFamily(const EngineProvider *self,
                                              const EngineProviderSlot *slot);
EngineProviderAuth EngineProvider_getAuth(const EngineProvider *self,
                                          const EngineProviderSlot *slot);
const char *EngineProvider_getNote(const EngineProvider *self,
                                   const EngineProviderSlot *slot);

#endif
