#include "annotation/annotation.h"
#include "ai/ai_sse.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: AiSseTest (src/ai/ai_sse_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; caller-fed Bytes only,
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

static int sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

static uint32_t sPollCalls = 0;

/* Records a transport poll and returns no frames for the caller-fed SSE test. */
static uint32_t fakePoll(void *handle, uint64_t budgetMs) {
    (void)handle;
    (void)budgetMs;
    sPollCalls++;
    return 0;
}

/* Supplies a successful connection callback without opening a transport. */
static bool fakeConnect(void *handle) {
    (void)handle;
    return true;
}

/* Accepts sends without side effects so the test can bind a complete source table. */
static bool fakeSend(void *handle, const uint8_t *Bytes, uint32_t len) {
    (void)handle;
    (void)Bytes;
    (void)len;
    return true;
}

/* Provides the no-op close callback for the in-process source table. */
static void fakeClose(void *handle) {
    (void)handle;
}

static const HavenWsSource sFakeSource = {
    fakeConnect,
    fakePoll,
    fakeSend,
    fakeClose,
};

/* Exercises incremental SSE parsing, slot binding, cancellation, timeout, truncation, and null guards. */
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- single event decode --------------------------------------------------
    AiSse sse = AiSse();
    CHECK(AiSse_getTimeout(&sse) == AI_SSE_POLL_BUDGET_MS);
    CHECK(!AiSse_isCancelled(&sse));
    CHECK(!AiSse_isDone(&sse));
    CHECK(AiSse_getLineLen(&sse) == 0);

    char text[512];
    bool truncated = false;
    const char *one = "event: message\ndata: hello\n\n";
    CHECK(AiSse_feed(&sse, one, strlen(one), text, sizeof(text), &truncated));
    CHECK(!truncated);
    CHECK(strcmp(text, "hello") == 0);

    // --- multi-line data joins with newline ------------------------------------
    AiSse multi = AiSse();
    const char *two = "data: line1\ndata: line2\n\n";
    CHECK(AiSse_feed(&multi, two, strlen(two), text, sizeof(text), &truncated));
    CHECK(strcmp(text, "line1\nline2") == 0);

    // --- comments ignored, \r stripped ------------------------------------------
    AiSse cr = AiSse();
    const char *three = ": keep-alive\r\ndata: hi\r\n\r\n";
    CHECK(AiSse_feed(&cr, three, strlen(three), text, sizeof(text), &truncated));
    CHECK(strcmp(text, "hi") == 0);

    // --- partial line staging across feeds --------------------------------------
    AiSse part = AiSse();
    CHECK(!AiSse_feed(&part, "data: hel", 9, text, sizeof(text), &truncated));
    CHECK(AiSse_getLineLen(&part) == 9);
    CHECK(AiSse_feed(&part, "lo\n\n", 4, text, sizeof(text), &truncated));
    CHECK(strcmp(text, "hello") == 0);

    // --- custom event name remembered --------------------------------------------
    AiSse ev = AiSse();
    const char *evBytes = "event: done\ndata: x\n\n";
    CHECK(AiSse_feed(&ev, evBytes, strlen(evBytes), text, sizeof(text), &truncated));
    const char *evName = AiSse_getEvent(&ev);
    CHECK(evName != nullptr && strcmp(evName, "done") == 0);

    // --- [DONE] latches done -----------------------------------------------------
    AiSse fin = AiSse();
    CHECK(AiSse_feed(&fin, "data: [DONE]\n\n", 14, text, sizeof(text), &truncated));
    CHECK(AiSse_isDone(&fin));
    CHECK(!AiSse_feed(&fin, "data: late\n\n", 12, text, sizeof(text), &truncated));

    // --- explicit one-slot binding ------------------------------------------------
    AiSse bound = AiSse();
    int fakeHandle = 7;
    CHECK(AiSse_getSlotHandle(&bound) == nullptr);
    CHECK(AiSse_bind(&bound, &fakeHandle, &sFakeSource));
    CHECK(AiSse_getSlotHandle(&bound) == &fakeHandle);
    CHECK(AiSse_getSlotSource(&bound) == &sFakeSource);
    CHECK(!AiSse_bind(&bound, &fakeHandle, &sFakeSource)); // rebind refused
    int otherHandle = 9;
    CHECK(!AiSse_bind(&bound, &otherHandle, &sFakeSource)); // unbind first
    AiSse_unbind(&bound);
    CHECK(AiSse_getSlotHandle(&bound) == nullptr);
    CHECK(AiSse_getSlotSource(&bound) == nullptr);
    CHECK(!AiSse_bind(&bound, nullptr, &sFakeSource));
    CHECK(!AiSse_bind(&bound, &fakeHandle, nullptr));
    CHECK(!AiSse_bind(nullptr, &fakeHandle, &sFakeSource));
    AiSse_unbind(nullptr); // null-safe no-op

    // --- cancel latch + reset ------------------------------------------------------
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

    // --- zero timeout drops ----------------------------------------------------------
    AiSse tmo = AiSse();
    AiSse_setTimeout(&tmo, 0);
    CHECK(AiSse_getTimeout(&tmo) == 0);
    CHECK(!AiSse_feed(&tmo, one, strlen(one), text, sizeof(text), &truncated));
    AiSse_setTimeout(&tmo, AI_SSE_POLL_BUDGET_MS);
    CHECK(AiSse_feed(&tmo, one, strlen(one), text, sizeof(text), &truncated));

    // --- truncation flag (Rule 35.3) ---------------------------------------------------
    AiSse trunc = AiSse();
    char tiny[4];
    CHECK(!AiSse_feed(&trunc, one, strlen(one), tiny, sizeof(tiny), &truncated));
    CHECK(truncated);
    CHECK(!AiSse_feed(&trunc, one, strlen(one), tiny, sizeof(tiny), nullptr));

    // --- nullptr / empty / wrong-shape matrix -------------------------------------------
    CHECK(!AiSse_feed(nullptr, one, strlen(one), text, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, nullptr, 3, text, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, one, strlen(one), nullptr, sizeof(text), &truncated));
    CHECK(!AiSse_feed(&sse, one, strlen(one), text, 0, &truncated));
    CHECK(!AiSse_feed(&sse, "", 0, text, sizeof(text), &truncated)); // no event
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

    // --- setters round-trip ---------------------------------------------------------------
    AiSse st = AiSse();
    AiSse_setDone(&st, true);
    CHECK(AiSse_isDone(&st));
    AiSse_setDone(&st, false);
    CHECK(!AiSse_isDone(&st));

    if (sFailures == 0) {
        printf("ai_sse_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("ai_sse_test: %d FAILURES\n", sFailures);
    return 1;
}
