#include "annotation/annotation.h"
#include "app/app_provider.h"

#include <stddef.h>
#include <string.h>

;;DEFINITION
/** The app/automation directory: 29 static descriptors naming the targets the
 * AppBroker may drive, each with slug, display name, transport family,
 * credential scheme and the bundle id / CLI / URL a driver resolves. It exists
 * so a new target is a row, not a branch. Lookup is linear and allocates
 * nothing; the table is immutable, so readers need no lock. Availability is
 * runtime — a target whose driver is unbound reports UNAVAILABLE through the
 * broker verdict rather than crashing the descriptor. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AppProvider (app/app_provider)
 * with zero code changes: edit rows, never touch logic)
 * ============================================================================
 * The app/automation directory: 29 static descriptor rows covering
 * the automation targets the AppBroker may drive (apple-notes,
 * apple-music, spotify-local, apple-shortcuts, osascript, telegram,
 * messenger, python, php, node, go-toolchain, rust-toolchain, unity,
 * unreal, godot, discord-bot, discord-presence, slack, whatsapp,
 * signal, email, sms, matrix, mattermost, line, teams, webhooks,
 * irc, imessage) — each with a canonical slug, display name,
 * transport family, credential scheme, and the bundle id / CLI / URL
 * a driver resolves. One table, linear slug lookup, zero allocation,
 * immutable (thread-safe reads without locks). Execution lives behind
 * the injected AppDriverTable (app_broker.h), implemented in
 * vexspoke/R3 — this file never runs scripts and never touches IPC.
 *
 * Availability is runtime, AppDetect-style: a target whose driver is
 * unbound (or whose helper is absent) reports UNAVAILABLE through the
 * broker verdict — descriptors never crash when the app is missing.
 *
 * STRUCT FIELDS (Mirroring app/app_provider.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   uint32_t reserved;   // singleton marker; no mutable state — all data
 *                        // lives in the static const AppProviderSlot rows
 *
 * SLOT RECORD (AppProviderSlot — Rule 3 co-location, zero behavior of
 * its own; all query behavior hangs off this table class):
 * ----------------------------------------------------------------------------
 *   const char *slug;             // canonical key, e.g. "apple-notes"
 *   const char *displayName;      // human label, e.g. "Notes"
 *   AppProviderFamily family;     // transport bucket enum
 *   AppProviderAuth   auth;       // credential scheme enum
 *   const char *bundleIdOrScheme; // bundle id, CLI, or URL for the driver
 *   const char *note;             // caveat; nullptr when none
 *
 * PRIVATE HELPERS (none — rows live in kAppProviders[] below, each row
 * carrying exactly the SLOT RECORD fields above):
 * ----------------------------------------------------------------------------
 *   kAppProviders[]      — 29 static const rows (this file only)
 *   kAppProviderCount    — row count derived from the array
 *   sAppProviderShared   — zero-init singleton handle
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - AppProvider_shared()  : returns the singleton directory handle
 *
 * Core Functions:
 *   - AppProvider_count(self)                    : total rows (29)
 *   - AppProvider_at(self, i)                    : row at index i
 *   - AppProvider_get(self, slug)                : row by slug
 *   - AppProvider_resolveTarget(self, slot, dest): bundle/CLI/URL copy
 *
 * Getters (Rule 24; null-safe. Setters omitted — immutable rows, waiver):
 *   - AppProvider_getSlug / getDisplayName / getFamily / getAuth /
 *     getBundleIdOrScheme / getNote(self, slot)
 * ============================================================================
 */
;;PLATFORM_EXCLUSIVE("macOS")
;;INTENTION("messaging rows mirror the Hermes Agent gateway surface (MIT, NousResearch; behavior referenced from hermes_text.md, no Hermes code vendored of course)")
;;INTENTION("Apple-automation rows (notes/music/shortcuts/osascript) are macOS-only; webhook/socket rows degrade to UNAVAILABLE via the unbound-driver verdict when the helper is absent — AppDetect pattern, never a crash")
;;INTENTION("immutable slot records — setters omitted; data is static const; access via AppProvider_* table functions — Rule 33 Tier-2 waiver")
;;INTENTION("descriptors only: no osascript/IPC/sockets/threads here; execution is injected via AppDriverTable from vexspoke/R3 — Rule 17 api-haven clause")

static const AppProviderSlot kAppProviders[] = {
    {"apple-notes", "Notes",
     APP_PROVIDER_FAMILY_OSA_SCRIPT, APP_PROVIDER_AUTH_SYSTEM,
     "com.apple.Notes",
     "Apple Notes via osascript driver; macOS only"},
    {"apple-music", "Music",
     APP_PROVIDER_FAMILY_MUSIC_LOCAL, APP_PROVIDER_AUTH_SYSTEM,
     "com.apple.Music",
     "Music.app automation; macOS only"},
    {"spotify-local", "Spotify (local)",
     APP_PROVIDER_FAMILY_LOCAL_SOCKET, APP_PROVIDER_AUTH_NONE,
     "http://localhost:4370",
     "local web helper; UNAVAILABLE when the helper is absent"},
    {"apple-shortcuts", "Shortcuts",
     APP_PROVIDER_FAMILY_SHORTCUTS_CLI, APP_PROVIDER_AUTH_SYSTEM,
     "shortcuts",
     "shortcuts CLI runner; macOS only"},
    {"osascript", "osascript",
     APP_PROVIDER_FAMILY_OSA_SCRIPT, APP_PROVIDER_AUTH_SYSTEM,
     "osascript",
     "generic AppleScript runner; macOS only"},
    {"telegram", "Telegram",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://api.telegram.org",
     "bot token required, never stored in the arena"},
    {"messenger", "Messenger",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://graph.facebook.com",
     "page token required, never stored in the arena"},
    {"python", "Python",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "python3",
     "language runtime probe; UNAVAILABLE when not installed"},
    {"php", "PHP",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "php",
     "language runtime probe; UNAVAILABLE when not installed"},
    {"node", "Node",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "node",
     "language runtime probe; UNAVAILABLE when not installed"},
    {"go-toolchain", "Go",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "go",
     "language runtime probe; UNAVAILABLE when not installed"},
    {"rust-toolchain", "Rust",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "cargo",
     "language runtime probe; UNAVAILABLE when not installed"},
    {"unity", "Unity",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "Unity",
     "Unity Editor binary; UNAVAILABLE when not installed"},
    {"unreal", "Unreal",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "UnrealEditor",
     "Unreal Editor binary; UNAVAILABLE when not installed"},
    {"godot", "Godot",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "godot",
     "Godot binary; UNAVAILABLE when not installed"},
    {"discord-bot", "Discord Bot",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://discord.com/api",
     "bot token required, never stored in the arena"},
    {"discord-presence", "Discord Presence",
     APP_PROVIDER_FAMILY_LOCAL_SOCKET, APP_PROVIDER_AUTH_NONE,
     "http://localhost:6463",
     "Rich Presence IPC; UNAVAILABLE when Discord is closed"},
    {"slack", "Slack",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://slack.com/api",
     "Socket Mode bot; token required, never stored in the arena"},
    {"whatsapp", "WhatsApp",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://graph.facebook.com",
     "Business Cloud API; token required (Baileys bridge alt)"},
    {"signal", "Signal",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "signal-cli",
     "signal-cli daemon bridge; UNAVAILABLE when absent"},
    {"email", "Email",
     APP_PROVIDER_FAMILY_NATIVE_CLI, APP_PROVIDER_AUTH_NONE,
     "himalaya",
     "IMAP/SMTP via CLI helper; creds via ApiAuth, never in arena"},
    {"sms", "SMS",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://api.twilio.com",
     "Twilio SMS; token required, never stored in the arena"},
    {"matrix", "Matrix",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://matrix.org",
     "homeserver URL configurable; token required"},
    {"mattermost", "Mattermost",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://mattermost.com",
     "bot token required, never stored in the arena"},
    {"line", "LINE",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://api.line.me",
     "LINE Messaging API; channel token required"},
    {"teams", "Teams",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_TOKEN,
     "https://graph.microsoft.com",
     "Teams bot via Graph; token required"},
    {"webhooks", "Webhooks",
     APP_PROVIDER_FAMILY_REST_WEBHOOK, APP_PROVIDER_AUTH_NONE,
     "https://github.com",
     "inbound GitHub/GitLab events; no credential"},
    {"irc", "IRC",
     APP_PROVIDER_FAMILY_LOCAL_SOCKET, APP_PROVIDER_AUTH_NONE,
     "irc://localhost",
     "plugin bridge; UNAVAILABLE when bridge absent"},
    {"imessage", "iMessage",
     APP_PROVIDER_FAMILY_LOCAL_SOCKET, APP_PROVIDER_AUTH_NONE,
     "http://localhost:4801",
     "BlueBubbles bridge; macOS only, UNAVAILABLE when absent"},
};

static const uint32_t kAppProviderCount =
    (uint32_t)(sizeof(kAppProviders) / sizeof(kAppProviders[0]));

static AppProvider sAppProviderShared; // zero-init singleton

// CONSTRUCTORS

/** Returns the process-wide immutable app/automation directory handle. */
AppProvider *AppProvider_shared(void) {
    return &sAppProviderShared;
}

// CORE FUNCTIONS

/** Returns the directory row count, or zero for a null receiver. */
uint32_t AppProvider_count(const AppProvider *self) {
    if (!self)
        return 0;
    return kAppProviderCount;
}

/** Returns the row at index i, or nullptr for a null receiver or invalid index. */
const AppProviderSlot *AppProvider_at(const AppProvider *self, uint32_t i) {
    if (!self)
        return nullptr;
    if (i >= kAppProviderCount)
        return nullptr;
    return &kAppProviders[i];
}

/** Finds an automation target by exact slug, returning nullptr when absent. */
const AppProviderSlot *AppProvider_get(const AppProvider *self, const char *slug) {
    if (!self || !slug || (*slug) == '\0')
        return nullptr;
    for (uint32_t i = 0; i < kAppProviderCount; i++) {
        const AppProviderSlot *slot = &kAppProviders[i];
        if ((*slot).slug && strcmp((*slot).slug, slug) == 0)
            return slot;
    }
    return nullptr;
}

/** Copies the row's bundle, CLI, or URL target into the bounded destination. */
bool AppProvider_resolveTarget(const AppProvider *self,
                               const AppProviderSlot *slot,
                               char *outBuf, size_t outCap) {
    if (!self || !slot || !outBuf || outCap == 0)
        return false;
    const char *target = (*slot).bundleIdOrScheme;
    if (!target)
        return false;
    size_t len = strlen(target);
    if (len + 1 > outCap)
        return false;
    memcpy(outBuf, target, len + 1);
    return true;
}

// GETTERS

/** Returns the slot's canonical slug, or nullptr when no slot is supplied. */
const char *AppProvider_getSlug(const AppProvider *self,
                                const AppProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).slug : nullptr;
}

/** Returns the slot's display label, or nullptr when no slot is supplied. */
const char *AppProvider_getDisplayName(const AppProvider *self,
                                       const AppProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).displayName : nullptr;
}

/** Returns the slot's transport family, or the documented safe default. */
AppProviderFamily AppProvider_getFamily(const AppProvider *self,
                                        const AppProviderSlot *slot) {
    if (!self || !slot)
        return APP_PROVIDER_FAMILY_OSA_SCRIPT; // safe default
    return (*slot).family;
}

/** Returns the slot's authentication scheme, or the documented safe default. */
AppProviderAuth AppProvider_getAuth(const AppProvider *self,
                                    const AppProviderSlot *slot) {
    if (!self || !slot)
        return APP_PROVIDER_AUTH_NONE; // safe default
    return (*slot).auth;
}

/** Returns the slot's bundle identifier, CLI name, or URL target string. */
const char *AppProvider_getBundleIdOrScheme(const AppProvider *self,
                                            const AppProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).bundleIdOrScheme : nullptr;
}

/** Returns the slot's optional provider caveat. */
const char *AppProvider_getNote(const AppProvider *self,
                                const AppProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).note : nullptr;
}
