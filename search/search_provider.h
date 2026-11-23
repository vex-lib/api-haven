#ifndef SEARCH_PROVIDER_H
#define SEARCH_PROVIDER_H

#include "vex.h"

/**
 * search/search_provider.h, the blessed web-search directory (L1
 * descriptors), mirroring the harness/app provider pattern.
 *
 * One behavior class owns the catalog of search backends the engine
 * may query: canonical slug, display name, wire family, credential
 * scheme, endpoint base, and a note. Rows are static const slot
 * records in search_provider.c. Pure descriptors: no sockets here,
 * queries run through api/rest.h (Rest_get, 5s bound) in the MCP
 * renderer, which answers an honest error when the transport cannot
 * do the scheme (https until a TLS backend lands) or the key env is
 * absent. Scraping HTML to fake a search API is a defect per Rule 34;
 * every row below is a real search API (or self-hosted SearXNG).
 */

/**
 * Wire family, which response shape the renderer parses.
 *
 * SEARCH_FAMILY_SEARXNG    - GET {base}/search?q=..&format=json
 * SEARCH_FAMILY_GOOGLE_CSE - Google Custom Search JSON API (key+cx)
 * SEARCH_FAMILY_MEDIAWIKI  - action=query&list=search&format=json
 */
typedef enum SearchProviderFamily {
    SEARCH_FAMILY_SEARXNG,
    SEARCH_FAMILY_GOOGLE_CSE,
    SEARCH_FAMILY_MEDIAWIKI,
} SearchProviderFamily;

/**
 * Credential scheme of the backend.
 *
 * SEARCH_AUTH_NONE   - no credential (self-hosted, public wiki)
 * SEARCH_AUTH_KEY_CX - GOOGLE_CSE_KEY + GOOGLE_CSE_CX env pair
 */
typedef enum SearchProviderAuth {
    SEARCH_AUTH_NONE,
    SEARCH_AUTH_KEY_CX,
} SearchProviderAuth;

/**
 * SLOT RECORD, one row of the search directory (Rule 3 co-location).
 * Immutable: rows are static const data in search_provider.c; all
 * behavior hangs off the SearchProvider table class (Rule 33 Tier-2
 * waiver, see ;;INTENTION in search_provider.c).
 *
 * string slug            - canonical key, e.g. "searxng"
 * string displayName     - human label, e.g. "SearXNG (self-hosted)"
 * SearchProviderFamily family - wire family enum
 * SearchProviderAuth auth     - credential scheme enum
 * string endpoint        - base URL the renderer builds on
 * string note            - caveat; nullptr when none
 * u32 quotaPerDay             - free-tier requests/day (0 = unknown)
 * u32 quotaRemaining          - free-tier remainder (0 = unknown/exhausted)
 * i64 resetUnix               - quota window reset epoch secs (0 = unknown)
 * string authKind        - credential kind label; nullptr = see auth enum
 * string licenseFamily   - license family label; nullptr = UNKNOWN
 */
typedef struct SearchProviderSlot {
    string slug;
    string displayName;
    SearchProviderFamily family;
    SearchProviderAuth auth;
    string endpoint;
    string note;
    u32 quotaPerDay;
    u32 quotaRemaining;
    i64 resetUnix;
    string authKind;
    string licenseFamily;
} SearchProviderSlot;

/**
 * The table class, a singleton handle; state lives in the static const
 * rows in search_provider.c.
 *
 * u32 reserved - signature/marker; no mutable state
 */
typedef struct SearchProvider {
    u32 reserved;
} SearchProvider;

/**
 * --- Constructor ---
 * Returns the shared directory handle (static, zero-init, never nullptr).
 */
SearchProvider *SearchProvider_shared();

/**
 * --- Core functions ---
 * Total rows. Null-safe: 0 on nullptr self.
 */
u32 SearchProvider_count(const SearchProvider *self);
/** Row at index i. nullptr when out of range. */
const SearchProviderSlot *SearchProvider_at(const SearchProvider *self, u32 i);
/** Row by canonical slug. nullptr when absent. Linear scan (3 rows). */
const SearchProviderSlot *SearchProvider_get(const SearchProvider *self, string slug);
/**
 * Copies the row's endpoint into outBuf (NUL-terminated, dest-last per
 * Rule 9). False on nullptr self/slot/buf, zero cap, or truncation.
 */
bool SearchProvider_resolveEndpoint(const SearchProvider *self,
                                    const SearchProviderSlot *slot,
                                    utf8 *outBuf, bytes outCap);

/** --- Getters (Rule 24; null-safe; setters omitted, immutable rows) --- */
string SearchProvider_getSlug(const SearchProvider *self,
                                   const SearchProviderSlot *slot);
string SearchProvider_getDisplayName(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
SearchProviderFamily SearchProvider_getFamily(const SearchProvider *self,
                                              const SearchProviderSlot *slot);
SearchProviderAuth SearchProvider_getAuth(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
string SearchProvider_getEndpoint(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
string SearchProvider_getNote(const SearchProvider *self,
                                   const SearchProviderSlot *slot);
u32 SearchProvider_getQuotaPerDay(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
u32 SearchProvider_getQuotaRemaining(const SearchProvider *self,
                                          const SearchProviderSlot *slot);
i64 SearchProvider_getResetUnix(const SearchProvider *self,
                                    const SearchProviderSlot *slot);
string SearchProvider_getAuthKind(const SearchProvider *self,
                                       const SearchProviderSlot *slot);
string SearchProvider_getLicenseFamily(const SearchProvider *self,
                                            const SearchProviderSlot *slot);

#endif
