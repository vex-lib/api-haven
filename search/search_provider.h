#ifndef SEARCH_PROVIDER_H
#define SEARCH_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// search/search_provider.h — the blessed web-search directory (L1
// descriptors), mirroring the harness/app provider pattern.
//
// One behavior class owns the catalog of search backends the engine
// may query: canonical slug, display name, wire family, credential
// scheme, endpoint base, and a note. Rows are static const slot
// records in search_provider.c. Pure descriptors: no sockets here —
// queries run through api/rest.h (Rest_get, 5s bound) in the MCP
// renderer, which answers an honest error when the transport cannot
// do the scheme (https until a TLS backend lands) or the key env is
// absent. Scraping HTML to fake a search API is a defect per Rule 34;
// every row below is a real search API (or self-hosted SearXNG).

// Wire family — which response shape the renderer parses.
typedef enum SearchProviderFamily {
    SEARCH_FAMILY_SEARXNG,    // GET {base}/search?q=..&format=json
    SEARCH_FAMILY_GOOGLE_CSE, // Google Custom Search JSON API (key+cx)
    SEARCH_FAMILY_MEDIAWIKI,  // action=query&list=search&format=json
} SearchProviderFamily;

// Credential scheme of the backend.
typedef enum SearchProviderAuth {
    SEARCH_AUTH_NONE,      // no credential (self-hosted, public wiki)
    SEARCH_AUTH_KEY_CX,    // GOOGLE_CSE_KEY + GOOGLE_CSE_CX env pair
} SearchProviderAuth;

// SLOT RECORD — one row of the search directory (Rule 3 co-location).
// Immutable: rows are static const data in search_provider.c; all
// behavior hangs off the SearchProvider table class (Rule 33 Tier-2
// waiver, see ;;INTENTION in search_provider.c).
typedef struct SearchProviderSlot {
    const char *slug;         // canonical key, e.g. "searxng"
    const char *displayName;  // human label, e.g. "SearXNG (self-hosted)"
    SearchProviderFamily family; // wire family enum
    SearchProviderAuth auth;      // credential scheme enum
    const char *endpoint;     // base URL the renderer builds on
    const char *note;         // caveat; NULL when none
    uint32_t quotaPerDay;     // free-tier requests/day (0 = unknown)
    uint32_t quotaRemaining;  // free-tier remainder (0 = unknown/exhausted)
    int64_t resetUnix;        // quota window reset epoch secs (0 = unknown)
    const char *authKind;     // credential kind label; NULL = see auth enum
    const char *licenseFamily; // license family label; NULL = UNKNOWN
} SearchProviderSlot;

// The table class — a singleton handle; state lives in the static const
// rows in search_provider.c.
typedef struct SearchProvider {
    uint32_t reserved; // signature/marker; no mutable state
} SearchProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
SearchProvider *SearchProvider_shared(void);

// --- Core functions ---
// Total rows. Null-safe: 0 on NULL self.
uint32_t SearchProvider_count(const SearchProvider *self);
// Row at index i. NULL when out of range.
const SearchProviderSlot *SearchProvider_at(const SearchProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (3 rows).
const SearchProviderSlot *SearchProvider_get(const SearchProvider *self, const char *slug);
// Copies the row's endpoint into outBuf (NUL-terminated, dest-last per
// Rule 9). False on NULL self/slot/buf, zero cap, or truncation.
bool SearchProvider_resolveEndpoint(const SearchProvider *self,
                                    const SearchProviderSlot *slot,
                                    char *outBuf, size_t outCap);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *SearchProvider_getSlug(const SearchProvider *self,
                                   const SearchProviderSlot *slot);
const char *SearchProvider_getDisplayName(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
SearchProviderFamily SearchProvider_getFamily(const SearchProvider *self,
                                              const SearchProviderSlot *slot);
SearchProviderAuth SearchProvider_getAuth(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
const char *SearchProvider_getEndpoint(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
const char *SearchProvider_getNote(const SearchProvider *self,
                                   const SearchProviderSlot *slot);
uint32_t SearchProvider_getQuotaPerDay(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
uint32_t SearchProvider_getQuotaRemaining(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
int64_t SearchProvider_getResetUnix(const SearchProvider *self,
                                    const SearchProviderSlot *slot);
const char *SearchProvider_getAuthKind(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
const char *SearchProvider_getLicenseFamily(const SearchProvider *self,
                                            const SearchProviderSlot *slot);

#endif
