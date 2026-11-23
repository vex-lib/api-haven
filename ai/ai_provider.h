#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

#include "vex.h"

/**
 * ai/ai_provider.h, the AI provider directory (L1 descriptor table).
 *
 * One behavior class owns the whole ecosystem: names, native endpoints,
 * wire-contract families, auth schemes, and region buckets for every
 * provider the engine may talk to. Rows are static const slot records
 * (see ai_provider.c); this header declares the schema and the table API.
 */

/**
 * Wire contract of the provider's native endpoint.
 *
 * AI_PROVIDER_FAMILY_OPENAI_COMPAT - /chat/completions, OpenAI envelope
 * AI_PROVIDER_FAMILY_ANTHROPIC     - /v1/messages, Anthropic envelope
 * AI_PROVIDER_FAMILY_ROUTER        - gateway aggregating many upstreams
 * AI_PROVIDER_FAMILY_NATIVE        - bespoke envelope (Bedrock, Vertex, v0...)
 * AI_PROVIDER_FAMILY_LOCAL         - self-hosted (LMStudio, Ollama)
 */
typedef enum AiProviderFamily {
    AI_PROVIDER_FAMILY_OPENAI_COMPAT,
    AI_PROVIDER_FAMILY_ANTHROPIC,
    AI_PROVIDER_FAMILY_ROUTER,
    AI_PROVIDER_FAMILY_NATIVE,
    AI_PROVIDER_FAMILY_LOCAL,
} AiProviderFamily;

/**
 * Credential scheme of the provider's native endpoint.
 *
 * AI_PROVIDER_AUTH_BEARER    - Authorization: Bearer <key>
 * AI_PROVIDER_AUTH_API_KEY   - custom header (Azure "api-key": <key>)
 * AI_PROVIDER_AUTH_X_API_KEY - "x-api-key: <key>" (Anthropic, Cohere)
 * AI_PROVIDER_AUTH_NONE      - public or local (no credential)
 * AI_PROVIDER_AUTH_SPECIAL   - sigv4 / oauth / signed, adapter required
 */
typedef enum AiProviderAuth {
    AI_PROVIDER_AUTH_BEARER,
    AI_PROVIDER_AUTH_API_KEY,
    AI_PROVIDER_AUTH_X_API_KEY,
    AI_PROVIDER_AUTH_NONE,
    AI_PROVIDER_AUTH_SPECIAL,
} AiProviderAuth;

/** Mechanical region bucket (directory grouping, not a hard constraint). */
typedef enum AiProviderRegion {
    AI_PROVIDER_REGION_GLOBAL,
    AI_PROVIDER_REGION_CHINA,
    AI_PROVIDER_REGION_EUROPE,
    AI_PROVIDER_REGION_OTHER,
} AiProviderRegion;

/**
 * SLOT RECORD, one row of the provider directory (Rule 3 co-location).
 * Immutable: rows are static const data in ai_provider.c; all behavior
 * hangs off the AiProvider table class (Rule 33 Tier-2 waiver, see
 * ;;INTENTION in ai_provider.c).
 *
 * string slug          - canonical key, e.g. "openai", "deepseek"
 * string displayName   - human label, master list verbatim
 * string baseUrl       - verified native endpoint; nullptr = unverified
 * AiProviderFamily family   - wire contract
 * AiProviderAuth auth       - credential scheme
 * AiProviderRegion region   - bucket
 * string note          - caveat / variant note; nullptr when none
 * u32 quotaPerDay           - free-tier requests/day (0 = unknown)
 * u32 quotaRemaining        - free-tier remainder (0 = unknown/exhausted)
 * i64 resetUnix             - quota window reset epoch secs (0 = unknown)
 * string authKind      - credential kind label; nullptr = see auth enum
 * string licenseFamily - license family label; nullptr = UNKNOWN
 */
typedef struct AiProviderSlot {
    string slug;
    string displayName;
    string baseUrl;
    AiProviderFamily family;
    AiProviderAuth auth;
    AiProviderRegion region;
    string note;
    u32 quotaPerDay;
    u32 quotaRemaining;
    i64 resetUnix;
    string authKind;
    string licenseFamily;
} AiProviderSlot;

/**
 * The table class, a singleton handle; state lives in static const rows
 * generated under src/ai/data/ (see ai_provider.c overview).
 *
 * u32 reserved - signature/marker; no mutable state
 */
typedef struct AiProvider {
    u32 reserved;
} AiProvider;

/**
 * --- Constructor ---
 * Returns the shared directory handle (static, zero-init, never nullptr).
 */
AiProvider *AiProvider_shared();

/**
 * --- Core functions ---
 * Total rows across all region fragments. Null-safe: 0 on nullptr self.
 */
u32 AiProvider_count(const AiProvider *self);
/** Row at flat index i (fragments concatenated). nullptr when out of range. */
const AiProviderSlot *AiProvider_at(const AiProvider *self, u32 i);
/** Row by canonical slug. nullptr when absent. Linear scan (~260 rows). */
const AiProviderSlot *AiProvider_get(const AiProvider *self, string slug);
/**
 * Effective endpoint: verified baseUrl, else the family default
 * (anthropic  to  api.anthropic.com/v1; local  to  localhost:11434/v1;
 * otherwise the openrouter gateway). nullptr on nullptr self/slot.
 */
string AiProvider_resolveBaseUrl(const AiProvider *self,
                                      const AiProviderSlot *slot);

/** --- Getters (Rule 24; null-safe; setters omitted, immutable rows) --- */
string AiProvider_getSlug(const AiProvider *self, const AiProviderSlot *slot);
string AiProvider_getDisplayName(const AiProvider *self, const AiProviderSlot *slot);
string AiProvider_getBaseUrl(const AiProvider *self, const AiProviderSlot *slot);
AiProviderFamily AiProvider_getFamily(const AiProvider *self, const AiProviderSlot *slot);
AiProviderAuth AiProvider_getAuth(const AiProvider *self, const AiProviderSlot *slot);
AiProviderRegion AiProvider_getRegion(const AiProvider *self, const AiProviderSlot *slot);
string AiProvider_getNote(const AiProvider *self, const AiProviderSlot *slot);
u32 AiProvider_getQuotaPerDay(const AiProvider *self, const AiProviderSlot *slot);
u32 AiProvider_getQuotaRemaining(const AiProvider *self, const AiProviderSlot *slot);
i64 AiProvider_getResetUnix(const AiProvider *self, const AiProviderSlot *slot);
string AiProvider_getAuthKind(const AiProvider *self, const AiProviderSlot *slot);
string AiProvider_getLicenseFamily(const AiProvider *self, const AiProviderSlot *slot);

#endif