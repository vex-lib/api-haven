#include "api/haven_ws_fanout.h"
#include "annotation/annotation.h"

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
 * most 16 slots, each pairing a borrowed void* handle with its WsSource
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
 *     uint32_t count;                         // live rows (0..FANOUT_MAX)
 *   }
 *
 * SLOT RECORD (HavenWsSlot — Rule 3 co-location, zero behavior of its
 * own; all behavior hangs off this table class):
 * ----------------------------------------------------------------------------
 *   void *handle;                // borrowed transport handle (NULL = free)
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

/** Allocates and initializes an empty fixed-capacity fan-out registry. */
HavenWsFanout *HavenWsFanout_0(void) {
    HavenWsFanout *fanout = (HavenWsFanout*) calloc(1, sizeof(HavenWsFanout));
    if (!fanout)
        return nullptr;
    (*fanout).count = 0;
    return fanout;
}

// CORE FUNCTIONS

/** Releases the registry allocation; attached handles remain caller-owned. */
void HavenWsFanout_free(HavenWsFanout *fanout) {
    if (!fanout)
        return;
    free(fanout);
}

/** Attaches a unique borrowed handle and source table into an available slot. */
bool HavenWsFanout_attach(HavenWsFanout *fanout, void *handle,
                          const HavenWsSource *source) {
    if (!fanout || !handle || !source)
        return false;
    for (uint32_t i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &(*fanout).slots[i];
        if ((*slot).handle == handle)
            return false;
    }
    for (uint32_t i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &(*fanout).slots[i];
        if ((*slot).handle == nullptr) {
            (*slot).handle = handle;
            (*slot).source = source;
            (*fanout).count += 1;
            return true;
        }
    }
    return false;
}

/** Detaches a previously attached handle and clears its registry slot. */
bool HavenWsFanout_detach(HavenWsFanout *fanout, void *handle) {
    if (!fanout || !handle)
        return false;
    for (uint32_t i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &(*fanout).slots[i];
        if ((*slot).handle == handle) {
            (*slot).handle = nullptr;
            (*slot).source = nullptr;
            (*fanout).count -= 1;
            return true;
        }
    }
    return false;
}

/** Polls each attached source once using a slice derived from the supplied budget. */
uint32_t HavenWsFanout_pollStep(HavenWsFanout *fanout, uint64_t budgetMs) {
    if (!fanout)
        return 0;
    if (budgetMs == 0)
        return 0;
    if ((*fanout).count == 0)
        return 0;
    uint64_t slice = budgetMs / (*fanout).count;
    if (slice == 0)
        slice = 1;
    uint32_t delivered = 0;
    for (uint32_t i = 0; i < HAVEN_WS_FANOUT_MAX; i++) {
        HavenWsSlot *slot = &(*fanout).slots[i];
        if ((*slot).handle == nullptr || (*slot).source == nullptr)
            continue;
        const HavenWsSource *source = (*slot).source;
        if ((*source).poll == nullptr)
            continue;
        delivered += (*source).poll((*slot).handle, slice);
    }
    return delivered;
}

// GETTERS

/** Returns the number of currently attached handles. */
uint32_t HavenWsFanout_getCount(const HavenWsFanout *fanout) {
    if (!fanout)
        return 0;
    return (*fanout).count;
}

/** Returns the handle at index i, or nullptr for an invalid slot. */
void *HavenWsFanout_getHandle(const HavenWsFanout *fanout, uint32_t i) {
    if (!fanout)
        return nullptr;
    if (i >= HAVEN_WS_FANOUT_MAX)
        return nullptr;
    const HavenWsSlot *slot = &(*fanout).slots[i];
    return (*slot).handle;
}
