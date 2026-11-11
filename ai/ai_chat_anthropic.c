#include "annotation/annotation.h"
#include "ai/ai_chat_anthropic.h"
#include "api/rest.h"
#include "net/json.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/** The Anthropic Messages client: the same request shape as AiChat, rendered
 * into a different wire contract. It exists so the provider row decides the
 * family rather than the caller, and it borrows AiMessage from ai_chat.h
 * instead of declaring a second turn record. Model, max_tokens and messages[]
 * are written into caller-owned scratch, auth rides x-api-key, and the class
 * holds no state across calls. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiChatAnthropic (ai/ai_chat_anthropic)
 * getters; consumes the L1 AiProvider directory + api/rest core)
 * ============================================================================
 * Anthropic Messages chat client — Shape-A sister of AiChat. Resolves
 * the selected provider row to a native endpoint (Anthropic family
 * default https://api.anthropic.com/v1 when the row carries no verified
 * base), renders the Messages envelope (model, max_tokens, messages[])
 * into caller-owned scratch, and sends it via Rest_postJson. Auth rides
 * the x-api-key header. Zero allocation on every path. The AiMessage
 * turn record is borrowed from ai/ai_chat.h (slot-record reuse — no
 * second turn struct, Rule 3 stays green).
 *
 * STRUCT FIELDS (Mirroring ai/ai_chat_anthropic.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   const AiProvider *provider;  // directory handle; NULL = shared default
 *   const AiProviderSlot *peer;  // selected provider row; NULL = gateway
 *   const char *model;           // e.g. "claude-sonnet-4-5"; must be set
 *   const char *apiKey;          // x-api-key credential; NULL = no auth
 *   const char *baseUrlOverride; // wins over peer base / family default
 *   u32 maxTokens;          // token ceiling (default 1024)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs — ApiAuth precedent):
 *   - AiChatAnthropic_0()
 *   - AiChatAnthropic_1(model)
 *   - AiChatAnthropic_2(model, apiKey)
 *
 * Core Functions:
 *   - AiChatAnthropic_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
 *                                  urlBuf, urlCap, authOut) : render-only
 *   - AiChatAnthropic_complete(self, msgs, msgCount, bodyBuf, bodyCap, resp)
 *                                                    : render + send
 *
 * Setters:
 *   - AiChatAnthropic_setProvider / setPeer / setModel / setApiKey /
 *     setBaseUrl / setMaxTokens
 *
 * Getters:
 *   - AiChatAnthropic_getProvider / getPeer / getModel / getApiKey /
 *     getBaseUrl / getMaxTokens
 * ============================================================================
 */
;;INTENTION("Anthropic Messages gets its own class pair — promised by the ;;INTENTION in ai_chat.c; a mode flag on AiChat would fake the wire contract and the two-layer cap")

// --- static helpers ---------------------------------------------------------

/** Appends a literal and terminator when the remaining output capacity permits. */
static bool appendLiteral(char *out, bytes cap, bytes *used, const char *s) {
    const bytes n = strlen(s);
    if (!out || !used || *used + n + 1 > cap)
        return false;
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

/** Appends one JSON-escaped string to the current bounded output position. */
static bool appendJsonString(char *out, bytes cap, bytes *used, const char *s) {
    const int64_t w = Json_writeString(out + *used, cap - *used, s);
    if (w < 0)
        return false;
    *used += (bytes)w;
    return true;
}

// CONSTRUCTORS

/** Returns a cleared Anthropic chat value with the default token limit. */
AiChatAnthropic AiChatAnthropic_0(void) {
    AiChatAnthropic chat;
    memset(&chat, 0, sizeof(chat));
    chat.maxTokens = AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS;
    return chat;
}

/** Initializes an Anthropic chat value with a model and shared provider directory. */
AiChatAnthropic AiChatAnthropic_1(const char *model) {
    AiChatAnthropic chat = AiChatAnthropic();
    // provider borrows the shared singleton so directory calls are legal.
    chat.provider = AiProvider_shared();
    chat.model = model;
    return chat;
}

/** Initializes an Anthropic chat value with a model and borrowed API key. */
AiChatAnthropic AiChatAnthropic_2(const char *model, const char *apiKey) {
    AiChatAnthropic chat = AiChatAnthropic(model);
    chat.apiKey = apiKey;
    return chat;
}

// CORE FUNCTIONS

/** Renders the Anthropic Messages URL, body, and x-api-key auth descriptor. */
bool AiChatAnthropic_buildRequest(const AiChatAnthropic *self, const AiMessage *msgs,
                                  u32 msgCount, char *bodyBuf, bytes bodyCap,
                                  char *urlBuf, bytes urlCap, ApiAuth *authOut) {
    if (!self || !bodyBuf || !urlBuf || !authOut || !(*self).model)
        return false;
    if (!msgs && msgCount > 0)
        return false;
    if (urlCap == 0 || bodyCap == 0)
        return false;
    if ((*self).maxTokens == 0)
        return false;

    // Resolve endpoint: override > peer native base > Anthropic default.
    const char *base = "https://api.anthropic.com/v1";
    if ((*self).baseUrlOverride)
        base = (*self).baseUrlOverride;
    else if ((*self).peer) {
        const AiProvider *table = (*self).provider ? (*self).provider : AiProvider_shared();
        const char *resolved = AiProvider_resolveBaseUrl(table, (*self).peer);
        if (resolved)
            base = resolved;
    }

    const int printed = snprintf(urlBuf, urlCap, "%s/v1/messages", base);
    if (printed < 0 || (bytes)printed >= urlCap)
        return false;

    // Credential: Anthropic wire takes x-api-key, never Bearer.
    if ((*self).apiKey)
        (*authOut) = ApiAuth_apiKey("x-api-key", (*self).apiKey);
    else
        (*authOut) = ApiAuth_none();

    // Render {"model":"...","max_tokens":N,"messages":[{...},...]}
    bytes used = 0;
    if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"model\":"))
        return false;
    if (!appendJsonString(bodyBuf, bodyCap, &used, (*self).model))
        return false;
    char tokenBuf[32];
    const int tw = snprintf(tokenBuf, sizeof(tokenBuf), ",\"max_tokens\":%u,\"messages\":[",
                            (*self).maxTokens);
    if (tw < 0 || (bytes)tw >= sizeof(tokenBuf))
        return false;
    if (!appendLiteral(bodyBuf, bodyCap, &used, tokenBuf))
        return false;
    for (u32 i = 0; i < msgCount; i++) {
        const AiMessage *m = &msgs[i];
        if (i > 0 && !appendLiteral(bodyBuf, bodyCap, &used, ","))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"role\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, (*m).role))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, ",\"content\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, (*m).content))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "}"))
            return false;
    }
    if (!appendLiteral(bodyBuf, bodyCap, &used, "]}"))
        return false;
    return true;
}

/** Builds and sends an Anthropic Messages request through the REST core. */
bool AiChatAnthropic_complete(const AiChatAnthropic *self, const AiMessage *msgs,
                               u32 msgCount, char *bodyBuf, bytes bodyCap,
                               HttpResponse *resp) {
    if (!resp)
        return false;
    char urlBuf[512];
    ApiAuth auth = ApiAuth_none();
    if (!AiChatAnthropic_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
                                      urlBuf, sizeof(urlBuf), &auth))
        return false;
    return Rest_postJson(urlBuf, &auth, bodyBuf, strlen(bodyBuf), resp);
}

// SETTERS

/** Sets the borrowed provider directory used to resolve the selected peer. */
void AiChatAnthropic_setProvider(AiChatAnthropic *self, const AiProvider *provider) {
    if (self)
        (*self).provider = provider;
}

/** Selects the borrowed provider row for endpoint resolution. */
void AiChatAnthropic_setPeer(AiChatAnthropic *self, const AiProviderSlot *peer) {
    if (self)
        (*self).peer = peer;
}

/** Sets the borrowed model identifier used in the request envelope. */
void AiChatAnthropic_setModel(AiChatAnthropic *self, const char *model) {
    if (self)
        (*self).model = model;
}

/** Sets the borrowed x-api-key credential, or nullptr to omit authentication. */
void AiChatAnthropic_setApiKey(AiChatAnthropic *self, const char *apiKey) {
    if (self)
        (*self).apiKey = apiKey;
}

/** Sets an optional borrowed endpoint override. */
void AiChatAnthropic_setBaseUrl(AiChatAnthropic *self, const char *baseUrlOverride) {
    if (self)
        (*self).baseUrlOverride = baseUrlOverride;
}

/** Sets the request's maximum token count. */
void AiChatAnthropic_setMaxTokens(AiChatAnthropic *self, u32 maxTokens) {
    if (self)
        (*self).maxTokens = maxTokens;
}

// GETTERS

/** Returns the configured provider directory, or nullptr for a null receiver. */
const AiProvider *AiChatAnthropic_getProvider(const AiChatAnthropic *self) {
    return self ? (*self).provider : nullptr;
}

/** Returns the selected provider row, or nullptr when unset or self is null. */
const AiProviderSlot *AiChatAnthropic_getPeer(const AiChatAnthropic *self) {
    return self ? (*self).peer : nullptr;
}

/** Returns the borrowed model identifier, or nullptr when unset. */
const char *AiChatAnthropic_getModel(const AiChatAnthropic *self) {
    return self ? (*self).model : nullptr;
}

/** Returns the borrowed API key, or nullptr when unset. */
const char *AiChatAnthropic_getApiKey(const AiChatAnthropic *self) {
    return self ? (*self).apiKey : nullptr;
}

/** Returns the endpoint override, or nullptr when unset. */
const char *AiChatAnthropic_getBaseUrl(const AiChatAnthropic *self) {
    return self ? (*self).baseUrlOverride : nullptr;
}

/** Returns the configured token ceiling, or zero for a null receiver. */
u32 AiChatAnthropic_getMaxTokens(const AiChatAnthropic *self) {
    if (!self)
        return 0;
    return (*self).maxTokens;
}
