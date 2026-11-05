#ifndef AI_CHAT_GEMINI_H
#define AI_CHAT_GEMINI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ai/ai_chat.h"
#include "ai/ai_provider.h"
#include "api/auth.h"
#include "c23/constructor.h"
#include "net/http.h"

// ai/ai_chat_gemini.h — Gemini generateContent chat client (L2 behavior).
//
// Shape-A sister of AiChat: same buildRequest/complete silhouette, own
// wire contract. Renders the Gemini envelope (contents[] of role +
// parts[].text) into caller-owned scratch with the model riding the URL
// (.../v1beta/models/<model>:generateContent) and ships it through
// Rest_postJson. Credentials ride the "x-goog-api-key" header. Role map:
// "assistant" becomes "model", "system" folds into "user" (Gemini keeps
// system prompts in systemInstruction, out of scope here). Zero
// allocation on every path. No mode-flag branches — OpenAI envelopes
// stay in ai_chat, Gemini envelopes live here.

typedef struct AiChatGemini {
    const AiProvider *provider;  // directory handle; NULL = shared default
    const AiProviderSlot *peer;  // selected provider row; NULL = gateway
    const char *model;           // e.g. "gemini-2.0-flash"; must be set
    const char *apiKey;          // x-goog-api-key credential; NULL = no auth
    const char *baseUrlOverride; // wins over peer base / family default
} AiChatGemini;

// --- Constructors (value structs, no allocation — ApiAuth precedent) ---
AiChatGemini AiChatGemini_0(void);              // everything NULL
AiChatGemini AiChatGemini_1(const char *model); // shared table default
AiChatGemini AiChatGemini_2(const char *model, const char *apiKey);
#define AiChatGemini(...) CONSTRUCTOR_DISPATCH(AiChatGemini, __VA_ARGS__)

// --- Core functions ---
// Renders the request without sending: Gemini envelope into bodyBuf,
// final URL into urlBuf, credential into authOut. dest-last per Rule 9.
// Everything is caller-owned scratch; fast enough for build-mode reuse.
bool AiChatGemini_buildRequest(const AiChatGemini *self, const AiMessage *msgs,
                               uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                               char *urlBuf, size_t urlCap, ApiAuth *authOut);
// buildRequest + Rest_postJson through the shared REST core.
// resp is caller-owned (see net/http.h). No network in tests — use
// AiChatGemini_buildRequest for headless verification.
bool AiChatGemini_complete(const AiChatGemini *self, const AiMessage *msgs,
                            uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                            HttpResponse *resp);

// --- Setters ---
void AiChatGemini_setProvider(AiChatGemini *self, const AiProvider *provider);
void AiChatGemini_setPeer(AiChatGemini *self, const AiProviderSlot *peer);
void AiChatGemini_setModel(AiChatGemini *self, const char *model);
void AiChatGemini_setApiKey(AiChatGemini *self, const char *apiKey);
void AiChatGemini_setBaseUrl(AiChatGemini *self, const char *baseUrlOverride);

// --- Getters (Rule 24, null-safe) ---
const AiProvider *AiChatGemini_getProvider(const AiChatGemini *self);
const AiProviderSlot *AiChatGemini_getPeer(const AiChatGemini *self);
const char *AiChatGemini_getModel(const AiChatGemini *self);
const char *AiChatGemini_getApiKey(const AiChatGemini *self);
const char *AiChatGemini_getBaseUrl(const AiChatGemini *self);

#endif
