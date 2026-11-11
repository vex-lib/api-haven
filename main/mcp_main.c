#include "annotation/annotation.h"
#include "mcp/mcp_server.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/** The stdio transport for McpServer, and nothing else. It exists so the protocol
 * engine never learns about file descriptors: stdin is read as newline-delimited
 * JSON-RPC, each line goes to McpServer_handleLine, and the response is written
 * to stdout and flushed at once because clients block on the pipe. stdout is the
 * wire and carries no log byte; stderr carries every diagnostic. A line past
 * 64 KiB is drained and answered with a parse error so the stream never desyncs,
 * and EOF exits 0. */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: mcp_main (_main/mcp_main.c — thin entry point, owns zero structs;
 * Rule 23 MODULE allowance: procedural stdio transport for McpServer)
 * ============================================================================
 * Model Context Protocol stdio server runner. Reads newline-delimited
 * JSON-RPC from stdin, hands each line to McpServer_handleLine, and
 * writes each produced response back to stdout followed by '\n' (flushed
 * immediately — MCP clients wait on pipe output). Logs live on stderr
 * only so stdout stays a pure JSON channel (clients parse it as the
 * protocol stream; any stray byte corrupts the handshake).
 *
 * Line discipline: lines up to 64 KiB are handled as-is; a longer line is
 * drained (so the stream never desyncs) and answered with a parse error.
 * EOF on stdin exits 0.
 *
 * FUNCTION REGISTRY:
 * Core Functions:
 *   - _main() : run the stdio loop until EOF
 * ============================================================================
 */

// Overflow guard: line must fit the 64 KiB buffer (MCP payloads are small).
#define kLineCap 65536u

/** Runs the newline-framed MCP stdio loop until stdin reaches EOF. */
int main(void) {
    // Banner to stderr — never stdout (Rule: stdout is the JSON-RPC wire).
    fprintf(stderr, "vexgraph-mcp %s (stdio JSON-RPC; logs to stderr)\n",
            MCP_SERVER_VERSION);

    McpServer *server = McpServer_shared();
    static char line[kLineCap];
    char response[262144]; // worst case: full capture/app-dump escape

    while (fgets(line, (int)kLineCap, stdin) != nullptr) {
        const bytes len = strlen(line);
        // Strip the trailing newline (MCP framing is newline-delimited;
        // keep CR out of the parser too).
        bytes lineLen = len;
        if (lineLen > 0 && line[lineLen - 1] == '\n')
            lineLen--;
        if (lineLen > 0 && line[lineLen - 1] == '\r')
            lineLen--;
        line[lineLen] = '\0';

        // Overlong line: drain the remainder, answer a parse error so the
        // client learns the frame was rejected.
        if (lineLen == kLineCap - 1 && line[kLineCap - 2] != '\n') {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            fputs("{\"jsonrpc\":\"2.0\",\"id\":null,"
                  "\"error\":{\"code\":-32700,\"message\":\"Parse error: "
                  "line too long\"}}\n", stdout);
            fflush(stdout);
            continue;
        }

        const bool wrote = McpServer_handleLine(server, line, lineLen,
                                                response, sizeof(response));
        if (wrote) {
            fputs(response, stdout);
            fputc('\n', stdout);
            fflush(stdout);
        }
    }

    fprintf(stderr, "vexgraph-mcp: stdin closed, exiting\n");
    return 0;
}
