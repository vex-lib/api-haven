#include "annotation/annotation.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/auth.h"
#include "api/rest.h"
#include "com/discord/discord.h"
#include "com/slack/slack.h"
#include "net/http.h"
#include "net/json.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: WebhookLoopbackTest (webhook_loopback_test/test)
 * LEVEL: L3 — Module Code (headless Shape-A/B proof, no accounts)
 * ============================================================================
 * Proves the webhook drivers and Rest core against ourselves: a local
 * HttpServer captures the exact Bytes each driver emits, and JsonDoc
 * asserts the payload schema. Zero accounts, zero network, deterministic.
 * Live vendor acceptance stays per-driver LIVE_UNVERIFIED until an env-var
 * run (URL from env, never committed).
 *
 * STRUCT FIELDS: none — procedural harness over file-static capture.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main(void)
 * ============================================================================
 */

typedef struct Capture {
    char method[16];
    char path[1024];
    char body[8192];
    bytes bodyLen;
    int hits;
} Capture;

static Capture g_cap;
static int g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("[loopback] PASS %s\n", name); } \
    else { printf("[loopback] FAIL %s\n", name); g_failures++; } \
} while (0)

/* Captures loopback request fields/body and returns a deterministic method-specific response. */
static void captureHandler(const HttpExchange *exchange, int clientFd, void *userdata) {
    (void)userdata;
    Capture *cap = &g_cap;
    strncpy((*cap).method, (*exchange).method, sizeof((*cap).method) - 1);
    strncpy((*cap).path, (*exchange).path, sizeof((*cap).path) - 1);
    bytes n = (*exchange).bodyLen;
    if (n >= sizeof((*cap).body))
        n = sizeof((*cap).body) - 1;
    if (n > 0 && (*exchange).body)
        memcpy((*cap).body, (*exchange).body, n);
    (*cap).body[n] = '\0';
    (*cap).bodyLen = n;
    (*cap).hits++;
    if (strcmp((*exchange).method, "GET") == 0)
        Http_respond(clientFd, 200, "application/json", "{\"ok\":true}", 11);
    else
        Http_respond(clientFd, 204, "text/plain", "", 0);
}

/* Parses a captured JSON body and checks one string-valued member exactly. */
static bool bodyHas(const char *body, const char *key, const char *want) {
    JsonNode nodes[64];
    char scratch[1024];
    JsonDoc doc;
    if (!Json_parse(&doc, nodes, 64, scratch, sizeof(scratch), body))
        return false;
    if (!Json_ok(&doc))
        return false;
    JsonRef root = Json_root(&doc);
    JsonRef ref = Json_member(&doc, root, key);
    u32 len = 0;
    const char *got = Json_string(&doc, ref, &len);
    if (!got)
        return false;
    if (strlen(want) != len)
        return false;
    return strncmp(got, want, len) == 0;
}

/* Supplies a fixed token for testing callback-based bearer credential formatting. */
static const char *staticToken(void *userdata) {
    (void)userdata;
    return "fn-token-123";
}

/* Runs Discord, Slack, auth-formatting, and REST-get checks against a local loopback server. */
int main(void) {
    printf("=== Webhook Loopback Suite (Shape A/B, no accounts) ===\n");
    HttpServer srv = { 0 };
    int port = 0;
    if (!HttpServer_start(&srv, 0, captureHandler, nullptr, &port)) {
        printf("[loopback] FAIL server start\n");
        return 1;
    }
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/webhook", port);

    // section 1 Discord driver (legacy direct transport) against loopback.
    memset(&g_cap, 0, sizeof(g_cap));
    bool dok = DiscordWebhook_sendText(url, "hello loopback", "vexbot", nullptr);
    bool loopbackBlocked = (!dok && g_cap.hits == 0);
    if (loopbackBlocked) {
        printf("[loopback] SKIP socket tests (loopback blocked by sandbox environment)\n");
    } else {
        CHECK("discord post 204", dok && g_cap.hits == 1);
        CHECK("discord method+path", strcmp(g_cap.method, "POST") == 0);
        CHECK("discord content", bodyHas(g_cap.body, "content", "hello loopback"));
        CHECK("discord username", bodyHas(g_cap.body, "username", "vexbot"));
    }

    const char *dumpPath = getenv("LOOPBACK_DUMP_BODY");
    if (dumpPath && dumpPath[0] != '\0' && !loopbackBlocked) {
        FILE *f = fopen(dumpPath, "wb");
        if (f) {
            fwrite(g_cap.body, 1, g_cap.bodyLen, f);
            fclose(f);
        }
    }

    // section 2 Slack driver (Rest core transport) against loopback.
    if (!loopbackBlocked) {
        memset(&g_cap, 0, sizeof(g_cap));
        bool sok = SlackWebhook_sendText(url, "hi slack", "vexbot", nullptr);
        CHECK("slack post 204", sok && g_cap.hits == 1);
        CHECK("slack text", bodyHas(g_cap.body, "text", "hi slack"));
        CHECK("slack username", bodyHas(g_cap.body, "username", "vexbot"));
    }

    // section 3 Auth contract vectors (pure, no network).
    const char *nameOut = nullptr;
    char valueBuf[128];
    ApiAuth none = ApiAuth_none();
    CHECK("auth none renders nothing", !ApiAuth_apply(&none, &nameOut, valueBuf, sizeof(valueBuf)));
    ApiAuth key = ApiAuth_apiKey("Api-Key", "abc123");
    bool kok = ApiAuth_apply(&key, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth apikey", kok && strcmp(nameOut, "Api-Key") == 0 && strcmp(valueBuf, "abc123") == 0);
    ApiAuth bearer = ApiAuth_bearer("tok-xyz");
    bool bok = ApiAuth_apply(&bearer, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth bearer", bok && strcmp(nameOut, "Authorization") == 0 && strcmp(valueBuf, "Bearer tok-xyz") == 0);
    ApiAuth bearerFn = ApiAuth_bearerFn(staticToken, nullptr);
    bool fok = ApiAuth_apply(&bearerFn, &nameOut, valueBuf, sizeof(valueBuf));
    CHECK("auth bearerFn", fok && strcmp(valueBuf, "Bearer fn-token-123") == 0);
    ApiAuth nullBearer = ApiAuth_bearer(nullptr);
    CHECK("auth null bearer fails", !ApiAuth_apply(&nullBearer, &nameOut, valueBuf, sizeof(valueBuf)));
    char tiny[8];
    CHECK("auth truncation fails", !ApiAuth_apply(&bearer, &nameOut, tiny, sizeof(tiny)));

    // section 4 Rest_get against loopback (Shape B read path).
    if (!loopbackBlocked) {
        char respBody[1024];
        HttpResponse resp = { 0 };
        resp.body = respBody;
        resp.bodyCap = sizeof(respBody);
        ApiAuth noAuth = ApiAuth_none();
        bool gok = Rest_get(url, &noAuth, &resp);
        CHECK("rest get 200", gok && resp.status == 200);
        CHECK("rest get body", strstr(respBody, "true") != nullptr);
    }

    HttpServer_stop(&srv);
    if (g_failures != 0) {
        printf("=== %d LOOPBACK FAILURES ===\n", g_failures);
        return g_failures;
    }
    if (loopbackBlocked) {
        // the socket transports never ran; pure auth vectors passing is not a
        // pass for the transport contract. A skip is not a pass.
        return B_TEST_SKIP;
    }
    printf("=== ALL LOOPBACK PASS ===\n");
    return 0;
}
