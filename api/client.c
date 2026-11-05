#include "api/client.h"
#include "annotation/annotation.h"
#include "net/http.h"
#include "net/url.h"
#include "nio/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** The engine's own telemetry client: it formulates JSON packets off-heap and
 * transmits them over HTTP. It exists as the seam between the engine and the
 * transport, and it keeps every packet in caller-owned storage so a send costs
 * no allocation and leaves nothing behind. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: APIClient (api/client)
 * ============================================================================
 * High-performance, zero-allocation API Client for the VexGraph engine.
 * Formulates off-heap JSON telemetry packets and transmits them over HTTP.
 *
 * STRUCT FIELDS (Mirroring api/client.h):
 * ----------------------------------------------------------------------------
 *   uint64_t frameCount;       // Live rendered frame counter
 *   int32_t state;             // Engine runtime state flags
 *   int32_t activePipelines;   // Count of currently active Vulkan pipelines
 *   uint64_t totalAllocations; // Lifetime allocation counter
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - APIClient_sendTelemetry(endpointUrl, telemetry)
 * ============================================================================
 */
;;INTENTION("canonical shim for the quarantined attic/APIClient.java: same sendTelemetry contract, vexspoke transports, null-safe degrade (false) — Rule 35 cold-strict")

/** Splits a URL into bounded scheme, host, port, and path outputs. */
static bool parseUrl(const char *url, char *scheme, size_t schemeCap,
                     char *host, size_t hostCap, int *port,
                     char *path, size_t pathCap) {
    if (!url || url[0] == '\0') return false;

    const char *p = url;
    const char *schemeEnd = strstr(p, "://");
    if (schemeEnd) {
        size_t sLen = (size_t)(schemeEnd - p);
        if (sLen >= schemeCap) return false;
        memcpy(scheme, p, sLen);
        scheme[sLen] = '\0';
        p = schemeEnd + 3;
    } else {
        strncpy(scheme, "http", schemeCap - 1);
        scheme[schemeCap - 1] = '\0';
    }

    *port = Url_defaultPort(scheme);

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hLen = (size_t)(colon - p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        size_t hLen = (size_t)(slash - p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
    } else {
        size_t hLen = strlen(p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
    }

    if (slash) {
        strncpy(path, slash, pathCap - 1);
        path[pathCap - 1] = '\0';
    } else {
        strncpy(path, "/", pathCap - 1);
        path[pathCap - 1] = '\0';
    }

    return true;
}

/** Serializes telemetry into bounded JSON and reports HTTP success. */
bool APIClient_sendTelemetry(const char *endpointUrl, const APIClientTelemetry *telemetry) {
    if (!endpointUrl || !telemetry) {
        return false;
    }

    char scheme[16] = {0};
    char host[256] = {0};
    char path[512] = {0};
    int port = 80;

    if (!parseUrl(endpointUrl, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path))) {
        return false;
    }

    char jsonBuf[512];
    int n = snprintf(jsonBuf, sizeof(jsonBuf),
                     "{\"state\":%d,\"frameCount\":%llu,\"activePipelines\":%d,\"totalAllocations\":%llu}",
                     (*telemetry).state,
                     (unsigned long long)(*telemetry).frameCount,
                     (*telemetry).activePipelines,
                     (unsigned long long)(*telemetry).totalAllocations);
    if (n < 0 || (size_t)n >= sizeof(jsonBuf)) {
        return false;
    }

    HttpHeader headers[2] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Connection",   .value = "close" }
    };

    HttpRequest req = {
        .scheme = scheme,
        .method = "POST",
        .host = host,
        .port = port,
        .path = path,
        .headers = headers,
        .headerCount = 2,
        .body = jsonBuf,
        .bodyLen = (size_t)n,
        .timeoutMs = 5000
    };

    char respBody[2048];
    HttpResponse resp = { 0 };
    resp.body = respBody;
    resp.bodyCap = sizeof(respBody);
    Http_perform(&req, &resp);
    return resp.ok && resp.status >= 200 && resp.status < 300;
}
