#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "vex.h"
#include "app/app_broker.h"
#include "harness/harness.h"
#include "net/json.h"


/**
 * mcp/mcp_server.h, Model Context Protocol server engine (JSON-RPC 2.0).
 *
 * The api-haven CLI/MCP layer (PREFERENCES.md Rule 17): exposes the
 * vexspoke system probes (AppDetect, CaptureTool), the api-haven
 * catalogs (AiProvider, DbProvider, AssetProvider), and the bounded
 * driver seams (Harness, AppBroker, SearchProvider) as MCP tools +
 * resources over newline-delimited JSON-RPC on stdio (see kMcpTools /
 * kMcpResources in mcp_server.c for the hosted surface). Transport-agnostic core: feed it
 * one client line, it renders one response line (or none for
 * notifications). Zero allocation, caller-owned buffers, single-threaded,
 * immutable tool/resource tables behind fn-pointer handlers.
 *
 * Supported protocol versions: 2024-11-05, 2025-03-26, 2025-06-18.
 * v1 surface: initialize, ping, tools/list, tools/call, resources/list,
 * resources/read; notifications are accepted and not answered.
 */

#define MCP_SERVER_VERSION "0.1.0"
#define MCP_PROTOCOL_LATEST "2025-06-18"

/**
 * The MCP engine singleton handle. Protocol state mutates only under
 * initialize (single-threaded).
 *
 * utf8 protocolVersion[32] - negotiated client protocol version or latest
 * u32 initialized          - 1 after a successful initialize handshake
 * string name         - static server name ("vexgraph-mcp")
 * string version      - static server version (MCP_SERVER_VERSION)
 */
typedef struct McpServer {
    utf8 protocolVersion[32];
    u32 initialized;
    string name;
    string version;
} McpServer;

/**
 * One MCP tool or resource handler callback. Writes the plain-text body
 * into out (cap bytes). Returns true on success (isError false) or false
 * on failure (isError true).
 */
typedef bool (*McpToolFn)(const JsonDoc *doc, JsonRef args,
                          utf8 *out, bytes cap);

/**
 * Static descriptor entry for one hosted MCP tool.
 *
 * string name        - tool key (e.g. "app_detect")
 * string description - one-line MCP description
 * string inputSchema - pre-rendered JSON Schema string
 * McpToolFn handle        - tool invocation handler callback
 */
typedef struct McpToolSlot {
    string name;
    string description;
    string inputSchema;
    McpToolFn handle;
} McpToolSlot;

/**
 * Static descriptor entry for one hosted MCP resource URI.
 *
 * string uri      - stable resource URI (e.g. "system://apps")
 * string name     - human-readable resource label
 * string mimeType - MIME type string (e.g. "text/plain")
 * McpToolFn render     - resource body render callback
 */
typedef struct McpResourceSlot {
    string uri;
    string name;
    string mimeType;
    McpToolFn render;
} McpResourceSlot;

/**
 * --- Constructor ---
 * Returns the shared engine handle (static, zero-init, never nullptr).
 */
McpServer *McpServer_shared();

/**
 * --- Driver seam (Rule 17: exec lives in vexspoke/R3, injected here) ---
 * Binds the Harness/AppBroker driver tables hosted by this server's
 * file-static seam instances (16 bounded slots each). The standalone
 * mcp_server runner never binds: run/action tools then degrade to an
 * honest UNBOUND_DRIVER error instead of spawning. An R3 host binds
 * before serving. nullptr table fns unbind (degrade path). Null-safe.
 * Timeout bounds come per-call (timeoutMs arg) or per-seam default.
 */
fn McpServer_bindHarnessDriver(any driverCtx, HarnessDriverTable table);
fn McpServer_bindAppDriver(any driverCtx, AppDriverTable table);

/**
 * --- Core functions ---
 * Feed one JSON-RPC line (may span any length <= cap handling) and render
 * the response into outBuf (NUL-terminated) when one is due. Returns true
 * when a response line was written (caller prints it + '\n'). Returns
 * false for notifications (no reply) and for nullptr/no-cap inputs.
 * outCap must be > 0.
 */
bool McpServer_handleLine(McpServer *self, string line, bytes lineLen,
                          utf8 *outBuf, bytes outCap);

/** --- Getters (Rule 24; null-safe) --- */
string McpServer_getName(const McpServer *self);
string McpServer_getVersion(const McpServer *self);
string McpServer_getProtocolVersion(const McpServer *self);
bool McpServer_isInitialized(const McpServer *self);

#endif