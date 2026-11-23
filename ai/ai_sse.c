#include "vex.h"
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
 * AiSse_feed, this file never touches a socket, spawns a thread, or
 * allocates. Feed parses `event:` / `data:` lines (`:` comments
 * ignored, `\r` stripped), joins multi-line `data:` payloads with
 * `\n`, and reports a `"[DONE]"` payload by latching done. Overlong
 * lines past AI_SSE_LINE_CAP flush mid-line (hostile input never
 * overflows); textOut overflow returns false with the truncation flag
 * set (Rule 35.3, never silent).
 *
 * STRUCT FIELDS (Mirroring ai/ai_sse.h, exactly this file's class):
 * ----------------------------------------------------------------------------
 *   any slotHandle;                // borrowed fanout handle (nullptr = unbound)
 *   const HavenWsSource *slotSource; // borrowed fn-table (nullptr = unbound)
 *   u64 timeoutMs;              // per-slice bound (default 100)
 *   bool cancelled;                  // cancel flag: feed degrades to false
 *   bool done;                       // true once a "[DONE]" event dispatches
 *   utf8 lineBuf[AI_SSE_LINE_CAP];   // partial line staging (no alloc)
 *   u32 lineLen;                // staged Bytes in lineBuf
 *   utf8 eventBuf[AI_SSE_EVENT_CAP]; // current event name ("message" default,
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

/** --- static helpers --------------------------------------------------------- */

;;GETTER
static inline utf8 *AiSse_eventBuf(AiSse *self)
{
    return (*self).eventBuf;
}
;;GETTER
static inline utf8 *AiSse_lineBuf(AiSse *self)
{
    return (*self).lineBuf;
}
;;SETTER
static inline fn AiSse_setLineLen(AiSse *self, u32 lineLen)
{
    (*self).lineLen = lineLen;
}
;;SETTER
static inline fn AiSse_setSlotHandle(AiSse *self, any slotHandle)
{
    (*self).slotHandle = slotHandle;
}
;;SETTER
static inline fn AiSse_setSlotSource(AiSse *self, const HavenWsSource *slotSource)
{
    (*self).slotSource = slotSource;
}

/** Restores the default SSE event name in the parser's event buffer. */
static fn setDefaultEvent(AiSse *self) {
    string dflt = "message";
    bytes n = strlen(dflt);
    memcpy(AiSse_eventBuf(self), dflt, n + 1);
}

/** Appends a payload fragment and flags truncation when the output cannot hold it. */
static bool appendText(utf8 *textOut, bytes textCap, bytes *used,
                       string s, bytes n, bool *outTruncated) {
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

/**
 * Dispatch one complete line (no trailing newline). Appends data:
 * payloads into textOut; latches done on "[DONE]". Returns false only
 * on textOut overflow (flag set), dispatch state still advances.
 */
/** Applies one parsed SSE line, appending data or updating event completion state. */
static bool dispatchLine(AiSse *self, string line, bytes lineLen,
                         utf8 *textOut, bytes textCap, bytes *used,
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
        string v = line + 6;
        bytes vlen = lineLen - 6;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        bytes take = vlen;
        if (take >= AI_SSE_EVENT_CAP)
            take = AI_SSE_EVENT_CAP - 1;
        utf8 *eventBuf = AiSse_eventBuf(self);
        memcpy(eventBuf, v, take);
        eventBuf[take] = '\0';
        return true;
    }
    if (lineLen >= 5 && memcmp(line, "data:", 5) == 0) {
        string v = line + 5;
        bytes vlen = lineLen - 5;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        if (vlen == 6 && memcmp(v, "[DONE]", 6) == 0) {
            AiSse_setDone(self, true);
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

/** CONSTRUCTORS */

/** Returns an empty SSE decoder with its default polling timeout and event name. */
AiSse AiSse_0() {
    AiSse sse;
    memset(&sse, 0, sizeof(sse));
    sse.timeoutMs = AI_SSE_POLL_BUDGET_MS;
    setDefaultEvent(&sse);
    return sse;
}

/** CORE FUNCTIONS */

/** Feeds an input byte span and emits complete SSE data events into textOut. */
bool AiSse_feed(AiSse *self, string Bytes, bytes byteLen,
                utf8 *textOut, bytes textCap, bool *outTruncated) {
    if (self == nullptr)
        return false;
    if (outTruncated)
        *outTruncated = false;
    if (AiSse_isCancelled(self))
        return false;
    if (AiSse_isDone(self))
        return false;
    if (AiSse_getTimeout(self) == 0)
        return false;
    if (textOut == nullptr || textCap == 0)
        return false;
    if (byteLen > 0 && Bytes == nullptr)
        return false;
    textOut[0] = '\0';
    bytes used = 0;
    bool hasData = false;
    bool dispatched = false;
    utf8 *lineBuf = AiSse_lineBuf(self);
    for (bytes i = 0; i < byteLen; i++) {
        utf8 c = Bytes[i];
        if (c == '\n') {
            bytes llen = AiSse_getLineLen(self);
            if (llen > 0 && lineBuf[llen - 1] == '\r')
                llen--;
            if (!dispatchLine(self, lineBuf, llen, textOut, textCap,
                              &used, &hasData, &dispatched, outTruncated))
                return false;
            AiSse_setLineLen(self, 0);
            continue;
        }
        u32 curLen = AiSse_getLineLen(self);
        if (curLen + 1 >= AI_SSE_LINE_CAP) {
            if (!dispatchLine(self, lineBuf, curLen, textOut,
                              textCap, &used, &hasData, &dispatched,
                              outTruncated))
                return false;
            curLen = 0;
            AiSse_setLineLen(self, 0);
        }
        lineBuf[curLen] = c;
        AiSse_setLineLen(self, curLen + 1);
    }
    if (dispatched && used > 0 && textOut[used - 1] == '\n')
        textOut[used - 1] = '\0';
    return dispatched;
}

/** Binds the decoder to one borrowed fan-out handle and source table. */
bool AiSse_bind(AiSse *self, any handle, const HavenWsSource *source) {
    if (self == nullptr)
        return false;
    if (handle == nullptr || source == nullptr)
        return false;
    if (AiSse_getSlotHandle(self) != nullptr)
        return false;
    AiSse_setSlotHandle(self, handle);
    AiSse_setSlotSource(self, source);
    return true;
}

/** Clears the borrowed slot binding without closing its transport. */
fn AiSse_unbind(AiSse *self) {
    if (self == nullptr)
        return;
    AiSse_setSlotHandle(self, nullptr);
    AiSse_setSlotSource(self, nullptr);
}

/** Clears stream progress and completion state while retaining binding/configuration. */
fn AiSse_reset(AiSse *self) {
    if (self == nullptr)
        return;
    AiSse_setCancelled(self, false);
    AiSse_setDone(self, false);
    AiSse_setLineLen(self, 0);
    setDefaultEvent(self);
}

/** Marks the decoder cancelled so subsequent feed calls reject input. */
fn AiSse_cancel(AiSse *self) {
    if (self == nullptr)
        return;
    AiSse_setCancelled(self, true);
}

/** SETTERS */

/** Sets the timeout value associated with the bound polling slice. */
;;SETTER
fn AiSse_setTimeout(AiSse *self, u64 timeoutMs) {
    if (self == nullptr)
        return;
    (*self).timeoutMs = timeoutMs;
}

/** Sets the decoder's cancellation state. */
;;SETTER
fn AiSse_setCancelled(AiSse *self, bool cancelled) {
    if (self == nullptr)
        return;
    (*self).cancelled = cancelled;
}

/** Sets whether the stream has reached its terminal event. */
;;SETTER
fn AiSse_setDone(AiSse *self, bool done) {
    if (self == nullptr)
        return;
    (*self).done = done;
}

/** GETTERS */

/** Returns the borrowed fan-out handle, or nullptr for an unbound decoder. */
;;GETTER
any AiSse_getSlotHandle(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotHandle;
}

/** Returns the borrowed transport table, or nullptr when unbound. */
;;GETTER
const HavenWsSource *AiSse_getSlotSource(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotSource;
}

/** Returns the configured timeout, or zero for a null receiver. */
;;GETTER
u64 AiSse_getTimeout(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).timeoutMs;
}

/** Reports whether the decoder is cancelled. */
;;GETTER
bool AiSse_isCancelled(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).cancelled;
}

/** Reports whether the terminal SSE event has been observed. */
;;GETTER
bool AiSse_isDone(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).done;
}

/** Returns the number of bytes currently buffered for an incomplete line. */
;;GETTER
u32 AiSse_getLineLen(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).lineLen;
}

/** Returns a borrowed view of the current event name. */
;;GETTER
string AiSse_getEvent(const AiSse *self) {
    return self ? (*self).eventBuf : nullptr;
}
