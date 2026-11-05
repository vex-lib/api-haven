/* Owner test for api/rest.c (the Rest core). Proves the REST result contract
 * against a real loopback HttpServer — no accounts, no internet, deterministic:
 *   - a 2xx transfer succeeds and returns true;
 *   - 1xx/3xx/4xx/5xx transfers return false while resp keeps the status/body;
 *   - an explicitly required credential that cannot be rendered fails closed
 *     (nothing is sent);
 *   - a nullptr auth (and NONE) is an intentional public request.
 * Loopback that is blocked by the sandbox is an explicit skip, never a pass
 * (the Executable Evidence and Readiness Law).
 */
#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/auth.h"
#include "api/rest.h"
#include "net/http.h"

static int g_failures = 0;

#define CHECK(name, cond) do {                                  \
    if (cond) { printf("[rest] PASS %s\n", name); }             \
    else { printf("[rest] FAIL %s\n", name); g_failures++; }    \
} while (0)

/* Counts requests and answers "/status/NNN" with that status, else 200. */
static int g_hits;

static void statusHandler(const HttpExchange *exchange, int clientFd, void *userdata) {
    (void) userdata;
    g_hits++;
    int status = 200;
    const char *path = (*exchange).path;
    if (strncmp(path, "/status/", 8) == 0)
        status = atoi(path + 8);
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"status\":%d}", status);
    if (n < 0)
        n = 0;
    Http_respond(clientFd, status, "application/json", body, (size_t) n);
}

/* One GET into a fresh caller buffer; returns the call result and leaves resp. */
static bool getStatus(const char *url, const ApiAuth *auth, HttpResponse *resp, char *body, size_t cap) {
    memset(resp, 0, sizeof(*resp));
    (*resp).body = body;
    (*resp).bodyCap = cap;
    body[0] = '\0';
    return Rest_get(url, auth, resp);
}

int main(void) {
    printf("=== Rest core owner suite (loopback) ===\n");
    HttpServer srv = { 0 };
    int port = 0;
    if (!HttpServer_start(&srv, 0, statusHandler, nullptr, &port)) {
        fprintf(stderr, "rest_test: SKIP (loopback server unavailable)\n");
        return B_TEST_SKIP;
    }
    char url200[128], url204[128], url302[128], url400[128], url404[128], url429[128], url503[128], url500[128];
    snprintf(url200, sizeof(url200), "http://127.0.0.1:%d/status/200", port);
    snprintf(url204, sizeof(url204), "http://127.0.0.1:%d/status/204", port);
    snprintf(url302, sizeof(url302), "http://127.0.0.1:%d/status/302", port);
    snprintf(url400, sizeof(url400), "http://127.0.0.1:%d/status/400", port);
    snprintf(url404, sizeof(url404), "http://127.0.0.1:%d/status/404", port);
    snprintf(url429, sizeof(url429), "http://127.0.0.1:%d/status/429", port);
    snprintf(url503, sizeof(url503), "http://127.0.0.1:%d/status/503", port);
    snprintf(url500, sizeof(url500), "http://127.0.0.1:%d/status/500", port);

    ApiAuth none = ApiAuth_none();
    char body[512];
    HttpResponse resp;

    g_hits = 0;
    bool ok200 = getStatus(url200, &none, &resp, body, sizeof(body));
    if (g_hits == 0) {
        // No connection reached the server; the sandbox blocked loopback.
        HttpServer_stop(&srv);
        fprintf(stderr, "rest_test: SKIP (loopback blocked by sandbox)\n");
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
    CHECK("500 -> error body preserved", strstr(body, "500") != nullptr);

    // Public request via nullptr auth is allowed and reaches the server.
    int before = g_hits;
    CHECK("null auth -> sent", getStatus(url200, nullptr, &resp, body, sizeof(body)) && g_hits == before + 1);

    // A required credential that cannot be rendered must not send anything.
    ApiAuth missing = ApiAuth_bearer(nullptr);
    before = g_hits;
    CHECK("unrenderable bearer -> false", !getStatus(url200, &missing, &resp, body, sizeof(body)));
    CHECK("unrenderable bearer -> not sent", g_hits == before);

    HttpServer_stop(&srv);
    if (g_failures != 0) {
        printf("=== %d REST FAILURES ===\n", g_failures);
        return 1;
    }
    printf("=== ALL REST PASS ===\n");
    return 0;
}
