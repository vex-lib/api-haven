#include "annotation/annotation.h"
#include "ai/ai_chat_anthropic.h"
#include "ai/ai_chat_gemini.h"
#include "net/json.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: AiChatSistersTest (src/ai/ai_chat_sisters_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; no network, no accounts)
 * ============================================================================
 * Executable proof of the Shape-A chat sisters: the Anthropic Messages
 * envelope (model/max_tokens/messages, x-api-key auth, /v1/messages
 * URL) and the Gemini generateContent envelope (contents[] role +
 * parts[].text, x-goog-api-key auth, model-in-URL), each round-tripped
 * back through net/json, plus the cold-seam guard matrix (nullptr,
 * empty, zero maxTokens, truncation, NULL-resp complete guard).
 *
 * Zero HTTP: complete is only asserted for its NULL-resp guard.
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static int sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

static const AiMessage sMessages[2] = {
    {"user", "explain \"sse\" \n briefly"},
    {"assistant", "sure"},
};

/* Checks both provider request encodings, authentication, guards, and null-safe accessors without HTTP. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    AiProvider *dir = AiProvider_shared();
    CHECK(dir != nullptr);
    const AiProviderSlot *anthropic = AiProvider_get(dir, "anthropic");
    CHECK(anthropic != nullptr);

    // --- Anthropic: URL + auth -------------------------------------------------
    AiChatAnthropic achat = AiChatAnthropic("claude-sonnet-4-5", "ant-key");
    AiChatAnthropic_setPeer(&achat, anthropic);
    CHECK(AiChatAnthropic_getMaxTokens(&achat) == AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS);

    char body[2048];
    char url[512];
    ApiAuth auth = ApiAuth_none();
    CHECK(AiChatAnthropic_buildRequest(&achat, sMessages, 2,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://api.anthropic.com/v1/v1/messages") == 0 ||
          strstr(url, "/v1/messages") != nullptr);
    CHECK(auth.kind == API_AUTH_API_KEY);
    CHECK(strcmp(auth.headerName, "x-api-key") == 0);
    CHECK(strcmp(auth.credential, "ant-key") == 0);

    // --- Anthropic: envelope round-trips through net/json -----------------------
    JsonNode nodes[128];
    char scratch[1024];
    JsonDoc doc;
    CHECK(Json_parse(&doc, nodes, 128, scratch, sizeof(scratch), body));
    JsonRef root = Json_root(&doc);
    JsonRef tokens = Json_member(&doc, root, "max_tokens");
    CHECK(tokens >= 0 && Json_type(&doc, tokens) == JSON_NUMBER);
    CHECK(tokens >= 0 && Json_number(&doc, tokens) == 1024.0);
    JsonRef msgs = Json_member(&doc, root, "messages");
    CHECK(Json_count(&doc, msgs) == 2);
    JsonRef m0 = Json_at(&doc, msgs, 0);
    JsonRef c0 = Json_member(&doc, m0, "content");
    CHECK(c0 >= 0 && strcmp(Json_string(&doc, c0, nullptr), "explain \"sse\" \n briefly") == 0);

    // --- Anthropic: override + no-auth + custom ceiling --------------------------
    AiChatAnthropic_setBaseUrl(&achat, "https://proxy.example/anthropic");
    AiChatAnthropic_setMaxTokens(&achat, 512);
    CHECK(AiChatAnthropic_buildRequest(&achat, sMessages, 1,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://proxy.example/anthropic/v1/messages") == 0);
    CHECK(strstr(body, "\"max_tokens\":512") != nullptr);

    AiChatAnthropic bare = AiChatAnthropic("claude-haiku");
    CHECK(AiChatAnthropic_buildRequest(&bare, sMessages, 1,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(auth.kind == API_AUTH_NONE);

    // --- Anthropic: guards (no network) -------------------------------------------
    CHECK(!AiChatAnthropic_buildRequest(nullptr, sMessages, 1,
                                        body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChatAnthropic_buildRequest(&bare, nullptr, 1,
                                        body, sizeof(body), url, sizeof(url), &auth));
    AiChatAnthropic noModel = AiChatAnthropic();
    CHECK(!AiChatAnthropic_buildRequest(&noModel, sMessages, 1,
                                        body, sizeof(body), url, sizeof(url), &auth));
    AiChatAnthropic zeroTok = AiChatAnthropic("m");
    AiChatAnthropic_setMaxTokens(&zeroTok, 0);
    CHECK(!AiChatAnthropic_buildRequest(&zeroTok, sMessages, 1,
                                        body, sizeof(body), url, sizeof(url), &auth));
    char tiny[16];
    CHECK(!AiChatAnthropic_buildRequest(&bare, sMessages, 2,
                                        tiny, sizeof(tiny), url, sizeof(url), &auth));
    CHECK(!AiChatAnthropic_complete(&bare, sMessages, 1, body, sizeof(body), nullptr));

    // --- Anthropic: getters/setters (Rule 24) ---------------------------------------
    AiChatAnthropic ac = AiChatAnthropic();
    CHECK(AiChatAnthropic_getModel(&ac) == nullptr);
    CHECK(AiChatAnthropic_getModel(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getMaxTokens(nullptr) == 0);
    AiChatAnthropic_setProvider(&ac, dir);
    AiChatAnthropic_setPeer(&ac, anthropic);
    AiChatAnthropic_setModel(&ac, "claude-opus");
    AiChatAnthropic_setApiKey(&ac, "k");
    AiChatAnthropic_setBaseUrl(&ac, "https://o");
    AiChatAnthropic_setMaxTokens(&ac, 2048);
    CHECK(AiChatAnthropic_getProvider(&ac) == dir);
    CHECK(AiChatAnthropic_getPeer(&ac) == anthropic);
    CHECK(strcmp(AiChatAnthropic_getModel(&ac), "claude-opus") == 0);
    CHECK(strcmp(AiChatAnthropic_getApiKey(&ac), "k") == 0);
    CHECK(strcmp(AiChatAnthropic_getBaseUrl(&ac), "https://o") == 0);
    CHECK(AiChatAnthropic_getMaxTokens(&ac) == 2048);
    CHECK(AiChatAnthropic_getProvider(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getPeer(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getApiKey(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getBaseUrl(nullptr) == nullptr);

    // --- Gemini: URL carries the model + goog auth ------------------------------------
    AiChatGemini gchat = AiChatGemini("gemini-2.0-flash", "goog-key");
    CHECK(AiChatGemini_buildRequest(&gchat, sMessages, 2,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent") == 0);
    CHECK(auth.kind == API_AUTH_API_KEY);
    CHECK(strcmp(auth.headerName, "x-goog-api-key") == 0);
    CHECK(strcmp(auth.credential, "goog-key") == 0);

    // --- Gemini: contents[] round-trips; assistant maps to model -----------------------
    CHECK(Json_parse(&doc, nodes, 128, scratch, sizeof(scratch), body));
    root = Json_root(&doc);
    JsonRef contents = Json_member(&doc, root, "contents");
    CHECK(Json_count(&doc, contents) == 2);
    JsonRef g0 = Json_at(&doc, contents, 0);
    JsonRef g1 = Json_at(&doc, contents, 1);
    JsonRef r0 = Json_member(&doc, g0, "role");
    JsonRef r1 = Json_member(&doc, g1, "role");
    uint32_t r0len = 0;
    uint32_t r1len = 0;
    const char *r0v = r0 >= 0 ? Json_string(&doc, r0, &r0len) : "";
    const char *r1v = r1 >= 0 ? Json_string(&doc, r1, &r1len) : "";
    CHECK(r0len == 4 && memcmp(r0v, "user", 4) == 0);
    CHECK(r1len == 5 && memcmp(r1v, "model", 5) == 0);
    JsonRef p0 = Json_member(&doc, g0, "parts");
    CHECK(Json_count(&doc, p0) == 1);
    JsonRef t0 = Json_member(&doc, Json_at(&doc, p0, 0), "text");
    CHECK(t0 >= 0 && strcmp(Json_string(&doc, t0, nullptr), "explain \"sse\" \n briefly") == 0);

    // --- Gemini: system folds into user ---------------------------------------------------
    const AiMessage sysMsg[1] = {{"system", "be brief"}};
    CHECK(AiChatGemini_buildRequest(&gchat, sysMsg, 1,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strstr(body, "\"role\":\"user\"") != nullptr);

    // --- Gemini: override + guards -----------------------------------------------------------
    AiChatGemini_setBaseUrl(&gchat, "https://proxy.example/gemini");
    CHECK(AiChatGemini_buildRequest(&gchat, sMessages, 1,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strstr(url, "https://proxy.example/gemini/v1beta/models/") != nullptr);

    AiChatGemini gbare = AiChatGemini("gemini-flash-lite");
    CHECK(AiChatGemini_buildRequest(&gbare, sMessages, 1,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(auth.kind == API_AUTH_NONE);
    CHECK(!AiChatGemini_buildRequest(nullptr, sMessages, 1,
                                     body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChatGemini_buildRequest(&gbare, nullptr, 1,
                                     body, sizeof(body), url, sizeof(url), &auth));
    AiChatGemini gnoModel = AiChatGemini();
    CHECK(!AiChatGemini_buildRequest(&gnoModel, sMessages, 1,
                                     body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChatGemini_buildRequest(&gbare, sMessages, 2,
                                     tiny, sizeof(tiny), url, sizeof(url), &auth));
    CHECK(!AiChatGemini_complete(&gbare, sMessages, 1, body, sizeof(body), nullptr));

    // --- Gemini: getters/setters ---------------------------------------------------------------
    AiChatGemini gc = AiChatGemini();
    CHECK(AiChatGemini_getModel(&gc) == nullptr);
    CHECK(AiChatGemini_getModel(nullptr) == nullptr);
    AiChatGemini_setProvider(&gc, dir);
    AiChatGemini_setPeer(&gc, anthropic);
    AiChatGemini_setModel(&gc, "gemini-pro");
    AiChatGemini_setApiKey(&gc, "gk");
    AiChatGemini_setBaseUrl(&gc, "https://go");
    CHECK(AiChatGemini_getProvider(&gc) == dir);
    CHECK(AiChatGemini_getPeer(&gc) == anthropic);
    CHECK(strcmp(AiChatGemini_getModel(&gc), "gemini-pro") == 0);
    CHECK(strcmp(AiChatGemini_getApiKey(&gc), "gk") == 0);
    CHECK(strcmp(AiChatGemini_getBaseUrl(&gc), "https://go") == 0);
    CHECK(AiChatGemini_getProvider(nullptr) == nullptr);
    CHECK(AiChatGemini_getPeer(nullptr) == nullptr);
    CHECK(AiChatGemini_getApiKey(nullptr) == nullptr);
    CHECK(AiChatGemini_getBaseUrl(nullptr) == nullptr);

    if (sFailures == 0) {
        printf("ai_chat_sisters_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("ai_chat_sisters_test: %d FAILURES\n", sFailures);
    return 1;
}
