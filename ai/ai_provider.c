#include "annotation/annotation.h"
#include "ai/ai_provider.h"

#include <stddef.h>
#include <string.h>

;;DEFINITION
/** The provider directory. It exists so the engine gains or loses a provider by
 * editing a row instead of a code path: ~260 static descriptors carry name,
 * endpoint, wire family, auth scheme and region bucket, read by linear slug
 * lookup. The table is immutable and the lookup allocates nothing, so
 * concurrent readers need no lock. It resolves identity only — no request is
 * ever sent from here. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiProvider (ai/ai_provider)
 * with zero code changes: edit regenerate data, never touch logic)
 * ============================================================================
 * The AI provider directory: ~260 static descriptor rows for every provider
 * the engine may talk to — names, native endpoints, wire-contract families,
 * auth schemes, region buckets. One table, linear slug lookup, zero
 * allocation, immutable (thread-safe reads without locks).
 *
 * STRUCT FIELDS (Mirroring ai/ai_provider.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   u32 reserved;   // singleton marker; no mutable state — all data
 *                        // lives in the static const AiProviderSlot rows
 *
  * SLOT RECORD (AiProviderSlot — Rule 3 co-location, zero behavior of its own:
  * all query behavior hangs off this table class):
  * ----------------------------------------------------------------------------
  *   const char *slug;         // canonical key, e.g. "openai", "deepseek"
  *   const char *displayName;  // human label, master list verbatim
  *   const char *baseUrl;      // verified native endpoint; NULL = unverified
  *   AiProviderFamily family;  // wire contract enum
  *   AiProviderAuth   auth;    // credential scheme enum
  *   AiProviderRegion region;  // region bucket enum
  *   const char *note;         // caveat / variant note; NULL when none
  *   u32 quotaPerDay;     // free-tier requests/day (0 = unknown)
  *   u32 quotaRemaining;  // free-tier remainder (0 = unknown/exhausted)
  *   int64_t  resetUnix;       // quota window reset epoch secs (0 = unknown)
  *   const char *authKind;     // credential kind label; NULL = see auth
  *   const char *licenseFamily;// license family label; NULL = UNKNOWN
  *
  * Quota columns (tools/gen_providers.py QUOTA table): HF/Groq/Cloudflare
  * rows carry first-pass quotaPerDay/authKind/licenseFamily values; every
  * other generated row reads 0/NULL (unknown) until verified per-provider
  * numbers land. Supabase/Neon are database rows (db_provider.c), not AI
  * rows — their quotas belong to the database catalog when it gains them.
 *
 * PRIVATE HELPERS (generated data fragments — each row carries exactly the
 * SLOT RECORD fields above; fragments are included ONLY by this file):
 * ----------------------------------------------------------------------------
 *   kAiProvidersGlobal[]  — src/ai/data/providers_global.inc (215 rows)
 *   kAiProvidersChina[]   — src/ai/data/providers_china.inc  ( 32 rows)
 *   kAiProvidersEurope[]  — src/ai/data/providers_europe.inc ( 13 rows)
 *   kAiProviderGroups[]   — pointer table over the three fragments
 *   kAiProviderGroupSizes[] / kAiProviderGroupCount — row counts per fragment
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - AiProvider_shared()  : returns the singleton directory handle
 *
 * Core Functions:
 *   - AiProvider_count(self)                : total rows (260)
 *   - AiProvider_at(self, i)                : row at flat index i
 *   - AiProvider_get(self, slug)            : row by slug (linear scan)
 *   - AiProvider_resolveBaseUrl(self, slot) : baseUrl or family default
 *
  * Getters (Rule 24; null-safe. Setters omitted — immutable rows, waiver):
  *   - AiProvider_getSlug / getDisplayName / getBaseUrl / getFamily /
  *     getAuth / getRegion / getNote / getQuotaPerDay / getQuotaRemaining /
  *     getResetUnix / getAuthKind / getLicenseFamily(self, slot)
 * ============================================================================
 */
;;INTENTION("slot records decomposed into per-region .inc fragments for mechanical maintainability; included only by ai_provider.c — Rule 3 managed exception")
;;INTENTION("immutable slot records — setters omitted; data is static const; access via AiProvider_* table functions — Rule 33 Tier-2 waiver")
;;INTENTION("schema-first overview — 260 rows documented as schema + fragment counts, not an enumeration; each fragment header lists its set — Rule 23 zero-drift")

#include "ai/data/providers_global.inc"
#include "ai/data/providers_china.inc"
#include "ai/data/providers_europe.inc"

static const AiProviderSlot *const kAiProviderGroups[] = {
    kAiProvidersGlobal,
    kAiProvidersChina,
    kAiProvidersEurope,
};

static const u32 kAiProviderGroupSizes[] = {
    (u32)(sizeof(kAiProvidersGlobal) / sizeof(kAiProvidersGlobal[0])),
    (u32)(sizeof(kAiProvidersChina) / sizeof(kAiProvidersChina[0])),
    (u32)(sizeof(kAiProvidersEurope) / sizeof(kAiProvidersEurope[0])),
};

static const u32 kAiProviderGroupCount =
    (u32)(sizeof(kAiProviderGroups) / sizeof(kAiProviderGroups[0]));

static AiProvider sAiProviderShared; // zero-init singleton

// CONSTRUCTORS

/** Returns the process-wide immutable AI-provider directory handle. */
AiProvider *AiProvider_shared(void) {
    return &sAiProviderShared;
}

// CORE FUNCTIONS

/** Returns the number of directory rows, or zero for a null receiver. */
u32 AiProvider_count(const AiProvider *self) {
    if (!self)
        return 0;
    u32 total = 0;
    for (u32 g = 0; g < kAiProviderGroupCount; g++)
        total += kAiProviderGroupSizes[g];
    return total;
}

/** Returns the flattened directory row at index i, or nullptr out of range. */
const AiProviderSlot *AiProvider_at(const AiProvider *self, u32 i) {
    if (!self)
        return nullptr;
    for (u32 g = 0; g < kAiProviderGroupCount; g++) {
        if (i < kAiProviderGroupSizes[g])
            return &kAiProviderGroups[g][i];
        i -= kAiProviderGroupSizes[g];
    }
    return nullptr;
}

/** Finds a provider by exact slug, returning nullptr when absent or invalid. */
const AiProviderSlot *AiProvider_get(const AiProvider *self, const char *slug) {
    if (!self || !slug || (*slug) == '\0')
        return nullptr;
    const u32 total = AiProvider_count(self);
    for (u32 i = 0; i < total; i++) {
        const AiProviderSlot *slot = AiProvider_at(self, i);
        if ((*slot).slug && strcmp((*slot).slug, slug) == 0)
            return slot;
    }
    return nullptr;
}

/** Returns a row's configured endpoint or the fallback for its provider family. */
const char *AiProvider_resolveBaseUrl(const AiProvider *self,
                                      const AiProviderSlot *slot) {
    if (!self || !slot)
        return nullptr;
    if ((*slot).baseUrl)
        return (*slot).baseUrl;
    switch ((*slot).family) {
    case AI_PROVIDER_FAMILY_ANTHROPIC:
        return "https://api.anthropic.com/v1";
    case AI_PROVIDER_FAMILY_LOCAL:
        return "http://localhost:11434/v1";
    case AI_PROVIDER_FAMILY_ROUTER:
    case AI_PROVIDER_FAMILY_OPENAI_COMPAT:
    case AI_PROVIDER_FAMILY_NATIVE:
    default:
        return "https://openrouter.ai/api/v1";
    }
}

// GETTERS

/** Returns the slot's canonical slug, or nullptr when no slot is supplied. */
const char *AiProvider_getSlug(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).slug : nullptr;
}

/** Returns the slot's display label, or nullptr when no slot is supplied. */
const char *AiProvider_getDisplayName(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).displayName : nullptr;
}

/** Returns the slot's explicitly configured endpoint, if any. */
const char *AiProvider_getBaseUrl(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).baseUrl : nullptr;
}

/** Returns the slot's wire-contract family, or the documented safe default. */
AiProviderFamily AiProvider_getFamily(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return AI_PROVIDER_FAMILY_OPENAI_COMPAT; // safe default
    return (*slot).family;
}

/** Returns the slot's credential scheme, or the documented safe default. */
AiProviderAuth AiProvider_getAuth(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return AI_PROVIDER_AUTH_BEARER; // safe default
    return (*slot).auth;
}

/** Returns the slot's region classification, or the documented safe default. */
AiProviderRegion AiProvider_getRegion(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return AI_PROVIDER_REGION_GLOBAL; // safe default
    return (*slot).region;
}

/** Returns the slot's optional note, or nullptr when absent. */
const char *AiProvider_getNote(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).note : nullptr;
}

/** Returns the slot's daily quota value, or zero when unavailable. */
u32 AiProvider_getQuotaPerDay(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).quotaPerDay;
}

/** Returns the slot's remaining quota value, or zero when unavailable. */
u32 AiProvider_getQuotaRemaining(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).quotaRemaining;
}

/** Returns the slot's quota-reset Unix time, or zero when unavailable. */
int64_t AiProvider_getResetUnix(const AiProvider *self, const AiProviderSlot *slot) {
    if (!self || !slot)
        return 0;
    return (*slot).resetUnix;
}

/** Returns the slot's optional human-readable credential-kind label. */
const char *AiProvider_getAuthKind(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).authKind : nullptr;
}

/** Returns the slot's optional license-family label. */
const char *AiProvider_getLicenseFamily(const AiProvider *self, const AiProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).licenseFamily : nullptr;
}
