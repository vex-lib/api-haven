#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

#include <stdint.h>

// ai/ai_provider.h — the AI provider directory (L1 descriptor table).
//
// One behavior class owns the whole vexgraph-ecosystem-gitrepo: names, native endpoints,
// wire-contract families, auth schemes, and region buckets for every
// provider the engine may talk to. Rows are static const slot records
// (see ai_provider.c); this header declares the schema and the table API.

// Wire contract of the provider's native endpoint.
typedef enum AiProviderFamily {
    AI_PROVIDER_FAMILY_OPENAI_COMPAT, // /chat/completions, OpenAI envelope
    AI_PROVIDER_FAMILY_ANTHROPIC,     // /v1/messages, Anthropic envelope
    AI_PROVIDER_FAMILY_ROUTER,        // gateway aggregating many upstreams
    AI_PROVIDER_FAMILY_NATIVE,        // bespoke envelope (Bedrock, Vertex, v0...)
    AI_PROVIDER_FAMILY_LOCAL,         // self-hosted (LMStudio, Ollama)
} AiProviderFamily;

// Credential scheme of the provider's native endpoint.
typedef enum AiProviderAuth {
    AI_PROVIDER_AUTH_BEARER,    // Authorization: Bearer <key>
    AI_PROVIDER_AUTH_API_KEY,   // custom header (Azure "api-key": <key>)
    AI_PROVIDER_AUTH_X_API_KEY, // "x-api-key: <key>" (Anthropic, Cohere)
    AI_PROVIDER_AUTH_NONE,      // public or local (no credential)
    AI_PROVIDER_AUTH_SPECIAL,   // sigv4 / oauth / signed — adapter required
} AiProviderAuth;

// Mechanical region bucket (directory grouping, not a hard constraint).
typedef enum AiProviderRegion {
    AI_PROVIDER_REGION_GLOBAL,
    AI_PROVIDER_REGION_CHINA,
    AI_PROVIDER_REGION_EUROPE,
    AI_PROVIDER_REGION_OTHER,
} AiProviderRegion;

// SLOT RECORD — one row of the provider directory (Rule 3 co-location).
// Immutable: rows are static const data in ai_provider.c; all behavior
// hangs off the AiProvider table class (Rule 33 Tier-2 waiver, see
// ;;INTENTION in ai_provider.c).
typedef struct AiProviderSlot {
    const char *slug;        // canonical key, e.g. "openai", "deepseek"
    const char *displayName; // human label, master list verbatim
    const char *baseUrl;     // verified native endpoint; NULL = unverified
    AiProviderFamily family; // wire contract
    AiProviderAuth auth;     // credential scheme
    AiProviderRegion region; // bucket
    const char *note;        // caveat / variant note; NULL when none
    uint32_t quotaPerDay;    // free-tier requests/day (0 = unknown)
    uint32_t quotaRemaining; // free-tier remainder (0 = unknown/exhausted)
    int64_t resetUnix;       // quota window reset epoch secs (0 = unknown)
    const char *authKind;    // credential kind label; NULL = see auth enum
    const char *licenseFamily; // license family label; NULL = UNKNOWN
} AiProviderSlot;

// The table class — a singleton handle; state lives in static const rows
// generated under src/ai/data/ (see ai_provider.c overview).
typedef struct AiProvider {
    uint32_t reserved; // signature/marker; no mutable state
} AiProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
AiProvider *AiProvider_shared(void);

// --- Core functions ---
// Total rows across all region fragments. Null-safe: 0 on NULL self.
uint32_t AiProvider_count(const AiProvider *self);
// Row at flat index i (fragments concatenated). NULL when out of range.
const AiProviderSlot *AiProvider_at(const AiProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (~260 rows).
const AiProviderSlot *AiProvider_get(const AiProvider *self, const char *slug);
// Effective endpoint: verified baseUrl, else the family default
// (anthropic -> api.anthropic.com/v1; local -> localhost:11434/v1;
// otherwise the openrouter gateway). NULL on NULL self/slot.
const char *AiProvider_resolveBaseUrl(const AiProvider *self,
                                      const AiProviderSlot *slot);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *AiProvider_getSlug(const AiProvider *self, const AiProviderSlot *slot);
const char *AiProvider_getDisplayName(const AiProvider *self, const AiProviderSlot *slot);
const char *AiProvider_getBaseUrl(const AiProvider *self, const AiProviderSlot *slot);
AiProviderFamily AiProvider_getFamily(const AiProvider *self, const AiProviderSlot *slot);
AiProviderAuth AiProvider_getAuth(const AiProvider *self, const AiProviderSlot *slot);
AiProviderRegion AiProvider_getRegion(const AiProvider *self, const AiProviderSlot *slot);
const char *AiProvider_getNote(const AiProvider *self, const AiProviderSlot *slot);
uint32_t AiProvider_getQuotaPerDay(const AiProvider *self, const AiProviderSlot *slot);
uint32_t AiProvider_getQuotaRemaining(const AiProvider *self, const AiProviderSlot *slot);
int64_t AiProvider_getResetUnix(const AiProvider *self, const AiProviderSlot *slot);
const char *AiProvider_getAuthKind(const AiProvider *self, const AiProviderSlot *slot);
const char *AiProvider_getLicenseFamily(const AiProvider *self, const AiProviderSlot *slot);

#endif