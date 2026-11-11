#include "annotation/annotation.h"
#include "ai/ai_sse.h"

#include <string.h>

;;DEFINITION
/** The incremental Server-Sent Events decoder. It exists because a token stream
 * arrives in arbitrary socket slices: the host polls a bound HavenWsFanout slot
 * and feeds whatever arrived, and this class keeps the partial line, joins
 * multi-line data: payloads and latches [DONE]. It never touches a socket,
 * spawns a thread or allocates. Overlong lines flush mid-line so hostile input
 * cannot overflow, and a full output buffer reports truncation rather than
 * dropping text silently. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiSse (ai/ai_sse)
 * caller-budgeted slot binding, zero socket/thread/alloc)
 * ============================================================================
 * Incremental Server-Sent Events decoder for AI token streams. A host
 * caller may poll one bound HavenWsFanout slot in 100ms slices
 * (HavenWsFanout_pollStep) and feeds the delivered Bytes here with
 * AiSse_feed — this file never touches a socket, spawns a thread, or
 * allocates. Feed parses `event:` / `data:` lines (`:` comments
 * ignored, `\r` stripped), joins multi-line `data:` payloads with
 * `\n`, and reports a `"[DONE]"` payload by latching done. Overlong
 * lines past AI_SSE_LINE_CAP flush mid-line (hostile input never
 * overflows); textOut overflow returns false with the truncation flag
 * set (Rule 35.3, never silent).
 *
 * STRUCT FIELDS (Mirroring ai/ai_sse.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   void *slotHandle;                // borrowed fanout handle (NULL = unbound)
 *   const HavenWsSource *slotSource; // borrowed fn-table (NULL = unbound)
 *   u64 timeoutMs;              // per-slice bound (default 100)
 *   bool cancelled;                  // cancel flag: feed degrades to false
 *   bool done;                       // true once a "[DONE]" event dispatches
 *   char lineBuf[AI_SSE_LINE_CAP];   // partial line staging (no alloc)
 *   u32 lineLen;                // staged Bytes in lineBuf
 *   char eventBuf[AI_SSE_EVENT_CAP]; // current event name ("message" default,
 *                                    // sticky until the next event: line)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value struct, no allocation):
 *   - AiSse_0()
 *
 * Core Functions:
 *   - AiSse_feed(self, Bytes, byteLen, textOut, textCap, outTruncated)
 *   - AiSse_bind(self, handle, source)
 *   - AiSse_unbind(self)
 *   - AiSse_reset(self)
 *   - AiSse_cancel(self)
 *
 * Setters:
 *   - AiSse_setTimeout(self, timeoutMs)
 *   - AiSse_setCancelled(self, cancelled)
 *   - AiSse_setDone(self, done)
 *
 * Getters:
 *   - AiSse_getSlotHandle(self)
 *   - AiSse_getSlotSource(self)
 *   - AiSse_getTimeout(self)
 *   - AiSse_isCancelled(self)
 *   - AiSse_isDone(self)
 *   - AiSse_getLineLen(self)
 *   - AiSse_getEvent(self)
 * ============================================================================
 */
;;INTENTION("fanout-slot SSE binding: AiSse borrows one HavenWsFanout slot via the existing WsSource table; this class only decodes caller-fed Bytes. No socket adapter is implemented in this workspace.")

// --- static helpers ---------------------------------------------------------

/** Restores the default SSE event name in the parser's event buffer. */
static void setDefaultEvent(AiSse *self) {
    const char *dflt = "message";
    bytes n = strlen(dflt);
    memcpy((*self).eventBuf, dflt, n + 1);
}

/** Appends a payload fragment and flags truncation when the output cannot hold it. */
static bool appendText(char *textOut, bytes textCap, bytes *used,
                       const char *s, bytes n, bool *outTruncated) {
    if (*used + n + 1 > textCap) {
        if (outTruncated)
            *outTruncated = true;
        return false;
    }
    memcpy(textOut + *used, s, n);
    *used += n;
    textOut[*used] = '\0';
    return true;
}

// Dispatch one complete line (no trailing newline). Appends data:
// payloads into textOut; latches done on "[DONE]". Returns false only
// on textOut overflow (flag set) — dispatch state still advances.
/** Applies one parsed SSE line, appending data or updating event completion state. */
static bool dispatchLine(AiSse *self, const char *line, bytes lineLen,
                         char *textOut, bytes textCap, bytes *used,
                         bool *hasData, bool *dispatched,
                         bool *outTruncated) {
    if (lineLen == 0) {
        if (*hasData) {
            *dispatched = true;
            *hasData = false;
        }
        return true;
    }
    if ((*line) == ':')
        return true;
    if (lineLen >= 6 && memcmp(line, "event:", 6) == 0) {
        const char *v = line + 6;
        bytes vlen = lineLen - 6;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        bytes take = vlen;
        if (take >= AI_SSE_EVENT_CAP)
            take = AI_SSE_EVENT_CAP - 1;
        memcpy((*self).eventBuf, v, take);
        (*self).eventBuf[take] = '\0';
        return true;
    }
    if (lineLen >= 5 && memcmp(line, "data:", 5) == 0) {
        const char *v = line + 5;
        bytes vlen = lineLen - 5;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        if (vlen == 6 && memcmp(v, "[DONE]", 6) == 0) {
            (*self).done = true;
            *hasData = true;
            return true;
        }
        if (!appendText(textOut, textCap, used, v, vlen, outTruncated))
            return false;
        if (!appendText(textOut, textCap, used, "\n", 1, outTruncated))
            return false;
        *hasData = true;
        return true;
    }
    return true;
}

// CONSTRUCTORS

/** Returns an empty SSE decoder with its default polling timeout and event name. */
AiSse AiSse_0(void) {
    AiSse sse;
    memset(&sse, 0, sizeof(sse));
    sse.timeoutMs = AI_SSE_POLL_BUDGET_MS;
    setDefaultEvent(&sse);
    return sse;
}

// CORE FUNCTIONS

/** Feeds an input byte span and emits complete SSE data events into textOut. */
bool AiSse_feed(AiSse *self, const char *Bytes, bytes byteLen,
                char *textOut, bytes textCap, bool *outTruncated) {
    if (self == nullptr)
        return false;
    if (outTruncated)
        *outTruncated = false;
    if ((*self).cancelled)
        return false;
    if ((*self).done)
        return false;
    if ((*self).timeoutMs == 0)
        return false;
    if (textOut == nullptr || textCap == 0)
        return false;
    if (byteLen > 0 && Bytes == nullptr)
        return false;
    textOut[0] = '\0';
    bytes used = 0;
    bool hasData = false;
    bool dispatched = false;
    for (bytes i = 0; i < byteLen; i++) {
        char c = Bytes[i];
        if (c == '\n') {
            bytes llen = (*self).lineLen;
            if (llen > 0 && (*self).lineBuf[llen - 1] == '\r')
                llen--;
            if (!dispatchLine(self, (*self).lineBuf, llen, textOut, textCap,
                              &used, &hasData, &dispatched, outTruncated))
                return false;
            (*self).lineLen = 0;
            continue;
        }
        if ((*self).lineLen + 1 >= AI_SSE_LINE_CAP) {
            if (!dispatchLine(self, (*self).lineBuf, (*self).lineLen, textOut,
                              textCap, &used, &hasData, &dispatched,
                              outTruncated))
                return false;
            (*self).lineLen = 0;
        }
        (*self).lineBuf[(*self).lineLen] = c;
        (*self).lineLen += 1;
    }
    if (dispatched && used > 0 && textOut[used - 1] == '\n')
        textOut[used - 1] = '\0';
    return dispatched;
}

/** Binds the decoder to one borrowed fan-out handle and source table. */
bool AiSse_bind(AiSse *self, void *handle, const HavenWsSource *source) {
    if (self == nullptr)
        return false;
    if (handle == nullptr || source == nullptr)
        return false;
    if ((*self).slotHandle != nullptr)
        return false;
    (*self).slotHandle = handle;
    (*self).slotSource = source;
    return true;
}

/** Clears the borrowed slot binding without closing its transport. */
void AiSse_unbind(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).slotHandle = nullptr;
    (*self).slotSource = nullptr;
}

/** Clears stream progress and completion state while retaining binding/configuration. */
void AiSse_reset(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = false;
    (*self).done = false;
    (*self).lineLen = 0;
    setDefaultEvent(self);
}

/** Marks the decoder cancelled so subsequent feed calls reject input. */
void AiSse_cancel(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = true;
}

// SETTERS

/** Sets the timeout value associated with the bound polling slice. */
void AiSse_setTimeout(AiSse *self, u64 timeoutMs) {
    if (self == nullptr)
        return;
    (*self).timeoutMs = timeoutMs;
}

/** Sets the decoder's cancellation state. */
void AiSse_setCancelled(AiSse *self, bool cancelled) {
    if (self == nullptr)
        return;
    (*self).cancelled = cancelled;
}

/** Sets whether the stream has reached its terminal event. */
void AiSse_setDone(AiSse *self, bool done) {
    if (self == nullptr)
        return;
    (*self).done = done;
}

// GETTERS

/** Returns the borrowed fan-out handle, or nullptr for an unbound decoder. */
void *AiSse_getSlotHandle(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotHandle;
}

/** Returns the borrowed transport table, or nullptr when unbound. */
const HavenWsSource *AiSse_getSlotSource(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotSource;
}

/** Returns the configured timeout, or zero for a null receiver. */
u64 AiSse_getTimeout(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).timeoutMs;
}

/** Reports whether the decoder is cancelled. */
bool AiSse_isCancelled(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).cancelled;
}

/** Reports whether the terminal SSE event has been observed. */
bool AiSse_isDone(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).done;
}

/** Returns the number of bytes currently buffered for an incomplete line. */
u32 AiSse_getLineLen(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).lineLen;
}

/** Returns a borrowed view of the current event name. */
const char *AiSse_getEvent(const AiSse *self) {
    return self ? (*self).eventBuf : nullptr;
}
