#ifndef AI_CHAT_ANTHROPIC_H
#define AI_CHAT_ANTHROPIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ai/ai_chat.h"
#include "ai/ai_provider.h"
#include "api/auth.h"
#include "c23/constructor.h"
#include "net/http.h"

// ai/ai_chat_anthropic.h — Anthropic Messages chat client (L2 behavior).
//
// Shape-A sister of AiChat: same buildRequest/complete silhouette, own
// wire contract. Renders the Anthropic envelope (model, max_tokens,
// messages[]) into caller-owned scratch and ships it through
// Rest_postJson. Credentials ride the "x-api-key" header (Anthropic
// scheme). Zero allocation on every path. No mode-flag branches —
// OpenAI envelopes stay in ai_chat, Anthropic envelopes live here.

// Default token ceiling when the caller never sets one.
#define AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS 1024u

typedef struct AiChatAnthropic {
    const AiProvider *provider;  // directory handle; NULL = shared default
    const AiProviderSlot *peer;  // selected provider row; NULL = gateway
    const char *model;           // e.g. "claude-sonnet-4-5"; must be set
    const char *apiKey;          // x-api-key credential; NULL = no auth
    const char *baseUrlOverride; // wins over peer base / family default
    uint32_t maxTokens;          // token ceiling (default 1024)
} AiChatAnthropic;

// --- Constructors (value structs, no allocation — ApiAuth precedent) ---
AiChatAnthropic AiChatAnthropic_0(void);              // everything NULL/0
AiChatAnthropic AiChatAnthropic_1(const char *model); // shared table default
AiChatAnthropic AiChatAnthropic_2(const char *model, const char *apiKey);
#define AiChatAnthropic(...) CONSTRUCTOR_DISPATCH(AiChatAnthropic, __VA_ARGS__)

// --- Core functions ---
// Renders the request without sending: Anthropic envelope into bodyBuf,
// final URL into urlBuf, credential into authOut. dest-last per Rule 9.
// Everything is caller-owned scratch; fast enough for build-mode reuse.
bool AiChatAnthropic_buildRequest(const AiChatAnthropic *self, const AiMessage *msgs,
                                  uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                                  char *urlBuf, size_t urlCap, ApiAuth *authOut);
// buildRequest + Rest_postJson through the shared REST core.
// resp is caller-owned (see net/http.h). No network in tests — use
// AiChatAnthropic_buildRequest for headless verification.
bool AiChatAnthropic_complete(const AiChatAnthropic *self, const AiMessage *msgs,
                               uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                               HttpResponse *resp);

// --- Setters ---
void AiChatAnthropic_setProvider(AiChatAnthropic *self, const AiProvider *provider);
void AiChatAnthropic_setPeer(AiChatAnthropic *self, const AiProviderSlot *peer);
void AiChatAnthropic_setModel(AiChatAnthropic *self, const char *model);
void AiChatAnthropic_setApiKey(AiChatAnthropic *self, const char *apiKey);
void AiChatAnthropic_setBaseUrl(AiChatAnthropic *self, const char *baseUrlOverride);
void AiChatAnthropic_setMaxTokens(AiChatAnthropic *self, uint32_t maxTokens);

// --- Getters (Rule 24, null-safe) ---
const AiProvider *AiChatAnthropic_getProvider(const AiChatAnthropic *self);
const AiProviderSlot *AiChatAnthropic_getPeer(const AiChatAnthropic *self);
const char *AiChatAnthropic_getModel(const AiChatAnthropic *self);
const char *AiChatAnthropic_getApiKey(const AiChatAnthropic *self);
const char *AiChatAnthropic_getBaseUrl(const AiChatAnthropic *self);
uint32_t AiChatAnthropic_getMaxTokens(const AiChatAnthropic *self);

#endif
