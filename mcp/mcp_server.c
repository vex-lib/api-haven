#include "annotation/annotation.h"
#include "mcp/mcp_server.h"

#include "ai/ai_provider.h"
#include "api/rest.h"
#include "app/app_broker.h"
#include "app/app_provider.h"
#include "asset/asset_broker.h"
#include "asset/asset_provider.h"
#include "database/db_provider.h"
#include "harness/engine_provider.h"
#include "harness/harness.h"
#include "net/json.h"
#include "search/search_provider.h"
#include "system/app_detect.h"
#include "system/capture_tool.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** The Model Context Protocol engine: a newline-delimited JSON-RPC 2.0 core that
 * hosts the connector shapes as MCP tools and resources. It exists so the tool
 * surface is described by tables rather than by control flow — the handshake,
 * ping and tool/resource dispatch all read the same immutable McpToolSlot /
 * McpResourceSlot records, each handler a bare fn pointer into render logic.
 * Zero allocation, no network, single-threaded. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: McpServer (mcp/mcp_server)
 * api-haven CLI/MCP tool server; no OS/window/memory management)
 * ============================================================================
 * The Model Context Protocol engine: newline-delimited JSON-RPC 2.0
 * server core that hosts the connector shapes as MCP tools and
 * resources. Handles the handshake (initialize), liveness (ping), and
 * the tool/resource surface, rendering responses into caller-owned
 * buffers. Immutable describer tables (McpToolSlot / McpResourceSlot)
 * hang off this class (Rule 3 slot-record co-location); each handler is
 * a bare fn pointer into registry-render logic — zero allocation, no
 * network, single-threaded.
 *
 * STRUCT FIELDS (Mirroring mcp/mcp_server.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   char protocolVersion[32]; // negotiated client version or latest
 *   uint32_t initialized;     // 1 after a successful initialize handshake
 *   const char *name;         // "vexgraph-mcp"
 *   const char *version;      // MCP_SERVER_VERSION
 *
 * SLOT RECORD (McpToolSlot — Rule 3 co-location, behavior deferred to the
 * fn-pointer handler; one row per exposed tool):
 * ----------------------------------------------------------------------------
 *   const char *name;         // tool key, e.g. "app_detect"
 *   const char *description;  // one-line MCP description
 *   const char *inputSchema;  // JSON Schema (pre-rendered static text)
 *   McpToolFn handle;         // bool (*)(doc, argsRef, out, cap)
 *
 * SLOT RECORD (McpResourceSlot — same co-location; one row per URI):
 * ----------------------------------------------------------------------------
 *   const char *uri;          // e.g. "system://apps"
 *   const char *name;         // human label
 *   const char *mimeType;     // "text/plain"
 *   McpToolFn render;         // bool (*)(doc, -1, out, cap) — no args
 *
 * PRIVATE HELPERS (static, file-local — signatures + roles so the file
 * reads from the overview alone; each is pure render/protocol logic with
 * zero state):
 * ----------------------------------------------------------------------------
 *   appendStr / appendFmt(buf, cap, pos, ...)   — bounded builders
 *   familyName / authName / regionName / dbFamilyName / kindName(...) —
 *                          enum→label maps with fallbacks
 *   containsFold(haystack, needle) — ASCII case-insensitive substring
 *                          (registry rows are ASCII identifiers)
 *   escapeJsonText(src, out, cap)   — \" \\ \n \r \t escaping for body text
 *   methodIs(doc, ref, want)        — view-safe method compare
 *   readStringArg(doc, argsRef, key, out, cap) — copy an args string;
 *                          false when absent/not-a-string (args=-1 ⇒ false)
 *   rawIdExtract(line, len, out, cap) — copy the raw "id" token verbatim
 *                          (quoted string id keeps its quotes; null/absent
 *                          ⇒ false so the caller treats it as notification)
 *   respondError(code, message, id, out, cap) — JSON-RPC error envelope
 *   respondPing(id, out, cap)                  — empty result
 *   respondToolsList(id, out, cap)             — static tool table
 *   respondResourcesList(id, out, cap)         — static resource table
 *   respondInitializeReal(self, doc, id, out, cap) — negotiate protocol
 *                          version + capabilities handshake
 *   renderAppDetect(doc, argsRef, out, cap)    — tool + resource body for
 *                          system://apps (14 known apps; optional "name"
 *                          arg for a single app detail)
 *   renderCaptureStatus(doc, argsRef, out, cap) — system://capture body:
 *                          liveness per kind, optional "kind" filter
 *   renderAiLookup(doc, argsRef, out, cap)     — provider row by "slug" or
 *                          substring "query" (cap 20 matches)
 *   renderDbLookup(doc, argsRef, out, cap)     — db source by "slug",
 *                          "engine", or "query"; all rows when bare
 *   renderAssetLookup(doc, argsRef, out, cap)  — asset source by "slug"
 *                          or "query"; all rows when bare
 *   renderAssetDownload(doc, argsRef, out, cap) — cache-confined download
 *                          plan for one "slug" (+ optional "file"): license
 *                          + attribution + cache path + chunked-copy terms;
 *                          answers the plan, never fetches (no network)
 *   renderEngineList(doc, argsRef, out, cap) — 20 engine rows (optional
 *                          substring "query"); all rows when bare
 *   renderHarnessRun(doc, argsRef, out, cap) — async spawn: "engine" +
 *                          "prompt" (+ optional "timeoutMs") into a seam
 *                          slot; answers job-id, never blocks stdio
 *   renderHarnessPoll(doc, argsRef, out, cap) — "jobId" status line
 *   renderAppList(doc, argsRef, out, cap)    — 29 app rows (optional
 *                          "query"); all rows when bare
 *   renderAppAction(doc, argsRef, out, cap)  — bounded action: "target" +
 *                          "action" (+ optional "params", "timeoutMs");
 *                          answers job-id + output, UNBOUND_DRIVER with
 *                          no driver, BUSY_FULL when slots fill
 *   renderAppPoll(doc, argsRef, out, cap)    — "jobId" stored verdict
 *   renderSearchList(doc, argsRef, out, cap) — 3 search backends
 *                          (optional "query"); all rows when bare
 *   renderWebSearch(doc, argsRef, out, cap)  — live query: "query" (+
 *                          optional "provider", "count", "base") via
 *                          Rest_get (5s bound); honest TLS/key/transport
 *                          errors, never scraped HTML
 *   copyJsonString(doc, ref, out, cap)       — string view copy into a
 *                          caller buffer (truncates past cap, NUL always)
 *   stripHtmlTags(dst, dcap, src)            — drop <...> spans in place
 *                          (MediaWiki snippets); always NUL-terminated
 *   encodeQueryComponent(src, out, cap)      — percent-encode for ?q=
 *                          (unreserved pass through, space ⇒ %20)
 *   searchFamilyName / searchAuthName(...)   — enum→label maps
 *   readNumberArg(doc, argsRef, key, outNum) — numeric arg; false when
 *                          absent/not-a-number (args=-1 ⇒ false)
 *   engineFamilyName / engineAuthName / appFamilyName / appAuthName(...) —
 *                          enum→label maps with fallbacks
 *   harnessStatusName / appStatusName(...) — slot verdict labels
 *   ensureSeam()                             — latch-init the file-static
 *                          Harness/AppBroker seam instances once
 *   findResource(uri)                          — resource table lookup
 * ============================================================================
  */
;;INTENTION("protocol boundary: list/poll/catalog reads touch registries only — no side effects; run/action enqueue bounded driver work async (job-id, never blocks stdio) with per-call timeoutMs — no unbounded waits, no in-process exec (Rule 17 driver seam: UNBOUND_DRIVER until an R3 host binds)")
;;INTENTION("no pagination (cursor): the full small tables ship in one page; capabilities advertise no listChanged/subscriptions")
;;INTENTION("id echo is a raw token slice of the client line (numbers and strings, quotes preserved); null/missing id ⇒ notification, no response — per JSON-RPC 2.0")
;;INTENTION("privacy wall documented in renderCaptureStatus: macOS has no public API for other apps' ScreenCaptureKit sessions — liveness is the honest contract")

// One tool handler: writes the plain-text tool body into out (cap Bytes).
// Return true = success (isError false), false = failure (isError true).
typedef bool (*McpToolFn)(const JsonDoc *doc, JsonRef args,
                          char *out, size_t cap);

typedef struct McpToolSlot {
    const char *name;
    const char *description;
    const char *inputSchema;
    McpToolFn handle;
} McpToolSlot;

typedef struct McpResourceSlot {
    const char *uri;
    const char *name;
    const char *mimeType;
    McpToolFn render;
} McpResourceSlot;

// --- static tool handler forward declarations ---
static bool renderAppDetect(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap);
static bool renderCaptureStatus(const JsonDoc *doc, JsonRef args,
                                char *out, size_t cap);
static bool renderAiLookup(const JsonDoc *doc, JsonRef args,
                           char *out, size_t cap);
static bool renderDbLookup(const JsonDoc *doc, JsonRef args,
                           char *out, size_t cap);
static bool renderEngineList(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap);
static bool renderHarnessRun(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap);
static bool renderHarnessPoll(const JsonDoc *doc, JsonRef args,
                              char *out, size_t cap);
static bool renderAppList(const JsonDoc *doc, JsonRef args,
                          char *out, size_t cap);
static bool renderAppAction(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap);
static bool renderAppPoll(const JsonDoc *doc, JsonRef args,
                          char *out, size_t cap);
static bool renderSearchList(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap);
static bool renderWebSearch(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap);
static bool renderAssetLookup(const JsonDoc *doc, JsonRef args,
                              char *out, size_t cap);
static bool renderAssetDownload(const JsonDoc *doc, JsonRef args,
                                char *out, size_t cap);

// HOSTED TOOLS — the connector surface exposed to AI clients.
static const McpToolSlot kMcpTools[] = {
    {
        "app_detect",
        "Known developer-tool presence: list the 14 known coding CLIs/agents "
        "(opencode, codex, claude, t3, cursor, hermes, nous, grok, gemini, "
        "aider, goose, cline, qwen-code, continue) with PATH/bundle/live-"
        "process state, or detail one by name.",
        "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\","
        "\"description\":\"exact known-app key to detail; omit to list all\"}},"
        "\"additionalProperties\":false}",
        renderAppDetect,
    },
    {
        "capture_status",
        "Screen/audio capture liveness right now: which known capture tools "
        "(OBS, QuickTime Player, macOS Screenshot, Screen Studio, CleanShot X, "
        "Kap, Loom, ScreenFlow, Camtasia, Filmage, Zoom, Voice Memos, "
        "Loopback, BlackHole, Soundflower) are running or installed, per kind. "
        "Privacy wall: other apps' capture sessions are invisible to macOS "
        "APIs — this reports process liveness, the honest contract.",
        "{\"type\":\"object\",\"properties\":{\"kind\":{\"type\":\"string\","
        "\"enum\":[\"screen\",\"stream\",\"audio\"],"
        "\"description\":\"filter by capture kind; omit for all\"}},"
        "\"additionalProperties\":false}",
        renderCaptureStatus,
    },
    {
        "ai_provider_lookup",
        "AI provider directory lookup across 260 providers: exact slug "
        "(openai, anthropic, zhipu-ai, nous-research, ...) with resolved base "
        "URL and auth scheme, or a substring query over names.",
        "{\"type\":\"object\",\"properties\":{\"slug\":{\"type\":\"string\","
        "\"description\":\"exact provider slug, e.g. openai\"},"
        "\"query\":{\"type\":\"string\",\"description\":\"case-insensitive "
        "substring over slug/display/note\"}},\"additionalProperties\":false}",
        renderAiLookup,
    },
    {
        "db_data_source_lookup",
        "Database data-source directory: 25 sources with complete driver "
        "support (PostgreSQL, MySQL, Oracle, ...). Lookup by slug, by driver "
        "engine key, or list all with ports and wire families.",
        "{\"type\":\"object\",\"properties\":{\"slug\":{\"type\":\"string\","
        "\"description\":\"exact data-source slug, e.g. postgresql\"},"
        "\"engine\":{\"type\":\"string\",\"description\":\"driver/engine key, "
        "e.g. postgres\"},\"query\":{\"type\":\"string\",\"description\":"
        "\"case-insensitive substring\"}},\"additionalProperties\":false}",
        renderDbLookup,
    },
    {
        "engine_list",
        "CLI coding-engine directory: 20 engines (claude-code, codex, "
        "opencode, aider, pi, goose, cursor-cli, hermes, ...) with CLI "
        "binary, transport family, and auth scheme. Optional substring "
        "query; bare call lists all.",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\","
        "\"description\":\"case-insensitive substring over "
        "slug/display/cli\"}},\"additionalProperties\":false}",
        renderEngineList,
    },
    {
        "harness_run",
        "Spawn one bounded engine job: engine slug + prompt (+ optional "
        "timeoutMs, default 100ms) into a 16-slot table. Answers a job-id "
        "immediately and never blocks stdio — poll it with harness_poll. "
        "UNBOUND_DRIVER until an R3 host binds a driver; BUSY_FULL when "
        "all 16 slots run.",
        "{\"type\":\"object\",\"properties\":{\"engine\":{\"type\":\"string\","
        "\"description\":\"exact engine slug, e.g. codex\"},"
        "\"prompt\":{\"type\":\"string\",\"description\":\"task text "
        "(truncated past 32KiB)\"},\"timeoutMs\":{\"type\":\"number\","
        "\"description\":\"per-job bound; omit for the seam default\"}},"
        "\"required\":[\"engine\",\"prompt\"],\"additionalProperties\":false}",
        renderHarnessRun,
    },
    {
        "harness_poll",
        "Job status check: numeric jobId from harness_run. Answers the "
        "slot verdict (RUNNING/DONE/TIMEOUT/IDLE) without blocking.",
        "{\"type\":\"object\",\"properties\":{\"jobId\":{\"type\":\"number\","
        "\"description\":\"job id answered by harness_run\"}},"
        "\"required\":[\"jobId\"],\"additionalProperties\":false}",
        renderHarnessPoll,
    },
    {
        "app_list",
        "App/automation directory: 29 targets (apple-notes, spotify-local, "
        "python, godot, telegram, slack, whatsapp, ...) with transport "
        "family, auth scheme, and bundle/CLI/URL. Optional substring "
        "query; bare call lists all.",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\","
        "\"description\":\"case-insensitive substring over "
        "slug/display/target\"}},\"additionalProperties\":false}",
        renderAppList,
    },
    {
        "app_action",
        "Run one bounded app action: target slug + action string (+ optional "
        "params JSON text, timeoutMs). Answers job-id + driver output. "
        "UNBOUND_DRIVER until an R3 host binds a driver; BUSY_FULL when "
        "all 16 slots run.",
        "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\","
        "\"description\":\"exact app slug, e.g. apple-notes\"},"
        "\"action\":{\"type\":\"string\",\"description\":\"action verb, "
        "e.g. list-notes\"},\"params\":{\"type\":\"string\","
        "\"description\":\"params JSON text; omit for {}\""
        "},\"timeoutMs\":{\"type\":\"number\",\"description\":\"per-action "
        "bound; omit for the seam default\"}},"
        "\"required\":[\"target\",\"action\"],\"additionalProperties\":false}",
        renderAppAction,
    },
    {
        "app_poll",
        "Action verdict check: numeric jobId from app_action. Answers the "
        "stored slot verdict (DONE/TIMEOUT/RUNNING/IDLE) without blocking.",
        "{\"type\":\"object\",\"properties\":{\"jobId\":{\"type\":\"number\","
        "\"description\":\"job id answered by app_action\"}},"
        "\"required\":[\"jobId\"],\"additionalProperties\":false}",
        renderAppPoll,
    },
    {
        "search_list",
        "Blessed web-search directory: 3 backends (searxng self-hosted, "
        "google-cse, wikimedia) with wire family and auth scheme. "
        "Optional substring query; bare call lists all. APIs only — "
        "scraping HTML to fake search is out of scope by design.",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\","
        "\"description\":\"case-insensitive substring over "
        "slug/display/endpoint\"}},\"additionalProperties\":false}",
        renderSearchList,
    },
    {
        "web_search",
        "Live web search over a blessed API: query text (+ optional "
        "provider slug defaulting to searxng, count 1-10 default 5, base "
        "URL override for self-hosted SearXNG). Bounded 5s fetch, answers "
        "title|url+snippet lines. Honest errors: missing keys, TLS-only "
        "providers before the TLS backend lands, unreachable hosts.",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\","
        "\"description\":\"search text\"},\"provider\":{\"type\":\"string\","
        "\"description\":\"exact backend slug, e.g. searxng\"},"
        "\"count\":{\"type\":\"number\",\"description\":\"max results 1-10\"},"
        "\"base\":{\"type\":\"string\",\"description\":\"SearXNG base URL "
        "override, e.g. http://localhost:8888\"}},"
        "\"required\":[\"query\"],\"additionalProperties\":false}",
        renderWebSearch,
    },
    {
        "asset_lookup",
        "External asset-source directory: 12 blessed sources (Unsplash, "
        "Pexels, Pixabay, Openverse, Wikimedia Commons, Sketchfab, "
        "Freesound, Poly Haven, AmbientCG, OpenGameArt) plus catalog-only "
        "rows (Kenney, Quaternius). Lookup by slug, substring query, or "
        "list all with licenses and sample URLs. Catalog only — no "
        "scraping, per Rule 34.",
        "{\"type\":\"object\",\"properties\":{\"slug\":{\"type\":\"string\","
        "\"description\":\"exact asset-source slug, e.g. unsplash\"},"
        "\"query\":{\"type\":\"string\",\"description\":\"case-insensitive "
        "substring\"}},\"additionalProperties\":false}",
        renderAssetLookup,
    },
    {
        "asset_download",
        "Cache-confined download plan for one asset source: slug "
        "(required) + file name (optional, defaults to <slug>-sample). "
        "Answers the license family, attribution line, cache path, and "
        "per-chunk 100ms copy terms — Bytes stream via AssetBroker "
        "chunked copies under VexHome_cache, never fetched here.",
        "{\"type\":\"object\",\"properties\":{\"slug\":{\"type\":\"string\","
        "\"description\":\"exact asset-source slug, e.g. poly-haven\"},"
        "\"file\":{\"type\":\"string\",\"description\":\"cache file name, "
        "e.g. fox.glb\"}},\"required\":[\"slug\"],"
        "\"additionalProperties\":false}",
        renderAssetDownload,
    },
};

// HOSTED RESOURCES — plain-text registry dumps under stable URIs.
static const McpResourceSlot kMcpResources[] = {
    {
        "system://apps",
        "Known developer tools (14 app registry rows)",
        "text/plain",
        renderAppDetect,
    },
    {
        "system://capture",
        "Capture tools and their live liveness",
        "text/plain",
        renderCaptureStatus,
    },
    {
        "db://data-sources",
        "Database data sources with complete driver support",
        "text/plain",
        renderDbLookup,
    },
    {
        "engines://catalog",
        "CLI coding engines (20 directory rows)",
        "text/plain",
        renderEngineList,
    },
    {
        "apps://catalog",
        "App/automation targets (29 directory rows)",
        "text/plain",
        renderAppList,
    },
    {
        "search://providers",
        "Blessed web-search backends (3 directory rows)",
        "text/plain",
        renderSearchList,
    },
    {
        "assets://catalog",
        "Blessed external asset sources (12 directory rows)",
        "text/plain",
        renderAssetLookup,
    },
};

static const size_t kMcpToolCount = sizeof(kMcpTools) / sizeof(kMcpTools[0]);
static const size_t kMcpResourceCount =
    sizeof(kMcpResources) / sizeof(kMcpResources[0]);

// --- bounded builders -------------------------------------------------------

/** Appends as much of s as fits and keeps the destination NUL-terminated. */
static void appendStr(char *buf, size_t cap, size_t *pos, const char *s) {
    if ((*pos) >= cap)
        return;
    size_t room = cap - (*pos) - 1;
    size_t take = strlen(s);
    if (take > room)
        take = room;
    memcpy(buf + (*pos), s, take);
    (*pos) += take;
    buf[*pos] = '\0';
}

/** Formats text at the current output position, clamping the cursor to capacity. */
static void appendFmt(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
    if ((*pos) >= cap)
        return;
    va_list ap;
    va_start(ap, fmt);
    int want = vsnprintf(buf + (*pos), cap - (*pos), fmt, ap);
    va_end(ap);
    if (want > 0) {
        (*pos) += (size_t)want;
        if ((*pos) > cap - 1)
            (*pos) = cap - 1;
    }
}

// --- enum → label maps ------------------------------------------------------

/** Maps an AI provider family enum to its protocol-facing label. */
static const char *familyName(AiProviderFamily f) {
    switch (f) {
        case AI_PROVIDER_FAMILY_OPENAI_COMPAT: return "openai-compat";
        case AI_PROVIDER_FAMILY_ANTHROPIC:     return "anthropic";
        case AI_PROVIDER_FAMILY_ROUTER:        return "router";
        case AI_PROVIDER_FAMILY_NATIVE:        return "native";
        case AI_PROVIDER_FAMILY_LOCAL:         return "local";
    }
    return "?";
}

/** Maps an AI provider auth enum to its protocol-facing label. */
static const char *authName(AiProviderAuth a) {
    switch (a) {
        case AI_PROVIDER_AUTH_BEARER:    return "bearer";
        case AI_PROVIDER_AUTH_API_KEY:   return "api-key-header";
        case AI_PROVIDER_AUTH_X_API_KEY: return "x-api-key";
        case AI_PROVIDER_AUTH_NONE:      return "none";
        case AI_PROVIDER_AUTH_SPECIAL:   return "special";
    }
    return "?";
}

/** Maps an AI provider region enum to its display label. */
static const char *regionName(AiProviderRegion r) {
    switch (r) {
        case AI_PROVIDER_REGION_GLOBAL: return "global";
        case AI_PROVIDER_REGION_CHINA:  return "china";
        case AI_PROVIDER_REGION_EUROPE: return "europe";
        case AI_PROVIDER_REGION_OTHER:  return "other";
    }
    return "?";
}

/** Maps a database provider family enum to its display label. */
static const char *dbFamilyName(DbProviderFamily f) {
    switch (f) {
        case DB_PROVIDER_FAMILY_SQL:        return "sql";
        case DB_PROVIDER_FAMILY_SQL_COMPAT: return "sql-compat";
        case DB_PROVIDER_FAMILY_NO_SQL:     return "nosql";
        case DB_PROVIDER_FAMILY_EMBEDDED:   return "embedded";
    }
    return "?";
}

/** Maps a capture kind enum to its display label. */
static const char *kindName(CaptureKind kind) {
    switch (kind) {
        case CAPTURE_KIND_SCREEN: return "screen";
        case CAPTURE_KIND_STREAM: return "stream";
        case CAPTURE_KIND_AUDIO:  return "audio";
    }
    return "?";
}

/** Maps a CLI engine family enum to its display label. */
static const char *engineFamilyName(EngineProviderFamily f) {
    switch (f) {
        case ENGINE_PROVIDER_FAMILY_CLI:    return "cli";
        case ENGINE_PROVIDER_FAMILY_LOCAL:  return "local";
        case ENGINE_PROVIDER_FAMILY_REMOTE: return "remote";
    }
    return "?";
}

/** Maps an engine authentication enum to its display label. */
static const char *engineAuthName(EngineProviderAuth a) {
    switch (a) {
        case ENGINE_PROVIDER_AUTH_NONE:    return "none";
        case ENGINE_PROVIDER_AUTH_API_KEY: return "api-key";
        case ENGINE_PROVIDER_AUTH_OAUTH:   return "oauth";
        case ENGINE_PROVIDER_AUTH_SYSTEM:  return "system";
    }
    return "?";
}

/** Maps an app automation family enum to its display label. */
static const char *appFamilyName(AppProviderFamily f) {
    switch (f) {
        case APP_PROVIDER_FAMILY_OSA_SCRIPT:    return "osa-script";
        case APP_PROVIDER_FAMILY_SHORTCUTS_CLI: return "shortcuts-cli";
        case APP_PROVIDER_FAMILY_MUSIC_LOCAL:   return "music-local";
        case APP_PROVIDER_FAMILY_REST_WEBHOOK:  return "rest-webhook";
        case APP_PROVIDER_FAMILY_LOCAL_SOCKET:  return "local-socket";
        case APP_PROVIDER_FAMILY_NATIVE_CLI:    return "native-cli";
    }
    return "?";
}

/** Maps an app authentication enum to its display label. */
static const char *appAuthName(AppProviderAuth a) {
    switch (a) {
        case APP_PROVIDER_AUTH_NONE:   return "none";
        case APP_PROVIDER_AUTH_SYSTEM: return "system";
        case APP_PROVIDER_AUTH_TOKEN:  return "token";
    }
    return "?";
}

/** Maps a harness job status enum to its response label. */
static const char *harnessStatusName(HarnessStatus s) {
    switch (s) {
        case HARNESS_STATUS_IDLE:      return "IDLE";
        case HARNESS_STATUS_RUNNING:   return "RUNNING";
        case HARNESS_STATUS_DONE:      return "DONE";
        case HARNESS_STATUS_TIMEOUT:   return "TIMEOUT";
        case HARNESS_STATUS_BUSY_FULL: return "BUSY_FULL";
    }
    return "?";
}

/** Maps an app action status enum to its response label. */
static const char *appStatusName(AppStatus s) {
    switch (s) {
        case APP_STATUS_IDLE:      return "IDLE";
        case APP_STATUS_RUNNING:   return "RUNNING";
        case APP_STATUS_DONE:      return "DONE";
        case APP_STATUS_TIMEOUT:   return "TIMEOUT";
        case APP_STATUS_BUSY_FULL: return "BUSY_FULL";
    }
    return "?";
}

/** Maps a search provider family enum to its display label. */
static const char *searchFamilyName(SearchProviderFamily f) {
    switch (f) {
        case SEARCH_FAMILY_SEARXNG:    return "searxng";
        case SEARCH_FAMILY_GOOGLE_CSE: return "google-cse";
        case SEARCH_FAMILY_MEDIAWIKI:  return "mediawiki";
    }
    return "?";
}

/** Maps a search authentication enum to its display label. */
static const char *searchAuthName(SearchProviderAuth a) {
    switch (a) {
        case SEARCH_AUTH_NONE:   return "none";
        case SEARCH_AUTH_KEY_CX: return "key-cx-env";
    }
    return "?";
}

// Copy a JSON string view into a caller buffer (NUL always, silent
// truncation past cap). False on non-string refs or empty caps.
/** Copies a JSON string node to out, truncating at capacity and terminating it. */
static bool copyJsonString(const JsonDoc *doc, JsonRef ref,
                           char *out, size_t cap) {
    if (!out || cap == 0)
        return false;
    out[0] = '\0';
    if (ref < 0 || Json_type(doc, ref) != JSON_STRING)
        return false;
    uint32_t len = 0;
    const char *view = Json_string(doc, ref, &len);
    if ((size_t)len >= cap)
        len = (uint32_t)(cap - 1);
    memcpy(out, view, len);
    out[len] = '\0';
    return true;
}

// Drop <...> spans in place (MediaWiki search snippets ship HTML).
// Always NUL-terminated; never grows the text.
/** Copies source text while removing spans enclosed by angle brackets. */
static void stripHtmlTags(char *dst, size_t dcap, const char *src) {
    if (!dst || dcap == 0)
        return;
    size_t o = 0;
    bool inTag = false;
    for (const char *p = src; *p && o + 1 < dcap; p++) {
        if ((*p) == '<') {
            inTag = true;
            continue;
        }
        if ((*p) == '>') {
            inTag = false;
            continue;
        }
        if (!inTag)
            dst[o++] = *p;
    }
    dst[o] = '\0';
}

// Percent-encode one query component: unreserved Bytes pass through,
// everything else becomes %XX (space ⇒ %20, never +). False when out
// is too small — the caller degrades instead of sending a corrupt URL.
/** Percent-encodes a URL query component into a bounded output buffer. */
static bool encodeQueryComponent(const char *src, char *out, size_t cap) {
    static const char *hex = "0123456789ABCDEF";
    if (!src || !out || cap == 0)
        return false;
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char c = *p;
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (unreserved) {
            if (o + 1 >= cap)
                return false;
            out[o++] = (char)c;
        } else {
            if (o + 3 >= cap)
                return false;
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 15];
            out[o++] = hex[c & 15];
        }
    }
    if (o >= cap)
        return false;
    out[o] = '\0';
    return true;
}

// --- harness/app seam (file-static instances behind the bind seam) --------

static Harness sHarnessJobs;
static AppBroker sAppJobs;
static bool sSeamReady;

/** Lazily initializes the file-static harness and app broker instances once. */
static void ensureSeam(void) {
    if (sSeamReady)
        return;
    sHarnessJobs = Harness();
    sAppJobs = AppBroker();
    sSeamReady = true;
}

/** Binds the host-provided driver context and callbacks for harness jobs. */
void McpServer_bindHarnessDriver(void *driverCtx, HarnessDriverTable table) {
    ensureSeam();
    Harness_setDriver(&sHarnessJobs, driverCtx, table);
}

/** Binds the host-provided driver context and callbacks for app actions. */
void McpServer_bindAppDriver(void *driverCtx, AppDriverTable table) {
    ensureSeam();
    AppBroker_setDriver(&sAppJobs, driverCtx, table);
}

// --- string helpers ---------------------------------------------------------

/** Tests for an ASCII case-insensitive substring match. */
static bool containsFold(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    const size_t nl = strlen(needle);
    if (nl == 0)
        return true;
    const size_t hl = strlen(haystack);
    if (nl > hl)
        return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        while (j < nl) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z')
                b = (char)(b + 32);
            if (a != b)
                break;
            j++;
        }
        if (j == nl)
            return true;
    }
    return false;
}

/** Escapes JSON string characters into the supplied output buffer. */
static void escapeJsonText(const char *src, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = src; *p && o + 6 < cap; p++) {
        char c = *p;
        switch (c) {
            case '"':  appendStr(out, cap, &o, "\\\""); break;
            case '\\': appendStr(out, cap, &o, "\\\\"); break;
            case '\n': appendStr(out, cap, &o, "\\n"); break;
            case '\r': appendStr(out, cap, &o, "\\r"); break;
            case '\t': appendStr(out, cap, &o, "\\t"); break;
            default:
                out[o++] = c;
                out[o] = '\0';
        }
    }
    out[o] = '\0';
}

/** Compares a parsed JSON string method value with an exact method name. */
static bool methodIs(const JsonDoc *doc, JsonRef method, const char *want) {
    uint32_t len = 0;
    const char *s = Json_string(doc, method, &len);
    if (!s)
        return false;
    const size_t wl = strlen(want);
    return (size_t)len == wl && memcmp(s, want, wl) == 0;
}

// Copy one numeric argument from an args subobject. Returns false when
// the member is absent or not a number (outNum untouched; args < 0 ⇒ false).
/** Reads a numeric member from an argument object without changing output on failure. */
static bool readNumberArg(const JsonDoc *doc, JsonRef args,
                          const char *key, double *outNum) {
    if (args < 0 || !outNum)
        return false;
    JsonRef v = Json_member(doc, args, key);
    if (v < 0 || Json_type(doc, v) != JSON_NUMBER)
        return false;
    (*outNum) = Json_number(doc, v);
    return true;
}

// Copy one string argument from an args subobject. Returns false when the
// member is absent or not a string (out stays ""). Safe for args < 0.
/** Copies a string member from an argument object into a bounded buffer. */
static bool readStringArg(const JsonDoc *doc, JsonRef args,
                          const char *key, char *out, size_t cap) {
    out[0] = '\0';
    if (args < 0 || !out || cap == 0)
        return false;
    JsonRef v = Json_member(doc, args, key);
    if (v < 0)
        return false;
    uint32_t len = 0;
    const char *s = Json_string(doc, v, &len);
    if (!s)
        return false;
    if (len >= cap)
        len = (uint32_t)(cap - 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return true;
}

// Raw "id" token extraction from the client line — echoed verbatim per
// JSON-RPC 2.0 (a quoted string id keeps its quotes; a bare numeric id or
// "null" literal is copied as-is). Returns false when absent so the caller
// treats the message as a notification.
/** Copies the JSON-RPC id token verbatim for echoing in the response. */
static bool rawIdExtract(const char *line, size_t lineLen,
                         char *out, size_t cap) {
    if (!line || !out || cap == 0)
        return false;
    out[0] = '\0';
    const char *end = line + lineLen;
    const char *p = line;
    bool found = false;
    while (p + 4 <= end) {
        // the exact 4-byte token "id"
        if (p[0] == '"' && p[1] == 'i' && p[2] == 'd' && p[3] == '"') {
            found = true;
            break;
        }
        p++;
    }
    if (!found)
        return false;
    p += 4;
    while (p < end && ((*p) == ' ' || (*p) == '\t'))
        p++;
    if (p < end && (*p) == ':')
        p++;
    while (p < end && ((*p) == ' ' || (*p) == '\t'))
        p++;
    if (p >= end)
        return false;
    size_t o = 0;
    if ((*p) == '"') {
        // quoted string id — copy through the closing unescaped quote
        if (o + 1 < cap)
            out[o++] = (*p); // opening quote
        p++;
        while (p < end && o + 1 < cap) {
            char c = *p;
            if (c == '\\' && p + 1 < end) {
                if (o + 2 < cap) {
                    out[o++] = *p;
                    out[o++] = *(p + 1);
                }
                p += 2;
                continue;
            }
            if (c == '"') {
                if (o + 1 < cap)
                    out[o++] = c; // closing quote
                out[o] = '\0';
                return true;
            }
            if (o + 1 < cap)
                out[o++] = c;
            p++;
        }
        return false;
    }
    // number or null literal
    while (p < end && o + 1 < cap) {
        char c = *p;
        if (c == ',' || c == '}' || c == ' ' || c == '\t' || c == '\n')
            break;
        out[o++] = c;
        p++;
    }
    out[o] = '\0';
    return o > 0;
}

// --- error envelope ---------------------------------------------------------

/** Writes a JSON-RPC error response using the supplied code, message, and id. */
static void respondError(int code, const char *message, const char *id,
                         char *out, size_t cap) {
    size_t pos = 0;
    appendStr(out, cap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
    appendStr(out, cap, &pos, id && (*id) != '\0' ? id : "null");
    appendFmt(out, cap, &pos, ",\"error\":{\"code\":%d,\"message\":\"%s\"}}",
              code, message);
}

// --- plain responses --------------------------------------------------------

/** Writes the empty successful JSON-RPC response used for ping. */
static void respondPing(const char *id, char *out, size_t cap) {
    size_t pos = 0;
    appendStr(out, cap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
    appendStr(out, cap, &pos, id);
    appendStr(out, cap, &pos, ",\"result\":{}}");
}

/** Serializes the static hosted-tool registry as a JSON-RPC result. */
static void respondToolsList(const char *id, char *out, size_t cap) {
    size_t pos = 0;
    appendStr(out, cap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
    appendStr(out, cap, &pos, id);
    appendStr(out, cap, &pos, ",\"result\":{\"tools\":[");
    for (size_t i = 0; i < kMcpToolCount; i++) {
        if (i > 0)
            appendStr(out, cap, &pos, ",");
        appendStr(out, cap, &pos, "{\"name\":\"");
        appendStr(out, cap, &pos, kMcpTools[i].name);
        appendStr(out, cap, &pos, "\",\"description\":\"");
        appendStr(out, cap, &pos, kMcpTools[i].description);
        appendStr(out, cap, &pos, "\",\"inputSchema\":");
        appendStr(out, cap, &pos, kMcpTools[i].inputSchema);
        appendStr(out, cap, &pos, "}");
    }
    appendStr(out, cap, &pos, "]}}");
}

/** Serializes the static resource registry as a JSON-RPC result. */
static void respondResourcesList(const char *id, char *out, size_t cap) {
    size_t pos = 0;
    appendStr(out, cap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
    appendStr(out, cap, &pos, id);
    appendStr(out, cap, &pos, ",\"result\":{\"resources\":[");
    for (size_t i = 0; i < kMcpResourceCount; i++) {
        if (i > 0)
            appendStr(out, cap, &pos, ",");
        appendStr(out, cap, &pos, "{\"uri\":\"");
        appendStr(out, cap, &pos, kMcpResources[i].uri);
        appendStr(out, cap, &pos, "\",\"name\":\"");
        appendStr(out, cap, &pos, kMcpResources[i].name);
        appendStr(out, cap, &pos, "\",\"mimeType\":\"");
        appendStr(out, cap, &pos, kMcpResources[i].mimeType);
        appendStr(out, cap, &pos, "\"}");
    }
    appendStr(out, cap, &pos, "]}}");
}

/** Negotiates a supported MCP version and emits the initialize response. */
static bool respondInitializeReal(McpServer *self, const JsonDoc *doc,
                                  const char *id, char *out, size_t cap) {
    // negotiate: echo a supported client version, else our latest
    JsonRef params = Json_member(doc, Json_root(doc), "params");
    JsonRef pv = params >= 0 ? Json_member(doc, params, "protocolVersion") : -1;
    const char *chosen = MCP_PROTOCOL_LATEST;
    if (pv >= 0) {
        uint32_t len = 0;
        const char *s = Json_string(doc, pv, &len);
        if (s) {
            char buf[40];
            if (len >= sizeof(buf))
                len = (uint32_t)(sizeof(buf) - 1);
            memcpy(buf, s, len);
            buf[len] = '\0';
            if (strcmp(buf, "2024-11-05") == 0)
                chosen = "2024-11-05";
            else if (strcmp(buf, "2025-03-26") == 0)
                chosen = "2025-03-26";
            else if (strcmp(buf, "2025-06-18") == 0)
                chosen = "2025-06-18";
        }
    }
    snprintf((*self).protocolVersion, sizeof((*self).protocolVersion), "%s",
             chosen);
    (*self).initialized = 1;
    size_t pos = 0;
    appendStr(out, cap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
    appendStr(out, cap, &pos, id);
    appendFmt(out, cap, &pos,
              ",\"result\":{\"protocolVersion\":\"%s\","
              "\"capabilities\":{\"tools\":{},\"resources\":{}},"
              "\"serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"}}}",
              (*self).protocolVersion, (*self).name, (*self).version);
    return true;
}

// --- tool renderers (plain text bodies) -------------------------------------

/** Renders known application presence, optionally for one requested app. */
static bool renderAppDetect(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap) {
    size_t pos = 0;
    AppDetect *detect = AppDetect_shared();
    char name[128];
    const bool haveName = readStringArg(doc, args, "name", name, sizeof(name));
    if (haveName && (*name) != '\0') {
        // single-app detail
        const AppSlot *slot = AppDetect_get(detect, name);
        if (!slot) {
            appendFmt(out, cap, &pos, "unknown app: %s", name);
            return false;
        }
        const char *nm = AppDetect_getName(detect, slot);
        appendFmt(out, cap, &pos, "app: %s\n", nm);
        appendFmt(out, cap, &pos, "  display: %s\n",
                  AppDetect_getDisplayName(detect, slot));
        appendFmt(out, cap, &pos, "  onPath: %d\n",
                  AppDetect_isOnPath(detect, nm) ? 1 : 0);
        appendFmt(out, cap, &pos, "  appBundle: %d\n",
                  AppDetect_isAppBundle(detect, nm) ? 1 : 0);
        appendFmt(out, cap, &pos, "  running: %d\n",
                  AppDetect_isRunning(detect, slot) ? 1 : 0);
        const char *note = AppDetect_getNote(detect, slot);
        if (note)
            appendFmt(out, cap, &pos, "  note: %s\n", note);
        return true;
    }
    const uint32_t total = AppDetect_count(detect);
    appendFmt(out, cap, &pos, "KNOWN APPS (%u) — onPath | appBundle | running\n",
              total);
    for (uint32_t i = 0; i < total; i++) {
        const AppSlot *slot = AppDetect_at(detect, i);
        const char *nm = AppDetect_getName(detect, slot);
        appendFmt(out, cap, &pos, "- %s | %s | onPath=%d bundle=%d running=%d\n",
                  nm, AppDetect_getDisplayName(detect, slot),
                  AppDetect_isOnPath(detect, nm) ? 1 : 0,
                  AppDetect_isAppBundle(detect, nm) ? 1 : 0,
                  AppDetect_isRunning(detect, slot) ? 1 : 0);
    }
    return true;
}

/** Renders capture-tool process liveness, optionally filtered by capture kind. */
static bool renderCaptureStatus(const JsonDoc *doc, JsonRef args,
                                char *out, size_t cap) {
    size_t pos = 0;
    CaptureTool *tools = CaptureTool_shared();
    char kind[32];
    const bool haveKind = readStringArg(doc, args, "kind", kind, sizeof(kind));
    const bool wantScreen = !haveKind || strcmp(kind, "screen") == 0;
    const bool wantStream = !haveKind || strcmp(kind, "stream") == 0;
    const bool wantAudio = !haveKind || strcmp(kind, "audio") == 0;
    appendStr(out, cap, &pos,
              "CAPTURE STATUS — known capture tools alive now\n"
              "(privacy wall: other apps' screen-capture sessions are not\n"
              "visible to macOS APIs; liveness is the honest contract)\n");
    const uint32_t total = CaptureTool_count(tools);
    const char *sections[3] = {"SCREEN:", "STREAM:", "AUDIO:"};
    const bool wants[3] = {wantScreen, wantStream, wantAudio};
    const CaptureKind kinds[3] = {CAPTURE_KIND_SCREEN, CAPTURE_KIND_STREAM,
                                  CAPTURE_KIND_AUDIO};
    for (uint32_t s = 0; s < 3; s++) {
        if (!wants[s])
            continue;
        appendFmt(out, cap, &pos, "%s\n", sections[s]);
        for (uint32_t i = 0; i < total; i++) {
            const CaptureSlot *slot = CaptureTool_at(tools, i);
            if (CaptureTool_getKind(tools, slot) != kinds[s])
                continue;
            const bool isDriver = CaptureTool_getProcKey(tools, slot) == nullptr;
            appendFmt(out, cap, &pos, "- %s | %s | running=%d installed=%d%s\n",
                      CaptureTool_getSlug(tools, slot),
                      CaptureTool_getDisplayName(tools, slot),
                      CaptureTool_isRunning(tools, slot) ? 1 : 0,
                      CaptureTool_isInstalled(tools, slot) ? 1 : 0,
                      isDriver ? " (driver)" : "");
        }
        appendFmt(out, cap, &pos, "running %s: %u\n",
                  kindName(kinds[s]), CaptureTool_countRunning(tools, kinds[s]));
    }
    appendFmt(out, cap, &pos,
              "SUMMARY: running all=%u screen=%u stream=%u audio=%u\n",
              CaptureTool_countRunningAll(tools),
              CaptureTool_countRunning(tools, CAPTURE_KIND_SCREEN),
              CaptureTool_countRunning(tools, CAPTURE_KIND_STREAM),
              CaptureTool_countRunning(tools, CAPTURE_KIND_AUDIO));
    return true;
}

/** Renders an exact AI-provider lookup or a bounded substring result list. */
static bool renderAiLookup(const JsonDoc *doc, JsonRef args,
                           char *out, size_t cap) {
    size_t pos = 0;
    AiProvider *dir = AiProvider_shared();
    char slug[128];
    char query[128];
    const bool haveSlug = readStringArg(doc, args, "slug", slug, sizeof(slug));
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));

    if (haveSlug && (*slug) != '\0') {
        const AiProviderSlot *slot = AiProvider_get(dir, slug);
        if (!slot) {
            appendFmt(out, cap, &pos, "unknown provider: %s", slug);
            return false;
        }
        appendFmt(out, cap, &pos, "provider: %s\n",
                  AiProvider_getSlug(dir, slot));
        appendFmt(out, cap, &pos, "  display: %s\n",
                  AiProvider_getDisplayName(dir, slot));
        appendFmt(out, cap, &pos, "  family: %s\n",
                  familyName(AiProvider_getFamily(dir, slot)));
        appendFmt(out, cap, &pos, "  auth: %s\n",
                  authName(AiProvider_getAuth(dir, slot)));
        appendFmt(out, cap, &pos, "  region: %s\n",
                  regionName(AiProvider_getRegion(dir, slot)));
        const char *base = AiProvider_getBaseUrl(dir, slot);
        appendFmt(out, cap, &pos, "  base: %s\n",
                  base ? base : "(null — family default)");
        const char *resolved = AiProvider_resolveBaseUrl(dir, slot);
        appendFmt(out, cap, &pos, "  resolved: %s\n",
                  resolved ? resolved : "?");
        const char *note = AiProvider_getNote(dir, slot);
        if (note)
            appendFmt(out, cap, &pos, "  note: %s\n", note);
        return true;
    }

    if (!haveQuery || (*query) == '\0') {
        appendStr(out, cap, &pos,
                  "pass slug=... (exact provider key) or query=... (substring); "
                  "examples: openai, anthropic, zhipu-ai, nous-research");
        return false;
    }

    // substring scan (cap 20 matches, counted)
    const uint32_t total = AiProvider_count(dir);
    uint32_t shown = 0;
    uint32_t hits = 0;
    for (uint32_t i = 0; i < total; i++) {
        const AiProviderSlot *slot = AiProvider_at(dir, i);
        const char *sl = AiProvider_getSlug(dir, slot);
        if (!containsFold(sl, query)) {
            const char *display = AiProvider_getDisplayName(dir, slot);
            const char *note = AiProvider_getNote(dir, slot);
            if (!containsFold(display, query) && !containsFold(note, query))
                continue;
        }
        hits++;
        if (shown < 20) {
            const char *resolved = AiProvider_resolveBaseUrl(dir, slot);
            appendFmt(out, cap, &pos, "- %s | %s | %s | %s | %s\n",
                      sl, AiProvider_getDisplayName(dir, slot),
                      familyName(AiProvider_getFamily(dir, slot)),
                      authName(AiProvider_getAuth(dir, slot)),
                      resolved != nullptr ? resolved : "?");
            shown++;
        }
    }
    appendFmt(out, cap, &pos, "matches: %u (showing %u)\n", hits, shown);
    return true;
}

/** Renders database-source lookup results by slug, engine, or substring. */
static bool renderDbLookup(const JsonDoc *doc, JsonRef args,
                           char *out, size_t cap) {
    size_t pos = 0;
    DbProvider *dir = DbProvider_shared();
    char slug[128];
    char engine[128];
    char query[128];
    const bool haveSlug = readStringArg(doc, args, "slug", slug, sizeof(slug));
    const bool haveEngine = readStringArg(doc, args, "engine", engine,
                                          sizeof(engine));
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));

    if (haveSlug && (*slug) != '\0') {
        const DbProviderSlot *slot = DbProvider_get(dir, slug);
        if (!slot) {
            appendFmt(out, cap, &pos, "unknown data source: %s", slug);
            return false;
        }
        appendFmt(out, cap, &pos, "data-source: %s\n",
                  DbProvider_getSlug(dir, slot));
        appendFmt(out, cap, &pos, "  display: %s\n",
                  DbProvider_getDisplayName(dir, slot));
        appendFmt(out, cap, &pos, "  engine: %s\n",
                  DbProvider_getEngine(dir, slot));
        appendFmt(out, cap, &pos, "  port: %u\n",
                  DbProvider_getDefaultPort(dir, slot));
        appendFmt(out, cap, &pos, "  family: %s\n",
                  dbFamilyName(DbProvider_getFamily(dir, slot)));
        const char *note = DbProvider_getNote(dir, slot);
        if (note)
            appendFmt(out, cap, &pos, "  note: %s\n", note);
        return true;
    }

    if (haveEngine && (*engine) != '\0') {
        const DbProviderSlot *slot = DbProvider_findByEngine(dir, engine);
        if (!slot) {
            appendFmt(out, cap, &pos, "unknown engine: %s", engine);
            return false;
        }
        appendFmt(out, cap, &pos, "- %s | %s | engine=%s | port=%u | %s\n",
                  DbProvider_getSlug(dir, slot),
                  DbProvider_getDisplayName(dir, slot),
                  DbProvider_getEngine(dir, slot),
                  DbProvider_getDefaultPort(dir, slot),
                  dbFamilyName(DbProvider_getFamily(dir, slot)));
        return true;
    }

    // bare (or query-filtered) listing of the full 25-row table
    const uint32_t total = DbProvider_count(dir);
    for (uint32_t i = 0; i < total; i++) {
        const DbProviderSlot *slot = DbProvider_at(dir, i);
        if (haveQuery && (*query) != '\0') {
            const char *sl = DbProvider_getSlug(dir, slot);
            const char *display = DbProvider_getDisplayName(dir, slot);
            const char *note = DbProvider_getNote(dir, slot);
            if (!containsFold(sl, query) && !containsFold(display, query) &&
                !containsFold(note, query))
                continue;
        }
        appendFmt(out, cap, &pos, "- %s | %s | engine=%s | port=%u | %s\n",
                  DbProvider_getSlug(dir, slot),
                  DbProvider_getDisplayName(dir, slot),
                  DbProvider_getEngine(dir, slot),
                  DbProvider_getDefaultPort(dir, slot),
                  dbFamilyName(DbProvider_getFamily(dir, slot)));
    }
    return true;
}

/** Renders the engine catalog, optionally filtered by a substring query. */
static bool renderEngineList(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap) {
    size_t pos = 0;
    EngineProvider *dir = EngineProvider_shared();
    char query[128];
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));
    const uint32_t total = EngineProvider_count(dir);
    appendFmt(out, cap, &pos, "ENGINES (%u) — slug | display | cli | family | auth\n",
              total);
    for (uint32_t i = 0; i < total; i++) {
        const EngineProviderSlot *slot = EngineProvider_at(dir, i);
        if (haveQuery && (*query) != '\0') {
            const char *sl = EngineProvider_getSlug(dir, slot);
            const char *display = EngineProvider_getDisplayName(dir, slot);
            const char *cli = EngineProvider_getCliName(dir, slot);
            if (!containsFold(sl, query) && !containsFold(display, query) &&
                !containsFold(cli, query))
                continue;
        }
        appendFmt(out, cap, &pos, "- %s | %s | %s | %s | %s\n",
                  EngineProvider_getSlug(dir, slot),
                  EngineProvider_getDisplayName(dir, slot),
                  EngineProvider_getCliName(dir, slot),
                  engineFamilyName(EngineProvider_getFamily(dir, slot)),
                  engineAuthName(EngineProvider_getAuth(dir, slot)));
    }
    return true;
}

/** Admits a bounded harness job and renders its id or an observable rejection. */
static bool renderHarnessRun(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap) {
    size_t pos = 0;
    ensureSeam();
    EngineProvider *dir = EngineProvider_shared();
    char slug[128];
    static char prompt[32768];
    double timeoutNum = 0;
    const bool haveTimeout = readNumberArg(doc, args, "timeoutMs", &timeoutNum);
    if (!readStringArg(doc, args, "engine", slug, sizeof(slug)) ||
        (*slug) == '\0') {
        appendStr(out, cap, &pos, "missing engine: pass an engine slug, e.g. codex");
        return false;
    }
    const EngineProviderSlot *slot = EngineProvider_get(dir, slug);
    if (!slot) {
        appendFmt(out, cap, &pos, "unknown engine: %s", slug);
        return false;
    }
    if (!readStringArg(doc, args, "prompt", prompt, sizeof(prompt)) ||
        (*prompt) == '\0') {
        appendStr(out, cap, &pos, "missing prompt: pass task text");
        return false;
    }
    Harness_setEngine(&sHarnessJobs, slot);
    uint64_t bound = 0;
    if (haveTimeout && timeoutNum > 0)
        bound = (uint64_t)timeoutNum;
    uint32_t jobId = 0;
    if (!Harness_run(&sHarnessJobs, prompt, strlen(prompt), bound, &jobId)) {
        HarnessStatus verdict = Harness_getLastStatus(&sHarnessJobs);
        if (verdict == HARNESS_STATUS_BUSY_FULL) {
            appendStr(out, cap, &pos,
                      "BUSY_FULL: all 16 harness slots RUNNING — "
                      "poll an old job, then retry");
            return false;
        }
        appendFmt(out, cap, &pos,
                  "UNBOUND_DRIVER: engine '%s' (cli '%s') resolved, but no "
                  "Harness driver is bound in this process — exec lives in "
                  "vexspoke/R3 (McpServer_bindHarnessDriver); nothing spawned",
                  EngineProvider_getSlug(dir, slot),
                  EngineProvider_getCliName(dir, slot));
        return false;
    }
    appendFmt(out, cap, &pos,
              "job %u RUNNING engine=%s cli=%s — poll with harness_poll",
              jobId, EngineProvider_getSlug(dir, slot),
              EngineProvider_getCliName(dir, slot));
    return true;
}

/** Renders the stored status for a previously issued harness job id. */
static bool renderHarnessPoll(const JsonDoc *doc, JsonRef args,
                              char *out, size_t cap) {
    size_t pos = 0;
    ensureSeam();
    double idNum = 0;
    if (!readNumberArg(doc, args, "jobId", &idNum) || idNum <= 0 ||
        idNum > HARNESS_MAX_JOBS) {
        appendStr(out, cap, &pos, "unknown job: pass a jobId from harness_run");
        return false;
    }
    uint32_t jobId = (uint32_t)idNum;
    HarnessStatus st = Harness_poll(&sHarnessJobs, jobId);
    const HarnessJob *job = nullptr;
    if (jobId >= 1) {
        uint32_t count = Harness_getJobCount(&sHarnessJobs);
        for (uint32_t i = 0; i < count; i++) {
            const HarnessJob *cand = Harness_getJobAt(&sHarnessJobs, i);
            if (cand && (*cand).jobId == jobId) {
                job = cand;
                break;
            }
        }
    }
    if (!job) {
        appendFmt(out, cap, &pos, "unknown job: %u", jobId);
        return false;
    }
    EngineProvider *dir = EngineProvider_shared();
    const char *engineSlug = (*job).engine
        ? EngineProvider_getSlug(dir, (*job).engine) : "?";
    appendFmt(out, cap, &pos, "job %u: %s engine=%s", jobId,
              harnessStatusName(st), engineSlug);
    return true;
}

/** Renders the app automation catalog, optionally filtered by query. */
static bool renderAppList(const JsonDoc *doc, JsonRef args,
                          char *out, size_t cap) {
    size_t pos = 0;
    AppProvider *dir = AppProvider_shared();
    char query[128];
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));
    const uint32_t total = AppProvider_count(dir);
    appendFmt(out, cap, &pos, "APPS (%u) — slug | display | family | auth | target\n",
              total);
    for (uint32_t i = 0; i < total; i++) {
        const AppProviderSlot *slot = AppProvider_at(dir, i);
        if (haveQuery && (*query) != '\0') {
            const char *sl = AppProvider_getSlug(dir, slot);
            const char *display = AppProvider_getDisplayName(dir, slot);
            const char *target = AppProvider_getBundleIdOrScheme(dir, slot);
            if (!containsFold(sl, query) && !containsFold(display, query) &&
                !containsFold(target, query))
                continue;
        }
        appendFmt(out, cap, &pos, "- %s | %s | %s | %s | %s\n",
                  AppProvider_getSlug(dir, slot),
                  AppProvider_getDisplayName(dir, slot),
                  appFamilyName(AppProvider_getFamily(dir, slot)),
                  appAuthName(AppProvider_getAuth(dir, slot)),
                  AppProvider_getBundleIdOrScheme(dir, slot));
    }
    return true;
}

/** Runs a bounded app action and renders its result, output, or rejection. */
static bool renderAppAction(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap) {
    size_t pos = 0;
    ensureSeam();
    AppProvider *dir = AppProvider_shared();
    char slug[128];
    char action[128];
    static char params[16384];
    static char actionOut[16384];
    double timeoutNum = 0;
    const bool haveTimeout = readNumberArg(doc, args, "timeoutMs", &timeoutNum);
    if (!readStringArg(doc, args, "target", slug, sizeof(slug)) ||
        (*slug) == '\0') {
        appendStr(out, cap, &pos,
                  "missing target: pass an app slug, e.g. apple-notes");
        return false;
    }
    const AppProviderSlot *slot = AppProvider_get(dir, slug);
    if (!slot) {
        appendFmt(out, cap, &pos, "unknown target: %s", slug);
        return false;
    }
    if (!readStringArg(doc, args, "action", action, sizeof(action)) ||
        (*action) == '\0') {
        appendStr(out, cap, &pos, "missing action: pass an action verb");
        return false;
    }
    params[0] = '\0';
    readStringArg(doc, args, "params", params, sizeof(params));
    const char *paramsText = (*params) != '\0' ? params : "{}";
    AppBroker_setTarget(&sAppJobs, slot);
    if (haveTimeout && timeoutNum > 0)
        AppBroker_setTimeout(&sAppJobs, (uint64_t)timeoutNum);
    uint32_t jobId = 0;
    size_t outLen = 0;
    bool ok = AppBroker_action(&sAppJobs, action, paramsText, strlen(paramsText),
                               actionOut, sizeof(actionOut), &jobId);
    if (!ok) {
        AppStatus verdict = AppBroker_getLastStatus(&sAppJobs);
        if (verdict == APP_STATUS_BUSY_FULL) {
            appendStr(out, cap, &pos,
                      "BUSY_FULL: all 16 app slots RUNNING — "
                      "poll an old job, then retry");
            return false;
        }
        if (verdict == APP_STATUS_TIMEOUT) {
            appendFmt(out, cap, &pos, "job %u TIMEOUT target=%s action=%s",
                      jobId, AppProvider_getSlug(dir, slot), action);
            return false;
        }
        appendFmt(out, cap, &pos,
                  "UNBOUND_DRIVER: target '%s' action '%s' resolved, but no "
                  "App driver is bound in this process — exec lives in "
                  "vexspoke/R3 (McpServer_bindAppDriver); nothing ran",
                  AppProvider_getSlug(dir, slot), action);
        return false;
    }
    const AppJob *job = nullptr;
    uint32_t count = AppBroker_getJobCount(&sAppJobs);
    for (uint32_t i = 0; i < count; i++) {
        const AppJob *cand = AppBroker_getJobAt(&sAppJobs, i);
        if (cand && (*cand).jobId == jobId) {
            job = cand;
            outLen = (*cand).outLen;
            break;
        }
    }
    appendFmt(out, cap, &pos, "job %u DONE target=%s action=%s outLen=%zu\n",
              jobId, AppProvider_getSlug(dir, slot), action, outLen);
    (void)job;
    appendStr(out, cap, &pos, actionOut);
    return true;
}

/** Renders the stored status for a previously issued app action id. */
static bool renderAppPoll(const JsonDoc *doc, JsonRef args,
                          char *out, size_t cap) {
    size_t pos = 0;
    ensureSeam();
    double idNum = 0;
    if (!readNumberArg(doc, args, "jobId", &idNum) || idNum <= 0 ||
        idNum > APP_MAX_JOBS) {
        appendStr(out, cap, &pos, "unknown job: pass a jobId from app_action");
        return false;
    }
    uint32_t jobId = (uint32_t)idNum;
    AppStatus st = AppBroker_poll(&sAppJobs, jobId);
    const AppJob *job = nullptr;
    uint32_t count = AppBroker_getJobCount(&sAppJobs);
    for (uint32_t i = 0; i < count; i++) {
        const AppJob *cand = AppBroker_getJobAt(&sAppJobs, i);
        if (cand && (*cand).jobId == jobId) {
            job = cand;
            break;
        }
    }
    if (!job) {
        appendFmt(out, cap, &pos, "unknown job: %u", jobId);
        return false;
    }
    AppProvider *dir = AppProvider_shared();
    const char *targetSlug = (*job).target
        ? AppProvider_getSlug(dir, (*job).target) : "?";
    appendFmt(out, cap, &pos, "job %u: %s target=%s", jobId,
              appStatusName(st), targetSlug);
    return true;
}

/** Renders the blessed search-provider catalog, optionally filtered by query. */
static bool renderSearchList(const JsonDoc *doc, JsonRef args,
                             char *out, size_t cap) {
    size_t pos = 0;
    SearchProvider *dir = SearchProvider_shared();
    char query[128];
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));
    const uint32_t total = SearchProvider_count(dir);
    appendFmt(out, cap, &pos,
              "SEARCH PROVIDERS (%u) — slug | display | family | auth | endpoint\n",
              total);
    for (uint32_t i = 0; i < total; i++) {
        const SearchProviderSlot *slot = SearchProvider_at(dir, i);
        if (haveQuery && (*query) != '\0') {
            const char *sl = SearchProvider_getSlug(dir, slot);
            const char *display = SearchProvider_getDisplayName(dir, slot);
            const char *ep = SearchProvider_getEndpoint(dir, slot);
            if (!containsFold(sl, query) && !containsFold(display, query) &&
                !containsFold(ep, query))
                continue;
        }
        appendFmt(out, cap, &pos, "- %s | %s | %s | %s | %s\n",
                  SearchProvider_getSlug(dir, slot),
                  SearchProvider_getDisplayName(dir, slot),
                  searchFamilyName(SearchProvider_getFamily(dir, slot)),
                  searchAuthName(SearchProvider_getAuth(dir, slot)),
                  SearchProvider_getEndpoint(dir, slot));
    }
    return true;
}

/** Queries a configured documented search API and renders bounded result rows. */
static bool renderWebSearch(const JsonDoc *doc, JsonRef args,
                            char *out, size_t cap) {
    size_t pos = 0;
    SearchProvider *dir = SearchProvider_shared();
    static char query[2048];
    char slug[128];
    char base[512];
    double countNum = 0;
    if (!readStringArg(doc, args, "query", query, sizeof(query)) ||
        (*query) == '\0') {
        appendStr(out, cap, &pos, "missing query: pass search text");
        return false;
    }
    slug[0] = '\0';
    readStringArg(doc, args, "provider", slug, sizeof(slug));
    if ((*slug) == '\0') {
        const char *dflt = "searxng";
        size_t dl = strlen(dflt) + 1;
        if (dl > sizeof(slug))
            dl = sizeof(slug);
        memcpy(slug, dflt, dl);
    }
    const SearchProviderSlot *slot = SearchProvider_get(dir, slug);
    if (!slot) {
        appendFmt(out, cap, &pos, "unknown search provider: %s", slug);
        return false;
    }
    uint32_t want = 5;
    if (readNumberArg(doc, args, "count", &countNum) && countNum >= 1) {
        want = (uint32_t)countNum;
        if (want > 10)
            want = 10;
    }
    static char encoded[4096];
    if (!encodeQueryComponent(query, encoded, sizeof(encoded))) {
        appendStr(out, cap, &pos, "query too long to encode (4KiB cap)");
        return false;
    }

    // --- build the backend URL (documented APIs only, never scraping) ---
    static char url[2048];
    url[0] = '\0';
    SearchProviderFamily fam = SearchProvider_getFamily(dir, slot);
    if (fam == SEARCH_FAMILY_SEARXNG) {
        const char *ep = SearchProvider_getEndpoint(dir, slot);
        base[0] = '\0';
        if (readStringArg(doc, args, "base", base, sizeof(base)) &&
            (*base) != '\0')
            ep = base;
        size_t bl = strlen(ep);
        while (bl > 0 && ep[bl - 1] == '/')
            bl--;
        int n = snprintf(url, sizeof(url), "%.*s/search?q=%s&format=json",
                         (int)bl, ep, encoded);
        if (n <= 0 || (size_t)n >= sizeof(url)) {
            appendStr(out, cap, &pos, "search URL overflow (2KiB cap)");
            return false;
        }
    } else if (fam == SEARCH_FAMILY_GOOGLE_CSE) {
        const char *key = getenv("GOOGLE_CSE_KEY");
        const char *cx = getenv("GOOGLE_CSE_CX");
        if (!key || (*key) == '\0' || !cx || (*cx) == '\0') {
            appendStr(out, cap, &pos,
                      "missing GOOGLE_CSE_KEY/GOOGLE_CSE_CX env: export both "
                      "(never the repo, never the arena) and retry");
            return false;
        }
        int n = snprintf(url, sizeof(url),
                         "%s/customsearch/v1?key=%s&cx=%s&q=%s&num=%u",
                         SearchProvider_getEndpoint(dir, slot),
                         key, cx, encoded, want > 10 ? 10 : want);
        if (n <= 0 || (size_t)n >= sizeof(url)) {
            appendStr(out, cap, &pos, "search URL overflow (2KiB cap)");
            return false;
        }
    } else {
        int n = snprintf(url, sizeof(url),
                         "%s/w/api.php?action=query&list=search&srsearch=%s"
                         "&srlimit=%u&format=json",
                         SearchProvider_getEndpoint(dir, slot),
                         encoded, want);
        if (n <= 0 || (size_t)n >= sizeof(url)) {
            appendStr(out, cap, &pos, "search URL overflow (2KiB cap)");
            return false;
        }
    }

    // --- bounded fetch (Rest_get: 5s; caller-owned buffers, no alloc) ---
    static char fetch[32768];
    HttpResponse resp;
    resp.body = fetch;
    resp.bodyCap = sizeof(fetch);
    resp.bodyLen = 0;
    ApiAuth none = ApiAuth_none();
    if (!Rest_get(url, &none, &resp) || !resp.ok) {
        if (strncmp(url, "https://", 8) == 0) {
            appendFmt(out, cap, &pos,
                      "TLS_BACKEND_MISSING: '%s' needs https and this build "
                      "speaks http:// only (no TLS backend yet) — use the "
                      "searxng provider, or wait for the curl backend",
                      SearchProvider_getSlug(dir, slot));
            return false;
        }
        appendFmt(out, cap, &pos,
                  "transport error: no response from '%s' (status %d) — "
                  "is the backend up? SearXNG defaults to "
                  "http://localhost:8888",
                  SearchProvider_getSlug(dir, slot), resp.status);
        return false;
    }
    if (resp.status != 200) {
        appendFmt(out, cap, &pos, "provider HTTP %d from '%s'",
                  resp.status, SearchProvider_getSlug(dir, slot));
        return false;
    }

    // --- parse the documented JSON shape per family ---
    JsonNode nodes[256];
    static char scratch[65536];
    JsonDoc rdoc;
    if (!Json_parse(&rdoc, nodes, 256, scratch, sizeof(scratch), fetch)) {
        appendFmt(out, cap, &pos,
                  "provider '%s' returned non-JSON (first 80 Bytes: %.80s)",
                  SearchProvider_getSlug(dir, slot), fetch);
        return false;
    }
    JsonRef items = -1;
    const char *tKey = "title";
    const char *uKey = "url";
    const char *sKey = "content";
    if (fam == SEARCH_FAMILY_SEARXNG) {
        items = Json_member(&rdoc, Json_root(&rdoc), "results");
    } else if (fam == SEARCH_FAMILY_GOOGLE_CSE) {
        items = Json_member(&rdoc, Json_root(&rdoc), "items");
        uKey = "link";
        sKey = "snippet";
    } else {
        JsonRef q = Json_member(&rdoc, Json_root(&rdoc), "query");
        if (q >= 0)
            items = Json_member(&rdoc, q, "search");
        sKey = "snippet";
    }
    uint32_t total = items >= 0 ? Json_count(&rdoc, items) : 0;
    uint32_t shown = total < want ? total : want;
    appendFmt(out, cap, &pos, "WEB SEARCH '%s' via %s — %u result%s (showing %u)\n",
              query, SearchProvider_getSlug(dir, slot),
              total, total == 1 ? "" : "s", shown);
    if (total == 0)
        return true;
    static char title[256];
    static char link[512];
    static char snippet[1024];
    static char plain[1024];
    for (uint32_t i = 0; i < shown; i++) {
        JsonRef item = Json_at(&rdoc, items, i);
        title[0] = '\0';
        link[0] = '\0';
        snippet[0] = '\0';
        if (item >= 0) {
            copyJsonString(&rdoc, Json_member(&rdoc, item, tKey),
                           title, sizeof(title));
            copyJsonString(&rdoc, Json_member(&rdoc, item, uKey),
                           link, sizeof(link));
            copyJsonString(&rdoc, Json_member(&rdoc, item, sKey),
                           snippet, sizeof(snippet));
        }
        const char *text = snippet;
        if (fam == SEARCH_FAMILY_MEDIAWIKI) {
            stripHtmlTags(plain, sizeof(plain), snippet);
            text = plain;
        }
        appendFmt(out, cap, &pos, "- %s | %s\n  %s\n",
                  (*title) != '\0' ? title : "(untitled)",
                  (*link) != '\0' ? link : "(no url)", text);
    }
    return true;
}

// --- asset renderers (Rule 34 catalog surface; lookup precedent: renderDbLookup) ---

/** Renders asset-source details or a filtered catalog listing. */
static bool renderAssetLookup(const JsonDoc *doc, JsonRef args,
                              char *out, size_t cap) {
    size_t pos = 0;
    AssetProvider *dir = AssetProvider_shared();
    char slug[128];
    char query[128];
    const bool haveSlug = readStringArg(doc, args, "slug", slug, sizeof(slug));
    const bool haveQuery = readStringArg(doc, args, "query", query,
                                         sizeof(query));

    if (haveSlug && (*slug) != '\0') {
        const AssetProviderSlot *slot = AssetProvider_get(dir, slug);
        if (!slot) {
            appendFmt(out, cap, &pos, "unknown asset source: %s", slug);
            return false;
        }
        appendFmt(out, cap, &pos, "asset-source: %s\n",
                  AssetProvider_getSlug(dir, slot));
        appendFmt(out, cap, &pos, "  display: %s\n",
                  AssetProvider_getDisplayName(dir, slot));
        const char *api = AssetProvider_getApiBase(dir, slot);
        appendFmt(out, cap, &pos, "  api: %s\n",
                  api ? api : "(catalog-only — curated manifest, no search API)");
        appendFmt(out, cap, &pos, "  license: %s\n",
                  AssetProvider_getLicenseFamily(dir, slot));
        appendFmt(out, cap, &pos, "  sample: %s by %s\n",
                  AssetProvider_getSampleTitle(dir, slot),
                  AssetProvider_getSampleAuthor(dir, slot));
        appendFmt(out, cap, &pos, "  preview: %s\n",
                  AssetProvider_getSamplePreview(dir, slot));
        appendFmt(out, cap, &pos, "  download: %s\n",
                  AssetProvider_getSampleDownload(dir, slot));
        const char *note = AssetProvider_getNote(dir, slot);
        if (note)
            appendFmt(out, cap, &pos, "  note: %s\n", note);
        return true;
    }

    // bare (or query-filtered) listing of the full 12-row table
    const uint32_t total = AssetProvider_count(dir);
    for (uint32_t i = 0; i < total; i++) {
        const AssetProviderSlot *slot = AssetProvider_at(dir, i);
        if (haveQuery && (*query) != '\0') {
            const char *sl = AssetProvider_getSlug(dir, slot);
            const char *display = AssetProvider_getDisplayName(dir, slot);
            const char *note = AssetProvider_getNote(dir, slot);
            if (!containsFold(sl, query) && !containsFold(display, query) &&
                !containsFold(note, query))
                continue;
        }
        const char *api = AssetProvider_getApiBase(dir, slot);
        appendFmt(out, cap, &pos, "- %s | %s | %s | %s\n",
                  AssetProvider_getSlug(dir, slot),
                  AssetProvider_getDisplayName(dir, slot),
                  AssetProvider_getLicenseFamily(dir, slot),
                  api != nullptr ? api : "catalog-only");
    }
    return true;
}

/** Renders a cache-confined download plan without fetching or executing anything. */
static bool renderAssetDownload(const JsonDoc *doc, JsonRef args,
                                char *out, size_t cap) {
    size_t pos = 0;
    AssetProvider *dir = AssetProvider_shared();
    char slug[128];
    char file[128];
    if (!readStringArg(doc, args, "slug", slug, sizeof(slug)) ||
        (*slug) == '\0') {
        appendStr(out, cap, &pos, "missing slug: pass an asset-source slug, e.g. poly-haven");
        return false;
    }
    const AssetProviderSlot *slot = AssetProvider_get(dir, slug);
    if (!slot) {
        appendFmt(out, cap, &pos, "unknown asset source: %s", slug);
        return false;
    }
    file[0] = '\0';
    readStringArg(doc, args, "file", file, sizeof(file));
    char fileName[160];
    if ((*file) == '\0') {
        int n = snprintf(fileName, sizeof(fileName), "%s-sample",
                         AssetProvider_getSlug(dir, slot));
        if (n <= 0 || (size_t)n >= sizeof(fileName)) {
            appendStr(out, cap, &pos, "file name overflow (160 cap)");
            return false;
        }
    } else {
        size_t fl = strlen(file);
        if (fl + 1 > sizeof(fileName)) {
            appendStr(out, cap, &pos, "file name overflow (160 cap)");
            return false;
        }
        memcpy(fileName, file, fl + 1);
    }
    AssetBroker broker = AssetBroker();
    char cachePath[256];
    if (!AssetBroker_cachePath(&broker, fileName, cachePath, sizeof(cachePath))) {
        appendStr(out, cap, &pos, "cache path overflow (256 cap)");
        return false;
    }
    appendFmt(out, cap, &pos, "asset download plan: %s\n",
              AssetProvider_getSlug(dir, slot));
    appendFmt(out, cap, &pos, "  sample: %s by %s\n",
              AssetProvider_getSampleTitle(dir, slot),
              AssetProvider_getSampleAuthor(dir, slot));
    appendFmt(out, cap, &pos, "  license: %s (attribution rendered before import)\n",
              AssetProvider_getLicenseFamily(dir, slot));
    appendFmt(out, cap, &pos, "  download: %s\n",
              AssetProvider_getSampleDownload(dir, slot));
    appendFmt(out, cap, &pos, "  cache: %s\n", cachePath);
    appendStr(out, cap, &pos,
              "  terms: Bytes stream via AssetBroker 64KiB chunked copies, "
              "per-chunk 100ms budget + cancel, VexHome_cache-confined, "
              "closed before Memory_freeAll — this tool answers the plan, "
              "never fetches (no network, no exec)");
    return true;
}

// --- resource read ----------------------------------------------------------

/** Looks up a hosted resource by exact URI. */
static const McpResourceSlot *findResource(const char *uri) {
    if (!uri)
        return nullptr;
    for (size_t i = 0; i < kMcpResourceCount; i++) {
        if (strcmp(kMcpResources[i].uri, uri) == 0)
            return &kMcpResources[i];
    }
    return nullptr;
}

// --- _main dispatch ----------------------------------------------------------

/** Parses one JSON-RPC input line and writes a response when it is a request. */
bool McpServer_handleLine(McpServer *self, const char *line, size_t lineLen,
                          char *outBuf, size_t outCap) {
    if (!self || !line || !outBuf || outCap < 2)
        return false;
    outBuf[0] = '\0';

    // Raw id echo (verbatim, per JSON-RPC).
    char id[64];
    const bool hasId = rawIdExtract(line, lineLen, id, sizeof(id));
    const bool idNull = hasId && strcmp(id, "null") == 0;
    const char *idS = idNull ? "null" : (hasId ? id : "null");

    // Reject batches deterministically (the core is line-oriented).
    const char *first = line;
    while (first < line + lineLen && ((*first) == ' ' || (*first) == '\t'))
        first++;
    if (first < line + lineLen && (*first) == '[') {
        respondError(-32600, "Batch requests are not supported", idS,
                     outBuf, outCap);
        return true;
    }

    // Parse (failed parse => parse error). Note: line must be
    // NUL-terminated by the caller (in practice a read-line buffer).
    // Pools sized for run/action prompts: 256 nodes, 64KiB of decoded
    // escapes (a 32KiB prompt with escapes must still fit).
    JsonNode nodes[256];
    char scratch[65536];
    JsonDoc doc;
    if (!Json_parse(&doc, nodes, 256, scratch, sizeof(scratch), line)) {
        respondError(-32700, "Parse error", idS, outBuf, outCap);
        return true;
    }

    JsonRef root = Json_root(&doc);
    JsonRef method = Json_member(&doc, root, "method");
    if (method < 0) {
        if (hasId && !idNull)
            respondError(-32600, "Invalid Request", idS, outBuf, outCap);
        return hasId && !idNull;
    }

    // Notifications never answer (JSON-RPC 2.0).
    if (!hasId || idNull)
        return false;

    if (methodIs(&doc, method, "initialize"))
        return respondInitializeReal(self, &doc, idS, outBuf, outCap);
    if (methodIs(&doc, method, "ping")) {
        respondPing(idS, outBuf, outCap);
        return true;
    }
    if (methodIs(&doc, method, "tools/list")) {
        respondToolsList(idS, outBuf, outCap);
        return true;
    }
    if (methodIs(&doc, method, "resources/list")) {
        respondResourcesList(idS, outBuf, outCap);
        return true;
    }
    if (methodIs(&doc, method, "resources/read")) {
        JsonRef params = Json_member(&doc, root, "params");
        char uri[256];
        readStringArg(&doc, params, "uri", uri, sizeof(uri));
        const McpResourceSlot *res = findResource(uri);
        if (!res) {
            respondError(-32602, "Unknown resource", idS, outBuf, outCap);
            return true;
        }
        char body[65536];
        body[0] = '\0';
        (void)(*res).render(&doc, -1, body, sizeof(body));
        char escaped[131072];
        escapeJsonText(body, escaped, sizeof(escaped));
        size_t pos = 0;
        appendStr(outBuf, outCap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
        appendStr(outBuf, outCap, &pos, idS);
        appendFmt(outBuf, outCap, &pos,
                  ",\"result\":{\"contents\":[{\"uri\":\"%s\",\"mimeType\":"
                  "\"%s\",\"text\":\"%s\"}]}}",
                  (*res).uri, (*res).mimeType, escaped);
        return true;
    }
    if (methodIs(&doc, method, "tools/call")) {
        JsonRef params = Json_member(&doc, root, "params");
        char toolName[128];
        readStringArg(&doc, params, "name", toolName, sizeof(toolName));
        if ((*toolName) == '\0') {
            respondError(-32602, "Unknown tool", idS, outBuf, outCap);
            return true;
        }
        const McpToolSlot *tool = nullptr;
        for (size_t i = 0; i < kMcpToolCount; i++) {
            if (strcmp(kMcpTools[i].name, toolName) == 0) {
                tool = &kMcpTools[i];
                break;
            }
        }
        if (!tool) {
            respondError(-32602, "Unknown tool", idS, outBuf, outCap);
            return true;
        }
        JsonRef args = Json_member(&doc, params, "arguments");
        char body[65536];
        body[0] = '\0';
        const bool ok = (*tool).handle(&doc, args, body, sizeof(body));
        char escaped[131072];
        escapeJsonText(body, escaped, sizeof(escaped));
        size_t pos = 0;
        appendStr(outBuf, outCap, &pos, "{\"jsonrpc\":\"2.0\",\"id\":");
        appendStr(outBuf, outCap, &pos, idS);
        if (ok)
            appendStr(outBuf, outCap, &pos, ",\"result\":{\"content\":[");
        else
            appendStr(outBuf, outCap, &pos,
                      ",\"result\":{\"isError\":true,\"content\":[");
        appendFmt(outBuf, outCap, &pos, "{\"type\":\"text\",\"text\":\"%s\"}]}}",
                  escaped);
        return true;
    }

    respondError(-32601, "Method not found", idS, outBuf, outCap);
    return true;
}

// CONSTRUCTORS

/** Returns the shared server instance with its static identity initialized. */
McpServer *McpServer_shared(void) {
    static McpServer sServerShared; // zero-init singleton
    sServerShared.name = "vexgraph-mcp";
    sServerShared.version = MCP_SERVER_VERSION;
    return &sServerShared;
}

// GETTERS

/** Returns the configured server name, or nullptr for a null receiver. */
const char *McpServer_getName(const McpServer *self) {
    return self ? (*self).name : nullptr;
}

/** Returns the configured server version, or nullptr for a null receiver. */
const char *McpServer_getVersion(const McpServer *self) {
    return self ? (*self).version : nullptr;
}

/** Returns the negotiated protocol version, or nullptr before initialization. */
const char *McpServer_getProtocolVersion(const McpServer *self) {
    return self ? (*self).protocolVersion : nullptr;
}

/** Reports whether the server has completed its initialize handshake. */
bool McpServer_isInitialized(const McpServer *self) {
    return self && (*self).initialized != 0;
}
