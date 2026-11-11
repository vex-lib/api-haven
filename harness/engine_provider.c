#include "annotation/annotation.h"
#include "harness/engine_provider.h"

#include <stddef.h>
#include <string.h>

;;DEFINITION
/** The CLI coding-engine directory: 20 static descriptors naming the engines the
 * Harness may drive, each with slug, display name, CLI binary, transport family
 * and credential scheme. It exists so an engine is a row, not a branch. Lookup
 * is linear and allocation-free and the table is immutable. Execution lives
 * behind the injected HarnessDriverTable — this file never spawns. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: EngineProvider (harness/engine_provider)
 * with zero code changes: edit rows, never touch logic)
 * ============================================================================
 * The CLI coding-engine directory: 20 static descriptor rows covering
 * the CLI engines the Harness may drive (claude-code, opencode,
 * kilocode, t3-code, agy, codex, gemini-cli, aider, pi, goose,
 * cursor-cli, kiro-cli, windsurf, qwen-code, crush, hermes, amp,
 * gptme, openhands, copilot-cli) — each with a canonical slug,
 * display name, CLI binary name, transport family, and credential
 * scheme. One table, linear slug lookup, zero allocation, immutable
 * (thread-safe reads without locks). Execution lives behind the
 * injected HarnessDriverTable (harness.h), implemented in
 * vexspoke/R3 — this file never spawns.
 *
 * STRUCT FIELDS (Mirroring harness/engine_provider.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   u32 reserved;   // singleton marker; no mutable state — all data
 *                        // lives in the static const EngineProviderSlot rows
 *
 * SLOT RECORD (EngineProviderSlot — Rule 3 co-location, zero behavior of
 * its own; all query behavior hangs off this table class):
 * ----------------------------------------------------------------------------
 *   const char *slug;           // canonical key, e.g. "claude-code"
 *   const char *displayName;    // human label, e.g. "Claude Code"
 *   const char *cliName;        // CLI binary the driver spawns
 *   EngineProviderFamily family;// transport bucket enum
 *   EngineProviderAuth   auth;  // credential scheme enum
 *   const char *note;           // caveat; NULL when none
 *
 * PRIVATE HELPERS (none — rows live in kEngineProviders[] below, each
 * row carrying exactly the SLOT RECORD fields above):
 * ----------------------------------------------------------------------------
 *   kEngineProviders[]      — 20 static const rows (this file only)
 *   kEngineProviderCount    — row count derived from the array
 *   sEngineProviderShared   — zero-init singleton handle
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - EngineProvider_shared()  : returns the singleton directory handle
 *
 * Core Functions:
 *   - EngineProvider_count(self)                   : total rows (20)
 *   - EngineProvider_at(self, i)                   : row at index i
 *   - EngineProvider_get(self, slug)               : row by slug
 *   - EngineProvider_resolveCli(self, slot, dest)  : cliName copy, dest-last
 *
 * Getters (Rule 24; null-safe. Setters omitted — immutable rows, waiver):
 *   - EngineProvider_getSlug / getDisplayName / getCliName / getFamily /
 *     getAuth / getNote(self, slot)
 * ============================================================================
 */
;;INTENTION("immutable slot records — setters omitted; data is static const; access via EngineProvider_* table functions — Rule 33 Tier-2 waiver")
;;INTENTION("descriptors only: no spawn/popen/system/fork/exec or threads here; execution is injected via HarnessDriverTable from vexspoke/R3 — Rule 17 api-haven clause")

static const EngineProviderSlot kEngineProviders[] = {
    {"claude-code", "Claude Code", "claude",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Anthropic subscription login via the CLI"},
    {"opencode", "opencode", "opencode",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "provider keys via opencode.json"},
    {"kilocode", "Kilocode", "kilocode",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "provider keys via Kilo Code config"},
    {"t3-code", "T3 Code", "t3",
     ENGINE_PROVIDER_FAMILY_REMOTE, ENGINE_PROVIDER_AUTH_OAUTH,
     "T3 Chat account via the CLI"},
    {"agy", "agy", "agy",
     ENGINE_PROVIDER_FAMILY_LOCAL, ENGINE_PROVIDER_AUTH_NONE,
     "local agent runtime, no credential"},
    {"codex", "Codex CLI", "codex",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "OpenAI ChatGPT Plus/Pro login via the CLI"},
    {"gemini-cli", "Gemini CLI", "gemini",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Google login via the CLI"},
    {"aider", "Aider", "aider",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "git-native edits via provider keys"},
    {"pi", "Pi", "pi",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_NONE,
     "local-first MIT harness, BYOK optional"},
    {"goose", "Goose", "goose",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "MCP-driven automation via provider keys"},
    {"cursor-cli", "Cursor CLI", "cursor-agent",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Cursor subscription via the CLI"},
    {"kiro-cli", "Kiro CLI", "kiro",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Kiro subscription via the CLI"},
    {"windsurf", "Windsurf", "windsurf",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Windsurf account via the CLI"},
    {"qwen-code", "Qwen Code", "qwen",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "Qwen provider keys via config"},
    {"crush", "Crush", "crush",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "BYOK provider keys, FSL license"},
    {"hermes", "Hermes Agent", "hermes",
     ENGINE_PROVIDER_FAMILY_LOCAL, ENGINE_PROVIDER_AUTH_NONE,
     "self harness over loopback, no credential"},
    {"amp", "Amp", "amp",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "Amp subscription via the CLI"},
    {"gptme", "gptme", "gptme",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "local-first agent via provider keys"},
    {"openhands", "OpenHands", "openhands",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_API_KEY,
     "autonomous runs via provider keys"},
    {"copilot-cli", "Copilot CLI", "copilot",
     ENGINE_PROVIDER_FAMILY_CLI, ENGINE_PROVIDER_AUTH_OAUTH,
     "GitHub login via the CLI"},
};

static const u32 kEngineProviderCount =
    (u32)(sizeof(kEngineProviders) / sizeof(kEngineProviders[0]));

static EngineProvider sEngineProviderShared; // zero-init singleton

// CONSTRUCTORS

/** Returns the process-wide immutable engine directory handle. */
EngineProvider *EngineProvider_shared(void) {
    return &sEngineProviderShared;
}

// CORE FUNCTIONS

/** Returns the directory row count, or zero for a null receiver. */
u32 EngineProvider_count(const EngineProvider *self) {
    if (!self)
        return 0;
    return kEngineProviderCount;
}

/** Returns the row at index i, or nullptr for a null receiver or invalid index. */
const EngineProviderSlot *EngineProvider_at(const EngineProvider *self, u32 i) {
    if (!self)
        return nullptr;
    if (i >= kEngineProviderCount)
        return nullptr;
    return &kEngineProviders[i];
}

/** Finds an engine by exact slug, returning nullptr when absent or invalid. */
const EngineProviderSlot *EngineProvider_get(const EngineProvider *self, const char *slug) {
    if (!self || !slug || (*slug) == '\0')
        return nullptr;
    for (u32 i = 0; i < kEngineProviderCount; i++) {
        const EngineProviderSlot *slot = &kEngineProviders[i];
        if ((*slot).slug && strcmp((*slot).slug, slug) == 0)
            return slot;
    }
    return nullptr;
}

/** Copies the row's CLI executable name into the bounded destination. */
bool EngineProvider_resolveCli(const EngineProvider *self,
                               const EngineProviderSlot *slot,
                               char *outBuf, bytes outCap) {
    if (!self || !slot || !outBuf || outCap == 0)
        return false;
    const char *cli = (*slot).cliName;
    if (!cli)
        return false;
    bytes len = strlen(cli);
    if (len + 1 > outCap)
        return false;
    memcpy(outBuf, cli, len + 1);
    return true;
}

// GETTERS

/** Returns the slot's canonical slug, or nullptr when no slot is supplied. */
const char *EngineProvider_getSlug(const EngineProvider *self,
                                   const EngineProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).slug : nullptr;
}

/** Returns the slot's display label, or nullptr when no slot is supplied. */
const char *EngineProvider_getDisplayName(const EngineProvider *self,
                                          const EngineProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).displayName : nullptr;
}

/** Returns the slot's CLI executable name, or nullptr when absent. */
const char *EngineProvider_getCliName(const EngineProvider *self,
                                      const EngineProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).cliName : nullptr;
}

/** Returns the slot's transport family, or the documented safe default. */
EngineProviderFamily EngineProvider_getFamily(const EngineProvider *self,
                                              const EngineProviderSlot *slot) {
    if (!self || !slot)
        return ENGINE_PROVIDER_FAMILY_CLI; // safe default
    return (*slot).family;
}

/** Returns the slot's credential scheme, or the documented safe default. */
EngineProviderAuth EngineProvider_getAuth(const EngineProvider *self,
                                          const EngineProviderSlot *slot) {
    if (!self || !slot)
        return ENGINE_PROVIDER_AUTH_NONE; // safe default
    return (*slot).auth;
}

/** Returns the slot's optional provider note. */
const char *EngineProvider_getNote(const EngineProvider *self,
                                   const EngineProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).note : nullptr;
}
