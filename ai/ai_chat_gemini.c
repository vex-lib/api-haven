#include "vex.h"
#include "ai/ai_chat_gemini.h"
#include "api/rest.h"
#include "net/json.h"
#include "re/primitive/vex-string.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/** The Gemini generateContent client: the same request shape as AiChat, with
 * the model riding the URL instead of the body and the role vocabulary
 * translated on the way out ("assistant" becomes "model", "system" folds into
 * "user"). It borrows AiMessage from ai_chat.h rather than declaring a second
 * turn record, renders into caller-owned scratch, authenticates with
 * x-goog-api-key, and retains nothing between calls. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiChatGemini (ai/ai_chat_gemini)
 * getters; consumes the L1 AiProvider directory + api/rest core)
 * ============================================================================
 * Gemini generateContent chat client, Shape-A sister of AiChat. Resolves
 * the selected provider row to a native endpoint (Google default
 * https://generativelanguage.googleapis.com when the row carries no
 * verified base), renders the generateContent envelope (contents[] of
 * role + parts[].text) into caller-owned scratch with the model riding
 * the URL (.../v1beta/models/<model>:generateContent), and sends it via
 * Rest_postJson. Auth rides the x-goog-api-key header. Role map:
 * "assistant" becomes "model", "system" folds into "user". Zero
 * allocation on every path. The AiMessage turn record is borrowed from
 * ai/ai_chat.h (slot-record reuse, no second turn struct, Rule 3
 * stays green).
 *
 * STRUCT FIELDS (Mirroring ai/ai_chat_gemini.h, exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   const AiProvider *provider;  // directory handle; nullptr = shared default
 *   const AiProviderSlot *peer;  // selected provider row; nullptr = gateway
 *   string model;           // e.g. "gemini-2.0-flash"; must be set
 *   string apiKey;          // x-goog-api-key credential; nullptr = no auth
 *   string baseUrlOverride; // wins over peer base / family default
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs, ApiAuth precedent):
 *   - AiChatGemini_0()
 *   - AiChatGemini_1(model)
 *   - AiChatGemini_2(model, apiKey)
 *
 * Core Functions:
 *   - AiChatGemini_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
 *                               urlBuf, urlCap, authOut) : render-only
 *   - AiChatGemini_complete(self, msgs, msgCount, bodyBuf, bodyCap, resp)
 *                                                    : render + send
 *
 * Setters:
 *   - AiChatGemini_setProvider / setPeer / setModel / setApiKey /
 *     setBaseUrl
 *
 * Getters:
 *   - AiChatGemini_getProvider / getPeer / getModel / getApiKey /
 *     getBaseUrl
 * ============================================================================
 */
;;INTENTION("Gemini generateContent gets its own class pair, same Shape-A reasoning as the Anthropic sister; the role map keeps system prompts sendable without a second envelope struct")

/** --- static helpers --------------------------------------------------------- */

/** Appends a literal and terminator when the remaining output capacity permits. */
static bool appendLiteral(utf8 *out, bytes cap, bytes *used, string s) {
    const bytes n = strlen(s);
    if (!out || !used || *used + n + 1 > cap)
        return false;
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

/** Appends one JSON-escaped string to the current bounded output position. */
static bool appendJsonString(utf8 *out, bytes cap, bytes *used, string s) {
    const i64 w = Json_writeString(out + *used, cap - *used, s);
    if (w < 0)
        return false;
    *used += (bytes)w;
    return true;
}

/** Gemini content role: assistant becomes model, system folds to user. */
/** Maps chat roles to Gemini's user/model vocabulary. */
static string mapRole(string role) {
    if (!role)
        return "user";
    string token = role;
    if (string_equals(token, "assistant"))
        return "model";
    if (string_equals(token, "system"))
        return "user";
    return role;
}

/** CONSTRUCTORS */

/** Returns an AiChatGemini value with all fields cleared. */
AiChatGemini AiChatGemini_0() {
    AiChatGemini chat;
    memset(&chat, 0, sizeof(chat));
    return chat;
}

/** Initializes a Gemini chat value with a model and shared provider directory. */
AiChatGemini AiChatGemini_1(string model) {
    AiChatGemini chat = AiChatGemini();
    chat.provider = AiProvider_shared();
    chat.model = model;
    return chat;
}

/** Initializes a Gemini chat value with a model and borrowed API key. */
AiChatGemini AiChatGemini_2(string model, string apiKey) {
    AiChatGemini chat = AiChatGemini(model);
    chat.apiKey = apiKey;
    return chat;
}

/** CORE FUNCTIONS */

;;GETTER
static inline string AiMessage_role(const AiMessage *self)
{
    return (*self).role;
}
;;GETTER
static inline string AiMessage_content(const AiMessage *self)
{
    return (*self).content;
}

/** Renders the Gemini generateContent URL, JSON body, and API-key auth descriptor. */
bool AiChatGemini_buildRequest(const AiChatGemini *self, const AiMessage *msgs,
                               u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                               utf8 *urlBuf, bytes urlCap, ApiAuth *authOut) {
    string model = AiChatGemini_getModel(self);
    if (!self || !bodyBuf || !urlBuf || !authOut || !model)
        return false;
    if (!msgs && msgCount > 0)
        return false;
    if (urlCap == 0 || bodyCap == 0)
        return false;

    string base = "https://generativelanguage.googleapis.com";
    string baseUrlOverride = AiChatGemini_getBaseUrl(self);
    const AiProviderSlot *peer = AiChatGemini_getPeer(self);
    if (baseUrlOverride)
        base = baseUrlOverride;
    else if (peer) {
        const AiProvider *provider = AiChatGemini_getProvider(self);
        const AiProvider *table = provider ? provider : AiProvider_shared();
        string resolved = AiProvider_resolveBaseUrl(table, peer);
        if (resolved)
            base = resolved;
    }

    const i32 printed = snprintf(urlBuf, urlCap, "%s/v1beta/models/%s:generateContent",
                                 base, model);
    if (printed < 0 || (bytes)printed >= urlCap)
        return false;

    string apiKey = AiChatGemini_getApiKey(self);
    if (apiKey)
        (*authOut) = ApiAuth_apiKey("x-goog-api-key", apiKey);
    else
        (*authOut) = ApiAuth_none();

    bytes used = 0;
    if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"contents\":["))
        return false;
    for (u32 i = 0; i < msgCount; i++) {
        const AiMessage *m = &msgs[i];
        if (i > 0 && !appendLiteral(bodyBuf, bodyCap, &used, ","))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"role\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, mapRole(AiMessage_role(m))))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, ",\"parts\":[{\"text\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, AiMessage_content(m)))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "}]}"))
            return false;
    }
    if (!appendLiteral(bodyBuf, bodyCap, &used, "]}"))
        return false;
    return true;
}

/** Builds and sends a Gemini generateContent request through the REST core. */
bool AiChatGemini_complete(const AiChatGemini *self, const AiMessage *msgs,
                            u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                            HttpResponse *resp) {
    if (!resp)
        return false;
    utf8 urlBuf[512];
    ApiAuth auth = ApiAuth_none();
    if (!AiChatGemini_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
                                   urlBuf, sizeof(urlBuf), &auth))
        return false;
    return Rest_postJson(urlBuf, &auth, bodyBuf, strlen(bodyBuf), resp);
}

/** SETTERS */

/** Sets the borrowed provider directory used to resolve the selected peer. */
fn AiChatGemini_setProvider(AiChatGemini *self, const AiProvider *provider) {
    if (self)
        (*self).provider = provider;
}

/** Selects the borrowed provider row for endpoint resolution. */
fn AiChatGemini_setPeer(AiChatGemini *self, const AiProviderSlot *peer) {
    if (self)
        (*self).peer = peer;
}

/** Sets the borrowed model identifier used in the request URL. */
fn AiChatGemini_setModel(AiChatGemini *self, string model) {
    if (self)
        (*self).model = model;
}

/** Sets the borrowed x-goog-api-key credential, or nullptr to omit authentication. */
fn AiChatGemini_setApiKey(AiChatGemini *self, string apiKey) {
    if (self)
        (*self).apiKey = apiKey;
}

/** Sets an optional borrowed endpoint override. */
;;SETTER
fn AiChatGemini_setBaseUrl(AiChatGemini *self, string baseUrlOverride) {
    if (self)
        (*self).baseUrlOverride = baseUrlOverride;
}

/** GETTERS */

/** Returns the configured provider directory, or nullptr for a null receiver. */
;;GETTER
const AiProvider *AiChatGemini_getProvider(const AiChatGemini *self) {
    return self ? (*self).provider : nullptr;
}

/** Returns the selected provider row, or nullptr when unset or self is null. */
;;GETTER
const AiProviderSlot *AiChatGemini_getPeer(const AiChatGemini *self) {
    return self ? (*self).peer : nullptr;
}

/** Returns the borrowed model identifier, or nullptr when unset. */
;;GETTER
string AiChatGemini_getModel(const AiChatGemini *self) {
    return self ? (*self).model : nullptr;
}

/** Returns the borrowed API key, or nullptr when unset. */
;;GETTER
string AiChatGemini_getApiKey(const AiChatGemini *self) {
    return self ? (*self).apiKey : nullptr;
}

/** Returns the endpoint override, or nullptr when unset. */
;;GETTER
string AiChatGemini_getBaseUrl(const AiChatGemini *self) {
    return self ? (*self).baseUrlOverride : nullptr;
}
