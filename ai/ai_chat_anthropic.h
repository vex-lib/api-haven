#ifndef AI_CHAT_ANTHROPIC_H
#define AI_CHAT_ANTHROPIC_H

#include "vex.h"
#include "ai/ai_chat.h"
#include "ai/ai_provider.h"
#include "api/auth.h"
#include "net/http.h"

/**
 * ai/ai_chat_anthropic.h, Anthropic Messages chat client (L2 behavior).
 *
 * Shape-A sister of AiChat: same buildRequest/complete silhouette, own
 * wire contract. Renders the Anthropic envelope (model, max_tokens,
 * messages[]) into caller-owned scratch and ships it through
 * Rest_postJson. Credentials ride the "x-api-key" header (Anthropic
 * scheme). Zero allocation on every path. No mode-flag branches,
 * OpenAI envelopes stay in ai_chat, Anthropic envelopes live here.
 */

/** Default token ceiling when the caller never sets one. */
#define AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS 1024u

/**
 * const AiProvider *provider  - directory handle; nullptr = shared default
 * const AiProviderSlot *peer  - selected provider row; nullptr = gateway
 * string model           - e.g. "claude-sonnet-4-5"; must be set
 * string apiKey          - x-api-key credential; nullptr = no auth
 * string baseUrlOverride - wins over peer base / family default
 * u32 maxTokens               - token ceiling (default 1024)
 */
typedef struct AiChatAnthropic {
    const AiProvider *provider;
    const AiProviderSlot *peer;
    string model;
    string apiKey;
    string baseUrlOverride;
    u32 maxTokens;
} AiChatAnthropic;

/** --- Constructors (value structs, no allocation, ApiAuth precedent) --- */
AiChatAnthropic AiChatAnthropic_0(); /** everything nullptr/0 */
AiChatAnthropic AiChatAnthropic_1(string model); /** shared table default */
AiChatAnthropic AiChatAnthropic_2(string model, string apiKey);
#define AiChatAnthropic(...) CONSTRUCTOR_DISPATCH(AiChatAnthropic, __VA_ARGS__)

/**
 * --- Core functions ---
 * Renders the request without sending: Anthropic envelope into bodyBuf,
 * final URL into urlBuf, credential into authOut. dest-last per Rule 9.
 * Everything is caller-owned scratch; fast enough for build-mode reuse.
 */
bool AiChatAnthropic_buildRequest(const AiChatAnthropic *self, const AiMessage *msgs,
                                  u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                                  utf8 *urlBuf, bytes urlCap, ApiAuth *authOut);
/**
 * buildRequest + Rest_postJson through the shared REST core.
 * resp is caller-owned (see net/http.h). No network in tests, use
 * AiChatAnthropic_buildRequest for headless verification.
 */
bool AiChatAnthropic_complete(const AiChatAnthropic *self, const AiMessage *msgs,
                               u32 msgCount, utf8 *bodyBuf, bytes bodyCap,
                               HttpResponse *resp);

/** --- Setters --- */
fn AiChatAnthropic_setProvider(AiChatAnthropic *self, const AiProvider *provider);
fn AiChatAnthropic_setPeer(AiChatAnthropic *self, const AiProviderSlot *peer);
fn AiChatAnthropic_setModel(AiChatAnthropic *self, string model);
fn AiChatAnthropic_setApiKey(AiChatAnthropic *self, string apiKey);
fn AiChatAnthropic_setBaseUrl(AiChatAnthropic *self, string baseUrlOverride);
fn AiChatAnthropic_setMaxTokens(AiChatAnthropic *self, u32 maxTokens);

/** --- Getters (Rule 24, null-safe) --- */
const AiProvider *AiChatAnthropic_getProvider(const AiChatAnthropic *self);
const AiProviderSlot *AiChatAnthropic_getPeer(const AiChatAnthropic *self);
string AiChatAnthropic_getModel(const AiChatAnthropic *self);
string AiChatAnthropic_getApiKey(const AiChatAnthropic *self);
string AiChatAnthropic_getBaseUrl(const AiChatAnthropic *self);
u32 AiChatAnthropic_getMaxTokens(const AiChatAnthropic *self);

#endif
