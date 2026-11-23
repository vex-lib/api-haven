#include "vex.h"
#include "api/auth.h"

#include <stdio.h>
#include <string.h>


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
 * STRUCT FIELDS (Mirroring api/auth.h, exactly this file's class):
 * ----------------------------------------------------------------------------
 *   ApiAuthKind kind;           // NONE / API_KEY / BEARER / BEARER_FN
 *   string headerName;     // API_KEY header name; else nullptr
 *   string credential;     // API_KEY value / BEARER token; else nullptr
 *   ApiTokenFn tokenFn;         // BEARER_FN callback; else nullptr
 *   any tokenUserdata;        // BEARER_FN userdata; else nullptr
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

/** CONSTRUCTORS */
/** Creates an authentication descriptor that adds no credential header. */
ApiAuth ApiAuth_none() {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_NONE;
    return auth;
}

/** Creates a descriptor for a caller-owned API-key header and value. */
ApiAuth ApiAuth_apiKey(string headerName, string key) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_API_KEY;
    auth.headerName = headerName;
    auth.credential = key;
    return auth;
}

/** Creates a descriptor for a caller-owned bearer token. */
ApiAuth ApiAuth_bearer(string token) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_BEARER;
    auth.credential = token;
    return auth;
}

/** Creates a bearer descriptor whose token is obtained from the supplied callback. */
ApiAuth ApiAuth_bearerFn(ApiTokenFn fn, any userdata) {
    ApiAuth auth = { 0 };
    auth.kind = API_AUTH_BEARER_FN;
    auth.tokenFn = fn;
    auth.tokenUserdata = userdata;
    return auth;
}

/** CORE FUNCTIONS */
;;GETTER
static inline ApiAuthKind ApiAuth_kind(const ApiAuth *self)
{
    return (*self).kind;
}
;;GETTER
static inline string ApiAuth_headerName(const ApiAuth *self)
{
    return (*self).headerName;
}
;;GETTER
static inline string ApiAuth_credential(const ApiAuth *self)
{
    return (*self).credential;
}
;;GETTER
static inline ApiTokenFn ApiAuth_tokenFn(const ApiAuth *self)
{
    return (*self).tokenFn;
}
;;GETTER
static inline any ApiAuth_tokenUserdata(const ApiAuth *self)
{
    return (*self).tokenUserdata;
}

/** Copies the selected credential into valueBuf and returns its header name. */
bool ApiAuth_apply(const ApiAuth *auth, string *nameOut,
                   utf8 *valueBuf, bytes valueCap) {
    if (!auth || !nameOut || !valueBuf || valueCap == 0)
        return false;
    ApiAuthKind kind = ApiAuth_kind(auth);
    if (kind == API_AUTH_NONE)
        return false;
    if (kind == API_AUTH_API_KEY) {
        string headerName = ApiAuth_headerName(auth);
        string credential = ApiAuth_credential(auth);
        if (!headerName || !credential)
            return false;
        bytes n = strlen(credential);
        if (n + 1 > valueCap)
            return false;
        memcpy(valueBuf, credential, n + 1);
        *nameOut = headerName;
        return true;
    }
    string token = nullptr;
    if (kind == API_AUTH_BEARER)
        token = ApiAuth_credential(auth);
    else if (kind == API_AUTH_BEARER_FN && ApiAuth_tokenFn(auth))
        token = ApiAuth_tokenFn(auth)(ApiAuth_tokenUserdata(auth));
    if (!token)
        return false;
    i32 n = snprintf(valueBuf, valueCap, "Bearer %s", token);
    if (n < 0 || (bytes)n >= valueCap)
        return false;
    *nameOut = "Authorization";
    return true;
}
