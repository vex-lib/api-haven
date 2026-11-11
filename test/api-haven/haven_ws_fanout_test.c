#include "annotation/annotation.h"
#include "api/haven_ws_fanout.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: HavenWsFanoutTest (api-haven/test/haven_ws_fanout_test.c)
 * LEVEL: L2 — Behavior verification (headless; no threads, no sockets, no network)
 * ============================================================================
 * Executable proof of the HavenWsFanout class: 16-slot registry over
 * opaque transport handles and HavenWsSource fn-tables, attach/detach lifecycle,
 * duplicate rejection, table-full rejection, even-budget slicing across active
 * slots, getter invariants, and cold-seam null-safety.
 *
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

typedef struct MockTransport {
    u32 pollCount;
    u64 lastBudget;
    u32 framesToDeliver;
} MockTransport;

/* Records each assigned poll budget and returns the mock transport's queued frame count. */
static u32 mockPoll(void *handle, u64 budgetMs) {
    if (!handle)
        return 0;
    MockTransport *mock = (MockTransport*) handle;
    (*mock).pollCount++;
    (*mock).lastBudget = budgetMs;
    return (*mock).framesToDeliver;
}

/* Supplies successful connect behavior for the fanout source table. */
static bool mockConnect(void *handle) {
    (void)handle;
    return true;
}

/* Accepts outbound bytes without network activity for the mock source table. */
static bool mockSend(void *handle, const u8 *Bytes, u32 len) {
    (void)handle;
    (void)Bytes;
    (void)len;
    return true;
}

/* Supplies the no-op close callback for mock transports. */
static void mockClose(void *handle) {
    (void)handle;
}

static const HavenWsSource sMockSource = {
    mockConnect,
    mockPoll,
    mockSend,
    mockClose,
};

/* Exercises fanout attachment, fair bounded polling, slot reuse, saturation, and null safety. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- 1. Creation and Initial State ---------------------------------------
    HavenWsFanout *fanout = HavenWsFanout();
    CHECK(fanout != nullptr);
    CHECK(HavenWsFanout_getCount(fanout) == 0);
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        CHECK(HavenWsFanout_getHandle(fanout, i) == nullptr);
    }
    // Out of bounds getter
    CHECK(HavenWsFanout_getHandle(fanout, HAVEN_WS_FANOUT_MAX) == nullptr);

    // --- 2. Attach and Duplicate Rejection ------------------------------------
    MockTransport t1 = {0, 0, 2};
    MockTransport t2 = {0, 0, 5};

    CHECK(HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 1);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == &t1);

    // Duplicate handle rejection
    CHECK(!HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 1);

    // Attach second transport
    CHECK(HavenWsFanout_attach(fanout, &t2, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 2);
    CHECK(HavenWsFanout_getHandle(fanout, 1) == &t2);

    // --- 3. PollStep Budget Slicing ------------------------------------------
    // budgetMs = 10ms across 2 slots -> 5ms per slot
    u32 delivered = HavenWsFanout_pollStep(fanout, 10);
    CHECK(delivered == (2 + 5));
    CHECK(t1.pollCount == 1);
    CHECK(t1.lastBudget == 5);
    CHECK(t2.pollCount == 1);
    CHECK(t2.lastBudget == 5);

    // Tiny budget slice: budget 1ms / 2 slots -> slice 0 clamped to min 1ms
    delivered = HavenWsFanout_pollStep(fanout, 1);
    CHECK(delivered == (2 + 5));
    CHECK(t1.pollCount == 2);
    CHECK(t1.lastBudget == 1);
    CHECK(t2.pollCount == 2);
    CHECK(t2.lastBudget == 1);

    // Zero budget delivers 0 and does not poll
    delivered = HavenWsFanout_pollStep(fanout, 0);
    CHECK(delivered == 0);
    CHECK(t1.pollCount == 2);
    CHECK(t2.pollCount == 2);

    // --- 4. Detach and Slot Reuse ---------------------------------------------
    CHECK(HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_getCount(fanout) == 1);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == nullptr);
    CHECK(HavenWsFanout_getHandle(fanout, 1) == &t2);

    // Unknown handle detach fails
    CHECK(!HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_getCount(fanout) == 1);

    // Polling after detach only polls remaining active slots
    t2.framesToDeliver = 3;
    delivered = HavenWsFanout_pollStep(fanout, 10);
    CHECK(delivered == 3);
    CHECK(t1.pollCount == 2); // Unchanged
    CHECK(t2.pollCount == 3);
    CHECK(t2.lastBudget == 10); // 10 / 1 active slot = 10ms

    // Re-attach into freed slot 0
    CHECK(HavenWsFanout_attach(fanout, &t1, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == 2);
    CHECK(HavenWsFanout_getHandle(fanout, 0) == &t1);

    // --- 5. Capacity Limit (16 slots) ----------------------------------------
    MockTransport extras[HAVEN_WS_FANOUT_MAX];
    memset(extras, 0, sizeof(extras));
    // Currently 2 slots filled (0 and 1). Fill remaining 14 slots (indices 2..15)
    for (u32 i = 2; i < HAVEN_WS_FANOUT_MAX; i++) {
        CHECK(HavenWsFanout_attach(fanout, &extras[i], &sMockSource));
    }
    CHECK(HavenWsFanout_getCount(fanout) == HAVEN_WS_FANOUT_MAX);

    // 17th transport rejected
    MockTransport overflow = {0, 0, 0};
    CHECK(!HavenWsFanout_attach(fanout, &overflow, &sMockSource));
    CHECK(HavenWsFanout_getCount(fanout) == HAVEN_WS_FANOUT_MAX);

    // Detach all
    CHECK(HavenWsFanout_detach(fanout, &t1));
    CHECK(HavenWsFanout_detach(fanout, &t2));
    for (u32 i = 2; i < HAVEN_WS_FANOUT_MAX; i++) {
        CHECK(HavenWsFanout_detach(fanout, &extras[i]));
    }
    CHECK(HavenWsFanout_getCount(fanout) == 0);

    // Polling with 0 count delivers 0
    CHECK(HavenWsFanout_pollStep(fanout, 10) == 0);

    // --- 6. Cold-Seam Null-Safety --------------------------------------------
    CHECK(HavenWsFanout_getCount(nullptr) == 0);
    CHECK(HavenWsFanout_getHandle(nullptr, 0) == nullptr);
    CHECK(HavenWsFanout_pollStep(nullptr, 10) == 0);
    CHECK(!HavenWsFanout_attach(nullptr, &t1, &sMockSource));
    CHECK(!HavenWsFanout_attach(fanout, nullptr, &sMockSource));
    CHECK(!HavenWsFanout_attach(fanout, &t1, nullptr));
    CHECK(!HavenWsFanout_detach(nullptr, &t1));
    CHECK(!HavenWsFanout_detach(fanout, nullptr));

    HavenWsFanout_free(nullptr); // Safe no-op
    HavenWsFanout_free(fanout);

    if (sFailures == 0) {
        printf("haven_ws_fanout_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("haven_ws_fanout_test: %d FAILURES\n", sFailures);
    return 1;
}
