#ifndef VEX_API_HAVEN_TEST_H
#define VEX_API_HAVEN_TEST_H

#include "vex.h"

/**
 * Single suite entry in the consolidated vex/api-haven automated test runner.
 * name: CLI filter token matching argv[1].
 * run: suite entrypoint returning 0 on pass or non-zero on failure.
 */
typedef struct SuiteEntry {
    const utf8 *name;
    i32 (*run)();
} SuiteEntry;

/**
 * In-process mock WebSocket transport for HavenWsFanout tests.
 * pollCount: number of times mockPoll was called.
 * lastBudget: most recent poll budget in milliseconds.
 * framesToDeliver: synthetic frame count returned on each poll.
 */
typedef struct MockTransport {
    u32 pollCount;
    u64 lastBudget;
    u32 framesToDeliver;
} MockTransport;

/**
 * Captured HTTP request state for loopback webhook tests.
 * method: captured HTTP method verb.
 * path: captured request path.
 * body: captured request payload bytes.
 * bodyLen: length of captured payload in bytes.
 * hits: number of requests observed by the loopback server.
 */
typedef struct Capture {
    char method[16];
    char path[1024];
    char body[8192];
    bytes bodyLen;
    i32 hits;
} Capture;

#endif
