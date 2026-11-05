#ifndef AI_CHAT_H
#define AI_CHAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ai/ai_provider.h"
#include "api/auth.h"
#include "c23/constructor.h"
#include "net/http.h"

// ai/ai_chat.h — OpenAI-compatible chat completion client (L2 behavior).
//
// Dogfoods the AiProvider directory: resolves the provider's native
// endpoint (or family default), renders the OpenAI envelope into a
// caller-owned body buffer, and ships it through the shared REST core.
// Anthropic-envelope providers get their own class (ai_chat_anthropic),
// same shape, different wire contract.

// SLOT RECORD (owned by AiChat, Rule 3 co-location): one chat turn.
typedef struct AiMessage {
    const char *role;    // "system" | "user" | "assistant"
    const char *content; // message body
} AiMessage;

typedef struct AiChat {
    const AiProvider *provider;  // directory handle; NULL = shared default
    const AiProviderSlot *peer;  // selected provider row; NULL = gateway
    const char *model;           // e.g. "grok-2-latest"; must be set
    const char *apiKey;          // Bearer credential; NULL = no auth
    const char *baseUrlOverride; // wins over peer base / family default
} AiChat;

// --- Constructors (value structs, no allocation — ApiAuth precedent) ---
AiChat AiChat_0(void);                       // everything NULL
AiChat AiChat_1(const char *model);          // shared table, gateway default
AiChat AiChat_2(const char *model, const char *apiKey);
#define AiChat(...) CONSTRUCTOR_DISPATCH(AiChat, __VA_ARGS__)

// --- Core functions ---
// Renders the request without sending: OpenAI envelope into bodyBuf,
// final URL into urlBuf, credential into authOut. dest-last per Rule 9.
// Everything is caller-owned scratch; fast enough for build-mode reuse.
bool AiChat_buildRequest(const AiChat *self, const AiMessage *msgs, uint32_t msgCount,
                         char *bodyBuf, size_t bodyCap,
                         char *urlBuf, size_t urlCap,
                         ApiAuth *authOut);
// buildRequest + Rest_postJson through the shared REST core.
// resp is caller-owned (see net/http.h). No network in tests — use
// AiChat_buildRequest for headless verification.
bool AiChat_complete(const AiChat *self, const AiMessage *msgs, uint32_t msgCount,
                     char *bodyBuf, size_t bodyCap,
                     HttpResponse *resp);

// --- Setters ---
void AiChat_setProvider(AiChat *self, const AiProvider *provider);
void AiChat_setPeer(AiChat *self, const AiProviderSlot *peer);
void AiChat_setModel(AiChat *self, const char *model);
void AiChat_setApiKey(AiChat *self, const char *apiKey);
void AiChat_setBaseUrl(AiChat *self, const char *baseUrlOverride);

// --- Getters (Rule 24, null-safe) ---
const AiProvider *AiChat_getProvider(const AiChat *self);
const AiProviderSlot *AiChat_getPeer(const AiChat *self);
const char *AiChat_getModel(const AiChat *self);
const char *AiChat_getApiKey(const AiChat *self);
const char *AiChat_getBaseUrl(const AiChat *self);

#endif
