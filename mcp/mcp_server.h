#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "app/app_broker.h"
#include "harness/harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// mcp/mcp_server.h — Model Context Protocol server engine (JSON-RPC 2.0).
//
// The api-haven CLI/MCP layer (PREFERENCES.md Rule 17): exposes the
// vexspoke system probes (AppDetect, CaptureTool), the api-haven
// catalogs (AiProvider, DbProvider, AssetProvider), and the bounded
// driver seams (Harness, AppBroker, SearchProvider) as MCP tools +
// resources over newline-delimited JSON-RPC on stdio (see kMcpTools /
// kMcpResources in mcp_server.c for the hosted surface). Transport-agnostic core: feed it
// one client line, it renders one response line (or none for
// notifications). Zero allocation, caller-owned buffers, single-threaded,
// immutable tool/resource tables behind fn-pointer handlers.
//
// Supported protocol versions: 2024-11-05, 2025-03-26, 2025-06-18.
// v1 surface: initialize, ping, tools/list, tools/call, resources/list,
// resources/read; notifications are accepted and not answered.

#define MCP_SERVER_VERSION "0.1.0"
#define MCP_PROTOCOL_LATEST "2025-06-18"

// The MCP engine — singleton handle; protocol state mutates only under
// initialize (no threads; safe).
typedef struct McpServer {
    // --- protocol state (mutated by initialize, read-only after) ---
    char protocolVersion[32]; // negotiated client version or latest
    uint32_t initialized;     // 1 after a successful initialize handshake
    // --- identity (static) ---
    const char *name;         // "vexgraph-mcp"
    const char *version;      // MCP_SERVER_VERSION
} McpServer;

// --- Constructor ---
// Returns the shared engine handle (static, zero-init, never NULL).
McpServer *McpServer_shared(void);

// --- Driver seam (Rule 17: exec lives in vexspoke/R3, injected here) ---
// Binds the Harness/AppBroker driver tables hosted by this server's
// file-static seam instances (16 bounded slots each). The standalone
// mcp_server runner never binds: run/action tools then degrade to an
// honest UNBOUND_DRIVER error instead of spawning. An R3 host binds
// before serving. NULL table fns unbind (degrade path). Null-safe.
// Timeout bounds come per-call (timeoutMs arg) or per-seam default.
void McpServer_bindHarnessDriver(void *driverCtx, HarnessDriverTable table);
void McpServer_bindAppDriver(void *driverCtx, AppDriverTable table);

// --- Core functions ---
// Feed one JSON-RPC line (may span any length <= cap handling) and render
// the response into outBuf (NUL-terminated) when one is due. Returns true
// when a response line was written (caller prints it + '\n'). Returns
// false for notifications (no reply) and for NULL/no-cap inputs.
// outCap must be > 0.
bool McpServer_handleLine(McpServer *self, const char *line, size_t lineLen,
                          char *outBuf, size_t outCap);

// --- Getters (Rule 24; null-safe) ---
const char *McpServer_getName(const McpServer *self);
const char *McpServer_getVersion(const McpServer *self);
const char *McpServer_getProtocolVersion(const McpServer *self);
bool McpServer_isInitialized(const McpServer *self);

#endif