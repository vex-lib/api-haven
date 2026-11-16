#include "vex.h"
#include "api/haven_ws_fanout.h"

#include <stdlib.h>

;;DEFINITION
/** A fixed fan-out table over opaque socket handles. It exists so R2 can drive
 * several sockets from one tick without owning a socket API: at most 16 slots,
 * each pairing a borrowed handle with its WsSource callback table, advanced by
 * HavenWsFanout_pollStep under a millisecond budget. The callbacks are the
 * external seam — no socket adapter lives in this workspace — and FrameInbox is
 * a bounded byte inbox, not a transport. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: HavenWsFanout (api/haven_ws_fanout)
 * ============================================================================
 * A fixed fan-out table over opaque socket handles for the R2 layer: at
 * most 16 slots, each pairing a borrowed any handle with its WsSource
 * fn-table {connect, poll, send, close}. A host caller drives delivery by
 * calling HavenWsFanout_pollStep per tick with a millisecond budget —
 * zero pthread_*, zero socket syscalls in this file. The supplied callbacks
 * are an external seam; no socket adapter is implemented in this workspace.
 * FrameInbox is a bounded byte inbox, not a socket transport. This registry only
 * fans out over callback slots.
 *
 * STRUCT FIELDS (Mirroring api/haven_ws_fanout.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   HavenWsFanout {
 *     HavenWsSlot slots[HAVEN_WS_FANOUT_MAX]; // 16 fixed rows, no alloc
 *     u32 count;                         // live rows (0..FANOUT_MAX)
 *   }
 *
 * SLOT RECORD (HavenWsSlot — Rule 3 co-location, zero behavior of its
 * own; all behavior hangs off this table class):
 * ----------------------------------------------------------------------------
 *   any handle;                // borrowed transport handle (NULL = free)
 *   const HavenWsSource *source; // fn-table bound at attach (NULL = free)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - HavenWsFanout()  : HavenWsFanout_0()
 *
 * Core Functions:
 *   - HavenWsFanout_free(fanout)
 *   - HavenWsFanout_attach(fanout, handle, source)
 *   - HavenWsFanout_detach(fanout, handle)
 *   - HavenWsFanout_pollStep(fanout, budgetMs)
 *
 * Getters (Rule 24; null-safe. Setters omitted — rows managed by
 * attach/detach, provider-table waiver):
 *   - HavenWsFanout_getCount(fanout)
 *   - HavenWsFanout_getHandle(fanout, i)
 * ============================================================================
 */
;;INTENTION("host-driven fan-out registry with no threads: pollStep fans a millisecond budget over opaque WsSource slots; actual socket adapters are outside this implementation")
;;DRAFT

// haven_ws_fanout.c — HavenWsFanout port. Fixed table, borrowed handles,
// host-driven poll slices. No threads, no sockets, no allocation steady-state
// (the registry block is calloc-owned shim state, freed by _free before
// Memory_freeAll per Rule 26).

// CONSTRUCTORS

;;SETTER
static inline fn HavenWsFanout_setCount(HavenWsFanout *self, u32 count) { (*self).count = count; }
;;GETTER
static inline HavenWsSlot *HavenWsFanout_slots(HavenWsFanout *self) { return (*self).slots; }
;;GETTER
static inline const HavenWsSlot *HavenWsFanout_slotsConst(const HavenWsFanout *self) { return (*self).slots; }
;;GETTER
static inline any HavenWsSlot_handle(const HavenWsSlot *self) { return (*self).handle; }
;;SETTER
static inline fn HavenWsSlot_setHandle(HavenWsSlot *self, any handle) { (*self).handle = handle; }
;;GETTER
static inline const HavenWsSource *HavenWsSlot_source(const HavenWsSlot *self) { return (*self).source; }
;;SETTER
static inline fn HavenWsSlot_setSource(HavenWsSlot *self, const HavenWsSource *source) { (*self).source = source; }
;;GETTER
static inline u32 (*HavenWsSource_poll(const HavenWsSource *self))(any, u64) { return (*self).poll; }

/** Allocates and initializes an empty fixed-capacity fan-out registry. */
HavenWsFanout *HavenWsFanout_0() {
    HavenWsFanout *fanout = (HavenWsFanout*) calloc(1, sizeof(HavenWsFanout));
    if (!fanout)
        return nullptr;
    HavenWsFanout_setCount(fanout, 0);
    return fanout;
}

// CORE FUNCTIONS

/** Releases the registry allocation; attached handles remain caller-owned. */
fn HavenWsFanout_free(HavenWsFanout *fanout) {
    if (!fanout)
        return;
    free(fanout);
}

/** Attaches a unique borrowed handle and source table into an available slot. */
bool HavenWsFanout_attach(HavenWsFanout *fanout, any handle,
                          const HavenWsSource *source) {
    if (!fanout || !handle || !source)
        return false;
    HavenWsSlot *slots = HavenWsFanout_slots(fanout);
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &slots[i];
        if (HavenWsSlot_handle(slot) == handle)
            return false;
    }
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &slots[i];
        if (HavenWsSlot_handle(slot) == nullptr) {
            HavenWsSlot_setHandle(slot, handle);
            HavenWsSlot_setSource(slot, source);
            HavenWsFanout_setCount(fanout, HavenWsFanout_getCount(fanout) + 1);
            return true;
        }
    }
    return false;
}

/** Detaches a previously attached handle and clears its registry slot. */
bool HavenWsFanout_detach(HavenWsFanout *fanout, any handle) {
    if (!fanout || !handle)
        return false;
    HavenWsSlot *slots = HavenWsFanout_slots(fanout);
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &slots[i];
        if (HavenWsSlot_handle(slot) == handle) {
            HavenWsSlot_setHandle(slot, nullptr);
            HavenWsSlot_setSource(slot, nullptr);
            HavenWsFanout_setCount(fanout, HavenWsFanout_getCount(fanout) - 1);
            return true;
        }
    }
    return false;
}

/** Polls each attached source once using a slice derived from the supplied budget. */
u32 HavenWsFanout_pollStep(HavenWsFanout *fanout, u64 budgetMs) {
    if (!fanout)
        return 0;
    if (budgetMs == 0)
        return 0;
    u32 count = HavenWsFanout_getCount(fanout);
    if (count == 0)
        return 0;
    u64 slice = budgetMs / count;
    if (slice == 0)
        slice = 1;
    u32 delivered = 0;
    HavenWsSlot *slots = HavenWsFanout_slots(fanout);
    for (u32 i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &slots[i];
        any handle = HavenWsSlot_handle(slot);
        const HavenWsSource *source = HavenWsSlot_source(slot);
        if (handle == nullptr || source == nullptr)
            continue;
        u32 (*pollFn)(any, u64) = HavenWsSource_poll(source);
        if (pollFn == nullptr)
            continue;
        delivered += pollFn(handle, slice);
    }
    return delivered;
}

// GETTERS

/** Returns the number of currently attached handles. */
;;GETTER
u32 HavenWsFanout_getCount(const HavenWsFanout *fanout) {
    if (!fanout)
        return 0;
    return (*fanout).count;
}

/** Returns the handle at index i, or nullptr for an invalid slot. */
any HavenWsFanout_getHandle(const HavenWsFanout *fanout, u32 i) {
    if (!fanout)
        return nullptr;
    if (i >= HAVEN_WS_FANOUT_MAX)
        return nullptr;
    const HavenWsSlot *slot = &HavenWsFanout_slotsConst(fanout)[i];
    return HavenWsSlot_handle(slot);
}
