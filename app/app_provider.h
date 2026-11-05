#ifndef APP_PROVIDER_H
#define APP_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// app/app_provider.h — the app/automation directory (L1 descriptors),
// mirroring the harness/ engine pattern.
//
// One behavior class owns the catalog of automation targets the engine
// may drive: canonical slug, verbatim display name, transport family,
// credential scheme, bundle id / CLI / URL the driver resolves, and an
// optional note. Rows are static const slot records in app_provider.c.
// Pure descriptors: no osascript, no IPC, no includes beyond standard
// headers (Rule 17: exec lives behind the injected AppDriverTable in
// app_broker.h, implemented outside api-haven).

// Coarse transport bucket — how a future driver reaches the target.
typedef enum AppProviderFamily {
    APP_PROVIDER_FAMILY_OSA_SCRIPT,    // AppleScript via osascript driver
    APP_PROVIDER_FAMILY_SHORTCUTS_CLI, // Apple Shortcuts via shortcuts CLI
    APP_PROVIDER_FAMILY_MUSIC_LOCAL,   // local Music.app automation
    APP_PROVIDER_FAMILY_REST_WEBHOOK,  // HTTPS webhook (bot/page tokens)
    APP_PROVIDER_FAMILY_LOCAL_SOCKET,  // loopback helper (e.g. port 4370)
    APP_PROVIDER_FAMILY_NATIVE_CLI,    // native binary probe (python3 --version etc.)
} AppProviderFamily;

// Credential scheme of the automation target.
typedef enum AppProviderAuth {
    APP_PROVIDER_AUTH_NONE,   // no credential (local helper, unattended)
    APP_PROVIDER_AUTH_SYSTEM, // OS identity / TCC grant, ambient
    APP_PROVIDER_AUTH_TOKEN,  // bot/page token via ApiAuth, never in arena
} AppProviderAuth;

// SLOT RECORD — one row of the app directory (Rule 3 co-location).
// Immutable: rows are static const data in app_provider.c; all behavior
// hangs off the AppProvider table class (Rule 33 Tier-2 waiver, see
// ;;INTENTION in app_provider.c).
typedef struct AppProviderSlot {
    const char *slug;            // canonical key, e.g. "apple-notes"
    const char *displayName;     // human label, e.g. "Notes"
    AppProviderFamily family;    // transport bucket
    AppProviderAuth auth;        // credential scheme
    const char *bundleIdOrScheme;// bundle id, CLI, or URL the driver resolves
    const char *note;            // caveat; NULL when none
} AppProviderSlot;

// The table class — a singleton handle; state lives in the static const
// rows in app_provider.c.
typedef struct AppProvider {
    uint32_t reserved; // signature/marker; no mutable state
} AppProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
AppProvider *AppProvider_shared(void);

// --- Core functions ---
// Total rows. Null-safe: 0 on NULL self.
uint32_t AppProvider_count(const AppProvider *self);
// Row at index i. NULL when out of range.
const AppProviderSlot *AppProvider_at(const AppProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (29 rows).
const AppProviderSlot *AppProvider_get(const AppProvider *self, const char *slug);
// Copies the row's bundleIdOrScheme into outBuf (NUL-terminated,
// dest-last per Rule 9). False on NULL self/slot/buf, zero cap, or
// truncation.
bool AppProvider_resolveTarget(const AppProvider *self,
                               const AppProviderSlot *slot,
                               char *outBuf, size_t outCap);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *AppProvider_getSlug(const AppProvider *self,
                                const AppProviderSlot *slot);
const char *AppProvider_getDisplayName(const AppProvider *self,
                                       const AppProviderSlot *slot);
AppProviderFamily AppProvider_getFamily(const AppProvider *self,
                                        const AppProviderSlot *slot);
AppProviderAuth AppProvider_getAuth(const AppProvider *self,
                                    const AppProviderSlot *slot);
const char *AppProvider_getBundleIdOrScheme(const AppProvider *self,
                                            const AppProviderSlot *slot);
const char *AppProvider_getNote(const AppProvider *self,
                                const AppProviderSlot *slot);

#endif
