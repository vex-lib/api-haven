#ifndef AI_SSE_H
#define AI_SSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api/haven_ws_fanout.h"
#include "c23/constructor.h"

// ai/ai_sse.h — the AiSse class: pure incremental SSE stream decoder
// (L2 behavior, R2 api-haven).
//
// Caller-fed Bytes only: a host caller may poll one bound HavenWsFanout slot
// (100ms slices via HavenWsFanout_pollStep) and feeds the delivered Bytes
// here with AiSse_feed. No sockets, no threads, no allocation, no logging
// on the feed path (Rule 35 hot-minimal: one nullptr entry guard only).
// Truncation is never silent: textOut overflow returns false and sets the
// flag (Rule 35.3). Cold-seam matrix (nullptr/empty/overflow/truncation/
// cancelled/timeout) lives in ai/tests/ai_sse_test.c only.

// Per-slice poll bound (Rule 27): one caller-supplied slice never exceeds 100ms.
#define AI_SSE_POLL_BUDGET_MS 100u
// Longest buffered partial line (SSE comment churn included).
#define AI_SSE_LINE_CAP 2048u
// Longest remembered event name ("message", "done", ...).
#define AI_SSE_EVENT_CAP 64u

typedef struct AiSse {
    void *slotHandle;                    // borrowed fanout handle (NULL = unbound)
    const HavenWsSource *slotSource;     // borrowed fn-table (NULL = unbound)
    uint64_t timeoutMs;                  // per-slice bound (default AI_SSE_POLL_BUDGET_MS)
    bool cancelled;                      // cancel flag: feed degrades to false
    bool done;                           // true once a "[DONE]" event dispatches
    char lineBuf[AI_SSE_LINE_CAP];       // partial line staging (no alloc)
    uint32_t lineLen;                    // staged Bytes in lineBuf
    char eventBuf[AI_SSE_EVENT_CAP];     // current event name ("message" default)
} AiSse;

// --- Constructor (value struct, no allocation) ---
AiSse AiSse_0(void);
#define AiSse(...) CONSTRUCTOR_DISPATCH(AiSse, __VA_ARGS__)

// --- Core functions ---
// Feed caller-owned Bytes; decoded data: payloads append into textOut
// (NUL-terminated, events separated by "\n"). Returns true when at least
// one event dispatched. Drop-degrade false on NULL self/Bytes(out,cap),
// cancelled, "[DONE]"-already-seen, or textCap overflow (flag set).
// outTruncated is last (dest-last, Rule 9); NULL flag = degrade silently.
bool AiSse_feed(AiSse *self, const char *Bytes, size_t byteLen,
                char *textOut, size_t textCap, bool *outTruncated);
// Explicit one-slot binding over the existing WsSource table (Rule 33
// canonical move): borrows handle + table, never closes/frees (detach
// via AiSse_unbind before the fanout detaches, per Rule 26).
bool AiSse_bind(AiSse *self, void *handle, const HavenWsSource *source);
void AiSse_unbind(AiSse *self);
// Drop staged state (line/event/done) without unbinding the slot.
void AiSse_reset(AiSse *self);
// Latch the cancel flag (feed degrades to false until reset).
void AiSse_cancel(AiSse *self);

// --- Setters ---
void AiSse_setTimeout(AiSse *self, uint64_t timeoutMs);
void AiSse_setCancelled(AiSse *self, bool cancelled);
void AiSse_setDone(AiSse *self, bool done);

// --- Getters (Rule 24, null-safe) ---
void *AiSse_getSlotHandle(const AiSse *self);
const HavenWsSource *AiSse_getSlotSource(const AiSse *self);
uint64_t AiSse_getTimeout(const AiSse *self);
bool AiSse_isCancelled(const AiSse *self);
bool AiSse_isDone(const AiSse *self);
uint32_t AiSse_getLineLen(const AiSse *self);
// Borrowed current event name, valid until the next feed/reset or destruction.
const char *AiSse_getEvent(const AiSse *self);

#endif
