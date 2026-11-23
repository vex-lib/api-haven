#ifndef AI_CHAT_GEMINI_H
#define AI_CHAT_GEMINI_H

#include "vex.h"
#include "ai/ai_chat.h"
#include "ai/ai_provider.h"
#include "api/auth.h"
#include "net/http.h"

/**
 * ai/ai_chat_gemini.h, Gemini generateContent chat client (L2 behavior).
 *
 * Shape-A sister of AiChat: same buildRequest/complete silhouette, own
 * wire contract. Renders the Gemini envelope (contents[] of role +
 * parts[].text) into caller-owned scratch with the model riding the URL
 * (.../v1beta/models/<model>:generateContent) and ships it through
 * Rest_postJson. Credentials ride the "x-goog-api-key" header. Role map:
 * "assistant" becomes "model", "system" folds into "user" (Gemini keeps
 * system prompts in systemInstruction, out of scope here). Zero
 * allocation on every path. No mode-flag branches, OpenAI envelopes
 * stay in ai_chat, Gemini envelopes live here.
 */

/**
 * const AiProvider *provider  - directory handle; nullptr = shared default
 * const AiProviderSlot *peer  - selected provider row; nullptr = gateway
 * string model           - e.g. "gemini-2.0-flash"; must be set
 * string apiKey          - x-goog-api-key credential; nullptr = no auth
 * string baseUrlOverride - wins over peer base / family default
 */
typedef struct AiChatGemini {
    const AiProvider *provider;
    const AiProviderSlot *peer;
    string model;
    string apiKey;
    string baseUrlOverride;
} AiChatGemini;

/** --- Constructors (value structs, no allocation, ApiAuth precedent) --- */
AiChatGemini AiChatGemini_0(); /** everything nullptr */
AiChatGemini AiChatGemini_1(string model); /** shared table default */
AiChatGemini AiChatGemini_2(string model, string apiKey);
#define AiChatGemini(...) CONSTRUCTOR_DISPATCH(AiChatGemini, __VA_ARGS__)

/**
 * --- Core functions ---
 * Renders the request without sending: Gemini envelope into bodyBuf,
 * final URL into urlBuf, credential into authOut. dest-last per Rule 9.
 * Everything is caller-owned scratch; fast enough for build-mode reuse.
 */
bool AiChatGemini_buildRequest(const AiChatGemini *self, const AiMessage *msgs,
                               u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                               utf8 *urlBuf, bytes urlCap, ApiAuth *authOut);
/**
 * buildRequest + Rest_postJson through the shared REST core.
 * resp is caller-owned (see net/http.h). No network in tests, use
 * AiChatGemini_buildRequest for headless verification.
 */
bool AiChatGemini_complete(const AiChatGemini *self, const AiMessage *msgs,
                            u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                            HttpResponse *resp);

/** --- Setters --- */
fn AiChatGemini_setProvider(AiChatGemini *self, const AiProvider *provider);
fn AiChatGemini_setPeer(AiChatGemini *self, const AiProviderSlot *peer);
fn AiChatGemini_setModel(AiChatGemini *self, string model);
fn AiChatGemini_setApiKey(AiChatGemini *self, string apiKey);
fn AiChatGemini_setBaseUrl(AiChatGemini *self, string baseUrlOverride);

/** --- Getters (Rule 24, null-safe) --- */
const AiProvider *AiChatGemini_getProvider(const AiChatGemini *self);
const AiProviderSlot *AiChatGemini_getPeer(const AiChatGemini *self);
string AiChatGemini_getModel(const AiChatGemini *self);
string AiChatGemini_getApiKey(const AiChatGemini *self);
string AiChatGemini_getBaseUrl(const AiChatGemini *self);

#endif
