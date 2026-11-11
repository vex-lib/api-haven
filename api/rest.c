#include "api/rest.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/annotation.h"
#include "net/url.h"

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
 * STRUCT FIELDS: none — procedural core over HttpRequest/HttpResponse.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Rest_postJson(url, auth, jsonBody, jsonLen, resp)
 *   - Rest_get(url, auth, resp)
 * ============================================================================
 */

/** Parses the URL into bounded scheme, host, port, and path outputs. */
static bool parseUrl(const char *url, char *scheme, bytes schemeCap,
                     char *host, bytes hostCap, int *port,
                     char *path, bytes pathCap) {
    if (!url || url[0] == '\0')
        return false;
    const char *p = url;
    const char *schemeEnd = strstr(p, "://");
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
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
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
static bool statusIsSuccess(int status) {
    return status >= 200 && status < 300;
}

/** Builds and performs one bounded JSON HTTP request using the shared transport. */
static bool perform(const char *url, const ApiAuth *auth, const char *method,
                    const char *body, bytes bodyLen, HttpResponse *resp) {
    if (!url || !method || !resp)
        return false;
    char scheme[16] = { 0 };
    char host[256] = { 0 };
    char path[512] = { 0 };
    int port = 80;
    if (!parseUrl(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)))
        return false;
    char authValue[512] = { 0 };
    const char *authName = nullptr;
    HttpHeader headers[3];
    u32 headerCount = 0;
    headers[headerCount].name = "Content-Type";
    headers[headerCount].value = "application/json";
    headerCount++;
    // Fail closed: an explicitly required credential (kind != NONE) that cannot be
    // rendered must not silently become an unauthenticated request. A nullptr auth,
    // or NONE, is an intentional public request and carries no header.
    if (auth && (*auth).kind != API_AUTH_NONE) {
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
    // The transport reports only that an exchange completed; the application
    // result is the status. On failure the caller's resp keeps the transferred
    // status/body so a 4xx/5xx error body can still be read.
    if (!Http_perform(&req, resp))
        return false;
    return statusIsSuccess((*resp).status);
}

// CORE FUNCTIONS
/** Sends a JSON request body and stores the transport result in resp. */
bool Rest_postJson(const char *url, const ApiAuth *auth,
                   const char *jsonBody, bytes jsonLen, HttpResponse *resp) {
    return perform(url, auth, "POST", jsonBody, jsonLen, resp);
}

/** Sends an authenticated or unauthenticated GET and stores the result in resp. */
bool Rest_get(const char *url, const ApiAuth *auth, HttpResponse *resp) {
    return perform(url, auth, "GET", nullptr, 0, resp);
}
