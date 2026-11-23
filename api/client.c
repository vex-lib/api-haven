#include "vex.h"
#include "api/client.h"
#include "net/http.h"
#include "net/url.h"
#include "nio/mem.h"
#include "re/primitive/vex-string.h"

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
 *   u64 frameCount;       // Live rendered frame counter
 *   i32 state;             // Engine runtime state flags
 *   i32 activePipelines;   // Count of currently active Vulkan pipelines
 *   u64 totalAllocations; // Lifetime allocation counter
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - APIClient_sendTelemetry(endpointUrl, telemetry)
 * ============================================================================
 */
;;INTENTION("canonical shim for the quarantined attic/APIClient.java: same sendTelemetry contract, vexspoke transports, null-safe degrade (false), Rule 35 cold-strict")

/** Splits a URL into bounded scheme, host, port, and path outputs. */
static bool parseUrl(string url, utf8 *scheme, bytes schemeCap,
                     utf8 *host, bytes hostCap, i32 *port,
                     utf8 *path, bytes pathCap) {
    if (!url || url[0] == '\0')
        return false;

    string p = url;
    bytes schemeIdx = string_indexOf(p, "://");
    string schemeEnd = (schemeIdx != SIZE_MAX) ? (p + schemeIdx) : nullptr;
    if (schemeEnd) {
        bytes sLen = (bytes)(schemeEnd - p);
        if (sLen >= schemeCap)
            return false;
        memcpy(scheme, p, sLen);
        scheme[sLen] = '\0';
        p = schemeEnd + 3;
    } else {
        strncpy(scheme, "http", schemeCap - 1);
        scheme[schemeCap - 1] = '\0';
    }

    *port = Url_defaultPort(scheme);

    bytes slashIdx = string_findChar(p, '/');
    string slash = (slashIdx != SIZE_MAX) ? (p + slashIdx) : nullptr;
    bytes colonIdx = string_findChar(p, ':');
    string colon = (colonIdx != SIZE_MAX) ? (p + colonIdx) : nullptr;

    if (colon && (!slash || colon < slash)) {
        bytes hLen = (bytes)(colon - p);
        if (hLen >= hostCap)
            return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        bytes hLen = (bytes)(slash - p);
        if (hLen >= hostCap)
            return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
    } else {
        bytes hLen = strlen(p);
        if (hLen >= hostCap)
            return false;
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

;;GETTER
static inline i32 APIClientTelemetry_state(const APIClientTelemetry *self)
{
    return (*self).state;
}
;;GETTER
static inline u64 APIClientTelemetry_frameCount(const APIClientTelemetry *self)
{
    return (*self).frameCount;
}
;;GETTER
static inline i32 APIClientTelemetry_activePipelines(const APIClientTelemetry *self)
{
    return (*self).activePipelines;
}
;;GETTER
static inline u64 APIClientTelemetry_totalAllocations(const APIClientTelemetry *self)
{
    return (*self).totalAllocations;
}

/** Serializes telemetry into bounded JSON and reports HTTP success. */
bool APIClient_sendTelemetry(string endpointUrl, const APIClientTelemetry *telemetry) {
    if (!endpointUrl || !telemetry)
        return false;

    utf8 scheme[16] = {0};
    utf8 host[256] = {0};
    utf8 path[512] = {0};
    i32 port = 80;

    if (!parseUrl(endpointUrl, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)))
        return false;

    utf8 jsonBuf[512];
    i32 n = snprintf(jsonBuf, sizeof(jsonBuf),
                     "{\"state\":%d,\"frameCount\":%llu,\"activePipelines\":%d,\"totalAllocations\":%llu}",
                     APIClientTelemetry_state(telemetry),
                     (u64)APIClientTelemetry_frameCount(telemetry),
                     APIClientTelemetry_activePipelines(telemetry),
                     (u64)APIClientTelemetry_totalAllocations(telemetry));
    if (n < 0 || (bytes)n >= sizeof(jsonBuf))
        return false;

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
        .bodyLen = (bytes)n,
        .timeoutMs = 5000
    };

    utf8 respBody[2048];
    HttpResponse resp = { 0 };
    resp.body = respBody;
    resp.bodyCap = sizeof(respBody);
    Http_perform(&req, &resp);
    return resp.ok && resp.status >= 200 && resp.status < 300;
}
