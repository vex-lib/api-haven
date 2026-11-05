#ifndef API_AUTH_H
#define API_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// api/auth.h — credential contract for every api-haven driver (Shape B/C/D).
//
// One struct describes how a request authenticates; drivers never hand-roll
// Authorization headers again. Zero allocation: credentials are borrowed
// pointers (stable storage owned by the caller), the rendered header value
// lands in a caller-owned buffer.
//
// Kinds:
//   NONE      — no header (public endpoints, loopback proofs).
//   API_KEY   — custom header name + raw value (pinecone "Api-Key", etc).
//   BEARER    — "Authorization: Bearer <token>" (supabase, AI APIs, ...).
//   BEARER_FN — token pulled from a callback each call (OAuth2 refresh in
//               oauth/token.h lands here later; the token must outlive the
//               HTTP exchange — static or pool storage, never a stack temp).

typedef enum ApiAuthKind {
    API_AUTH_NONE = 0,
    API_AUTH_API_KEY,
    API_AUTH_BEARER,
    API_AUTH_BEARER_FN,
} ApiAuthKind;

// Returns the current bearer token. Must return stable storage.
typedef const char *(*ApiTokenFn)(void *userdata);

typedef struct ApiAuth ApiAuth;

struct ApiAuth {
    ApiAuthKind kind;
    const char *headerName;   // API_KEY only (e.g. "Api-Key"); else nullptr
    const char *credential;   // API_KEY value / BEARER token; else nullptr
    ApiTokenFn tokenFn;       // BEARER_FN only; else nullptr
    void *tokenUserdata;      // BEARER_FN only; else nullptr
};

// --- Constructors (borrowed pointers, no allocation) ---
ApiAuth ApiAuth_none(void);
ApiAuth ApiAuth_apiKey(const char *headerName, const char *key);
ApiAuth ApiAuth_bearer(const char *token);
ApiAuth ApiAuth_bearerFn(ApiTokenFn fn, void *userdata);

// --- Core ---
// Renders the header for one request. Returns false when kind is NONE
// (no header) or the credential is missing. On true, nameOut points at
// the header name (borrowed) and valueBuf holds the NUL-terminated value.
bool ApiAuth_apply(const ApiAuth *auth, const char **nameOut,
                   char *valueBuf, size_t valueCap);

#endif
