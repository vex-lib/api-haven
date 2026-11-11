#include "annotation/annotation.h"
#include "search/search_provider.h"

#include <stddef.h>
#include <string.h>

;;DEFINITION
/** The blessed web-search directory: 3 static descriptors (searxng, google-cse,
 * wikimedia) with slug, display name, wire family, credential scheme and endpoint
 * base. It exists so a search backend is a row, not a branch; queries run through
 * api/rest.h in the MCP renderer, never here. Calling a documented API and
 * parsing its documented JSON is a client using a contract — scraping is not
 * something this class does. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SearchProvider (search/search_provider)
 * with zero code changes: edit rows, never touch logic)
 * ============================================================================
 * The blessed web-search directory: 3 static descriptor rows covering
 * the search backends the engine may query (searxng, google-cse,
 * wikimedia) — each with a canonical slug, display name, wire family,
 * credential scheme, and endpoint base. One table, linear slug lookup,
 * zero allocation, immutable (thread-safe reads without locks).
 * Queries run through api/rest.h in the MCP renderer, never here.
 *
 * NOT SCRAPING (basic computer science): calling a documented search
 * API over HTTP and parsing its documented JSON response is a client
 * using a contract — request schema in, response schema out, credentials
 * where the contract says. Scraping is the opposite: fetching human
 * HTML meant for browsers and reverse-engineering it into a fake API
 * (selectors as schema, layout as versioning, ToS bypass as auth).
 * One is a function call over a socket; the other is screen-reading
 * someone else's UI and pretending it's an interface. Rule 34 bans the
 * second; every row below is the first — a real search API or
 * self-hosted SearXNG.
 *
 * STRUCT FIELDS (Mirroring search/search_provider.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   u32 reserved;   // singleton marker; no mutable state — all data
 *                        // lives in the static const SearchProviderSlot rows
 *
  * SLOT RECORD (SearchProviderSlot — Rule 3 co-location, zero behavior of
  * its own; all query behavior hangs off this table class):
  * ----------------------------------------------------------------------------
  *   const char *slug;            // canonical key, e.g. "searxng"
  *   const char *displayName;     // human label
  *   SearchProviderFamily family; // wire family enum
  *   SearchProviderAuth   auth;   // credential scheme enum
  *   const char *endpoint;        // base URL the renderer builds on
  *   const char *note;            // caveat; NULL when none
  *   u32 quotaPerDay;        // free-tier requests/day (0 = unknown)
  *   u32 quotaRemaining;     // free-tier remainder (0 = unknown)
  *   int64_t  resetUnix;          // quota window reset epoch secs (0 = unknown)
  *   const char *authKind;        // credential kind label; NULL = see auth
  *   const char *licenseFamily;   // license family label; NULL = UNKNOWN
  *
  * TODO(quota): the 3 static rows below carry first-pass quota values
  * (google-cse 100/day free tier, wikimedia CC-BY-SA, searxng unlimited
  * self-hosted = 0/unknown). Refine per-backend numbers when verified,
  * mirroring the AiProvider QUOTA table in tools/gen_providers.py.
 *
 * PRIVATE HELPERS (none — rows live in kSearchProviders[] below, each
 * row carrying exactly the SLOT RECORD fields above):
 * ----------------------------------------------------------------------------
 *   kSearchProviders[]      — 3 static const rows (this file only)
 *   kSearchProviderCount    — row count derived from the array
 *   sSearchProviderShared   — zero-init singleton handle
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - SearchProvider_shared()  : returns the singleton directory handle
 *
 * Core Functions:
 *   - SearchProvider_count(self)                     : total rows (3)
 *   - SearchProvider_at(self, i)                     : row at index i
 *   - SearchProvider_get(self, slug)                 : row by slug
 *   - SearchProvider_resolveEndpoint(self, slot, dest) : endpoint copy
 *
  * Getters (Rule 24; null-safe. Setters omitted — immutable rows, waiver):
  *   - SearchProvider_getSlug / getDisplayName / getFamily / getAuth /
  *     getEndpoint / getNote / getQuotaPerDay / getQuotaRemaining /
  *     getResetUnix / getAuthKind / getLicenseFamily(self, slot)
 * ============================================================================
 */
;;INTENTION("immutable slot records — setters omitted; data is static const; access via SearchProvider_* table functions — Rule 33 Tier-2 waiver")
;;INTENTION("descriptors only: no sockets/HTTP here; queries run via api/rest.h in the MCP renderer — Rule 17 api-haven clause; scraping to fake a search API is a Rule 34 defect")

static const SearchProviderSlot kSearchProviders[] = {
    {"searxng", "SearXNG (self-hosted)",
     SEARCH_FAMILY_SEARXNG, SEARCH_AUTH_NONE,
     "http://localhost:8888",
     "self-hosted metasearch; base overridable per call; works today (http)",
     0, 0, 0, nullptr, nullptr},
    {"google-cse", "Google Custom Search",
     SEARCH_FAMILY_GOOGLE_CSE, SEARCH_AUTH_KEY_CX,
     "https://www.googleapis.com",
     "needs GOOGLE_CSE_KEY + GOOGLE_CSE_CX env; https needs TLS backend",
     100, 0, 0, "GOOGLE_CSE_KEY+CX pair", "proprietary"},
    {"wikimedia", "Wikimedia",
     SEARCH_FAMILY_MEDIAWIKI, SEARCH_AUTH_NONE,
     "https://en.wikipedia.org",
     "keyless MediaWiki API; https needs TLS backend",
     0, 0, 0, nullptr, "CC-BY-SA"},
};

static const u32 kSearchProviderCount =
    (u32)(sizeof(kSearchProviders) / sizeof(kSearchProviders[0]));

static SearchProvider sSearchProviderShared; // zero-init singleton

// CONSTRUCTORS

/** Returns the process-wide immutable web-search directory handle. */
SearchProvider *SearchProvider_shared(void) {
    return &sSearchProviderShared;
}

// CORE FUNCTIONS

/** Returns the directory row count, or zero for a null receiver. */
u32 SearchProvider_count(const SearchProvider *self) {
    if (!self)
        return 0;
    return kSearchProviderCount;
}

/** Returns the row at index i, or nullptr for a null receiver or invalid index. */
const SearchProviderSlot *SearchProvider_at(const SearchProvider *self, u32 i) {
    if (!self)
        return nullptr;
    if (i >= kSearchProviderCount)
        return nullptr;
    return &kSearchProviders[i];
}

/** Finds a search provider by exact slug, returning nullptr when absent. */
const SearchProviderSlot *SearchProvider_get(const SearchProvider *self, const char *slug) {
    if (!self || !slug || (*slug) == '\0')
        return nullptr;
    for (u32 i = 0; i < kSearchProviderCount; i++) {
        const SearchProviderSlot *slot = &kSearchProviders[i];
        if ((*slot).slug && strcmp((*slot).slug, slug) == 0)
            return slot;
    }
    return nullptr;
}

/** Copies the provider endpoint into outBuf when it fits including its terminator. */
bool SearchProvider_resolveEndpoint(const SearchProvider *self,
                                    const SearchProviderSlot *slot,
                                    char *outBuf, bytes outCap) {
    if (!self || !slot || !outBuf || outCap == 0)
        return false;
    const char *ep = (*slot).endpoint;
    if (!ep)
        return false;
    bytes len = strlen(ep);
    if (len + 1 > outCap)
        return false;
    memcpy(outBuf, ep, len + 1);
    return true;
}

// GETTERS

/** Returns the slot's canonical slug, or nullptr when no slot is supplied. */
const char *SearchProvider_getSlug(const SearchProvider *self,
                                   const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).slug : nullptr;
}

/** Returns the slot's display label, or nullptr when no slot is supplied. */
const char *SearchProvider_getDisplayName(const SearchProvider *self,
                                          const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).displayName : nullptr;
}

/** Returns the slot's wire family, or the documented safe default. */
SearchProviderFamily SearchProvider_getFamily(const SearchProvider *self,
                                              const SearchProviderSlot *slot) {
    if (!self || !slot)
        return SEARCH_FAMILY_SEARXNG; // safe default
    return (*slot).family;
}

/** Returns the slot's authentication scheme, or the documented safe default. */
SearchProviderAuth SearchProvider_getAuth(const SearchProvider *self,
                                          const SearchProviderSlot *slot) {
    if (!self || !slot)
        return SEARCH_AUTH_NONE; // safe default
    return (*slot).auth;
}

/** Returns the slot's configured search endpoint, if any. */
const char *SearchProvider_getEndpoint(const SearchProvider *self,
                                       const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).endpoint : nullptr;
}

/** Returns the slot's optional provider note. */
const char *SearchProvider_getNote(const SearchProvider *self,
                                   const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).note : nullptr;
}

/** Returns the slot's daily quota value, or zero when unavailable. */
u32 SearchProvider_getQuotaPerDay(const SearchProvider *self,
                                       const SearchProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).quotaPerDay;
}

/** Returns the slot's remaining quota value, or zero when unavailable. */
u32 SearchProvider_getQuotaRemaining(const SearchProvider *self,
                                          const SearchProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).quotaRemaining;
}

/** Returns the slot's quota-reset Unix time, or zero when unavailable. */
int64_t SearchProvider_getResetUnix(const SearchProvider *self,
                                    const SearchProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).resetUnix;
}

/** Returns the slot's optional human-readable credential-kind label. */
const char *SearchProvider_getAuthKind(const SearchProvider *self,
                                       const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).authKind : nullptr;
}

/** Returns the slot's optional license-family label. */
const char *SearchProvider_getLicenseFamily(const SearchProvider *self,
                                            const SearchProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).licenseFamily : nullptr;
}
