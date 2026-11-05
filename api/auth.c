#include "api/auth.h"

#include <stdio.h>
#include <string.h>

#include "annotation/annotation.h"

;;DEFINITION
/** One request's authentication, in one struct. It exists so the Rest core never
 * grows a per-vendor header builder: a driver fills ApiAuth and the core renders
 * it. Every string is borrowed from caller-owned stable storage and rendering
 * writes into the caller's buffer, so the class owns no memory and frees
 * nothing. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ApiAuth (api/auth)
 * ============================================================================
 * How any request authenticates, in one struct. Drivers pass an ApiAuth to
 * the Rest core instead of hand-rolling Authorization headers. All strings
 * are borrowed (caller-owned stable storage); rendering is allocation-free
 * into a caller buffer.
 *
 * STRUCT FIELDS (Mirroring api/auth.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   ApiAuthKind kind;           // NONE / API_KEY / BEARER / BEARER_FN
 *   const char *headerName;     // API_KEY header name; else nullptr
 *   const char *credential;     // API_KEY value / BEARER token; else nullptr
 *   ApiTokenFn tokenFn;         // BEARER_FN callback; else nullptr
 *   void *tokenUserdata;        // BEARER_FN userdata; else nullptr
 *
 * PRIVATE HELPERS: None.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - ApiAuth_none()
 *   - ApiAuth_apiKey(headerName, key)
 *   - ApiAuth_bearer(token)
 *   - ApiAuth_bearerFn(fn, userdata)
 *
 * Core Functions:
 *   - ApiAuth_apply(auth, nameOut, valueBuf, valueCap)
 * ============================================================================
 */

// CONSTRUCTORS
/** Creates an authentication descriptor that adds no credential header. */
ApiAuth ApiAuth_none(void) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_NONE;
    return auth;
}

/** Creates a descriptor for a caller-owned API-key header and value. */
ApiAuth ApiAuth_apiKey(const char *headerName, const char *key) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_API_KEY;
    auth.headerName = headerName;
    auth.credential = key;
    return auth;
}

/** Creates a descriptor for a caller-owned bearer token. */
ApiAuth ApiAuth_bearer(const char *token) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_BEARER;
    auth.credential = token;
    return auth;
}

/** Creates a bearer descriptor whose token is obtained from the supplied callback. */
ApiAuth ApiAuth_bearerFn(ApiTokenFn fn, void *userdata) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_BEARER_FN;
    auth.tokenFn = fn;
    auth.tokenUserdata = userdata;
    return auth;
}

// CORE FUNCTIONS
/** Copies the selected credential into valueBuf and returns its header name. */
bool ApiAuth_apply(const ApiAuth *auth, const char **nameOut,
                   char *valueBuf, size_t valueCap) {
    if (!auth || !nameOut || !valueBuf || valueCap == 0)
        return false;
    if ((*auth).kind == API_AUTH_NONE)
        return false;
    if ((*auth).kind == API_AUTH_API_KEY) {
        if (!(*auth).headerName || !(*auth).credential)
            return false;
        size_t n = strlen((*auth).credential);
        if (n + 1 > valueCap)
            return false;
        memcpy(valueBuf, (*auth).credential, n + 1);
        *nameOut = (*auth).headerName;
        return true;
    }
    const char *token = nullptr;
    if ((*auth).kind == API_AUTH_BEARER)
        token = (*auth).credential;
    else if ((*auth).kind == API_AUTH_BEARER_FN && (*auth).tokenFn)
        token = (*auth).tokenFn((*auth).tokenUserdata);
    if (!token)
        return false;
    int n = snprintf(valueBuf, valueCap, "Bearer %s", token);
    if (n < 0 || (size_t)n >= valueCap)
        return false;
    *nameOut = "Authorization";
    return true;
}
