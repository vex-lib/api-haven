#include "annotation/annotation.h"
#include "app/app_provider.h"
#include "app/app_broker.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: AppBrokerTest (_test/app_broker_test.c — local-only scratch, never committed)
 * LEVEL: L2 — Behavior verification (headless; no scripts, no network)
 * ============================================================================
 * Executable proof of the src/app/ seam: the AppProvider allowlist
 * (count, at, get, resolveTarget, null-safety), the unbound-driver
 * degrade (action false, no slot consumed), a stub echo-driver success
 * (DONE + outLen), and the TIMEOUT path on driver failure. The stub
 * runs in-process — no osascript, no IPC, no sockets.
 *
 * Lives in umbrella _test/ (gitignored) so api-haven stays GitHub-clean.
 * Build: cc -std=gnu23 -Wall -Wextra -Werror -I projects/api-haven/src
 *   -I projects/vexspoke/src _test/app_broker_test.c
 *   projects/api-haven/src/app/app_provider.c
 *   projects/api-haven/src/app/app_broker.c -o /tmp/app_broker_test
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

// --- stub drivers (in-process; never touch scripts or sockets) ---------------
static int sRunCalls = 0;
static u64 sLastTimeoutMs = 0;

/* Copies the requested action into the bounded output and records successful stub dispatch. */
static bool stubEcho(void *driverCtx, const AppProviderSlot *target,
                     const char *action, const char *paramsJson,
                     bytes paramsLen, u64 timeoutMs,
                     char *outBuf, bytes outCap, bytes *outLen) {
    (void)driverCtx;
    (void)target;
    (void)paramsJson;
    (void)paramsLen;
    sRunCalls++;
    sLastTimeoutMs = timeoutMs;
    const char *reply = action ? action : "";
    bytes len = strlen(reply);
    if (len + 1 > outCap)
        return false;
    memcpy(outBuf, reply, len + 1);
    if (outLen)
        (*outLen) = len;
    return true;
}

/* Simulates a driver rejection without writing output or changing broker state. */
static bool stubFail(void *driverCtx, const AppProviderSlot *target,
                     const char *action, const char *paramsJson,
                     bytes paramsLen, u64 timeoutMs,
                     char *outBuf, bytes outCap, bytes *outLen) {
    (void)driverCtx;
    (void)target;
    (void)action;
    (void)paramsJson;
    (void)paramsLen;
    (void)timeoutMs;
    (void)outBuf;
    (void)outCap;
    (void)outLen;
    return false;
}

/* Verifies app-provider lookup and broker behavior for unbound, successful, and failed actions. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- AppProvider allowlist ----------------------------------------------
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
    CHECK(notes && strcmp(AppProvider_getBundleIdOrScheme(dir, notes),
                          "com.apple.Notes") == 0);

    const AppProviderSlot *tg = AppProvider_get(dir, "telegram");
    CHECK(tg && AppProvider_getFamily(dir, tg) == APP_PROVIDER_FAMILY_REST_WEBHOOK);
    CHECK(tg && AppProvider_getAuth(dir, tg) == APP_PROVIDER_AUTH_TOKEN);

    const AppProviderSlot *spotify = AppProvider_get(dir, "spotify-local");
    CHECK(spotify && AppProvider_getFamily(dir, spotify) == APP_PROVIDER_FAMILY_LOCAL_SOCKET);

    char target[64];
    CHECK(AppProvider_resolveTarget(dir, notes, target, sizeof(target)));
    CHECK(strcmp(target, "com.apple.Notes") == 0);
    char tiny[4];
    CHECK(!AppProvider_resolveTarget(dir, notes, tiny, sizeof(tiny)));
    CHECK(!AppProvider_resolveTarget(dir, notes, nullptr, 64));
    CHECK(!AppProvider_resolveTarget(nullptr, notes, target, sizeof(target)));

    // --- null-safety (Rule 24) ----------------------------------------------
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

    // --- unbound-driver degrade (target set, no driver: no slot consumed) ---
    AppBroker bare = AppBroker();
    AppBroker_setTarget(&bare, notes);
    CHECK(AppBroker_getTarget(&bare) == notes);
    CHECK(AppBroker_getDriver(&bare) == nullptr);
    CHECK(AppBroker_getTimeout(&bare) == 100);
    CHECK(!AppBroker_isRunning(&bare));
    char out[128];
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
    int driverCtx = 3;
    AppBroker_setDriver(&noTarget, &driverCtx, echoTable);
    CHECK(!AppBroker_action(&noTarget, "x", "{}", 2, out, sizeof(out), &jobId));

    // --- stub echo-driver success (DONE + outLen) -----------------------------
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
    CHECK(sLastTimeoutMs == 250);
    CHECK(strcmp(out, "list-notes") == 0);
    CHECK(AppBroker_getLastStatus(&b) == APP_STATUS_DONE);
    CHECK(AppBroker_getJobStatus(&b, jobId) == APP_STATUS_DONE);
    CHECK(AppBroker_poll(&b, jobId) == APP_STATUS_DONE);
    CHECK(!AppBroker_isRunning(&b));
    const AppJob *slot = AppBroker_getJobAt(&b, 0);
    CHECK(slot != nullptr);
    CHECK(slot && (*slot).outLen == 10);
    CHECK(AppBroker_getJobAt(&b, 1) == nullptr);
    CHECK(!AppBroker_cancel(&b, jobId)); // DONE slot: false
    CHECK(AppBroker_poll(&b, 999) == APP_STATUS_IDLE);
    CHECK(AppBroker_poll(nullptr, 1) == APP_STATUS_IDLE);

    // --- TIMEOUT path on driver failure ---------------------------------------
    AppDriverTable failTable;
    failTable.runActionFn = stubFail;
    AppBroker_setDriver(&b, &driverCtx, failTable);
    u32 failId = 0;
    CHECK(!AppBroker_action(&b, "doomed", "{}", 2, out, sizeof(out), &failId));
    CHECK(AppBroker_getLastStatus(&b) == APP_STATUS_TIMEOUT);
    CHECK(AppBroker_getJobStatus(&b, failId) == APP_STATUS_TIMEOUT);

    if (sFailures == 0) {
        printf("app_broker_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("app_broker_test: %d FAILURES\n", sFailures);
    return 1;
}
