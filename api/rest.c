#include "vex.h"
#include "api/rest.h"

#include <stdlib.h>
#include <string.h>

#include "net/url.h"
#include "re/primitive/vex-string.h"

;;DEFINITION
/** The one place a request becomes bytes: URL parsing, auth injection and HTTP
 * transport. It exists so drivers hand over (url, auth, JSON body) and get an
 * HttpResponse in their own buffer instead of re-implementing transport. Success
 * means a completed transfer AND a 2xx status; a 4xx/5xx/3xx keeps the status
 * and body in the caller's response for inspection, and a required credential
 * that cannot be rendered fails closed before anything is sent. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Rest (api/rest)
 * ============================================================================
 * URL parsing, auth injection, and HTTP transport in one place. Drivers
 * hand over (url, auth, JSON body) and get back an HttpResponse in their
 * own buffer. Success means a completed transport AND a 2xx status; a
 * 4xx/5xx/3xx result returns false while the caller's resp keeps the
 * transferred status and body for inspection. An explicitly required
 * credential that cannot be rendered fails closed (no request is sent).
 * Canonical home of parseUrl (client.c / discord.c copies
 * migrate here later per Rule 33).
 *
 * STRUCT FIELDS: none, procedural core over HttpRequest/HttpResponse.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Rest_postJson(url, auth, jsonBody, jsonLen, resp)
 *   - Rest_get(url, auth, resp)
 * ============================================================================
 */

/** Parses the URL into bounded scheme, host, port, and path outputs. */
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

/** A transferred response is a success only on a 2xx status; redirects and errors are not. */
static bool statusIsSuccess(i32 status) {
    return status >= 200 && status < 300;
}

;;GETTER
static inline ApiAuthKind ApiAuth_kind(const ApiAuth *self)
{
    return (*self).kind;
}
;;GETTER
static inline i32 HttpResponse_status(const HttpResponse *self)
{
    return (*self).status;
}

/** Builds and performs one bounded JSON HTTP request using the shared transport. */
static bool perform(string url, const ApiAuth *auth, string method,
                    string body, bytes bodyLen, HttpResponse *resp) {
    if (!url || !method || !resp)
        return false;
    utf8 scheme[16] = { 0 };
    utf8 host[256] = { 0 };
    utf8 path[512] = { 0 };
    i32 port = 80;
    if (!parseUrl(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)))
        return false;
    utf8 authValue[512] = { 0 };
    string authName = nullptr;
    HttpHeader headers[3];
    u32 headerCount = 0;
    headers[headerCount].name = "Content-Type";
    headers[headerCount].value = "application/json";
    headerCount++;
    if (auth && ApiAuth_kind(auth) != API_AUTH_NONE) {
        if (!ApiAuth_apply(auth, &authName, authValue, sizeof(authValue)))
            return false;
        headers[headerCount].name = authName;
        headers[headerCount].value = authValue;
        headerCount++;
    }
    headers[headerCount].name = "Connection";
    headers[headerCount].value = "close";
    headerCount++;
    HttpRequest req = {
        .scheme = scheme,
        .method = method,
        .host = host,
        .port = port,
        .path = path,
        .headers = headers,
        .headerCount = headerCount,
        .body = body,
        .bodyLen = bodyLen,
        .timeoutMs = 5000
    };
    if (!Http_perform(&req, resp))
        return false;
    return statusIsSuccess(HttpResponse_status(resp));
}

/** CORE FUNCTIONS */
/** Sends a JSON request body and stores the transport result in resp. */
bool Rest_postJson(string url, const ApiAuth *auth,
                   string jsonBody, bytes jsonLen, HttpResponse *resp) {
    return perform(url, auth, "POST", jsonBody, jsonLen, resp);
}

/** Sends an authenticated or unauthenticated GET and stores the result in resp. */
bool Rest_get(string url, const ApiAuth *auth, HttpResponse *resp) {
    return perform(url, auth, "GET", nullptr, 0, resp);
}
