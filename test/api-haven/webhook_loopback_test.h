#ifndef WEBHOOK_LOOPBACK_TEST_H
#define WEBHOOK_LOOPBACK_TEST_H

#include "vex.h"

/**
 * Captured HTTP request snapshot recorded by the loopback webhook test server.
 *
 * utf8 method[16]   - HTTP method string of the received request
 * utf8 path[1024]   - request target path string
 * utf8 body[8192]   - raw request body bytes
 * bytes bodyLen     - number of valid bytes written into body
 * i32 hits          - count of requests handled by the loopback callback
 */
typedef struct Capture {
    utf8 method[16];
    utf8 path[1024];
    utf8 body[8192];
    bytes bodyLen;
    i32 hits;
} Capture;

#endif
