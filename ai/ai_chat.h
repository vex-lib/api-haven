#ifndef AI_CHAT_H
#define AI_CHAT_H

#include "vex.h"
#include "ai/ai_provider.h"
#include "api/auth.h"
#include "net/http.h"

/**
 * ai/ai_chat.h, OpenAI-compatible chat completion client (L2 behavior).
 *
 * Dogfoods the AiProvider directory: resolves the provider's native
 * endpoint (or family default), renders the OpenAI envelope into a
 * caller-owned body buffer, and ships it through the shared REST core.
 * Anthropic-envelope providers get their own class (ai_chat_anthropic),
 * same shape, different wire contract.
 */

/**
 * SLOT RECORD (owned by AiChat, Rule 3 co-location): one chat turn.
 *
 * string role    - "system" | "user" | "assistant"
 * string content - message body
 */
typedef struct AiMessage {
    string role;
    string content;
} AiMessage;

/**
 * const AiProvider *provider  - directory handle; nullptr = shared default
 * const AiProviderSlot *peer  - selected provider row; nullptr = gateway
 * string model           - e.g. "grok-2-latest"; must be set
 * string apiKey          - Bearer credential; nullptr = no auth
 * string baseUrlOverride - wins over peer base / family default
 */
typedef struct AiChat {
    const AiProvider *provider;
    const AiProviderSlot *peer;
    string model;
    string apiKey;
    string baseUrlOverride;
} AiChat;

/** --- Constructors (value structs, no allocation, ApiAuth precedent) --- */
AiChat AiChat_0(); /** everything nullptr */
AiChat AiChat_1(string model); /** shared table, gateway default */
AiChat AiChat_2(string model, string apiKey);
#define AiChat(...) CONSTRUCTOR_DISPATCH(AiChat, __VA_ARGS__)

/**
 * --- Core functions ---
 * Renders the request without sending: OpenAI envelope into bodyBuf,
 * final URL into urlBuf, credential into authOut. dest-last per Rule 9.
 * Everything is caller-owned scratch; fast enough for build-mode reuse.
 */
bool AiChat_buildRequest(const AiChat *self, const AiMessage *msgs, u32 msgCount,
                         utf8 *bodyBuf, bytes bodyCap,
                         utf8 *urlBuf, bytes urlCap,
                         ApiAuth *authOut);
/**
 * buildRequest + Rest_postJson through the shared REST core.
 * resp is caller-owned (see net/http.h). No network in tests, use
 * AiChat_buildRequest for headless verification.
 */
bool AiChat_complete(const AiChat *self, const AiMessage *msgs, u32 msgCount,
                     utf8 *bodyBuf, bytes bodyCap,
                     HttpResponse *resp);

/** --- Setters --- */
fn AiChat_setProvider(AiChat *self, const AiProvider *provider);
fn AiChat_setPeer(AiChat *self, const AiProviderSlot *peer);
fn AiChat_setModel(AiChat *self, string model);
fn AiChat_setApiKey(AiChat *self, string apiKey);
fn AiChat_setBaseUrl(AiChat *self, string baseUrlOverride);

/** --- Getters (Rule 24, null-safe) --- */
const AiProvider *AiChat_getProvider(const AiChat *self);
const AiProviderSlot *AiChat_getPeer(const AiChat *self);
string AiChat_getModel(const AiChat *self);
string AiChat_getApiKey(const AiChat *self);
string AiChat_getBaseUrl(const AiChat *self);

#endif
