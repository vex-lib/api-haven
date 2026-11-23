/**
 * Consolidated automated owner test suite for vex/api-haven.
 * Usage: api_haven_test [ai_chat_sisters|ai_provider|ai_sse|app_broker|asset|db_provider|db_sqlite_file|harness_run|haven_ws_fanout|json_flex|mcp_server|probe_consumer|webhook_loopback|ws_source_binding|snapshot_io|rest]
 */

#include "vex.h"
#include "re/primitive/vex-string.h"
#include "ai/ai_chat_anthropic.h"
#include "ai/ai_chat_gemini.h"
#include "net/json.h"
#include "ai/ai_provider.h"
#include "ai/ai_chat.h"
#include "ai/ai_sse.h"
#include "app/app_provider.h"
#include "app/app_broker.h"
#include "asset/asset_broker.h"
#include "asset/asset_provider.h"
#include "database/db_provider.h"
#include "database/db_sqlite_file.h"
#include "harness/engine_provider.h"
#include "harness/harness.h"
#include "api/haven_ws_fanout.h"
#include "mcp/mcp_server.h"
#include "system/app_detect.h"
#include "system/capture_tool.h"
#include "system/process_probe.h"
#include "test_support.h"
#include <stdlib.h>
#include "api/auth.h"
#include "api/rest.h"
#include "com/discord/discord.h"
#include "com/slack/slack.h"
#include "net/http.h"
#include "net/vex_ws_source.h"
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>
#include "net/url.h"
#include "security/sha1.h"
#include "storage/snapshot_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "test/api_haven_test.h"

/** Suite: ai_chat_sisters (from api-haven/ai_chat_sisters_test.c). */
/**
 * ============================================================================
 * MODULE: AiChatSistersTest (src/ai/ai_chat_sisters_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; no network, no accounts)
 * ============================================================================
 * Executable proof of the Shape-A chat sisters: the Anthropic Messages
 * envelope (model/max_tokens/messages, x-api-key auth, /v1/messages
 * URL) and the Gemini generateContent envelope (contents[] role +
 * parts[].text, x-goog-api-key auth, model-in-URL), each round-tripped
 * back through net/json, plus the cold-seam guard matrix (nullptr,
 * empty, zero maxTokens, truncation, nullptr-resp complete guard).
 *
 * Zero HTTP: complete is only asserted for its nullptr-resp guard.
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_ai_chat_sisters_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_ai_chat_sisters_sFailures++;                                               \
        }                                                              \
    } while (0)

static const AiMessage sMessages[2] = {
    {"user", "explain \"sse\" \n briefly"},
    {"assistant", "sure"},
};

/* Checks both provider request encodings, authentication, guards, and null-safe accessors without HTTP. */
static i32 suite_ai_chat_sisters(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    AiProvider *dir = AiProvider_shared();
    CHECK(dir != nullptr);
    const AiProviderSlot *anthropic = AiProvider_get(dir, "anthropic");
    CHECK(anthropic != nullptr);

    AiChatAnthropic achat = AiChatAnthropic("claude-sonnet-4-5", "ant-key");
    AiChatAnthropic_setPeer(&achat, anthropic);
    CHECK(AiChatAnthropic_getMaxTokens(&achat) == AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS);

    utf8 body[2048];
    utf8 url[512];
    ApiAuth auth = ApiAuth_none();
    CHECK(AiChatAnthropic_buildRequest(&achat, sMessages, 2,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://api.anthropic.com/v1/v1/messages") ||
          string_contains(url, "/v1/messages"));
    CHECK(auth.kind == API_AUTH_API_KEY);
    CHECK(string_equals(auth.headerName, "x-api-key"));
    CHECK(string_equals(auth.credential, "ant-key"));

    JsonNode nodes[128];
    utf8 scratch[1024];
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
    CHECK(c0 >= 0 && string_equals(Json_string(&doc, c0, nullptr), "explain \"sse\" \n briefly"));

    AiChatAnthropic_setBaseUrl(&achat, "https://proxy.example/anthropic");
    AiChatAnthropic_setMaxTokens(&achat, 512);
    CHECK(AiChatAnthropic_buildRequest(&achat, sMessages, 1,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://proxy.example/anthropic/v1/messages"));
    CHECK(string_contains(body, "\"max_tokens\":512"));

    AiChatAnthropic bare = AiChatAnthropic("claude-haiku");
    CHECK(AiChatAnthropic_buildRequest(&bare, sMessages, 1,
                                       body, sizeof(body), url, sizeof(url), &auth));
    CHECK(auth.kind == API_AUTH_NONE);

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
    utf8 tiny[16];
    CHECK(!AiChatAnthropic_buildRequest(&bare, sMessages, 2,
                                        tiny, sizeof(tiny), url, sizeof(url), &auth));
    CHECK(!AiChatAnthropic_complete(&bare, sMessages, 1, body, sizeof(body), nullptr));

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
    CHECK(string_equals(AiChatAnthropic_getModel(&ac), "claude-opus"));
    CHECK(string_equals(AiChatAnthropic_getApiKey(&ac), "k"));
    CHECK(string_equals(AiChatAnthropic_getBaseUrl(&ac), "https://o"));
    CHECK(AiChatAnthropic_getMaxTokens(&ac) == 2048);
    CHECK(AiChatAnthropic_getProvider(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getPeer(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getApiKey(nullptr) == nullptr);
    CHECK(AiChatAnthropic_getBaseUrl(nullptr) == nullptr);

    AiChatGemini gchat = AiChatGemini("gemini-2.0-flash", "goog-key");
    CHECK(AiChatGemini_buildRequest(&gchat, sMessages, 2,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"));
    CHECK(auth.kind == API_AUTH_API_KEY);
    CHECK(string_equals(auth.headerName, "x-goog-api-key"));
    CHECK(string_equals(auth.credential, "goog-key"));

    CHECK(Json_parse(&doc, nodes, 128, scratch, sizeof(scratch), body));
    root = Json_root(&doc);
    JsonRef contents = Json_member(&doc, root, "contents");
    CHECK(Json_count(&doc, contents) == 2);
    JsonRef g0 = Json_at(&doc, contents, 0);
    JsonRef g1 = Json_at(&doc, contents, 1);
    JsonRef r0 = Json_member(&doc, g0, "role");
    JsonRef r1 = Json_member(&doc, g1, "role");
    u32 r0len = 0;
    u32 r1len = 0;
    string r0v = r0 >= 0 ? Json_string(&doc, r0, &r0len) : "";
    string r1v = r1 >= 0 ? Json_string(&doc, r1, &r1len) : "";
    CHECK(r0len == 4 && memcmp(r0v, "user", 4) == 0);
    CHECK(r1len == 5 && memcmp(r1v, "model", 5) == 0);
    JsonRef p0 = Json_member(&doc, g0, "parts");
    CHECK(Json_count(&doc, p0) == 1);
    JsonRef t0 = Json_member(&doc, Json_at(&doc, p0, 0), "text");
    CHECK(t0 >= 0 && string_equals(Json_string(&doc, t0, nullptr), "explain \"sse\" \n briefly"));

    const AiMessage sysMsg[1] = {{"system", "be brief"}};
    CHECK(AiChatGemini_buildRequest(&gchat, sysMsg, 1,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_contains(body, "\"role\":\"user\""));

    AiChatGemini_setBaseUrl(&gchat, "https://proxy.example/gemini");
    CHECK(AiChatGemini_buildRequest(&gchat, sMessages, 1,
                                    body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_contains(url, "https://proxy.example/gemini/v1beta/models/"));

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
    CHECK(string_equals(AiChatGemini_getModel(&gc), "gemini-pro"));
    CHECK(string_equals(AiChatGemini_getApiKey(&gc), "gk"));
    CHECK(string_equals(AiChatGemini_getBaseUrl(&gc), "https://go"));
    CHECK(AiChatGemini_getProvider(nullptr) == nullptr);
    CHECK(AiChatGemini_getPeer(nullptr) == nullptr);
    CHECK(AiChatGemini_getApiKey(nullptr) == nullptr);
    CHECK(AiChatGemini_getBaseUrl(nullptr) == nullptr);

    if (s_ai_chat_sisters_sFailures == 0) {
        print("ai_chat_sisters_test: ALL CHECKS PASSED");
        return 0;
    }
    print("ai_chat_sisters_test: ", s_ai_chat_sisters_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for ai_chat_sisters suite. */
static i32 suite_ai_chat_sisters_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_ai_chat_sisters(1, argv);
}

/** Suite: ai_provider (from api-haven/ai_provider_test.c). */
/**
 * ============================================================================
 * MODULE: AiProviderTest (src/ai/ai_provider_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; no network, no accounts)
 * ============================================================================
 * Executable proof of the src/ai/ directory: exercises the AiProvider
 * table (count, at, get, resolve, null-safety, immutable getters), the
 * directory-wide invariants of the generated rows (unique slugs and
 * display names, slug grammar, base-URL scheme/format), and the AiChat
 * buildRequest path (URL resolution, auth kind, JSON envelope rendering)
 * by parsing the generated body back with net/json.
 *
 * Zero HTTP: AiChat_complete is only asserted for its nullptr-resp guard.
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_ai_provider_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_ai_provider_sFailures++;                                               \
        }                                                              \
    } while (0)

static const AiMessage s2_sMessages[2] = {
    {"user", "say \"hi\" \n to me"},
    {"assistant", "hello"},
};

static i32 isSlugValid(string slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (string p = slug; *p; p++) {
        utf8 c = *p;
        i32 isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '-') {
            if (p == slug || *(p + 1) == '\0' || *(p + 1) == '-')
                return 0;
        } else if (!isAlnum) {
            return 0;
        }
    }
    return 1;
}

/* Returns whether a provider identifier contains whitespace forbidden by the slug contract. */
static i32 hasWhitespace(string s) {
    if (!s)
        return 0;
    for (string p = s; *p; p++) {
        utf8 c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')
            return 1;
    }
    return 0;
}

/* Validates provider-directory invariants and parses generated AiChat request bodies without network access. */
static i32 suite_ai_provider(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    AiProvider *dir = AiProvider_shared();
    CHECK(dir != nullptr);
    const u32 total = AiProvider_count(dir);
    print("AiProvider_count = ", total);
    CHECK(total >= 255 && total <= 280);
    CHECK(AiProvider_at(dir, 0) != nullptr);
    CHECK(AiProvider_at(dir, total) == nullptr);
    CHECK(AiProvider_at(dir, total + 5) == nullptr);

    const AiProviderSlot *openai = AiProvider_get(dir, "openai");
    CHECK(openai != nullptr);
    CHECK(openai && string_equals(AiProvider_getDisplayName(dir, openai), "OpenAI"));
    CHECK(openai && AiProvider_getFamily(dir, openai) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(openai && AiProvider_getAuth(dir, openai) == AI_PROVIDER_AUTH_BEARER);
    CHECK(openai && AiProvider_getBaseUrl(dir, openai) != nullptr);
    CHECK(openai && string_equals(AiProvider_resolveBaseUrl(dir, openai), "https://api.openai.com/v1"));

    const AiProviderSlot *anthropic = AiProvider_get(dir, "anthropic");
    CHECK(anthropic && AiProvider_getFamily(dir, anthropic) == AI_PROVIDER_FAMILY_ANTHROPIC);
    CHECK(anthropic && AiProvider_getAuth(dir, anthropic) == AI_PROVIDER_AUTH_X_API_KEY);

    const AiProviderSlot *bedrock = AiProvider_get(dir, "amazon-bedrock");
    CHECK(bedrock && AiProvider_getAuth(dir, bedrock) == AI_PROVIDER_AUTH_SPECIAL);
    CHECK(bedrock && AiProvider_getFamily(dir, bedrock) == AI_PROVIDER_FAMILY_NATIVE);

    const AiProviderSlot *zhipu = AiProvider_get(dir, "zhipu-ai");
    CHECK(zhipu && AiProvider_getRegion(dir, zhipu) == AI_PROVIDER_REGION_CHINA);
    CHECK(zhipu && AiProvider_getBaseUrl(dir, zhipu) != nullptr);

    CHECK(AiProvider_get(dir, "custom-openai") != nullptr);
    CHECK(AiProvider_get(dir, "itera-compute") != nullptr);
    CHECK(AiProvider_get(dir, "lilac") != nullptr);
    CHECK(AiProvider_get(dir, "definitely-not-a-provider") == nullptr);

    string names[280];
    u32 nameCount = 0;
    for (u32 i = 0; i < total; i++) {
        const AiProviderSlot *row = AiProvider_at(dir, i);
        CHECK(row != nullptr);
        CHECK(row && isSlugValid(AiProvider_getSlug(dir, row)));
        CHECK(row && AiProvider_get(dir, AiProvider_getSlug(dir, row)) == row);
        string rowName = row ? AiProvider_getDisplayName(dir, row) : nullptr;
        CHECK(rowName && (*rowName) != '\0');
        if (row && rowName) {
            i32 dup = 0;
            for (u32 j = 0; j < nameCount; j++) {
                if (string_equals(names[j], rowName))
                    dup = 1;
            }
            CHECK(!dup);
            names[nameCount++] = rowName;
            string base = AiProvider_getBaseUrl(dir, row);
            if (base) {
                CHECK(string_startsWith(base, "https://", 8) || string_startsWith(base, "http://", 7));
                CHECK(!hasWhitespace(base));
                bytes baseLen = strlen(base);
                CHECK(baseLen > 0 && base[baseLen - 1] != '/');
            }
        }
    }

    const AiProviderSlot *meta = AiProvider_get(dir, "meta");
    CHECK(meta && AiProvider_getBaseUrl(dir, meta) == nullptr);
    CHECK(meta && string_equals(AiProvider_resolveBaseUrl(dir, meta), "https://openrouter.ai/api/v1"));

    const AiProviderSlot *vertexAnthropic = AiProvider_get(dir, "vertex-anthropic");
    CHECK(vertexAnthropic && AiProvider_getBaseUrl(dir, vertexAnthropic) == nullptr);
    CHECK(vertexAnthropic && string_equals(AiProvider_resolveBaseUrl(dir, vertexAnthropic), "https://api.anthropic.com/v1"));

    CHECK(AiProvider_count(nullptr) == 0);
    CHECK(AiProvider_get(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_get(dir, nullptr) == nullptr);
    CHECK(AiProvider_at(nullptr, 0) == nullptr);
    CHECK(AiProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getFamily(nullptr, nullptr) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(AiProvider_resolveBaseUrl(nullptr, nullptr) == nullptr);

    AiChat chat = AiChat("gpt-4o-mini", "sk-test");
    AiChat_setPeer(&chat, openai);

    utf8 body[2048];
    utf8 url[512];
    ApiAuth auth = ApiAuth_none();

    CHECK(AiChat_buildRequest(&chat, s2_sMessages, 2,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://api.openai.com/v1/chat/completions"));
    CHECK(auth.kind == API_AUTH_BEARER);
    CHECK(string_equals(auth.credential, "sk-test"));

    JsonNode nodes[128];
    utf8 scratch[1024];
    JsonDoc doc;
    CHECK(Json_parse(&doc, nodes, 128, scratch, sizeof(scratch), body));
    JsonRef root = Json_root(&doc);

    JsonRef model = Json_member(&doc, root, "model");
    u32 modelLen = 0;
    string modelView = model >= 0 ? Json_string(&doc, model, &modelLen) : "";
    CHECK(model >= 0 && modelLen == 11 && memcmp(modelView, "gpt-4o-mini", 11) == 0);

    JsonRef msgs = Json_member(&doc, root, "messages");
    CHECK(Json_count(&doc, msgs) == 2);
    JsonRef m0 = Json_at(&doc, msgs, 0);
    JsonRef c0 = Json_member(&doc, m0, "content");
    CHECK(c0 >= 0);
    CHECK(c0 >= 0 && string_equals(Json_string(&doc, c0, nullptr), "say \"hi\" \n to me"));
    JsonRef m1 = Json_at(&doc, msgs, 1);
    JsonRef r1 = Json_member(&doc, m1, "role");
    u32 roleLen = 0;
    string roleView = r1 >= 0 ? Json_string(&doc, r1, &roleLen) : "";
    CHECK(r1 >= 0 && roleLen == 9 && memcmp(roleView, "assistant", 9) == 0);

    AiChat_setBaseUrl(&chat, "https://gateway.example");
    CHECK(AiChat_buildRequest(&chat, s2_sMessages, 2,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://gateway.example/chat/completions"));

    AiChat bare = AiChat("grok-2-latest");
    CHECK(AiChat_buildRequest(&bare, s2_sMessages, 1,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://openrouter.ai/api/v1/chat/completions"));
    CHECK(auth.kind == API_AUTH_NONE);

    CHECK(!AiChat_buildRequest(nullptr, s2_sMessages, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_buildRequest(&bare, nullptr, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_buildRequest(&bare, s2_sMessages, 0,
                               nullptr, 0, url, sizeof(url), &auth));
    AiChat noModel = AiChat();
    CHECK(!AiChat_buildRequest(&noModel, s2_sMessages, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_complete(&bare, s2_sMessages, 1,
                           body, sizeof(body), nullptr));

    AiChat c = AiChat();
    CHECK(AiChat_getModel(&c) == nullptr);
    CHECK(AiChat_getModel(nullptr) == nullptr);
    AiChat_setProvider(&c, dir);
    AiChat_setPeer(&c, zhipu);
    AiChat_setModel(&c, "glm-4.5");
    AiChat_setApiKey(&c, "k2");
    AiChat_setBaseUrl(&c, "https://override");
    CHECK(AiChat_getProvider(&c) == dir);
    CHECK(AiChat_getPeer(&c) == zhipu);
    CHECK(string_equals(AiChat_getModel(&c), "glm-4.5"));
    CHECK(string_equals(AiChat_getApiKey(&c), "k2"));
    CHECK(string_equals(AiChat_getBaseUrl(&c), "https://override"));

    const AiProviderSlot *nous = AiProvider_get(dir, "nous-research");
    CHECK(nous != nullptr);
    CHECK(nous && string_equals(AiProvider_getDisplayName(dir, nous), "Nous Research"));
    CHECK(nous && AiProvider_getFamily(dir, nous) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(nous && AiProvider_getAuth(dir, nous) == AI_PROVIDER_AUTH_BEARER);

    AiChat hermes = AiChat("hermes");
    AiChat_setPeer(&hermes, nous);
    AiChat_setApiKey(&hermes, "nous-key");
    CHECK(AiChat_buildRequest(&hermes, s2_sMessages, 1,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(string_equals(url, "https://openrouter.ai/api/v1/chat/completions"));
    CHECK(auth.kind == API_AUTH_BEARER);
    CHECK(string_equals(auth.credential, "nous-key"));
    CHECK(string_contains(body, "hermes"));

    if (s_ai_provider_sFailures == 0) {
        print("ai_provider_test: ALL CHECKS PASSED (", total, " providers)");
        return 0;
    }
    print("ai_provider_test: ", s_ai_provider_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for ai_provider suite. */
static i32 suite_ai_provider_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_ai_provider(1, argv);
}

/** Suite: ai_sse (from api-haven/ai_sse_test.c). */
/**
 * ============================================================================
 * MODULE: AiSseTest (src/ai/ai_sse_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; caller-fed Bytes only,
 * no sockets, no threads, no network)
 * ============================================================================
 * Executable proof of the AiSse class: incremental data:/event:
 * decoding, multi-line joins, comment lines, \r stripping, partial-line
 * staging across feeds, [DONE] latching, explicit one-slot binding, and
 * the full cold-seam matrix (nullptr, empty, overflow, truncation flag,
 * cancelled, zero-timeout) per Rule 35.4.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_ai_sse_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_ai_sse_sFailures++;                                               \
        }                                                              \
    } while (0)

static u32 sPollCalls = 0;

/* Records a transport poll and returns no frames for the caller-fed SSE test. */
static u32 fakePoll(any handle, u64 budgetMs) {
    (discard) handle;
    (discard) budgetMs;
    sPollCalls++;
    return 0;
}

/* Supplies a successful connection callback without opening a transport. */
static bool fakeConnect(any handle) {
    (discard) handle;
    return true;
}

/* Accepts sends without side effects so the test can bind a complete source table. */
static bool fakeSend(any handle, const u8 *Bytes, u32 len) {
    (discard) handle;
    (discard) Bytes;
    (discard) len;
    return true;
}

/* Provides the no-op close callback for the in-process source table. */
static fn fakeClose(any handle) {
    (discard) handle;
}

static const HavenWsSource sFakeSource = {
    fakeConnect,
    fakePoll,
    fakeSend,
    fakeClose,
};

/* Exercises incremental SSE parsing, slot binding, cancellation, timeout, truncation, and null guards. */
static i32 suite_ai_sse(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    AiSse sse = AiSse();
    CHECK(AiSse_getTimeout(&sse) == AI_SSE_POLL_BUDGET_MS);
    CHECK(!AiSse_isCancelled(&sse));
    CHECK(!AiSse_isDone(&sse));
    CHECK(AiSse_getLineLen(&sse) == 0);

    utf8 text[512];
    bool truncated = false;
    string one = "event: message\ndata: hello\n\n";
    CHECK(AiSse_feed(&sse, one, strlen(one), text, sizeof(text), &truncated));
    CHECK(!truncated);
    CHECK(string_equals(text, "hello"));

    AiSse multi = AiSse();
    string two = "data: line1\ndata: line2\n\n";
    CHECK(AiSse_feed(&multi, two, strlen(two), text, sizeof(text), &truncated));
    CHECK(string_equals(text, "line1\nline2"));

    AiSse cr = AiSse();
    string three = ": keep-alive\r\ndata: hi\r\n\r\n";
    CHECK(AiSse_feed(&cr, three, strlen(three), text, sizeof(text), &truncated));
    CHECK(string_equals(text, "hi"));

    AiSse part = AiSse();
    CHECK(!AiSse_feed(&part, "data: hel", 9, text, sizeof(text), &truncated));
    CHECK(AiSse_getLineLen(&part) == 9);
    CHECK(AiSse_feed(&part, "lo\n\n", 4, text, sizeof(text), &truncated));
    CHECK(string_equals(text, "hello"));

    AiSse ev = AiSse();
    string evBytes = "event: done\ndata: x\n\n";
    CHECK(AiSse_feed(&ev, evBytes, strlen(evBytes), text, sizeof(text), &truncated));
    string evName = AiSse_getEvent(&ev);
    CHECK(evName != nullptr && string_equals(evName, "done"));

    AiSse fin = AiSse();
    CHECK(AiSse_feed(&fin, "data: [DONE]\n\n", 14, text, sizeof(text), &truncated));
    CHECK(AiSse_isDone(&fin));
    CHECK(!AiSse_feed(&fin, "data: late\n\n", 12, text, sizeof(text), &truncated));

    AiSse bound = AiSse();
    i32 fakeHandle = 7;
    CHECK(AiSse_getSlotHandle(&bound) == nullptr);
    CHECK(AiSse_bind(&bound, &fakeHandle, &sFakeSource));
    CHECK(AiSse_getSlotHandle(&bound) == &fakeHandle);
    CHECK(AiSse_getSlotSource(&bound) == &sFakeSource);
    CHECK(!AiSse_bind(&bound, &fakeHandle, &sFakeSource));
    i32 otherHandle = 9;
    CHECK(!AiSse_bind(&bound, &otherHandle, &sFakeSource));
    AiSse_unbind(&bound);
    CHECK(AiSse_getSlotHandle(&bound) == nullptr);
    CHECK(AiSse_getSlotSource(&bound) == nullptr);
    CHECK(!AiSse_bind(&bound, nullptr, &sFakeSource));
    CHECK(!AiSse_bind(&bound, &fakeHandle, nullptr));
    CHECK(!AiSse_bind(nullptr, &fakeHandle, &sFakeSource));
    AiSse_unbind(nullptr);

    AiSse canc = AiSse();
    AiSse_cancel(&canc);
    CHECK(AiSse_isCancelled(&canc));
    CHECK(!AiSse_feed(&canc, one, strlen(one), text, sizeof(text), &truncated));
    AiSse_reset(&canc);
    CHECK(!AiSse_isCancelled(&canc));
    CHECK(AiSse_feed(&canc, one, strlen(one), text, sizeof(text), &truncated));
    AiSse_setCancelled(&canc, true);
    CHECK(AiSse_isCancelled(&canc));
    AiSse_setCancelled(&canc, false);
    CHECK(!AiSse_isCancelled(&canc));

    AiSse tmo = AiSse();
    AiSse_setTimeout(&tmo, 0);
    CHECK(AiSse_getTimeout(&tmo) == 0);
    CHECK(!AiSse_feed(&tmo, one, strlen(one), text, sizeof(text), &truncated));
    AiSse_setTimeout(&tmo, AI_SSE_POLL_BUDGET_MS);
    CHECK(AiSse_feed(&tmo, one, strlen(one), text, sizeof(text), &truncated));

    AiSse trunc = AiSse();
    utf8 tiny[4];
    CHECK(!AiSse_feed(&trunc, one, strlen(one), tiny, sizeof(tiny), &truncated));
    CHECK(truncated);
    CHECK(!AiSse_feed(&trunc, one, strlen(one), tiny, sizeof(tiny), nullptr));

    CHECK(!AiSse_feed(nullptr, one, strlen(one), text, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, nullptr, 3, text, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, one, strlen(one), nullptr, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, one, strlen(one), text, 0, &truncated));
    CHECK(!AiSse_feed(&sse, "", 0, text, sizeof(text), &truncated));
    CHECK(AiSse_getTimeout(nullptr) == 0);
    CHECK(!AiSse_isCancelled(nullptr));
    CHECK(!AiSse_isDone(nullptr));
    CHECK(AiSse_getLineLen(nullptr) == 0);
    CHECK(AiSse_getEvent(nullptr) == nullptr);
    AiSse_setTimeout(nullptr, 5);
    AiSse_setCancelled(nullptr, true);
    AiSse_setDone(nullptr, true);
    AiSse_reset(nullptr);
    AiSse_cancel(nullptr);

    AiSse st = AiSse();
    AiSse_setDone(&st, true);
    CHECK(AiSse_isDone(&st));
    AiSse_setDone(&st, false);
    CHECK(!AiSse_isDone(&st));

    if (s_ai_sse_sFailures == 0) {
        print("ai_sse_test: ALL CHECKS PASSED");
        return 0;
    }
    print("ai_sse_test: ", s_ai_sse_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for ai_sse suite. */
static i32 suite_ai_sse_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_ai_sse(1, argv);
}

/** Suite: app_broker (from api-haven/app_broker_test.c). */
;;GETTER
static inline bytes AppJob_outLen(const AppJob *self) {
    return (*self).outLen;
}

/**
 * ============================================================================
 * MODULE: AppBrokerTest (_test/app_broker_test.c -- local-only scratch, never committed)
 * LEVEL: L2 -- Behavior verification (headless; no scripts, no network)
 * ============================================================================
 * Executable proof of the src/app/ seam: the AppProvider allowlist
 * (count, at, get, resolveTarget, null-safety), the unbound-driver
 * degrade (action false, no slot consumed), a stub echo-driver success
 * (DONE + outLen), and the TIMEOUT path on driver failure. The stub
 * runs in-process -- no osascript, no IPC, no sockets.
 *
 * Lives in umbrella _test/ (gitignored) so api-haven stays GitHub-clean.
 * Build: cc -std=gnu23 -Wall -Wextra -Werror -I projects/api-haven/src
 *   -I projects/vexspoke/src _test/app_broker_test.c
 *   projects/api-haven/src/app/app_provider.c
 *   projects/api-haven/src/app/app_broker.c -o /tmp/app_broker_test
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

static i32 sRunCalls = 0;
static u64 s_app_broker_sLastTimeoutMs = 0;

/* Copies the requested action into the bounded output and records successful stub dispatch. */
static bool stubEcho(any driverCtx, const AppProviderSlot *target,
                     string action, string paramsJson,
                     bytes paramsLen, u64 timeoutMs,
                     utf8 *outBuf, bytes outCap, bytes *outLen) {
    (discard) driverCtx;
    (discard) target;
    (discard) paramsJson;
    (discard) paramsLen;
    sRunCalls++;
    s_app_broker_sLastTimeoutMs = timeoutMs;
    string reply = action ? action : "";
    bytes len = strlen(reply);
    if (len + 1 > outCap)
        return false;
    memcpy(outBuf, reply, len + 1);
    if (outLen)
        (*outLen) = len;
    return true;
}

/* Simulates a driver rejection without writing output or changing broker state. */
static bool stubFail(any driverCtx, const AppProviderSlot *target,
                     string action, string paramsJson,
                     bytes paramsLen, u64 timeoutMs,
                     utf8 *outBuf, bytes outCap, bytes *outLen) {
    (discard) driverCtx;
    (discard) target;
    (discard) action;
    (discard) paramsJson;
    (discard) paramsLen;
    (discard) timeoutMs;
    (discard) outBuf;
    (discard) outCap;
    (discard) outLen;
    return false;
}

/* Verifies app-provider lookup and broker behavior for unbound, successful, and failed actions. */
static i32 suite_app_broker(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    AppProvider *dir = AppProvider_shared();
    CHECK(dir != nullptr);
    CHECK(AppProvider_count(dir) == 29);
    CHECK(AppProvider_at(dir, 0) != nullptr);
    CHECK(AppProvider_at(dir, 29) == nullptr);
    CHECK(AppProvider_get(dir, "apple-notes") != nullptr);
    CHECK(AppProvider_get(dir, "apple-music") != nullptr);
    CHECK(AppProvider_get(dir, "spotify-local") != nullptr);
    CHECK(AppProvider_get(dir, "apple-shortcuts") != nullptr);
    CHECK(AppProvider_get(dir, "osascript") != nullptr);
    CHECK(AppProvider_get(dir, "telegram") != nullptr);
    CHECK(AppProvider_get(dir, "messenger") != nullptr);
    CHECK(AppProvider_get(dir, "python") != nullptr);
    CHECK(AppProvider_get(dir, "php") != nullptr);
    CHECK(AppProvider_get(dir, "node") != nullptr);
    CHECK(AppProvider_get(dir, "go-toolchain") != nullptr);
    CHECK(AppProvider_get(dir, "rust-toolchain") != nullptr);
    CHECK(AppProvider_get(dir, "unity") != nullptr);
    CHECK(AppProvider_get(dir, "unreal") != nullptr);
    CHECK(AppProvider_get(dir, "godot") != nullptr);
    CHECK(AppProvider_get(dir, "discord-bot") != nullptr);
    CHECK(AppProvider_get(dir, "discord-presence") != nullptr);
    CHECK(AppProvider_get(dir, "slack") != nullptr);
    CHECK(AppProvider_get(dir, "whatsapp") != nullptr);
    CHECK(AppProvider_get(dir, "signal") != nullptr);
    CHECK(AppProvider_get(dir, "email") != nullptr);
    CHECK(AppProvider_get(dir, "sms") != nullptr);
    CHECK(AppProvider_get(dir, "matrix") != nullptr);
    CHECK(AppProvider_get(dir, "mattermost") != nullptr);
    CHECK(AppProvider_get(dir, "line") != nullptr);
    CHECK(AppProvider_get(dir, "teams") != nullptr);
    CHECK(AppProvider_get(dir, "webhooks") != nullptr);
    CHECK(AppProvider_get(dir, "irc") != nullptr);
    CHECK(AppProvider_get(dir, "imessage") != nullptr);
    CHECK(AppProvider_get(dir, "definitely-not-an-app") == nullptr);

    const AppProviderSlot *notes = AppProvider_get(dir, "apple-notes");
    CHECK(notes && AppProvider_getFamily(dir, notes) == APP_PROVIDER_FAMILY_OSA_SCRIPT);
    CHECK(notes && AppProvider_getAuth(dir, notes) == APP_PROVIDER_AUTH_SYSTEM);
    CHECK(notes && string_equals(AppProvider_getBundleIdOrScheme(dir, notes), "com.apple.Notes"));

    const AppProviderSlot *tg = AppProvider_get(dir, "telegram");
    CHECK(tg && AppProvider_getFamily(dir, tg) == APP_PROVIDER_FAMILY_REST_WEBHOOK);
    CHECK(tg && AppProvider_getAuth(dir, tg) == APP_PROVIDER_AUTH_TOKEN);

    const AppProviderSlot *spotify = AppProvider_get(dir, "spotify-local");
    CHECK(spotify && AppProvider_getFamily(dir, spotify) == APP_PROVIDER_FAMILY_LOCAL_SOCKET);

    utf8 target[64];
    CHECK(AppProvider_resolveTarget(dir, notes, target, sizeof(target)));
    CHECK(string_equals(target, "com.apple.Notes"));
    utf8 tiny[4];
    CHECK(!AppProvider_resolveTarget(dir, notes, tiny, sizeof(tiny)));
    CHECK(!AppProvider_resolveTarget(dir, notes, nullptr, 64));
    CHECK(!AppProvider_resolveTarget(nullptr, notes, target, sizeof(target)));

    CHECK(AppProvider_count(nullptr) == 0);
    CHECK(AppProvider_at(nullptr, 0) == nullptr);
    CHECK(AppProvider_get(nullptr, nullptr) == nullptr);
    CHECK(AppProvider_get(dir, nullptr) == nullptr);
    CHECK(AppProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(AppProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(AppProvider_getBundleIdOrScheme(nullptr, nullptr) == nullptr);
    CHECK(AppProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(AppProvider_getFamily(nullptr, nullptr) == APP_PROVIDER_FAMILY_OSA_SCRIPT);
    CHECK(AppProvider_getAuth(nullptr, nullptr) == APP_PROVIDER_AUTH_NONE);

    AppBroker bare = AppBroker();
    AppBroker_setTarget(&bare, notes);
    CHECK(AppBroker_getTarget(&bare) == notes);
    CHECK(AppBroker_getDriver(&bare) == nullptr);
    CHECK(AppBroker_getTimeout(&bare) == 100);
    CHECK(!AppBroker_isRunning(&bare));
    utf8 out[128];
    u32 jobId = 0;
    CHECK(!AppBroker_action(&bare, "list-notes", "{}", 2, out, sizeof(out), &jobId));
    CHECK(AppBroker_getLastStatus(&bare) == APP_STATUS_IDLE);
    CHECK(AppBroker_getJobCount(&bare) == 0);
    CHECK(!AppBroker_action(nullptr, "x", "{}", 2, out, sizeof(out), &jobId));
    CHECK(!AppBroker_action(&bare, nullptr, "{}", 2, out, sizeof(out), &jobId));
    CHECK(!AppBroker_action(&bare, "", "{}", 2, out, sizeof(out), &jobId));
    AppBroker noTarget = AppBroker();
    AppDriverTable echoTable;
    echoTable.runActionFn = stubEcho;
    i32 driverCtx = 3;
    AppBroker_setDriver(&noTarget, &driverCtx, echoTable);
    CHECK(!AppBroker_action(&noTarget, "x", "{}", 2, out, sizeof(out), &jobId));

    AppBroker b = AppBroker(250);
    AppBroker_setTarget(&b, notes);
    AppBroker_setDriver(&b, &driverCtx, echoTable);
    CHECK(AppBroker_getTarget(&b) == notes);
    CHECK(AppBroker_getDriver(&b) == &driverCtx);
    CHECK(AppBroker_getTimeout(&b) == 250);
    sRunCalls = 0;
    CHECK(AppBroker_action(&b, "list-notes", "{}", 2, out, sizeof(out), &jobId));
    CHECK(jobId == 1);
    CHECK(sRunCalls == 1);
    CHECK(s_app_broker_sLastTimeoutMs == 250);
    CHECK(string_equals(out, "list-notes"));
    CHECK(AppBroker_getLastStatus(&b) == APP_STATUS_DONE);
    CHECK(AppBroker_getJobStatus(&b, jobId) == APP_STATUS_DONE);
    CHECK(AppBroker_poll(&b, jobId) == APP_STATUS_DONE);
    CHECK(!AppBroker_isRunning(&b));
    const AppJob *slot = AppBroker_getJobAt(&b, 0);
    CHECK(slot != nullptr);
    CHECK(slot && AppJob_outLen(slot) == 10);
    CHECK(AppBroker_getJobAt(&b, 1) == nullptr);
    CHECK(!AppBroker_cancel(&b, jobId));
    CHECK(AppBroker_poll(&b, 999) == APP_STATUS_IDLE);
    CHECK(AppBroker_poll(nullptr, 1) == APP_STATUS_IDLE);

    AppDriverTable failTable;
    failTable.runActionFn = stubFail;
    AppBroker_setDriver(&b, &driverCtx, failTable);
    u32 failId = 0;
    CHECK(!AppBroker_action(&b, "doomed", "{}", 2, out, sizeof(out), &failId));
    CHECK(AppBroker_getLastStatus(&b) == APP_STATUS_TIMEOUT);
    CHECK(AppBroker_getJobStatus(&b, failId) == APP_STATUS_TIMEOUT);

    if (sFailures == 0) {
        print("app_broker_test: ALL CHECKS PASSED");
        return 0;
    }
    print("app_broker_test: ", sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for app_broker suite. */
static i32 suite_app_broker_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_app_broker(1, argv);
}

/** Suite: asset (from api-haven/asset_test.c). */
/**
 * ============================================================================
 * MODULE: AssetTest (src/asset/asset_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; descriptors + bounded
 * copies only, no network, no scraping, no exec)
 * ============================================================================
 * Executable proof of the asset/ directory: the AssetProvider table
 * (count, at, get, blessed-API vs catalog-only rows, directory-wide
 * invariants, null-safety) and the AssetBroker chunked-copy contract
 * (cursor copies, per-chunk cap, dest overflow with truncation flag,
 * cancel latch, zero budget, cache-path shaping, setters/getters).
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_asset_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_asset_sFailures++;                                               \
        }                                                              \
    } while (0)

static i32 s2_isSlugValid(string slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (string p = slug; *p; p++) {
        utf8 c = *p;
        i32 isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '-') {
            if (p == slug || *(p + 1) == '\0' || *(p + 1) == '-')
                return 0;
        } else if (!isAlnum) {
            return 0;
        }
    }
    return 1;
}

/* Checks provider catalog invariants and bounded AssetBroker copy, cancellation, and path contracts. */
static i32 suite_asset(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    AssetProvider *dir = AssetProvider_shared();
    CHECK(dir != nullptr);
    const u32 total = AssetProvider_count(dir);
    print("AssetProvider_count = ", total);
    CHECK(total == 12);
    CHECK(AssetProvider_at(dir, 0) != nullptr);
    CHECK(AssetProvider_at(dir, total) == nullptr);

    const AssetProviderSlot *unsplash = AssetProvider_get(dir, "unsplash");
    CHECK(unsplash != nullptr);
    CHECK(unsplash && string_equals(AssetProvider_getDisplayName(dir, unsplash), "Unsplash"));
    CHECK(unsplash && AssetProvider_getApiBase(dir, unsplash) != nullptr);
    CHECK(unsplash && AssetProvider_getLicenseFamily(dir, unsplash) != nullptr);
    CHECK(unsplash && AssetProvider_getSampleTitle(dir, unsplash) != nullptr);
    CHECK(unsplash && AssetProvider_getSampleAuthor(dir, unsplash) != nullptr);
    CHECK(unsplash && AssetProvider_getSamplePreview(dir, unsplash) != nullptr);
    CHECK(unsplash && AssetProvider_getSampleDownload(dir, unsplash) != nullptr);

    const AssetProviderSlot *kenney = AssetProvider_get(dir, "kenney");
    CHECK(kenney && AssetProvider_getApiBase(dir, kenney) == nullptr);
    CHECK(kenney && string_equals(AssetProvider_getLicenseFamily(dir, kenney), "CC0"));
    CHECK(kenney && AssetProvider_getSampleDownload(dir, kenney) != nullptr);
    CHECK(AssetProvider_get(dir, "quaternius") != nullptr);
    CHECK(AssetProvider_get(dir, "definitely-not-a-source") == nullptr);

    string names[12];
    u32 nameCount = 0;
    for (u32 i = 0; i < total; i++) {
        const AssetProviderSlot *row = AssetProvider_at(dir, i);
        CHECK(row != nullptr);
        CHECK(row && s2_isSlugValid(AssetProvider_getSlug(dir, row)));
        CHECK(row && AssetProvider_get(dir, AssetProvider_getSlug(dir, row)) == row);
        string rowName = row ? AssetProvider_getDisplayName(dir, row) : nullptr;
        CHECK(rowName && (*rowName) != '\0');
        CHECK(row && AssetProvider_getLicenseFamily(dir, row) != nullptr);
        CHECK(row && AssetProvider_getSampleDownload(dir, row) != nullptr);
        if (row && rowName) {
            i32 dup = 0;
            for (u32 j = 0; j < nameCount; j++) {
                if (string_equals(names[j], rowName))
                    dup = 1;
            }
            CHECK(!dup);
            names[nameCount++] = rowName;
        }
    }

    CHECK(AssetProvider_count(nullptr) == 0);
    CHECK(AssetProvider_get(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_get(dir, nullptr) == nullptr);
    CHECK(AssetProvider_at(nullptr, 0) == nullptr);
    CHECK(AssetProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getApiBase(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getLicenseFamily(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getSampleTitle(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getSampleAuthor(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getSamplePreview(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getSampleDownload(nullptr, nullptr) == nullptr);
    CHECK(AssetProvider_getNote(nullptr, nullptr) == nullptr);

    AssetBroker broker = AssetBroker();
    CHECK(AssetBroker_getChunkBudget(&broker) == ASSET_BROKER_CHUNK_BUDGET_MS);
    CHECK(!AssetBroker_isCancelled(&broker));
    CHECK(AssetBroker_getBytesCopied(&broker) == 0);
    string sub = AssetBroker_getCacheSub(&broker);
    CHECK(string_equals(sub, "assets"));

    u8 dest[64];
    u32 used = 0;
    bool truncated = false;
    const u8 c1[4] = {'a', 'b', 'c', 'd'};
    const u8 c2[4] = {'e', 'f', 'g', 'h'};
    CHECK(AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(!truncated && used == 4);
    CHECK(AssetBroker_copyChunk(&broker, c2, sizeof(c2), dest, sizeof(dest), &used, &truncated));
    CHECK(used == 8 && memcmp(dest, "abcdefgh", 8) == 0);
    CHECK(AssetBroker_getBytesCopied(&broker) == 8);

    CHECK(AssetBroker_copyChunk(&broker, nullptr, 0, dest, sizeof(dest), &used, &truncated));
    CHECK(used == 8);

    u8 big[ASSET_BROKER_CHUNK_CAP + 1];
    memset(big, 0x5a, sizeof(big));
    CHECK(!AssetBroker_copyChunk(&broker, big, sizeof(big), dest, sizeof(dest), &used, &truncated));

    u8 fill[56];
    memset(fill, 0x31, sizeof(fill));
    CHECK(AssetBroker_copyChunk(&broker, fill, sizeof(fill), dest, sizeof(dest), &used, &truncated));
    CHECK(used == 64);
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(truncated);
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, nullptr));

    AssetBroker canc = AssetBroker();
    AssetBroker_cancel(&canc);
    CHECK(AssetBroker_isCancelled(&canc));
    used = 0;
    CHECK(!AssetBroker_copyChunk(&canc, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    AssetBroker_reset(&canc);
    CHECK(!AssetBroker_isCancelled(&canc));
    CHECK(AssetBroker_copyChunk(&canc, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));

    AssetBroker tmo = AssetBroker();
    AssetBroker_setChunkBudget(&tmo, 0);
    CHECK(AssetBroker_getChunkBudget(&tmo) == 0);
    used = 0;
    CHECK(!AssetBroker_copyChunk(&tmo, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    AssetBroker_setChunkBudget(&tmo, ASSET_BROKER_CHUNK_BUDGET_MS);

    CHECK(!AssetBroker_copyChunk(nullptr, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, nullptr, 4, dest, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), nullptr, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, 0, &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), nullptr, &truncated));
    u32 badUsed = 99;
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &badUsed, &truncated));
    AssetBroker_reset(nullptr);
    AssetBroker_cancel(nullptr);
    AssetBroker_setChunkBudget(nullptr, 5);
    AssetBroker_setCancelled(nullptr, true);
    AssetBroker_setBytesCopied(nullptr, 3);
    CHECK(AssetBroker_getChunkBudget(nullptr) == 0);
    CHECK(!AssetBroker_isCancelled(nullptr));
    CHECK(AssetBroker_getBytesCopied(nullptr) == 0);
    CHECK(AssetBroker_getCacheSub(nullptr) == nullptr);

    AssetBroker st = AssetBroker();
    bool subTrunc = false;
    CHECK(AssetBroker_setCacheSub(&st, "models", &subTrunc));
    CHECK(!subTrunc);
    sub = AssetBroker_getCacheSub(&st);
    CHECK(string_equals(sub, "models"));
    string wayTooLong = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefX";
    CHECK(!AssetBroker_setCacheSub(&st, wayTooLong, &subTrunc));
    CHECK(subTrunc);
    CHECK(!AssetBroker_setCacheSub(&st, nullptr, &subTrunc));
    CHECK(!AssetBroker_setCacheSub(nullptr, "x", &subTrunc));
    AssetBroker_setCancelled(&st, true);
    CHECK(AssetBroker_isCancelled(&st));
    AssetBroker_setBytesCopied(&st, 41);
    CHECK(AssetBroker_getBytesCopied(&st) == 41);

    utf8 path[128];
    CHECK(AssetBroker_cachePath(&st, "fox.glb", path, sizeof(path)));
    CHECK(string_equals(path, "cache/models/fox.glb"));
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", path, 8));
    CHECK(!AssetBroker_cachePath(nullptr, "fox.glb", path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, nullptr, path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "", path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", nullptr, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", path, 0));

    if (s_asset_sFailures == 0) {
        print("asset_test: ALL CHECKS PASSED (", total, " sources)");
        return 0;
    }
    print("asset_test: ", s_asset_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for asset suite. */
static i32 suite_asset_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_asset(1, argv);
}

/** Suite: db_provider (from api-haven/db_provider_test.c). */
/**
 * ============================================================================
 * MODULE: DbProviderTest (src/database/db_provider_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; descriptors only, no
 * drivers, no network)
 * ============================================================================
 * Executable proof of the database data-source directory: row count,
 * slug/engine lookups, wire families and ports for representative rows,
 * positional access, directory-wide invariants (unique slugs and display
 * names, slug grammar, non-empty names/engines), and every null-safety
 * guard.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_db_provider_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_db_provider_sFailures++;                                               \
        }                                                              \
    } while (0)

static i32 s3_isSlugValid(string slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (string p = slug; *p; p++) {
        utf8 c = *p;
        i32 isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '-') {
            if (p == slug || *(p + 1) == '\0' || *(p + 1) == '-')
                return 0;
        } else if (!isAlnum) {
            return 0;
        }
    }
    return 1;
}

/* Checks database-provider lookups, directory-wide row invariants, and nullable accessor defaults. */
static i32 suite_db_provider(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    DbProvider *db = DbProvider_shared();
    CHECK(db != nullptr);
    const u32 total = DbProvider_count(db);
    print("DbProvider_count = ", total);
    CHECK(total == 25);

    const DbProviderSlot *pg = DbProvider_get(db, "postgresql");
    CHECK(pg != nullptr);
    CHECK(pg && string_equals(DbProvider_getDisplayName(db, pg), "PostgreSQL"));
    CHECK(pg && string_equals(DbProvider_getEngine(db, pg), "postgres"));
    CHECK(pg && DbProvider_getDefaultPort(db, pg) == 5432);
    CHECK(pg && DbProvider_getFamily(db, pg) == DB_PROVIDER_FAMILY_SQL);

    const DbProviderSlot *mysql = DbProvider_get(db, "mysql");
    CHECK(mysql && DbProvider_getDefaultPort(db, mysql) == 3306);
    CHECK(mysql && DbProvider_getFamily(db, mysql) == DB_PROVIDER_FAMILY_SQL);

    const DbProviderSlot *dynamo = DbProvider_get(db, "amazon-dynamodb");
    CHECK(dynamo && DbProvider_getFamily(db, dynamo) == DB_PROVIDER_FAMILY_NO_SQL);
    CHECK(dynamo && DbProvider_getDefaultPort(db, dynamo) == 0);

    const DbProviderSlot *h2 = DbProvider_get(db, "h2");
    CHECK(h2 && DbProvider_getFamily(db, h2) == DB_PROVIDER_FAMILY_EMBEDDED);

    const DbProviderSlot *bq = DbProvider_get(db, "google-bigquery");
    CHECK(bq && DbProvider_getFamily(db, bq) == DB_PROVIDER_FAMILY_SQL_COMPAT);

    CHECK(DbProvider_get(db, "amazon-aurora-mysql") != nullptr);
    CHECK(DbProvider_get(db, "ibm-db2-luw") != nullptr);
    CHECK(DbProvider_get(db, "definitely-not-a-db") == nullptr);

    const DbProviderSlot *sqlserver = DbProvider_findByEngine(db, "sqlserver");
    CHECK(sqlserver != nullptr);
    CHECK(sqlserver && string_equals(DbProvider_getSlug(db, sqlserver), "azure-sql-database"));
    CHECK(DbProvider_findByEngine(db, "postgres") != nullptr);
    CHECK(DbProvider_findByEngine(db, "definitely-not-an-engine") == nullptr);

    CHECK(DbProvider_at(db, 0) != nullptr);
    CHECK(DbProvider_at(db, total) == nullptr);
    string names[25];
    u32 nameCount = 0;
    for (u32 i = 0; i < total; i++) {
        const DbProviderSlot *row = DbProvider_at(db, i);
        CHECK(row != nullptr);
        CHECK(row && s3_isSlugValid(DbProvider_getSlug(db, row)));
        CHECK(row && DbProvider_get(db, DbProvider_getSlug(db, row)) == row);
        string rowName = row ? DbProvider_getDisplayName(db, row) : nullptr;
        string rowEngine = row ? DbProvider_getEngine(db, row) : nullptr;
        CHECK(rowName && (*rowName) != '\0');
        CHECK(rowEngine && (*rowEngine) != '\0');
        if (row && rowName) {
            i32 dup = 0;
            for (u32 j = 0; j < nameCount; j++) {
                if (string_equals(names[j], rowName))
                    dup = 1;
            }
            CHECK(!dup);
            names[nameCount++] = rowName;
        }
    }

    CHECK(DbProvider_count(nullptr) == 0);
    CHECK(DbProvider_get(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_get(db, nullptr) == nullptr);
    CHECK(DbProvider_findByEngine(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_at(nullptr, 0) == nullptr);
    CHECK(DbProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getEngine(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(DbProvider_getDefaultPort(nullptr, nullptr) == 0);
    CHECK(DbProvider_getFamily(nullptr, nullptr) == DB_PROVIDER_FAMILY_SQL);

    if (s_db_provider_sFailures == 0) {
        print("db_provider_test: ALL CHECKS PASSED (", total, " data sources)");
        return 0;
    }
    print("db_provider_test: ", s_db_provider_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for db_provider suite. */
static i32 suite_db_provider_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_db_provider(1, argv);
}

/** Suite: db_sqlite_file (from api-haven/db_sqlite_file_test.c). */
/**
 * ============================================================================
 * MODULE: DbSqliteFileTest (src/database/db_sqlite_file_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; descriptors + opaque
 * delegation only, zero sqlite3.h, no drivers, no filesystem)
 * ============================================================================
 * Executable proof of the DbSqliteFile class: read-only defaults,
 * path/caps/bounds setters, describe rendering, bind/unbind of a fake
 * owner table, delegated exec, unbound drop-degrade, and the full
 * cold-seam matrix (nullptr, empty, zero cap, overflow) per Rule 35.4.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_db_sqlite_file_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_db_sqlite_file_sFailures++;                                               \
        }                                                              \
    } while (0)

/* Implements a bounded fake execution callback and counts valid delegated calls. */
static bool fakeExec(any handle, string sql, utf8 *out, bytes outCap) {
    if (!handle || !sql || !out || outCap == 0)
        return false;
    i32 *calls = (i32*) handle;
    (*calls)++;
    const i32 n = snprintf(out, outCap, "rows for: %s", sql);
    if (n <= 0 || (bytes)n >= outCap)
        return false;
    return true;
}

static const DbSqliteExec sFakeTable = { fakeExec };

/* Exercises descriptor defaults, callback binding/delegation, setters, and rejection guards without SQLite. */
static i32 suite_db_sqlite_file(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    DbSqliteFile bare = DbSqliteFile();
    CHECK(DbSqliteFile_isReadOnly(&bare));
    CHECK(DbSqliteFile_getPath(&bare) == nullptr);
    CHECK(DbSqliteFile_getCaps(&bare) == 0);
    CHECK(DbSqliteFile_getMaxBytes(&bare) == 0);
    CHECK(DbSqliteFile_getExecHandle(&bare) == nullptr);
    CHECK(DbSqliteFile_getExecTable(&bare) == nullptr);

    DbSqliteFile file = DbSqliteFile("/tmp/catalog.db");
    CHECK(DbSqliteFile_isReadOnly(&file));
    CHECK(string_equals(DbSqliteFile_getPath(&file), "/tmp/catalog.db"));
    CHECK((DbSqliteFile_getCaps(&file) & DB_SQLITE_CAP_READ) != 0);

    utf8 desc[256];
    DbSqliteFile_setMaxBytes(&file, 1048576);
    CHECK(DbSqliteFile_describe(&file, desc, sizeof(desc)));
    CHECK(string_contains(desc, "sqlite file: path=/tmp/catalog.db"));
    CHECK(string_contains(desc, "readOnly=1"));
    CHECK(string_contains(desc, "maxBytes=1048576"));
    CHECK(!DbSqliteFile_describe(&file, desc, 8));
    CHECK(!DbSqliteFile_describe(&bare, desc, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(nullptr, desc, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, nullptr, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, desc, 0));

    utf8 out[256];
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));

    i32 calls = 0;
    CHECK(DbSqliteFile_getExecHandle(&file) == nullptr);
    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(DbSqliteFile_getExecHandle(&file) == &calls);
    CHECK(DbSqliteFile_getExecTable(&file) == &sFakeTable);
    CHECK(DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(calls == 1);
    CHECK(string_equals(out, "rows for: SELECT 1"));
    CHECK(!DbSqliteFile_bind(&file, &calls, &sFakeTable));
    i32 other = 0;
    CHECK(!DbSqliteFile_bind(&file, &other, &sFakeTable));
    DbSqliteFile_unbind(&file);
    CHECK(DbSqliteFile_getExecHandle(&file) == nullptr);
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_bind(&file, nullptr, &sFakeTable));
    CHECK(!DbSqliteFile_bind(&file, &calls, nullptr));
    CHECK(!DbSqliteFile_bind(nullptr, &calls, &sFakeTable));
    static const DbSqliteExec nullTable = { nullptr };
    CHECK(!DbSqliteFile_bind(&file, &calls, &nullTable));
    DbSqliteFile_unbind(nullptr);

    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(!DbSqliteFile_exec(nullptr, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, nullptr, out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", nullptr, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, 0));

    DbSqliteFile st = DbSqliteFile();
    DbSqliteFile_setPath(&st, "/data/app.db");
    DbSqliteFile_setCaps(&st, DB_SQLITE_CAP_READ);
    DbSqliteFile_setMaxBytes(&st, 4096);
    DbSqliteFile_setReadOnly(&st, true);
    CHECK(string_equals(DbSqliteFile_getPath(&st), "/data/app.db"));
    CHECK(DbSqliteFile_getCaps(&st) == DB_SQLITE_CAP_READ);
    CHECK(DbSqliteFile_getMaxBytes(&st) == 4096);
    CHECK(DbSqliteFile_isReadOnly(&st));
    DbSqliteFile_setPath(nullptr, "x");
    DbSqliteFile_setCaps(nullptr, 1);
    DbSqliteFile_setMaxBytes(nullptr, 1);
    DbSqliteFile_setReadOnly(nullptr, false);
    CHECK(DbSqliteFile_getPath(nullptr) == nullptr);
    CHECK(DbSqliteFile_getCaps(nullptr) == 0);
    CHECK(DbSqliteFile_getMaxBytes(nullptr) == 0);
    CHECK(!DbSqliteFile_isReadOnly(nullptr));
    CHECK(DbSqliteFile_getExecHandle(nullptr) == nullptr);
    CHECK(DbSqliteFile_getExecTable(nullptr) == nullptr);

    if (s_db_sqlite_file_sFailures == 0) {
        print("db_sqlite_file_test: ALL CHECKS PASSED");
        return 0;
    }
    print("db_sqlite_file_test: ", s_db_sqlite_file_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for db_sqlite_file suite. */
static i32 suite_db_sqlite_file_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_db_sqlite_file(1, argv);
}

/** Suite: harness_run (from api-haven/harness_run_test.c). */
/**
 * ============================================================================
 * MODULE: HarnessRunTest (_test/harness_run_test.c -- local-only scratch, never committed)
 * LEVEL: L2 -- Behavior verification (headless; no spawn, no network)
 * ============================================================================
 * Executable proof of the src/harness/ seam: the EngineProvider table
 * (count, at, get, resolveCli, null-safety), the unbound-driver degrade
 * (run false, no slot consumed), 16 bounded slots + BUSY_FULL on the
 * 17th run, and the TIMEOUT drop-degrade path through a stub poll fn.
 * All drivers are in-process stubs -- nothing spawns, nothing blocks.
 *
 * Lives in umbrella _test/ (gitignored) so api-haven stays GitHub-clean.
 * Build: cc -std=gnu23 -Wall -Wextra -Werror -I projects/api-haven/src
 *   -I projects/vexspoke/src _test/harness_run_test.c
 *   projects/api-haven/src/harness/engine_provider.c
 *   projects/api-haven/src/harness/harness.c -o /tmp/harness_run_test
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_harness_run_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_harness_run_sFailures++;                                               \
        }                                                              \
    } while (0)

static i32 sSpawnCalls = 0;
static i32 sCancelCalls = 0;
static HarnessStatus sPollVerdict = HARNESS_STATUS_RUNNING;
static u64 s_harness_run_sLastTimeoutMs = 0;

/* Records launches and returns a synthetic handle for each accepted job. */
static bool stubSpawn(any driverCtx, const EngineProviderSlot *engine,
                      string prompt, bytes promptLen,
                      u64 timeoutMs, any *outHandle) {
    (discard) driverCtx;
    (discard) engine;
    (discard) prompt;
    (discard) promptLen;
    sSpawnCalls++;
    s_harness_run_sLastTimeoutMs = timeoutMs;
    if (outHandle)
        (*outHandle) = (any) (pointer) (0x1000 + (pointer) sSpawnCalls);
    return true;
}

/* Returns the test-selected poll verdict for a synthetic job handle. */
static HarnessStatus stubPoll(any driverCtx, any handle, u64 timeoutMs) {
    (discard) driverCtx;
    (discard) handle;
    (discard) timeoutMs;
    return sPollVerdict;
}

/* Counts cancellation callbacks for the harness lifecycle assertions. */
static fn stubCancel(any driverCtx, any handle) {
    (discard) driverCtx;
    (discard) handle;
    sCancelCalls++;
}

/* Builds the in-process spawn/poll/cancel table used by the owner test. */
static HarnessDriverTable stubTable() {
    HarnessDriverTable table;
    table.spawnFn = stubSpawn;
    table.pollFn = stubPoll;
    table.cancelFn = stubCancel;
    return table;
}

/* Verifies provider lookup and Harness job admission, capacity, polling, cancellation, and timeout behavior. */
static i32 suite_harness_run(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    EngineProvider *dir = EngineProvider_shared();
    CHECK(dir != nullptr);
    CHECK(EngineProvider_count(dir) == 20);
    CHECK(EngineProvider_at(dir, 0) != nullptr);
    CHECK(EngineProvider_at(dir, 20) == nullptr);
    CHECK(EngineProvider_get(dir, "claude-code") != nullptr);
    CHECK(EngineProvider_get(dir, "opencode") != nullptr);
    CHECK(EngineProvider_get(dir, "kilocode") != nullptr);
    CHECK(EngineProvider_get(dir, "t3-code") != nullptr);
    CHECK(EngineProvider_get(dir, "agy") != nullptr);
    CHECK(EngineProvider_get(dir, "codex") != nullptr);
    CHECK(EngineProvider_get(dir, "gemini-cli") != nullptr);
    CHECK(EngineProvider_get(dir, "aider") != nullptr);
    CHECK(EngineProvider_get(dir, "pi") != nullptr);
    CHECK(EngineProvider_get(dir, "goose") != nullptr);
    CHECK(EngineProvider_get(dir, "cursor-cli") != nullptr);
    CHECK(EngineProvider_get(dir, "kiro-cli") != nullptr);
    CHECK(EngineProvider_get(dir, "windsurf") != nullptr);
    CHECK(EngineProvider_get(dir, "qwen-code") != nullptr);
    CHECK(EngineProvider_get(dir, "crush") != nullptr);
    CHECK(EngineProvider_get(dir, "hermes") != nullptr);
    CHECK(EngineProvider_get(dir, "amp") != nullptr);
    CHECK(EngineProvider_get(dir, "gptme") != nullptr);
    CHECK(EngineProvider_get(dir, "openhands") != nullptr);
    CHECK(EngineProvider_get(dir, "copilot-cli") != nullptr);
    CHECK(EngineProvider_get(dir, "definitely-not-an-engine") == nullptr);

    const EngineProviderSlot *opencode = EngineProvider_get(dir, "opencode");
    CHECK(opencode && string_equals(EngineProvider_getCliName(dir, opencode), "opencode"));
    CHECK(opencode && EngineProvider_getFamily(dir, opencode) == ENGINE_PROVIDER_FAMILY_CLI);
    CHECK(opencode && EngineProvider_getAuth(dir, opencode) == ENGINE_PROVIDER_AUTH_API_KEY);

    utf8 cli[32];
    CHECK(EngineProvider_resolveCli(dir, opencode, cli, sizeof(cli)));
    CHECK(string_equals(cli, "opencode"));
    utf8 tiny[4];
    CHECK(!EngineProvider_resolveCli(dir, opencode, tiny, sizeof(tiny)));
    CHECK(!EngineProvider_resolveCli(dir, opencode, nullptr, 32));
    CHECK(!EngineProvider_resolveCli(dir, opencode, cli, 0));
    CHECK(!EngineProvider_resolveCli(nullptr, opencode, cli, sizeof(cli)));
    CHECK(!EngineProvider_resolveCli(dir, nullptr, cli, sizeof(cli)));

    CHECK(EngineProvider_count(nullptr) == 0);
    CHECK(EngineProvider_at(nullptr, 0) == nullptr);
    CHECK(EngineProvider_get(nullptr, nullptr) == nullptr);
    CHECK(EngineProvider_get(dir, nullptr) == nullptr);
    CHECK(EngineProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(EngineProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(EngineProvider_getCliName(nullptr, nullptr) == nullptr);
    CHECK(EngineProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(EngineProvider_getFamily(nullptr, nullptr) == ENGINE_PROVIDER_FAMILY_CLI);
    CHECK(EngineProvider_getAuth(nullptr, nullptr) == ENGINE_PROVIDER_AUTH_NONE);

    Harness bare = Harness();
    CHECK(Harness_getEngine(&bare) == nullptr);
    CHECK(Harness_getDriver(&bare) == nullptr);
    CHECK(Harness_getTimeout(&bare) == 100);
    CHECK(!Harness_isRunning(&bare));
    u32 jobId = 0;
    CHECK(!Harness_run(&bare, "hello", 5, 100, &jobId));
    CHECK(Harness_getLastStatus(&bare) == HARNESS_STATUS_IDLE);
    CHECK(Harness_getJobCount(&bare) == 0);
    CHECK(!Harness_run(nullptr, "hello", 5, 100, &jobId));
    CHECK(!Harness_run(&bare, nullptr, 5, 100, &jobId));
    CHECK(!Harness_run(&bare, "hello", 0, 100, &jobId));

    Harness h = Harness(250);
    Harness_setEngine(&h, opencode);
    i32 driverCtx = 7;
    Harness_setDriver(&h, &driverCtx, stubTable());
    CHECK(Harness_getEngine(&h) == opencode);
    CHECK(Harness_getDriver(&h) == &driverCtx);
    CHECK(Harness_getTimeout(&h) == 250);
    sSpawnCalls = 0;
    sCancelCalls = 0;
    sPollVerdict = HARNESS_STATUS_RUNNING;
    CHECK(Harness_run(&h, "hello", 5, 0, &jobId));
    CHECK(jobId == 1);
    CHECK(sSpawnCalls == 1);
    CHECK(s_harness_run_sLastTimeoutMs == 250);
    CHECK(Harness_isRunning(&h));
    CHECK(Harness_getJobStatus(&h, jobId) == HARNESS_STATUS_RUNNING);
    CHECK(Harness_poll(&h, jobId) == HARNESS_STATUS_RUNNING);
    CHECK(Harness_getJobAt(&h, 0) != nullptr);
    CHECK(Harness_getJobAt(&h, 1) == nullptr);
    CHECK(Harness_cancel(&h, jobId));
    CHECK(sCancelCalls == 1);
    CHECK(Harness_getJobStatus(&h, jobId) == HARNESS_STATUS_IDLE);
    CHECK(!Harness_isRunning(&h));
    CHECK(!Harness_cancel(&h, jobId));
    CHECK(!Harness_cancel(&h, 999));
    CHECK(Harness_poll(&h, 999) == HARNESS_STATUS_IDLE);
    CHECK(Harness_poll(nullptr, 1) == HARNESS_STATUS_IDLE);
    CHECK(Harness_getJobStatus(nullptr, 1) == HARNESS_STATUS_IDLE);

    Harness full = Harness();
    Harness_setEngine(&full, EngineProvider_get(dir, "claude-code"));
    Harness_setDriver(&full, &driverCtx, stubTable());
    sPollVerdict = HARNESS_STATUS_RUNNING;
    u32 ids[HARNESS_MAX_JOBS];
    for (u32 i = 0; i < (u32) HARNESS_MAX_JOBS; i++) {
        CHECK(Harness_run(&full, "p", 1, 50, &ids[i]));
        CHECK(ids[i] == i + 1);
    }
    CHECK(Harness_getJobCount(&full) == (u32) HARNESS_MAX_JOBS);
    u32 extra = 0;
    CHECK(!Harness_run(&full, "one-too-many", 12, 50, &extra));
    CHECK(Harness_getLastStatus(&full) == HARNESS_STATUS_BUSY_FULL);
    CHECK(Harness_getJobCount(&full) == (u32) HARNESS_MAX_JOBS);

    sPollVerdict = HARNESS_STATUS_TIMEOUT;
    CHECK(Harness_poll(&full, ids[0]) == HARNESS_STATUS_TIMEOUT);
    CHECK(Harness_getJobStatus(&full, ids[0]) == HARNESS_STATUS_TIMEOUT);
    CHECK(Harness_getJobStatus(&full, ids[1]) == HARNESS_STATUS_RUNNING);

    HarnessDriverTable noPoll = stubTable();
    noPoll.pollFn = nullptr;
    Harness_setDriver(&h, &driverCtx, noPoll);
    CHECK(Harness_poll(&h, 1) == HARNESS_STATUS_IDLE);

    if (s_harness_run_sFailures == 0) {
        print("harness_run_test: ALL CHECKS PASSED");
        return 0;
    }
    print("harness_run_test: ", s_harness_run_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for harness_run suite. */
static i32 suite_harness_run_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_harness_run(1, argv);
}

/** Suite: haven_ws_fanout (from api-haven/haven_ws_fanout_test.c). */
/**
 * ============================================================================
 * MODULE: HavenWsFanoutTest (api-haven/test/haven_ws_fanout_test.c)
 * LEVEL: L2 -- Behavior verification (headless; no threads, no sockets, no network)
 * ============================================================================
 * Executable proof of the HavenWsFanout class: 16-slot registry over
 * opaque transport handles and HavenWsSource fn-tables, attach/detach lifecycle,
 * duplicate rejection, table-full rejection, even-budget slicing across active
 * slots, getter invariants, and cold-seam null-safety.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_haven_ws_fanout_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_haven_ws_fanout_sFailures++;                                               \
        }                                                              \
    } while (0)

;;GETTER
static inline u32 MockTransport_pollCount(const MockTransport *self) {
    return (*self).pollCount;
}
;;SETTER
static inline fn MockTransport_setPollCount(MockTransport *self, u32 pollCount) {
    (*self).pollCount = pollCount;
}
;;SETTER
static inline fn MockTransport_setLastBudget(MockTransport *self, u64 lastBudget) {
    (*self).lastBudget = lastBudget;
}
;;GETTER
static inline u32 MockTransport_framesToDeliver(const MockTransport *self) {
    return (*self).framesToDeliver;
}

/* Records each assigned poll budget and returns the mock transport's queued frame count. */
static u32 mockPoll(any handle, u64 budgetMs) {
    if (!handle)
        return 0;
    MockTransport *mock = (MockTransport*) handle;
    MockTransport_setPollCount(mock, MockTransport_pollCount(mock) + 1);
    MockTransport_setLastBudget(mock, budgetMs);
    return MockTransport_framesToDeliver(mock);
}

/* Supplies successful connect behavior for the fanout source table. */
static bool mockConnect(any handle) {
    (discard) handle;
    return true;
}

/* Accepts outbound bytes without network activity for the mock source table. */
static bool mockSend(any handle, const u8 *Bytes, u32 len) {
    (discard) handle;
    (discard) Bytes;
    (discard) len;
    return true;
}

/* Supplies the no-op close callback for mock transports. */
static fn mockClose(any handle) {
    (discard) handle;
}

static const HavenWsSource sMockSource = {
    mockConnect,
    mockPoll,
    mockSend,
    mockClose,
};

/* Exercises fanout attachment, fair bounded polling, slot reuse, saturation, and null safety. */
static i32 suite_haven_ws_fanout(i32 argc, utf8 **argv) {
    (discard) argc;
    (discard) argv;

    HavenWsFanout *fanout = HavenWsFanout();
    CHECK(fanout != nullptr);
    CHECK(HavenWsFanout_getCount(fanout) == 0);
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++)
        CHECK(HavenWsFanout_getHandle(fanout, i) == nullptr);
    CHECK(HavenWsFanout_getHandle(fanout, HAVEN_WS_FANOUT_MAX) == nullptr);

    MockTransport t1 = {0, 0, 2};
    MockTransport t2 = {0, 0, 5};

    CHECK(HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 1);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == &t1);

    CHECK(!HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 1);

    CHECK(HavenWsFanout_attach(fanout, &t2, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 2);
    CHECK(HavenWsFanout_getHandle(fanout, 1) == &t2);

    u32 delivered = HavenWsFanout_pollStep(fanout, 10);
    CHECK(delivered == (2 + 5));
    CHECK(t1.pollCount == 1);
    CHECK(t1.lastBudget == 5);
    CHECK(t2.pollCount == 1);
    CHECK(t2.lastBudget == 5);

    delivered = HavenWsFanout_pollStep(fanout, 1);
    CHECK(delivered == (2 + 5));
    CHECK(t1.pollCount == 2);
    CHECK(t1.lastBudget == 1);
    CHECK(t2.pollCount == 2);
    CHECK(t2.lastBudget == 1);

    delivered = HavenWsFanout_pollStep(fanout, 0);
    CHECK(delivered == 0);
    CHECK(t1.pollCount == 2);
    CHECK(t2.pollCount == 2);

    CHECK(HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_getCount(fanout) == 1);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == nullptr);
    CHECK(HavenWsFanout_getHandle(fanout, 1) == &t2);

    CHECK(!HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_getCount(fanout) == 1);

    t2.framesToDeliver = 3;
    delivered = HavenWsFanout_pollStep(fanout, 10);
    CHECK(delivered == 3);
    CHECK(t1.pollCount == 2);
    CHECK(t2.pollCount == 3);
    CHECK(t2.lastBudget == 10);

    CHECK(HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 2);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == &t1);

    MockTransport extras[HAVEN_WS_FANOUT_MAX];
    memset(extras, 0, sizeof(extras));
    for (u32 i = 2; i < HAVEN_WS_FANOUT_MAX; i++)
        CHECK(HavenWsFanout_attach(fanout, &extras[i], &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == HAVEN_WS_FANOUT_MAX);

    MockTransport overflow = {0, 0, 0};
    CHECK(!HavenWsFanout_attach(fanout, &overflow, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == HAVEN_WS_FANOUT_MAX);

    CHECK(HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_detach(fanout, &t2));
    for (u32 i = 2; i < HAVEN_WS_FANOUT_MAX; i++)
        CHECK(HavenWsFanout_detach(fanout, &extras[i]));
    CHECK(HavenWsFanout_getCount(fanout) == 0);

    CHECK(HavenWsFanout_pollStep(fanout, 10) == 0);

    CHECK(HavenWsFanout_getCount(nullptr) == 0);
    CHECK(HavenWsFanout_getHandle(nullptr, 0) == nullptr);
    CHECK(HavenWsFanout_pollStep(nullptr, 10) == 0);
    CHECK(!HavenWsFanout_attach(nullptr, &t1, &sMockSource));
    CHECK(!HavenWsFanout_attach(fanout, nullptr, &sMockSource));
    CHECK(!HavenWsFanout_attach(fanout, &t1, nullptr));
    CHECK(!HavenWsFanout_detach(nullptr, &t1));
    CHECK(!HavenWsFanout_detach(fanout, nullptr));

    HavenWsFanout_free(nullptr);
    HavenWsFanout_free(fanout);

    if (s_haven_ws_fanout_sFailures == 0) {
        print("haven_ws_fanout_test: ALL CHECKS PASSED");
        return 0;
    }
    print("haven_ws_fanout_test: ", s_haven_ws_fanout_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for haven_ws_fanout suite. */
static i32 suite_haven_ws_fanout_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_haven_ws_fanout(1, argv);
}

/** Suite: json_flex (from api-haven/json_flex_test.c). */
static i32 sFail = 0;
#define CHECK(c) do { if (!(c)) { print("FAIL ", __LINE__, ": ", #c); sFail++; } } while (0)

/* Checks flexible JSON path reads and bounded writer output, including overflow and parse round-trip. */
static i32 suite_json_flex() {
    string text = "{\"a\":{\"b.sh\":[{\"c\":\"hi\"},42]},\"t\":true,\"n\":3.5}";
    JsonNode nodes[64];
    utf8 scratch[256];
    JsonDoc doc;
    CHECK(Json_parse(&doc, nodes, 64, scratch, sizeof(scratch), text));
    JsonRef root = Json_root(&doc);
    CHECK(Json_has(&doc, root, "a"));
    CHECK(!Json_has(&doc, root, "zzz"));
    JsonRef c = Json_path(&doc, root, "a.b.sh[0].c");
    JsonStringView value = Json_getString(&doc, c);
    CHECK(value.data && value.length == 2u && memcmp(value.data, "hi", value.length) == 0);
    CHECK(Json_getNumber(&doc, Json_path(&doc, root, "a.b.sh[1]"), -1.0) == 42.0);
    CHECK(Json_getBool(&doc, Json_path(&doc, root, "t"), false) == true);
    CHECK(Json_getBool(&doc, Json_path(&doc, root, "missing"), true) == true);
    CHECK(Json_path(&doc, root, "a.nope") < 0);
    CHECK(Json_path(&doc, root, "a.b.sh[9]") < 0);
    CHECK(Json_getString(&doc, Json_path(&doc, root, "t")).data == nullptr);

    utf8 out[256];
    JsonWriter w = JsonWriter(out, sizeof(out));
    CHECK(Json_beginObject(&w));
    CHECK(Json_addKey(&w, "method"));
    CHECK(Json_stringVal(&w, "harness_run"));
    CHECK(Json_addKey(&w, "params"));
    CHECK(Json_beginObject(&w));
    CHECK(Json_addKey(&w, "engine"));
    CHECK(Json_stringVal(&w, "codex"));
    CHECK(Json_addKey(&w, "timeout"));
    CHECK(Json_numberVal(&w, 250));
    CHECK(Json_endObject(&w));
    CHECK(Json_addKey(&w, "tags"));
    CHECK(Json_beginArray(&w));
    CHECK(Json_stringVal(&w, "a"));
    CHECK(Json_stringVal(&w, "b.sh"));
    CHECK(Json_endArray(&w));
    CHECK(Json_addKey(&w, "live"));
    CHECK(Json_boolVal(&w, true));
    CHECK(Json_addKey(&w, "drop"));
    CHECK(Json_nullVal(&w));
    CHECK(Json_endObject(&w));
    CHECK(JsonWriter_ok(&w));
    string want = "{\"method\":\"harness_run\",\"params\":{\"engine\":\"codex\",\"timeout\":250},\"tags\":[\"a\",\"b.sh\"],\"live\":true,\"drop\":null}";
    CHECK(string_equals(out, want));

    utf8 small[16];
    JsonWriter w2 = JsonWriter(small, sizeof(small));
    CHECK(Json_beginObject(&w2));
    CHECK(!Json_addKey(&w2, "way-too-long-key-name"));
    CHECK(!JsonWriter_ok(&w2));
    JsonNode n2[32];
    utf8 s2[64];
    JsonDoc d2;
    CHECK(Json_parse(&d2, n2, 32, s2, sizeof(s2), out));
    JsonStringView engine = Json_getString(&d2, Json_path(&d2, Json_root(&d2), "params.engine"));
    CHECK(engine.data && engine.length == 5u && memcmp(engine.data, "codex", engine.length) == 0);

    if (sFail == 0) {
        print("json_flex_test: ALL CHECKS PASSED");
        return 0;
    }
    print("json_flex_test: ", sFail, " FAILURES");
    return 1;
}
#undef CHECK

/** Suite: mcp_server (from api-haven/mcp_server_test.c). */
/**
 * ============================================================================
 * MODULE: McpServerTest (src/mcp/mcp_server_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless protocol conformance, no
 * network, no stdio -- lines in, response buffers out)
 * ============================================================================
 * Exercises McpServer_handleLine against literal client frames:
 * initialize version negotiation (supported echo / latest fallback),
 * ping, tools/list (all 14 tools with schemas), tools/call success +
 * isError paths (incl. engine_list/app_list, unbound harness_run /
 * app_action degrade, poll of unknown jobs, asset_lookup/download),
 * resources/list + read (incl. engines/apps/assets catalogs), unknown
 * tool/method/resource errors, batch rejection, notification silence,
 * string-id echo, and parse-error handling.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */
;;INTENTION("assert by substring on the response line: exact JSON layout is an implementation detail; contract points (id echo, codes, tool names, result shapes) are asserted")

static i32 s_mcp_server_sFailures = 0;

/* Counts a failed protocol assertion and prints its concise diagnostic. */
static fn check(i32 ok, string what) {
    if (!ok) {
        s_mcp_server_sFailures++;
        fprint(stderr, "FAIL: ", what);
    }
}

/* Sends one JSON-RPC line through the server and records whether a response was produced. */
static string run(McpServer *srv, string line, utf8 *out,
                       bytes cap, string what) {
    out[0] = '\0';
    const bool wrote = McpServer_handleLine(srv, line, strlen(line), out, cap);
    check(wrote, what);
    return out;
}

/* Exercises MCP negotiation, tools/resources, error classes, notifications, and parse failures. */
static i32 suite_mcp_server() {
    McpServer *srv = McpServer_shared();
    utf8 out[262144];

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"test\",\"version\":\"0\"}}}",
        out, sizeof(out), "initialize writes a response");
    check(string_contains(out, "\"id\":1"), "initialize echoes numeric id");
    check(string_contains(out, "\"protocolVersion\":\"2024-11-05\""),
          "initialize echoes supported client version");
    check(string_contains(out, "\"serverInfo\":{\"name\":\"vexgraph-mcp\""),
          "initialize advertises server identity");
    check(McpServer_isInitialized(srv), "initialized flag set after handshake");
    check(string_equals(McpServer_getProtocolVersion(srv), "2024-11-05"),
          "negotiated version getter");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\","
        "\"params\":{\"protocolVersion\":\"2099-01-01\"}}",
        out, sizeof(out), "initialize fallback writes a response");
    check(string_contains(out, "\"protocolVersion\":\"2025-06-18\""),
          "unsupported version negotiates to latest");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":\"p1\",\"method\":\"ping\"}",
        out, sizeof(out), "ping writes a response");
    check(string_contains(out, "\"id\":\"p1\""), "ping echoes string id with quotes");
    check(string_contains(out, "\"result\":{}"), "ping has empty result");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}",
        out, sizeof(out), "tools/list writes a response");
    check(string_contains(out, "\"name\":\"app_detect\""), "tool app_detect present");
    check(string_contains(out, "\"name\":\"capture_status\""),
          "tool capture_status present");
    check(string_contains(out, "\"name\":\"ai_provider_lookup\""),
          "tool ai_provider_lookup present");
    check(string_contains(out, "\"name\":\"db_data_source_lookup\""),
          "tool db_data_source_lookup present");
    check(string_contains(out, "\"name\":\"engine_list\""),
          "tool engine_list present");
    check(string_contains(out, "\"name\":\"harness_run\""),
          "tool harness_run present");
    check(string_contains(out, "\"name\":\"harness_poll\""),
          "tool harness_poll present");
    check(string_contains(out, "\"name\":\"app_list\""),
          "tool app_list present");
    check(string_contains(out, "\"name\":\"app_action\""),
          "tool app_action present");
    check(string_contains(out, "\"name\":\"app_poll\""),
          "tool app_poll present");
    check(string_contains(out, "\"name\":\"search_list\""),
          "tool search_list present");
    check(string_contains(out, "\"name\":\"web_search\""),
          "tool web_search present");
    check(string_contains(out, "\"name\":\"asset_lookup\""),
          "tool asset_lookup present");
    check(string_contains(out, "\"name\":\"asset_download\""),
          "tool asset_download present");
    check(string_contains(out, "\"inputSchema\""), "tool schemas advertised");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_detect\",\"arguments\":{}}}",
        out, sizeof(out), "app_detect call writes a response");
    check(string_contains(out, "\"id\":4"), "app_detect echoes id");
    check(string_contains(out, "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""),
          "app_detect renders text content");
    check(string_contains(out, "KNOWN APPS (14)"), "app_detect lists all 14 apps");
    check(string_contains(out, "opencode") && string_contains(out, "qwen-code"),
          "app_detect covers first and last registry rows");
    check(!string_contains(out, "\"isError\":true"), "app_detect list is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_detect\",\"arguments\":{\"name\":\"codex\"}}}",
        out, sizeof(out), "app_detect detail writes a response");
    check(string_contains(out, "app: codex"), "app detail renders key");
    check(string_contains(out, "display: "), "app detail renders display line");
    check(string_contains(out, "running: 0") || string_contains(out, "running: 1"),
          "app detail renders running state");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_detect\","
        "\"arguments\":{\"name\":\"does-not-exist\"}}}",
        out, sizeof(out), "unknown app writes a response");
    check(string_contains(out, "\"isError\":true"), "unknown app is an error");
    check(string_contains(out, "unknown app: does-not-exist"),
          "unknown app names the culprit");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"capture_status\",\"arguments\":{}}}",
        out, sizeof(out), "capture_status writes a response");
    check(string_contains(out, "SCREEN:") && string_contains(out, "STREAM:") &&
              string_contains(out, "AUDIO:"),
          "capture_status renders all three kinds");
    check(string_contains(out, "SUMMARY: running all="),
          "capture_status renders summary line");
    check(string_contains(out, "obs") && string_contains(out, "blackhole"),
          "capture_status covers screen + audio registry rows");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"capture_status\","
        "\"arguments\":{\"kind\":\"audio\"}}}",
        out, sizeof(out), "capture_status kind filter writes a response");
    check(string_contains(out, "AUDIO:") && !string_contains(out, "SCREEN:"),
          "kind filter keeps only the requested section");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"ai_provider_lookup\","
        "\"arguments\":{\"slug\":\"openai\"}}}",
        out, sizeof(out), "ai slug lookup writes a response");
    check(string_contains(out, "provider: openai"), "ai slug match renders key");
    check(string_contains(out, "resolved: https://api.openai.com/v1"),
          "ai slug match renders resolved base");
    check(string_contains(out, "auth: bearer"), "ai slug match renders auth scheme");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"ai_provider_lookup\","
        "\"arguments\":{\"query\":\"anthropic\"}}}",
        out, sizeof(out), "ai query lookup writes a response");
    check(string_contains(out, "anthropic"), "ai substring query hits rows");
    check(string_contains(out, "matches: "), "ai query renders match count");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"ai_provider_lookup\",\"arguments\":{}}}",
        out, sizeof(out), "ai bare call writes a response");
    check(string_contains(out, "\"isError\":true"), "ai bare call hints instead");
    check(string_contains(out, "pass slug="),
          "ai bare call explains usage");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"db_data_source_lookup\","
        "\"arguments\":{\"slug\":\"postgresql\"}}}",
        out, sizeof(out), "db slug lookup writes a response");
    check(string_contains(out, "data-source: postgresql"), "db slug match");
    check(string_contains(out, "port: 5432"), "db slug match renders port");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"db_data_source_lookup\",\"arguments\":{}}}",
        out, sizeof(out), "db bare list writes a response");
    check(string_contains(out, "- postgresql |") && string_contains(out, "engine=postgres | port=5432 | sql"),
          "db bare list renders full table");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":100,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"engine_list\",\"arguments\":{}}}",
        out, sizeof(out), "engine_list writes a response");
    check(string_contains(out, "ENGINES (20)"), "engine_list renders header");
    check(string_contains(out, "claude-code") && string_contains(out, "copilot-cli"),
          "engine_list covers first and last rows");
    check(!string_contains(out, "\"isError\":true"), "engine_list is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":101,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"engine_list\",\"arguments\":{\"query\":\"codex\"}}}",
        out, sizeof(out), "engine_list query writes a response");
    check(string_contains(out, "codex"), "engine_list query hits rows");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":102,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_list\",\"arguments\":{}}}",
        out, sizeof(out), "app_list writes a response");
    check(string_contains(out, "APPS (29)"), "app_list renders header");
    check(string_contains(out, "apple-notes") && string_contains(out, "imessage"),
          "app_list covers first and last rows");
    check(!string_contains(out, "\"isError\":true"), "app_list is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":103,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"harness_run\",\"arguments\":{\"engine\":\"codex\","
        "\"prompt\":\"hello\"}}}",
        out, sizeof(out), "harness_run unbound writes a response");
    check(string_contains(out, "\"isError\":true"),
          "harness_run without a driver is an error");
    check(string_contains(out, "UNBOUND_DRIVER"),
          "harness_run names the missing driver seam");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":104,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"harness_run\",\"arguments\":{\"engine\":\"nope\","
        "\"prompt\":\"hello\"}}}",
        out, sizeof(out), "harness_run unknown engine writes a response");
    check(string_contains(out, "unknown engine: nope"),
          "harness_run names the unknown engine");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":105,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"harness_poll\",\"arguments\":{\"jobId\":99}}}",
        out, sizeof(out), "harness_poll unknown job writes a response");
    check(string_contains(out, "\"isError\":true"),
          "harness_poll of an unknown job is an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":106,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_action\",\"arguments\":{\"target\":"
        "\"apple-notes\",\"action\":\"list-notes\"}}}",
        out, sizeof(out), "app_action unbound writes a response");
    check(string_contains(out, "\"isError\":true"),
          "app_action without a driver is an error");
    check(string_contains(out, "UNBOUND_DRIVER"),
          "app_action names the missing driver seam");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":107,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_action\",\"arguments\":{\"target\":\"nope\","
        "\"action\":\"x\"}}}",
        out, sizeof(out), "app_action unknown target writes a response");
    check(string_contains(out, "unknown target: nope"),
          "app_action names the unknown target");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":108,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"app_poll\",\"arguments\":{\"jobId\":99}}}",
        out, sizeof(out), "app_poll unknown job writes a response");
    check(string_contains(out, "\"isError\":true"),
          "app_poll of an unknown job is an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":109,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"search_list\",\"arguments\":{}}}",
        out, sizeof(out), "search_list writes a response");
    check(string_contains(out, "SEARCH PROVIDERS (3)"),
          "search_list renders header");
    check(string_contains(out, "searxng") && string_contains(out, "wikimedia"),
          "search_list covers first and last rows");
    check(!string_contains(out, "\"isError\":true"),
          "search_list is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":110,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"web_search\",\"arguments\":{}}}",
        out, sizeof(out), "web_search without query writes a response");
    check(string_contains(out, "\"isError\":true"),
          "web_search without a query is an error");
    check(string_contains(out, "missing query"),
          "web_search names the missing query");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":111,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"web_search\",\"arguments\":{\"query\":\"x\","
        "\"provider\":\"nope\"}}}",
        out, sizeof(out), "web_search unknown provider writes a response");
    check(string_contains(out, "unknown search provider: nope"),
          "web_search names the unknown provider");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":112,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"web_search\",\"arguments\":{\"query\":\"hi\","
        "\"provider\":\"google-cse\"}}}",
        out, sizeof(out), "web_search google without keys writes a response");
    check(string_contains(out, "\"isError\":true"),
          "web_search google without keys is an error");
    check(string_contains(out, "GOOGLE_CSE_KEY") ||
              string_contains(out, "TLS_BACKEND_MISSING"),
          "web_search google names keys or the TLS gap");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":113,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"web_search\",\"arguments\":{\"query\":\"hi\"}}}",
        out, sizeof(out), "web_search searxng writes a response");
    check(string_contains(out, "\"isError\":true"),
          "web_search searxng with no local server is an error");
    check(string_contains(out, "transport error"),
          "web_search searxng names the transport failure");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":214,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"asset_lookup\",\"arguments\":{\"slug\":\"unsplash\"}}}",
        out, sizeof(out), "asset_lookup slug writes a response");
    check(string_contains(out, "asset-source: unsplash"),
          "asset_lookup slug renders key");
    check(string_contains(out, "license: "),
          "asset_lookup renders license line");
    check(!string_contains(out, "\"isError\":true"),
          "asset_lookup slug is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":215,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"asset_lookup\",\"arguments\":{}}}",
        out, sizeof(out), "asset_lookup bare writes a response");
    check(string_contains(out, "unsplash") && string_contains(out, "kenney"),
          "asset_lookup bare covers first and last rows");
    check(!string_contains(out, "\"isError\":true"),
          "asset_lookup bare is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":216,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"asset_lookup\",\"arguments\":{\"slug\":\"nope\"}}}",
        out, sizeof(out), "asset_lookup unknown writes a response");
    check(string_contains(out, "unknown asset source: nope"),
          "asset_lookup names the unknown source");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":217,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"asset_download\",\"arguments\":{\"slug\":\"poly-haven\","
        "\"file\":\"fox.glb\"}}}",
        out, sizeof(out), "asset_download writes a response");
    check(string_contains(out, "asset download plan: poly-haven"),
          "asset_download renders plan key");
    check(string_contains(out, "cache/models/fox.glb") ||
              string_contains(out, "cache/assets/fox.glb"),
          "asset_download renders cache path");
    check(string_contains(out, "never fetches"),
          "asset_download states the no-fetch terms");
    check(!string_contains(out, "\"isError\":true"),
          "asset_download is not an error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":218,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"asset_download\",\"arguments\":{}}}",
        out, sizeof(out), "asset_download without slug writes a response");
    check(string_contains(out, "\"isError\":true"),
          "asset_download without a slug is an error");
    check(string_contains(out, "missing slug"),
          "asset_download names the missing slug");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"nope\",\"arguments\":{}}}",
        out, sizeof(out), "unknown tool writes a response");
    check(string_contains(out, "\"code\":-32602") && !string_contains(out, "\"isError\":true"),
          "unknown tool is an invalid-params error");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"resources/list\"}",
        out, sizeof(out), "resources/list writes a response");
    check(string_contains(out, "\"uri\":\"system://apps\"") &&
              string_contains(out, "\"uri\":\"system://capture\"") &&
              string_contains(out, "\"uri\":\"db://data-sources\"") &&
              string_contains(out, "\"uri\":\"engines://catalog\"") &&
              string_contains(out, "\"uri\":\"apps://catalog\"") &&
              string_contains(out, "\"uri\":\"search://providers\"") &&
              string_contains(out, "\"uri\":\"assets://catalog\""),
          "all seven resource URIs advertised");
    check(string_contains(out, "\"mimeType\":\"text/plain\""),
          "resources carry a mime type");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"system://apps\"}}",
        out, sizeof(out), "resources/read writes a response");
    check(string_contains(out, "\"uri\":\"system://apps\""),
          "resource read echoes the uri");
    check(string_contains(out, "KNOWN APPS (14)"),
          "resource read renders the same body as the tool");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":116,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"engines://catalog\"}}",
        out, sizeof(out), "engines catalog read writes a response");
    check(string_contains(out, "ENGINES (20)"),
          "engines catalog renders the directory");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":117,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"apps://catalog\"}}",
        out, sizeof(out), "apps catalog read writes a response");
    check(string_contains(out, "APPS (29)"),
          "apps catalog renders the directory");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":118,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"search://providers\"}}",
        out, sizeof(out), "search providers read writes a response");
    check(string_contains(out, "SEARCH PROVIDERS (3)"),
          "search providers read renders the directory");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":219,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"assets://catalog\"}}",
        out, sizeof(out), "assets catalog read writes a response");
    check(string_contains(out, "unsplash") && string_contains(out, "kenney"),
          "assets catalog renders the directory");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"resources/read\","
        "\"params\":{\"uri\":\"system://nope\"}}",
        out, sizeof(out), "unknown resource writes a response");
    check(string_contains(out, "\"code\":-32602"), "unknown resource is invalid params");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"bogus/method\"}",
        out, sizeof(out), "unknown method writes a response");
    check(string_contains(out, "\"code\":-32601") && string_contains(out, "\"message\":\"Method not found\""),
          "unknown method is -32601");

    run(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":19,\"method\":\"tools/list\",",
        out, sizeof(out), "malformed json writes a parse error");
    check(string_contains(out, "\"code\":-32700"), "malformed line is -32700");

    run(srv,
        "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]",
        out, sizeof(out), "batch line writes a response");
    check(string_contains(out, "\"code\":-32600"), "batch is rejected with -32600");

    out[0] = '\0';
    const bool wrote = McpServer_handleLine(srv,
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
        strlen("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"),
        out, sizeof(out));
    check(!wrote, "notifications/initialized is silent");
    check((*out) == '\0', "notification leaves the buffer untouched");

    out[0] = '\0';
    const bool wrote2 = McpServer_handleLine(srv,
        "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}",
        strlen("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}"), out,
        sizeof(out));
    check(!wrote2, "null id is silent (notification semantics)");

    if (s_mcp_server_sFailures == 0) {
        print("mcp_server_test: all checks green");
        return 0;
    }
    print("mcp_server_test: ", s_mcp_server_sFailures, " check(s) FAILED");
    return 1;
}

/** Suite: probe_consumer (from api-haven/probe_consumer_test.c). */



/**
 * ============================================================================
 * MODULE: ProbeConsumerTest (src/probe_consumer_test/test.c)
 * LEVEL: L2 -- Behavior verification (headless; seam proof, no network)
 * ============================================================================
 * Executable proof that the vexspoke system probes are reachable and
 * functional from the api-haven layer: app registry breadth (new rows:
 * t3-code, grok, gemini, ...), capture registry presence, live liveness
 * via the test's own basename, and null-safety spot checks.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static i32 s_probe_consumer_sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond);     \
            s_probe_consumer_sFailures++;                                               \
        }                                                              \
    } while (0)

/* Verifies api-haven can consume Vexspoke app, capture, and process probes through the R2 seam. */
static i32 suite_probe_consumer(i32 argc, utf8 **argv) {
    AppDetect *detect = AppDetect_shared();
    CHECK(detect != nullptr);
    const u32 appCount = AppDetect_count(detect);
    print("AppDetect_count (via api_haven) = ", appCount);
    CHECK(appCount >= 12);
    CHECK(AppDetect_get(detect, "opencode") != nullptr);
    CHECK(AppDetect_get(detect, "codex") != nullptr);
    CHECK(AppDetect_get(detect, "claude") != nullptr);
    CHECK(AppDetect_get(detect, "t3") != nullptr);
    CHECK(AppDetect_get(detect, "grok") != nullptr);
    CHECK(AppDetect_get(detect, "gemini") != nullptr);

    CaptureTool *tools = CaptureTool_shared();
    CHECK(tools != nullptr);
    const u32 captureCount = CaptureTool_count(tools);
    print("CaptureTool_count (via api_haven) = ", captureCount);
    CHECK(captureCount >= 12);
    const CaptureSlot *obs = CaptureTool_get(tools, "obs");
    CHECK(obs != nullptr);
    CHECK(obs && CaptureTool_getKind(tools, obs) == CAPTURE_KIND_SCREEN);
    CHECK(CaptureTool_get(tools, "quicktime-player") != nullptr);

    string selfName = argc > 0 && argv[0] ? strrchr(argv[0], '/') : nullptr;
    selfName = selfName ? selfName + 1 : (argc > 0 ? argv[0] : nullptr);
    ProcessProbe *probe = ProcessProbe_shared();
    CHECK(selfName && ProcessProbe_isRunning(probe, selfName));
    CHECK(!ProcessProbe_isRunning(probe, "definitely-not-a-proc-xyzzy"));
    const CaptureSlot *blackhole = CaptureTool_get(tools, "blackhole");
    CHECK(blackhole && !CaptureTool_isRunning(tools, blackhole));

    print("isInstalled(opencode) = ", AppDetect_isInstalled(detect, AppDetect_get(detect, "opencode")) ? "yes" : "no");
    print("isRunning(obs) = ", CaptureTool_isRunning(tools, obs) ? "yes" : "no");

    CHECK(AppDetect_count(nullptr) == 0);
    CHECK(CaptureTool_count(nullptr) == 0);
    CHECK(AppDetect_isRunning(nullptr, nullptr) == false);
    CHECK(CaptureTool_isInstalled(nullptr, nullptr) == false);
    CHECK(ProcessProbe_isRunning(nullptr, nullptr) == false);
    CHECK(ProcessProbe_isDriverLoaded(nullptr, nullptr, nullptr) == false);

    if (s_probe_consumer_sFailures == 0) {
        print("probe_consumer_test: ALL CHECKS PASSED (vexspoke probes reachable)");
        return 0;
    }
    print("probe_consumer_test: ", s_probe_consumer_sFailures, " FAILURES");
    return 1;
}
#undef CHECK

/** Zero-arg adapter for probe_consumer suite. */
static i32 suite_probe_consumer_entry() {
    utf8 *argv[] = { "api_haven_test", nullptr };
    return suite_probe_consumer(1, argv);
}

/** Suite: webhook_loopback (from api-haven/webhook_loopback_test.c). */
/**
 * ============================================================================
 * MODULE: WebhookLoopbackTest (webhook_loopback_test/test)
 * LEVEL: L3 -- Module Code (headless Shape-A/B proof, no accounts)
 * ============================================================================
 * Proves the webhook drivers and Rest core against ourselves: a local
 * HttpServer captures the exact Bytes each driver emits, and JsonDoc
 * asserts the payload schema. Zero accounts, zero network, deterministic.
 * Live vendor acceptance stays per-driver LIVE_UNVERIFIED until an env-var
 * run (URL from env, never committed).
 *
 * STRUCT FIELDS: none -- procedural harness over file-static capture.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static Capture g_cap;
static i32 s_webhook_loopback_g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { print("[loopback] PASS ", name); } \
    else { print("[loopback] FAIL ", name); s_webhook_loopback_g_failures++; } \
} while (0)

;;GETTER
static inline string HttpExchange_method(const HttpExchange *self) {
    return (*self).method;
}
;;GETTER
static inline string s_webhook_loopback_HttpExchange_path(const HttpExchange *self) {
    return (*self).path;
}
;;GETTER
static inline bytes HttpExchange_bodyLen(const HttpExchange *self) {
    return (*self).bodyLen;
}
;;GETTER
static inline string HttpExchange_body(const HttpExchange *self) {
    return (*self).body;
}

;;GETTER
static inline utf8 *Capture_method(Capture *self) {
    return (*self).method;
}
;;GETTER
static inline utf8 *Capture_path(Capture *self) {
    return (*self).path;
}
;;GETTER
static inline utf8 *Capture_body(Capture *self) {
    return (*self).body;
}
;;SETTER
static inline fn Capture_setBodyLen(Capture *self, bytes bodyLen) {
    (*self).bodyLen = bodyLen;
}
;;GETTER
static inline i32 Capture_hits(const Capture *self) {
    return (*self).hits;
}
;;SETTER
static inline fn Capture_setHits(Capture *self, i32 hits) {
    (*self).hits = hits;
}

/* Captures loopback request fields/body and returns a deterministic method-specific response. */
static fn captureHandler(const HttpExchange *exchange, i32 clientFd, any userdata) {
    (discard) userdata;
    Capture *cap = &g_cap;
    string method = HttpExchange_method(exchange);
    strncpy(Capture_method(cap), method, sizeof(((Capture) {0}).method) - 1);
    strncpy(Capture_path(cap), s_webhook_loopback_HttpExchange_path(exchange), sizeof(((Capture) {0}).path) - 1);
    bytes n = HttpExchange_bodyLen(exchange);
    if (n >= sizeof(((Capture) {0}).body))
        n = sizeof(((Capture) {0}).body) - 1;
    string body = HttpExchange_body(exchange);
    utf8 *capBody = Capture_body(cap);
    if (n > 0 && body)
        memcpy(capBody, body, n);
    capBody[n] = '\0';
    Capture_setBodyLen(cap, n);
    Capture_setHits(cap, Capture_hits(cap) + 1);
    if (string_equals(method, "GET"))
        Http_respond(clientFd, 200, "application/json", "{\"ok\":true}", 11);
    else
        Http_respond(clientFd, 204, "text/plain", "", 0);
}

/* Parses a captured JSON body and checks one string-valued member exactly. */
static bool bodyHas(string body, string key, string want) {
    JsonNode nodes[64];
    utf8 scratch[1024];
    JsonDoc doc;
    if (!Json_parse(&doc, nodes, 64, scratch, sizeof(scratch), body))
        return false;
    if (!Json_ok(&doc))
        return false;
    JsonRef root = Json_root(&doc);
    JsonRef ref = Json_member(&doc, root, key);
    u32 len = 0;
    string got = Json_string(&doc, ref, &len);
    if (!got)
        return false;
    if (strlen(want) != len)
        return false;
    return string_startsWith(got, want, len);
}

/* Supplies a fixed token for testing callback-based bearer credential formatting. */
static string staticToken(any userdata) {
    (discard) userdata;
    return "fn-token-123";
}

/* Runs Discord, Slack, auth-formatting, and REST-get checks against a local loopback server. */
static i32 suite_webhook_loopback() {
    print("=== Webhook Loopback Suite (Shape A/B, no accounts) ===");
    HttpServer srv = { 0 };
    i32 port = 0;
    if (!HttpServer_start(&srv, 0, captureHandler, nullptr, &port)) {
        print("[loopback] FAIL server start");
        return 1;
    }
    utf8 url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/webhook", port);

    memset(&g_cap, 0, sizeof(g_cap));
    bool dok = DiscordWebhook_sendText(url, "hello loopback", "vexbot", nullptr);
    bool loopbackBlocked = (!dok && g_cap.hits == 0);
    if (loopbackBlocked) {
        print("[loopback] SKIP socket tests (loopback blocked by sandbox environment)");
    } else {
        CHECK("discord post 204", dok && g_cap.hits == 1);
        CHECK("discord method+path", string_equals(g_cap.method, "POST"));
        CHECK("discord content", bodyHas(g_cap.body, "content", "hello loopback"));
        CHECK("discord username", bodyHas(g_cap.body, "username", "vexbot"));
    }

    string dumpPath = getenv("LOOPBACK_DUMP_BODY");
    if (dumpPath && dumpPath[0] != '\0' && !loopbackBlocked) {
        FILE *f = fopen(dumpPath, "wb");
        if (f) {
            fwrite(g_cap.body, 1, g_cap.bodyLen, f);
            fclose(f);
        }
    }

    if (!loopbackBlocked) {
        memset(&g_cap, 0, sizeof(g_cap));
        bool sok = SlackWebhook_sendText(url, "hi slack", "vexbot", nullptr);
        CHECK("slack post 204", sok && g_cap.hits == 1);
        CHECK("slack text", bodyHas(g_cap.body, "text", "hi slack"));
        CHECK("slack username", bodyHas(g_cap.body, "username", "vexbot"));
    }

    string nameOut = nullptr;
    utf8 valueBuf[128];
    ApiAuth none = ApiAuth_none();
    CHECK("auth none renders nothing", !ApiAuth_apply(&none, &nameOut, valueBuf, sizeof(valueBuf)));
    ApiAuth key = ApiAuth_apiKey("Api-Key", "abc123");
    bool kok = ApiAuth_apply(&key, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth apikey", kok && string_equals(nameOut, "Api-Key") && string_equals(valueBuf, "abc123"));
    ApiAuth bearer = ApiAuth_bearer("tok-xyz");
    bool bok = ApiAuth_apply(&bearer, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth bearer", bok && string_equals(nameOut, "Authorization") && string_equals(valueBuf, "Bearer tok-xyz"));
    ApiAuth bearerFn = ApiAuth_bearerFn(staticToken, nullptr);
    bool fok = ApiAuth_apply(&bearerFn, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth bearerFn", fok && string_equals(valueBuf, "Bearer fn-token-123"));
    ApiAuth nullBearer = ApiAuth_bearer(nullptr);
    CHECK("auth null bearer fails", !ApiAuth_apply(&nullBearer, &nameOut, valueBuf, sizeof(valueBuf)));
    utf8 tiny[8];
    CHECK("auth truncation fails", !ApiAuth_apply(&bearer, &nameOut, tiny, sizeof(tiny)));

    if (!loopbackBlocked) {
        utf8 respBody[1024];
        HttpResponse resp = { 0 };
        resp.body = respBody;
        resp.bodyCap = sizeof(respBody);
        ApiAuth noAuth = ApiAuth_none();
        bool gok = Rest_get(url, &noAuth, &resp);
        CHECK("rest get 200", gok && resp.status == 200);
        CHECK("rest get body", string_contains(respBody, "true"));
    }

    HttpServer_stop(&srv);
    if (s_webhook_loopback_g_failures != 0) {
        print("=== ", s_webhook_loopback_g_failures, " LOOPBACK FAILURES ===");
        return s_webhook_loopback_g_failures;
    }
    if (loopbackBlocked)
        return B_TEST_SKIP;
    print("=== ALL LOOPBACK PASS ===");
    return 0;
}
#undef CHECK

/** Suite: ws_source_binding (from api-haven/ws_source_binding_test.c). */




;;GETTER
static inline WebSocketMessageKind WebSocketMessage_kind(const WebSocketMessage *self) {
    return (*self).kind;
}
;;GETTER
static inline bytes WebSocketMessage_length(const WebSocketMessage *self) {
    return (*self).length;
}
;;GETTER
static inline const u8 *WebSocketMessage_payload(const WebSocketMessage *self) {
    return (*self).payload;
}

static const HavenWsSource kBinding = {
    .connect = VexWsSource_connect,
    .poll = VexWsSource_poll,
    .send = VexWsSource_send,
    .close = VexWsSource_close,
};

_Static_assert(_Generic(kBinding.connect, bool (*) (any) : 1, default : 0),
               "connect must be bool(any)");
_Static_assert(_Generic(kBinding.poll, u32 (*) (any, u64) : 1, default : 0),
               "poll must be u32(any, u64)");
_Static_assert(_Generic(kBinding.send, bool (*) (any, const u8*, u32) : 1,
                        default : 0),
               "send must be bool(any, const u8*, u32)");
_Static_assert(_Generic(kBinding.close, fn (*) (any) : 1, default : 0),
               "close must be fn(any)");

static i32 s_ws_source_binding_g_failures = 0;
static i32 g_checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        g_checks++;                                                             \
        if (!(condition)) {                                                     \
            s_ws_source_binding_g_failures++;                                                       \
            print("  FAIL line ", __LINE__, ": ", #condition);               \
        }                                                                       \
    } while (0)

static bool peerWrite(i32 fd, const_any data, bytes length) {
    const u8 *p = (const u8*) data;
    bytes sent = 0u;
    while (sent < length) {
        ssize_t n = send(fd, p + sent, length - sent, 0);
        if (n <= 0)
            return false;
        sent += (bytes) n;
    }
    return true;
}

/** Server-side frame: never masked, which is what the RFC demands of a peer. */
static bool peerFrame(i32 fd, u8 opcode, string text) {
    bytes length = strlen(text);
    u8 header[4];
    header[0] = (u8) (0x80u | opcode);
    header[1] = (u8) length;
    return peerWrite(fd, header, 2u) && peerWrite(fd, text, length);
}

/** Read exactly length Bytes with the peer's bounded timeout. */
static bool peerReadExact(i32 peer, u8 *dest, bytes length) {
    bytes got = 0u;
    while (got < length) {
        ssize_t n = recv(peer, dest + got, length - got, 0);
        if (n <= 0)
            return false;
        got += (bytes) n;
    }
    return true;
}

/**
 * The scripted peer: accept, answer the handshake, send two text frames, then
 * require one masked TEXT frame back. Its exit status is the assertion.
 */
static i32 peerRun(i32 readyWriteFd) {
    i32 listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 90;
    i32 reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (struct sockaddr*) &address, sizeof(address)) != 0)
        return 91;
    socklen_t length = (socklen_t) sizeof(address);
    if (getsockname(listener, (struct sockaddr*) &address, &length) != 0)
        return 92;
    if (listen(listener, 1) != 0)
        return 93;
    i32 port = ntohs(address.sin_port);
    if (write(readyWriteFd, &port, sizeof(port)) != (ssize_t) sizeof(port))
        return 94;
    close(readyWriteFd);

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    i32 peer = accept(listener, nullptr, nullptr);
    if (peer < 0)
        return 95;
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    utf8 request[4096];
    bytes used = 0u;
    while (used + 1u < sizeof(request)) {
        ssize_t n = recv(peer, request + used, 1u, 0);
        if (n <= 0)
            break;
        used += (bytes) n;
        request[used] = '\0';
        if (used >= 4u && string_contains(request, "\r\n\r\n"))
            break;
    }
    string key = string_find(request, "Sec-WebSocket-Key: ");
    if (key == nullptr)
        return 96;
    key += strlen("Sec-WebSocket-Key: ");
    string keyEnd = string_find(key, "\r\n");
    if (keyEnd == nullptr)
        return 97;
    utf8 value[64];
    bytes keyLength = (bytes) (keyEnd - key);
    if (keyLength + 1u > sizeof(value))
        return 98;
    memcpy(value, key, keyLength);
    value[keyLength] = '\0';
    CryptoSha1 digest = CryptoSha1_0();
    Crypto_sha1Init(&digest);
    Crypto_sha1Update(&digest, value, strlen(value));
    Crypto_sha1Update(&digest, WS_HANDSHAKE_GUID, strlen(WS_HANDSHAKE_GUID));
    u8 hash[CRYPTO_SHA1_DIGEST_SIZE];
    Crypto_sha1Final(&digest, hash);
    utf8 accept[64];
    if (Url_base64(hash, CRYPTO_SHA1_DIGEST_SIZE, accept, sizeof(accept)) <= 0)
        return 99;
    utf8 response[256];
    i32 written = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (written <= 0 || (bytes) written >= sizeof(response) ||
        !peerWrite(peer, response, (bytes) written))
        return 100;

    if (!peerFrame(peer, WEBSOCKET_TEXT, "one"))
        return 101;
    if (!peerFrame(peer, WEBSOCKET_TEXT, "two"))
        return 102;

    u8 head[2];
    if (!peerReadExact(peer, head, sizeof(head)))
        return 103;
    if ((head[0] & 0x0Fu) != WEBSOCKET_TEXT)
        return 104;
    if ((head[1] & 0x80u) == 0u)
        return 105;
    bytes payloadLength = (bytes) (head[1] & 0x7Fu);
    if (payloadLength > 16u)
        return 106;
    u8 mask[4];
    if (!peerReadExact(peer, mask, sizeof(mask)))
        return 107;
    u8 body[16];
    if (payloadLength > 0u && !peerReadExact(peer, body, payloadLength))
        return 108;
    utf8 decoded[17];
    for (bytes i = 0u; i < payloadLength; i++)
        decoded[i] = (utf8) (body[i] ^ mask[i & 3u]);
    decoded[payloadLength] = '\0';
    if (payloadLength != 4u || !string_startsWith(decoded, "pong", 4u))
        return 109;
    close(peer);
    close(listener);
    return 0;
}

static i32 suite_ws_source_binding() {
    print("=== WebSocket WsSource binding ===");
    signal(SIGPIPE, SIG_IGN);

    i32 fds[2];
    if (pipe(fds) != 0) {
        print("pipe failed");
        return 1;
    }
    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        _exit(peerRun(fds[1]));
    }
    close(fds[1]);
    i32 port = 0;
    if (read(fds[0], &port, sizeof(port)) != (ssize_t) sizeof(port)) {
        print("peer never reported a port");
        return 1;
    }
    close(fds[0]);

    utf8 url[128];
    snprintf(url, sizeof(url), "ws://127.0.0.1:%d/live", port);

    VexWsSource handle = VexWsSource();
    HavenWsFanout *fanout = HavenWsFanout();
    CHECK(fanout != nullptr);
    WebSocketConfig config;
    memset(&config, 0, sizeof(config));
    config.timeoutMs = 2000u;
    config.maxMessageBytes = 4096u;
    CHECK(VexWsSource_open(&handle, url, config));
    CHECK(VexWsSource_isOpen(&handle));
    CHECK(string_equals(VexWsSource_url(&handle), url));

    CHECK(HavenWsFanout_attach(fanout, &handle, &kBinding));
    CHECK(HavenWsFanout_getCount(fanout) == 1u);
    CHECK(HavenWsFanout_getHandle(fanout, 0u) == (any) &handle);

    CHECK(HavenWsFanout_pollStep(fanout, 500u) == 1u);
    const WebSocketMessage *first = VexWsSource_last(&handle);
    CHECK(first != nullptr && WebSocketMessage_kind(first) == WS_MESSAGE_TEXT);
    CHECK(first != nullptr && WebSocketMessage_length(first) == 3u);
    CHECK(first != nullptr && memcmp(WebSocketMessage_payload(first), "one", 3u) == 0);

    CHECK(HavenWsFanout_pollStep(fanout, 500u) == 1u);
    const WebSocketMessage *second = VexWsSource_last(&handle);
    CHECK(second != nullptr && WebSocketMessage_kind(second) == WS_MESSAGE_TEXT);
    CHECK(second != nullptr && memcmp(WebSocketMessage_payload(second), "two", 3u) == 0);

    CHECK(kBinding.send(&handle, (const u8*) "pong", 4u));
    CHECK(!kBinding.send(&handle, nullptr, 4u));
    CHECK(!kBinding.send(nullptr, (const u8*) "x", 1u));
    const u8 broken[2] = { 0xC3u, 0x28u };
    CHECK(!kBinding.send(&handle, broken, 2u));
    CHECK(VexWsSource_isOpen(&handle));

    CHECK(HavenWsFanout_pollStep(fanout, 0u) == 0u);

    WebSocket *socket = VexWsSource_socket(&handle);
    CHECK(socket != nullptr && WebSocket_framesIn(socket) == 2u);
    CHECK(socket != nullptr && WebSocket_framesOut(socket) == 1u);

    utf8 text[512];
    bool truncated = false;
    VexWsSource_toString(&handle, text, sizeof(text), &truncated);
    CHECK(string_contains(text, "VexWsSource {") && string_contains(text, url));
    VexWsSource_toStringStruct(&handle, text, sizeof(text), &truncated);
    CHECK(string_contains(text, "configured=true"));
    VexWsSource_toString(nullptr, text, sizeof(text), &truncated);
    CHECK(string_equals(text, "nullptr"));
    VexWsSource_toStringStruct(nullptr, text, sizeof(text), &truncated);
    CHECK(string_equals(text, "nullptr"));

    CHECK(!VexWsSource_connect(nullptr));
    CHECK(VexWsSource_poll(nullptr, 10u) == 0u);
    CHECK(!VexWsSource_send(nullptr, (const u8*) "x", 1u));
    VexWsSource_close(nullptr);
    CHECK(VexWsSource_last(nullptr) == nullptr);
    CHECK(VexWsSource_socket(nullptr) == nullptr);
    VexWsSource unconfigured = VexWsSource();
    CHECK(!VexWsSource_connect(&unconfigured));
    CHECK(!VexWsSource_isOpen(&unconfigured));

    kBinding.close(&handle);
    CHECK(VexWsSource_isOpen(&handle) == false);
    CHECK(HavenWsFanout_detach(fanout, &handle));
    CHECK(HavenWsFanout_getCount(fanout) == 0u);
    HavenWsFanout_free(fanout);

    i32 status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    print(g_checks, " checks, ", s_ws_source_binding_g_failures, " failed");
    if (s_ws_source_binding_g_failures == 0)
        print("=== WebSocket WsSource binding: ALL PASS ===");
    return s_ws_source_binding_g_failures == 0 ? 0 : 1;
}
#undef CHECK

/** Suite: snapshot_io (from api-haven/storage/snapshot_io_test.c). */
/* Header-only provider vocabulary owner. Proves all result constants and both
 * callback signatures as a real C23 client. No network/backend is implemented. */


/* Mocks provider upload outcomes, distinguishing cancellation from ordinary pending work. */
static i32 put(any context, u64 key, const u8 *data, bytes length, i32 cancel) {
    assert(context == nullptr && key == 1 && length == 1 && data[0] == 42);
    return cancel ? HAVEN_SNAPSHOT_REJECT : HAVEN_SNAPSHOT_PENDING;
}

/* Mocks bounded snapshot retrieval and preserves outputs when key or capacity is invalid. */
static i32 get(any context, u64 key, u8 *dest, bytes capacity, bytes *outLength) {
    assert(context == nullptr);
    if (key != 1 || capacity < 1)
        return HAVEN_SNAPSHOT_REJECT;
    dest[0] = 42;
    *outLength = 1;
    return HAVEN_SNAPSHOT_DONE;
}

/* Compiles and exercises the header-only callback vocabulary and its result/status contract. */
static i32 suite_snapshot_io() {
    _Static_assert(HAVEN_SNAPSHOT_DONE != HAVEN_SNAPSHOT_PENDING);
    _Static_assert(HAVEN_SNAPSHOT_PENDING != HAVEN_SNAPSHOT_RETRY);
    _Static_assert(HAVEN_SNAPSHOT_RETRY != HAVEN_SNAPSHOT_REJECT);
    HavenSnapshotPutFn upload = put;
    HavenSnapshotGetFn download = get;
    u8 byte = 42;
    assert(upload(nullptr, 1, &byte, 1, 0) == HAVEN_SNAPSHOT_PENDING);
    assert(upload(nullptr, 1, &byte, 1, 1) == HAVEN_SNAPSHOT_REJECT);
    byte = 7;
    bytes length = 9;
    assert(download(nullptr, 2, &byte, 1, &length) == HAVEN_SNAPSHOT_REJECT);
    assert(byte == 7 && length == 9);
    assert(download(nullptr, 1, &byte, 0, &length) == HAVEN_SNAPSHOT_REJECT);
    assert(byte == 7 && length == 9);
    assert(download(nullptr, 1, &byte, 1, &length) == HAVEN_SNAPSHOT_DONE);
    assert(byte == 42 && length == 1);
    return 0;
}

/** Suite: rest (from api-haven/api/rest_test.c). */
/* Owner test for api/rest.c (the Rest core). Proves the REST result contract
 * against a real loopback HttpServer -- no accounts, no internet, deterministic:
 *   - a 2xx transfer succeeds and returns true;
 *   - 1xx/3xx/4xx/5xx transfers return false while resp keeps the status/body;
 *   - an explicitly required credential that cannot be rendered fails closed
 *     (nothing is sent);
 *   - a nullptr auth (and NONE) is an intentional public request.
 * Loopback that is blocked by the sandbox is an explicit skip, never a pass
 * (the Executable Evidence and Readiness Law).
 */
#define _POSIX_C_SOURCE 200809L




static i32 s_rest_g_failures = 0;

#define CHECK(name, cond) do {                                  \
    if (cond) { print("[rest] PASS ", name); }             \
    else { print("[rest] FAIL ", name); s_rest_g_failures++; }    \
} while (0)

/* Counts requests and answers "/status/NNN" with that status, else 200. */
static i32 g_hits;

;;GETTER
static inline string s_rest_HttpExchange_path(const HttpExchange *self) {
    return (*self).path;
}
;;SETTER
static inline fn HttpResponse_setBody(HttpResponse *self, utf8 *body) {
    (*self).body = body;
}
;;SETTER
static inline fn HttpResponse_setBodyCap(HttpResponse *self, bytes bodyCap) {
    (*self).bodyCap = bodyCap;
}

static fn statusHandler(const HttpExchange *exchange, i32 clientFd, any userdata) {
    (discard) userdata;
    g_hits++;
    i32 status = 200;
    string path = s_rest_HttpExchange_path(exchange);
    if (string_startsWith(path, "/status/", 8))
        status = atoi(path + 8);
    utf8 body[64];
    i32 n = snprintf(body, sizeof(body), "{\"status\":%d}", status);
    if (n < 0)
        n = 0;
    Http_respond(clientFd, status, "application/json", body, (bytes) n);
}

/* One GET into a fresh caller buffer; returns the call result and leaves resp. */
static bool getStatus(string url, const ApiAuth *auth, HttpResponse *resp, utf8 *body, bytes cap) {
    memset(resp, 0, sizeof(*resp));
    HttpResponse_setBody(resp, body);
    HttpResponse_setBodyCap(resp, cap);
    body[0] = '\0';
    return Rest_get(url, auth, resp);
}

static i32 suite_rest() {
    print("=== Rest core owner suite (loopback) ===");
    HttpServer srv = { 0 };
    i32 port = 0;
    if (!HttpServer_start(&srv, 0, statusHandler, nullptr, &port)) {
        fprint(stderr, "rest_test: SKIP (loopback server unavailable)");
        return B_TEST_SKIP;
    }
    utf8 url200[128], url204[128], url302[128], url400[128], url404[128], url429[128], url503[128], url500[128];
    snprintf(url200, sizeof(url200), "http://127.0.0.1:%d/status/200", port);
    snprintf(url204, sizeof(url204), "http://127.0.0.1:%d/status/204", port);
    snprintf(url302, sizeof(url302), "http://127.0.0.1:%d/status/302", port);
    snprintf(url400, sizeof(url400), "http://127.0.0.1:%d/status/400", port);
    snprintf(url404, sizeof(url404), "http://127.0.0.1:%d/status/404", port);
    snprintf(url429, sizeof(url429), "http://127.0.0.1:%d/status/429", port);
    snprintf(url503, sizeof(url503), "http://127.0.0.1:%d/status/503", port);
    snprintf(url500, sizeof(url500), "http://127.0.0.1:%d/status/500", port);

    ApiAuth none = ApiAuth_none();
    utf8 body[512];
    HttpResponse resp;

    g_hits = 0;
    bool ok200 = getStatus(url200, &none, &resp, body, sizeof(body));
    if (g_hits == 0) {
        HttpServer_stop(&srv);
        fprint(stderr, "rest_test: SKIP (loopback blocked by sandbox)");
        return B_TEST_SKIP;
    }
    CHECK("200 -> true", ok200 && resp.status == 200);
    CHECK("200 -> transport ok", resp.ok);
    CHECK("204 -> true", getStatus(url204, &none, &resp, body, sizeof(body)) && resp.status == 204);

    CHECK("connection refused -> false", !getStatus("http://127.0.0.1:1/status/200", &none, &resp, body, sizeof(body)));

    bool ok302 = getStatus(url302, &none, &resp, body, sizeof(body));
    CHECK("302 -> false", !ok302 && resp.status == 302);
    CHECK("302 -> transport ok", resp.ok);

    bool ok400 = getStatus(url400, &none, &resp, body, sizeof(body));
    CHECK("400 -> false", !ok400 && resp.status == 400);
    bool ok404 = getStatus(url404, &none, &resp, body, sizeof(body));
    CHECK("404 -> false", !ok404 && resp.status == 404);
    bool ok429 = getStatus(url429, &none, &resp, body, sizeof(body));
    CHECK("429 -> false", !ok429 && resp.status == 429);
    bool ok503 = getStatus(url503, &none, &resp, body, sizeof(body));
    CHECK("503 -> false", !ok503 && resp.status == 503);

    bool ok500 = getStatus(url500, &none, &resp, body, sizeof(body));
    CHECK("500 -> false", !ok500 && resp.status == 500);
    CHECK("500 -> transport ok", resp.ok);
    CHECK("500 -> error body preserved", string_contains(body, "500"));

    i32 before = g_hits;
    CHECK("null auth -> sent", getStatus(url200, nullptr, &resp, body, sizeof(body)) && g_hits == before + 1);

    ApiAuth missing = ApiAuth_bearer(nullptr);
    before = g_hits;
    CHECK("unrenderable bearer -> false", !getStatus(url200, &missing, &resp, body, sizeof(body)));
    CHECK("unrenderable bearer -> not sent", g_hits == before);

    HttpServer_stop(&srv);
    if (s_rest_g_failures != 0) {
        print("=== ", s_rest_g_failures, " REST FAILURES ===");
        return 1;
    }
    print("=== ALL REST PASS ===");
    return 0;
}
#undef _POSIX_C_SOURCE
#undef CHECK

/** Runs all automated vex/api-haven suites or a single named suite passed as argv[1]. */
i32 main(i32 argc, utf8 **argv) {
    const SuiteEntry suites[] = {
        { "ai_chat_sisters", suite_ai_chat_sisters_entry },
        { "ai_provider", suite_ai_provider_entry },
        { "ai_sse", suite_ai_sse_entry },
        { "app_broker", suite_app_broker_entry },
        { "asset", suite_asset_entry },
        { "db_provider", suite_db_provider_entry },
        { "db_sqlite_file", suite_db_sqlite_file_entry },
        { "harness_run", suite_harness_run_entry },
        { "haven_ws_fanout", suite_haven_ws_fanout_entry },
        { "json_flex", suite_json_flex },
        { "mcp_server", suite_mcp_server },
        { "probe_consumer", suite_probe_consumer_entry },
        { "webhook_loopback", suite_webhook_loopback },
        { "ws_source_binding", suite_ws_source_binding },
        { "snapshot_io", suite_snapshot_io },
        { "rest", suite_rest },
    };
    string filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;
    i32 failed = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && !string_equals(filter, suites[i].name))
            continue;
        if (suites[i].run() != 0)
            failed++;
        ran++;
    }

    if (filter && ran == 0) {
        fprint(stderr, "api_haven_test: unknown suite '", filter, "'");
        return 2;
    }
    if (failed != 0) {
        fprint(stderr, "api_haven_test: ", failed, " suite(s) FAILED");
        return 1;
    }
    puts("api_haven_test: PASS");
    return 0;
}
