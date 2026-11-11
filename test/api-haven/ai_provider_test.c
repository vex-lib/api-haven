#include "annotation/annotation.h"
#include "ai/ai_provider.h"
#include "ai/ai_chat.h"
#include "net/json.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: AiProviderTest (src/ai/ai_provider_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; no network, no accounts)
 * ============================================================================
 * Executable proof of the src/ai/ directory: exercises the AiProvider
 * table (count, at, get, resolve, null-safety, immutable getters), the
 * directory-wide invariants of the generated rows (unique slugs and
 * display names, slug grammar, base-URL scheme/format), and the AiChat
 * buildRequest path (URL resolution, auth kind, JSON envelope rendering)
 * by parsing the generated body back with net/json.
 *
 * Zero HTTP: AiChat_complete is only asserted for its NULL-resp guard.
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
    {"user", "say \"hi\" \n to me"},
    {"assistant", "hello"},
};

// [a-z0-9]+(-[a-z0-9]+)* — canonical slug grammar (generator-enforced).
static int isSlugValid(const char *slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (const char *p = slug; *p; p++) {
        char c = *p;
        int isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
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
static int hasWhitespace(const char *s) {
    if (!s)
        return 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')
            return 1;
    }
    return 0;
}

/* Validates provider-directory invariants and parses generated AiChat request bodies without network access. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- directory shape ---------------------------------------------------
    AiProvider *dir = AiProvider_shared();
    CHECK(dir != nullptr);
    const u32 total = AiProvider_count(dir);
    printf("AiProvider_count = %u\n", total);
    CHECK(total >= 255 && total <= 280);
    CHECK(AiProvider_at(dir, 0) != nullptr);
    CHECK(AiProvider_at(dir, total) == nullptr);
    CHECK(AiProvider_at(dir, total + 5) == nullptr);

    // --- verified rows ------------------------------------------------------
    const AiProviderSlot *openai = AiProvider_get(dir, "openai");
    CHECK(openai != nullptr);
    CHECK(openai && strcmp(AiProvider_getDisplayName(dir, openai), "OpenAI") == 0);
    CHECK(openai && AiProvider_getFamily(dir, openai) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(openai && AiProvider_getAuth(dir, openai) == AI_PROVIDER_AUTH_BEARER);
    CHECK(openai && AiProvider_getBaseUrl(dir, openai) != nullptr);
    CHECK(openai && strcmp(AiProvider_resolveBaseUrl(dir, openai),
                           "https://api.openai.com/v1") == 0);

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

    // --- directory consistency (every row, generated-data invariants) --------
    const char *names[280];
    u32 nameCount = 0;
    for (u32 i = 0; i < total; i++) {
        const AiProviderSlot *row = AiProvider_at(dir, i);
        CHECK(row != nullptr);
        CHECK(row && isSlugValid(AiProvider_getSlug(dir, row)));
        // slug uniqueness: get() must resolve every row back to itself
        CHECK(row && AiProvider_get(dir, AiProvider_getSlug(dir, row)) == row);
        const char *rowName = row ? AiProvider_getDisplayName(dir, row) : nullptr;
        CHECK(rowName && (*rowName) != '\0');
        if (row && rowName) {
            int dup = 0;
            for (u32 j = 0; j < nameCount; j++) {
                if (strcmp(names[j], rowName) == 0)
                    dup = 1;
            }
            CHECK(!dup); // menu-safe: no two rows share a display name
            names[nameCount++] = rowName;
            const char *base = AiProvider_getBaseUrl(dir, row);
            if (base) {
                CHECK(strncmp(base, "https://", 8) == 0 || strncmp(base, "http://", 7) == 0);
                CHECK(!hasWhitespace(base));
                bytes baseLen = strlen(base);
                CHECK(baseLen > 0 && base[baseLen - 1] != '/');
            }
        }
    }

    // --- family-default resolution on NULL-base rows -----------------------
    const AiProviderSlot *meta = AiProvider_get(dir, "meta");
    CHECK(meta && AiProvider_getBaseUrl(dir, meta) == nullptr);
    CHECK(meta && strcmp(AiProvider_resolveBaseUrl(dir, meta),
                         "https://openrouter.ai/api/v1") == 0);

    const AiProviderSlot *vertexAnthropic = AiProvider_get(dir, "vertex-anthropic");
    CHECK(vertexAnthropic && AiProvider_getBaseUrl(dir, vertexAnthropic) == nullptr);
    CHECK(vertexAnthropic && strcmp(AiProvider_resolveBaseUrl(dir, vertexAnthropic),
                                    "https://api.anthropic.com/v1") == 0);

    // --- null-safety (Rule 24) ----------------------------------------------
    CHECK(AiProvider_count(nullptr) == 0);
    CHECK(AiProvider_get(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_get(dir, nullptr) == nullptr);
    CHECK(AiProvider_at(nullptr, 0) == nullptr);
    CHECK(AiProvider_getDisplayName(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getSlug(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getNote(nullptr, nullptr) == nullptr);
    CHECK(AiProvider_getFamily(nullptr, nullptr) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(AiProvider_resolveBaseUrl(nullptr, nullptr) == nullptr);

    // --- AiChat: URL resolution + auth ---------------------------------------
    AiChat chat = AiChat("gpt-4o-mini", "sk-test");
    AiChat_setPeer(&chat, openai);

    char body[2048];
    char url[512];
    ApiAuth auth = ApiAuth_none();

    CHECK(AiChat_buildRequest(&chat, sMessages, 2,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://api.openai.com/v1/chat/completions") == 0);
    CHECK(auth.kind == API_AUTH_BEARER);
    CHECK(strcmp(auth.credential, "sk-test") == 0);

    // --- AiChat: envelope JSON round-trips through net/json ------------------
    JsonNode nodes[128];
    char scratch[1024];
    JsonDoc doc;
    CHECK(Json_parse(&doc, nodes, 128, scratch, sizeof(scratch), body));
    JsonRef root = Json_root(&doc);

    JsonRef model = Json_member(&doc, root, "model");
    u32 modelLen = 0;
    const char *modelView = model >= 0 ? Json_string(&doc, model, &modelLen) : "";
    // Note: Json_string views are NUL-terminated only when escaped into
    // scratch — plain views point into src and need the length (memcmp).
    CHECK(model >= 0 && modelLen == 11 && memcmp(modelView, "gpt-4o-mini", 11) == 0);

    JsonRef msgs = Json_member(&doc, root, "messages");
    CHECK(Json_count(&doc, msgs) == 2);
    JsonRef m0 = Json_at(&doc, msgs, 0);
    JsonRef c0 = Json_member(&doc, m0, "content");
    CHECK(c0 >= 0);
    CHECK(c0 >= 0 && strcmp(Json_string(&doc, c0, nullptr), "say \"hi\" \n to me") == 0);
    JsonRef m1 = Json_at(&doc, msgs, 1);
    JsonRef r1 = Json_member(&doc, m1, "role");
    u32 roleLen = 0;
    const char *roleView = r1 >= 0 ? Json_string(&doc, r1, &roleLen) : "";
    CHECK(r1 >= 0 && roleLen == 9 && memcmp(roleView, "assistant", 9) == 0);

    // --- AiChat: override + gateway + no-auth paths --------------------------
    AiChat_setBaseUrl(&chat, "https://gateway.example");
    CHECK(AiChat_buildRequest(&chat, sMessages, 2,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://gateway.example/chat/completions") == 0);

    AiChat bare = AiChat("grok-2-latest"); // peer NULL -> gateway
    CHECK(AiChat_buildRequest(&bare, sMessages, 1,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://openrouter.ai/api/v1/chat/completions") == 0);
    CHECK(auth.kind == API_AUTH_NONE);

    // --- AiChat: guard paths (no network) ------------------------------------
    CHECK(!AiChat_buildRequest(nullptr, sMessages, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_buildRequest(&bare, nullptr, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_buildRequest(&bare, sMessages, 0,
                               nullptr, 0, url, sizeof(url), &auth));
    AiChat noModel = AiChat();
    CHECK(!AiChat_buildRequest(&noModel, sMessages, 1,
                               body, sizeof(body), url, sizeof(url), &auth));
    CHECK(!AiChat_complete(&bare, sMessages, 1,
                           body, sizeof(body), nullptr));

    // --- AiChat: getters/setters (Rule 24) -----------------------------------
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
    CHECK(strcmp(AiChat_getModel(&c), "glm-4.5") == 0);
    CHECK(strcmp(AiChat_getApiKey(&c), "k2") == 0);
    CHECK(strcmp(AiChat_getBaseUrl(&c), "https://override") == 0);

    // --- nous-research / Hermes support (chat-able through the directory) ----
    const AiProviderSlot *nous = AiProvider_get(dir, "nous-research");
    CHECK(nous != nullptr);
    CHECK(nous && strcmp(AiProvider_getDisplayName(dir, nous), "Nous Research") == 0);
    CHECK(nous && AiProvider_getFamily(dir, nous) == AI_PROVIDER_FAMILY_OPENAI_COMPAT);
    CHECK(nous && AiProvider_getAuth(dir, nous) == AI_PROVIDER_AUTH_BEARER);

    AiChat hermes = AiChat("hermes"); // provider = shared; peer set below
    AiChat_setPeer(&hermes, nous);
    AiChat_setApiKey(&hermes, "nous-key");
    CHECK(AiChat_buildRequest(&hermes, sMessages, 1,
                              body, sizeof(body), url, sizeof(url), &auth));
    CHECK(strcmp(url, "https://openrouter.ai/api/v1/chat/completions") == 0);
    CHECK(auth.kind == API_AUTH_BEARER);
    CHECK(strcmp(auth.credential, "nous-key") == 0);
    CHECK(strstr(body, "hermes") != nullptr);

    if (sFailures == 0) {
        printf("ai_provider_test: ALL CHECKS PASSED (%u providers)\n", total);
        return 0;
    }
    printf("ai_provider_test: %d FAILURES\n", sFailures);
    return 1;
}
